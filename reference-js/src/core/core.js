// アプリの中核: プロジェクトの読み書き、課題の編集、取り消し、計算の更新。画面（React）からはここだけを触る。
// 依存（グローバル）: Sched, CsvIO, Engine, Store, SAMPLE_CSV, DEMO_CSV, DEFAULT_HOLIDAYS
import * as Folder from "./folder.js";
const HOL0 = DEFAULT_HOLIDAYS.join("\n");
const listeners = new Set();
export const core = { P: null, T: null, R: null, cal: null, error: null, undoS: [], redoS: [], toasts: [], ver: 0, saveFailed: false, prefs: loadPrefs(),
  fs: { supported: Folder.supported, handle: null, name: "", perm: "none", state: "off", saved: 0, msg: "", stems: new Set() } };
export const subscribe = (f) => { listeners.add(f); return () => listeners.delete(f); };
export const getVer = () => core.ver;
const emit = () => { core.ver++; listeners.forEach((f) => f()); };

/* ---------- 端末ごとの設定（テーマ・密度など） ---------- */
function loadPrefs() {
  const d = { theme: "auto", density: "normal", hint: false, me: "自分" };
  try { return Object.assign(d, JSON.parse(localStorage.getItem("regroadmap.v3.prefs") || "{}")); } catch (e) { return d; }
}
export function setPref(k, v) {
  core.prefs[k] = v;
  try { localStorage.setItem("regroadmap.v3.prefs", JSON.stringify(core.prefs)); } catch (e) {}
  emit();
}

/* ---------- 通知 ---------- */
let toastN = 0;
export function toast(msg, action) {
  const t = { id: ++toastN, msg, action };
  core.toasts = core.toasts.concat(t); emit();
  setTimeout(() => { core.toasts = core.toasts.filter((x) => x.id !== t.id); emit(); }, action ? 7000 : 3600);
}
export function dismissToast(id) { core.toasts = core.toasts.filter((x) => x.id !== id); emit(); }

/* ---------- 日付などの小道具 ---------- */
export const todayIso = () => { const d = new Date(), p = (n) => String(n).padStart(2, "0"); return d.getFullYear() + "-" + p(d.getMonth() + 1) + "-" + p(d.getDate()); };
export const nowDT = () => { const d = new Date(), p = (n) => String(n).padStart(2, "0"); return todayIso() + "T" + p(d.getHours()) + ":" + p(d.getMinutes()); };
const holidaysFromText = (txt) => { const out = []; String(txt || "").split(/[\s,;、]+/).forEach((tok) => { const n = CsvIO.parseDate(tok); if (n != null) out.push(Sched.fmt(n)); }); return out; };

/* ---------- プロジェクトの保存形式 ---------- */
export const defaultUI = () => ({ collapsed: {}, hide: {}, zoom: "week", level: 0, view: "gantt", lane: "none", colorBy: "root", cols: { asg: true, status: true, dur: true, float: true }, nameW: 300 });
function hydrate(rec) {
  let imp = null;
  if (CsvIO.parse(rec.csv || "").rows.length) imp = CsvIO.importCSV(rec.csv);
  const ok = imp && !imp.error;
  const meta = ok ? imp.meta : { hasDeadlineCol: true, scenarios: [], extraHeaders: [] };
  (rec.scenarios || []).forEach((s) => { if (meta.scenarios.indexOf(s) < 0) meta.scenarios.push(s); });
  const ui = Object.assign(defaultUI(), rec.ui || {});
  if (!ui.cols) ui.cols = defaultUI().cols;
  if (typeof ui.zoom !== "string") ui.zoom = "week";
  core.P = { id: rec.id, name: rec.name, key: rec.key || "", base: rec.base || todayIso(), hol: rec.hol != null ? rec.hol : HOL0, scn: rec.scn || "標準",
    ui, res: Object.assign({ on: false, cap: 1 }, rec.res || {}), issues: ok ? imp.issues : [], meta, loadWarns: ok ? imp.warns : [] };
}
export function record() {
  const P = core.P;
  return { v: 3, id: P.id, name: P.name, key: P.key, base: P.base, hol: P.hol, scn: P.scn, scenarios: P.meta.scenarios, ui: P.ui, res: P.res, csv: CsvIO.exportCSV(P.issues, P.meta, {}) };
}
function persist() { core.saveFailed = !Store.save(record()); Store.setCurrent(core.P.id); fsQueue(core.P.id); }
function saveRec(rec) { const ok = Store.save(rec); fsQueue(rec.id); return ok; }   // 新しく作る・取り込むプロジェクト用（フォルダにも書く）
const newRecord = (name, key, csv, base) => ({ v: 3, id: Store.newId(), name, key: key || "", base: base || todayIso(), hol: HOL0, scn: "標準", scenarios: [], ui: defaultUI(), csv });
const EMPTY_CSV = "Issue Id,Issue Type,Summary,Parent,Priority,Status,Start date,Due date,Labels,Description,Outward issue link (Blocks),Deadline,Duration (weeks)\r\n";

/* ---------- 計算の更新・編集（取り消しつき） ---------- */
function rebuild() {
  const P = core.P;
  core.cal = Sched.make(holidaysFromText(P.hol));
  P.meta.scenarios = P.meta.scenarios || [];
  core.T = Engine.build(P.issues, P.meta);
  if (core.T.error) { core.R = null; core.error = core.T.error; return core.T.error; }
  core.error = null;
  if (core.T.scenarios.indexOf(P.scn) < 0) P.scn = "標準";
  core.R = Engine.compute(core.T, core.cal, P.base, P.scn, { level: P.res });
  return null;
}
const snapshot = () => JSON.stringify({ issues: core.P.issues, meta: core.P.meta, scn: core.P.scn });
const restore = (s) => { const o = JSON.parse(s); core.P.issues = o.issues; core.P.meta = o.meta; core.P.scn = o.scn; };
export function mutate(fn, post) {
  const snap = snapshot();
  fn();
  let err = rebuild();
  if (!err && post) { post(); err = rebuild(); }
  if (err) { restore(snap); rebuild(); toast("その編集はできません: " + err); emit(); return false; }
  if (snapshot() !== snap) { core.undoS.push(snap); if (core.undoS.length > 100) core.undoS.shift(); core.redoS = []; }
  persist(); emit(); return true;
}
export function undo() { if (!core.undoS.length) return false; core.redoS.push(snapshot()); restore(core.undoS.pop()); rebuild(); persist(); emit(); return true; }
export function redo() { if (!core.redoS.length) return false; core.undoS.push(snapshot()); restore(core.redoS.pop()); rebuild(); persist(); emit(); return true; }

