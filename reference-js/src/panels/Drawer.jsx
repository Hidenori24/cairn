// 右の詳細パネル。「詳細」「依存関係」「コメント」の3つに分けて、短く保つ。
import React, { useEffect, useState } from "react";
import { core, setName, setTag, setField, setStatus, setDuration, removeScenarioValue, setEarliest, setDeadline, addLink, removeLink, addComment, delComment, reparent, resolveRef, descendantsOf, mutate, toast, fmtD, num, D } from "../core/core.js";
import { ui, setUi, selectOnly, useAll } from "../core/state.js";
import { closeDetail, addChild, addSibling, indentSel, outdentSel, moveSel, deleteSel, startEdit } from "../core/actions.js";
import { Icon, Btn, TypeIcon, StatusPill, FloatChip, MenuButton, Field, Avatar } from "../components/ui.jsx";
import { statusMenu } from "../components/cells.jsx";

const GEN = ["", "子", "孫", "ひ孫", "玄孫"];
const genName = (d) => (d === 0 ? "最上位" : d < GEN.length ? GEN[d] : "第" + d + "世代");

function TextField({ value, onCommit, placeholder, list, className, multiline, label, rows }) {
  const [v, setV] = useState(value == null ? "" : String(value));
  useEffect(() => { setV(value == null ? "" : String(value)); }, [value]);
  const commit = () => { if (v !== (value == null ? "" : String(value))) onCommit(v); };
  const P = { value: v, placeholder, list, className: "inp " + (className || ""), "aria-label": label, onChange: (e) => setV(e.target.value), onBlur: commit };
  return multiline
    ? <textarea {...P} rows={rows || 3} />
    : <input {...P} onKeyDown={(e) => { if (e.isComposing) return; if (e.key === "Enter") { e.preventDefault(); commit(); e.target.blur(); } else if (e.key === "Escape") { e.stopPropagation(); setV(value == null ? "" : String(value)); e.target.blur(); } }} />;
}
function Sel({ value, options, onChange, label }) {
  const list = options.slice(); if (value && list.indexOf(value) < 0) list.unshift(value);
  return <select className="inp" aria-label={label} value={value || ""} onChange={(e) => onChange(e.target.value)}>{list.map((x) => <option key={x} value={x}>{x || "（なし）"}</option>)}</select>;
}
function DateF({ value, onChange, title, label }) {
  return <input className="inp mono" type="date" aria-label={label} title={title} value={value != null ? D(value) : ""} onChange={(e) => onChange(e.target.value ? CsvIO.parseDate(e.target.value) : null)} />;
}
const Group = ({ title, children, right }) => <section className="grp"><div className="grp-h"><h3>{title}</h3>{right}</div>{children}</section>;

