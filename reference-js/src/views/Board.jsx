// ボード: 状況ごとの列に課題カードを並べる（JIRAのボードに近い形）。列へドラッグすると状況が変わる。
import React, { useState } from "react";
import { core, visibleLeaves, rootIndexMap, setStatus, fmtShort, num, wk } from "../core/core.js";
import { ui, useAll, selectOnly, toggleSel, openMenu, curFilter } from "../core/state.js";
import { openDetail, rowMenu, startEdit } from "../core/actions.js";
import { Icon, TypeIcon, Avatar, FloatChip } from "../components/ui.jsx";

const PRIO = { highest: ["▲▲", "late"], high: ["▲", "late"], medium: ["＝", "tight"], low: ["▼", "accent"], lowest: ["▼▼", "accent"] };
function boardCols(T, ids) {
  const cols = ["To Do", "In Progress", "Done"];
  ids.forEach((id) => { const st = (T.byId[id].status || "").trim(); if (st && !cols.some((c) => c.toLowerCase() === st.toLowerCase())) cols.push(st); });
  return cols;
}
const colOf = (T, id, cols) => { const st = (T.byId[id].status || "").trim(); if (!st) return cols[0]; return cols.filter((c) => c.toLowerCase() === st.toLowerCase())[0] || cols[0]; };

export function Board() {
  useAll();
  const { P, T, R } = core, [over, setOver] = useState(null);
  if (!T || T.error) return <div className="empty">{T ? T.error : ""}</div>;
  if (!P.issues.length) return <div className="empty first"><h2>まだ課題がありません</h2><p>ガントかリストで課題を追加すると、ここに並びます。</p></div>;
  const leaves = visibleLeaves(curFilter());
  ui.order = leaves;
  const cols = boardCols(T, leaves), lane = P.ui.lane || "none", ri = rootIndexMap(), groups = [];
  if (lane === "none") groups.push({ key: "all", title: "", ids: leaves });
  else {
    const m = {}, order = [];
    leaves.forEach((id) => {
      const k = lane === "asg" ? T.byId[id].assignee || "（未割当）" : T.rootOf[id];
      if (!m[k]) { m[k] = { key: k, title: lane === "asg" ? k : T.name[k], ids: [], c: (ri[T.rootOf[id]] || 0) % 8 }; order.push(k); }
      m[k].ids.push(id);
    });
    if (lane === "asg") order.sort((a, b) => (a === "（未割当）") - (b === "（未割当）") || String(a).localeCompare(String(b), "ja"));
    order.forEach((k) => groups.push(m[k]));
  }
  if (!leaves.length) return <div className="empty small-empty">条件に合う課題がありません。</div>;

  const card = (id, cur, i) => {
    const o = T.byId[id], r = R[id], pr = PRIO[String(o.prio).toLowerCase()], path = [];
    for (let p = T.parentOf[id]; p; p = T.parentOf[p]) path.unshift(T.name[p]);
    const sel = ui.sel.indexOf(id) >= 0;
    return (
      <div key={id} className={"bcard" + (sel ? " sel" : "") + (ui.cursor === id ? " cur" : "")} data-rid={id} draggable tabIndex={0} style={{ "--c": "var(--c" + ((ri[T.rootOf[id]] || 0) % 8) + ")" }}
        onClick={(e) => { if (e.ctrlKey || e.metaKey) toggleSel(id); else selectOnly(id); }} onDoubleClick={() => openDetail(id)}
        onContextMenu={(e) => { e.preventDefault(); if (ui.sel.indexOf(id) < 0) selectOnly(id); openMenu(e.clientX, e.clientY, rowMenu(id)); }}
        onDragStart={(e) => { e.dataTransfer.setData("text/plain", id); e.dataTransfer.effectAllowed = "move"; }}>
        <div className="bt"><TypeIcon type={o.type} /><span className="rid">{T.did[id]}</span><span style={{ marginLeft: "auto" }}><FloatChip r={r} plain /></span></div>
        <div className="bn">{T.name[id]}</div>
        {path.length > 0 && <div className="bp">{path.join(" › ")}</div>}
        <div className="bf">
          {pr && <span className={"pr " + pr[1]} title={o.prio}>{pr[0]}</span>}
          {o.comments && o.comments.length > 0 && <span className="cmc"><Icon n="message" size={12} />{o.comments.length}</span>}
          <span className="muted mono sm">{fmtShort(r.asapStart)}〜{fmtShort(r.asapFinish)}</span>
          {o.assignee && <span style={{ marginLeft: "auto" }}><Avatar name={o.assignee} /></span>}
        </div>
      </div>
    );
  };

  return (
    <div className="board">
      {groups.map((g) => (
        <section key={g.key} className="lane">
          {g.title && <h4 className="lane-t" style={g.c != null ? { "--c": "var(--c" + g.c + ")" } : null}>{g.title}<span className="muted"> {g.ids.length}件</span></h4>}
          <div className="bcols" style={{ gridTemplateColumns: "repeat(" + cols.length + ",minmax(200px,1fr))" }}>
            {cols.map((c) => {
              const mine = g.ids.filter((id) => colOf(T, id, cols) === c), k = g.key + "|" + c;
              return (
                <div key={c} className={"bcol" + (over === k ? " over" : "")}
                  onDragOver={(e) => { e.preventDefault(); setOver(k); }} onDragLeave={() => setOver((v) => (v === k ? null : v))}
                  onDrop={(e) => { e.preventDefault(); setOver(null); const id = e.dataTransfer.getData("text/plain"); if (id && T.byId[id] && colOf(T, id, cols) !== c) setStatus(id, c); }}>
                  <div className="bch"><span>{c}</span><span className="muted">{mine.length}</span></div>
                  {mine.map((id, i) => card(id, c, i))}
                </div>
              );
            })}
          </div>
        </section>
      ))}
    </div>
  );
}
