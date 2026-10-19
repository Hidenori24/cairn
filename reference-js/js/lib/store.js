// プロジェクトの保存。1プロジェクト = CSV（課題のデータベース）+ 設定。ブラウザの localStorage に置く。
// localStorage が使えない環境ではメモリ上だけで動き、persistent=false になる。
var Store = (function () {
  "use strict";
  var IDX = "regroadmap.v3.index", PFX = "regroadmap.v3.p.";
  var mem = {}, persistent = true, lastError = "";
  function get(k) { try { var v = localStorage.getItem(k); return v == null ? (k in mem ? mem[k] : null) : v; } catch (e) { persistent = false; return k in mem ? mem[k] : null; } }
  function set(k, v) {
    mem[k] = v;
    try { localStorage.setItem(k, v); lastError = ""; return true; } catch (e) { persistent = false; lastError = String(e && e.message || e); return false; }
  }
  function del(k) { delete mem[k]; try { localStorage.removeItem(k); } catch (e) {} }
  function readJSON(k) { try { var v = get(k); return v ? JSON.parse(v) : null; } catch (e) { return null; } }

  function index() {
    var x = readJSON(IDX);
    if (!x || !Array.isArray(x.order)) x = { order: [], current: null };
    x.order = x.order.filter(function (id) { return get(PFX + id) != null; });
    if (x.current && x.order.indexOf(x.current) < 0) x.current = x.order[0] || null;
    return x;
  }
  function saveIndex(x) { return set(IDX, JSON.stringify(x)); }
  function load(id) { return readJSON(PFX + id); }
  function save(p) {
    var ok = set(PFX + p.id, JSON.stringify(p));
    var x = index(); if (x.order.indexOf(p.id) < 0) x.order.push(p.id); saveIndex(x);
    return ok;
  }
  function remove(id) { del(PFX + id); var x = index(); x.order = x.order.filter(function (i) { return i !== id; }); if (x.current === id) x.current = x.order[0] || null; saveIndex(x); }
  function setCurrent(id) { var x = index(); x.current = id; saveIndex(x); }
  function newId() { return "p" + Date.now().toString(36) + Math.random().toString(36).slice(2, 6); }
  function list() { var x = index(); return x.order.map(function (id) { var p = load(id); return p ? { id: id, name: p.name, key: p.key } : null; }).filter(Boolean); }
  // 全プロジェクトのバックアップ（JSON文字列）
  function backup() {
    var all = list().map(function (m) { return load(m.id); });
    return JSON.stringify({ app: "regroadmap", version: 3, exported: new Date().toISOString(), projects: all }, null, 1);
  }
  return { index: index, load: load, save: save, remove: remove, setCurrent: setCurrent, newId: newId, list: list, backup: backup,
    isPersistent: function () { return persistent; }, lastError: function () { return lastError; } };
})();
