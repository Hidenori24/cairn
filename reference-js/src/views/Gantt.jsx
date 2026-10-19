// ガント: 左に課題の表（列は幅を変えられる）、右に時間軸のバー。依存の矢印、余裕のハッチ、ドラッグでの日程変更。
import React, { useEffect, useLayoutEffect, useMemo, useRef, useState } from "react";
import { core, setUI, setPref, visibleRows, rootIndexMap, setEarliest, setDuration, relocate, workdaysIn, fmtD, fmtShort, num, wk, setField } from "../core/core.js";
import { ui, setUi, useAll, selectOnly, toggleSel, rangeTo, openMenu, openModal, curFilter } from "../core/state.js";
import { rowMenu, openDetail, addRootIssue, startEdit } from "../core/actions.js";
import { Icon, Btn, TypeIcon } from "../components/ui.jsx";
import { NameCell, AsgCell, StatusCell, DurCell, FloatCell } from "../components/cells.jsx";

export const ZOOM_PX = { day: 22, week: 6, month: 2.4 };
export const COLW0 = { asg: 84, status: 92, dur: 52, float: 62 };
const GRIP = 18, MIN = { name: 160, asg: 48, status: 64, dur: 40, float: 48 };
export const rowH = () => (core.prefs.density === "compact" ? 26 : 30);
const PALETTE = 8;
const hash = (s) => { let h = 0; for (let i = 0; i < s.length; i++) h = (h * 31 + s.charCodeAt(i)) >>> 0; return h; };

/* ---- ツールチップ（DOMを直接書き換えて軽く動かす） ---- */
function useTip() {
  const ref = useRef(null);
  const show = (e, lines) => {
    const t = ref.current; if (!t) return;
    t.textContent = ""; lines.forEach((l, i) => { const d = document.createElement("div"); if (i === 0) d.className = "tt"; d.textContent = l; t.append(d); });
    t.style.display = "block";
    const w = t.offsetWidth, h = t.offsetHeight;
    t.style.left = Math.max(8, Math.min(innerWidth - w - 8, e.clientX + 14)) + "px"; t.style.top = Math.max(8, Math.min(innerHeight - h - 8, e.clientY + 16)) + "px";
  };
  const hide = () => { if (ref.current) ref.current.style.display = "none"; };
  return { ref, show, hide };
}
function tipLines(id) {
  const { T, R } = core, o = T.byId[id], r = R[id];
  const L = [T.did[id] + "  " + T.name[id]];
  L.push("計画 " + fmtD(r.asapStart) + " 〜 " + fmtD(r.asapFinish) + (r.leaf ? "（" + num(r.weeks) + "週）" : ""));
  if (!r.done) L.push("最遅 " + fmtD(r.alapStart) + " 〜 " + fmtD(r.alapFinish));
  L.push(r.done ? "完了" : "余裕 " + (r.float < 0 ? "−" + wk(r.float) + "週（遅れ）" : r.float + "営業日"));
  if (T.ownDl[id] != null) L.push("期限 " + fmtD(T.ownDl[id]));
  if (o.assignee) L.push("担当 " + o.assignee);
  if (r.waited) L.push("担当者の別作業待ち");
  return L;
}

/* ---- 幅のドラッグ変更 ---- */
function useColW() {
  const P = core.P, [live, setLive] = useState(null);
  const base = { name: P.ui.nameW || 300, ...COLW0, ...(P.ui.colW || {}) };
  const w = { ...base, ...(live || {}) };
  const start = (key) => (e) => {
    e.preventDefault(); e.stopPropagation();
    const x0 = e.clientX, w0 = w[key], el = e.currentTarget;
    try { el.setPointerCapture(e.pointerId); } catch (er) {}
    const mv = (ev) => setLive({ [key]: Math.max(MIN[key], Math.round(w0 + ev.clientX - x0)) });
    const up = (ev) => {
      el.removeEventListener("pointermove", mv); el.removeEventListener("pointerup", up); el.removeEventListener("pointercancel", up);
      const nw = Math.max(MIN[key], Math.round(w0 + ev.clientX - x0)); setLive(null);
      if (key === "name") setUI({ nameW: nw }); else setUI({ colW: { ...(core.P.ui.colW || {}), [key]: nw } });
    };
    el.addEventListener("pointermove", mv); el.addEventListener("pointerup", up); el.addEventListener("pointercancel", up);
  };
  return { w, start };
}

