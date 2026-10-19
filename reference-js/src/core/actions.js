// 画面の操作（キーボード・メニュー・コマンドパレット共通）。core の編集と、選択・編集中の状態をまとめて扱う。
import { core, addIssue, deleteIssues, indent, outdent, moveIssue, setStatus, setUI, setPref, undo, redo, toast, bulk, descendantsOf, setProject, setRes, openProject, projectList, dupProject } from "./core.js";
import { ui, setUi, selectOnly, openModal, closeModal } from "./state.js";

export function startEdit(id, cell) { selectOnly(id, { scroll: true }); setUi({ editing: id, editCell: cell || "name" }); }
export function stopEdit() { if (ui.editing) setUi({ editing: null, fresh: null }); }

function addAndEdit(parentId, afterId) {
  const nid = addIssue(parentId, afterId);
  if (nid) { selectOnly(nid, { scroll: true }); setUi({ editing: nid, editCell: "name", fresh: nid }); }
  return nid;
}
export const addRootIssue = () => {
  // 何も選ばれていなければ末尾のルートの後ろへ
  const roots = core.T && core.T.roots; return addAndEdit("", roots && roots.length ? roots[roots.length - 1] : "");
};
export const addSibling = (id) => (id ? addAndEdit(core.T.parentOf[id] || "", id) : addRootIssue());
export const addChild = (id) => addAndEdit(id, null);

export function openDetail(id, tab) { if (id) selectOnly(id); setUi({ drawer: true, drawerTab: tab || ui.drawerTab }); }
export const closeDetail = () => setUi({ drawer: false });
export const toggleDetail = () => setUi({ drawer: !ui.drawer });

const selIds = () => ui.order.filter((x) => ui.sel.indexOf(x) >= 0).concat(ui.sel.filter((x) => ui.order.indexOf(x) < 0));
export function deleteSel() {
  if (!ui.sel.length) return;
  const ids = ui.sel.slice(); const nxt = ui.order[Math.max(0, ui.order.indexOf(ids[0]) - 1)];
  // 子孫を含めて一括で消す。消えた後の選択は直前の行へ
  if (deleteIssues(ids)) { const keep = nxt && core.T.byId[nxt] ? nxt : null; selectOnly(keep); }
}
export function indentSel() { const ids = selIds(); let ok = false; ids.forEach((id) => { if (core.T.byId[id] && indent(id)) ok = true; }); if (!ok && ids.length) toast("これ以上は下げられません（直前の兄弟の子になります）"); }
export function outdentSel() { const ids = selIds().reverse(); let ok = false; ids.forEach((id) => { if (core.T.byId[id] && outdent(id)) ok = true; }); if (!ok && ids.length) toast("すでに最上位です"); }
export function moveSel(dir) {
  const ids = selIds(); if (!ids.length) return;
  (dir > 0 ? ids.slice().reverse() : ids).forEach((id) => moveIssue(id, dir));
}
export function setStatusSel(st) { const ids = ui.sel.slice(); ids.forEach((id) => setStatus(id, st)); }
export function setAssigneeSel(name) { const ids = ui.sel.slice(); bulk(ids, (o) => { o.assignee = name; }); }
export function setPrioSel(p) { bulk(ui.sel.slice(), (o) => { o.prio = p; }); }
export function nextStatus(id) {
  const cat = Engine.statusCat(core.T.byId[id].status);
  setStatus(id, cat === "todo" ? "In Progress" : cat === "prog" ? "Done" : "To Do");
}
export function expandAll(open) {
  const c = {}; if (!open) core.T.dfs.forEach((id) => { if (core.T.kids[id].length) c[id] = true; });
  setUI({ collapsed: c });
}
export function collapseTo(level) { // level: 1 = ルートのみ表示 …（ui.level とは別に、開閉だけを変える）
  const c = {}; core.T.dfs.forEach((id) => { if (core.T.kids[id].length && core.T.depth[id] >= level - 1) c[id] = true; });
  setUI({ collapsed: c });
}

