// 報告: ガント（インライン SVG）。座標は core/gantt_geom で、画面と同じ関数から出す。
// 長いガントは行ごとに区切って（チャンク）別々の SVG にし、印刷でページの区切りに合わせる。見出しと背景は最初の SVG にだけ
// 定義して、残りは <use> で使い回す（日の拡大率でも出力が膨らまない）。
#include <algorithm>
#include <cmath>
#include <map>
#include <set>

#include "report_internal.hpp"
#include "roadmap/csv.hpp"
#include "roadmap/text.hpp"

namespace roadmap::report {

namespace {

constexpr double kRowH = 26;
constexpr double kNameW = 300, kAsgW = 84, kStatusW = 92, kDurW = 52, kFloatW = 62;
constexpr double kLeftW = kNameW + kAsgW + kStatusW + kDurW + kFloatW;
constexpr float kAllTimelineW = 900;  // 拡大率「全体」のときの時間軸の幅
constexpr double kPrintPx = 1047;     // A4 横（余白 10mm）の幅（96dpi）
constexpr double kChunkPagePx = 460;  // 1 ページに入れたい高さ（最初のページの見出し・凡例の分を見込む）
constexpr int kPrintDaysPerPanel = 62;


std::string pathNum(double v) { return n1(v); }

struct Sink {
  std::string seps, left, bars, deadline;  // seps / deadline は path の d
};

struct Arrow {
  std::vector<GanttPoint> pts;  // 全体の y（見出しなし）
  int cls = 0;                  // 0 通常 1 余裕0 2 遅れ 3 日付が合わない
  double minY = 0, maxY = 0;
};

// 折れ線の道のりの割合 frac の位置
GanttPoint pointAtFrac(const std::vector<GanttPoint>& pts, double frac) {
  double total = 0;
  for (std::size_t i = 0; i + 1 < pts.size(); ++i) total += std::hypot(pts[i + 1].x - pts[i].x, pts[i + 1].y - pts[i].y);
  const double want = total * frac;
  double run = 0;
  for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
    const double len = std::hypot(pts[i + 1].x - pts[i].x, pts[i + 1].y - pts[i].y);
    if (len > 0 && run + len >= want) {
      const double t = (want - run) / len;
      return {static_cast<float>(pts[i].x + (pts[i + 1].x - pts[i].x) * t), static_cast<float>(pts[i].y + (pts[i + 1].y - pts[i].y) * t)};
    }
    run += len;
  }
  return pts.empty() ? GanttPoint{} : pts.back();
}

// 水平または垂直の線分を y の範囲 [lo, hi] で切る。範囲に入らなければ false
bool clipSeg(GanttPoint p, GanttPoint q, double lo, double hi, GanttPoint& a, GanttPoint& b) {
  if (p.y == q.y) {
    if (p.y < lo || p.y > hi) return false;
    a = p;
    b = q;
    return true;
  }
  const double y0 = std::min(p.y, q.y), y1 = std::max(p.y, q.y);
  if (y1 < lo || y0 > hi) return false;
  a = {p.x, static_cast<float>(std::clamp<double>(p.y, lo, hi))};
  b = {q.x, static_cast<float>(std::clamp<double>(q.y, lo, hi))};
  return true;
}

struct Gantt {
  const Ctx& cx;
  const ThemeColors& k;
  GanttAxis ax;
  GanttColorBy by = GanttColorBy::Root;
  std::string styleZoom;
  double headH = 46;
  double totalW = 0;
  std::size_t nRows = 0;
  std::size_t rowsPerChunk = 20;
  bool detail = false;
  std::vector<int> rootIdx;
  Day capHi = 0;  // 描く日の右端（上限を掛けたもの）

  explicit Gantt(const Ctx& c) : cx(c), k(c.k) {}

  unsigned surface() const { return k.surface; }
  std::string H(unsigned rgb) const { return hex(rgb); }

  unsigned barRgb(const GanttBarColor& c) const {
    switch (c.kind) {
      case GanttBarColor::Kind::Accent: return k.accent;
      case GanttBarColor::Kind::Late: return k.late;
      case GanttBarColor::Kind::TightEdge: return k.tightEdge;
      default: return k.c[static_cast<std::size_t>(((c.index % 8) + 8) % 8)];
    }
  }

  // ---- 共有する定義（パターン・見出し・背景）----
  std::string defs() const {
    std::string d = "<defs>\n";
    // 進行中のバーの斜線（白）
    d += "<pattern id=\"gp\" width=\"6\" height=\"6\" patternUnits=\"userSpaceOnUse\" patternTransform=\"rotate(45)\">"
         "<path d=\"M0 0V6\" stroke=\"#ffffff\" stroke-opacity=\"0.4\" stroke-width=\"2\"/></pattern>\n";
    const auto hatch = [&](const char* id, unsigned rgb) {
      d += std::string("<pattern id=\"") + id + "\" width=\"5\" height=\"5\" patternUnits=\"userSpaceOnUse\" patternTransform=\"rotate(45)\">" +
           "<path d=\"M0 0V5\" stroke=\"" + H(rgb) + "\" stroke-opacity=\"0.75\" stroke-width=\"1.4\"/></pattern>\n";
    };
    hatch("hs-ok", k.muted);
    hatch("hs-tight", k.tightEdge);
    hatch("hs-late", k.late);
    d += "<g id=\"gh\">" + header() + "</g>\n";
    d += "<g id=\"gb\">" + background() + "</g>\n";
    d += "</defs>\n";
    return d;
  }