export function Gantt() {
  useAll();
  const { P, T, R, cal } = core, u = P.ui, RH = rowH();
  const wrap = useRef(null), tip = useTip();
  const [cw, setCw] = useState(1200), [drop, setDrop] = useState(null);
  const dragId = useRef(null);
  const { w: colW, start: startResize } = useColW();

  useLayoutEffect(() => {
    const el = wrap.current; if (!el) return;
    const f = () => setCw(el.clientWidth); f();
    if (typeof ResizeObserver !== "undefined") { const ro = new ResizeObserver(f); ro.observe(el); return () => ro.disconnect(); }
  }, []);

  const rows = T && !T.error ? visibleRows(curFilter()) : [];
  ui.order = rows.map((r) => r.id);
  const cols = u.cols || {};
  const LW = GRIP + colW.name + (cols.asg ? colW.asg : 0) + (cols.status ? colW.status : 0) + (cols.dur ? colW.dur : 0) + (cols.float ? colW.float : 0);

  // 時間軸の範囲
  const geom = useMemo(() => {
    if (!T || T.error || !R) return null;
    const base = Sched.parse(P.base);
    let lo = base, hi = base + 120;
    T.leafOrder.forEach((id) => { const r = R[id]; lo = Math.min(lo, r.asapStart); hi = Math.max(hi, r.asapFinish + 21, r.alapFinish + 21); if (T.dl[id] != null) hi = Math.max(hi, T.dl[id] + 21); });
    const rs = lo - ((Sched.dow(lo) + 6) % 7) - 7;
    return { base, lo, hi, rs };
  }, [core.ver, P.base]); // eslint-disable-line
  const zoom = u.zoom;
  const px = !geom ? 6 : zoom === "all" ? Math.max(0.9, Math.min(12, (cw - LW - 24) / (geom.hi - geom.rs + 1))) : (ZOOM_PX[zoom] || 6);
  const HEAD = zoom === "day" || zoom === "week" ? 46 : 30;

  // 初期位置・拡大率変更時に基準日が見える位置へ
  useLayoutEffect(() => {
    const el = wrap.current; if (!el || !geom) return;
    el.scrollLeft = zoom === "all" ? 0 : Math.max(0, (geom.base - geom.rs) * px - 90);
  }, [P.id, zoom]); // eslint-disable-line

  // 選択行を見える位置へ
  useEffect(() => {
    if (!ui.cursor || !wrap.current) return;
    const el = wrap.current.querySelector('[data-rid="' + CSS.escape(ui.cursor) + '"]'); if (el) el.scrollIntoView({ block: "nearest" });
  }, [ui.scrollSig]);

  if (!T || T.error) return <div className="empty"><Icon n="warn" size={28} /><p>{T ? T.error : "読み込めません"}</p></div>;
  if (!P.issues.length) return (
    <div className="empty first">
      <div className="empty-ic"><Icon n="gantt" size={30} /></div>
      <h2>まだ課題がありません</h2>
      <p>最初の課題を追加するか、JIRAのCSVを取り込んでください。</p>
      <div className="empty-act"><Btn variant="pri" icon="plus" onClick={() => addRootIssue()}>最初の課題を追加</Btn><Btn icon="upload" onClick={() => openModal("import")}>CSVを取り込む</Btn></div>
    </div>
  );

  const { base, rs, hi } = geom, re = hi;
  const W = Math.max(1, Math.round((re - rs + 1) * px)), X = (n) => (n - rs) * px;
  const ri = rootIndexMap();
  const idx = {}; rows.forEach((r, i) => { idx[r.id] = i; });
  const colorOf = (id) => {
    if (u.colorBy === "asg") { const a = T.byId[id].assignee; return a ? hash(a) % PALETTE : 0; }
    return (ri[T.rootOf[id]] || 0) % PALETTE;
  };

  /* --- 軸 --- */
  const months = [], weeks = [], days = [];
  for (let d = rs; d <= re; d++) {
    const dt = new Date(d * 86400000);
    if (dt.getUTCDate() === 1 || d === rs) months.push({ d, y: dt.getUTCFullYear(), m: dt.getUTCMonth() + 1 });
    if (zoom === "day") days.push({ d, dt, dow: dt.getUTCDay(), n: dt.getUTCDate() });
  }
  let lastLabelX = -1e9;   // 狭いときは、ラベルどうしが重ならない間隔（58px以上）で間引く
  const monthLabels = months.map((m) => ({ ...m, x: X(m.d) })).filter((m) => { if (m.x - lastLabelX < 58) return false; lastLabelX = m.x; return true; });
  const wstep = px * 7 >= 40 ? 1 : px * 7 >= 26 ? 2 : 3;
  if (zoom === "week") for (let n = rs, i = 0; n <= re; n += 7, i++) if (i % wstep === 0) { const dt = new Date(n * 86400000); weeks.push({ d: n, t: (dt.getUTCMonth() + 1) + "/" + dt.getUTCDate() }); }
  const hol = Object.keys(cal.hol).map(Number).filter((n) => n >= rs && n <= re && Sched.dow(n) >= 1 && Sched.dow(n) <= 5);

  /* --- 選択中に関係する行 --- */
  const rel = {};
  const cur = ui.cursor && T.byId[ui.cursor] ? ui.cursor : null;
  const nearest = (x) => { while (x && idx[x] == null) x = T.parentOf[x]; return x || null; };
  if (cur) {
    const mark = (x) => { const v = nearest(x); if (v) rel[v] = true; };
    (T.leaves[cur] || []).forEach((l) => { (T.preds[l] || []).forEach(mark); (T.succs[l] || []).forEach(mark); });
  }

  /* --- 依存の矢印 --- */
  const links = [], seen = {};
  T.leafOrder.forEach((l) => {
    (T.preds[l] || []).forEach((p) => {
      const a = nearest(p), b = nearest(l); if (!a || !b || a === b) return;
      const k = a + ">" + b; if (seen[k]) return; seen[k] = 1; links.push([a, b]);
    });
  });
  const curRows = {}; if (cur) { const v = nearest(cur); if (v) curRows[v] = true; }
  const endX = (id) => (T.kids[id].length ? X(R[id].asapFinish + 1) : T.kind[id] === "ms" ? X(R[id].asapFinish) + px / 2 + 7 : X(R[id].asapFinish + 1));
  const startX = (id) => (!T.kids[id].length && T.kind[id] === "ms" ? X(R[id].asapFinish) + px / 2 - 7 : X(R[id].asapStart));
  const paths = links.map(([a, b]) => {
    const x1 = endX(a), x2 = startX(b), y1 = idx[a] * RH + RH / 2, y2 = idx[b] * RH + RH / 2;
    let d;
    if (x2 >= x1 + 12) d = "M" + x1 + " " + y1 + "H" + (x1 + 6) + "V" + y2 + "H" + x2;
    else { const ym = y1 + (y2 > y1 ? 1 : -1) * RH / 2; d = "M" + x1 + " " + y1 + "H" + (x1 + 6) + "V" + ym + "H" + (x2 - 6) + "V" + y2 + "H" + x2; }
    const hot = curRows[a] || curRows[b], rb = R[b];
    const risk = rb.done ? "" : rb.float < 0 ? "late" : rb.float === 0 ? "tight" : "";
    return { d, k: a + ">" + b, cls: "lk" + (hot ? " hot" : "") + (risk ? " " + risk : "") + (cur && !hot ? " dim" : ""), mk: hot ? "hot" : risk || "n" };
  });
  paths.sort((p, q) => (p.cls.indexOf("hot") >= 0) - (q.cls.indexOf("hot") >= 0));

  /* --- 行の操作 --- */
  const onRowClick = (e, id) => {
    if (e.shiftKey) rangeTo(id); else if (e.ctrlKey || e.metaKey) toggleSel(id); else selectOnly(id);
    wrap.current && wrap.current.focus({ preventScroll: true });
  };
  const toggle = (id, open) => setUI({ collapsed: { ...u.collapsed, [id]: open } });
  const dragOver = (e, id) => {
    if (!dragId.current) return;
    e.preventDefault();
    const r = e.currentTarget.getBoundingClientRect(), y = (e.clientY - r.top) / r.height;
    const where = y < 0.28 ? "before" : y > 0.72 ? "after" : "into";
    if (!drop || drop.id !== id || drop.where !== where) setDrop({ id, where });
  };
  const onDrop = (e, id) => {
    e.preventDefault(); const from = dragId.current; dragId.current = null; const d = drop; setDrop(null);
    if (from && d) relocate(from, id, d.where);
  };

  /* --- バーのドラッグ --- */
  const startDrag = (e, id, mode) => {
    if (e.button != null && e.button !== 0) return;
    e.preventDefault(); e.stopPropagation();
    const bar = e.currentTarget.closest(".bar"), r = R[id], x0 = e.clientX, left0 = parseFloat(bar.style.left), w0 = parseFloat(bar.style.width);
    let dd = 0, moved = false; const tgt = e.currentTarget;
    try { tgt.setPointerCapture(e.pointerId); } catch (er) {}
    const label = () => (mode === "move" ? "着手 " + fmtD(r.asapStart + dd) : (() => { const days = Math.max(1, workdaysIn(r.asapStart, r.asapFinish + dd)); return "期間 " + num(days / 5) + "週（" + days + "営業日）"; })());
    const mv = (ev) => {
      dd = Math.round((ev.clientX - x0) / px); if (dd) moved = true;
      if (mode === "move") bar.style.left = left0 + dd * px + "px"; else bar.style.width = Math.max(w0 + dd * px, px) + "px";
      tip.show(ev, [label()]);
    };
    const up = () => {
      tgt.removeEventListener("pointermove", mv); tgt.removeEventListener("pointerup", up); tgt.removeEventListener("pointercancel", up);
      try { tgt.releasePointerCapture(e.pointerId); } catch (er) {}
      tip.hide();
      if (!moved || !dd) { bar.style.left = left0 + "px"; bar.style.width = w0 + "px"; return; }
      if (mode === "move") {
        let ns = r.asapStart + dd; ns = dd > 0 ? cal.workday(ns - 1, 1) : cal.lastWork(ns);
        if (ns === r.asapStart) { bar.style.left = left0 + "px"; return; }
        setEarliest(id, ns);
      } else {
        let nf = r.asapFinish + dd; nf = dd > 0 ? cal.workday(nf - 1, 1) : cal.lastWork(nf);
        const days = Math.max(1, workdaysIn(r.asapStart, nf));
        if (days === r.days) { bar.style.width = w0 + "px"; return; }
        setDuration(id, days / 5);
      }
      // 反映されなかった場合に備えて、描画の更新を待たず元の位置へ（成功時は再描画で上書きされる）
      requestAnimationFrame(() => { if (bar.isConnected) { bar.style.left = ""; bar.style.width = ""; } });
    };
    tgt.addEventListener("pointermove", mv); tgt.addEventListener("pointerup", up); tgt.addEventListener("pointercancel", up);
  };

  /* --- 1行 --- */
  const renderRow = (row, i) => {
    const { id, depth, isPar, open } = row, o = T.byId[id], r = R[id];
    const risk = r.done ? "done" : r.float < 0 ? "late" : r.float <= 10 ? "tight" : "ok";
    const selected = ui.sel.indexOf(id) >= 0;
    const cls = "g-row" + (isPar ? " par" : "") + (depth === 0 ? " d0" : "") + (selected ? " sel" : "") + (ui.cursor === id ? " cur" : "") + (rel[id] ? " rel" : "") +
      (drop && drop.id === id ? " drop-" + drop.where : "");
    const bc = u.colorBy === "float" ? "var(--" + (risk === "ok" ? "accent" : risk === "done" ? "c0" : risk) + ")" : "var(--c" + colorOf(id) + ")";
    let track = null;
    const dl = T.ownDl[id];
    if (isPar) {
      const x0 = X(r.asapStart), x1 = X(r.asapFinish + 1), xl = X(Math.max(r.alapFinish, r.asapFinish) + 1);
      track = <>
        {!r.done && xl > x1 && <div className={"sum-slack " + risk} style={{ left: x1, width: xl - x1 }} />}
        <div className={"sum " + (r.done ? "done" : risk)} style={{ left: x0, width: Math.max(x1 - x0, 3), "--bc": bc }} onMouseEnter={(e) => tip.show(e, tipLines(id))} onMouseMove={(e) => tip.show(e, tipLines(id))} onMouseLeave={tip.hide} />
      </>;
    } else if (T.kind[id] === "ms") {
      track = <div className={"msd " + (r.done ? "done" : risk)} style={{ left: X(r.asapFinish) + px / 2 - 7, "--bc": bc }} onMouseEnter={(e) => tip.show(e, tipLines(id))} onMouseLeave={tip.hide} />;
    } else {
      const bw = Math.max((r.asapFinish - r.asapStart + 1) * px, 3), mv = !r.done && !r.pinned;
      track = <>
        {!r.done && r.alapFinish > r.asapFinish && <div className={"slack " + risk} style={{ left: X(r.asapFinish + 1), width: (r.alapFinish - r.asapFinish) * px }} />}
        {!r.done && r.alapFinish < r.asapFinish && <div className="over" style={{ left: X(r.alapFinish + 1), width: (r.asapFinish - r.alapFinish) * px }} />}
        <div className={"bar " + risk + (!r.done && r.float === 0 ? " crit" : "") + (r.prog ? " prog" : "") + (r.waited ? " wait" : "") + (mv ? " mv" : "")} data-bar={id} style={{ left: X(r.asapStart), width: bw, "--bc": bc }}
          onPointerDown={mv ? (e) => { if (e.target.classList.contains("hdl")) return; startDrag(e, id, "move"); } : undefined}
          onMouseEnter={(e) => tip.show(e, tipLines(id))} onMouseLeave={tip.hide}>
          {bw > 34 ? (r.done ? <Icon n="check" size={12} strokeWidth={3} /> : num(r.weeks) + "w") : null}
          {mv && <span className="hdl" title="ドラッグして期間を変更" onPointerDown={(e) => startDrag(e, id, "resize")} />}
        </div>
      </>;
    }
    return (
      <div key={id} className={cls} data-rid={id} style={{ height: RH }} role="row" aria-selected={selected}
        onClick={(e) => onRowClick(e, id)} onDoubleClick={() => openDetail(id)}
        onContextMenu={(e) => { e.preventDefault(); if (ui.sel.indexOf(id) < 0) selectOnly(id); openMenu(e.clientX, e.clientY, rowMenu(id)); }}
        onDragOver={(e) => dragOver(e, id)} onDragLeave={() => setDrop((d) => (d && d.id === id ? null : d))} onDrop={(e) => onDrop(e, id)}>
        <div className="g-left" style={{ width: LW }}>
          <span className="grip" draggable onDragStart={(e) => { dragId.current = id; e.dataTransfer.setData("text/plain", id); e.dataTransfer.effectAllowed = "move"; }} onDragEnd={() => { dragId.current = null; setDrop(null); }} title="ドラッグして並べ替え・階層を変更"><Icon n="grip" size={12} /></span>
          <div className="c c-name" style={{ width: colW.name }}><NameCell id={id} depth={depth} isPar={isPar} open={open} onToggle={() => toggle(id, open)} /></div>
          {cols.asg && <div className="c" style={{ width: colW.asg }}><AsgCell id={id} /></div>}
          {cols.status && <div className="c" style={{ width: colW.status }}><StatusCell id={id} /></div>}
          {cols.dur && <div className="c c-r" style={{ width: colW.dur }}><DurCell id={id} /></div>}
          {cols.float && <div className="c c-r" style={{ width: colW.float }}><FloatCell id={id} /></div>}
        </div>
        <div className="g-track" style={{ width: W }}>
          {track}
          {dl != null && <div className="dlm" style={{ left: X(dl) + px / 2 - 6 }} title={"期限 " + fmtD(dl)} />}
        </div>
      </div>
    );
  };

  const head = (key, label, extra) => (
    <div className={"hc" + (extra ? " " + extra : "")} style={{ width: key === "name" ? colW.name : colW[key] }}>
      <span>{label}</span><span className="rsz" onPointerDown={startResize(key)} role="separator" aria-label={label + "の幅"} />
    </div>
  );
  const bodyH = rows.length * RH;
  const hasWeekPattern = px * 7 >= 18;

  return (
    <>
    <div className="gantt" ref={wrap} tabIndex={0} role="treegrid" aria-label="ガントチャート" style={{ "--lw": LW + "px", "--rh": RH + "px", "--head": HEAD + "px" }}
      onContextMenu={(e) => { if (e.target === e.currentTarget) e.preventDefault(); }}>
      <div className="g-inner" style={{ width: LW + W }}>
        <div className="g-head" style={{ height: HEAD }}>
          <div className="g-corner" style={{ width: LW }}>
            <span style={{ width: GRIP }} />
            {head("name", "課題")}
            {cols.asg && head("asg", "担当")}
            {cols.status && head("status", "状況")}
            {cols.dur && head("dur", "期間", "r")}
            {cols.float && head("float", "余裕", "r")}
          </div>
          <div className="g-axis" style={{ width: W }}>
            <div className="ax-m" style={{ height: zoom === "day" || zoom === "week" ? 22 : HEAD }}>
              {monthLabels.map((m) => <span key={m.d} className="ax-ml" style={{ left: m.x }}>{m.y}/{m.m}</span>)}
              {months.map((m) => <i key={m.d} className="ax-tick" style={{ left: X(m.d) }} />)}
            </div>
            {zoom === "day" && <div className="ax-d">{days.map((d) => <span key={d.d} className={"ax-dl" + (d.dow === 0 || d.dow === 6 ? " we" : "")} style={{ left: X(d.d), width: px }}>{d.n}</span>)}</div>}
            {zoom === "week" && <div className="ax-d">{weeks.map((w) => <span key={w.d} className="ax-wl" style={{ left: X(w.d) }}>{w.t}</span>)}</div>}
            <div className="ax-base" style={{ left: X(base) }}><span>基準日</span></div>
          </div>
        </div>
        <div className="g-body" style={{ height: bodyH }}>
          <div className={"g-bg" + (hasWeekPattern && zoom !== "day" ? " wk" : "")} style={{ left: LW, width: W, "--wp": px * 7 + "px" }}>
            {zoom === "day" && days.filter((d) => d.dow === 0 || d.dow === 6).map((d) => <i key={d.d} className="we" style={{ left: X(d.d), width: px }} />)}
            {hol.map((n) => <i key={n} className="hol" style={{ left: X(n), width: px }} />)}
            <i className="bl" style={{ left: X(base) - 1 }} />
          </div>
          <svg className="g-links" style={{ left: LW }} width={W} height={bodyH} aria-hidden="true">
            <defs>
              {["n", "hot", "tight", "late"].map((k) => <marker key={k} id={"ah-" + k} viewBox="0 0 8 8" refX="7" refY="4" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0.8 7 4 0 7.2z" className={"ah " + k} /></marker>)}
            </defs>
            {paths.map((p) => <path key={p.k} d={p.d} className={p.cls} markerEnd={"url(#ah-" + p.mk + ")"} />)}
          </svg>
          {rows.map(renderRow)}
          {!rows.length && <div className="empty small-empty" style={{ position: "sticky", left: 0, width: "min(100%,520px)" }}>条件に合う課題がありません。</div>}
        </div>
      </div>
    </div>
    <div className="tip" ref={tip.ref} />
    {!core.prefs.hint && (
        <div className="hint-bar" role="note">
          <Icon n="help" size={15} />
          <span><b>ダブルクリック</b>で名前を編集 ／ <b>N</b> で行を追加 ／ <b>Tab</b> で階層を下げる ／ バーを<b>ドラッグ</b>で日程変更 ／ <b>Ctrl+K</b> でコマンド</span>
          <button type="button" className="btn ghost sm" onClick={() => setPref("hint", true)}>閉じる</button>
        </div>
      )}
    </>
  );
}

/* ---- ガント用のツールバー部分 ---- */
export function scrollToBase() {
  const el = document.querySelector(".gantt"); if (!el || !core.P) return;
  const px = ZOOM_PX[core.P.ui.zoom]; if (!px) { el.scrollLeft = 0; return; }
  // 基準日の位置は Gantt の幾何と同じ式で求める（lo はバーの最小日）
  const base = el.querySelector(".ax-base"); if (base) el.scrollLeft = Math.max(0, base.offsetLeft - 90);
}
