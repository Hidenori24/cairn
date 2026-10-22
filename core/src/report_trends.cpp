// 報告: 統計の「推移」の節（画面の統計ビューと同じ内容を、core/trends の集計から SVG にする。Issue #54 の A）
// 絞り込みには従わず、プロジェクト全体の集計。スクリプトは入れない。
#include <algorithm>
#include <cmath>
#include <functional>

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

// ---- 状況の件数の推移（累積フロー図。Issue #64）----
// 完了（下・実線）→ 進行中（中・破線）→ 未着手（上・点線）の積み上げ。記録のない週は線を引かず、斜めの線の帯と「記録なし」で示す
std::string cfdCard(const Ctx& cx, const History& hist, Day base) {
  const ThemeColors& k = cx.k;
  std::string o = "<div class=\"card\"><h3>状況の件数の推移（累積フロー） <span class=\"note\">週ごとの未着手・進行中・完了の件数</span></h3>\n";
  o += "<p class=\"note\">アプリを開いたとき・週次レポートを出したとき・統計の「いま記録する」で、週に 1 回までの件数を覚えています（最大 " +
       std::to_string(kHistoryMaxWeeks) + " 週）。件数だけで、課題の名前は覚えません。絞り込みに関係なく、プロジェクト全体です。</p>\n";
  const CfdSeries cs = cfdSeries(hist, base);
  if (!cs.drawable()) return o + "<p>" + esc(cfdPendingText(cs.recorded)) + "</p></div>\n";

  const int cols = static_cast<int>(cs.weeks.size());
  const double bw = std::clamp(kW / cols, 6.0, 64.0);
  const double chartH = 140, axisH = 20, top = 22;
  const int every = std::max(1, static_cast<int>(std::ceil(34.0 / bw)));
  const std::string ink = hex(k.ink), muted = hex(k.muted), lineC = hex(k.line);

  o += "<div class=\"legend\">";
  const auto legendLineSw = [&](const std::string& fill, bool dashed, const char* dash, const std::string& label) {
    o += "<span><svg width=\"26\" height=\"12\" viewBox=\"0 0 26 12\" aria-hidden=\"true\"><rect x=\"0\" y=\"0\" width=\"26\" height=\"12\" fill=\"" + fill + "\"/><path d=\"M0 1H26\" stroke=\"" + ink + "\" stroke-width=\"2\" fill=\"none\"" +
         (dashed ? std::string(" stroke-dasharray=\"") + dash + "\"" : std::string()) + "/></svg>" + esc(label) + "</span>";
  };
  legendLineSw(hex(mix(k.ok, 0.55, k.dark ? k.bg : k.surface)), false, "", "完了（実線）");
  legendLineSw(hex(mix(k.accent, 0.55, k.dark ? k.bg : k.surface)), true, "5 4", "進行中（破線）");
  legendLineSw(hex(mix(k.line2, 0.55, k.dark ? k.bg : k.surface)), true, "2 3", "未着手（点線）");
  o += "<span><svg width=\"26\" height=\"12\" viewBox=\"0 0 26 12\" aria-hidden=\"true\"><rect x=\"0.5\" y=\"0.5\" width=\"25\" height=\"11\" fill=\"none\" stroke=\"" +
       muted + "\" stroke-dasharray=\"2 2\"/></svg>記録なし（線を引かない）</span>";
  o += "</div>\n";

  const double unit = (chartH - top) / std::max(1, cs.maxTotal);
  const auto xOf = [&](int i) { return bw * i + bw / 2; };
  const auto yAt = [&](int v) { return chartH - unit * v; };
  std::string g;
  // 記録のない週: 点線の枠の薄い帯（連続する週はひとつの帯にして「記録なし」と書く）
  for (int i = 0; i < cols;) {
    if (!cs.weeks[static_cast<std::size_t>(i)].gap) {
      ++i;
      continue;
    }
    int j = i;
    while (j < cols && cs.weeks[static_cast<std::size_t>(j)].gap) ++j;
    const double x0 = bw * i, w = bw * (j - i);
    g += "<rect x=\"" + n1(x0) + "\" y=\"0\" width=\"" + n1(w) + "\" height=\"" + n1(chartH) + "\" fill=\"" + muted +
         "\" fill-opacity=\"0.08\" stroke=\"" + muted + "\" stroke-opacity=\"0.5\" stroke-dasharray=\"2 3\"/>";
    if (w >= textW("記録なし", 11) + 6) addText(g, x0 + w / 2, chartH / 2 + 4, "記録なし", TextStyle{11, false, "middle", muted, 0.8});
    i = j;
  }
  g += line(0, chartH, bw * cols, chartH, lineC, 1);
  // 隣り合う記録のある週のあいだを面で塗る。ひとつだけ孤立した週は細い柱にする（前後を補って線を作らない）
  const auto at = [&](int i) -> const CfdWeek& { return cs.weeks[static_cast<std::size_t>(i)]; };
  // 面は、背景に混ぜた不透明な色で塗る（半透明のまま隣どうしを並べると、継ぎ目が線に見える）
  const unsigned bgU = k.dark ? k.bg : k.surface;
  const auto soft = [&](unsigned c) { return hex(mix(c, 0.55, bgU)); };
  const auto quad = [&](double x0, double ya0, double yb0, double x1, double ya1, double yb1, unsigned fill) {
    // 下辺 (x0,ya0)-(x1,ya1)、上辺 (x0,yb0)-(x1,yb1)
    g += "<path d=\"M" + n1(x0) + " " + n1(ya0) + "L" + n1(x1) + " " + n1(ya1) + "L" + n1(x1) + " " + n1(yb1) + "L" + n1(x0) + " " + n1(yb0) + "Z\" fill=\"" + soft(fill) +
         "\" shape-rendering=\"crispEdges\"/>";
  };
  for (int i = 0; i + 1 < cols; ++i) {
    if (at(i).gap || at(i + 1).gap) continue;
    const CfdWeek &a = at(i), &b = at(i + 1);
    const double x0 = xOf(i), x1 = xOf(i + 1);
    quad(x0, chartH, yAt(a.done), x1, chartH, yAt(b.done), k.ok);
    quad(x0, yAt(a.done), yAt(a.done + a.doing), x1, yAt(b.done), yAt(b.done + b.doing), k.accent);
    quad(x0, yAt(a.done + a.doing), yAt(a.total), x1, yAt(b.done + b.doing), yAt(b.total), k.line2);
  }
  // 境界の線。記録のある連続した週ごとにひとつの折れ線（<path>）として引く。破線・点線の位相はその中で続き、
  // 記録なしの区間でだけ切れる（週の組ごとに引くと、1 区間が破線より短い 104 週では実線に見える）
  for (int i = 0; i < cols;) {
    if (at(i).gap) {
      ++i;
      continue;
    }
    int j = i;
    while (j < cols && !at(j).gap) ++j;
    if (j - i >= 2) {
      const auto poly = [&](const std::function<int(const CfdWeek&)>& val, const char* dash) {
        std::string d;
        for (int m = i; m < j; ++m) d += std::string(m == i ? "M" : "L") + n1(xOf(m)) + " " + n1(yAt(val(at(m))));
        g += "<path d=\"" + d + "\" stroke=\"" + ink + "\" stroke-width=\"2\" fill=\"none\"" +
             (*dash ? std::string(" stroke-dasharray=\"") + dash + "\"" : std::string()) + "/>";
      };
      poly([](const CfdWeek& w) { return w.done; }, "");
      poly([](const CfdWeek& w) { return w.done + w.doing; }, "5 4");
      poly([](const CfdWeek& w) { return w.total; }, "2 3");
    }
    i = j;
  }
  const double colW = std::min(6.0, bw * 0.5);
  for (int i = 0; i < cols; ++i) {
    const CfdWeek& w = at(i);
    if (w.gap) continue;
    const bool prevOk = i > 0 && !at(i - 1).gap, nextOk = i + 1 < cols && !at(i + 1).gap;
    if (!prevOk && !nextOk) {  // 孤立した週
      const double x = xOf(i) - colW / 2;
      addRect(g, x, yAt(w.done), colW, chartH - yAt(w.done), soft(k.ok));
      addRect(g, x, yAt(w.done + w.doing), colW, yAt(w.done) - yAt(w.done + w.doing), soft(k.accent));
      addRect(g, x, yAt(w.total), colW, yAt(w.done + w.doing) - yAt(w.total), soft(k.line2));
    }
    if (bw >= 10 || !prevOk || !nextOk) g += circle(xOf(i), yAt(w.total), 2.5, ink);  // 列が細いときは、連続した週の中の点を省く（点が連なって点線を隠さない）
  }
  // 最新の週の値（色だけに頼らない: 数字でも読める）
  for (int i = cols - 1; i >= 0; --i) {
    if (at(i).gap) continue;
    const CfdWeek& w = at(i);
    const double x = std::min(xOf(i), bw * cols - 4);
    addText(g, x, yAt(w.total) - 7, "合計 " + std::to_string(w.total), TextStyle{0, false, "end", muted, 1});
    break;
  }
  // 横軸の週の見出し
  for (int i = 0; i < cols; ++i)
    if (i % every == 0 || i == cols - 1) addText(g, xOf(i), chartH + 15, fmtShort(at(i).start), TextStyle{0, false, "middle", muted, 1});
  // 週ごとの当たり（マウスを載せると値。記録のない週は「記録なし」）
  for (int i = 0; i < cols; ++i)
    g += "<g><title>" + esc(cfdWeekText(at(i))) + "</title><rect x=\"" + n1(bw * i) + "\" y=\"0\" width=\"" + n1(bw) + "\" height=\"" + n1(chartH + axisH) +
         "\" fill=\"transparent\"/></g>";
  o += "<div class=\"gseg\">" + svgOpen(bw * cols, chartH + axisH, "状況の件数の推移（累積フロー図）") + g + "</svg></div>\n";
  const int gaps = cols - cs.recorded;
  if (gaps > 0) o += "<p class=\"note\">記録のない週が " + std::to_string(gaps) + " 週あります（点線の枠の帯。線は引かず、補いません）。</p>\n";
  return o + "</div>\n";
}

}  // namespace

std::string renderTrends(const Ctx& cx) {
  const ExportInput& in = cx.in;
  static const ProjectMeta kNoMeta;
  const Result<TrendReport> res = computeTrends(in.tree, in.issues, in.meta ? *in.meta : kNoMeta, in.rows, in.calendar, in.base);
  std::string out = "<h2>推移</h2>\n<p class=\"note\">作成日（Created）・完了日（Resolved）・更新日（Updated）から出しています。"
                    "週は月曜始まり、営業日は休業日を除きます。絞り込みに関係なく、プロジェクト全体の集計です。</p>\n";
  // 状況の件数の履歴（累積フロー）は CSV の日付に関係なく、覚えている記録から出す
  const std::string cfd = in.history ? cfdCard(cx, *in.history, in.base) : std::string();
  if (!res.ok()) return out + "<p class=\"empty\">推移を集計できません。</p>\n" + cfd;
  const TrendReport& tr = res.value();
  if (tr.items == 0) return out + "<p class=\"empty\">集計できる作業がありません。</p>\n" + cfd;
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
  return out + cfd;
}

}  // namespace roadmap::report