  std::string header() const {
    std::string o;
    addRect(o, 0, 0, totalW, headH, H(k.surface2));
    o += "<path d=\"M0 " + pathNum(headH - 0.5) + "H" + pathNum(totalW) + "\" stroke=\"" + H(k.line) + "\" fill=\"none\"/>";
    // 左の表の見出し
    const double base = headH - 11;
    const TextStyle lab{11, true, "", H(k.muted), 1};
    addText(o, 8, base, "課題", lab);
    addText(o, kNameW + 8, base, "担当", lab);
    addText(o, kNameW + kAsgW + 8, base, "状況", lab);
    addText(o, kNameW + kAsgW + kStatusW + 8, base, "期間", lab);
    addText(o, kNameW + kAsgW + kStatusW + kDurW + 8, base, "余裕", lab);
    // 時間軸の見出し
    const double rowH2 = headH > 40 ? headH / 2 : headH;
    float lastLabel = -1e9f;
    std::string months, vlines, dayWd, dayWe, weeks;
    const float weekW = ax.px * 7;
    const int every = ganttWeekLabelEvery(weekW, 1.f);
    const int dayEvery = std::max(1, static_cast<int>(std::ceil(24.f / std::max(ax.px, 1.f))));
    for (Day d = ax.rs; d <= capHi; ++d) {
      const Ymd ymd = civilFromDays(d);
      const float x = ax.x(d);
      const bool panelStart = (d - ax.rs) % kPrintDaysPerPanel == 0;
      const int daysToPanelEnd = kPrintDaysPerPanel - ((d - ax.rs) % kPrintDaysPerPanel);
      const bool labelNearPanelEnd = static_cast<float>(daysToPanelEnd) * ax.px < 58.f;
      if (panelStart) {
        // 各パネルの先頭に必ず日付を置き、直前パネルのラベルは境界に近づけない。
        lastLabel = -1e9f;
        const std::string label = d == ax.rs ? std::to_string(ymd.y) + "/" + std::to_string(ymd.m)
                                               : std::to_string(ymd.m) + "/" + std::to_string(ymd.d);
        addText(months, x + 4, rowH2 / 2 + 4, label);
        lastLabel = x;
      } else if ((ymd.d == 1 || d == ax.rs) && !labelNearPanelEnd && x - lastLabel >= 58.f) {
        addText(months, x + 4, rowH2 / 2 + 4, std::to_string(ymd.y) + "/" + std::to_string(ymd.m));
        lastLabel = x;
      }
      if (ymd.d == 1) vlines += "M" + pathNum(x) + " 0v" + pathNum(headH);
      if (styleZoom == "day") {
        const int dayInPanel = (d - ax.rs) % kPrintDaysPerPanel;
        if (dayInPanel % dayEvery == 0) {
          const int w = dow(d);
          std::string& tgt = (w == 0 || w == 6) ? dayWe : dayWd;
          addText(tgt, x + ax.px / 2, rowH2 + rowH2 / 2 + 4, std::to_string(ymd.d));
        }
      } else if (styleZoom == "week" && dow(d) == 1) {
        const int wk = (d - ax.rs) / 7;
        if (wk % every == 0) addText(weeks, x + 2, rowH2 + rowH2 / 2 + 4, fmtShort(d));
      }
    }
    o += "<g fill=\"" + H(k.ink) + "\">" + months + "</g>";
    if (!dayWd.empty()) o += "<g text-anchor=\"middle\" fill=\"" + H(k.ink) + "\">" + dayWd + "</g>";
    if (!dayWe.empty()) o += "<g text-anchor=\"middle\" fill=\"" + H(k.muted) + "\">" + dayWe + "</g>";
    if (!weeks.empty()) o += "<g fill=\"" + H(k.muted) + "\">" + weeks + "</g>";
    if (!vlines.empty()) o += "<path d=\"" + vlines + "\" stroke=\"" + H(k.line) + "\" fill=\"none\"/>";
    // 基準日
    {
      const double bx = ax.x(cx.in.base) + ax.px / 2;
      const double w = 3 * 11 + 8;
      addRect(o, bx - w / 2, headH - 20, w, 19, H(k.accent), 3);
      addText(o, bx, headH - 6, "基準日", TextStyle{11, true, "middle", H(k.accentInk), 1});
    }
    return o;
  }

  std::string leftHeader() const {
    std::string o;
    addRect(o, 0, 0, kLeftW, headH, H(k.surface2));
    o += "<path d=\"M0 " + pathNum(headH - 0.5) + "H" + pathNum(kLeftW) + "\" stroke=\"" + H(k.line) + "\" fill=\"none\"/>";
    const double y = headH - 11;
    const TextStyle lab{11, true, "", H(k.muted), 1};
    addText(o, 8, y, "課題", lab);
    addText(o, kNameW + 8, y, "担当", lab);
    addText(o, kNameW + kAsgW + 8, y, "状況", lab);
    addText(o, kNameW + kAsgW + kStatusW + 8, y, "期間", lab);
    addText(o, kNameW + kAsgW + kStatusW + kDurW + 8, y, "余裕", lab);
    return o;
  }

