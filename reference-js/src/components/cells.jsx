// ガント・リストで共通に使う、行の中のセル（表示と、その場での編集）
import React, { useEffect, useRef, useState } from "react";
import { core, setName, setDuration, setField, setStatus, indent, outdent, num } from "../core/core.js";
import { ui, setUi, openMenu } from "../core/state.js";
import { stopEdit, addChild, addSibling, startEdit, openDetail } from "../core/actions.js";
import { Icon, TypeIcon, Avatar, StatusPill, FloatChip } from "./ui.jsx";

let suppressBlur = false;

/* ---- 名前の編集 ---- */
export function NameEditor({ id }) {
  const [v, setV] = useState(core.T.name[id]);
  const ref = useRef(null);
  useEffect(() => { const el = ref.current; if (el) { el.focus(); el.select(); } }, []);
  useEffect(() => { const el = ref.current; if (el && document.activeElement !== el && ui.editing === id) el.focus(); });
  const commit = () => { const t = v.trim(); if (t && t !== core.T.name[id]) setName(id, t); };
  const keep = (fn) => { suppressBlur = true; commit(); fn(); setTimeout(() => { suppressBlur = false; }, 0); };
  const onKey = (e) => {
    e.stopPropagation();
    if (e.isComposing || e.keyCode === 229) return;       // 日本語入力の確定のEnterは無視
    if (e.key === "Enter") {
      e.preventDefault();
      if (e.shiftKey) { commit(); stopEdit(); addChild(id); }
      else if (e.ctrlKey || e.metaKey || ui.fresh === id) { commit(); stopEdit(); addSibling(id); }
      else { commit(); stopEdit(); }
    } else if (e.key === "Escape") { e.preventDefault(); stopEdit(); }
    else if (e.key === "Tab") { e.preventDefault(); keep(() => (e.shiftKey ? outdent(id) : indent(id))); }
    else if (e.key === "ArrowDown" || e.key === "ArrowUp") { if (!e.altKey) { e.preventDefault(); commit(); stopEdit(); const o = ui.order, i = o.indexOf(id), j = i + (e.key === "ArrowDown" ? 1 : -1); if (o[j]) startEdit(o[j]); } }
  };
  return <input ref={ref} className="ed nm-ed" value={v} aria-label="件名" onChange={(e) => setV(e.target.value)} onKeyDown={onKey} onBlur={() => { if (suppressBlur) return; commit(); if (ui.editing === id) stopEdit(); }} onClick={(e) => e.stopPropagation()} onDoubleClick={(e) => e.stopPropagation()} />;
}

/* ---- 汎用の1行入力（担当者・期間など） ---- */
export function CellInput({ value, onCommit, type = "text", list, min, step, width, className, placeholder }) {
  const [v, setV] = useState(value == null ? "" : String(value));
  const ref = useRef(null);
  useEffect(() => { const el = ref.current; if (el) { el.focus(); el.select && el.select(); } }, []);
  const commit = () => { onCommit(v); };
  return <input ref={ref} className={"ed " + (className || "")} type={type} list={list} min={min} step={step} value={v} placeholder={placeholder} style={width ? { width } : null} onChange={(e) => setV(e.target.value)}
    onKeyDown={(e) => { e.stopPropagation(); if (e.isComposing) return; if (e.key === "Enter") { e.preventDefault(); commit(); stopEdit(); } else if (e.key === "Escape") { e.preventDefault(); stopEdit(); } else if (e.key === "Tab") { commit(); stopEdit(); } }}
    onBlur={() => { commit(); if (ui.editing) stopEdit(); }} onClick={(e) => e.stopPropagation()} onDoubleClick={(e) => e.stopPropagation()} />;
}