export function openProject(id) {
  const rec = Store.load(id); if (!rec) return false;
  hydrate(rec); core.undoS = []; core.redoS = [];
  const err = rebuild(); if (err) toast("このプロジェクトを計算できません: " + err);
  Store.setCurrent(id); emit(); return !err;
}
export function init() {
  let idx = Store.index();
  if (!idx.order.length) {
    const a = newRecord("サンプル（法規対応）", "", SAMPLE_CSV, "2026-10-19");
    const b = newRecord("階層デモ（セキュアブート）", "DEV", DEMO_CSV, "2026-10-19");
    Store.save(a); Store.save(b); Store.setCurrent(a.id); idx = Store.index();
    fsMeta(a.id).seed = true; fsMeta(b.id).seed = true; fsmSave();   // 手を付けていないサンプルはフォルダに書き出さない
  }
  openProject(idx.current || idx.order[0]);
}
export const projectList = () => Store.list();

/* ---------- 設定の変更（取り消しの対象外） ---------- */
export function setUI(patch) { Object.assign(core.P.ui, patch); persist(); emit(); }
export function setProject(patch, recompute) { Object.assign(core.P, patch); if (recompute) rebuild(); persist(); emit(); }
export function setRes(patch) { Object.assign(core.P.res, patch); rebuild(); persist(); emit(); }
export function addScenario(name) {
  name = String(name || "").trim(); if (!name || name === "標準") return;
  const P = core.P; if (P.meta.scenarios.indexOf(name) < 0) P.meta.scenarios.push(name);
  P.scn = name; rebuild(); persist(); emit();
  toast("シナリオ「" + name + "」を追加しました。期間を変えると、このシナリオだけに保存されます");
}

/* ---------- 課題の参照 ---------- */
export const byId = (id) => core.T && core.T.byId[id];
export function resolveRef(str) {
  const { P, T } = core; str = String(str || "").trim(); if (!str) return null;
  let hit = null; const low = str.toLowerCase();
  P.issues.forEach((o) => { if (!hit && (T.did[o.id] === str || o.key === str || o.id === str)) hit = o.id; });
  if (hit) return hit;
  P.issues.forEach((o) => { if (!hit && (T.did[o.id].toLowerCase() === low || o.key.toLowerCase() === low)) hit = o.id; });
  if (hit) return hit;
  const m = P.issues.filter((o) => T.name[o.id].toLowerCase().indexOf(low) >= 0);
  return m.length === 1 ? m[0].id : null;
}
export function descendantsOf(id) { const out = [], st = [id]; while (st.length) { const x = st.pop(); (core.T.kids[x] || []).forEach((k) => { out.push(k); st.push(k); }); } return out; }
export const siblings = (id) => { const p = core.T.parentOf[id]; return p ? core.T.kids[p] : core.T.roots; };