function Detail({ id }) {
  const { P, T, R } = core, o = T.byId[id], r = R[id], isPar = !!T.kids[id].length, cat = T.cat[id], dep = T.depth[id], tg = Engine.tagOf(o.summary);
  const path = []; for (let p = T.parentOf[id]; p; p = T.parentOf[p]) path.unshift(p);
  const curP = o.parent && T.byId[o.parent] ? T.did[o.parent] : "";
  const onParent = (v) => {
    if (!v.trim()) { reparent(id, ""); return; }
    const pid = resolveRef(v);
    if (!pid) { toast("「" + v + "」に合う課題が見つかりません"); return; }
    if (pid === id || descendantsOf(id).indexOf(pid) >= 0) { toast("自分の配下は親にできません"); return; }
    reparent(id, pid);
  };
  const scnOn = P.scn !== "標準";
  const shownDl = T.ownDl[id] != null && !P.meta.hasDeadlineCol ? T.ownDl[id] : o.deadline;
  return (
    <div className="dr-body">
      {path.length > 0 && <div className="crumbs">{path.map((p) => <button key={p} type="button" onClick={() => selectOnly(p)}>{T.name[p]}</button>)}</div>}
      <TextField className="title-in" value={tg.name} label="件名" onCommit={(v) => setName(id, v.trim())} />
      <Group title="基本">
        <div className="fgrid">
          <Field label="番号（表示用ID）"><TextField value={tg.tag} label="番号" placeholder={o.key || o.id} onCommit={(v) => setTag(id, v)} /></Field>
          <Field label="状況"><Sel value={o.status || "To Do"} options={["To Do", "In Progress", "Done"]} label="状況" onChange={(v) => setStatus(id, v)} /></Field>
          <Field label="担当者"><TextField value={o.assignee} list="dl-assignees" label="担当者" placeholder="未割当" onCommit={(v) => setField(id, "assignee", v.trim())} /></Field>
          <Field label="優先度"><Sel value={o.prio} options={["Highest", "High", "Medium", "Low", "Lowest"]} label="優先度" onChange={(v) => setField(id, "prio", v)} /></Field>
          <Field label="種別"><Sel value={o.type} options={["Epic", "Story", "Task", "Sub-task", "Bug", "Milestone"]} label="種別" onChange={(v) => setField(id, "type", v)} /></Field>
          <Field label="ラベル" full><TextField value={o.labels.join(", ")} label="ラベル" placeholder="カンマ区切り" onCommit={(v) => setField(id, "labels", v.split(/[,、]/).map((s) => s.trim()).filter(Boolean))} /></Field>
          <Field label="親" full><TextField value={curP} list="dl-issues" label="親" placeholder="空欄で最上位" onCommit={onParent} /></Field>
        </div>
      </Group>
      <Group title="日程">
        <div className="fgrid">
          {!isPar ? (
            <Field label={"期間（週）" + (scnOn ? " @" + P.scn : "")} full>
              <div className="stepper">
                <Btn size="sm" icon="minus" title="1週短く" onClick={() => setDuration(id, Math.max(0, num(r.weeks - 1)))} />
                <input className="inp mono" type="number" min="0" step="0.5" aria-label="期間（週）" key={id + ":" + num(r.weeks)} defaultValue={num(r.weeks)} onBlur={(e) => { const n = parseFloat(e.target.value); if (!isNaN(n) && n >= 0 && n !== r.weeks) setDuration(id, n); }} onKeyDown={(e) => { if (e.key === "Enter") e.target.blur(); }} />
                <Btn size="sm" icon="plus" title="1週長く" onClick={() => setDuration(id, num(r.weeks + 1))} />
                {scnOn && o.alt[P.scn] != null && <Btn size="sm" variant="ghost" onClick={() => removeScenarioValue(id)}>標準に戻す</Btn>}
              </div>
            </Field>
          ) : <Field label="期間" full><div className="val muted">配下の {r.n} 件から集計（完了 {r.nDone}）</div></Field>}
          <Field label="期限"><DateF value={shownDl} label="期限" onChange={(v) => setDeadline(id, v)} /></Field>
          {!isPar && cat !== "done" && !r.pinned && <Field label="着手可能日"><DateF value={o.notBefore} label="着手可能日" title="この日より前には始めない（空欄で解除）" onChange={(v) => setEarliest(id, v)} /></Field>}
          {!isPar && cat === "prog" && <Field label="残り（週）"><input className="inp mono" type="number" min="0" step="0.5" aria-label="残り（週）" placeholder="任意" key={id + ":rem"} defaultValue={o.remW != null ? num(o.remW) : ""} onBlur={(e) => { const n = parseFloat(e.target.value); setField(id, "remW", isNaN(n) ? null : n); }} /></Field>}
          {!isPar && (cat === "prog" || cat === "done") && <Field label="実績開始"><DateF value={o.start} label="実績開始" onChange={(v) => setField(id, "start", v)} /></Field>}
          {!isPar && cat === "done" && <Field label="完了日"><DateF value={o.resolved} label="完了日" onChange={(v) => setField(id, "resolved", v)} /></Field>}
        </div>
        <div className="plan">
          <div><span>計画（最早）</span><b className="mono">{fmtD(r.asapStart)} 〜 {fmtD(r.asapFinish)}</b></div>
          <div><span>最遅</span><b className="mono">{r.done ? "—" : fmtD(r.alapStart) + " 〜 " + fmtD(r.alapFinish)}</b></div>
          <div><span>余裕</span><span><FloatChip r={r} />{!r.done && <span className="muted sm">　{r.float}営業日 {r.status}</span>}</span></div>
        </div>
        {r.waited && <div className="note">担当者「{o.assignee}」の別の作業が終わるのを待っています。先行だけなら {fmtD(r.freeStart)} から始められます。</div>}
        {!isPar && o.notBefore != null && !r.done && r.asapStart > o.notBefore && <div className="note">着手可能日の指定（{fmtD(o.notBefore)}）より後になります。先行や担当者の都合です。</div>}
        {isPar && <div className="note">子があるため、期間と状況は配下から集計されます。依存関係と期限は配下の全作業に効きます。</div>}
      </Group>
      <Group title="説明"><TextField multiline value={o.desc} label="説明" placeholder="メモ、受け入れ条件など" onCommit={(v) => setField(id, "desc", v)} /></Group>
      <div className="idline muted sm mono">{genName(dep)}（{dep + 1}階層目）／ Issue Id {o.id}{o.key ? " ／ " + o.key : ""}</div>
    </div>
  );
}