  std::string background() const {
    const double bodyH = headH + kRowH * static_cast<double>(rowsPerChunk);
    std::string o;
    std::string we, hol, wk;
    const float weekW = ax.px * 7;
    for (Day d = ax.rs; d <= capHi; ++d) {
      const int w = dow(d);
      const float x = ax.x(d);
      if (styleZoom == "day" && (w == 0 || w == 6)) we += "M" + pathNum(x) + " " + pathNum(headH) + "h" + pathNum(ax.px) + "v" + pathNum(bodyH - headH) + "h-" + pathNum(ax.px) + "z";
      if (w >= 1 && w <= 5 && cx.in.calendar.isHoliday(d))
        hol += "M" + pathNum(x) + " " + pathNum(headH) + "h" + pathNum(std::max(ax.px, 1.f)) + "v" + pathNum(bodyH - headH) + "h-" + pathNum(std::max(ax.px, 1.f)) + "z";
      if (weekW >= 18.f && w == 1) wk += "M" + pathNum(x) + " " + pathNum(headH) + "V" + pathNum(bodyH);
    }
    if (!we.empty()) o += "<path d=\"" + we + "\" fill=\"" + H(mix(k.wkend.rgb, k.wkend.a, k.surface)) + "\"/>";
    if (!hol.empty()) o += "<path d=\"" + hol + "\" fill=\"" + H(mix(k.holbg.rgb, k.holbg.a, k.surface)) + "\"/>";
    if (!wk.empty()) o += "<path d=\"" + wk + "\" stroke=\"" + H(mix(k.rowline.rgb, k.rowline.a, k.surface)) + "\" fill=\"none\"/>";
    // 基準日の縦線
    const double bx = ax.x(cx.in.base) + ax.px / 2;
    o += "<path d=\"M" + pathNum(bx) + " " + pathNum(headH) + "V" + pathNum(bodyH) + "\" stroke=\"" + H(mix(k.accent, 0.8, k.surface)) +
         "\" stroke-width=\"1.5\" fill=\"none\"/>";
    // 左の表と時間軸の境
    o += "<path d=\"M" + pathNum(kLeftW) + " 0V" + pathNum(bodyH) + "\" stroke=\"" + H(k.line) + "\" fill=\"none\"/>";
    return o;
  }

  // ---- 1 行 ----
  std::string tipText(int id) const {
    const Tree& T = cx.in.tree;
    const std::size_t u = static_cast<std::size_t>(id);
    const ScheduleRow& r = cx.in.rows[u];
    const Issue& o = cx.in.issues[u];
    std::vector<std::string> L;
    L.push_back(T.did[u] + "  " + T.name[u]);
    L.push_back("計画 " + fmtLong(r.asapStart) + " 〜 " + fmtLong(r.asapFinish) + (r.leaf ? "（" + csv::fmtNum(r.weeks) + "週）" : ""));
    if (!r.done) L.push_back("最遅 " + fmtLong(r.alapStart) + " 〜 " + fmtLong(r.alapFinish));
    if (r.done) L.push_back("完了");
    else if (r.floatDays < 0) L.push_back("余裕 −" + text::jsNumber(std::max(1.0, text::jsRound(std::abs(r.floatDays) / 5.0))) + "週（遅れ）");
    else L.push_back("余裕 " + std::to_string(r.floatDays) + "営業日");
    if (T.ownDl[u]) L.push_back("期限 " + fmtLong(*T.ownDl[u]));
    if (!o.assignee.empty()) L.push_back("担当 " + o.assignee);
    if (r.waited) L.push_back("担当者の別作業待ち");
    if (cx.bl) {
      const BaselineEntry& e = cx.bl->entries[u];
      if (e.state == BaselineState::Added) L.push_back("ベースライン「" + cx.bl->baselineName + "」: 計画になかった");
      else if (e.planFinish)
        L.push_back("計画（" + cx.bl->baselineName + "） " + (e.planStart ? fmtLong(*e.planStart) + " 〜 " : std::string()) + fmtLong(*e.planFinish) +
                    "・差 " + baselineDiffText(e) + (baselineDiffNote(e).empty() ? std::string() : "（" + baselineDiffNote(e) + "）"));
    }
    std::string t;
    for (std::size_t i = 0; i < L.size(); ++i) t += (i ? "\n" : "") + esc(L[i]);
    return "<title>" + t + "</title>";
  }