/* ---- 状況をその場で変える ---- */
export function statusMenu(id, e) {
  const r = e.currentTarget.getBoundingClientRect(), cur = core.T.byId[id].status;
  const names = ["To Do", "In Progress", "Done"];
  openMenu(r.left, r.bottom + 2, names.map((n) => ({ label: n, checked: n.toLowerCase() === String(cur || "To Do").trim().toLowerCase(), onClick: () => { const ids = ui.sel.indexOf(id) >= 0 ? ui.sel.slice() : [id]; ids.forEach((x) => setStatus(x, n)); } })));
}
export function StatusCell({ id }) {
  const o = core.T.byId[id];
  if (core.T.kids[id].length) { const r = core.R[id]; return <span className="muted sm mono">{r.nDone}/{r.n}</span>; }
  return <StatusPill status={o.status} onClick={(e) => { e.stopPropagation(); statusMenu(id, e); }} />;
}

export function AsgCell({ id }) {
  const o = core.T.byId[id];
  if (core.T.kids[id].length) return null;
  if (ui.editing === id && ui.editCell === "asg") return <CellInput value={o.assignee} list="dl-assignees" onCommit={(v) => { v = v.trim(); if (v !== o.assignee) setField(id, "assignee", v); }} />;
  return <span className="asgc" onDoubleClick={(e) => { e.stopPropagation(); startEdit(id, "asg"); }} title={o.assignee ? o.assignee + "（ダブルクリックで変更）" : "ダブルクリックで担当者を設定"}>
    {o.assignee ? <><Avatar name={o.assignee} /><span className="asgn">{o.assignee}</span></> : <span className="ph">—</span>}</span>;
}

export function DurCell({ id }) {
  const T = core.T, r = core.R[id];
  if (T.kids[id].length) return <span className="muted sm mono">{r.n}件</span>;
  if (T.kind[id] === "ms") return <span className="muted sm">◆</span>;
  if (ui.editing === id && ui.editCell === "dur") return <CellInput type="number" min="0" step="0.5" width="100%" value={num(r.weeks)} onCommit={(v) => { const n = parseFloat(v); if (!isNaN(n) && n >= 0 && n !== r.weeks) setDuration(id, n); }} />;
  return <span className="mono sm durc" onDoubleClick={(e) => { e.stopPropagation(); if (!r.done) startEdit(id, "dur"); }} title="ダブルクリックで期間（週）を変更">{num(r.weeks)}w</span>;
}

export function FloatCell({ id }) { return <FloatChip r={core.R[id]} plain />; }

/* ---- 件名セル（開閉・種別・ID・名前） ---- */
export function NameCell({ id, depth, isPar, open, onToggle, indentPx = 14 }) {
  const o = core.T.byId[id], editing = ui.editing === id && ui.editCell === "name";
  const nc = (o.comments && o.comments.length) || 0;
  return (
    <div className="nmc" style={{ paddingLeft: 4 + depth * indentPx }}>
      {isPar ? <button type="button" className="twist" aria-label={open ? "閉じる" : "開く"} aria-expanded={open} onClick={(e) => { e.stopPropagation(); onToggle(); }} onDoubleClick={(e) => e.stopPropagation()}><Icon n={open ? "down" : "right"} size={14} /></button> : <span className="twist none" />}
      <TypeIcon type={o.type} />
      <span className="rid">{core.T.did[id]}</span>
      {editing ? <NameEditor id={id} /> : <span className="nm" onDoubleClick={(e) => { e.stopPropagation(); startEdit(id); }}>{core.T.name[id]}</span>}
      {!editing && nc > 0 && <span className="cmc" title={"コメント " + nc + "件"}><Icon n="message" size={12} />{nc}</span>}
      {!editing && <span className="rowact">
        <button type="button" className="ra" title="子を追加" aria-label="子を追加" onClick={(e) => { e.stopPropagation(); addChild(id); }} onDoubleClick={(e) => e.stopPropagation()}><Icon n="plus" size={13} /></button>
        <button type="button" className="ra" title="詳細を開く" aria-label="詳細を開く" onClick={(e) => { e.stopPropagation(); openDetail(id); }} onDoubleClick={(e) => e.stopPropagation()}><Icon n="right" size={13} /></button>
      </span>}
    </div>
  );
}
