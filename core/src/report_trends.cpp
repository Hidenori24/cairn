// 報告: 統計の「推移」の節（画面の統計ビューと同じ内容を、core/trends の集計から SVG にする。Issue #54 の A）
// 絞り込みには従わず、プロジェクト全体の集計。スクリプトは入れない。
#include <algorithm>
#include <cmath>

#include "report_internal.hpp"
#include "roadmap/stats.hpp"
#include "roadmap/trends.hpp"

namespace roadmap::report {

namespace {

constexpr double kW = 960;  // 図の幅（viewBox）

std::string svgOpen(double w, double h, const std::string& label) {
  return "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" + n1(w) + "\" height=\"" + n1(h) + "\" viewBox=\"0 0 " + n1(w) + " " + n1(h) +
         "\" role=\"img\" aria-label=\"" + esc(label) + "\">";
}

void legendSw(std::string& o, const std::string& color, const std::string& label) {
  o += "<span><i class=\"sw\" style=\"background:" + color + "\"></i>" + esc(label) + "</span>";
}

// 線の見本つきの凡例（実線・点線）
void legendLine(std::string& o, const std::string& color, bool dashed, const std::string& label) {
  o += "<span><svg width=\"26\" height=\"10\" viewBox=\"0 0 26 10\" aria-hidden=\"true\"><path d=\"M0 5H26\" stroke=\"" + color +
       "\" stroke-width=\"2\" fill=\"none\"" + (dashed ? " stroke-dasharray=\"5 4\"" : "") + "/></svg>" + esc(label) + "</span>";
}

std::string line(double x0, double y0, double x1, double y1, const std::string& color, double w, bool dashed = false) {
  return "<path d=\"M" + n1(x0) + " " + n1(y0) + "L" + n1(x1) + " " + n1(y1) + "\" stroke=\"" + color + "\" stroke-width=\"" + n1(w) + "\" fill=\"none\"" +
         (dashed ? " stroke-dasharray=\"5 4\"" : "") + "/>";
}

std::string circle(double cx, double cy, double r, const std::string& fill, const std::string& stroke = {}) {
  std::string s = "<circle cx=\"" + n1(cx) + "\" cy=\"" + n1(cy) + "\" r=\"" + n1(r) + "\" fill=\"" + fill + "\"";
  if (!stroke.empty()) s += " stroke=\"" + stroke + "\" stroke-width=\"2\"";
  return s + "/>";
}

std::string numStr(double v) {
  std::string s = n1(v);
  if (s.size() > 2 && s.compare(s.size() - 2, 2, ".0") == 0) s.resize(s.size() - 2);
  return s;
}

std::string pct(double v) { return std::to_string(static_cast<int>(v * 100 + 0.5)) + "%"; }

// ---- バーンダウン／バーンアップ ----
std::string burnCard(const Ctx& cx, const TrendReport& tr) {
  const ThemeColors& k = cx.k;
  const TrendBurn& b = tr.burn;
  std::string o = "<div class=\"card\"><h3>バーンダウン／バーンアップ <span class=\"note\">週ごとの作った数・完了した数と、残り</span></h3>\n";
  if (b.weeks.empty()) return o + "<p>作成日（Created）のある作業がないので、バーンダウンは出せません。</p></div>\n";

  const int nPast = static_cast<int>(b.weeks.size());
  const int nFut = b.forecast.ok ? std::min(b.forecast.weeksAhead, kFutureWeeks) : 0;
  const int cols = nPast + nFut;
  const double bw = std::clamp(kW / cols, 12.0, 44.0);
  const double barsH = 90, lineH = 110, axisH = 20;
  const int every = std::max(1, static_cast<int>(std::ceil(34.0 / bw)));
  const bool labels = bw >= 28;
  const Day lastStart = b.weeks.back().start;
  const auto weekStartOf = [&](int i) { return lastStart + (i - (nPast - 1)) * 7; };
  const auto colX = [&](int i) { return bw * i; };
  const std::string ink = hex(k.ink), muted = hex(k.muted), accent = hex(k.accent), ok = hex(k.ok);
  const std::string bg = hex(k.dark ? k.bg : k.surface);
  const std::string band = hex(mix(k.selbg.rgb, k.selbg.a, k.surface));

  o += "<div class=\"legend\">";
  legendSw(o, accent, "作った数");
  legendSw(o, ok, "完了した数");
  legendLine(o, ink, false, "残り（実線）");
  legendLine(o, accent, true, "ペース予測（点線）");
  o += "</div>\n";

  const auto xLabels = [&](std::string& g, double chartH) {
    for (int i = 0; i < cols; ++i) {
      const bool now = i == b.nowIndex;
      if (i % every != 0 && !now) continue;
      addText(g, colX(i) + bw / 2, chartH + 15, fmtShort(weekStartOf(i)),
              TextStyle{0, now, "middle", now ? ink : muted, i < nPast ? 1.0 : 0.7});
    }
  };
  // 週ごとの当たり（マウスを載せると値）。予測の週は予測の値
  const auto hover = [&](std::string& g, double chartH) {
    for (int i = 0; i < cols; ++i) {
      std::string t;
      if (i < nPast) {
        const TrendWeek& w = b.weeks[static_cast<std::size_t>(i)];
        t = fmtMd(w.start) + " からの週" + (w.partial ? "（基準日まで）" : "") + "\n作った " + std::to_string(w.created) + " 件\n完了 " +
            std::to_string(w.resolved) + " 件\n残り " + std::to_string(w.remaining) + " 件";
      } else {
        const double left = std::max(0.0, static_cast<double>(b.remainingNow) - b.forecast.perWeek * (i - (nPast - 1)));
        t = fmtMd(weekStartOf(i)) + " からの週（予測）\n残り およそ " + std::to_string(static_cast<int>(left + 0.5)) + " 件\n（直近 " +
            std::to_string(kTrendPaceWeeks) + " 週の平均 週 " + numStr(b.forecast.perWeek) + " 件のペース）";
      }
      g += "<g><title>" + esc(t) + "</title><rect x=\"" + n1(colX(i)) + "\" y=\"0\" width=\"" + n1(bw) + "\" height=\"" + n1(chartH + axisH) +
           "\" fill=\"transparent\"/></g>";
    }
  };

  // 棒: 作った数（左）・完了した数（右）
  {
    int mx = 1;
    for (const TrendWeek& w : b.weeks) mx = std::max({mx, w.created, w.resolved});
    const double unit = (barsH - 18) / mx;
    std::string g;
    addRect(g, colX(b.nowIndex), 0, bw, barsH, band);
    g += line(0, barsH, bw * cols, barsH, hex(k.line), 1);
    for (int i = 0; i < nPast; ++i) {
      const TrendWeek& w = b.weeks[static_cast<std::size_t>(i)];
      const auto bar = [&](double x0, double x1, int n, const std::string& fill) {
        if (n <= 0) return;
        const double h = std::max(unit * n, 2.0);
        addRect(g, x0, barsH - h, x1 - x0, h, fill, 2);
        if (labels) addText(g, (x0 + x1) / 2, barsH - h - 3, std::to_string(n), TextStyle{0, false, "middle", muted, 1});
      };
      bar(colX(i) + bw * 0.12, colX(i) + bw * 0.48, w.created, accent);
      bar(colX(i) + bw * 0.52, colX(i) + bw * 0.88, w.resolved, ok);
    }
    xLabels(g, barsH);
    hover(g, barsH);
    o += "<p class=\"note\">作った数と完了した数（週ごと。棒の上の数字が件数）</p>\n<div class=\"gseg\">" +
         svgOpen(bw * cols, barsH + axisH, "週ごとの作った数と完了した数") + g + "</svg></div>\n";
  }

  // 折れ線: 残り。ペース予測は点線
  {
    int mx = 1;
    for (const TrendWeek& w : b.weeks) mx = std::max(mx, w.remaining);
    const double top = 18;
    const auto yOf = [&](double v) { return lineH - (lineH - top) * v / mx; };
    const auto xOf = [&](int i) { return colX(i) + bw / 2; };
    std::string g;
    addRect(g, colX(b.nowIndex), 0, bw, lineH, band);
    g += line(0, lineH, bw * cols, lineH, hex(k.line), 1);
    for (int i = 1; i < nPast; ++i)
      g += line(xOf(i - 1), yOf(b.weeks[static_cast<std::size_t>(i) - 1].remaining), xOf(i), yOf(b.weeks[static_cast<std::size_t>(i)].remaining), ink, 2);
    for (int i = 0; i < nPast; ++i) {
      const int v = b.weeks[static_cast<std::size_t>(i)].remaining;
      g += i == nPast - 1 ? circle(xOf(i), yOf(v), 4, bg, ink) : circle(xOf(i), yOf(v), 3, ink);
      if (labels || i == 0 || i == nPast - 1) addText(g, xOf(i), yOf(v) - 8, std::to_string(v), TextStyle{0, false, "middle", muted, 1});
    }
    if (b.forecast.ok && nFut > 0) {
      const bool reaches = b.forecast.weeksAhead <= nFut;
      const int endI = reaches ? nPast - 1 + b.forecast.weeksAhead : nPast - 1 + nFut;
      const double endV = reaches ? 0.0 : std::max(0.0, static_cast<double>(b.remainingNow) - b.forecast.perWeek * nFut);
      const double ex = xOf(endI), ey = yOf(endV);
      g += line(xOf(nPast - 1), yOf(b.remainingNow), ex, ey, accent, 2, true);
      g += "<path d=\"M" + n1(ex) + " " + n1(ey - 5) + "L" + n1(ex + 5) + " " + n1(ey) + "L" + n1(ex) + " " + n1(ey + 5) + "L" + n1(ex - 5) + " " + n1(ey) +
           "Z\" fill=\"" + accent + "\"/>";
      const std::string lb = reaches ? "完了の見込み " + fmtShort(b.forecast.finishWeek) : std::string("さらに先");
      const double tw = textW(lb, 12);
      const double tx = std::min(ex - tw / 2, bw * cols - tw);
      addRect(g, tx - 3, ey - 10 - 13, tw + 6, 16, bg, 3);
      addText(g, tx, ey - 10 - 1, lb, TextStyle{0, false, "", accent, 1});
    }
    xLabels(g, lineH);
    hover(g, lineH);
    o += "<p class=\"note\">残り（その週の終わり。基準日の週は基準日。点線は今のペースで続けた場合）</p>\n<div class=\"gseg\">" +
         svgOpen(bw * cols, lineH + axisH, "残りの推移とペース予測") + g + "</svg></div>\n";
  }

  const std::string ft = trendForecastText(b.forecast, b.remainingNow);
  const bool bad = !b.forecast.ok && !ft.empty() && ft[0] == '!';
  o += std::string(bad ? "<p class=\"warn\">" : "<p>") + esc(ft) + "</p>\n</div>\n";
  return o;
}

// 階級別の棒（5 行）
std::string bucketSvg(const Ctx& cx, const std::array<int, kTrendBuckets>& hist, int total, const unsigned (&cols)[kTrendBuckets], bool bangLast,
                      const std::string& label) {
  const ThemeColors& k = cx.k;
  int mx = 1;
  for (int h : hist) mx = std::max(mx, h);
  const double lw = 110, cw = 90, bw = std::max(kW - lw - cw - 8, 80.0);
  std::string o;
  for (int i = 0; i < kTrendBuckets; ++i) {
    const double y = 24.0 * i, ty = y + 12 + 4;
    const bool bang = bangLast && i == kTrendBuckets - 1;
    const int n = hist[static_cast<std::size_t>(i)];
    const double len = bw * n / mx;
    std::string g;
    addText(g, 6, ty, std::string(bang ? "! " : "") + trendBucketLabel(i) + " 営業日", TextStyle{0, bang, "", hex(bang ? k.late : k.ink), 1});
    if (len > 0.5) addRect(g, lw, y + 4, len, 16, hex(cols[i]), 3);
    std::string cnt = std::to_string(n) + " 件";
    if (total > 0) cnt += "（" + pct(static_cast<double>(n) / total) + "）";
    addText(g, lw + len + 8, ty, cnt, TextStyle{0, false, "", hex(k.muted), 1});
    o += "<g><title>" + esc(std::string(trendBucketLabel(i)) + " 営業日: " + std::to_string(n) + " 件") + "</title>" + g + "</g>";
  }
  return "<div class=\"gseg\">" + svgOpen(kW, 24.0 * kTrendBuckets, label) + o + "</svg></div>\n";
}

std::string leadCard(const Ctx& cx, const TrendReport& tr) {
  const ThemeColors& k = cx.k;
  std::string o = "<div class=\"card\"><h3>リードタイム <span class=\"note\">作成 → 完了の営業日</span></h3>\n";
  if (tr.lead.n == 0) return o + "<p>作成日と完了日の両方がある完了済みの作業がないので、リードタイムは出せません。</p></div>\n";
  o += "<p>完了 " + std::to_string(tr.lead.n) + " 件　中央値 " + numStr(tr.lead.median) + " 営業日　90 パーセンタイル " + std::to_string(tr.lead.p90) +
       " 営業日　最長 " + std::to_string(tr.lead.maxDays) + " 営業日</p>\n";
  const unsigned cols[kTrendBuckets] = {k.accent, k.accent, k.accent, k.accent, k.accent};
  o += bucketSvg(cx, tr.lead.hist, tr.lead.n, cols, false, "リードタイムの階級別の度数");
  return o + "</div>\n";
}

std::string agingCard(const Ctx& cx, const TrendReport& tr) {
  const ThemeColors& k = cx.k;
  std::string o = "<div class=\"card\"><h3>滞留日数 <span class=\"note\">未完了で、最後に動いた日から基準日までの営業日</span></h3>\n";
  if (tr.aging.n == 0) return o + "<p>最後に動いた日のわかる未完了の作業がないので、滞留は出せません。</p></div>\n";
  o += "<p class=\"note\">最後に動いた日は、更新日（Updated）、なければ開始日、なければ作成日（Created）です。基準日より先の日付は 0 日です。</p>\n";
  const unsigned cols[kTrendBuckets] = {k.ok, k.ok, k.tight, k.tight, k.late};
  o += bucketSvg(cx, tr.aging.hist, tr.aging.n, cols, true, "滞留日数の階級別の度数");
  o += "<p class=\"note\">長く動いていない順（上位 " + std::to_string(tr.aging.top.size()) + " 件）</p>\n";
  o += "<table><thead><tr><th>#</th><th>Issue Id</th><th>課題</th><th>担当</th><th class=\"n\">動いていない日数</th><th>最後に動いた日</th></tr></thead><tbody>\n";
  int rank = 0;
  for (const AgingItem& it : tr.aging.top) {
    ++rank;
    const bool old = trendBucket(it.days) == kTrendBuckets - 1;
    const char* from = it.from == AgingFrom::Updated ? "更新日" : it.from == AgingFrom::Start ? "開始日" : "作成日";
    o += "<tr><td class=\"id\">" + std::to_string(rank) + "</td><td class=\"id\">" + esc(it.id) + "</td><td class=\"nm\">" + esc(it.name) + "</td><td>" +
         esc(it.assignee.empty() ? std::string("（担当なし）") : it.assignee) + "</td><td class=\"n" + (old ? " rk-late" : "") + "\">" + (old ? "! " : "") +
         std::to_string(it.days) + " 営業日</td><td>" + esc(fmtLong(it.last)) + "（" + from + "）</td></tr>\n";
  }
  return o + "</tbody></table>\n</div>\n";
}

}  // namespace

std::string renderTrends(const Ctx& cx) {
  const ExportInput& in = cx.in;
  static const ProjectMeta kNoMeta;
  const Result<TrendReport> res = computeTrends(in.tree, in.issues, in.meta ? *in.meta : kNoMeta, in.rows, in.calendar, in.base);
  std::string out = "<h2>推移</h2>\n<p class=\"note\">作成日（Created）・完了日（Resolved）・更新日（Updated）から出しています。状況の履歴は記録していません。"
                    "週は月曜始まり、営業日は休業日を除きます。絞り込みに関係なく、プロジェクト全体の集計です。</p>\n";
  if (!res.ok()) return out + "<p class=\"empty\">推移を集計できません。</p>\n";
  const TrendReport& tr = res.value();
  if (tr.items == 0) return out + "<p class=\"empty\">集計できる作業がありません。</p>\n";
  out += "<p class=\"note\">対象の作業 " + std::to_string(tr.items) + " 件（マイルストーンを除く）</p>\n";
  out += burnCard(cx, tr);
  out += leadCard(cx, tr);
  out += agingCard(cx, tr);
  const std::vector<std::string> notes = trendNotes(tr);
  if (!notes.empty()) {
    out += "<div class=\"card\">";
    for (const std::string& n : notes) out += "<p class=\"note\">・" + esc(n) + "</p>";
    out += "</div>\n";
  }
  return out;
}

}  // namespace roadmap::report