  void emitRow(Sink& s, const VisibleRow& vr, double y) const {
    const Tree& T = cx.in.tree;
    const std::size_t u = static_cast<std::size_t>(vr.id);
    const ScheduleRow& r = cx.in.rows[u];
    const Issue& o = cx.in.issues[u];
    const Risk risk = riskOf(r.floatDays, r.done);
    const double cy = y + kRowH / 2;
    const double ty = cy + 4;

    // ---- 左の表 ----
    {
      const double indent = std::min(static_cast<double>(std::max(vr.depth, 0)) * 14.0, kNameW - 90);
      const double nx = 8 + indent;
      const std::string did = (T.isLeaf(vr.id) && T.kind[u] == Kind::Ms ? "◆ " : "") + T.did[u];
      const double didW = textW(did, 12) + 6;
      const double nameMax = kNameW - nx - 10;
      std::string nm;
      TextStyle ts;
      ts.bold = vr.isPar;
      openText(nm, nx, ty, ts);
      if (didW < nameMax * 0.6) {
        nm += "<tspan fill=\"" + H(k.muted) + "\" font-weight=\"400\">" + esc(did) + "</tspan> " + esc(clipText(T.name[u], nameMax - didW, 12));
      } else {
        nm += esc(clipText(T.name[u], nameMax, 12));
      }
      nm += "</text>";
      s.left += nm;
      if (!vr.isPar && !o.assignee.empty()) addText(s.left, kNameW + 8, ty, clipText(o.assignee, kAsgW - 12, 12));
      {
        const double sx = kNameW + kAsgW + 8;
        if (vr.isPar) addText(s.left, sx, ty, std::to_string(r.nDone) + "/" + std::to_string(r.n), TextStyle{0, false, "", H(k.muted), 1});
        else addText(s.left, sx, ty, clipText(o.status.empty() ? std::string("To Do") : o.status, kStatusW - 12, 12), TextStyle{0, false, "", H(catInk(k, statusCat(o.status))), 1});
      }
      {
        const double dx = kNameW + kAsgW + kStatusW + 8;
        if (vr.isPar) addText(s.left, dx, ty, std::to_string(r.n) + "件", TextStyle{0, false, "", H(k.muted), 1});
        else if (T.kind[u] == Kind::Ms) addText(s.left, dx, ty, "◆", TextStyle{0, false, "", H(k.muted), 1});
        else addText(s.left, dx, ty, csv::fmtNum(r.weeks) + "w", TextStyle{0, false, "", H(r.done ? k.muted : k.ink), 1});
      }
      {
        const double fx = kNameW + kAsgW + kStatusW + kDurW + 8;
        addText(s.left, fx, ty, floatLabel(r), TextStyle{0, risk == Risk::Late, "", H(riskInk(k, risk)), 1});
      }
    }

    // ---- バー ----
    const GanttRowShape sh = ganttRowShape(ax, T, r, vr.id, vr.isPar);
    const GanttBarColor bc = ganttBarColor(by, risk, rootIdx[u], o.assignee);
    const unsigned fillRgb = r.done ? mix(barRgb(bc), 0.45, k.surface) : barRgb(bc);
    const double by0 = y + 6, bh = kRowH - 12;
    if (sh.kind == GanttRowShape::Kind::Summary) {
      if (sh.hasSlack)
        addRect(s.bars, sh.slackX0, cy - 2, sh.slackX1 - sh.slackX0, 4, H(mix(risk == Risk::Ok ? k.muted : riskInk(k, risk), 0.35, k.surface)));
      // まとめバー: 上の帯と、両端が下に尖った形
      const double x0 = sh.x0, w = sh.x1 - sh.x0, t0 = cy - 6;
      const double tr = std::min(6.0, w / 2);
      s.bars += "<path d=\"M" + pathNum(x0) + " " + pathNum(t0) + "h" + pathNum(w) + "v12l-" + pathNum(tr) + " -6h-" + pathNum(std::max(0.0, w - 2 * tr)) +
                "l-" + pathNum(tr) + " 6z\" fill=\"" + H(r.done ? mix(barRgb(bc), 0.5, k.surface) : barRgb(bc)) + "\"";
      s.bars += detail ? ">" + tipText(vr.id) + "</path>" : "/>";
    } else if (sh.kind == GanttRowShape::Kind::Milestone) {
      const double rr = 7.2;
      s.bars += "<path d=\"M" + pathNum(sh.msX) + " " + pathNum(cy - rr) + "l" + pathNum(rr) + " " + pathNum(rr) + "l-" + pathNum(rr) + " " + pathNum(rr) +
                "l-" + pathNum(rr) + " -" + pathNum(rr) + "z\" fill=\"" + H(fillRgb) + "\"";
      if (risk == Risk::Late) s.bars += " stroke=\"" + H(k.late) + "\" stroke-width=\"1.5\"";
      s.bars += detail ? ">" + tipText(vr.id) + "</path>" : "/>";
    } else {
      if (sh.hasSlack) {
        const char* id = risk == Risk::Late ? "hs-late" : risk == Risk::Tight ? "hs-tight" : "hs-ok";
        addRect(s.bars, sh.slackX0, cy - 3, sh.slackX1 - sh.slackX0, 6, std::string("url(#") + id + ")");
      }
      if (sh.hasOver) addRect(s.bars, sh.overX0, cy - 3, sh.overX1 - sh.overX0, 6, H(mix(k.late, 0.55, k.surface)), 2);
      std::string extra;
      if (!r.done && r.floatDays < 0) extra = "stroke=\"" + H(k.late) + "\" stroke-width=\"2\"";
      else if (!r.done && r.floatDays == 0) extra = "stroke=\"" + H(k.tightEdge) + "\" stroke-width=\"2\"";
      const double bw = sh.x1 - sh.x0;
      if (detail) {
        std::string rc;
        addRect(rc, sh.x0, by0, bw, bh, H(fillRgb), 3, 1, extra);
        rc.resize(rc.size() - 2);  // 末尾の "/>" を外して、説明（title）を子にする
        s.bars += rc + ">" + tipText(vr.id) + "</rect>";
      } else {
        addRect(s.bars, sh.x0, by0, bw, bh, H(fillRgb), 3, 1, extra);
      }
      if (r.prog) addRect(s.bars, sh.x0, by0, bw, bh, "url(#gp)", 3);
      if (r.waited)
        addRect(s.bars, sh.x0, by0, bw, bh, "none", 3, 1, "stroke=\"" + H(k.ink) + "\" stroke-width=\"1.4\" stroke-dasharray=\"3 2\"");
      if (bw > 34) {
        std::string lab = r.done ? std::string("✓") : csv::fmtNum(r.weeks) + "w";
        if (!r.done && r.floatDays < 0) lab += " !";
        addText(s.bars, sh.x0 + 6, ty, lab, TextStyle{11, true, "", "#ffffff", 1});
      }
    }
    if (cx.bl) emitPlan(s, vr, y);
    if (sh.hasDeadline) {
      s.deadline += "M" + pathNum(sh.dlX) + " " + pathNum(cy - 6) + "l6 6l-6 6l-6-6z";
    }
  }