/* ---------- 課題の操作 ---------- */
const nextId = () => { let m = 0; core.P.issues.forEach((o) => { if (/^\d+$/.test(o.id)) m = Math.max(m, +o.id); }); return String(m + 1); };
function newIssue(parentId, name) {
  const { P, T } = core, id = nextId(), p = parentId ? T.byId[parentId] : null;
  const type = !p ? "Epic" : (Engine.isEpic(p) ? "Task" : "Sub-task");
  return { id, key: P.key ? P.key + "-" + id : "", type, summary: name || "新しい課題", parent: parentId || "", prio: "Medium", status: "To Do", assignee: "",
    start: null, due: null, deadline: null, resolved: null, notBefore: null, comments: [], durW: 1, estW: null, remW: null, alt: {}, labels: [], desc: "", blocks: [],
    extra: (P.meta.extraHeaders || []).map(() => "") };
}
// afterId があればその兄弟の直後（配下の最後の後ろ）に、なければ末尾に入れる。作った課題のidを返す。
export function addIssue(parentId, afterId, name) {
  const o = newIssue(parentId, name);
  const ok = mutate(() => {
    const P = core.P;
    if (afterId) {
      const i = P.issues.findIndex((x) => x.id === afterId); let j = i + 1; const sub = {}; sub[afterId] = true;
      while (j < P.issues.length && sub[P.issues[j].parent]) { sub[P.issues[j].id] = true; j++; }
      P.issues.splice(j, 0, o);
    } else P.issues.push(o);
    if (parentId) P.ui.collapsed[parentId] = false;
  });
  return ok ? o.id : null;
}
export function deleteIssues(ids) {
  const gone = {}; ids.forEach((id) => { gone[id] = true; descendantsOf(id).forEach((k) => { gone[k] = true; }); });
  const n = Object.keys(gone).length;
  const ok = mutate(() => {
    core.P.issues = core.P.issues.filter((o) => !gone[o.id]);
    core.P.issues.forEach((o) => { o.blocks = o.blocks.filter((b) => !gone[b]); });
  });
  if (ok) toast(n + "件を削除しました", { label: "元に戻す", fn: undo });
  return ok;
}
export function moveIssue(id, dir) {
  const sb = siblings(id), i = sb.indexOf(id), j = i + dir; if (j < 0 || j >= sb.length) return false;
  return mutate(() => {
    const P = core.P, a = P.issues.findIndex((o) => o.id === sb[i]), b = P.issues.findIndex((o) => o.id === sb[j]);
    const t = P.issues[a]; P.issues[a] = P.issues[b]; P.issues[b] = t;
  });
}
export function reparent(id, newParent) {
  return mutate(() => {
    const P = core.P, o = core.T.byId[id], idx = P.issues.indexOf(o); P.issues.splice(idx, 1); o.parent = newParent || ""; P.issues.push(o);
    if (newParent) P.ui.collapsed[newParent] = false;
  });
}
export function indent(id) { const sb = siblings(id), i = sb.indexOf(id); return i > 0 ? reparent(id, sb[i - 1]) : false; }
export function outdent(id) {
  const p = core.T.parentOf[id]; if (!p) return false;
  return mutate(() => {
    const P = core.P, o = core.T.byId[id], gp = core.T.parentOf[p] || "";
    P.issues.splice(P.issues.indexOf(o), 1); o.parent = gp;
    const pi = P.issues.findIndex((x) => x.id === p); P.issues.splice(pi + 1, 0, o);
  });
}
// 行を別の行の前/後/子へ（ドラッグでの並べ替え用）
export function relocate(id, targetId, where) {
  if (id === targetId || descendantsOf(id).indexOf(targetId) >= 0) return false;
  return mutate(() => {
    const P = core.P, T = core.T, o = T.byId[id];
    P.issues.splice(P.issues.indexOf(o), 1);
    if (where === "into") { o.parent = targetId; P.issues.push(o); P.ui.collapsed[targetId] = false; return; }
    const t = T.byId[targetId]; o.parent = t.parent;
    let ti = P.issues.indexOf(t);
    if (where === "after") { let j = ti + 1; const sub = {}; sub[targetId] = true; while (j < P.issues.length && sub[P.issues[j].parent]) { sub[P.issues[j].id] = true; j++; } ti = j; }
    P.issues.splice(ti, 0, o);
  });
}
export function setField(id, field, value) { return mutate(() => { core.T.byId[id][field] = value; }); }
export function setStatus(id, st) {
  return mutate(() => {
    const o = core.T.byId[id], cat = Engine.statusCat(st); o.status = st;
    const today = CsvIO.parseDate(todayIso()), base = Sched.parse(core.P.base);
    if (cat === "done" && o.resolved == null) o.resolved = Math.min(today, base);
    if (cat === "prog" && o.start == null) o.start = Math.min(today, base);
  });
}
// 表示用の番号（件名の先頭の [B-1] の部分）を変える。空にすると、キーかIDの表示に戻る。
export function setTag(id, tag) {
  tag = String(tag || "").replace(/[\[\]]/g, "").trim();
  const T = core.T, o = T.byId[id], cur = Engine.tagOf(o.summary);
  if (tag === cur.tag) return true;
  if (tag) { const dup = core.P.issues.find((x) => x.id !== id && T.did[x.id].toLowerCase() === tag.toLowerCase()); if (dup) { toast("「" + tag + "」はすでに別の課題（" + T.name[dup.id] + "）で使われています"); return false; } }
  return mutate(() => { o.summary = (tag ? "[" + tag + "] " : "") + cur.name; });
}
export function setName(id, name) { return mutate(() => { const o = core.T.byId[id], t = Engine.tagOf(o.summary); o.summary = (t.tag ? "[" + t.tag + "] " : "") + (name || "（無題）"); }); }
export function setDuration(id, w) { return mutate(() => { const o = core.T.byId[id]; if (core.P.scn === "標準") o.durW = w; else o.alt[core.P.scn] = w; }); }
export function removeScenarioValue(id) { return mutate(() => { delete core.T.byId[id].alt[core.P.scn]; }); }
export function setEarliest(id, day) {
  return mutate(() => { core.T.byId[id].notBefore = day; }, () => {
    if (day != null && core.R[id].asapStart !== day) { core.T.byId[id].notBefore = null; toast("その日には動かせません（先行や、同じ担当者の作業が終わるまで待つため）"); }
  });
}
export function setDeadline(id, day) {
  return mutate(() => {
    const P = core.P;
    // Deadline 列の無いCSVでは、これまで期限として使っていた Due date を Deadline 列へ移してから設定する
    if (!P.meta.hasDeadlineCol) { P.issues.forEach((x) => { if (core.T.ownDl[x.id] != null) x.deadline = core.T.ownDl[x.id]; }); P.meta.hasDeadlineCol = true; }
    core.T.byId[id].deadline = day;
  });
}
export function addLink(fromId, toId) {
  if (fromId === toId) { toast("自分自身には依存できません"); return false; }
  if (core.T.byId[fromId].blocks.indexOf(toId) >= 0) { toast("すでに設定されています"); return false; }
  return mutate(() => { core.T.byId[fromId].blocks.push(toId); });
}
export function removeLink(fromId, toId) { return mutate(() => { const o = core.T.byId[fromId]; o.blocks = o.blocks.filter((b) => b !== toId); }); }
export function addComment(id, text, who) {
  text = String(text || "").trim(); if (!text) return false;
  who = (who || "自分").replace(/;/g, "；"); setPref("me", who);
  return mutate(() => { const o = core.T.byId[id]; (o.comments = o.comments || []).push({ at: nowDT(), who, text }); });
}
export function delComment(id, i) { return mutate(() => { core.T.byId[id].comments.splice(i, 1); }); }
// 複数の課題へまとめて適用
export function bulk(ids, fn) { return mutate(() => { ids.forEach((id) => fn(core.T.byId[id])); }); }

/* ---------- テンプレート（子タスクの雛形） ----------
   items: [{ n: 名前, w: 期間(週), lv: 入れ子の深さ(0=直下の子), after: [先行する項目の番号…], t: 種別(省略可), lab: [ラベル], d: 説明 }]
   名前の {名前} は追加先の課題名に置き換わる。追加先に [B-1] のような番号があれば、子は [B-1-1] … と続けて振る。 */
