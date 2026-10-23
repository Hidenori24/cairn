// 報告: 統計（画面の統計ビューと同じ内容を、core/stats・core/load の集計から SVG にする）
#include <algorithm>
#include <cmath>

#include "report_internal.hpp"
#include "roadmap/gantt_geom.hpp"
#include "roadmap/load.hpp"
#include "roadmap/stats.hpp"
#include "roadmap/text.hpp"

namespace roadmap::report {

namespace {

constexpr double kW = 960;  // 図の幅（viewBox）
constexpr double kRow = 26;
constexpr std::size_t kLinesPerSvg = 24;  // 1 つの SVG に入れる行数（印刷でページをまたぐとき、行の途中で切れないように）


std::string svgOpen(double w, double h, const std::string& label) {
  return "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" + n1(w) + "\" height=\"" + n1(h) + "\" viewBox=\"0 0 " + n1(w) + " " + n1(h) +
         "\" role=\"img\" aria-label=\"" + esc(label) + "\">";
}

std::string pct(double v) { return std::to_string(static_cast<int>(v * 100 + 0.5)) + "%"; }

std::string md(Day d) {
  const Ymd y = civilFromDays(d);
  return std::to_string(y.m) + "/" + std::to_string(y.d);
}

struct StatsCx {
  const Ctx& cx;
  const ThemeColors& k;
  std::string H(unsigned rgb) const { return hex(rgb); }
  unsigned surface() const { return k.surface; }
};

void legendSw(std::string& o, const std::string& color, const std::string& label) {
  o += "<span><i class=\"sw\" style=\"background:" + color + "\"></i>" + esc(label) + "</span>";
}

// 積み上げ棒（横）。合計 0 のときは枠だけ
void stackedBar(const StatsCx& s, std::string& o, double x, double y, double w, double h, double a, double b, double c) {
  const ThemeColors& k = s.k;
  addRect(o, x, y, w, h, hex(k.surface3), 3);
  const double total = a + b + c;
  if (total > 0) {
    double x0 = x, acc = 0;
    const double vals[3] = {a, b, c};
    const unsigned cols[3] = {k.ok, k.accent, k.line2};
    for (int i = 0; i < 3; ++i) {
      if (vals[i] <= 0) continue;
      acc += vals[i];
      const double x1 = x + w * (acc / total);
      if (x1 - x0 >= 1.0) addRect(o, x0, y, x1 - x0, h, hex(cols[i]));
      x0 = x1;
    }
  }
  o += "<rect x=\"" + n1(x) + "\" y=\"" + n1(y) + "\" width=\"" + n1(w) + "\" height=\"" + n1(h) + "\" rx=\"3\" fill=\"none\" stroke=\"" + hex(k.line) + "\"/>";
}

struct GroupLine {
  std::string title;
  double nDone = 0, nProg = 0, nTodo = 0;
  std::string detail;
  bool late = false;
};

// 担当別・ルート別・優先度別の行（名前・積み上げ棒・説明）。行が多いときは区切って複数の SVG にする
std::string groupRows(const StatsCx& s, const std::vector<GroupLine>& lines, double nameW, double barW, const std::string& label) {
  const ThemeColors& k = s.k;
  std::string out;
  for (std::size_t r0 = 0; r0 < lines.size(); r0 += kLinesPerSvg) {
    const std::size_t r1 = std::min(lines.size(), r0 + kLinesPerSvg);
    std::string o;
    for (std::size_t i = r0; i < r1; ++i) {
      const GroupLine& g = lines[i];
      const double y = kRow * static_cast<double>(i - r0);
      const double ty = y + kRow / 2 + 4;
      addText(o, 6, ty, clipText(g.title, nameW - 12, 12), TextStyle{0, false, "", hex(k.ink), 1});
      stackedBar(s, o, nameW, y + (kRow - 12) / 2, barW, 12, g.nDone, g.nProg, g.nTodo);
      const double dx = nameW + barW + 12;
      addText(o, dx, ty, clipText(g.detail, std::max(kW - dx - 6, 40.0), 12), TextStyle{0, g.late, "", hex(g.late ? k.late : k.muted), 1});
      o += "<path d=\"M0 " + n1(y + kRow - 0.5) + "H" + n1(kW) + "\" stroke=\"" + hex(mix(k.rowline.rgb, k.rowline.a, k.surface)) + "\" fill=\"none\"/>";
    }
    out += "<div class=\"gseg\">" + svgOpen(kW, kRow * static_cast<double>(r1 - r0), label) + o + "</svg></div>\n";
  }
  return out;
}

std::string groupText(const GroupStat& g) {
  std::string s = "完了 " + std::to_string(g.nDone) + "／" + std::to_string(g.n) + "件";
  if (g.hasOpen) s += "・残り " + n1(g.weeksOpen()) + "週";
  if (g.nLate > 0) s += "・! 遅れ " + std::to_string(g.nLate);
  else if (g.nTight > 0) s += "・余裕小 " + std::to_string(g.nTight);
  return s;
}

std::string omittedNote(std::size_t shown, std::size_t total) {
  if (total <= shown) return {};
  return "<p class=\"warn\">全 " + std::to_string(total) + " 行のうち、先頭の " + std::to_string(shown) + " 行だけを出しています。</p>\n";
}

// ---- 全体 ----
std::string overall(const StatsCx& s, const ProjectStats& st) {
  const ThemeColors& k = s.k;
  std::string o = "<div class=\"card\"><h3>全体</h3>\n<div class=\"tiles\">";
  const auto tile = [&](const std::string& label, const std::string& value, const char* cls) {
    o += "<div class=\"tile\"><div class=\"l\">" + esc(label) + "</div><div class=\"v" + (cls && *cls ? std::string(" ") + cls : std::string()) + "\">" +
         esc(value) + "</div></div>";
  };
  tile("進み（期間ベース）", pct(st.progress()), "");
  tile("作業", std::to_string(st.done) + " / " + std::to_string(st.tasks), "");
  tile("遅れ", st.late > 0 ? "! " + std::to_string(st.late) : "0", st.late > 0 ? "rk-late" : "");
  tile("余裕小（10日以内）", std::to_string(st.tight), st.tight > 0 ? "rk-tight" : "");
  tile("完了の見込み", st.hasFinish ? fmtShort(st.finish) : std::string("—"), "");
  tile("担当なし", std::to_string(st.unassigned), st.unassigned > 0 ? "rk-tight" : "");
  o += "</div>\n";
  std::string svg;
  stackedBar(s, svg, 0, 0, kW, 16, st.done, st.prog, st.todo);
  o += "<div class=\"gseg\">" + svgOpen(kW, 18, "状況の内訳") + svg + "</svg></div>\n<div class=\"legend\">";
  legendSw(o, hex(k.ok), "完了 " + std::to_string(st.done));
  legendSw(o, hex(k.accent), "進行中 " + std::to_string(st.prog));
  legendSw(o, hex(k.line2), "未着手 " + std::to_string(st.todo));
  o += "</div>\n";
  std::string extra = "全体 " + n1(st.weeks) + " 週のうち " + n1(st.weeksDone) + " 週が完了";
  if (st.milestones > 0)
    extra += "　マイルストーン " + std::to_string(st.msDone) + " / " + std::to_string(st.milestones) + " 達成" +
             (st.msLate > 0 ? "（! 遅れ " + std::to_string(st.msLate) + "）" : "");
  if (st.noDeadline > 0) extra += "　期限なしの未完了 " + std::to_string(st.noDeadline) + " 件";
  o += "<p class=\"note\">" + esc(extra) + "</p></div>\n";
  return o;
}

// ---- 余裕の分布 ----
std::string floatDist(const StatsCx& s, const ProjectStats& st) {
  const ThemeColors& k = s.k;
  static const char* const labels[kFloatBuckets] = {"遅れ", "0〜5日", "6〜10日", "11〜20日", "21日以上"};
  const unsigned cols[kFloatBuckets] = {k.late, k.tight, k.tight, k.ok, k.ok};
  int mx = 1;
  for (int h : st.floatHist) mx = std::max(mx, h);
  const double lw = 90, cw = 40, bw = std::max(kW - lw - cw - 8, 80.0);
  std::string o;
  for (int i = 0; i < kFloatBuckets; ++i) {
    const double y = 24.0 * i, ty = y + 12 + 4;
    addText(o, 6, ty, i == 0 ? std::string("! ") + labels[i] : std::string(labels[i]), TextStyle{0, i == 0, "", hex(k.ink), 1});
    const double len = bw * st.floatHist[i] / mx;
    if (len > 0.5) addRect(o, lw, y + 4, len, 16, hex(cols[i]), 3);
    addText(o, lw + len + 8, ty, std::to_string(st.floatHist[i]), TextStyle{0, false, "", hex(k.muted), 1});
  }
  return "<div class=\"card\"><h3>余裕の分布 <span class=\"note\">未完了の作業</span></h3>\n<div class=\"gseg\">" +
         svgOpen(kW, 24.0 * kFloatBuckets, "余裕の分布") + o + "</svg></div></div>\n";
}

// ---- 週ごとの完了 ----
std::string weekly(const StatsCx& s, const ProjectStats& st) {
  const ThemeColors& k = s.k;
  const auto& wk = st.weeksTimeline;
  std::string o;
  if (wk.empty()) return {};
  int mx = 1;
  for (const auto& b : wk) mx = std::max({mx, b.actual, b.forecast});
  const double chartH = 110, axisH = 20;
  const double bw = std::clamp(kW / static_cast<double>(wk.size()), 10.0, 48.0);
  o += "<path d=\"M0 " + n1(chartH) + "H" + n1(bw * static_cast<double>(wk.size())) + "\" stroke=\"" + hex(k.line) + "\" fill=\"none\"/>";
  const int every = std::max(1, static_cast<int>(std::ceil(34.0 / bw)));  // 日付の見出しが重ならないように間引く
  for (std::size_t i = 0; i < wk.size(); ++i) {
    const double x = bw * static_cast<double>(i);
    const bool now = static_cast<int>(i) == st.nowIndex;
    if (now) addRect(o, x, 0, bw, chartH, hex(mix(k.selbg.rgb, k.selbg.a, k.surface)));
    const int n = wk[i].actual + wk[i].forecast;
    if (n > 0) {
      const double hh = (chartH - 18) * n / mx;
      addRect(o, x + bw * 0.18, chartH - hh, bw * 0.64, hh, hex(wk[i].actual > 0 ? k.ok : k.accent), 2);
      addText(o, x + bw / 2, chartH - hh - 3, std::to_string(n), TextStyle{0, false, "middle", hex(k.muted), 1});
    }
    if (static_cast<int>(i) % every == 0 || now)
      addText(o, x + bw / 2, chartH + 15, md(wk[i].start), TextStyle{0, now, "middle", hex(now ? k.ink : k.muted), 1});
  }
  std::string r = "<div class=\"card\"><h3>週ごとの完了 <span class=\"note\">過去は完了日（Resolved）、先は最早の完了日。強調した週が基準日の週</span></h3>\n<div class=\"gseg\">" +
                  svgOpen(kW, chartH + axisH, "週ごとの完了") + o + "</svg></div>\n<div class=\"legend\">";
  legendSw(r, hex(k.ok), "完了した数");
  legendSw(r, hex(k.accent), "完了の見込み");
  r += "</div></div>\n";
  return r;
}

// ---- 担当者別の負荷（ヒートマップ）----
struct HeatFill {
  unsigned rgb = 0;
  double a = 0;
};
HeatFill heatFill(const ThemeColors& k, int pc) {
  if (pc <= 0) return {0, 0};
  if (pc < 80) return {k.ok, 0.10 + 0.40 * pc / 80.0};
  const double t = (pc - 80) / 20.0;
  if (pc <= 100) return {k.tight, k.dark ? 0.80 + 0.15 * t : 0.30 + 0.28 * t};
  return {k.late, 0.92};
}

std::string heatmap(const StatsCx& s, const LoadReport& lr, const Tree& T, const std::vector<ScheduleRow>& R, const ExportInput& in) {
  const ThemeColors& k = s.k;
  std::string o = "<div class=\"card\"><h3>担当者別の負荷（週ごと） <span class=\"note\">未完了の作業を、最早の開始〜完了の営業日に均等に広げた量です</span></h3>\n";
  if (lr.load.empty() || lr.weekStarts.empty()) return o + "<p>未完了の作業がないので、負荷はありません。</p></div>\n";
  const std::size_t nw = lr.weekStarts.size();
  const double nameW = std::clamp(kW * 0.16, 90.0, 140.0), tailW = 92;
  const double cw = std::clamp((kW - nameW - tailW) / static_cast<double>(nw), 16.0, 44.0);
  const double gridW = cw * static_cast<double>(nw);
  const bool showTail = nameW + gridW + tailW <= kW + 0.5;

  o += "<div class=\"legend\">";
  legendSw(o, hex(mix(k.ok, heatFill(k, 40).a, k.surface)), "80% 未満");
  legendSw(o, hex(mix(k.tight, heatFill(k, 90).a, k.surface)), "80〜100%");
  legendSw(o, hex(mix(k.late, 0.92, k.surface)), "100% 超（!）");
  o += "</div>\n<p class=\"note\">";
  if (lr.cap > 1) o += "100% = 同時に " + std::to_string(lr.cap) + " 件が動いている状態（資源制限）。週の営業日数は休業日を除きます。";
  else o += "100% = 週の営業日ぶんの作業が 1 人に入っている状態です（資源制限で同時件数を増やしたときは、その件数が 100%）。";
  if (lr.lastFolded) o += "最後の列は、それより先の週をまとめたものです。";
  o += "</p>\n";

  const std::size_t nPeople = std::min(lr.load.size(), kReportMaxStatRows);
  const bool withTitles = nPeople * nw <= 3000;
  const bool withTasks = nPeople * nw <= 800;
  const std::size_t every = static_cast<std::size_t>(std::max(1.0, std::ceil(34.0 / cw)));  // 日付の見出しが重ならないように間引く
  for (std::size_t r0 = 0; r0 < nPeople; r0 += kLinesPerSvg) {
    const std::size_t r1 = std::min(nPeople, r0 + kLinesPerSvg);
    std::string g;
    const double headY = 14;
    for (std::size_t w = 0; w < nw; w += every) {
      const Ymd ymd = civilFromDays(lr.weekStarts[w]);
      addText(g, nameW + cw * static_cast<double>(w) + cw / 2, headY, std::to_string(ymd.m) + "/" + std::to_string(ymd.d),
              TextStyle{0, false, "middle", hex(w == 0 ? k.ink : k.muted), 1});
    }
    const double top = 20;
    for (std::size_t i = r0; i < r1; ++i) {
      const LoadRow& row = lr.load[i];
      const double y = top + kRow * static_cast<double>(i - r0);
      const double ty = y + kRow / 2 + 4;
      const std::string who = row.name.empty() ? std::string("（担当なし）") : row.name;
      addText(g, 6, ty, clipText(who, nameW - 12, 12), TextStyle{0, false, "", hex(k.ink), 1});
      addRect(g, nameW, y + 1, gridW, kRow - 2, hex(k.surface2), 3);
      for (std::size_t w = 0; w < nw; ++w) {
        const LoadCell& c = row.cells[w];
        const int pc = loadPercent(c.ratio);
        if (pc <= 0) continue;
        const double cx0 = nameW + cw * static_cast<double>(w);
        const HeatFill hf = heatFill(k, pc);
        std::string cell;
        addRect(cell, cx0 + 1, y + 1, cw - 2, kRow - 2, hex(mix(hf.rgb, hf.a, k.surface2)), 3);
        const std::string digits = std::to_string(pc);
        const std::string cands[3] = {pc > 100 ? "!" + digits : digits, digits, pc > 100 ? std::string("!") : std::string()};
        for (const std::string& t : cands) {
          if (t.empty()) break;
          if (textW(t, 11) > cw - 3) continue;
          addText(cell, cx0 + cw / 2, ty, t, TextStyle{11, pc > 100, "middle", hex((pc > 100 || (pc >= 80 && k.dark)) ? k.accentInk : k.ink), 1});
          break;
        }
        if (withTitles && c.n > 0) {
          const bool folded = lr.lastFolded && w + 1 == nw;
          std::string tt = who + " ・ " + md(lr.weekStarts[w]) + (folded ? " 以降" : " の週") + "\n負荷 " + std::to_string(pc) + "%" +
                           (pc > 100 ? "（オーバー）" : "") + "\n作業 " + n1(c.days) + " 営業日 ÷ 週の営業日 " + std::to_string(lr.workdays[w]) +
                           (lr.cap > 1 ? " × 同時 " + std::to_string(lr.cap) + " 件" : std::string());
          std::string title = "<title>" + esc(tt);
          if (withTasks) {
            for (int id : c.ids) {
              if (id < 0 || id >= T.n) continue;
              const ScheduleRow& r = R[static_cast<std::size_t>(id)];
              title += "\n" + esc("・" + T.name[static_cast<std::size_t>(id)] + "  （" + md(r.asapStart) + "〜" + md(r.asapFinish) + "）");
            }
            if (c.n > static_cast<int>(c.ids.size())) title += "\n" + esc("ほか " + std::to_string(c.n - static_cast<int>(c.ids.size())) + " 件");
          }
          title += "</title>";
          g += "<g>" + title + cell + "</g>";
        } else {
          g += cell;
        }
      }
      if (showTail && row.peak > 0) {
        const int pk = loadPercent(row.peak);
        addText(g, nameW + gridW + 8, ty, "最大 " + std::to_string(pk) + "%" + (pk > 100 ? " !" : ""),
                TextStyle{0, pk > 100, "", hex(pk > 100 ? k.late : k.muted), 1});
      }
    }
    o += "<div class=\"gseg\">" + svgOpen(kW, top + kRow * static_cast<double>(r1 - r0), "担当者別の負荷") + g + "</svg></div>\n";
  }
  o += omittedNote(nPeople, lr.load.size());
  (void)in;
  return o + "</div>\n";
}

// ---- 担当者別の完了（過去 8 週）----
std::string doneByAssignee(const StatsCx& s, const LoadReport& lr) {
  const ThemeColors& k = s.k;
  std::string o = "<div class=\"card\"><h3>担当者別の完了（過去 8 週） <span class=\"note\">完了日（Resolved）の週ごと。順位ではなく、誰が何を終えたかの記録です</span></h3>\n";
  if (lr.done.empty()) return o + "<p>過去 8 週に完了日（Resolved）が入った作業はありません。</p></div>\n";
  const std::size_t nw = lr.doneWeekStarts.size();
  const auto colorOf = [&](const DoneRow& d) { return d.name.empty() ? k.line2 : k.c[nameHash(d.name) % 8]; };
  std::vector<int> total(nw, 0);
  int mx = 1;
  for (std::size_t w = 0; w < nw; ++w) {
    for (const DoneRow& d : lr.done) total[w] += w < d.perWeek.size() ? d.perWeek[w] : 0;
    mx = std::max(mx, total[w]);
  }
  const double chartH = 110, axisH = 20;
  const double bw = std::clamp(kW * 0.6 / static_cast<double>(std::max<std::size_t>(1, nw)), 30.0, 72.0);
  std::string g;
  g += "<path d=\"M0 " + n1(chartH) + "H" + n1(bw * static_cast<double>(nw)) + "\" stroke=\"" + hex(k.line) + "\" fill=\"none\"/>";
  for (std::size_t w = 0; w < nw; ++w) {
    const double x = bw * static_cast<double>(w);
    const double unit = (chartH - 18) / mx;
    double y = chartH;
    for (const DoneRow& d : lr.done) {
      const int n = w < d.perWeek.size() ? d.perWeek[w] : 0;
      if (n <= 0) continue;
      const double top = y - unit * n;
      addRect(g, x + bw * 0.18, top, bw * 0.64, y - top, hex(colorOf(d)));
      g += "<path d=\"M" + n1(x + bw * 0.18) + " " + n1(top) + "h" + n1(bw * 0.64) + "\" stroke=\"" + hex(k.surface) + "\" fill=\"none\"/>";
      y = top;
    }
    if (total[w] > 0) addText(g, x + bw / 2, y - 3, std::to_string(total[w]), TextStyle{0, false, "middle", hex(k.muted), 1});
    addText(g, x + bw / 2, chartH + 15, md(lr.doneWeekStarts[w]), TextStyle{0, false, "middle", hex(k.muted), 1});
  }
  o += "<div class=\"gseg\">" + svgOpen(bw * static_cast<double>(nw), chartH + axisH, "担当者別の完了") + g + "</svg></div>\n";
  // 表
  o += "<table><thead><tr><th>担当者</th><th class=\"n\">完了した数</th><th class=\"n\">期間の合計</th><th class=\"n\">平均の期間</th></tr></thead><tbody>\n";
  int sumN = 0;
  double sumW = 0;
  const std::size_t shown = std::min(lr.done.size(), kReportMaxStatRows);
  for (std::size_t i = 0; i < shown; ++i) {
    const DoneRow& d = lr.done[i];
    o += "<tr><td><i class=\"sw\" style=\"background:" + hex(colorOf(d)) + "\"></i> " + esc(d.name.empty() ? std::string("（担当なし）") : d.name) +
         "</td><td class=\"n\">" + std::to_string(d.n) + " 件</td><td class=\"n\">" + n1(d.weeks) + " 週</td><td class=\"n\">" +
         (d.n > 0 ? n1(d.weeks / d.n) + " 週" : std::string("—")) + "</td></tr>\n";
  }
  for (const DoneRow& d : lr.done) {
    sumN += d.n;
    sumW += d.weeks;
  }
  if (lr.done.size() > 1)
    o += "<tr><td class=\"id\">合計</td><td class=\"n id\">" + std::to_string(sumN) + " 件</td><td class=\"n id\">" + n1(sumW) + " 週</td><td class=\"n id\">" +
         (sumN > 0 ? n1(sumW / sumN) + " 週" : std::string("—")) + "</td></tr>\n";
  o += "</tbody></table>\n" + omittedNote(shown, lr.done.size()) + "</div>\n";
  return o;
}

std::string atText(const std::string& at) {
  if (at.size() != 16) return {};
  return at.substr(0, 4) + "/" + at.substr(5, 2) + "/" + at.substr(8, 2) + " " + at.substr(11, 5);
}

// 「計画との差」の節（ベースラインを選んでいるとき。Issue #53）
std::string baselineSection(const StatsCx& s) {
  const BaselineCompare* bc = s.cx.bl;
  if (!bc) return {};
  const Tree& T = s.cx.in.tree;
  const BaselineSummary& sm = bc->sum;
  const std::string at = atText(bc->baselineAt);
  std::string o = "<div class=\"card\"><h3>計画との差 <span class=\"note\">ベースライン「" + esc(bc->baselineName) + "」" +
                  (at.empty() ? std::string() : "（保存 " + esc(at) + "）") + "との比較。営業日の差で、+ は計画より遅れ</span></h3>\n";
  if (bc->truncated) o += "<p class=\"note\">ベースラインは課題が多かったため、一部の課題だけです。</p>\n";
  const auto tile = [&](const char* label, int n, const char* cls) {
    return std::string("<div class=\"tile\"><div class=\"l\">") + label + "</div><div class=\"v" + (*cls ? std::string(" ") + cls : std::string()) +
           "\">" + std::to_string(n) + " 件</div></div>\n";
  };
  o += "<div class=\"tiles\">\n";
  o += tile("早まり", sm.early, "rk-ok");
  o += tile("計画どおり", sm.onPlan, "");
  o += tile(sm.late > 0 ? "! 遅れ" : "遅れ", sm.late, sm.late > 0 ? "rk-late" : "");
  o += tile("計画になかった", sm.added, "");
  o += tile("消えた", sm.removed, "");
  o += "</div>\n";
  if (sm.sameDayDiffers > 0)
    o += "<p class=\"note\">営業日の差が 0 でも完了日が違う課題が " + std::to_string(sm.sameDayDiffers) + " 件あります（休業日・土日をまたいだ日付の違い）。</p>\n";
  o += "<h3>遅れの大きい順 <span class=\"note\">" + std::to_string(bc->topLate.size()) + " 件 / 遅れ全体 " + std::to_string(bc->lateTotal) + " 件</span></h3>\n";
  if (bc->topLate.empty()) {
    o += "<p class=\"note\">計画より遅れている課題はありません。</p>\n";
  } else {
    o += "<table><thead><tr><th>課題</th><th>担当</th><th>計画の完了</th><th>今の見込み</th><th class=\"n\">差</th></tr></thead>\n<tbody>\n";
    for (int i : bc->topLate) {
      const std::size_t k = static_cast<std::size_t>(i);
      const BaselineEntry& e = bc->entries[k];
      const std::string who = text::trim(s.cx.in.issues[k].assignee);
      o += "<tr><td class=\"nm\"><span class=\"id\">" + esc(T.did[k]) + "</span> " + esc(T.name[k]) + "</td><td>" + (who.empty() ? std::string("（担当なし）") : esc(who)) +
           "</td><td>" + (e.planFinish ? esc(fmtMd(*e.planFinish)) : std::string("-")) + "</td><td>" + esc(fmtMd(e.nowFinish)) +
           "</td><td class=\"n rk-late\">" + esc(baselineDiffMark(e) + " " + baselineDiffText(e)) + "</td></tr>\n";
    }
    o += "</tbody></table>\n";
  }
  o += "</div>\n";
  return o;
}

}  // namespace

std::string renderStats(const Ctx& cx) {
  const ExportInput& in = cx.in;
  const Tree& T = in.tree;
  const StatsCx s{cx, cx.k};
  const ThemeColors& k = cx.k;
  std::string out = "<h2>統計</h2>\n<p class=\"note\">絞り込みに関係なく、プロジェクト全体の集計です。数える対象は作業（子を持たない課題）で、マイルストーンは別に数えます。基準日は " +
                    esc(fmtLong(in.base)) + " です。</p>\n";
  out += renderMtaBand(cx);  // マイルストーンの予測の遅れの警告は、統計の一番上（Issue #75）
  const ProjectStats st = computeStats(T, in.issues, in.rows, in.base);
  if (st.tasks == 0) {
    out += "<p class=\"empty\">集計できる作業がありません。課題を追加すると、ここに進みと内訳が出ます。</p>\n";
    out += baselineSection(s);  // マイルストーンだけ・全部削除したときも、計画との差（消えた課題など）は出す
    return out;
  }
  out += overall(s, st);
  out += floatDist(s, st);
  out += baselineSection(s);
  out += weekly(s, st);

  // 担当別
  {
    std::vector<GroupLine> lines;
    const std::size_t n = std::min(st.byAssignee.size(), kReportMaxStatRows);
    for (std::size_t i = 0; i < n; ++i) {
      const GroupStat& g = st.byAssignee[i];
      GroupLine l;
      l.title = g.name.empty() ? std::string("（担当なし）") : g.name;
      l.nDone = g.nDone;
      l.nProg = g.nProg;
      l.nTodo = g.nTodo;
      l.detail = groupText(g);
      l.late = g.nLate > 0;
      lines.push_back(std::move(l));
    }
    out += "<div class=\"card\"><h3>担当別 <span class=\"note\">未完了の期間が多い順</span></h3>\n" +
           groupRows(s, lines, 140, std::clamp(kW * 0.3, 100.0, 300.0), "担当別") + omittedNote(n, st.byAssignee.size()) + "</div>\n";
  }

  // 担当者別の負荷・完了
  const LoadReport lr = computeLoad(T, in.issues, in.rows, in.calendar, in.base, in.resource);
  out += heatmap(s, lr, T, in.rows, in);
  out += doneByAssignee(s, lr);

  // ルート課題別
  {
    std::vector<GroupLine> lines;
    const std::size_t n = std::min(st.byRoot.size(), kReportMaxStatRows);
    for (std::size_t i = 0; i < n; ++i) {
      const GroupStat& g = st.byRoot[i];
      GroupLine l;
      l.title = g.name;
      l.nDone = g.nDone;
      l.nProg = g.nProg;
      l.nTodo = g.nTodo;
      l.detail = pct(g.progress()) + "・";
      l.detail += g.hasFinish ? "見込み " + fmtShort(g.finish) + "・余裕 " + std::to_string(g.minFloat) + "日" : std::string("すべて完了");
      if (g.nLate > 0) l.detail += "・! 遅れ " + std::to_string(g.nLate);
      l.late = g.nLate > 0;
      lines.push_back(std::move(l));
    }
    out += "<div class=\"card\"><h3>ルート課題別 <span class=\"note\">Epic など最上位の課題ごとの進み</span></h3>\n" +
           groupRows(s, lines, std::clamp(kW * 0.32, 140.0, 320.0), std::clamp(kW * 0.25, 90.0, 260.0), "ルート課題別") +
           omittedNote(n, st.byRoot.size()) + "</div>\n";
  }

  // 優先度別
  {
    static const char* const names[5] = {"Highest", "High", "Medium", "Low", "Lowest"};
    std::vector<GroupLine> lines;
    for (int i = 0; i < 5; ++i) {
      GroupLine l;
      l.nDone = st.prioDone[i];
      l.nTodo = st.prioOpen[i];
      if (l.nDone + l.nTodo <= 0) continue;
      l.title = names[i];
      l.detail = "完了 " + std::to_string(st.prioDone[i]) + "／未完了 " + std::to_string(st.prioOpen[i]);
      lines.push_back(std::move(l));
    }
    out += "<div class=\"card\"><h3>優先度別</h3>\n" + groupRows(s, lines, 100, std::clamp(kW * 0.3, 100.0, 300.0), "優先度別") + "</div>\n";
  }

  // 推移（作成日・完了日・更新日から）
  out += renderTrends(cx);
  (void)k;
  return out;
}

}  // namespace roadmap::report