/* ---- 端末への入出力 ---- */
export function copyText(text, msg) {
  const done = () => toast(msg || "コピーしました");
  try { navigator.clipboard.writeText(text).then(done, () => { fallback(); }); } catch (e) { fallback(); }
  function fallback() {
    try { const ta = document.createElement("textarea"); ta.value = text; ta.style.position = "fixed"; ta.style.opacity = "0"; document.body.append(ta); ta.select(); document.execCommand("copy"); ta.remove(); done(); }
    catch (e2) { toast("コピーできませんでした。選択して手動でコピーしてください"); }
  }
}
/* ---- テンプレート ---- */
export const targetIds = (id) => (ui.sel.length > 1 && ui.sel.indexOf(id) >= 0 ? ui.sel.slice() : [id]);
export function openTemplateApply(id) { const ids = id ? targetIds(id) : ui.sel.slice(); if (ids.length) openModal("tplApply", { ids }); }
export function openTemplateSave(id) { if (id && core.T.kids[id] && core.T.kids[id].length) openModal("tplSave", { id }); else toast("子タスクがある課題を選んでください"); }
export function pickFile(cb) {
  const inp = document.createElement("input"); inp.type = "file"; inp.accept = ".csv,.tsv,.txt,.json,text/csv,application/json";
  inp.onchange = () => { const f = inp.files && inp.files[0]; if (f) cb(f); };
  inp.click();
}

/* ---- テーマ・密度 ---- */
export const cycleTheme = () => { const t = core.prefs.theme; setPref("theme", t === "auto" ? "light" : t === "light" ? "dark" : "auto"); };
export const THEME_LABEL = { auto: "自動", light: "ライト", dark: "ダーク" };

/* ---- 行のコンテキストメニュー ---- */
export function rowMenu(id) {
  const o = core.T.byId[id], multi = ui.sel.length > 1;
  const sb = core.T.parentOf[id] ? core.T.kids[core.T.parentOf[id]] : core.T.roots, i = sb.indexOf(id);
  const n = multi ? "（" + ui.sel.length + "件）" : "";
  const items = [];
  if (!multi) items.push({ label: "詳細を開く", icon: "pencil", kbd: "Space", onClick: () => openDetail(id) }, { label: "名前を変更", icon: "pencil", kbd: "F2", onClick: () => startEdit(id) }, { sep: true },
    { label: "子を追加", icon: "plus", kbd: "Shift+N", onClick: () => addChild(id) }, { label: "同じ階層に追加", icon: "plus", kbd: "N", onClick: () => addSibling(id) },
    { label: "テンプレから子タスクを追加…", icon: "copy", onClick: () => openTemplateApply(id) },
    { label: "この子タスクをテンプレとして保存…", icon: "copy", disabled: !core.T.kids[id].length, onClick: () => openTemplateSave(id) }, { sep: true });
  else items.push({ label: "テンプレから子タスクを追加…（" + ui.sel.length + "件へ）", icon: "copy", onClick: () => openTemplateApply(id) }, { sep: true });
  items.push({ header: "状況" + n },
    { label: "To Do", checked: !multi && Engine.statusCat(o.status) === "todo", onClick: () => setStatusSel("To Do") },
    { label: "In Progress", checked: !multi && Engine.statusCat(o.status) === "prog", onClick: () => setStatusSel("In Progress") },
    { label: "Done", checked: !multi && Engine.statusCat(o.status) === "done", onClick: () => setStatusSel("Done") }, { sep: true },
    { label: "階層を下げる", icon: "indent", kbd: "Tab", disabled: !multi && i <= 0, onClick: indentSel },
    { label: "階層を上げる", icon: "outdent", kbd: "Shift+Tab", disabled: !multi && !core.T.parentOf[id], onClick: outdentSel },
    { label: "上へ", icon: "arrowUp", kbd: "Alt+↑", disabled: !multi && i <= 0, onClick: () => moveSel(-1) },
    { label: "下へ", icon: "arrowDown", kbd: "Alt+↓", disabled: !multi && i >= sb.length - 1, onClick: () => moveSel(1) }, { sep: true },
    { label: "削除" + n, icon: "trash", kbd: "Del", danger: true, onClick: deleteSel });
  return items;
}