  // ---- 計画のバー（ベースライン。Issue #53）----
  unsigned planRgb() const { return mix(k.muted, 0.7, k.surface); }
  void emitPlan(Sink& s, const VisibleRow& vr, double y) const {
    const Tree& T = cx.in.tree;
    const std::size_t u = static_cast<std::size_t>(vr.id);
    const BaselineEntry& e = cx.bl->entries[u];
    const ScheduleRow& r = cx.in.rows[u];
    const bool isMs = !vr.isPar && T.kind[u] == Kind::Ms;
    const GanttRowShape sh = ganttRowShape(ax, T, r, vr.id, vr.isPar);
    const GanttPlanShape p = ganttPlanShape(ax, sh, e, isMs, 1.f, static_cast<float>(kRowH));
    if (p.kind == GanttPlanShape::Kind::Bar) {
      addRect(s.bars, p.x0, y + p.y0, p.w, p.y1 - p.y0, H(planRgb()), 1, 1, "class=\"plan\"");
    } else if (p.kind == GanttPlanShape::Kind::Milestone) {
      const double cy = y + (p.y0 + p.y1) / 2, rr = 3.2;
      s.bars += "<path class=\"plan\" d=\"M" + pathNum(p.msX) + " " + pathNum(cy - rr) + "l" + pathNum(rr) + " " + pathNum(rr) + "l-" + pathNum(rr) + " " +
                pathNum(rr) + "l-" + pathNum(rr) + " -" + pathNum(rr) + "z\" fill=\"" + H(planRgb()) + "\"/>";
    }
    if (baselineShowLabel(e)) {
      const std::string mark = baselineDiffMark(e);
      const std::string label = (mark.empty() ? std::string() : mark + " ") + baselineDiffText(e);
      const unsigned col = e.state == BaselineState::Late ? k.late : e.state == BaselineState::Early ? k.ok : k.muted;
      addText(s.bars, p.labelX, y + kRowH / 2 + 4, label, TextStyle{11, e.state != BaselineState::OnPlan, "", H(col), 1});
    }
  }

  // ---- 凡例 ----
  std::string legend() const {
    std::string o = "<div class=\"legend\">";
    const auto sw = [&](unsigned rgb, const std::string& label) {
      o += "<span><i class=\"sw\" style=\"background:" + H(rgb) + "\"></i>" + esc(label) + "</span>";
    };
    const Tree& T = cx.in.tree;
    if (by == GanttColorBy::Float) {
      sw(k.late, "遅れ（赤い枠と「!」）");
      sw(k.tightEdge, "余裕小");
      sw(k.accent, "余裕あり");
      sw(k.c[0], "完了");
    } else if (by == GanttColorBy::Assignee) {
      std::map<int, std::string> shown;  // 色番号 → その色の担当者名
      std::set<std::string> names;
      for (std::size_t i = 0; i < nRows; ++i) {
        const std::string a = text::trim(cx.in.issues[static_cast<std::size_t>(cx.rows[i].id)].assignee);
        if (a.empty() || !names.insert(a).second || names.size() > 12) continue;
        std::string& slot = shown[static_cast<int>(nameHash(a) % 8)];
        if (!slot.empty()) slot += "・";
        slot += a;
      }
      for (const auto& [idx, nm] : shown) sw(k.c[static_cast<std::size_t>(idx)], clipText(nm, 260, 12));
      if (names.size() > 12) o += "<span>ほかの担当者</span>";
      if (shown.empty()) o += "<span>担当者なし</span>";
    } else {
      std::set<int> seen;
      std::size_t n = 0;
      for (std::size_t i = 0; i < nRows; ++i) {
        const int id = cx.rows[i].id;
        const int ri = rootIdx[static_cast<std::size_t>(id)];
        if (ri <= 0 || !seen.insert(ri).second || n >= 8) continue;
        // 色番号の根の課題の名前
        const int root = T.rootOf[static_cast<std::size_t>(id)];
        sw(k.c[static_cast<std::size_t>(ri)], clipText(T.name[static_cast<std::size_t>(root)], 220, 12));
        ++n;
      }
      sw(k.c[0], "そのほか");
    }
    o += "<span>斜線 = 余裕</span><span>◇ 期限</span><span>赤い枠と「!」= 遅れ</span><span>橙の枠 = 余裕 0</span>";
    o += "<span>破線の枠 = 担当者の別作業待ち</span>";
    if (cx.bl) {
      sw(planRgb(), "計画のバー（ベースライン「" + clipText(cx.bl->baselineName, 200, 12) + "」）");
      o += "<span>「+3日」「! +3日」= 計画より遅れ（営業日）</span><span>「-2日」= 計画より早い</span>";
    }
    if (cx.opt.showDeps) o += "<span>赤い点線と「!」の矢印 = 日付が合わない依存</span>";
    o += "</div>\n";
    return o;
  }
};

}  // namespace

