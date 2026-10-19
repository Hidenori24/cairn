// 作業フォルダ（File System Access API）への読み書き。Chrome / Edge 用。
// 1プロジェクト = 「名前.csv」（JIRA互換の原本）+「名前.settings.json」（基準日・休業日など、CSVに入らない設定）。
// このファイルは画面や core.js のことを知らない。フォルダの中身を読み書きするだけ。
export const supported = typeof window !== "undefined" && typeof window.showDirectoryPicker === "function";

const BACKUP_DIR = "_バックアップ", DELETED_DIR = "_削除済み", SETTINGS_EXT = ".settings.json";

/* ---------- フォルダの記憶（IndexedDB。ハンドルは localStorage に入らないため） ---------- */
function idb() {
  return new Promise((res, rej) => {
    const r = indexedDB.open("regroadmap.fs", 1);
    r.onupgradeneeded = () => r.result.createObjectStore("h");
    r.onsuccess = () => res(r.result);
    r.onerror = () => rej(r.error);
  });
}
async function idbOp(mode, fn) {
  const db = await idb();
  return new Promise((res, rej) => {
    const tx = db.transaction("h", mode), out = fn(tx.objectStore("h"));
    tx.oncomplete = () => { db.close(); res(out && out.result); };
    tx.onerror = tx.onabort = () => { db.close(); rej(tx.error); };
  });
}
export async function loadHandle() { try { return (await idbOp("readonly", (s) => s.get("dir"))) || null; } catch (e) { return null; } }
async function saveHandle(h) { try { await idbOp("readwrite", (s) => s.put(h, "dir")); } catch (e) {} }
export async function forget() { try { await idbOp("readwrite", (s) => s.delete("dir")); } catch (e) {} }

/* ---------- 選択と権限 ---------- */
export async function pick() {
  const h = await window.showDirectoryPicker({ id: "regroadmap", mode: "readwrite" });
  await saveHandle(h);
  return h;
}
// request=true はクリックなどの操作の直後にだけ使える（ブラウザの決まり）
export async function permission(h, request) {
  const opt = { mode: "readwrite" };
  try {
    let p = await h.queryPermission(opt);
    if (p !== "granted" && request) p = await h.requestPermission(opt);
    return p;
  } catch (e) { return "denied"; }
}

/* ---------- 読み書き ---------- */
function decode(buf) {
  try { return new TextDecoder("utf-8", { fatal: true }).decode(buf); } catch (e) { try { return new TextDecoder("shift_jis").decode(buf); } catch (e2) { return new TextDecoder().decode(buf); } }
}
async function getFile(dir, name) { try { return await (await dir.getFileHandle(name)).getFile(); } catch (e) { if (e && e.name === "NotFoundError") return null; throw e; } }
async function put(dir, name, text) {
  const fh = await dir.getFileHandle(name, { create: true }), w = await fh.createWritable();
  try { await w.write(text); await w.close(); } catch (e) { try { await w.abort(); } catch (e2) {} throw e; }
}
const stamp = () => { const d = new Date(), p = (n) => String(n).padStart(2, "0"); return d.getFullYear() + p(d.getMonth() + 1) + p(d.getDate()) + "-" + p(d.getHours()) + p(d.getMinutes()) + p(d.getSeconds()); };

// フォルダ直下の *.csv を一覧にする。mtime は csv と settings の新しい方。
export async function list(h) {
  const csv = {}, set = {};
  for await (const [name, e] of h.entries()) {
    if (e.kind !== "file") continue;
    const low = name.toLowerCase();
    if (low.endsWith(SETTINGS_EXT)) set[name.slice(0, -SETTINGS_EXT.length)] = e;
    else if (low.endsWith(".csv")) csv[name.slice(0, -4)] = e;
  }
  const out = [];
  for (const stem of Object.keys(csv)) {
    const f = await csv[stem].getFile(), s = set[stem] ? await set[stem].getFile() : null;
    out.push({ stem, mtime: Math.max(f.lastModified, s ? s.lastModified : 0), hasSettings: !!s });
  }
  return out;
}
export async function read(h, stem) {
  const f = await getFile(h, stem + ".csv"); if (!f) return null;
  const s = await getFile(h, stem + SETTINGS_EXT);
  let settings = null;
  if (s) { try { settings = JSON.parse(decode(await s.arrayBuffer())); } catch (e) { settings = null; } }
  return { csv: decode(await f.arrayBuffer()), settings, mtime: Math.max(f.lastModified, s ? s.lastModified : 0) };
}
// 書き込む。そのセッションで最初に上書きするときだけ、元のCSVを _バックアップ に残す。戻り値は書き込み後の mtime。
const backedUp = new Set();
export async function write(h, stem, csvText, settingsObj) {
  const old = await getFile(h, stem + ".csv");
  if (old && old.size > 0 && !backedUp.has(stem)) {
    const bd = await h.getDirectoryHandle(BACKUP_DIR, { create: true });
    await put(bd, stem + "." + stamp() + ".csv", decode(await old.arrayBuffer()));
    backedUp.add(stem);
  }
  await put(h, stem + ".csv", csvText);
  await put(h, stem + SETTINGS_EXT, JSON.stringify(settingsObj, null, 1));
  const f = await getFile(h, stem + ".csv"), s = await getFile(h, stem + SETTINGS_EXT);
  return Math.max(f.lastModified, s ? s.lastModified : 0);
}
// 削除は「_削除済み」へ移す（コピーして元を消す）
export async function remove(h, stem) {
  const dd = await h.getDirectoryHandle(DELETED_DIR, { create: true }), t = stamp();
  for (const ext of [".csv", SETTINGS_EXT]) {
    const f = await getFile(h, stem + ext); if (!f) continue;
    await put(dd, stem + "." + t + ext, decode(await f.arrayBuffer()));
    await h.removeEntry(stem + ext);
  }
}