function Pill({ x, onRemove }) {
  const T = core.T;
  return <span className="pill"><button type="button" title={T.name[x]} onClick={() => selectOnly(x)}><TypeIcon type={T.byId[x].type} /> {T.did[x]} <span className="pn">{T.name[x]}</span></button><button type="button" className="x" aria-label="解除" onClick={onRemove}><Icon n="x" size={12} /></button></span>;
}
function DepBox({ label, hint, ids, onAdd, onRemove }) {
  const [v, setV] = useState("");
  const go = () => { const x = resolveRef(v); if (!x) { toast("「" + v + "」に合う課題が見つかりません"); return; } if (onAdd(x)) setV(""); };
  return (
    <Group title={label}>
      <p className="muted sm" style={{ margin: "0 0 6px" }}>{hint}</p>
      <div className="pills">{ids.length ? ids.map((x) => <Pill key={x} x={x} onRemove={() => onRemove(x)} />) : <span className="muted sm">なし</span>}</div>
      <div className="addrow"><input className="inp" list="dl-issues" value={v} placeholder="IDまたは件名を入力して Enter" aria-label={label + "を追加"} onChange={(e) => setV(e.target.value)} onKeyDown={(e) => { if (e.isComposing) return; if (e.key === "Enter") { e.preventDefault(); go(); } }} /><Btn size="sm" onClick={go}>追加</Btn></div>
    </Group>
  );
}
function Deps({ id }) {
  const { T } = core, o = T.byId[id], isPar = !!T.kids[id].length;
  const preds = core.P.issues.filter((a) => a.blocks.indexOf(id) >= 0).map((a) => a.id);
  const succs = o.blocks.filter((b) => T.byId[b]);
  const eff = !isPar ? T.preds[id].map((x) => T.did[x]) : [];
  const direct = preds.filter((p) => !T.kids[p].length).length;
  return (
    <div className="dr-body">
      <DepBox label="先行" hint="これが終わってから始める" ids={preds} onAdd={(x) => addLink(x, id)} onRemove={(x) => removeLink(x, id)} />
      <DepBox label="後続" hint="これが終わるのを待つ" ids={succs} onAdd={(x) => addLink(id, x)} onRemove={(x) => removeLink(id, x)} />
      {!isPar && eff.length !== direct && <div className="note">計算に使う先行（親の依存を含む）: {eff.join(", ")}</div>}
    </div>
  );
}

