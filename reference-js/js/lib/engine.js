// 課題ツリーの構築と、スケジュール計算（順方向=最早 / 逆方向=最遅 / 余裕）。
// 考え方:
//  - 子を持たない課題（葉）だけがスケジュールされる。親課題は配下の葉から集計した「まとめ行」になる。
//  - 親課題への依存関係・期限は、その配下のすべての葉に展開して扱う。
//  - 日付は営業日（土日と休業日を除く）で計算する。1週=5営業日。
var Engine = (function () {
  "use strict";
  var isEpic = function (o) { return /epic|エピック/i.test(o.type); };
  function statusCat(s) {
    s = String(s || "").trim();
    if (/^(done|closed|resolved|complete|completed|完了|解決|クローズ|済)/i.test(s)) return "done";
    if (/(progress|進行|対応中|doing|working|review|レビュー)/i.test(s)) return "prog";
    return "todo";
  }
  function tagOf(summary) {
    var m = String(summary).match(/^\[([^\]]+)\]\s*(.*)$/);
    return m ? { tag: m[1], name: m[2] } : { tag: "", name: summary };
  }
  var weekdaysBetween = function (a, b) { var c = 0; for (var d = a; d <= b; d++) { var w = (d + 4) % 7; if (w >= 1 && w <= 5) c++; } return c; };

  function build(issues, meta) {
    var T = { issues: issues, meta: meta, byId: {}, kids: {}, roots: [], depth: {}, rootOf: {}, leaves: {}, dfs: [], parentOf: {},
      did: {}, name: {}, baseWeeks: {}, kind: {}, warns: [], leafOrder: [], preds: {}, succs: {}, dl: {}, ownDl: {}, cat: {} };
    issues.forEach(function (o) {
      T.byId[o.id] = o; T.kids[o.id] = [];
      var t = tagOf(o.summary);
      T.did[o.id] = t.tag || o.key || o.id; T.name[o.id] = t.name || o.summary; T.cat[o.id] = statusCat(o.status);
    });
    issues.forEach(function (o) {
      if (o.parent && T.byId[o.parent] && o.parent !== o.id) { T.kids[o.parent].push(o.id); T.parentOf[o.id] = o.parent; } else T.roots.push(o.id);
    });
    // 親ループの検出（読み込み時に除去済みだが、編集の安全のため）
    for (var i = 0; i < issues.length; i++) {
      var seen = {}, p = issues[i].id;
      while (p) { if (seen[p]) return { error: "親子関係がループしています: " + T.did[p] }; seen[p] = true; p = T.parentOf[p]; }
    }
    // DFS 順・深さ・ルート・葉
    var visit = function (id, d, root) {
      T.depth[id] = d; T.rootOf[id] = root; T.dfs.push(id);
      var ks = T.kids[id], lv = [];
      ks.forEach(function (k) { lv = lv.concat(visit(k, d + 1, root)); });
      T.leaves[id] = ks.length ? lv : [id];
      return T.leaves[id];
    };
    T.roots.forEach(function (r) { visit(r, 0, r); });

    // 期間（週）
    var missing = [];
    T.dfs.forEach(function (id) {
      if (T.kids[id].length) return;
      var o = T.byId[id], w = o.durW;
      if (w == null) w = o.estW;
      if (w == null && o.start != null && o.due != null && o.due >= o.start) w = Math.max(1, weekdaysBetween(o.start, o.due)) / 5;
      if (w == null) { w = 1; missing.push(T.did[id]); }
      T.baseWeeks[id] = w;
      T.kind[id] = (w === 0 || /milestone|マイルストーン/i.test(o.type) || o.labels.some(function (l) { return /milestone|マイルストーン/i.test(l); })) ? "ms" : "task";
    });
    if (missing.length) T.warns.push("期間が読み取れず1週と仮置き: " + missing.slice(0, 10).join(", ") + (missing.length > 10 ? " ほか" + (missing.length - 10) + "件" : ""));

    // 依存関係（葉どうしに展開）
    T.dfs.forEach(function (id) { T.preds[id] = []; T.succs[id] = []; });
    var overlap = 0;
    issues.forEach(function (a) {
      a.blocks.forEach(function (bid) {
        if (!T.byId[bid]) return;
        T.leaves[a.id].forEach(function (la) {
          T.leaves[bid].forEach(function (lb) {
            if (la === lb) { overlap++; return; }
            if (T.preds[lb].indexOf(la) < 0) { T.preds[lb].push(la); T.succs[la].push(lb); }
          });
        });
      });
    });
    if (overlap) T.warns.push("親子の間（または同じ配下）の依存関係は無視しました");
    // トポロジカル順
    var leafIds = T.dfs.filter(function (id) { return !T.kids[id].length; });
    var indeg = {}; leafIds.forEach(function (id) { indeg[id] = T.preds[id].length; });
    var pos = {}; leafIds.forEach(function (id, i) { pos[id] = i; });
    var queue = leafIds.filter(function (id) { return indeg[id] === 0; });
    while (queue.length) {
      queue.sort(function (a, b) { return pos[a] - pos[b]; });
      var x = queue.shift(); T.leafOrder.push(x);
      T.succs[x].forEach(function (s) { indeg[s]--; if (indeg[s] === 0) queue.push(s); });
    }
    if (T.leafOrder.length !== leafIds.length) {
      var bad = leafIds.filter(function (id) { return T.leafOrder.indexOf(id) < 0; }).map(function (id) { return T.did[id]; });
      return { error: "先行・後続の関係がループしています: " + bad.slice(0, 6).join(", ") + (bad.length > 6 ? " ほか" : "") };
    }

    // 期限（自身の明示期限と、祖先からの継承）
    var hasCol = !!meta.hasDeadlineCol;
    T.dfs.forEach(function (id) {
      var o = T.byId[id], own = null;
      if (hasCol) own = o.deadline;
      else if (!T.kids[id].length && !T.succs[id].length) own = o.due;
      T.ownDl[id] = own;
    });
    leafIds.forEach(function (id) {
      var best = T.ownDl[id], p = T.parentOf[id];
      if (hasCol) while (p) { var d = T.ownDl[p]; if (d != null && (best == null || d < best)) best = d; p = T.parentOf[p]; }
      T.dl[id] = best;
    });
    var anyDl = leafIds.some(function (id) { return T.dl[id] != null && T.cat[id] !== "done"; });
    var noDl = leafIds.filter(function (id) { return !T.succs[id].length && T.dl[id] == null && T.cat[id] !== "done"; });
    if (noDl.length) T.warns.push(anyDl ? "期限が無い末端の課題" + noDl.length + "件は、最も遅い期限に合わせています" : "期限（Deadline）が1件も無いため、余裕は0として表示します。期限を入れると逆算できます");
    var sc = {}; issues.forEach(function (o) { Object.keys(o.alt || {}).forEach(function (s) { sc[s] = true; }); });
    T.scenarios = ["標準"];
    (meta.scenarios || []).concat(Object.keys(sc)).forEach(function (s) { if (T.scenarios.indexOf(s) < 0) T.scenarios.push(s); });
    return T;
  }

  // opts.level: { on: bool, cap: n } 担当者ごとの同時作業数を制限する（工数の制約）
  function compute(T, cal, baseIso, scn, opts) {
    opts = opts || {};
    var base = Sched.parse(baseIso), first = cal.workday(base - 1, 1);
    var days = {}, asap = {}, free = {}, alap = {}, wk = {}, res = {}, pinned = {}, zw = {};   // zw: 期間0のマイルストーンは営業日を消費しない（先行の完了日＝その日）
    var wOf = function (id) { var o = T.byId[id]; return (scn !== "標準" && o.alt && o.alt[scn] != null) ? o.alt[scn] : T.baseWeeks[id]; };
    var startOf = function (id, lookup) {   // 先行と着手可能日から決まる最早の開始
      var o = T.byId[id], s0 = first;
      if (o.notBefore != null) s0 = Math.max(s0, cal.lastWork(o.notBefore) === o.notBefore ? o.notBefore : cal.workday(o.notBefore, 1));
      if (T.preds[id].length) { var pm = Math.max.apply(null, T.preds[id].map(function (p) { return lookup[p][1]; })); s0 = Math.max(s0, zw[id] ? pm : cal.workday(pm, 1)); }
      return s0;
    };
    // 1) 先行関係だけで決まる最早（資源の制約なし）
    T.leafOrder.forEach(function (id) {
      var o = T.byId[id], cat = T.cat[id];
      wk[id] = wOf(id); days[id] = Math.max(Math.round(wk[id] * 5), 1); zw[id] = T.kind[id] === "ms" && wk[id] === 0;
      var s, f;
      if (cat === "done") {
        f = o.resolved != null ? o.resolved : (o.due != null ? o.due : cal.workday(first, -1));
        s = o.start != null ? o.start : f; if (s > f) s = f; pinned[id] = true;
      } else if (cat === "prog" && o.start != null && o.start <= base) {
        s = o.start;
        f = o.remW != null ? cal.workday(first, Math.max(Math.round(o.remW * 5), 1) - 1) : Math.max(cal.workday(s, days[id] - 1), first);
        pinned[id] = true;
      } else { s = startOf(id, asap); f = zw[id] ? s : cal.workday(s, days[id] - 1); }
      asap[id] = [s, f]; free[id] = [s, f];
    });
    // 2) 期限から逆算した最遅（資源に依存しない）
    var projEnd = null;
    T.leafOrder.forEach(function (id) { var d = T.dl[id]; if (d != null && T.cat[id] !== "done") projEnd = projEnd == null ? d : Math.max(projEnd, d); });
    for (var i = T.leafOrder.length - 1; i >= 0; i--) {
      var id = T.leafOrder[i];
      if (T.cat[id] === "done") { alap[id] = asap[id]; continue; }
      var c = [];
      if (T.dl[id] != null) c.push(cal.lastWork(T.dl[id]));
      T.succs[id].forEach(function (s) { if (T.cat[s] !== "done") c.push(zw[s] ? alap[s][0] : cal.workday(alap[s][0], -1)); });
      var f2 = c.length ? Math.min.apply(null, c) : (projEnd != null ? cal.lastWork(projEnd) : asap[id][1]);
      alap[id] = [zw[id] ? f2 : cal.workday(f2, -(days[id] - 1)), f2];
    }
    // 3) 担当者ごとの同時作業数の制限（最遅開始が早い作業から順に割り当てる）
    var level = !!(opts.level && opts.level.on), cap = Math.max(1, (opts.level && opts.level.cap) || 1);
    if (level) {
      var busy = {}, done3 = {}, order = T.leafOrder.map(function (id, k) { return { id: id, k: k }; });
      var who = function (id) { return String(T.byId[id].assignee || "").trim(); };
      var useRes = function (id) { return who(id) && T.kind[id] !== "ms" && T.cat[id] !== "done"; };
      T.leafOrder.forEach(function (id) {
        if (pinned[id]) { done3[id] = true; if (useRes(id)) (busy[who(id)] = busy[who(id)] || []).push(asap[id].slice()); }
      });
      var fit = function (person, est, d) {
        var list = busy[person] || [], t = est;
        for (var guard = 0; guard < 5000; guard++) {
          var f = cal.workday(t, d - 1), ov = list.filter(function (b) { return b[0] <= f && b[1] >= t; });
          if (ov.length < cap) return t;
          var pts = [t].concat(ov.map(function (b) { return b[0]; }).filter(function (x) { return x > t; })), bad = null;
          for (var q = 0; q < pts.length && bad == null; q++) {
            var act = ov.filter(function (b) { return b[0] <= pts[q] && b[1] >= pts[q]; });
            if (act.length >= cap) bad = act;
          }
          if (!bad) return t;
          t = cal.workday(Math.min.apply(null, bad.map(function (b) { return b[1]; })), 1);
        }
        return t;
      };
      var left = order.filter(function (x) { return !done3[x.id]; });
      while (left.length) {
        var elig = left.filter(function (x) { return T.preds[x.id].every(function (p) { return done3[p]; }); });
        if (!elig.length) break;
        elig.sort(function (a, b) { return (alap[a.id][0] - alap[b.id][0]) || (a.k - b.k); });
        var pick = elig[0].id, est = startOf(pick, asap), st = est;
        if (useRes(pick)) { st = fit(who(pick), est, days[pick]); (busy[who(pick)] = busy[who(pick)] || []).push([st, cal.workday(st, days[pick] - 1)]); }
        asap[pick] = [st, zw[pick] ? st : cal.workday(st, days[pick] - 1)]; done3[pick] = true;
        left = left.filter(function (x) { return x.id !== pick; });
      }
    }
    var stat = function (fl) { return fl < 0 ? "遅れ" : (fl <= 10 ? "余裕小" : "OK"); };
    T.leafOrder.forEach(function (id) {
      var a = asap[id], l = alap[id], done = T.cat[id] === "done";
      var fl = done ? 0 : cal.floatDays(a[1], l[1]);
      res[id] = { leaf: true, done: done, prog: T.cat[id] === "prog", weeks: wk[id], days: days[id], asapStart: a[0], asapFinish: a[1],
        freeStart: free[id][0], waited: !done && a[0] > free[id][0], pinned: !!pinned[id],
        alapStart: l[0], alapFinish: l[1], float: fl, status: done ? "完了" : stat(fl), n: 1, nDone: done ? 1 : 0 };
    });
    // 親のまとめ（深い方から）
    T.dfs.slice().reverse().forEach(function (id) {
      if (!T.kids[id].length) return;
      var L = T.leaves[id], act = L.filter(function (x) { return !res[x].done; });
      var nDone = L.length - act.length;
      var minF = act.length ? Math.min.apply(null, act.map(function (x) { return res[x].float; })) : 0;
      res[id] = {
        leaf: false, done: act.length === 0, prog: act.length > 0 && (nDone > 0 || act.some(function (x) { return res[x].prog; })),
        asapStart: Math.min.apply(null, L.map(function (x) { return res[x].asapStart; })),
        asapFinish: Math.max.apply(null, L.map(function (x) { return res[x].asapFinish; })),
        alapStart: Math.min.apply(null, L.map(function (x) { return res[x].alapStart; })),
        alapFinish: Math.max.apply(null, L.map(function (x) { return res[x].alapFinish; })),
        float: minF, status: act.length ? stat(minF) : "完了", n: L.length, nDone: nDone
      };
    });
    return res;
  }

  // 担当者ごとの負荷（未完了の末端課題）
  function workload(T, R, cal) {
    var by = {};
    T.leafOrder.forEach(function (id) {
      var o = T.byId[id], r = R[id], w = String(o.assignee || "").trim();
      if (!w || r.done || T.kind[id] === "ms") return;
      var x = by[w] = by[w] || { name: w, n: 0, weeks: 0, last: 0, ids: [], peak: 0, wait: 0 };
      x.n++; x.weeks += r.weeks; x.last = Math.max(x.last, r.asapFinish); x.ids.push(id); if (r.waited) x.wait++;
    });
    Object.keys(by).forEach(function (k) {
      var x = by[k], pts = x.ids.map(function (id) { return R[id].freeStart; });
      x.peak = Math.max.apply(null, pts.map(function (p) {
        return x.ids.filter(function (id) { return R[id].freeStart <= p && cal.workday(R[id].freeStart, Math.max(R[id].days, 1) - 1) >= p; }).length;
      }));
    });
    return Object.keys(by).map(function (k) { return by[k]; }).sort(function (a, b) { return b.weeks - a.weeks; });
  }

  return { build: build, compute: compute, workload: workload, statusCat: statusCat, tagOf: tagOf, isEpic: isEpic };
})();
