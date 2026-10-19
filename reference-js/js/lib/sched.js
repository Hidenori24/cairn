// 営業日カレンダー計算（土日と休業日を除く）。順方向（最早）・逆方向（最遅）の計算は app.js の compute() にあります。
// Backward/forward scheduler (port of model.py). Dates are integer days since 1970-01-01 (UTC).
var Sched = (function () {
  function parse(iso) {
    var p = iso.split("-");
    return Math.floor(Date.UTC(+p[0], +p[1] - 1, +p[2]) / 86400000);
  }
  function fmt(n) {
    var d = new Date(n * 86400000);
    var m = d.getUTCMonth() + 1, day = d.getUTCDate();
    return d.getUTCFullYear() + "-" + (m < 10 ? "0" : "") + m + "-" + (day < 10 ? "0" : "") + day;
  }
  function dow(n) { return (n + 4) % 7; } // 0 = Sunday

  function make(holidays) {
    var hol = {};
    holidays.forEach(function (h) { hol[parse(h)] = true; });
    function isWork(n) { var w = dow(n); return w >= 1 && w <= 5 && !hol[n]; }
    function workday(n, k) {
      if (k === 0) return n;
      var step = k > 0 ? 1 : -1, c = Math.abs(k);
      while (c) { n += step; if (isWork(n)) c--; }
      return n;
    }
    function lastWork(n) { return workday(n + 1, -1); }
    function nd(a, b) { var c = 0; for (var d = a; d <= b; d++) if (isWork(d)) c++; return c; }
    function floatDays(asapStart, alapStart) {
      return alapStart >= asapStart ? nd(asapStart, alapStart) - 1 : -(nd(alapStart, asapStart) - 1);
    }
    return { isWork: isWork, workday: workday, lastWork: lastWork, floatDays: floatDays, hol: hol };
  }

  function weeksOf(t, p) {
    switch (t.w) {
      case "C1B": return p.route === "nb" ? p.nb_select_w : 1;
      case "C11": return p.route === "nb" ? p.nb_exam_w : p.self_review_w;
      case "S6": return p.s6_w;
      case "S9": return p.s9_w;
      default: return t.w;
    }
  }

  function compute(tasks, holidays, params, baseIso) {
    var cal = make(holidays);
    var byId = {}, succs = {};
    tasks.forEach(function (t) { byId[t.id] = t; succs[t.id] = []; });
    tasks.forEach(function (t) { t.preds.forEach(function (q) { succs[q].push(t.id); }); });
    // listed order is topological (preds always listed earlier)
    var days = {}, asap = {}, alap = {};
    var base = parse(baseIso);
    var first = cal.workday(base - 1, 1);
    tasks.forEach(function (t) {
      days[t.id] = Math.max(Math.round(weeksOf(t, params) * 5), 1);
      var s = first;
      if (t.preds.length) {
        var m = -Infinity;
        t.preds.forEach(function (q) { m = Math.max(m, asap[q][1]); });
        s = cal.workday(m, 1);
      }
      asap[t.id] = [s, cal.workday(s, days[t.id] - 1)];
    });
    for (var i = tasks.length - 1; i >= 0; i--) {
      var t = tasks[i], c = [];
      if (t.deadline) c.push(cal.lastWork(parse(t.deadline)));
      succs[t.id].forEach(function (s) { c.push(cal.workday(alap[s][0], -1)); });
      var f = Math.min.apply(null, c);
      alap[t.id] = [cal.workday(f, -(days[t.id] - 1)), f];
    }
    var out = {};
    tasks.forEach(function (t) {
      var fl = cal.floatDays(asap[t.id][0], alap[t.id][0]);
      out[t.id] = {
        weeks: weeksOf(t, params), days: days[t.id],
        asapStart: asap[t.id][0], asapFinish: asap[t.id][1],
        alapStart: alap[t.id][0], alapFinish: alap[t.id][1],
        float: fl, status: fl < 0 ? "遅れ" : (fl <= 10 ? "余裕小" : "OK")
      };
    });
    return { result: out, cal: cal, succs: succs };
  }
  return { parse: parse, fmt: fmt, dow: dow, make: make, compute: compute };
})();
