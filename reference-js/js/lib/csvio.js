// CSV の読み書き。JIRA のエクスポート/インポート形式を土台に、逆算に必要な列（Deadline / Duration など）を足した形式。
// 課題レコードの形:
//   { id, key, type, summary, parent, prio, status, assignee, start, due, deadline, resolved,
//     durW, estW, remW, alt:{シナリオ名:週}, labels:[], desc, blocks:[課題id], extra:[...] }
// 日付は 1970-01-01 からの日数（整数）。
var CsvIO = (function () {
  "use strict";

  /* ---------- 低レベル: CSV 文字列 <-> 2次元配列 ---------- */
  function parse(text) {
    if (text.charCodeAt(0) === 0xFEFF) text = text.slice(1);
    var first = text.split(/\r?\n/, 1)[0];
    var cnt = function (ch) { var n = 0; for (var i = 0; i < first.length; i++) if (first[i] === ch) n++; return n; };
    var delim = ",";
    if (cnt("\t") > cnt(",")) delim = "\t"; else if (cnt(";") > cnt(",")) delim = ";";
    var rows = [], row = [], cur = "", q = false;
    for (var i = 0; i < text.length; i++) {
      var c = text[i];
      if (q) {
        if (c === '"') { if (text[i + 1] === '"') { cur += '"'; i++; } else q = false; } else cur += c;
      } else if (c === '"') q = true;
      else if (c === delim) { row.push(cur); cur = ""; }
      else if (c === "\n" || c === "\r") { if (c === "\r" && text[i + 1] === "\n") i++; row.push(cur); cur = ""; rows.push(row); row = []; }
      else cur += c;
    }
    if (cur !== "" || row.length) { row.push(cur); rows.push(row); }
    var data = rows.filter(function (r) { return r.some(function (x) { return String(x).trim() !== ""; }); });
    return { header: data[0] || [], rows: data.slice(1) };
  }
  function stringify(rows) {
    return rows.map(function (r) {
      return r.map(function (v) {
        v = v == null ? "" : String(v);
        return /[",\r\n]/.test(v) ? '"' + v.replace(/"/g, '""') + '"' : v;
      }).join(",");
    }).join("\r\n") + "\r\n";
  }
  var norm = function (s) { return String(s == null ? "" : s).toLowerCase().replace(/[\s　_]+/g, " ").trim(); };

  /* ---------- 値の読み取り ---------- */
  var MONTHS = { jan: 1, feb: 2, mar: 3, apr: 4, may: 5, jun: 6, jul: 7, aug: 8, sep: 9, oct: 10, nov: 11, dec: 12 };
  function mk(y, m, d) {
    var n = Math.floor(Date.UTC(y, m - 1, d) / 86400000), t = new Date(n * 86400000);
    return t.getUTCMonth() === m - 1 ? n : null;
  }
  function parseDate(s) {
    s = String(s || "").trim(); if (!s) return null; var m;
    if ((m = s.match(/^(\d{4})[-\/.年](\d{1,2})[-\/.月](\d{1,2})/))) return mk(+m[1], +m[2], +m[3]);
    if ((m = s.match(/^(\d{1,2})[\/\- ]([A-Za-z]{3,9})[\/\- ,.]*(\d{2,4})/))) {
      var mo = MONTHS[m[2].slice(0, 3).toLowerCase()], y = +m[3]; if (y < 100) y += 2000;
      return mo ? mk(y, mo, +m[1]) : null;
    }
    if ((m = s.match(/^([A-Za-z]{3,9})\.? (\d{1,2}),? (\d{4})/))) {
      var mo2 = MONTHS[m[1].slice(0, 3).toLowerCase()]; return mo2 ? mk(+m[3], mo2, +m[2]) : null;
    }
    return null;
  }
  // 期間 -> 週。plainIsWeeks: 数字だけなら週（Duration列）。そうでなければ 600 以上は秒（JIRAのエクスポート）。
  function parseDur(s, plainIsWeeks) {
    s = String(s || "").trim(); if (!s) return null;
    if (/^\d+(\.\d+)?$/.test(s)) { var n = +s; return plainIsWeeks ? n : (n >= 600 ? n / (8 * 3600 * 5) : n); }
    var tot = 0, hit = false, m, re = /(\d+(?:\.\d+)?)\s*(w|d|h|m|週間?|日|時間|分)/gi;
    while ((m = re.exec(s))) {
      hit = true; var v = +m[1], u = m[2].toLowerCase();
      tot += (u === "w" || u[0] === "週") ? v : (u === "d" || u === "日") ? v / 5 : (u === "h" || u === "時間") ? v / 40 : v / 2400;
    }
    return hit ? tot : null;
  }
  var pad2 = function (n) { return (n < 10 ? "0" : "") + n; };
  var MON = ["Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"];
  // "19/Oct/26 10:30 AM" や "2026-10-19 10:30" -> "2026-10-19T10:30"
  function parseDT(s) {
    s = String(s || "").trim(); var d = parseDate(s); if (d == null) return "";
    var m = s.match(/(\d{1,2}):(\d{2})\s*(AM|PM)?/i), hh = 0, mm = 0;
    if (m) { hh = +m[1]; mm = +m[2]; if (m[3]) { var pm = /pm/i.test(m[3]); if (pm && hh < 12) hh += 12; if (!pm && hh === 12) hh = 0; } }
    return Sched.fmt(d) + "T" + pad2(hh) + ":" + pad2(mm);
  }
  // "2026-10-19T10:30" -> "19/Oct/26 10:30 AM"（JIRAのコメント列と同じ書き方）
  function fmtDT(iso) {
    var m = String(iso || "").match(/^(\d{4})-(\d{2})-(\d{2})T(\d{2}):(\d{2})$/); if (!m) return "";
    var hh = +m[4], ap = hh >= 12 ? "PM" : "AM", h12 = hh % 12 === 0 ? 12 : hh % 12;
    return m[3] + "/" + MON[+m[2] - 1] + "/" + m[1].slice(2) + " " + pad2(h12) + ":" + m[5] + " " + ap;
  }
  // コメント1件 <-> "日時;投稿者;本文"
  function parseComment(cell) {
    var m = String(cell).match(/^([^;]*);([^;]*);([\s\S]*)$/);
    if (m && parseDate(m[1])) return { at: parseDT(m[1]), who: m[2].trim(), text: m[3] };
    return { at: "", who: "", text: String(cell) };
  }
  var fmtComment = function (c) { return fmtDT(c.at) + ";" + (c.who || "") + ";" + c.text; };
  var fmtDate = function (n) { return n == null ? "" : Sched.fmt(n); };
  function fmtEst(w) {
    if (w == null || w <= 0) return "";
    var d = Math.round(w * 5 * 100) / 100;
    if (Math.abs(w - Math.round(w)) < 1e-9) return Math.round(w) + "w";
    if (Math.abs(d - Math.round(d)) < 1e-9) return Math.round(d) + "d";
    return Math.max(1, Math.round(w * 40)) + "h";   // JIRAの見積は整数（1日=8時間、1週=5日）
  }
  var fmtNum = function (w) { return String(Math.round(w * 100) / 100); };

  /* ---------- 取り込み: CSV -> 課題レコード ---------- */
  function importCSV(text) {
    var parsed = parse(text), H = parsed.header, nH = H.map(norm), R = parsed.rows, warns = [];
    var find = function (syn) {
      var out = [];
      nH.forEach(function (h, i) { if (syn.some(function (s) { return typeof s === "string" ? h === norm(s) : s.test(h); })) out.push(i); });
      return out;
    };
    var col = {
      id: find(["issue id", "id", "課題id"]), key: find(["issue key", "key", "課題キー", "キー"]),
      type: find(["issue type", "issuetype", "type", "課題タイプ"]), sum: find(["summary", "件名", "要約"]),
      parent: find(["parent", "parent id", "親", "親課題"]), epicLink: find([/^(custom field \()?epic link\)?$/, "エピックリンク"]),
      epicName: find([/epic name/, "エピック名"]), prio: find(["priority", "優先度"]), status: find(["status", "ステータス"]),
      assignee: find(["assignee", "担当者", "担当"]), earliest: find(["earliest start", "着手可能日", "start no earlier than"]), comment: find(["comment", "コメント"]),
      start: find([/start date/, "開始日"]), due: find(["due date", "due", "期限日", "期限"]),
      deadline: find(["deadline", "目標期限", "逆算期限"]), resolved: find(["resolved", "解決日", "resolution date"]),
      est: find(["original estimate", "original time estimate", "元の見積り", "初期見積り"]),
      remW: find(["remaining (weeks)", "残り(週)", "残り（週）"]), remE: find(["remaining estimate", "残りの見積り"]),
      labels: find(["labels", "ラベル"]), desc: find(["description", "説明"]),
      bOut: find([/^outward issue link.*block/, /^外向き.*block/]), bIn: find([/^inward issue link.*block/, /^内向き.*block/])
    };
    if (!col.sum.length) return { error: "「Summary」列が見つかりません。1行目に列名があるCSVを読み込んでください。" };
    var durCols = [];
    H.forEach(function (h, i) {
      var m = String(h).trim().match(/^(duration \(weeks\)|期間\(週\)|期間（週）)\s*(?:@\s*(.+))?$/i);
      if (m) durCols.push({ i: i, scn: m[2] ? m[2].trim() : null });
    });
    var scenarios = [];
    durCols.forEach(function (d) { if (d.scn && scenarios.indexOf(d.scn) < 0) scenarios.push(d.scn); });
    var known = {};
    Object.keys(col).forEach(function (k) { col[k].forEach(function (i) { known[i] = true; }); });
    durCols.forEach(function (d) { known[d.i] = true; });
    var extraIdx = [];
    H.forEach(function (h, i) { if (!known[i]) extraIdx.push(i); });

    var cell = function (r, idx) { for (var k = 0; k < idx.length; k++) { var v = (r[idx[k]] == null ? "" : String(r[idx[k]])).trim(); if (v) return v; } return ""; };
    var cells = function (r, idx) { return idx.map(function (i) { return (r[i] == null ? "" : String(r[i])).trim(); }).filter(Boolean); };

    var raw = [], skipped = 0;
    R.forEach(function (r) {
      var summary = cell(r, col.sum);
      if (!summary) { skipped++; return; }
      var o = {
        id: cell(r, col.id), key: cell(r, col.key), type: cell(r, col.type) || "Task", summary: summary,
        _parent: cell(r, col.parent), _epicLink: cell(r, col.epicLink), epicName: cell(r, col.epicName),
        prio: cell(r, col.prio), status: cell(r, col.status), assignee: cell(r, col.assignee),
        start: parseDate(cell(r, col.start)), due: parseDate(cell(r, col.due)), deadline: parseDate(cell(r, col.deadline)),
        resolved: parseDate(cell(r, col.resolved)), labels: cells(r, col.labels), desc: (r[col.desc[0]] == null ? "" : String(r[col.desc[0]])).replace(/\s+$/, ""),
        _out: cells(r, col.bOut), _in: cells(r, col.bIn), blocks: [], alt: {},
        notBefore: parseDate(cell(r, col.earliest)), comments: cells(r, col.comment).map(parseComment),
        extra: extraIdx.map(function (i) { return r[i] == null ? "" : String(r[i]); })
      };
      if (!col.desc.length) o.desc = "";
      var rem = parseDur(cell(r, col.remW), true);
      if (rem == null) rem = parseDur(cell(r, col.remE), false);
      o.remW = rem;
      o.estW = parseDur(cell(r, col.est), false);
      var dd = null;
      durCols.forEach(function (d) { if (!d.scn && dd == null) dd = parseDur(r[d.i], true); });
      o.durW = dd;
      durCols.forEach(function (d) { if (d.scn) { var v = parseDur(r[d.i], true); if (v != null) o.alt[d.scn] = v; } });
      raw.push(o);
    });
    if (!raw.length) return { error: "課題の行が見つかりません。" };

    // id の確定（空・重複は採番）
    var seen = {}, maxN = 0;
    raw.forEach(function (o) { if (/^\d+$/.test(o.id)) maxN = Math.max(maxN, +o.id); });
    var dupIds = 0;
    raw.forEach(function (o) {
      if (!o.id || seen[o.id]) { if (o.id) dupIds++; o._origId = o.id; o.id = String(++maxN); } else o._origId = o.id;
      seen[o.id] = true;
    });
    if (dupIds) warns.push("Issue Id が重複している行が" + dupIds + "件あり、別のIDを振りました");
    var byRef = {}, epicRef = {};
    raw.forEach(function (o) { [o._origId, o.key, o.id].forEach(function (k) { if (k && !byRef[k]) byRef[k] = o; }); });
    raw.forEach(function (o) {
      if (/epic|エピック/i.test(o.type)) [o.summary, o.epicName].forEach(function (k) { if (k && !epicRef[k]) epicRef[k] = o; });
    });
    var unresolved = [];
    raw.forEach(function (o) {
      o.parent = "";
      [o._parent, o._epicLink].forEach(function (v) {
        if (!v || o.parent) return;
        var p = byRef[v] || epicRef[v];
        if (p && p !== o) o.parent = p.id;
        else unresolved.push(o.summary.slice(0, 20) + " → 親 " + v);
      });
    });
    // 親の循環を切る
    var byId = {}; raw.forEach(function (o) { byId[o.id] = o; });
    raw.forEach(function (o) {
      var seenP = {}, p = o;
      while (p && p.parent) {
        if (seenP[p.id]) { warns.push("親子関係がループしていたため「" + o.summary.slice(0, 20) + "」の親を外しました"); o.parent = ""; break; }
        seenP[p.id] = true; p = byId[p.parent];
      }
    });
    // 依存関係（A が B をブロック = A が B の先行）
    var addBlock = function (from, to) { if (from !== to && from.blocks.indexOf(to.id) < 0) from.blocks.push(to.id); };
    raw.forEach(function (o) {
      o._out.forEach(function (v) { var x = byRef[v]; if (x) addBlock(o, x); else unresolved.push(o.summary.slice(0, 20) + " → " + v); });
      o._in.forEach(function (v) { var x = byRef[v]; if (x) addBlock(x, o); else unresolved.push(v + " → " + o.summary.slice(0, 20)); });
    });
    raw.forEach(function (o) { delete o._parent; delete o._epicLink; delete o._out; delete o._in; delete o._origId; });
    if (unresolved.length) warns.push("参照先が見つからず無視: " + unresolved.slice(0, 5).join(", ") + (unresolved.length > 5 ? " ほか" + (unresolved.length - 5) + "件" : ""));
    if (skipped) warns.push("Summary が空の行を" + skipped + "件、読み飛ばしました");
    ["start", "due", "deadline"].forEach(function (k) {
      var idx = col[k], bad = 0;
      if (idx.length) R.forEach(function (r) { var v = cell(r, idx); if (v && parseDate(v) == null) bad++; });
      if (bad) warns.push("日付として読めない値が" + bad + "件（" + (k === "start" ? "Start date" : k === "due" ? "Due date" : "Deadline") + "列）");
    });
    return {
      issues: raw,
      meta: { hasDeadlineCol: col.deadline.length > 0, scenarios: scenarios, extraHeaders: extraIdx.map(function (i) { return H[i]; }) },
      warns: warns
    };
  }

  /* ---------- 書き出し: 課題レコード -> CSV ----------
     opts.planned: true なら Start/Due に計画日を、Duration に実効の週数を書く（JIRAへ持ち込む用）
     opts.rollup:  function(id) -> { start, finish }（親課題の集計日）
     opts.plan:    function(id) -> { start, finish, weeks }（葉の計画日）
     opts.effDeadline: function(id) -> 日数|null */
  function exportCSV(issues, meta, opts) {
    opts = opts || {};
    var byId = {}, kids = {};
    issues.forEach(function (o) { byId[o.id] = o; kids[o.id] = []; });
    var roots = [];
    issues.forEach(function (o) { if (o.parent && byId[o.parent]) kids[o.parent].push(o); else roots.push(o); });
    var order = [];
    (function walk(list) { list.forEach(function (o) { order.push(o); walk(kids[o.id]); }); })(roots);

    var scnNames = (meta.scenarios || []).slice();
    issues.forEach(function (o) { Object.keys(o.alt || {}).forEach(function (s) { if (scnNames.indexOf(s) < 0) scnNames.push(s); }); });
    var maxLab0 = Math.max(1, Math.max.apply(null, issues.map(function (o) { return o.labels.length; })));
    var maxBl = Math.max(1, Math.max.apply(null, issues.map(function (o) { return o.blocks.length; })));
    var anyKey = issues.some(function (o) { return o.key; });
    var anyAssignee = issues.some(function (o) { return o.assignee; });
    var anyRes = issues.some(function (o) { return o.resolved != null; });
    var anyRem = issues.some(function (o) { return o.remW != null; });
    var anyNB = issues.some(function (o) { return o.notBefore != null; });
    var maxCm = Math.max(0, Math.max.apply(null, issues.map(function (o) { return (o.comments || []).length; })));
    var withDeadline = meta.hasDeadlineCol || opts.planned || issues.some(function (o) { return o.deadline != null; });
    var isEpic = function (o) { return /epic|エピック/i.test(o.type); };
    var epicNameOf = function (o) { return o.epicName || o.summary; };
    // JIRAの階層は「Epic > タスク（Story・Bug）> サブタスク」の3段まで（サブタスクの下に子は作れない）。
    // JIRA用の書き出しでは、タスクより深い課題をすべて、その上のタスクの直下のサブタスクにする。
    // 間にあった課題の名前は、説明の先頭に「[元の階層] …」として残す。原本（app形式）には元の階層がそのまま残る。
    var isMs = function (o) { return /milestone|マイルストーン/i.test(o.type); };
    var effParent = {}, trail = {}, retype = {}, msLab = {};
    if (opts.planned) issues.forEach(function (o) {
      if (isMs(o)) msLab[o.id] = (o.labels || []).some(function (l) { return /^milestone$/i.test(l); }) ? 0 : 1;
      if (isEpic(o)) { effParent[o.id] = ""; return; }
      var a = o, chain = [];
      while (a.parent && byId[a.parent] && !isEpic(byId[a.parent])) { chain.unshift(a); a = byId[a.parent]; }
      if (a === o) {   // タスクの階層
        var pe = byId[o.parent]; effParent[o.id] = pe && isEpic(pe) ? pe.id : "";
        if (!/^(task|story|bug)$/i.test(o.type)) retype[o.id] = "Task";
      } else {         // それより深い課題はサブタスク
        effParent[o.id] = a.id; retype[o.id] = "Sub-task";
        if (chain.length > 1) trail[o.id] = chain.slice(0, -1).map(function (x) { return x.summary; }).join(" > ");
      }
    });
    var maxLab = maxLab0 + (Object.keys(msLab).some(function (k) { return msLab[k]; }) ? 1 : 0);
    var labelsOf = function (o) { return msLab[o.id] ? o.labels.concat(["Milestone"]) : o.labels; };
    var cols = [];
    var C = function (h, f) { cols.push({ h: h, f: f }); };
    C("Issue Id", function (o) { return o.id; });
    if (anyKey) C("Issue key", function (o) { return o.key; });
    C("Issue Type", function (o) { return retype[o.id] || o.type; });
    C("Summary", function (o) { return o.summary; });
    C("Parent", function (o) { if (opts.planned) return effParent[o.id] || ""; return o.parent && byId[o.parent] ? o.parent : ""; });
    C("Epic Name", function (o) { return isEpic(o) ? epicNameOf(o) : ""; });
    C("Epic Link", function (o) { var p = byId[o.parent]; return p && isEpic(p) && !isEpic(o) ? epicNameOf(p) : ""; });
    C("Priority", function (o) { return o.prio; });
    C("Status", function (o) { return o.status; });
    if (anyAssignee) C("Assignee", function (o) { return o.assignee; });
    C("Start date", function (o) {
      if (opts.planned) { var p = opts.plan(o.id); if (p) return fmtDate(p.start); }
      return fmtDate(o.start);
    });
    C("Due date", function (o) {
      if (opts.planned) { var p = opts.plan(o.id); if (p) return fmtDate(p.finish); }
      return fmtDate(o.due);
    });
    if (anyRes) C("Resolved", function (o) { return fmtDate(o.resolved); });
    C("Original estimate", function (o) {
      if (opts.planned) { var p = opts.plan(o.id); return p && p.leaf ? fmtEst(p.weeks) : ""; }
      return o.estW != null ? fmtEst(o.estW) : "";
    });
    for (var i = 0; i < maxLab; i++) (function (i) { C("Labels", function (o) { return labelsOf(o)[i] || ""; }); })(i);
    C("Description", function (o) { return trail[o.id] ? "[元の階層] " + trail[o.id] + (o.desc ? "\n" + o.desc : "") : o.desc; });
    for (var j = 0; j < maxBl; j++) (function (j) { C("Outward issue link (Blocks)", function (o) { return o.blocks[j] || ""; }); })(j);
    for (var cm = 0; cm < maxCm; cm++) (function (cm) { C("Comment", function (o) { var c = (o.comments || [])[cm]; return c ? fmtComment(c) : ""; }); })(cm);
    if (anyNB) C("Earliest start", function (o) { return fmtDate(o.notBefore); });
    if (withDeadline) C("Deadline", function (o) {
      if (opts.planned && opts.ownDeadline) return fmtDate(opts.ownDeadline(o.id));
      return fmtDate(o.deadline);
    });
    if (anyRem) C("Remaining (weeks)", function (o) { return o.remW != null ? fmtNum(o.remW) : ""; });
    C("Duration (weeks)", function (o) {
      if (opts.planned) { var p = opts.plan(o.id); if (p && p.leaf) return fmtNum(p.baseWeeks); return ""; }
      return o.durW != null ? fmtNum(o.durW) : "";
    });
    scnNames.forEach(function (s) {
      C("Duration (weeks) @" + s, function (o) { return o.alt && o.alt[s] != null ? fmtNum(o.alt[s]) : ""; });
    });
    (meta.extraHeaders || []).forEach(function (h, k) { C(h, function (o) { return (o.extra && o.extra[k]) || ""; }); });

    var rows = [cols.map(function (c) { return c.h; })];
    order.forEach(function (o) { rows.push(cols.map(function (c) { return c.f(o); })); });
    return stringify(rows);
  }

  return { parse: parse, stringify: stringify, parseDT: parseDT, fmtDT: fmtDT, parseDate: parseDate, parseDur: parseDur, importCSV: importCSV, exportCSV: exportCSV, fmtDate: fmtDate };
})();