const PH = ["期間仮置き"];
const BUILTIN_TEMPLATES = [
  { id: "b:doc", builtin: true, name: "外部認証向けの文書対応", desc: "内部レビュー → 英訳 → 外部提出 → 外部指摘の対応", items: [
    { n: "内部レビューと指摘修正", w: 1, lv: 0, after: [], lab: PH }, { n: "英訳", w: 1, lv: 0, after: [0], lab: PH },
    { n: "外部認証機関への提出", w: 0.2, lv: 0, after: [1], lab: PH }, { n: "外部指摘への対応とクローズ", w: 2, lv: 0, after: [2], lab: PH }] },
  { id: "b:eval", builtin: true, name: "評価の実施と報告", desc: "評価方案 → 評価実施 → 報告書 → 提出と指摘クローズ", items: [
    { n: "評価方案の作成", w: 1, lv: 0, after: [], lab: PH }, { n: "評価の実施", w: 1, lv: 0, after: [0], lab: PH },
    { n: "評価報告書の作成", w: 1, lv: 0, after: [1], lab: PH }, { n: "評価報告書の提出と指摘クローズ", w: 1, lv: 0, after: [2], lab: PH }] },
  { id: "b:review", builtin: true, name: "作成 → レビュー → 承認", desc: "成果物をつくり、レビューして承認を得る基本形", items: [
    { n: "{名前}の作成", w: 2, lv: 0, after: [], lab: PH }, { n: "レビュー", w: 1, lv: 0, after: [0], lab: PH },
    { n: "指摘の修正", w: 1, lv: 0, after: [1], lab: PH }, { n: "承認", w: 0.2, lv: 0, after: [2], lab: PH }] },
  { id: "b:publish", builtin: true, name: "公開用の原稿づくり", desc: "原稿作成 → レビュー → 校了 → 公開（印刷）", items: [
    { n: "公開用原稿の作成", w: 2, lv: 0, after: [], lab: PH }, { n: "レビュー", w: 1, lv: 0, after: [0], lab: PH },
    { n: "校了", w: 0.4, lv: 0, after: [1], lab: PH }, { n: "公開・印刷", w: 0.4, lv: 0, after: [2], lab: PH }] },
];
export const templates = () => BUILTIN_TEMPLATES.concat(core.prefs.templates || []);
const userTemplates = () => (core.prefs.templates || []).slice();
// 課題の配下（子・孫…）を雛形の項目にする。件名の番号タグ（[B-1-2]）は外し、先行関係は項目の番号に直す。
export function templateItemsFrom(id, withWeeks) {
  const { T } = core, ids = T.dfs.filter((x) => descendantsOf(id).indexOf(x) >= 0), at = {};
  ids.forEach((x, i) => { at[x] = i; });
  return ids.map((x) => {
    const o = T.byId[x], leaf = !T.kids[x].length;
    const it = { n: T.name[x], w: leaf && withWeeks ? (o.durW != null ? o.durW : (o.estW != null ? o.estW : 1)) : (leaf ? 1 : null), lv: T.depth[x] - T.depth[id] - 1,
      after: ids.filter((m) => T.byId[m].blocks.indexOf(x) >= 0).map((m) => at[m]), lab: (o.labels || []).slice() };
    if (/milestone|マイルストーン/i.test(o.type || "")) it.t = "Milestone";
    if (o.desc) it.d = o.desc;
    return it;
  });
}
export function saveTemplate(name, items, desc) {
  name = String(name || "").trim(); if (!name || !items.length) return false;
  const list = userTemplates(), t = { id: "u:" + Date.now().toString(36) + Math.random().toString(36).slice(2, 5), name, desc: desc || (items.length + "件の子タスク"), items };
  list.push(t); setPref("templates", list); toast("テンプレート「" + name + "」を保存しました"); return t.id;
}
export function deleteTemplate(id) { setPref("templates", userTemplates().filter((t) => t.id !== id)); }
// 複数の課題に、同じ雛形の子タスクをまとめて追加する（取り消しは1回で戻る）
export function applyTemplate(parentIds, tpl) {
  const items = (tpl && tpl.items) || []; if (!items.length || !parentIds.length) return 0;
  let made = 0;
  const ok = mutate(() => {
    const P = core.P, T = core.T;
    parentIds.forEach((pid) => {
      const par = P.issues.find((x) => x.id === pid); if (!par) return;
      const ptag = Engine.tagOf(par.summary).tag, pname = T.name[pid] || par.summary;
      const cnt = {}; cnt[pid] = (T.kids[pid] || []).length;
      const tagOfId = {}; tagOfId[pid] = ptag;
      let at = P.issues.indexOf(par) + 1; const sub = {}; sub[pid] = true;
      while (at < P.issues.length && sub[P.issues[at].parent]) { sub[P.issues[at].id] = true; at++; }
      const made1 = [], lastAt = [];
      items.forEach((it, i) => {
        const lv = Math.max(0, Math.min(it.lv || 0, lastAt.length)), up = lv === 0 ? pid : lastAt[lv - 1], upObj = lv === 0 ? par : made1.find((m) => m.id === up);
        const o = newIssue(up, it.n);
        o.type = it.t || (Engine.isEpic(upObj) ? "Task" : "Sub-task");
        cnt[up] = (cnt[up] || 0) + 1;
        const tag = tagOfId[up] ? tagOfId[up] + "-" + cnt[up] : "";
        tagOfId[o.id] = tag;
        o.summary = (tag ? "[" + tag + "] " : "") + String(it.n).replace(/\{名前\}/g, pname);
        o.durW = it.w != null ? it.w : 1; o.labels = (it.lab || []).slice(); o.desc = it.d || "";
        P.issues.splice(at++, 0, o); made1.push(o); lastAt.length = lv; lastAt[lv] = o.id; made++;
      });
      items.forEach((it, i) => (it.after || []).forEach((j) => { if (made1[j] && j !== i && made1[j].blocks.indexOf(made1[i].id) < 0) made1[j].blocks.push(made1[i].id); }));
      P.ui.collapsed[pid] = false;
    });
  });
  if (ok) toast(made + "件の子タスクを追加しました", { label: "元に戻す", fn: undo });
  return ok ? made : 0;
}

