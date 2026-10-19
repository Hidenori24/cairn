// アプリ全体: 上部バー、ツールバー、作業領域、右の詳細パネル、キーボード操作
import React, { useEffect, useRef, useState } from "react";
import { core, undo, redo, setUI, setPref, setProject, setRes, summary, rootIndexMap, floatText, fmtD, readFile, openProject, projectList, setUI as setUI2, fsGrant } from "./core/core.js";
import { ui, setUi, useAll, selectOnly, moveCursor, clearSel, pruneSel, openModal, openMenu, closeMenu, closeModal } from "./core/state.js";
import * as A from "./core/actions.js";
import { Icon, Btn, MenuButton, Menu, Popover, Toasts, FloatChip, useOutside } from "./components/ui.jsx";
import { Gantt, scrollToBase } from "./views/Gantt.jsx";
import { Board } from "./views/Board.jsx";
import { List } from "./views/List.jsx";
import { Goals } from "./views/Goals.jsx";
import { Drawer } from "./panels/Drawer.jsx";
import { ModalHost } from "./panels/Dialogs.jsx";
import { Palette } from "./panels/Palette.jsx";

const HOST_THEME = document.documentElement.getAttribute("data-theme");

/* ---------- 上部バー ---------- */
function ProjectSwitcher() {
  const [open, setOpen] = useState(false), ref = useRef(null), P = core.P;
  const list = projectList();
  const items = list.map((p) => ({ label: p.name + (p.key ? "  [" + p.key + "]" : ""), checked: p.id === P.id, onClick: () => { if (p.id !== P.id) openProject(p.id); } }))
    .concat([{ sep: true }, { label: "新しいプロジェクト", icon: "plus", onClick: () => openModal("newProject") }, { label: "名前・キー・削除", icon: "pencil", onClick: () => openModal("editProject") },
      { sep: true }, { label: "CSVを取り込む", icon: "upload", onClick: () => openModal("import") }, { label: "書き出し（JIRA用CSV・共有JSON）", icon: "download", onClick: () => openModal("export") }]);
  return (
    <>
      <button type="button" ref={ref} className="proj" aria-haspopup="menu" aria-expanded={open} onClick={() => setOpen((v) => !v)} title="プロジェクトを切り替え">
        <span className="proj-ic"><Icon n="gantt" size={15} strokeWidth={2.4} /></span>
        <span className="proj-n">{P.name}</span>{P.key && <span className="proj-k">{P.key}</span>}<Icon n="down" size={14} />
      </button>
      {open && <Menu anchor={ref.current} items={items} onClose={() => setOpen(false)} width={300} />}
    </>
  );
}
function ViewTabs() {
  const v = core.P.ui.view;
  return (
    <div className="vtabs" role="tablist" aria-label="表示">
      {[["gantt", "ガント", "gantt"], ["board", "ボード", "board"], ["list", "リスト", "list"], ["goals", "ギャップ", "target"]].map(([k, l, ic]) => (
        <button key={k} type="button" role="tab" aria-selected={v === k} className={v === k ? "on" : ""} onClick={() => setUI({ view: k })}><Icon n={ic} size={15} /><span>{l}</span></button>
      ))}
    </div>
  );
}
function Search() {
  const ref = useRef(null);
  useEffect(() => { window.__focusSearch = () => ref.current && ref.current.focus(); }, []);
  return (
    <div className="search">
      <Icon n="search" size={15} />
      <input ref={ref} value={ui.q} onChange={(e) => setUi({ q: e.target.value })} placeholder="件名・ID・ラベルで絞り込み  ( / )" aria-label="絞り込み"
        onKeyDown={(e) => { if (e.key === "Escape") { setUi({ q: "" }); e.target.blur(); } }} />
      {ui.q && <button type="button" className="btn ghost icon-only xs" aria-label="クリア" onClick={() => setUi({ q: "" })}><Icon n="x" size={13} /></button>}
    </div>
  );
}
function Chips() {
  const s = summary(), f = ui.flt;
  const chip = (k, cls, label, n) => <button type="button" key={k} className={"schip " + cls + (f === k ? " on" : "")} aria-pressed={f === k} onClick={() => setUi({ flt: f === k ? "" : k })} title="クリックでこの条件だけ表示"><b>{n}</b><span>{label}</span></button>;
  return (
    <div className="chips" aria-label="全体の状況">
      {chip("late", s.late ? "late" : "zero", "遅れ", s.late)}
      {chip("tight", s.tight ? "tight" : "zero", "余裕小", s.tight)}
      <span className="schip done" title="完了した作業 / 全体"><b>{s.done}</b><span>/ {s.total} 完了</span></span>
      <GoalsButton />
    </div>
  );
}
function GoalsButton() {
  const [open, setOpen] = useState(false), ref = useRef(null);
  const { T, R, P } = core, ri = rootIndexMap();
  const roots = T && !T.error ? T.roots.filter((r) => T.kids[r].length || Engine.isEpic(T.byId[r])) : [];
  if (!roots.length) return null;
  return (
    <>
      <button type="button" ref={ref} className="btn ghost sm" onClick={() => setOpen((v) => !v)} aria-haspopup="true" aria-expanded={open} title="ルート課題ごとの余裕と、表示のオン・オフ"><Icon n="target" size={14} /><span>ゴール</span></button>
      {open && (
        <Popover anchor={ref.current} onClose={() => setOpen(false)} width={380} className="goals">
          <div className="goals-h">ルート課題ごとの余裕</div>
          {roots.map((r) => {
            const x = R[r], k = x.done ? "done" : x.float < 0 ? "late" : x.float <= 10 ? "tight" : "ok", dl = T.ownDl[r];
            return (
              <div key={r} className="goal">
                <label className="chk" title="ガント・ボードに表示"><input type="checkbox" checked={!P.ui.hide[r]} onChange={(e) => setUI({ hide: { ...P.ui.hide, [r]: !e.target.checked } })} /></label>
                <i className="dot" style={{ background: "var(--c" + ((ri[r] || 0) % 8) + ")" }} />
                <button type="button" className="gn" onClick={() => { setOpen(false); selectOnly(r, { scroll: true }); }}>{T.name[r]}</button>
                <span className="muted sm mono">{x.nDone}/{x.n}</span>
                <FloatChip r={x} />
                {dl != null && <span className="muted sm mono" title="期限">〜{fmtD(dl).replace(/^\d+\//, "")}</span>}
              </div>
            );
          })}
        </Popover>
      )}
    </>
  );
}
function MoreMenu() {
  const prefs = core.prefs;
  return (
    <MenuButton variant="ghost" icon="more" title="その他" align="right" items={() => [
      { label: "プロジェクト設定", icon: "sliders", onClick: () => openModal("settings") },
      { label: "担当者の負荷", icon: "user", onClick: () => openModal("workload") },
      { label: "読み込み時の注意", icon: "warn", onClick: () => openModal("warnings") },
      { sep: true },
      { header: "テーマ" }, ...[["auto", "自動"], ["light", "ライト"], ["dark", "ダーク"]].map(([k, l]) => ({ label: l, checked: prefs.theme === k, onClick: () => setPref("theme", k) })),
      { header: "密度" }, ...[["normal", "標準"], ["compact", "コンパクト"]].map(([k, l]) => ({ label: l, checked: prefs.density === k, onClick: () => setPref("density", k) })),
      { sep: true },
      { label: "ショートカット一覧", icon: "help", kbd: "?", onClick: () => openModal("help") },
    ]} />
  );
}
/* ---------- 保存先フォルダの状態 ---------- */
const hhmm = (t) => { const d = new Date(t), p = (n) => String(n).padStart(2, "0"); return p(d.getHours()) + ":" + p(d.getMinutes()); };
function SyncChip() {
  const f = core.fs;
  if (!f.supported) return null;
  let cls = "off", label = "ブラウザ内のみ", title = "保存先はこのブラウザの中だけです。クリックして、CSVを書き込むフォルダを選べます";
  if (f.handle) {
    if (f.state === "need") { cls = "warn"; label = "許可が必要"; title = "フォルダ「" + f.name + "」への保存が止まっています。クリックして許可してください"; }
    else if (f.state === "error") { cls = "err"; label = "保存できません"; title = f.msg; }
    else if (f.state === "saving") { cls = "busy"; label = "保存中…"; title = "フォルダ「" + f.name + "」に書き込んでいます"; }
    else { cls = "ok"; label = f.name + (f.saved ? "  " + hhmm(f.saved) + " 保存" : ""); title = "フォルダ「" + f.name + "」に自動で保存しています。クリックで詳細"; }
  }
  return (
    <button type="button" className={"syncchip " + cls} title={title} onClick={() => (f.handle && f.state === "need" ? fsGrant() : openModal("folder"))}>
      <Icon n="folder" size={14} /><span>{label}</span>
    </button>
  );
}
function TopBar() {
  const canU = core.undoS.length > 0, canR = core.redoS.length > 0;
  return (
    <header className="topbar">
      <ProjectSwitcher /><span className="vsep" />
      <ViewTabs />
      <Search />
      <Chips />
      <span style={{ flex: 1 }} />
      <SyncChip />
      <button type="button" className="btn ghost cmdk" onClick={() => setUi({ palette: true })} title="コマンドパレット"><Icon n="cmd" size={14} /><span>Ctrl+K</span></button>
      <Btn variant="ghost" icon="undo" title="元に戻す (Ctrl+Z)" disabled={!canU} onClick={undo} />
      <Btn variant="ghost" icon="redo" title="やり直す (Ctrl+Shift+Z)" disabled={!canR} onClick={redo} />
      <Btn variant="pri" icon="plus" onClick={() => A.addRootIssue()} title="最上位の課題を追加">課題</Btn>
      <MoreMenu />
    </header>
  );
}

/* ---------- ツールバー ---------- */
function ToolSelect({ value, onChange, options, label, icon }) {
  return (
    <label className="tsel" title={label}>
      {icon && <Icon n={icon} size={14} />}
      <select value={value} aria-label={label} onChange={(e) => onChange(e.target.value)}>{options.map(([v, l]) => <option key={v} value={v}>{l}</option>)}</select>
    </label>
  );
}
function Seg({ value, onChange, options, label }) {
  return <div className="seg" role="group" aria-label={label}>{options.map(([v, l]) => <button key={v} type="button" aria-pressed={value === v} className={value === v ? "on" : ""} onClick={() => onChange(v)}>{l}</button>)}</div>;
}
function Toolbar() {
  const { P, T } = core, u = P.ui, view = u.view;
  const names = {}; P.issues.forEach((o) => { if (o.assignee) names[o.assignee] = (names[o.assignee] || 0) + 1; });
  const asgOpts = [["", "全員"], ...Object.keys(names).sort((a, b) => a.localeCompare(b, "ja")).map((n) => [n, n]), ["\u0001", "未割当"], ["\u0000", "担当者あり"]];
  const warns = ((T && T.warns) || []).length + ((P.loadWarns) || []).length;
  const scns = (T && T.scenarios) || ["標準"];
  return (
    <div className="toolbar">
      <ToolSelect icon="user" label="担当者で絞り込み" value={ui.asg} onChange={(v) => setUi({ asg: v })} options={asgOpts} />
      <ToolSelect icon="filter" label="状況で絞り込み" value={ui.st} onChange={(v) => setUi({ st: v })} options={[["", "状況: すべて"], ["open", "未完了"], ["todo", "To Do"], ["prog", "進行中"], ["done", "完了"]]} />
      <ToolSelect icon="flag" label="優先度で絞り込み" value={ui.pr} onChange={(v) => setUi({ pr: v })} options={[["", "優先度: すべて"], ["highest", "Highest"], ["high", "High"], ["medium", "Medium"], ["low", "Low"], ["lowest", "Lowest"]]} />
      {view === "gantt" && <>
        <Seg label="拡大率" value={u.zoom} onChange={(v) => setUI({ zoom: v })} options={[["day", "日"], ["week", "週"], ["month", "月"], ["all", "全体"]]} />
        <ToolSelect label="バーの色分け" value={u.colorBy} onChange={(v) => setUI({ colorBy: v })} options={[["root", "色: Epic"], ["float", "色: 余裕"], ["asg", "色: 担当者"]]} />
        <ToolSelect label="表示する階層" value={String(u.level || 0)} onChange={(v) => setUI({ level: +v })} options={[["0", "階層: すべて"], ["1", "1階層まで"], ["2", "2階層まで"], ["3", "3階層まで"], ["4", "4階層まで"]]} />
        <MenuButton size="sm" variant="ghost" icon="folder" items={[{ label: "すべて開く", onClick: () => A.expandAll(true) }, { label: "すべて閉じる", onClick: () => A.expandAll(false) }, { label: "2階層目まで開く", onClick: () => A.collapseTo(2) }, { label: "3階層目まで開く", onClick: () => A.collapseTo(3) }]}>展開</MenuButton>
        <Btn size="sm" variant="ghost" icon="target" onClick={scrollToBase} title="基準日の位置へ">基準日へ</Btn>
      </>}
      {view === "board" && <ToolSelect label="スイムレーン" value={u.lane || "none"} onChange={(v) => setUI({ lane: v })} options={[["none", "レーン: なし"], ["asg", "レーン: 担当者"], ["root", "レーン: ルート課題"]]} />}
      {view === "list" && <MenuButton size="sm" variant="ghost" icon="folder" items={[{ label: "すべて開く", onClick: () => A.expandAll(true) }, { label: "すべて閉じる", onClick: () => A.expandAll(false) }]}>展開</MenuButton>}
      {(ui.q || ui.asg || ui.flt || ui.st || ui.pr) && <Btn size="sm" variant="ghost" icon="x" onClick={() => setUi({ q: "", asg: "", flt: "", st: "", pr: "" })}>絞り込み解除{ui.flt ? "（" + ({ late: "遅れ", tight: "余裕小", done: "完了", open: "未完了" }[ui.flt]) + "）" : ""}</Btn>}
      <span style={{ flex: 1 }} />
      {warns > 0 && <Btn size="sm" variant="ghost" icon="warn" onClick={() => openModal("warnings")} title="読み込み時の注意">注意 {warns}</Btn>}
      <label className="tsel date" title="基準日（ここから前に進める計画の起点）"><Icon n="calendar" size={14} /><span className="lb">基準日</span>
        <input type="date" className="mono" aria-label="基準日" value={P.base} onChange={(e) => e.target.value && setProject({ base: e.target.value }, true)} /></label>
      <ToolSelect icon="flag" label="シナリオ" value={P.scn} onChange={(v) => setProject({ scn: v }, true)} options={scns.map((s) => [s, s === "標準" ? "シナリオ: 標準" : s])} />
      <button type="button" className={"btn sm toggle" + (P.res.on ? " on" : "")} aria-pressed={P.res.on} onClick={() => setRes({ on: !P.res.on })} title="担当者ごとに同時作業を制限して計画します（設定で件数を変更）"><Icon n="user" size={14} /><span>工数制約 {P.res.on ? "ON" : "OFF"}</span></button>
    </div>
  );
}

/* ---------- 複数選択のまとめて編集 ---------- */
function BulkBar() {
  const n = ui.sel.length; if (n < 2) return null;
  const [asg, setAsg] = useState("");
  return (
    <div className="bulk" role="toolbar" aria-label="まとめて編集">
      <b>{n}件を選択</b>
      <MenuButton size="sm" icon="check" items={["To Do", "In Progress", "Done"].map((s) => ({ label: s, onClick: () => A.setStatusSel(s) }))}>状況</MenuButton>
      <MenuButton size="sm" icon="flag" items={["Highest", "High", "Medium", "Low", "Lowest"].map((s) => ({ label: s, onClick: () => A.setPrioSel(s) }))}>優先度</MenuButton>
      <input className="inp" list="dl-assignees" placeholder="担当者を設定…" aria-label="担当者をまとめて設定" value={asg} onChange={(e) => setAsg(e.target.value)} onKeyDown={(e) => { if (e.key === "Enter" && !e.isComposing) { A.setAssigneeSel(asg.trim()); setAsg(""); } }} />
      <Btn size="sm" icon="copy" title="選択した課題すべてに、同じ子タスクの雛形を追加" onClick={() => A.openTemplateApply()}>テンプレ</Btn>
      <Btn size="sm" icon="indent" title="階層を下げる (Tab)" onClick={A.indentSel} />
      <Btn size="sm" icon="outdent" title="階層を上げる (Shift+Tab)" onClick={A.outdentSel} />
      <Btn size="sm" variant="danger" icon="trash" onClick={A.deleteSel}>削除</Btn>
      <Btn size="sm" variant="ghost" icon="x" title="選択を解除 (Esc)" onClick={clearSel} />
    </div>
  );
}

/* ---------- キーボード ---------- */
function useKeys() {
  useEffect(() => {
    const onKey = (e) => {
      if (e.defaultPrevented) return;
      const mod = e.ctrlKey || e.metaKey, t = e.target;
      const typing = t && t.closest && t.closest("input,textarea,select,[contenteditable=true]");
      if (mod && (e.key === "k" || e.key === "K")) { e.preventDefault(); setUi({ palette: !ui.palette }); return; }
      if (ui.modal || ui.palette) return;
      if (typing) return;
      if (mod && !e.altKey && (e.key === "z" || e.key === "Z")) { e.preventDefault(); e.shiftKey ? redo() : undo(); return; }
      if (mod && (e.key === "y" || e.key === "Y")) { e.preventDefault(); redo(); return; }
      if (mod || !core.T || core.T.error) return;
      const inGrid = t === document.body || t === document.documentElement || !!(t.closest && t.closest(".workspace"));
      const onBtn = !!(t.closest && t.closest("button,a,summary"));
      const u = core.P.ui, cur = ui.cursor;
      if (e.key === "?" ) { e.preventDefault(); openModal("help"); return; }
      if (e.key === "/") { e.preventDefault(); window.__focusSearch && window.__focusSearch(); return; }
      if (!inGrid) return;
      if (e.key === "1" || e.key === "2" || e.key === "3" || e.key === "4") { setUI({ view: ["gantt", "board", "list", "goals"][+e.key - 1] }); return; }
      if (e.key === "Escape") { if (ui.menu) closeMenu(); else if (ui.sel.length) clearSel(); else if (ui.drawer) A.closeDetail(); return; }
      if (e.key === "ArrowDown" || e.key === "ArrowUp") {
        e.preventDefault(); const d = e.key === "ArrowDown" ? 1 : -1;
        if (e.altKey) A.moveSel(d); else moveCursor(d, e.shiftKey); return;
      }
      if (e.key === "ArrowRight" || e.key === "ArrowLeft") {
        if (!cur || !core.T.byId[cur]) return; e.preventDefault();
        const isPar = !!core.T.kids[cur].length, closed = !!u.collapsed[cur];
        if (e.key === "ArrowRight") { if (isPar && closed) setUI({ collapsed: { ...u.collapsed, [cur]: false } }); else if (isPar) moveCursor(1); }
        else if (isPar && !closed) setUI({ collapsed: { ...u.collapsed, [cur]: true } });
        else if (core.T.parentOf[cur]) selectOnly(core.T.parentOf[cur], { scroll: true });
        return;
      }
      if ((e.key === "Enter" && !onBtn) || e.key === "F2") { if (cur && core.T.byId[cur]) { e.preventDefault(); A.startEdit(cur); } return; }
      if (e.key === "n" || e.key === "N" || e.key === "Insert") { e.preventDefault(); if (e.shiftKey && cur) A.addChild(cur); else A.addSibling(cur); return; }
      if (e.key === "Tab") { if (cur) { e.preventDefault(); e.shiftKey ? A.outdentSel() : A.indentSel(); } return; }
      if (e.key === "Delete" || e.key === "Backspace") { if (ui.sel.length) { e.preventDefault(); A.deleteSel(); } return; }
      if (e.key === " " && !onBtn) { e.preventDefault(); A.toggleDetail(); return; }
    };
    document.addEventListener("keydown", onKey);
    return () => document.removeEventListener("keydown", onKey);
  }, []);
}

/* ---------- 全体 ---------- */
export function App() {
  useAll(); useKeys();
  const { P, T } = core;
  pruneSel();
  const theme = core.prefs.theme, dens = core.prefs.density;
  useEffect(() => {
    const r = document.documentElement;
    // 「自動」のときは、ホスト（開いている画面）が決めたテーマに従う
    if (theme === "auto") { if (HOST_THEME) r.setAttribute("data-theme", HOST_THEME); else r.removeAttribute("data-theme"); } else r.setAttribute("data-theme", theme);
    r.setAttribute("data-density", dens);
  }, [theme, dens]);
  // ファイルのドロップで取り込む
  useEffect(() => {
    const over = (e) => { if (e.dataTransfer && Array.from(e.dataTransfer.types || []).indexOf("Files") >= 0) e.preventDefault(); };
    const drop = async (e) => {
      const f = e.dataTransfer && e.dataTransfer.files && e.dataTransfer.files[0]; if (!f) return;
      e.preventDefault(); openModal("import", { prefill: await readFile(f), fname: f.name });
    };
    window.addEventListener("dragover", over); window.addEventListener("drop", drop);
    return () => { window.removeEventListener("dragover", over); window.removeEventListener("drop", drop); };
  }, []);
  // 選択が変わったとき、行を見える位置へ（キーボード以外の選択でも）
  const view = P.ui.view;
  const asgNames = Array.from(new Set(P.issues.map((o) => o.assignee).filter(Boolean)));
  return (
    <div className={"app" + (ui.drawer ? " with-drawer" : "")}>
      <TopBar />
      <Toolbar />
      {core.error && <div className="banner err" role="alert"><Icon n="warn" size={16} /><span>計算できません: {core.error}</span></div>}
      {core.fs.handle && core.fs.state === "need" && <div className="banner warn" role="alert"><Icon n="warn" size={16} /><span>フォルダ「{core.fs.name}」への自動保存が止まっています（開き直したため、書き込みの許可が必要です）。それまでの編集はこのブラウザには保存されています。</span><Btn size="sm" variant="pri" onClick={fsGrant}>許可して再開</Btn></div>}
      {core.fs.handle && core.fs.state === "error" && <div className="banner err" role="alert"><Icon n="warn" size={16} /><span>フォルダに保存できません: {core.fs.msg}</span><Btn size="sm" onClick={() => openModal("folder")}>詳細</Btn></div>}
      {core.saveFailed && <div className="banner warn" role="alert"><Icon n="warn" size={16} /><span>このブラウザには保存できていません。閉じると内容が失われます。</span><Btn size="sm" onClick={() => openModal("export")}>書き出して控える</Btn></div>}
      <div className="workspace">
        <main className="main" aria-label="作業領域">{view === "board" ? <Board /> : view === "list" ? <List /> : view === "goals" ? <Goals /> : <Gantt />}<BulkBar /></main>
        <Drawer />
      </div>
      <datalist id="dl-assignees">{asgNames.map((n) => <option key={n} value={n} />)}</datalist>
      <datalist id="dl-issues">{T && !T.error ? P.issues.map((o) => <option key={o.id} value={T.did[o.id]}>{T.name[o.id]}</option>) : null}</datalist>
      {ui.menu && <Menu anchor={{ x: ui.menu.x, y: ui.menu.y }} items={ui.menu.items} onClose={closeMenu} />}
      {ui.palette && <Palette />}
      <ModalHost />
      <Toasts />
    </div>
  );
}
