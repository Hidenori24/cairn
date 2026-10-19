// ゴール: マイルストーンごとに「期限（理想）」と「見込み（現状）」の差と、その差を作っている未完了タスクの連なりを見る。
import React, { useState } from "react";
import { core, fmtD, fmtShort, num } from "../core/core.js";
import { ui, useAll } from "../core/state.js";
import { openDetail } from "../core/actions.js";
import { Icon, TypeIcon, Avatar, StatusPill } from "../components/ui.jsx";

const PRIO_RANK = { highest: 0, high: 1, medium: 2, low: 3, lowest: 4 };
const prioRank = (id) => { const r = PRIO_RANK[String(core.T.byId[id].prio || "").toLowerCase()]; return r == null ? 2 : r; };
const isGoal = (id) => { const T = core.T, o = T.byId[id]; return !T.kids[id].length && (/milestone|マイルストーン/i.test(o.type || "") || (o.labels || []).some((l) => /milestone|マイルストーン/i.test(l))); };

// ゴールから前提を遡り、最も遅く終わる未完了の先行を辿った一本道（＝いまゴールを決めている連なり）
function drivingChain(goal) {
  const { T, R } = core, chain = [], seen = {};
  let cur = goal;
  while (cur) {
    const ps = T.preds[cur].filter((p) => T.cat[p] !== "done");
    if (!ps.length) break;
    ps.sort((a, b) => R[b].asapFinish - R[a].asapFinish || R[a].float - R[b].float);
    cur = ps[0]; if (seen[cur]) break; seen[cur] = 1; chain.unshift(cur);
  }
  return chain;
}
function allBlockers(goal) {
  const { T } = core, seen = {}, stack = [goal];
  while (stack.length) { const x = stack.pop(); T.preds[x].forEach((p) => { if (T.cat[p] !== "done" && !seen[p]) { seen[p] = 1; stack.push(p); } }); }
  return Object.keys(seen);
}

function GapTrack({ goal }) {
  const { P, T, R } = core, r = R[goal], dl = T.dl[goal], fc = r.asapFinish, b = Sched.parse(P.base);
  const lo = Math.min(b, dl != null ? dl : b), hi = Math.max(dl != null ? dl : fc, fc) + 4, span = Math.max(hi - lo, 1);
  const pos = (d) => ((d - lo) / span) * 100 + "%";
  const late = dl != null && fc > dl;
  return (
    <div className="gtrack" aria-hidden="true">
      <div className="gt-line" />
      <div className="gt-base" style={{ left: pos(b) }}><i /><span>基準日</span></div>
      {dl != null && fc !== dl && <div className={"gt-seg " + (late ? "late" : "ok")} style={{ left: pos(Math.min(dl, fc)), width: (Math.abs(fc - dl) / span) * 100 + "%" }} />}
      {dl != null && <div className="gt-dl" style={{ left: pos(dl) }}><i /><span>期限 {fmtShort(dl)}</span></div>}
      <div className={"gt-fc " + (dl == null ? "none" : late ? "late" : "ok")} style={{ left: pos(fc) }}><i /><span>見込み {fmtShort(fc)}</span></div>
    </div>
  );
}

function Step({ id, n }) {
  const { T, R } = core, r = R[id], o = T.byId[id];
  return (
    <li className={"gstep" + (r.float < 0 ? " late" : "") + (T.cat[id] === "prog" ? " prog" : "")}>
      <span className="gn">{n}</span>
      <button type="button" className="gs-main" onClick={() => openDetail(id, "detail")} title="詳細を開く">
        <TypeIcon type={o.type} />
        <span className="mono sm muted">{T.did[id]}</span>
        <span className="gs-name">{T.name[id]}</span>
      </button>
      <span className="gs-asg">{o.assignee ? <><Avatar name={o.assignee} /><span className="sm">{o.assignee}</span></> : <span className="sm ph">担当未定</span>}</span>
      <span className="gs-when mono sm">{r.asapStart === r.asapFinish ? fmtShort(r.asapFinish) : fmtShort(r.asapStart) + "〜" + fmtShort(r.asapFinish)}<span className="muted">{r.leaf && T.kind[id] !== "ms" ? "（" + num(r.weeks) + "週）" : ""}</span></span>
      <span className="gs-dl mono sm">{T.ownDl[id] != null ? "期限 " + fmtShort(T.ownDl[id]) : <span className="ph">期限なし</span>}</span>
      <span className={"gs-fl " + (r.done ? "done" : r.float < 0 ? "late" : r.float <= 10 ? "tight" : "ok")} title="このタスクの期限（または後続からの逆算）に対する余裕">{r.done ? "完了" : r.float < 0 ? "−" + Math.abs(r.float) + "営業日" : "余裕 " + r.float + "日"}</span>
      <StatusPill status={o.status} />
          </li>
  );
}

