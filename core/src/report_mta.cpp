// 報告: 統計の「マイルストーンの予測の推移」と遅れの警告（Issue #75）。画面の統計ビューと同じ内容を、core/mta の集計から SVG・表にする。
// 絞り込みには従わず、プロジェクト全体。スクリプトは入れない。線種・印・文字でも区別する（色だけに頼らない）。
#include <algorithm>
#include <cmath>

#include "report_internal.hpp"

namespace roadmap::report {

namespace {

constexpr double kW = 960;  // 図の幅（viewBox）
constexpr std::size_t kMaxTableRows = 400;

std::string svgOpen(double w, double h, const std::string& label) {
  return "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" + n1(w) + "\" height=\"" + n1(h) + "\" viewBox=\"0 0 " + n1(w) + " " + n1(h) +
         "\" role=\"img\" aria-label=\"" + esc(label) + "\">";
}

// 破線の指定 dash/gap（dash == 0 は実線 = 属性なし）
std::string dashAttr(int dash, int gap) {
  if (dash <= 0) return {};
  return " stroke-dasharray=\"" + std::to_string(dash) + " " + std::to_string(gap) + "\"";
}

std::string pathLine(double x0, double y0, double x1, double y1, const std::string& color, double w, int dash, int gap, double opacity = 1) {
  std::string o = "<path d=\"M" + n1(x0) + " " + n1(y0) + "L" + n1(x1) + " " + n1(y1) + "\" stroke=\"" + color + "\" stroke-width=\"" + n1(w) +
                  "\" fill=\"none\"" + dashAttr(dash, gap);
  if (opacity < 0.999) o += " stroke-opacity=\"" + alphaStr(opacity) + "\"";
  return o + "/>";
}

// 印（点）。達成済みは中を背景色にした輪
std::string marker(MtaMarker m, double x, double y, double r, const std::string& color, const std::string& bg, bool hollow) {
  const std::string fill = hollow ? bg : color;
  const std::string st = " stroke=\"" + color + "\" stroke-width=\"2\"";
  switch (m) {
    case MtaMarker::Circle:
      return "<circle cx=\"" + n1(x) + "\" cy=\"" + n1(y) + "\" r=\"" + n1(r) + "\" fill=\"" + fill + "\"" + st + "/>";
    case MtaMarker::Square:
      return "<rect x=\"" + n1(x - r) + "\" y=\"" + n1(y - r) + "\" width=\"" + n1(2 * r) + "\" height=\"" + n1(2 * r) + "\" fill=\"" + fill + "\"" + st + "/>";
    case MtaMarker::Triangle:
      return "<path d=\"M" + n1(x) + " " + n1(y - r - 1) + "L" + n1(x + r + 1) + " " + n1(y + r) + "L" + n1(x - r - 1) + " " + n1(y + r) + "Z\" fill=\"" + fill +
             "\"" + st + " stroke-linejoin=\"round\"/>";
    case MtaMarker::Diamond:
      return "<path d=\"M" + n1(x) + " " + n1(y - r - 1) + "L" + n1(x + r + 1) + " " + n1(y) + "L" + n1(x) + " " + n1(y + r + 1) + "L" + n1(x - r - 1) + " " +
             n1(y) + "Z\" fill=\"" + fill + "\"" + st + " stroke-linejoin=\"round\"/>";
    case MtaMarker::Cross:
      return "<path d=\"M" + n1(x - r) + " " + n1(y - r) + "L" + n1(x + r) + " " + n1(y + r) + "M" + n1(x + r) + " " + n1(y - r) + "L" + n1(x - r) + " " +
             n1(y + r) + "\" fill=\"none\"" + st + " stroke-linecap=\"round\"/>";
    case MtaMarker::Plus:
      return "<path d=\"M" + n1(x - r - 1) + " " + n1(y) + "L" + n1(x + r + 1) + " " + n1(y) + "M" + n1(x) + " " + n1(y - r - 1) + "L" + n1(x) + " " + n1(y + r + 1) +
             "\" fill=\"none\"" + st + " stroke-linecap=\"round\"/>";
  }
  return {};
}

struct Label {
  double y = 0;
  std::string text, color;
};

// 右の余白に並べる文字が重ならないよう、上から順に最小の間隔を空ける
void spreadLabels(std::vector<Label>& v, double minGap, double top, double bottom) {
  std::stable_sort(v.begin(), v.end(), [](const Label& a, const Label& b) { return a.y < b.y; });
  double prev = top - minGap;
  for (Label& l : v) {
    l.y = std::max(l.y, prev + minGap);
    prev = l.y;
  }
  // 下にはみ出したら、下から詰め直す
  double next = bottom + minGap;
  for (std::size_t i = v.size(); i-- > 0;) {
    v[i].y = std::min(v[i].y, next - minGap);
    next = v[i].y;
  }
}

Result<MtaReport> compute(const Ctx& cx) {
  const ExportInput& in = cx.in;
  if (!in.weeklyAuto) return Error{"記録がありません"};
  MtaOptions o;
  o.lateDays = in.mtaLateDays;
  return computeMta(in.tree, in.issues, in.rows, in.calendar, *in.weeklyAuto, in.base, cx.bl, o);
}

std::string chart(const Ctx& cx, const MtaReport& rep, const std::vector<std::size_t>& shown) {
  const ThemeColors& k = cx.k;
  const MtaYAxis ax = mtaYAxis(rep, shown);
  if (!ax.ok) return {};
  const int nc = static_cast<int>(rep.columns.size());
  const double left = 46, right = 168, top = 16, plotH = 210, axisH = 22;
  const double plotW = kW - left - right;
  const double colW = plotW / nc;
  const double h = top + plotH + axisH;
  const std::string ink = hex(k.ink), muted = hex(k.muted), lineC = hex(k.line), lateC = hex(k.late);
  const std::string bg = hex(k.dark ? k.bg : k.surface);
  const auto xOf = [&](int i) { return left + colW * (i + 0.5); };
  const double span = std::max(1.0, static_cast<double>(ax.hi - ax.lo));
  const auto yOf = [&](Day d) { return top + (static_cast<double>(ax.hi) - static_cast<double>(d)) / span * plotH; };
  const int every = std::max(1, static_cast<int>(std::ceil(46.0 / colW)));
  const double plotRight = left + plotW;

  std::string g;
  // 記録のない週: 薄い帯と点線の枠（連続する週はひとつの帯）
  for (int i = 0; i < nc;) {
    const MtaColumn& c = rep.columns[static_cast<std::size_t>(i)];
    if (c.recorded || c.now) {
      ++i;
      continue;
    }
    int j = i;
    while (j < nc && !rep.columns[static_cast<std::size_t>(j)].recorded && !rep.columns[static_cast<std::size_t>(j)].now) ++j;
    const double x0 = left + colW * i, w = colW * (j - i);
    g += "<rect x=\"" + n1(x0) + "\" y=\"" + n1(top) + "\" width=\"" + n1(w) + "\" height=\"" + n1(plotH) + "\" fill=\"" + muted +
         "\" fill-opacity=\"0.08\" stroke=\"" + muted + "\" stroke-opacity=\"0.5\" stroke-dasharray=\"2 3\"/>";
    if (w >= textW("記録なし", 11) + 6) addText(g, x0 + w / 2, top + plotH / 2 + 4, "記録なし", TextStyle{11, false, "middle", muted, 0.8});
    i = j;
  }
  // 横線（目盛り）と縦軸の日付
  for (Day t : ax.ticks) {
    const double y = yOf(t);
    g += pathLine(left, y, plotRight, y, lineC, 1, 0, 0);
    addText(g, left - 6, y + 4, fmtShort(t), TextStyle{0, false, "end", muted, 1});
  }
  addText(g, 2, top - 4, "遅れ ↑", TextStyle{11, false, "", muted, 1});
  // 「いま」の列の縦線
  g += pathLine(xOf(nc - 1), top, xOf(nc - 1), top + plotH, muted, 1, 3, 3, 0.7);

  std::vector<Label> labels;
  for (std::size_t order = 0; order < shown.size(); ++order) {
    const MtaSeries& s = rep.series[shown[order]];
    const MtaStyle st = mtaStyleOf(order);
    const std::string col = hex(k.c[st.color % 8]);
    // 期限（長い破線）・計画（点線）は、系列の色の細い横線。右に文字
    if (s.deadline) {
      const double y = yOf(*s.deadline);
      g += pathLine(left, y, plotRight, y, col, 1.4, 10, 5, 0.85);
      labels.push_back({y, std::string(s.plan && *s.plan == *s.deadline ? "期限・計画 " : "期限 ") + fmtShort(*s.deadline) + "（" + s.did + "）", col});
    }
    if (s.plan && !(s.deadline && *s.plan == *s.deadline)) {
      const double y = yOf(*s.plan);
      g += pathLine(left, y, plotRight, y, col, 1.4, 2, 3, 0.85);
      labels.push_back({y, "計画 " + fmtShort(*s.plan) + "（" + s.did + "）", col});
    }
  }
  for (std::size_t order = 0; order < shown.size(); ++order) {
    const MtaSeries& s = rep.series[shown[order]];
    const MtaStyle st = mtaStyleOf(order);
    const std::string col = hex(k.c[st.color % 8]);
    // 線: 隣り合う点のあいだだけ（記録のない週・達成のあとは切る）。破線の位相は区間ごとに引き直さず 1 本のパスで持ち越す
    std::string d;
    bool pen = false;
    for (int i = 0; i < nc; ++i) {
      const MtaPoint& p = s.points[static_cast<std::size_t>(i)];
      if (!p.has) {
        pen = false;
        continue;
      }
      d += std::string(pen ? "L" : "M") + n1(xOf(i)) + " " + n1(yOf(p.date));
      pen = true;
    }
    if (!d.empty()) g += "<path d=\"" + d + "\" stroke=\"" + col + "\" stroke-width=\"2\" fill=\"none\"" + dashAttr(st.dash, st.gap) + "/>";
    int lastI = -1;
    for (int i = 0; i < nc; ++i) {
      const MtaPoint& p = s.points[static_cast<std::size_t>(i)];
      if (!p.has) continue;
      g += marker(st.marker, xOf(i), yOf(p.date), 3.6, col, bg, p.achieved);
      if (p.late) {  // 「!」の丸い印（色だけに頼らない）
        g += "<circle cx=\"" + n1(xOf(i)) + "\" cy=\"" + n1(yOf(p.date) - 15) + "\" r=\"7\" fill=\"" + lateC + "\"/>";
        addText(g, xOf(i), yOf(p.date) - 11, "!", TextStyle{11, true, "middle", "#ffffff", 1});
      }
      if (p.achieved) addText(g, xOf(i) + 7, yOf(p.date) + 14, "達成", TextStyle{11, false, "", muted, 1});
      lastI = i;
    }
    if (lastI >= 0) labels.push_back({yOf(s.points[static_cast<std::size_t>(lastI)].date), s.did.empty() ? mtaSeriesName(s) : s.did, col});
  }
  spreadLabels(labels, 13, top + 6, top + plotH);
  for (const Label& l : labels) addText(g, plotRight + 6, l.y + 4, clipText(l.text, right - 8, 11), TextStyle{11, false, "", l.color, 1});
  g += pathLine(left, top + plotH, plotRight, top + plotH, lineC, 1, 0, 0);
  // 横軸
  for (int i = 0; i < nc; ++i) {
    const MtaColumn& c = rep.columns[static_cast<std::size_t>(i)];
    if (!c.now && i % every != 0) continue;
    addText(g, xOf(i), top + plotH + 15, c.now ? std::string("いま") : fmtShort(c.week), TextStyle{0, c.now, "middle", c.now ? ink : muted, 1});
  }
  // 列ごとの当たり（マウスを載せると、選んだ系列の値）
  for (int i = 0; i < nc; ++i) {
    std::string t;
    for (std::size_t order = 0; order < shown.size(); ++order) {
      if (!t.empty()) t += "\n\n";
      t += mtaPointText(rep.series[shown[order]], rep.columns[static_cast<std::size_t>(i)], static_cast<std::size_t>(i));
    }
    g += "<g><title>" + esc(t) + "</title><rect x=\"" + n1(left + colW * i) + "\" y=\"0\" width=\"" + n1(colW) + "\" height=\"" + n1(h) +
         "\" fill=\"transparent\"/></g>";
  }
  return "<div class=\"gseg\">" + svgOpen(kW, h, "マイルストーンの予測の推移") + g + "</svg></div>\n";
}

std::string legend(const Ctx& cx, const MtaReport& rep, const std::vector<std::size_t>& shown) {
  const ThemeColors& k = cx.k;
  const std::string muted = hex(k.muted), bg = hex(k.dark ? k.bg : k.surface);
  std::string o = "<div class=\"legend\">";
  for (std::size_t order = 0; order < shown.size(); ++order) {
    const MtaSeries& s = rep.series[shown[order]];
    const MtaStyle st = mtaStyleOf(order);
    const std::string col = hex(k.c[st.color % 8]);
    o += "<span><svg width=\"44\" height=\"14\" viewBox=\"0 0 44 14\" aria-hidden=\"true\">" + pathLine(0, 7, 44, 7, col, 2, st.dash, st.gap) +
         marker(st.marker, 22, 7, 3.6, col, bg, false) + "</svg>" + esc(mtaLegendText(s)) + "（" + esc(st.name) + "）</span>";
  }
  o += "<span><svg width=\"44\" height=\"14\" viewBox=\"0 0 44 14\" aria-hidden=\"true\">" + pathLine(0, 7, 44, 7, muted, 1.4, 10, 5) + "</svg>期限（長い破線の横線）</span>";
  o += "<span><svg width=\"44\" height=\"14\" viewBox=\"0 0 44 14\" aria-hidden=\"true\">" + pathLine(0, 7, 44, 7, muted, 1.4, 2, 3) + "</svg>計画（点線の横線）</span>";
  o += "<span><svg width=\"26\" height=\"12\" viewBox=\"0 0 26 12\" aria-hidden=\"true\"><rect x=\"0.5\" y=\"0.5\" width=\"25\" height=\"11\" fill=\"none\" stroke=\"" + muted +
       "\" stroke-dasharray=\"2 2\"/></svg>記録なし（線を引かない）</span>";
  o += "<span><b>!</b> しきい値以上の遅れ</span>";
  return o + "</div>\n";
}

std::string table(const MtaReport& rep) {
  std::string o = "<table><thead><tr><th>マイルストーン</th><th>週</th><th class=\"n\">予測日</th><th>前の点からの動き</th></tr></thead><tbody>\n";
  std::size_t rows = 0, omitted = 0;
  for (const MtaSeries& s : rep.series) {
    bool first = true;
    for (std::size_t c = 0; c < s.points.size(); ++c) {
      const MtaPoint& p = s.points[c];
      if (!p.has && !p.ended) continue;
      if (rows >= kMaxTableRows) {
        ++omitted;
        continue;
      }
      ++rows;
      const bool late = p.late;
      o += "<tr><td class=\"nm\">" + (first ? esc(mtaLegendText(s)) : std::string()) + "</td><td>" + esc(mtaColumnText(rep.columns[c])) +
           "</td><td class=\"n\">" + esc(mtaDateText(p)) + "</td><td" + (late ? " class=\"rk-late\"" : "") + ">" + esc(mtaMoveText(p)) + "</td></tr>\n";
      first = false;
    }
    if (first) {  // 点が 1 つもない（記録のない・削除されたものは出さない）
      continue;
    }
  }
  o += "</tbody></table>\n";
  if (omitted > 0) o += "<p class=\"note\">ほか " + std::to_string(omitted) + " 行は省きました。</p>\n";
  return o;
}

}  // namespace

std::string renderMtaBand(const Ctx& cx) {
  const Result<MtaReport> res = compute(cx);
  if (!res.ok() || res.value().warnings == 0) return {};
  const MtaReport& rep = res.value();
  std::string o = "<div class=\"card\"><h3 class=\"rk-late\">! 遅れの警告 <span class=\"note\">マイルストーンの完了の見込みが、前の週の記録から " +
                  std::to_string(rep.lateDays) + " 営業日以上遅れました</span></h3>\n<ul>\n";
  for (const MtaSeries& s : rep.series)
    if (s.warn) o += "<li class=\"rk-late\">" + esc(mtaWarnText(s)) + "</li>\n";
  return o + "</ul>\n</div>\n";
}

std::string renderMtaCard(const Ctx& cx) {
  const Result<MtaReport> res = compute(cx);
  if (!cx.in.weeklyAuto) return {};
  std::string o = "<div class=\"card\"><h3>マイルストーンの予測の推移 <span class=\"note\">週の頭に記録した、マイルストーンの完了の見込み日</span></h3>\n";
  o += "<p class=\"note\">アプリを開いたとき、週に 1 回までの「完了の見込み日」を覚えています（最大 " + std::to_string(kWeeklyAutoMaxSnapshots) +
       " 週）。線が上へ動けば遅れ、下へ動けば前倒し、同じ高さなら安定です。前の記録から " +
       std::to_string(res.ok() ? res.value().lateDays : mtaClampLateDays(cx.in.mtaLateDays)) + " 営業日以上遅れると「!」を付けます。絞り込みに関係なく、プロジェクト全体です。</p>\n";
  if (!res.ok()) return o + "<p>推移を集計できません。</p></div>\n";
  const MtaReport& rep = res.value();
  if (rep.series.empty() && !rep.hasRecords()) return o + "<p>マイルストーンがありません。</p></div>\n";
  if (!rep.drawable()) return o + "<p>" + esc(mtaPendingText(rep.recordedWeeks)) + "</p></div>\n";
  const std::vector<std::size_t> shown = mtaDefaultShown(rep);
  o += legend(cx, rep, shown);
  o += chart(cx, rep, shown);
  if (shown.size() < rep.series.size()) o += "<p class=\"note\">図には " + std::to_string(shown.size()) + " 本まで出しています。ほかは下の表で見られます。</p>\n";
  const int gaps = static_cast<int>(std::count_if(rep.columns.begin(), rep.columns.end(), [](const MtaColumn& c) { return !c.recorded && !c.now; }));
  if (gaps > 0) o += "<p class=\"note\">記録のない週が " + std::to_string(gaps) + " 週あります（点線の枠の帯。線は引かず、補いません）。</p>\n";
  if (rep.truncatedWeeks) o += "<p class=\"note\">古い週は省きました（最大 " + std::to_string(kMtaMaxWeeks) + " 週）。</p>\n";
  o += table(rep);
  return o + "</div>\n";
}

}  // namespace roadmap::report