function Comments({ id }) {
  const o = core.T.byId[id], cms = o.comments || [];
  const [text, setText] = useState(""), [who, setWho] = useState(core.prefs.me || "自分");
  const post = () => { if (addComment(id, text, who.trim())) setText(""); };
  return (
    <div className="dr-body">
      <div className="cmts">
        {!cms.length && <p className="muted sm">コメントはまだありません。</p>}
        {cms.map((c, i) => (
          <div key={i} className="cmt"><Avatar name={c.who || "?"} /><div className="cmt-m"><div className="cmh"><b>{c.who || "（不明）"}</b><span className="muted mono sm">{c.at ? c.at.replace("T", " ") : ""}</span><button type="button" className="x" aria-label="コメントを削除" onClick={() => delComment(id, i)}><Icon n="x" size={12} /></button></div><div className="cmb">{c.text}</div></div></div>
        ))}
      </div>
      <textarea className="inp" rows={3} placeholder="コメントを書く（Ctrl+Enterで投稿）" aria-label="コメント" value={text} onChange={(e) => setText(e.target.value)} onKeyDown={(e) => { if (e.key === "Enter" && (e.ctrlKey || e.metaKey)) { e.preventDefault(); post(); } }} />
      <div className="addrow"><input className="inp" style={{ maxWidth: 140 }} value={who} onChange={(e) => setWho(e.target.value)} aria-label="投稿者名" /><Btn variant="pri" size="sm" onClick={post}>投稿</Btn></div>
    </div>
  );
}

export function Drawer() {
  useAll();
  const { T } = core, id = ui.cursor && T && !T.error && T.byId[ui.cursor] ? ui.cursor : null;
  if (!ui.drawer) return null;
  const tab = ui.drawerTab, o = id ? T.byId[id] : null;
  const nPred = id ? core.P.issues.filter((a) => a.blocks.indexOf(id) >= 0).length + o.blocks.length : 0, nCm = o ? (o.comments || []).length : 0;
  const sb = id ? (T.parentOf[id] ? T.kids[T.parentOf[id]] : T.roots) : [], si = id ? sb.indexOf(id) : 0;
  return (
    <aside className="drawer" aria-label="課題の詳細">
      {!id ? (
        <div className="dr-empty"><button type="button" className="btn ghost icon-only dr-x" aria-label="閉じる" onClick={closeDetail}><Icon n="x" /></button><Icon n="pencil" size={26} /><p>課題を選ぶと、ここで詳しく編集できます。</p><p className="muted sm">行をダブルクリック、または Space で開きます。</p></div>
      ) : (
        <>
          <div className="dr-h">
            <TypeIcon type={o.type} /><span className="rid">{T.did[id]}</span>
            <StatusPill status={o.status} onClick={(e) => statusMenu(id, e)} />
            <FloatChip r={core.R[id]} />
            <span style={{ flex: 1 }} />
            <MenuButton variant="ghost" icon="more" title="操作" align="right" items={[
              { label: "子を追加", icon: "plus", onClick: () => addChild(id) }, { label: "同じ階層に追加", icon: "plus", onClick: () => addSibling(id) }, { sep: true },
              { label: "階層を下げる", icon: "indent", disabled: si <= 0, onClick: indentSel }, { label: "階層を上げる", icon: "outdent", disabled: !T.parentOf[id], onClick: outdentSel },
              { label: "上へ", icon: "arrowUp", disabled: si <= 0, onClick: () => moveSel(-1) }, { label: "下へ", icon: "arrowDown", disabled: si >= sb.length - 1, onClick: () => moveSel(1) }, { sep: true },
              { label: "削除", icon: "trash", danger: true, onClick: deleteSel }]} />
            <button type="button" className="btn ghost icon-only" aria-label="閉じる" onClick={closeDetail}><Icon n="x" /></button>
          </div>
          <div className="tabs" role="tablist">
            {[["detail", "詳細"], ["deps", "依存関係" + (nPred ? " " + nPred : "")], ["cm", "コメント" + (nCm ? " " + nCm : "")]].map(([k, l]) => (
              <button key={k} type="button" role="tab" aria-selected={tab === k} className={tab === k ? "on" : ""} onClick={() => setUi({ drawerTab: k })}>{l}</button>
            ))}
          </div>
          <div className="dr-scroll" key={id + tab}>{tab === "detail" ? <Detail id={id} /> : tab === "deps" ? <Deps id={id} /> : <Comments id={id} />}</div>
        </>
      )}
    </aside>
  );
}