/* ---------- 一覧・集計 ---------- */
// 絞り込みに合う課題の集合。filter: {q, asg, flt}
function visSet(filter) {
  const { T, R } = core;
  const q = (filter.q || "").trim().toLowerCase(), asg = filter.asg || "", flt = filter.flt || "", st = filter.st || "", pr = filter.pr || "";
  const filtering = !!(q || asg || flt || st || pr);
  const textOk = (id) => { if (!q) return true; const o = T.byId[id]; return (T.name[id] + " " + T.did[id] + " " + o.key + " " + o.labels.join(" ") + " " + o.assignee).toLowerCase().indexOf(q) >= 0; };
  const leafOk = (id, m) => {
    const o = T.byId[id], r = R[id];
    if (!m) return false;
    if (asg === "\u0000" ? !o.assignee : asg === "\u0001" ? !!o.assignee : asg && o.assignee !== asg) return false;   // \u0000=担当者あり / \u0001=未割当
    if (st && (st === "open" ? T.cat[id] === "done" : T.cat[id] !== st)) return false;
    if (pr && String(o.prio || "Medium").toLowerCase() !== pr) return false;
    if (flt === "late") return !r.done && r.float < 0;
    if (flt === "tight") return !r.done && r.float >= 0 && r.float <= 10;
    if (flt === "done") return r.done;
    if (flt === "open") return !r.done;
    return true;
  };
  const vis = {};
  const walk = (id, anc) => {
    const m = anc || textOk(id); let any = false;
    if (T.kids[id].length) T.kids[id].forEach((k) => { if (walk(k, m)) any = true; });
    else any = leafOk(id, m);
    if (any) vis[id] = true; return any;
  };
  T.roots.forEach((r) => walk(r, false));
  return { vis, filtering };
}
// 表示する行（ツリー順）。filter: {q, asg, flt}
export function visibleRows(filter) {
  const { T, P } = core; if (!T || T.error) return [];
  const { vis, filtering } = visSet(filter), lvl = P.ui.level || 0, rows = [];
  const emitRow = (id) => {
    if (!vis[id]) return;
    if (T.depth[id] === 0 && P.ui.hide[id]) return;
    const isPar = !!T.kids[id].length, open = !P.ui.collapsed[id] || filtering;
    rows.push({ id, depth: T.depth[id], isPar, open });
    if (isPar && open && (!lvl || T.depth[id] + 1 < lvl)) T.kids[id].forEach(emitRow);
  };
  T.roots.forEach(emitRow);
  return rows;
}
// 絞り込みに合う末端の課題（ボード用。開閉の状態には影響されない）
export function visibleLeaves(filter) {
  const { T, P } = core; if (!T || T.error) return [];
  const { vis } = visSet(filter);
  return T.dfs.filter((id) => !T.kids[id].length && vis[id] && !P.ui.hide[T.rootOf[id]]);
}
export function summary() {
  const { T, R } = core; const s = { late: 0, tight: 0, done: 0, total: 0 };
  if (!T || T.error) return s;
  T.leafOrder.forEach((id) => { const r = R[id]; s.total++; if (r.done) s.done++; else if (r.float < 0) s.late++; else if (r.float <= 10) s.tight++; });
  return s;
}
// ルート課題ごとの色番号（1〜7を順に使う。0は「Epicではない単独の課題」用のグレー）
export const rootIndexMap = () => { const m = {}; let n = 0; core.T.roots.forEach((r) => { if (core.T.kids[r].length || Engine.isEpic(core.T.byId[r])) m[r] = (n++ % 7) + 1; else m[r] = 0; }); return m; };

