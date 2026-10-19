// リスト: 表形式。列の見出しで並べ替え（並べ替え中は末端の課題だけをフラットに並べる）。
import React, { useEffect, useRef, useState } from "react";
import { core, setUI, visibleRows, visibleLeaves, fmtD, num, setField } from "../core/core.js";
import { ui, useAll, selectOnly, toggleSel, rangeTo, openMenu, curFilter } from "../core/state.js";
import { openDetail, rowMenu, startEdit } from "../core/actions.js";
import { Icon, TypeIcon, Avatar, StatusPill, FloatChip } from "../components/ui.jsx";
import { NameCell, AsgCell, StatusCell, DurCell, FloatCell } from "../components/cells.jsx";
import { rowH } from "./Gantt.jsx";

const COLS = [
  { k: "name", t: "課題", w: "minmax(260px,1fr)", get: (id) => core.T.name[id] },
  { k: "type", t: "種別", w: "86px", get: (id) => core.T.byId[id].type },
  { k: "status", t: "状況", w: "96px", get: (id) => ({ todo: 0, prog: 1, done: 2 })[core.T.cat[id]] },
  { k: "asg", t: "担当", w: "110px", get: (id) => core.T.byId[id].assignee || "￿" },
  { k: "prio", t: "優先度", w: "80px", get: (id) => ["highest", "high", "medium", "low", "lowest"].indexOf(String(core.T.byId[id].prio).toLowerCase()) },
  { k: "start", t: "開始（計画）", w: "104px", get: (id) => core.R[id].asapStart },
  { k: "end", t: "完了（計画）", w: "104px", get: (id) => core.R[id].asapFinish },
  { k: "dur", t: "期間", w: "64px", r: 1, get: (id) => core.R[id].weeks || 0 },
  { k: "float", t: "余裕", w: "72px", r: 1, get: (id) => (core.R[id].done ? 1e9 : core.R[id].float) },
  { k: "dl", t: "期限", w: "104px", get: (id) => (core.T.ownDl[id] != null ? core.T.ownDl[id] : 1e9) },
];
const short = (n) => fmtD(n).replace(/^\d+\//, "");

export function List() {
  useAll();
  const { P, T, R } = core, u = P.ui, [sort, setSort] = useState(null);
  const wrap = useRef(null);
  useEffect(() => {
    if (!ui.cursor || !wrap.current) return;
    const el = wrap.current.querySelector('[data-rid="' + CSS.escape(ui.cursor) + '"]'); if (el) el.scrollIntoView({ block: "nearest" });
  }, [ui.scrollSig]);
  if (!T || T.error) return <div className="empty">{T ? T.error : ""}</div>;
  if (!P.issues.length) return <div className="empty first"><h2>まだ課題がありません</h2><p>ガントかリストで課題を追加すると、ここに並びます。</p></div>;
  const filter = curFilter();
  let rows;
  if (sort) {
    const c = COLS.find((x) => x.k === sort.k), ids = visibleLeaves(filter);
    const cmp = (a, b) => { const x = c.get(a), y = c.get(b); const r = typeof x === "string" ? x.localeCompare(y, "ja") : x - y; return (r || ids.indexOf(a) - ids.indexOf(b)) * sort.d; };
    rows = ids.slice().sort(cmp).map((id) => ({ id, depth: 0, isPar: false, open: false, flat: true }));
  } else rows = visibleRows(filter);
  ui.order = rows.map((r) => r.id);
  const tpl = COLS.map((c) => c.w).join(" ");
  const toggleSort = (k) => setSort((s) => (!s || s.k !== k ? { k, d: 1 } : s.d === 1 ? { k, d: -1 } : null));
  const RH = rowH();

  return (
    <div className="list" ref={wrap} role="table" aria-label="課題の一覧" style={{ "--rh": RH + "px" }}>
      <div className="l-head" role="row" style={{ gridTemplateColumns: tpl }}>
        {COLS.map((c) => (
          <button key={c.k} type="button" role="columnheader" className={"lh" + (c.r ? " r" : "") + (sort && sort.k === c.k ? " on" : "")} onClick={() => toggleSort(c.k)} aria-sort={sort && sort.k === c.k ? (sort.d === 1 ? "ascending" : "descending") : "none"}>
            {c.t}{sort && sort.k === c.k && <Icon n={sort.d === 1 ? "up" : "down"} size={12} />}
          </button>
        ))}
      </div>
      {sort && <div className="l-note">並べ替え中は、末端の課題だけをフラットに表示します。見出しをもう一度押すと解除します。</div>}
      {rows.map((row) => {
        const { id, depth, isPar, open } = row, o = T.byId[id], r = R[id], sel = ui.sel.indexOf(id) >= 0;
        return (
          <div key={id} className={"l-row" + (isPar ? " par" : "") + (sel ? " sel" : "") + (ui.cursor === id ? " cur" : "")} data-rid={id} role="row" style={{ gridTemplateColumns: tpl, height: RH }}
            onClick={(e) => { if (e.shiftKey) rangeTo(id); else if (e.ctrlKey || e.metaKey) toggleSel(id); else selectOnly(id); }} onDoubleClick={() => openDetail(id)}
            onContextMenu={(e) => { e.preventDefault(); if (ui.sel.indexOf(id) < 0) selectOnly(id); openMenu(e.clientX, e.clientY, rowMenu(id)); }}>
            <div className="c"><NameCell id={id} depth={depth} isPar={isPar} open={open} onToggle={() => setUI({ collapsed: { ...u.collapsed, [id]: open } })} /></div>
            <div className="c sm muted">{o.type}</div>
            <div className="c"><StatusCell id={id} /></div>
            <div className="c"><AsgCell id={id} /></div>
            <div className="c sm muted">{o.prio}</div>
            <div className="c mono sm">{short(r.asapStart)}</div>
            <div className="c mono sm">{short(r.asapFinish)}</div>
            <div className="c c-r"><DurCell id={id} /></div>
            <div className="c c-r"><FloatCell id={id} /></div>
            <div className="c mono sm">{T.ownDl[id] != null ? short(T.ownDl[id]) : <span className="ph">—</span>}</div>
          </div>
        );
      })}
      {!rows.length && <div className="empty small-empty">条件に合う課題がありません。</div>}
    </div>
  );
}