/* ---- コマンド（パレット・ショートカット一覧に使う） ---- */
export function commands() {
  const P = core.P, ps = P ? P.ui : {};
  const C = [];
  const add = (group, title, run, extra) => C.push(Object.assign({ group, title, run }, extra || {}));
  add("課題", "課題を追加（最上位）", () => addRootIssue(), { icon: "plus" });
  add("課題", "選択した課題の子を追加", () => ui.cursor && addChild(ui.cursor), { kbd: "Shift+N", need: "sel" });
  add("課題", "選択した課題と同じ階層に追加", () => ui.cursor && addSibling(ui.cursor), { kbd: "N", need: "sel" });
  add("課題", "テンプレートから子タスクを追加（選択した課題へ）", () => openTemplateApply(ui.cursor), { need: "sel", icon: "copy" });
  add("課題", "選択した課題の子タスクをテンプレートとして保存", () => openTemplateSave(ui.cursor), { need: "sel" });
  add("課題", "選択した課題の名前を変更", () => ui.cursor && startEdit(ui.cursor), { kbd: "Enter / F2", need: "sel" });
  add("課題", "選択した課題を削除", deleteSel, { kbd: "Del", need: "sel" });
  add("課題", "詳細パネルを開く／閉じる", toggleDetail, { kbd: "Space" });
  add("表示", "ガントを表示", () => setUI({ view: "gantt" }), { kbd: "1" });
  add("表示", "ボードを表示", () => setUI({ view: "board" }), { kbd: "2" });
  add("表示", "リストを表示", () => setUI({ view: "list" }), { kbd: "3" });
  add("表示", "ゴールと現状のギャップを表示", () => setUI({ view: "goals" }), { kbd: "4" });
  [["day", "日"], ["week", "週"], ["month", "月"], ["all", "全体"]].forEach(([k, l]) => add("表示", "ガントの拡大率: " + l, () => setUI({ view: "gantt", zoom: k })));
  [["root", "Epic（ルート課題）"], ["float", "余裕"], ["asg", "担当者"]].forEach(([k, l]) => add("表示", "バーの色分け: " + l, () => setUI({ colorBy: k })));
  add("表示", "すべて開く", () => expandAll(true)); add("表示", "すべて閉じる", () => expandAll(false));
  add("表示", "遅れだけを表示", () => setUi({ flt: ui.flt === "late" ? "" : "late" }));
  add("表示", "余裕小だけを表示", () => setUi({ flt: ui.flt === "tight" ? "" : "tight" }));
  add("表示", "絞り込みを解除", () => setUi({ q: "", asg: "", flt: "", st: "", pr: "" }));
  add("表示", "未完了だけを表示", () => setUi({ st: ui.st === "open" ? "" : "open" }));
  add("表示", "担当者が未割当の課題だけを表示", () => setUi({ asg: ui.asg === "\u0001" ? "" : "\u0001" }));
  add("表示", "密度: " + (core.prefs.density === "compact" ? "標準に" : "コンパクトに"), () => setPref("density", core.prefs.density === "compact" ? "normal" : "compact"));
  add("表示", "テーマを切り替え（自動→ライト→ダーク）", cycleTheme);
  add("計画", "担当者ごとの同時作業の制限: " + (P && P.res.on ? "オフに" : "オンに"), () => setRes({ on: !P.res.on }));
  add("計画", "担当者の負荷を見る", () => openModal("workload"));
  add("計画", "基準日を今日にする", () => { const d = new Date(), p = (n) => String(n).padStart(2, "0"); setProject({ base: d.getFullYear() + "-" + p(d.getMonth() + 1) + "-" + p(d.getDate()) }, true); });
  add("計画", "プロジェクト設定（基準日・休業日・シナリオ）", () => openModal("settings"));
  add("編集", "元に戻す", undo, { kbd: "Ctrl+Z" }); add("編集", "やり直す", redo, { kbd: "Ctrl+Shift+Z" });
  add("プロジェクト", "新しいプロジェクト", () => openModal("newProject"));
  add("プロジェクト", "名前・キーを変更", () => openModal("editProject"));
  add("プロジェクト", "複製", dupProject);
  add("プロジェクト", "CSVを取り込む", () => openModal("import"));
  add("プロジェクト", "JIRA用CSV・共有JSONを書き出す", () => openModal("export"));
  add("プロジェクト", "全プロジェクトをバックアップ", () => openModal("export", { backup: true }));
  add("プロジェクト", "ショートカット一覧", () => openModal("help"), { kbd: "?" });
  projectList().forEach((p) => { if (p.id !== P.id) add("プロジェクトを開く", p.name, () => openProject(p.id)); });
  return C.filter((c) => c.need !== "sel" || ui.cursor);
}