std::string renderGantt(const Ctx& cx) {
  const ExportInput& in = cx.in;
  const ExportOptions& opt = cx.opt;
  const Tree& T = in.tree;
  std::string out = "<h2>ガント</h2>\n";
  if (cx.rows.empty()) {
    out += "<p class=\"empty\">表示する課題がありません。</p>\n";
    return out;
  }
  Gantt g(cx);
  g.nRows = std::min(cx.rows.size(), std::max<std::size_t>(1, opt.maxGanttRows));
  g.detail = g.nRows <= kReportDetailRows;
  g.by = ganttColorByFromKey(opt.colorBy);
  g.rootIdx = rootIndexMap(T, in.issues);

  // 時間軸
  std::string zoom = opt.zoom;
  if (zoom != "day" && zoom != "week" && zoom != "month" && zoom != "all") zoom = "week";
  g.styleZoom = ganttStyleZoom(zoom, 0);
  g.headH = static_cast<double>(ganttHeaderH(g.styleZoom, 1.f));
  const GanttDayRange rg = ganttDayRange(T, in.rows, in.base, cx.bl);
  g.ax.rs = ganttAxisStart(rg.lo);
  g.ax.hi = rg.hi;
  bool axisCut = false;
  if (g.ax.hi - g.ax.rs > kReportMaxAxisDays) {
    g.ax.hi = g.ax.rs + kReportMaxAxisDays;
    axisCut = true;
  }
  g.capHi = g.ax.hi;
  const int days = g.ax.hi - g.ax.rs + 1;
  g.ax.px = ganttZoomPx(zoom, 1.f, kAllTimelineW - 24.f, days);
  // 印刷用の横幅に合わせ、時間軸を約 2 か月ずつ表示する。
  // 「全体」でも極端に縮小しないよう、月表示相当を下限にする。
  const double timelinePrintW = kPrintPx - kLeftW - 12;
  g.ax.px = std::min(g.ax.px, static_cast<float>(timelinePrintW / kPrintDaysPerPanel));
  if (zoom == "all") g.ax.px = std::max(g.ax.px, 2.4f);
  g.ax.left = static_cast<float>(kLeftW);
  g.totalW = kLeftW + static_cast<double>(days) * static_cast<double>(g.ax.px);

  // チャンク（印刷で 1 ページに入る行数）
  const double rowsFit = std::floor((kChunkPagePx - g.headH) / kRowH);
  g.rowsPerChunk = static_cast<std::size_t>(std::clamp(rowsFit, 8.0, 150.0));
  const std::size_t nChunks = (g.nRows + g.rowsPerChunk - 1) / g.rowsPerChunk;

  // 表示行と矢印
  std::vector<VisibleRow> rows(cx.rows.begin(), cx.rows.begin() + static_cast<std::ptrdiff_t>(g.nRows));
  const std::vector<int> rowOf = ganttRowIndex(T, rows);

  std::vector<Arrow> arrows;
  std::size_t omittedArrows = 0;
  std::vector<std::vector<std::size_t>> perChunk(nChunks);
  if (opt.showDeps) {
    const std::vector<GanttLink> links = ganttLinks(T, in.rows, rowOf, 0, static_cast<int>(g.nRows));
    const double chunkH = kRowH * static_cast<double>(g.rowsPerChunk);
    std::size_t pieces = 0;
    arrows.reserve(links.size());
    for (const GanttLink& l : links) {
      const int ia = rows[static_cast<std::size_t>(l.a)].id, ib = rows[static_cast<std::size_t>(l.b)].id;
      const float cy0 = static_cast<float>(kRowH * l.a + kRowH / 2), cy1 = static_cast<float>(kRowH * l.b + kRowH / 2);
      const GanttArrowEnds e = ganttArrowEnds(g.ax, T, in.rows, ia, ib, cy0, cy1, 1.f);
      Arrow a;
      a.pts = ganttArrowRoute(e, static_cast<float>(kRowH), 1.f);
      a.minY = a.maxY = a.pts.front().y;
      for (const GanttPoint& p : a.pts) {
        a.minY = std::min<double>(a.minY, p.y);
        a.maxY = std::max<double>(a.maxY, p.y);
      }
      const ScheduleRow& sr = in.rows[static_cast<std::size_t>(l.succLeaf)];
      a.cls = l.conflict ? 3 : sr.floatDays < 0 ? 2 : sr.floatDays == 0 ? 1 : 0;
      const std::size_t cLo = std::min(nChunks - 1, static_cast<std::size_t>(std::max(0.0, a.minY) / chunkH));
      const std::size_t cHi = std::min(nChunks - 1, static_cast<std::size_t>(std::max(0.0, a.maxY) / chunkH));
      if (pieces + (cHi - cLo + 1) > kReportMaxArrowPieces) {
        ++omittedArrows;
        continue;
      }
      pieces += cHi - cLo + 1;
      for (std::size_t c = cLo; c <= cHi; ++c) perChunk[c].push_back(arrows.size());
      arrows.push_back(std::move(a));
    }
  }

  // 見出し
  const char* const zoomName = zoom == "day" ? "日" : zoom == "month" ? "月" : zoom == "all" ? "全体" : "週";
  const char* const colorName = g.by == GanttColorBy::Float ? "余裕" : g.by == GanttColorBy::Assignee ? "担当者" : "Epic";
  out += std::string("<p class=\"note\">拡大率: ") + zoomName + "　色分け: " + colorName + "　時間軸は約2か月ごとに分割　‹/› は前後の期間へ続く印　基準日の線: " + escapeXml(fmtLong(in.base)) + "</p>\n";
  if (cx.rows.size() > g.nRows)
    out += "<p class=\"warn\">課題が " + std::to_string(cx.rows.size()) + " 行あるため、ガントには先頭の " + std::to_string(g.nRows) +
           " 行だけを出しています（リストには全部出ます）。</p>\n";
  if (axisCut) out += "<p class=\"warn\">日付の範囲が広いため、ガントの時間軸は " + std::to_string(kReportMaxAxisDays) + " 日までで切っています。</p>\n";
  if (omittedArrows > 0)
    out += "<p class=\"warn\">依存の矢印が多いため、離れた行どうしの矢印 " + std::to_string(omittedArrows) + " 本を省いています。</p>\n";
  if (cx.bl) {
    const std::string at = cx.bl->baselineAt;
    out += "<p class=\"note\">ベースライン: 「" + esc(cx.bl->baselineName) + "」" +
           (at.size() == 16 ? "（保存 " + esc(at.substr(0, 4) + "/" + at.substr(5, 2) + "/" + at.substr(8, 2) + " " + at.substr(11, 5)) + "）" : std::string()) +
           "との差を、各バーの下の薄いバーと「+3日」の形で出しています。</p>\n";
  }
  out += g.legend();

  // 行データは一度だけ SVG defs に置き、各期間のページから <use> で参照する。
  // これにより長期・大規模プロジェクトでも HTML のサイズが期間数に比例して膨らまない。
  struct DrawChunk { std::string left, seps, bars, arrows; double height = 0; };
  std::vector<DrawChunk> drawChunks(nChunks);
  const bool dimOn = opt.scope == ReportScope::Current && !in.dimmed.empty() && in.dimmed.size() == static_cast<std::size_t>(T.n);
  std::string sharedDefs = g.defs();
  sharedDefs.resize(sharedDefs.size() - std::string("</defs>\n").size());
  for (std::size_t c = 0; c < nChunks; ++c) {
    const std::size_t r0 = c * g.rowsPerChunk, r1 = std::min(g.nRows, r0 + g.rowsPerChunk);
    const double chunkTop = kRowH * static_cast<double>(r0);
    DrawChunk& draw = drawChunks[c];
    draw.height = g.headH + kRowH * static_cast<double>(r1 - r0);
    Sink main;
    std::string dimLeft, dimBars;
    for (std::size_t i = r0; i < r1; ++i) {
      const VisibleRow& vr = rows[i];
      const double y = ganttRowY(static_cast<float>(g.headH), static_cast<float>(kRowH), static_cast<int>(i - r0));
      const std::string seg = "M0 " + pathNum(y + kRowH - 0.5) + "H" + pathNum(g.totalW);
      if (vr.depth == 0) main.seps += "M0 " + pathNum(y + 0.5) + "H" + pathNum(g.totalW);
      main.seps += seg;
      if (dimOn && in.dimmed[static_cast<std::size_t>(vr.id)]) {
        Sink d;
        g.emitRow(d, vr, y);
        dimLeft += "<g opacity=\"" + alphaStr(in.dimAlpha) + "\"><g fill=\"" + hex(g.k.ink) + "\">" + d.left + "</g></g>";
        dimBars += "<g opacity=\"" + alphaStr(in.dimAlpha) + "\">" + d.bars;
        if (!d.deadline.empty()) dimBars += "<path d=\"" + d.deadline + "\" fill=\"" + hex(g.k.surface) + "\" stroke=\"" + hex(g.k.dark ? g.k.ink : 0x000000) + "\" stroke-width=\"1.4\"/>";
        dimBars += "</g>";
      } else g.emitRow(main, vr, y);
    }
    draw.left = "<g fill=\"" + hex(g.k.ink) + "\">" + main.left + "</g>" + dimLeft;
    draw.seps = main.seps.empty() ? std::string() : "<path d=\"" + main.seps + "\" stroke=\"" + hex(mix(g.k.rowline.rgb, g.k.rowline.a, g.k.surface)) + "\" fill=\"none\"/>";
    draw.bars = main.bars;
    if (!main.deadline.empty()) draw.bars += "<path d=\"" + main.deadline + "\" fill=\"" + hex(g.k.surface) + "\" stroke=\"" + hex(g.k.dark ? g.k.ink : 0x000000) + "\" stroke-width=\"1.4\"/>";
    draw.bars += dimBars;
    if (!perChunk[c].empty()) {
      const double yLo = chunkTop, yHi = chunkTop + kRowH * static_cast<double>(r1 - r0), dy = g.headH - chunkTop;
      std::string strokes[4], heads[4], marks;
      for (std::size_t ai : perChunk[c]) {
        const Arrow& a = arrows[ai];
        std::string d; bool pen = false; GanttPoint cur;
        for (std::size_t i = 0; i + 1 < a.pts.size(); ++i) {
          GanttPoint p, q;
          if (!clipSeg(a.pts[i], a.pts[i + 1], yLo, yHi, p, q)) { pen = false; continue; }
          if (!pen || cur.x != p.x || cur.y != p.y) d += "M" + pathNum(p.x) + " " + pathNum(p.y + dy);
          if (p.y == q.y) d += "H" + pathNum(q.x); else d += "V" + pathNum(q.y + dy);
          cur = q; pen = true;
        }
        strokes[a.cls] += d;
        const GanttPoint last = a.pts.back();
        if (last.y >= yLo && last.y <= yHi) heads[a.cls] += "M" + pathNum(last.x) + " " + pathNum(last.y + dy) + "l-6-4v8z";
        if (a.cls == 3) {
          const GanttPoint m = pointAtFrac(a.pts, 0.5);
          if (m.y >= yLo && m.y <= yHi) { marks += "<circle cx=\"" + pathNum(m.x) + "\" cy=\"" + pathNum(m.y + dy) + "\" r=\"7\" fill=\"" + hex(g.k.late) + "\"/>"; addText(marks, m.x, m.y + dy + 4, "!", TextStyle{11, true, "middle", "#ffffff", 1}); }
        }
      }
      const unsigned cols[4] = {g.k.arrow, g.k.tightEdge, g.k.late, g.k.late};
      for (int cl = 0; cl < 4; ++cl) {
        if (!strokes[cl].empty()) draw.arrows += "<path d=\"" + strokes[cl] + "\" fill=\"none\" stroke=\"" + hex(cols[cl]) + "\" stroke-width=\"" + (cl == 3 ? "2.4" : "1.3") + "\"" + (cl == 3 ? " stroke-dasharray=\"5 5\"" : "") + "/>";
        if (!heads[cl].empty()) draw.arrows += "<path d=\"" + heads[cl] + "\" fill=\"" + hex(cols[cl]) + "\"/>";
      }
      draw.arrows += marks;
    }
    sharedDefs += "<g id=\"gleft" + std::to_string(c) + "\">" + draw.left + "</g><g id=\"gseps" + std::to_string(c) + "\">" + draw.seps + "</g><g id=\"gbars" + std::to_string(c) + "\">" + draw.bars + "</g><g id=\"garrows" + std::to_string(c) + "\">" + draw.arrows + "</g>\n";
  }
  sharedDefs += "</defs>\n";

  const int panels = (days + kPrintDaysPerPanel - 1) / kPrintDaysPerPanel;
  const double maxTimelineW = timelinePrintW;
  out += "<div class=\"scroll\">\n";
  for (int panel = 0; panel < panels; ++panel) {
    const int firstDay = panel * kPrintDaysPerPanel;
    const double x0 = kLeftW + static_cast<double>(firstDay) * g.ax.px;
    const double timelineW = std::min(maxTimelineW, kPrintDaysPerPanel * static_cast<double>(g.ax.px));
    const double panelW = kLeftW + timelineW;
    for (std::size_t c = 0; c < nChunks; ++c) {
      const DrawChunk& draw = drawChunks[c];
      std::string continuations;
      const std::size_t r0 = c * g.rowsPerChunk, r1 = std::min(g.nRows, r0 + g.rowsPerChunk);
      const double windowEnd = x0 + std::min(kPrintDaysPerPanel, days - firstDay) * static_cast<double>(g.ax.px);
      for (std::size_t i = r0; i < r1; ++i) {
        const VisibleRow& vr = rows[i];
        const ScheduleRow& sr = in.rows[static_cast<std::size_t>(vr.id)];
        const GanttRowShape shape = ganttRowShape(g.ax, T, sr, vr.id, vr.isPar);
        double barLo = shape.kind == GanttRowShape::Kind::Milestone ? shape.msX - 7.2 : shape.x0;
        double barHi = shape.kind == GanttRowShape::Kind::Milestone ? shape.msX + 7.2 : shape.x1;
        if (barLo < x0 && barHi > x0) {
          const double y = ganttRowY(static_cast<float>(g.headH), static_cast<float>(kRowH), static_cast<int>(i - r0)) + kRowH / 2 + 5;
          addText(continuations, x0 + 9, y, "‹", TextStyle{15, true, "middle", hex(g.k.ink), 1});
        }
        if (barLo < windowEnd && barHi > windowEnd) {
          const double y = ganttRowY(static_cast<float>(g.headH), static_cast<float>(kRowH), static_cast<int>(i - r0)) + kRowH / 2 + 5;
          addText(continuations, windowEnd - 9, y, "›", TextStyle{15, true, "middle", hex(g.k.ink), 1});
        }
      }
      std::string svg = "<div class=\"gseg" + std::string(panel > 0 && c == 0 ? " gantt-panel-start" : "") + "\"><svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" + pathNum(panelW) + "\" height=\"" + pathNum(draw.height) + "\" viewBox=\"0 0 " + pathNum(panelW) + " " + pathNum(draw.height) + "\" role=\"img\" aria-label=\"ガント、期間 " + std::to_string(panel + 1) + " / " + std::to_string(panels) + "、行 " + std::to_string(c + 1) + " / " + std::to_string(nChunks) + "\">\n";
      if (panel == 0 && c == 0) svg += sharedDefs;
      svg += "<rect width=\"" + pathNum(panelW) + "\" height=\"" + pathNum(draw.height) + "\" fill=\"" + hex(g.k.surface) + "\"/>" + g.leftHeader();
      svg += "<svg x=\"" + pathNum(kLeftW) + "\" y=\"0\" width=\"" + pathNum(timelineW) + "\" height=\"" + pathNum(draw.height) + "\" viewBox=\"" + pathNum(x0) + " 0 " + pathNum(timelineW) + " " + pathNum(draw.height) + "\"><use href=\"#gb\"/><use href=\"#gh\"/><use href=\"#gbars" + std::to_string(c) + "\"/><use href=\"#garrows" + std::to_string(c) + "\"/>" + continuations + "</svg>";
      svg += "<use href=\"#gseps" + std::to_string(c) + "\"/><use href=\"#gleft" + std::to_string(c) + "\"/>";
      svg += "</svg></div>\n";
      out += svg;
    }
  }
  out += "</div>\n";
  return out;
}

}  // namespace roadmap::report
