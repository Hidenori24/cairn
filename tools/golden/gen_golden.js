#!/usr/bin/env node
// golden/ の正解データを、reference-js のライブラリ（sched.js / csvio.js / engine.js）で作り直す。
// SPEC 12.2 の「Playwright で JS版を開いて保存する」手順と同じ計算を、Node だけで行う。
// 使い方: node tools/golden/gen_golden.js [出力先フォルダ]   （既定: golden/）
//         node tools/golden/gen_golden.js --check            （golden/ と一致するか確かめるだけ）
"use strict";
const fs = require("fs");
const path = require("path");
const vm = require("vm");

const root = path.resolve(__dirname, "..", "..");
const lib = path.join(root, "reference-js", "js", "lib");
const ctx = { console };
vm.createContext(ctx);
const src = ["sched.js", "csvio.js", "engine.js"].map((f) => fs.readFileSync(path.join(lib, f), "utf8")).join("\n");
vm.runInContext(src + "\nthis.Sched=Sched;this.CsvIO=CsvIO;this.Engine=Engine;", ctx, { filename: "reference-js/js/lib" });
const { Sched, CsvIO, Engine } = ctx;

const goldenDir = path.join(root, "golden");
const holidays = fs.readFileSync(path.join(goldenDir, "holidays.txt"), "utf8").split(/\s+/).filter(Boolean);
const cal = Sched.make(holidays);

function load(sample) {
  const text = fs.readFileSync(path.join(goldenDir, "samples", sample + ".csv"), "utf8");
  const imp = CsvIO.importCSV(text);
  if (imp.error) throw new Error(sample + ": " + imp.error);
  return imp;
}

function schedule(sample, base, scn, res) {
  const imp = load(sample);
  const T = Engine.build(imp.issues, imp.meta);
  if (T.error) throw new Error(sample + ": " + T.error);
  const R = Engine.compute(T, cal, base, scn, { level: res });
  const D = (n) => (n == null ? null : Sched.fmt(n));
  const rows = T.dfs.map((id) => {
    const r = R[id], leaf = !T.kids[id].length;
    return {
      id, did: T.did[id], name: T.name[id], depth: T.depth[id], leaf: r.leaf, cat: T.cat[id], kind: leaf ? T.kind[id] : null,
      asapStart: D(r.asapStart), asapFinish: D(r.asapFinish), alapStart: D(r.alapStart), alapFinish: D(r.alapFinish),
      float: r.float, status: r.status, weeks: leaf ? r.weeks : null, days: leaf ? r.days : null,
      waited: !!r.waited, pinned: !!r.pinned, effectiveDeadline: leaf ? D(T.dl[id]) : null, ownDeadline: D(T.ownDl[id]),
      n: r.n, nDone: r.nDone,
    };
  });
  const out = {
    input: "samples/" + sample + ".csv", baseDate: base, scenario: scn, levelResources: { on: res.on, cap: res.cap },
    holidays: "DEFAULT_HOLIDAYS(別掲 holidays.txt)", warns: T.warns, scenarios: T.scenarios, rows,
  };
  return JSON.stringify(out, null, 1);
}

function exportText(sample, kind) {
  const imp = load(sample);
  if (kind === "app") return CsvIO.exportCSV(imp.issues, imp.meta, {});
  const T = Engine.build(imp.issues, imp.meta);
  const R = Engine.compute(T, cal, "2026-10-19", "標準", { level: { on: false, cap: 1 } });
  return CsvIO.exportCSV(imp.issues, imp.meta, {
    planned: true,
    plan: (id) => { const r = R[id]; return r ? { start: r.asapStart, finish: r.asapFinish, leaf: r.leaf, weeks: r.weeks, baseWeeks: T.baseWeeks[id] } : null; },
    ownDeadline: (id) => (T.ownDl[id] != null ? T.ownDl[id] : null),
  });
}

const OFF = { on: false, cap: 1 };
const files = {};
for (const s of ["regulation_roadmap", "hierarchy_demo", "leveling_demo"]) {
  files[s + ".base.json"] = schedule(s, "2026-10-19", "標準", OFF);
  files[s + ".level1.json"] = schedule(s, "2026-10-19", "標準", { on: true, cap: 1 });
  files[s + ".level2.json"] = schedule(s, "2026-10-19", "標準", { on: true, cap: 2 });
  files[s + ".base_1102.json"] = schedule(s, "2026-11-02", "標準", OFF);
  files[s + ".roundtrip.csv"] = exportText(s, "app");
  files[s + ".jira.csv"] = exportText(s, "jira");
}
files["regulation_roadmap.scn_nb.json"] = schedule("regulation_roadmap", "2026-10-19", "第三者(NB)", OFF);

const check = process.argv.includes("--check");
const outDir = check ? goldenDir : path.resolve(process.argv[2] || goldenDir);
let bad = 0;
for (const [name, text] of Object.entries(files)) {
  const p = path.join(outDir, name);
  if (check) {
    const cur = fs.existsSync(p) ? fs.readFileSync(p, "utf8") : null;
    if (cur !== text) { bad++; console.log("差分あり: " + name); }
  } else fs.writeFileSync(p, text);
}
if (check) { console.log(bad ? bad + "件が一致しません" : "すべて一致"); process.exit(bad ? 1 : 0); }
console.log(Object.keys(files).length + "ファイルを書きました: " + outDir);