/* ---------- プロジェクトの操作 ---------- */
export function createProject(tpl, name, key) {
  const csv = tpl === "demo" ? DEMO_CSV : tpl === "reg" ? SAMPLE_CSV : EMPTY_CSV;
  const rec = newRecord(name || "新しいプロジェクト", (key || "").toUpperCase(), csv, tpl === "empty" ? todayIso() : "2026-10-19");
  saveRec(rec); openProject(rec.id); toast("「" + rec.name + "」を作りました");
}
export function editProject(name, key) { core.P.name = name || core.P.name; core.P.key = (key || "").toUpperCase(); persist(); emit(); }
export function dupProject() { const rec = record(); rec.id = Store.newId(); rec.name = core.P.name + " のコピー"; saveRec(rec); openProject(rec.id); toast("複製しました"); }
export function deleteProject() {
  const id = core.P.id, f = core.fs, m = fsmAll()[id];
  wDirty.delete(id);
  if (f.handle && m && m.file) {
    if (f.perm === "granted") {
      const stem = m.file;
      exclusive(() => Folder.remove(f.handle, stem)).then(() => { f.stems.delete(stem.toLowerCase()); toast("フォルダの「" + stem + ".csv」を「_削除済み」に移しました"); })
        .catch((e) => toast("フォルダのファイルを移せませんでした: " + errMsg(e)));
    } else toast("フォルダへの書き込み許可がないため、フォルダのファイルは残っています");
  }
  delete fsmAll()[id]; fsmSave();
  Store.remove(core.P.id);
  let idx = Store.index();
  if (!idx.order.length) { Store.save(newRecord("新しいプロジェクト", "", EMPTY_CSV)); idx = Store.index(); }
  openProject(idx.current || idx.order[0]);
}
export function doImport(text, name, mode) {
  if (/^\s*\{/.test(text)) return { json: true };
  const imp = CsvIO.importCSV(text);
  if (imp.error) return { error: imp.error };
  const T2 = Engine.build(imp.issues, imp.meta);
  if (T2.error) return { error: T2.error };
  if (mode === "replace") {
    const snap = snapshot(), P = core.P;
    P.issues = imp.issues; P.meta = imp.meta; P.ui.collapsed = {}; P.ui.hide = {}; P.scn = "標準";
    rebuild(); core.undoS.push(snap); core.redoS = []; P.loadWarns = imp.warns; persist(); emit();
    toast(P.issues.length + "件を読み込みました（置き換え）");
  } else {
    const rec = newRecord(name || "取り込んだCSV", "", text, todayIso()); saveRec(rec); openProject(rec.id); toast(imp.issues.length + "件を読み込みました");
  }
  return { ok: true };
}
// JSON（バックアップ・共有ファイル）の解析。競合があれば conflicts を返し、run(overwrite) で確定する。
export function parseBackup(text) {
  let o; try { o = JSON.parse(text); } catch (e) { return { error: "JSONを読み取れません" }; }
  if (!o || o.app !== "regroadmap" || !Array.isArray(o.projects)) return { error: "このアプリのバックアップ・共有ファイルではありません" };
  const list = o.projects.filter((p) => p && p.csv);
  if (!list.length) return { error: "プロジェクトが含まれていません" };
  const conflicts = list.filter((p) => p.id && Store.load(p.id));
  const run = (overwrite) => {
    let first = null;
    list.forEach((p) => { const same = p.id && Store.load(p.id); const rec = Object.assign({}, p, { id: same && overwrite ? p.id : Store.newId() }); saveRec(rec); if (!first) first = rec.id; });
    const have = {}; userTemplates().forEach((t) => { have[t.id] = true; });
    const add = (Array.isArray(o.templates) ? o.templates : []).filter((t) => t && t.id && Array.isArray(t.items) && t.name && !have[t.id] && !/^b:/.test(t.id));
    if (add.length) setPref("templates", userTemplates().concat(add));
    openProject(first); toast(list.length + "件のプロジェクトを読み込みました" + (add.length ? "（テンプレート " + add.length + "件を含む）" : ""));
  };
  return { conflicts, run };
}
export function exportText(kind) {   // kind: "jira" | "app" | "share" | "backup"
  const { P, T, R } = core;
  if (kind === "backup") { const b = JSON.parse(Store.backup()); b.templates = core.prefs.templates || []; return JSON.stringify(b, null, 1); }
  if (kind === "share") return JSON.stringify({ app: "regroadmap", version: 3, exported: new Date().toISOString(), projects: [record()], templates: core.prefs.templates || [] }, null, 1);
  const planned = kind === "jira";
  return CsvIO.exportCSV(P.issues, P.meta, { planned,
    plan: (id) => { const r = R[id]; return r ? { start: r.asapStart, finish: r.asapFinish, leaf: r.leaf, weeks: r.weeks, baseWeeks: T.baseWeeks[id] } : null; },
    ownDeadline: (id) => (T.ownDl[id] != null ? T.ownDl[id] : null) });
}
export async function saveFile(name, text, mime) {
  name = name.replace(/[\\/:*?"<>|]/g, "_");
  try {
    if (window.claude && window.claude.use) {
      const d = await window.claude.use("downloads");
      if (d) { await d.save({ filename: name, data: text }); return "saved"; }
    }
  } catch (e) { if (e && e.code === "declined") return "declined"; }
  try {
    const b = new Blob([text], { type: mime + ";charset=utf-8" }), a = document.createElement("a");
    a.href = URL.createObjectURL(b); a.download = name; document.body.append(a); a.click(); a.remove(); return "saved";
  } catch (e2) { return "failed"; }
}
export async function readFile(file) {
  const buf = await file.arrayBuffer();
  try { return new TextDecoder("utf-8", { fatal: true }).decode(buf); } catch (e) { try { return new TextDecoder("shift_jis").decode(buf); } catch (e2) { return new TextDecoder().decode(buf); } }
}

/* ---------- 表示用の小道具 ---------- */
export const DOW = ["日", "月", "火", "水", "木", "金", "土"];
export const D = (n) => Sched.fmt(n);
export const fmtD = (n) => { const p = D(n).split("-"); return p[0] + "/" + (+p[1]) + "/" + (+p[2]) + "(" + DOW[Sched.dow(n)] + ")"; };
export const fmtShort = (n) => { const p = D(n).split("-"); return (+p[1]) + "/" + (+p[2]); };
export const wk = (fl) => Math.max(1, Math.round(Math.abs(fl) / 5));
export const num = (v) => Math.round(v * 100) / 100;
export const statKey = (fl, done) => (done ? "done" : (fl < 0 ? "late" : (fl <= 10 ? "tight" : "ok")));
export const floatText = (r) => (r.done ? "完了" : (r.float < 0 ? "−" + wk(r.float) + "週" : r.float + "日"));
export const initials = (n) => { n = String(n || "").trim(); return n ? (/[　-鿿]/.test(n) ? n.slice(0, 1) : n.slice(0, 2).toUpperCase()) : ""; };
export const workdaysIn = (a, b) => { let c = 0; for (let d = a; d <= b; d++) if (core.cal.isWork(d)) c++; return c; };

/* ---------- 作業フォルダとの連携（CSVを原本として自動で書き込む） ---------- */
// 1プロジェクト = 「名前.csv」+「名前.settings.json」。編集のたびに少し待ってから書き込む。ブラウザ内の保存（localStorage）は予備として残す。
// 対応づけの記録（ファイル名・最後に書いた時刻・未書き込みの印）は、ブラウザごとに localStorage の別のキーに持つ。
const FSK = "regroadmap.v3.fsmeta";
let fsm = null;
function fsmAll() { if (!fsm) { try { fsm = JSON.parse(localStorage.getItem(FSK) || "{}") || {}; } catch (e) { fsm = {}; } } return fsm; }
function fsmSave() { try { localStorage.setItem(FSK, JSON.stringify(fsmAll())); } catch (e) {} }
function fsMeta(id) { const a = fsmAll(); return a[id] || (a[id] = {}); }
const errMsg = (e) => (e && (e.name ? e.name + ": " : "") + (e.message || "")) || String(e);
const safeStem = (s) => String(s || "").replace(/[\\/:*?"<>|\u0000-\u001f]/g, "_").replace(/^[.\s]+|[.\s]+$/g, "").slice(0, 80) || "project";
function freeStem(name, id) {
  const base = safeStem(name), used = new Set(core.fs.stems), all = fsmAll();
  Object.keys(all).forEach((k) => { if (k !== id && all[k].file) used.add(all[k].file.toLowerCase()); });
  let s = base, n = 2; while (used.has(s.toLowerCase())) s = base + "-" + n++;
  return s;
}
const settingsOf = (rec) => ({ app: "regroadmap", version: 3, id: rec.id, name: rec.name, key: rec.key, base: rec.base, hol: rec.hol, scn: rec.scn, scenarios: rec.scenarios, res: rec.res, ui: rec.ui });
function recFromFile(r, stem, base) {
  if (CsvIO.parse(r.csv || "").rows.length) {
    const imp = CsvIO.importCSV(r.csv);
    if (imp.error || Engine.build(imp.issues, imp.meta).error) return null;
  }
  const s = r.settings || {}, b = base || {};
  return { v: 3, id: s.id || b.id || Store.newId(), name: s.name || b.name || stem, key: s.key != null ? s.key : (b.key || ""), base: s.base || b.base || todayIso(),
    hol: s.hol != null ? s.hol : (b.hol != null ? b.hol : HOL0), scn: s.scn || b.scn || "標準", scenarios: s.scenarios || b.scenarios || [],
    ui: s.ui || b.ui || defaultUI(), res: s.res || b.res, csv: r.csv };
}

// フォルダへの操作は1本ずつ順番に行う（書き込みと読み込みが重ならないように）
let fsChain = Promise.resolve();
function exclusive(fn) { const p = fsChain.then(fn); fsChain = p.catch(() => {}); return p; }

const wDirty = new Set(), lastSig = {};
let wTimer = null;
function fsQueue(id) {
  const m = fsMeta(id); delete m.seed; m.dirty = true; fsmSave();
  const f = core.fs; if (!f.handle || f.perm !== "granted") return;   // 許可待ちのあいだは印だけ付け、許可後にまとめて書く
  wDirty.add(id); clearTimeout(wTimer); wTimer = setTimeout(() => exclusive(doFlush), 400);
  f.state = "saving";
}
function fsFail(e) {
  const f = core.fs;
  if (e && (e.name === "NotAllowedError" || e.name === "SecurityError")) { f.perm = "prompt"; f.state = "need"; f.msg = ""; }
  else { f.state = "error"; f.msg = errMsg(e); }
}
async function doFlush() {
  const f = core.fs, ids = Array.from(wDirty); wDirty.clear();
  if (!ids.length || !f.handle || f.perm !== "granted") return;
  try {
    for (const id of ids) {
      const rec = core.P && id === core.P.id ? record() : Store.load(id); if (!rec) continue;
      const m = fsMeta(id), sig = rec.csv + "\n" + JSON.stringify(settingsOf(rec));
      if (m.file && lastSig[id] === sig) { if (!wDirty.has(id)) m.dirty = false; continue; }   // 内容が変わっていなければ書かない
      if (!m.file) m.file = freeStem(rec.name, id);
      m.mtime = await Folder.write(f.handle, m.file, rec.csv, settingsOf(rec));
      lastSig[id] = sig; f.stems.add(m.file.toLowerCase());
      if (!wDirty.has(id)) m.dirty = false;
    }
    fsmSave(); f.state = wDirty.size ? "saving" : "idle"; f.saved = Date.now(); f.msg = "";
  } catch (e) { ids.forEach((i) => wDirty.add(i)); fsFail(e); }
  emit();
}

let lastSync = 0;
export function fsSync(opt) { return exclusive(() => doSync(opt || {})); }
async function doSync(opt) {
  const f = core.fs; if (!f.handle || f.perm !== "granted") return;
  f.state = "saving"; emit();
  const sum = { loaded: 0, wrote: 0, added: 0, kept: 0, bad: [] };
  try {
    const entries = await Folder.list(f.handle);
    f.stems = new Set(entries.map((e) => e.stem.toLowerCase()));
    const byStem = {}; entries.forEach((e) => { byStem[e.stem.toLowerCase()] = e; });
    const local = {}; Store.index().order.forEach((id) => { const r = Store.load(id); if (r) local[id] = r; });
    const cache = {}, rd = async (stem) => cache[stem] || (cache[stem] = await Folder.read(f.handle, stem));
    // 1) 対応づけ: 記録したファイル名 → settings の id の順
    const pairs = [], paired = new Set(), usedStem = new Set();
    Object.keys(local).forEach((id) => { const m = fsMeta(id), e = m.file && byStem[m.file.toLowerCase()]; if (e) { pairs.push({ id, e }); paired.add(id); usedStem.add(e.stem); } });
    const fresh = [];
    for (const e of entries) {
      if (usedStem.has(e.stem)) continue;
      const r = await rd(e.stem); if (!r) continue;
      const sid = r.settings && r.settings.id;
      if (sid && local[sid] && !paired.has(sid)) { fsMeta(sid).file = e.stem; pairs.push({ id: sid, e }); paired.add(sid); usedStem.add(e.stem); } else fresh.push(e);
    }
    // 2) フォルダにだけあるCSVは、新しいプロジェクトとして追加
    let firstNew = null;
    for (const e of fresh) {
      const rec = recFromFile(await rd(e.stem), e.stem, null);
      if (!rec) { sum.bad.push(e.stem); continue; }
      if (Store.load(rec.id)) rec.id = Store.newId();
      Store.save(rec); const m = fsMeta(rec.id); m.file = e.stem; m.mtime = e.mtime; m.dirty = false; sum.added++; firstNew = firstNew || rec.id;
    }
    // 3) 対応づいたもの: 新しい方を採る。両方変わっていたら、フォルダの版を別プロジェクトとして残す
    const toWrite = []; let reopen = false;
    for (const { id, e } of pairs) {
      const m = fsMeta(id), fileNew = e.mtime > (m.mtime || 0);
      if (fileNew && !m.dirty) {
        const rec = recFromFile(await rd(e.stem), e.stem, local[id]);
        if (!rec) { sum.bad.push(e.stem); continue; }
        rec.id = id; Store.save(rec); m.mtime = e.mtime; sum.loaded++; delete lastSig[id];
        if (core.P && core.P.id === id) reopen = true;
      } else if (m.dirty) {
        if (fileNew) {
          const rec = recFromFile(await rd(e.stem), e.stem, local[id]);
          if (rec) { rec.id = Store.newId(); rec.name += "（フォルダの版）"; Store.save(rec); fsMeta(rec.id).dirty = true; toWrite.push(rec.id); sum.kept++; }
        }
        toWrite.push(id);
      }
    }
    // 4) ブラウザにだけあるものはフォルダへ書き出す。手を付けていないサンプルは、フォルダに実データがあれば消す
    const hasReal = entries.length > 0 || sum.added > 0;
    Object.keys(local).forEach((id) => {
      if (paired.has(id)) return;
      if (fsMeta(id).seed) {
        if (hasReal) { Store.remove(id); delete fsmAll()[id]; if (core.P && core.P.id === id) reopen = true; }
        return;
      }
      toWrite.push(id);
    });
    for (const id of toWrite) {
      const rec = Store.load(id); if (!rec) continue;
      const m = fsMeta(id); if (!m.file) m.file = freeStem(rec.name, id);
      m.mtime = await Folder.write(f.handle, m.file, rec.csv, settingsOf(rec)); m.dirty = false; f.stems.add(m.file.toLowerCase());
      lastSig[id] = rec.csv + "\n" + JSON.stringify(settingsOf(rec)); sum.wrote++;
    }
    fsmSave();
    if (reopen) { const idx = Store.index(); openProject(core.P && Store.load(core.P.id) ? core.P.id : (firstNew || idx.current || idx.order[0])); }
    f.state = "idle"; f.saved = Date.now(); f.msg = ""; lastSync = Date.now();
  } catch (e) { fsFail(e); emit(); return; }
  emit();
  const parts = [];
  if (sum.loaded) parts.push("フォルダの更新を読み込み " + sum.loaded + "件");
  if (sum.added) parts.push("フォルダから追加 " + sum.added + "件");
  if (sum.wrote) parts.push("フォルダへ書き出し " + sum.wrote + "件");
  if (sum.kept) parts.push("両方で変更があったため、フォルダ側の内容を「…（フォルダの版）」として残しました");
  if (sum.bad.length) parts.push("読めないCSV: " + sum.bad.join(", ") + "（上書きしていません）");
  if (parts.length) toast(parts.join(" / "));
  else if (opt.manual) toast("フォルダに新しい変更はありません");
}
export async function fsInit() {
  const f = core.fs; if (!f.supported) return;
  const h = await Folder.loadHandle(); if (!h) return;
  f.handle = h; f.name = h.name;
  f.perm = await Folder.permission(h, false);
  f.state = f.perm === "granted" ? "idle" : "need"; emit();
  if (f.perm === "granted") await fsSync({});
  document.addEventListener("visibilitychange", () => {
    if (!f.handle) return;
    if (document.visibilityState === "hidden") { if (wDirty.size) { clearTimeout(wTimer); exclusive(doFlush); } return; }
    if (f.perm === "granted" && Date.now() - lastSync > 3000) fsSync({});   // 戻ってきたとき、フォルダ側の変更（他のPC・Excelなど）を取り込む
  });
}
export async function fsLink() {
  let h; try { h = await Folder.pick(); } catch (e) { if (e && e.name !== "AbortError") toast("フォルダを選べませんでした: " + errMsg(e)); return; }
  const f = core.fs;
  f.handle = h; f.name = h.name; f.stems = new Set(); wDirty.clear();
  const all = fsmAll(); Object.keys(all).forEach((id) => { delete all[id].file; delete all[id].mtime; }); fsmSave();   // 別のフォルダなら対応づけを作り直す
  f.perm = await Folder.permission(h, true); f.state = f.perm === "granted" ? "idle" : "need"; emit();
  if (f.perm === "granted") await fsSync({ manual: true });
}
export async function fsGrant() {   // 必ずクリックなどの操作から呼ぶこと
  const f = core.fs; if (!f.handle) return;
  f.perm = await Folder.permission(f.handle, true); f.state = f.perm === "granted" ? "idle" : "need"; emit();
  if (f.perm === "granted") await fsSync({});
}
export async function fsUnlink() {
  const f = core.fs; await Folder.forget(); clearTimeout(wTimer); wDirty.clear();
  Object.assign(f, { handle: null, name: "", perm: "none", state: "off", msg: "", stems: new Set() });
  const all = fsmAll(); Object.keys(all).forEach((id) => { delete all[id].file; delete all[id].mtime; }); fsmSave();
  emit(); toast("フォルダとの連携を解除しました。ブラウザ内のデータは残っています");
}
// 保存待ちのまま閉じようとしたときに止めるための確認用
export const fsPending = () => wDirty.size > 0 || core.fs.state === "saving";
