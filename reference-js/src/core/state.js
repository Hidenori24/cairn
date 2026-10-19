// 画面だけの一時的な状態（選択、編集中の行、開いているダイアログなど）。データ本体は core.js。
import { useSyncExternalStore } from "react";
import { core, subscribe, getVer } from "./core.js";

export const ui = {
  sel: [], cursor: null, anchor: null,   // 選択中の課題（複数可）、カーソル（主たる1件）
  editing: null, editCell: "name", fresh: null,   // 編集中の行・セル、追加直後の行
  drawer: false, drawerTab: "detail",
  q: "", asg: "", flt: "", st: "", pr: "",   // 絞り込み（検索語・担当・遅れ/余裕・状況・優先度）
  modal: null, palette: false, menu: null,
  order: [], v: 0, scrollSig: 0,
};
const ls = new Set();
export const emitUi = () => { ui.v++; ls.forEach((f) => f()); };
const subUi = (f) => { ls.add(f); return () => ls.delete(f); };
export function setUi(patch) { Object.assign(ui, patch); emitUi(); }
// いまの絞り込み条件（ガント・ボード・リストで共通）
export const curFilter = () => ({ q: ui.q, asg: ui.asg, flt: ui.flt, st: ui.st, pr: ui.pr });
export const clearFilter = () => setUi({ q: "", asg: "", flt: "", st: "", pr: "" });
export const useCore = () => useSyncExternalStore(subscribe, getVer);
export const useUi = () => { useSyncExternalStore(subUi, () => ui.v); return ui; };
export const useAll = () => { useCore(); return useUi(); };

/* ---- 選択 ---- */
export function selectOnly(id, opts) {
  Object.assign(ui, { sel: id ? [id] : [], cursor: id || null, anchor: id || null });
  if (opts && opts.scroll) ui.scrollSig++;
  emitUi();
}
export function toggleSel(id) {
  const has = ui.sel.indexOf(id) >= 0;
  ui.sel = has ? ui.sel.filter((x) => x !== id) : ui.sel.concat(id);
  ui.cursor = has ? (ui.sel[ui.sel.length - 1] || null) : id; ui.anchor = id; emitUi();
}
export function rangeTo(id) {
  const a = ui.order.indexOf(ui.anchor == null ? ui.cursor : ui.anchor), b = ui.order.indexOf(id);
  if (a < 0 || b < 0) { selectOnly(id); return; }
  const [lo, hi] = a < b ? [a, b] : [b, a];
  ui.sel = ui.order.slice(lo, hi + 1); ui.cursor = id; emitUi();
}
export function moveCursor(dir, extend) {
  const o = ui.order; if (!o.length) return;
  const i = ui.cursor ? o.indexOf(ui.cursor) : -1;
  const j = Math.max(0, Math.min(o.length - 1, i < 0 ? (dir > 0 ? 0 : o.length - 1) : i + dir));
  if (extend) { if (ui.anchor == null) ui.anchor = ui.cursor || o[j]; rangeTo(o[j]); }
  else selectOnly(o[j]);
  ui.scrollSig++; emitUi();
}
export function clearSel() { Object.assign(ui, { sel: [], cursor: null, anchor: null, editing: null }); emitUi(); }
export function pruneSel() {
  if (!core.T || core.T.error) return;
  const ok = (x) => core.T.byId[x];
  const sel = ui.sel.filter(ok);
  if (sel.length !== ui.sel.length || (ui.cursor && !ok(ui.cursor))) { ui.sel = sel; ui.cursor = ok(ui.cursor) ? ui.cursor : (sel[sel.length - 1] || null); if (!ok(ui.anchor)) ui.anchor = ui.cursor; }
  if (ui.editing && !ok(ui.editing)) ui.editing = null;
}

/* ---- ダイアログ・メニュー ---- */
export const openModal = (type, props) => setUi({ modal: { type, props: props || {} } });
export const closeModal = () => setUi({ modal: null });
export const openMenu = (x, y, items) => setUi({ menu: { x, y, items } });
export const closeMenu = () => setUi({ menu: null });