function GoalCard({ id, open, onToggle }) {
  const { T, R } = core, r = R[id], o = T.byId[id], dl = T.dl[id], fc = r.asapFinish;
  const chain = drivingChain(id), blockers = allBlockers(id), onChain = {}; chain.forEach((x) => (onChain[x] = 1));
  const others = blockers.filter((x) => !onChain[x]).sort((a, b) => R[a].asapFinish - R[b].asapFinish);
  const gap = dl != null ? core.cal.floatDays(Math.min(dl, fc), Math.max(dl, fc)) : null;   // 営業日の差
  const late = dl != null && fc > dl, k = dl == null ? "none" : late ? "late" : r.float <= 10 ? "tight" : "ok";
  const lateSteps = chain.filter((x) => R[x].float < 0).length;
  return (
    <section className={"gcard " + k + (prioRank(id) === 0 ? " top" : "")}>
      <header className="gc-h">
        <div className="gc-t">
          <TypeIcon type={o.type} /><span className="mono sm muted">{T.did[id]}</span>
          <h3>{T.name[id]}</h3>
          {prioRank(id) === 0 && <span className="gtop" title="優先度 Highest">最重要</span>}
        </div>
        <div className="gc-sum">
          <div className="gc-k"><span>期限（理想）</span><b className="mono">{dl != null ? fmtD(dl) : "未設定"}</b></div>
          <div className="gc-k"><span>見込み（現状）</span><b className="mono">{fmtD(fc)}</b></div>
          <div className={"gc-gap " + k}>
            {dl == null ? <><b>期限が未設定</b><span>日付を入れると、ここに差が出ます</span></>
              : late ? <><b>{gap}営業日 遅れる見込み</b><span>{lateSteps ? "遅れの原因: 下の連なり（" + lateSteps + "件が期限超過）" : "下の連なりの積み上げで超過"}</span></>
                : <><b>{gap === 0 ? "余裕なし（ちょうど間に合う）" : gap + "営業日の余裕"}</b><span>{lateSteps ? "ゴールには間に合うが、途中に期限超過が " + lateSteps + " 件" : "未完了の前提 " + blockers.length + " 件"}</span></>}
          </div>
        </div>
      </header>
      <GapTrack goal={id} />
      {chain.length === 0 ? <p className="gc-none sm muted">このゴールの前提に、未完了のタスクはありません。</p> : <>
        <div className="gc-sub"><Icon n="link" size={13} />いま日程を決めている連なり<span className="muted">（上から順にクリアしていく。1つ遅れるとゴールが遅れる）</span></div>
        <ol className="gsteps">
          {chain.map((x, i) => <Step key={x} id={x} n={i + 1} />)}
          <li className="gstep goal"><span className="gn"><Icon n="target" size={12} /></span><span className="gs-main static"><span className="mono sm muted">{T.did[id]}</span><span className="gs-name">{T.name[id]}</span></span></li>
        </ol>
      </>}
      {others.length > 0 && <>
        <button type="button" className="gc-more" aria-expanded={open} onClick={onToggle}><Icon n={open ? "down" : "right"} size={13} />並行して必要な未完了の前提 {others.length} 件</button>
        {open && <ol className="gsteps plain">{others.map((x) => <Step key={x} id={x} n="·" />)}</ol>}
      </>}
    </section>
  );
}

export function Goals() {
  useAll();
  const { P, T, R } = core, [openMap, setOpenMap] = useState({});
  if (!T || T.error) return <div className="empty">{T ? T.error : ""}</div>;
  const all = T.leafOrder.filter(isGoal);
  if (!all.length) return <div className="empty first"><h2>ゴール（マイルストーン）がありません</h2><p>課題の種別を <code>Milestone</code> にすると、ここに期限と見込みの差、律速になっている連なりが出ます。</p></div>;
  const open = all.filter((id) => T.cat[id] !== "done"), done = all.filter((id) => T.cat[id] === "done");
  open.sort((a, b) => prioRank(a) - prioRank(b) || (T.dl[a] == null) - (T.dl[b] == null) || (T.dl[a] || 0) - (T.dl[b] || 0) || R[a].asapFinish - R[b].asapFinish);
  const nLate = open.filter((id) => T.dl[id] != null && R[id].asapFinish > T.dl[id]).length, nNone = open.filter((id) => T.dl[id] == null).length;
  return (
    <div className="gv">
      <div className="gv-h">
        <h2>ゴールと現状のギャップ</h2>
        <p className="muted sm">期限＝ゴールに設定した日付（理想）、見込み＝いまの前提と期間で最短に終わる日（現状）。優先度が高いゴール、次に期限が近いゴールの順です。</p>
        <div className="gv-s"><span><b>{open.length}</b>未達のゴール</span><span className={nLate ? "late" : ""}><b>{nLate}</b>期限に間に合わない</span><span className={nNone ? "tight" : ""}><b>{nNone}</b>期限が未設定</span><span><b>{done.length}</b>達成済み</span></div>
      </div>
      {open.map((id) => <GoalCard key={id} id={id} open={!!openMap[id]} onToggle={() => setOpenMap({ ...openMap, [id]: !openMap[id] })} />)}
      {done.length > 0 && <p className="sm muted gv-done">達成済み: {done.map((id) => T.did[id] + " " + T.name[id]).join(" ／ ")}</p>}
    </div>
  );
}
