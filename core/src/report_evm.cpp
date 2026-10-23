// 報告: 統計の「EVM（出来高管理）」の節（画面の統計ビューと同じ内容を、core/evm の集計から SVG にする。Issue #65）
// 絞り込みには従わず、プロジェクト全体の集計。スクリプトは入れない。
#include <algorithm>
#include <cmath>

#include "report_internal.hpp"
#include "roadmap/evm.hpp"

namespace roadmap::report {

namespace {

constexpr double kW = 960;  // 図の幅（viewBox）

std::string svgOpen(double w, double h, const std::string& label) {
  return "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" + n1(w) + "\" height=\"" + n1(h) + "\" viewBox=\"0 0 " + n1(w) + " " + n1(h) +
         "\" role=\"img\" aria-label=\"" + esc(label) + "\">";
}

std::string pathLine(double x0, double y0, double x1, double y1, const std::string& color, double w, bool dashed = false) {
  return "<path d=\"M" + n1(x0) + " " + n1(y0) + "L" + n1(x1) + " " + n1(y1) + "\" stroke=\"" + color + "\" stroke-width=\"" + n1(w) + "\" fill=\"none\"" +
         (dashed ? " stroke-dasharray=\"5 4\"" : "") + "/>";
}

void legendLine(std::string& o, const std::string& color, bool dashed, const std::string& label) {
  o += "<span><svg width=\"26\" height=\"10\" viewBox=\"0 0 26 10\" aria-hidden=\"true\"><path d=\"M0 5H26\" stroke=\"" + color +
       "\" stroke-width=\"2\" fill=\"none\"" + (dashed ? " stroke-dasharray=\"5 4\"" : "") + "/></svg>" + esc(label) + "</span>";
}

std::string tile(const std::string& label, const std::string& value, bool bad) {
  return "<div class=\"tile\"><div class=\"l\">" + esc(label) + "</div><div class=\"v" + (bad ? " rk-late" : "") + "\">" + esc(value) + "</div></div>";
}

// S カーブ（PV・EV の累積と、今日の線、完了予測の点線）
std::string curveSvg(const Ctx& cx, const EvmReport& r) {
  const ThemeColors& k = cx.k;
  const double padL = 52, padR = 16, padT = 16, padB = 26, H = 230;
  const double pw = kW - padL - padR, ph = H - padT - padB;
  const Day d0 = r.curve.front().date;
  Day d1 = r.curve.back().date;
  if (d1 <= d0) d1 = d0 + 1;
  const double top = std::max(r.bac, 1e-9);
  const auto X = [&](Day d) { return padL + pw * static_cast<double>(d - d0) / static_cast<double>(d1 - d0); };
  const auto Y = [&](double v) { return padT + ph - ph * std::clamp(v / top, 0.0, 1.0); };
  const std::string ink = hex(k.ink), muted = hex(k.muted), accent = hex(k.accent), line = hex(k.line);
  const std::string bg = hex(k.dark ? k.bg : k.surface);
  std::string g;

  // 目盛り: 0 と BAC、x は週の見出しを間引く
  g += pathLine(padL, padT + ph, padL + pw, padT + ph, line, 1);
  g += pathLine(padL, Y(r.bac), padL + pw, Y(r.bac), line, 1, true);
  addText(g, padL - 6, padT + ph + 4, "0", TextStyle{0, false, "end", muted, 1});
  addText(g, padL - 6, Y(r.bac) + 4, n1(r.bac), TextStyle{0, false, "end", muted, 1});
  const std::size_t every = std::max<std::size_t>(1, (r.curve.size() + 9) / 10);
  for (std::size_t i = 0; i < r.curve.size(); i += every) {
    if (static_cast<int>(i) == r.nowIndex) continue;
    addText(g, X(r.curve[i].date), H - 8, fmtShort(r.curve[i].date), TextStyle{0, false, "middle", muted, 1});
  }
  // 今日の線
  const Day today = r.curve[static_cast<std::size_t>(std::max(r.nowIndex, 0))].date;
  g += pathLine(X(today), padT, X(today), padT + ph, ink, 1, true);
  addText(g, X(today), H - 8, "基準日 " + fmtShort(today), TextStyle{0, true, "middle", ink, 1});

  // PV（実線・細い・四角）
  for (std::size_t i = 1; i < r.curve.size(); ++i)
    g += pathLine(X(r.curve[i - 1].date), Y(r.curve[i - 1].pv), X(r.curve[i].date), Y(r.curve[i].pv), muted, 2);
  // EV（実線・太い・丸）
  const EvmPoint* prevEv = nullptr;
  for (const EvmPoint& p : r.curve) {
    if (!p.hasEv) break;
    if (prevEv) g += pathLine(X(prevEv->date), Y(prevEv->ev), X(p.date), Y(p.ev), accent, 3);
    prevEv = &p;
  }
  // 完了予測（点線）: 基準日の EV から、BAC に達する見込みの日まで
  if (r.forecast.ok && prevEv) {
    const double ex = X(r.forecast.finish), ey = Y(r.bac);
    g += pathLine(X(prevEv->date), Y(prevEv->ev), ex, ey, accent, 2, true);
    g += "<path d=\"M" + n1(ex) + " " + n1(ey - 5) + "L" + n1(ex + 5) + " " + n1(ey) + "L" + n1(ex) + " " + n1(ey + 5) + "L" + n1(ex - 5) + " " + n1(ey) +
         "Z\" fill=\"" + accent + "\"/>";
    const std::string lb = "完了の見込み " + fmtShort(r.forecast.finishWeek);
    const double tw = textW(lb, 12);
    const double tx = std::min(ex - tw / 2, padL + pw - tw);
    addRect(g, tx - 3, ey + 8, tw + 6, 16, bg, 3);
    addText(g, tx, ey + 20, lb, TextStyle{0, false, "", accent, 1});
  }
  // 点: マウスを載せると値（PV は四角、EV は丸）
  for (const EvmPoint& p : r.curve) {
    std::string tt = fmtMd(p.date) + " の終わり\nPV " + evmAmountText(p.pv);
    if (p.hasEv) tt += "\nEV " + evmAmountText(p.ev);
    g += "<g><title>" + esc(tt) + "</title>";
    const double px = X(p.date), py = Y(p.pv);
    addRect(g, px - 3, py - 3, 6, 6, muted);
    if (p.hasEv) g += "<circle cx=\"" + n1(px) + "\" cy=\"" + n1(Y(p.ev)) + "\" r=\"3.5\" fill=\"" + accent + "\"/>";
    g += "</g>";
  }
  return "<div class=\"gseg\">" + svgOpen(kW, H, "PV と EV の累積（S カーブ）") + g + "</svg></div>\n";
}

}  // namespace

std::string renderEvm(const Ctx& cx) {
  const ExportInput& in = cx.in;
  if (!in.evmBaseline) return {};
  const Result<EvmReport> res = computeEvm(in.tree, in.issues, in.rows, in.calendar, in.base, in.evmBaseline, in.evm);
  std::string o = "<div class=\"card\"><h3>EVM（出来高管理） <span class=\"note\">ベースライン「" + esc(in.evmBaseline->name) +
                  "」の日程と比べた、スケジュール系の指標</span></h3>\n";
  if (!res.ok()) return o + "<p class=\"warn\">" + esc("! " + res.error()) + "</p></div>\n";
  const EvmReport& r = res.value();
  if (!r.ok()) {
    o += "<p class=\"warn\">" + esc(evmReasonText(r)) + "</p>\n";
    for (const std::string& s : evmNotes(r)) o += "<p class=\"note\">・" + esc(s) + "</p>\n";
    return o + "</div>\n";
  }
  const std::string mark = r.warn ? "! " : "";
  o += "<div class=\"tiles\">\n";
  o += tile("BAC（総計画量）", evmAmountText(r.bac), false) + "\n";
  o += tile("PV（計画価値）", evmAmountText(r.pv), false) + "\n";
  o += tile("EV（出来高）", evmAmountText(r.ev), false) + "\n";
  o += tile("SV（EV − PV）", (r.warn && r.sv < 0 ? mark : std::string()) + evmSignedAmountText(r.sv), r.warn) + "\n";
  o += tile("SPI（EV ÷ PV）", mark + evmSpiText(r), r.warn) + "\n";
  o += tile("ES の遅れ（営業日。+ は遅れ）", (r.warn && r.esDelta > 0 ? mark : std::string()) + evmEsDeltaText(r), r.warn) + "\n";
  o += "</div>\n";
  o += "<p>" + esc(evmEsSentence(r)) + "　" + esc(evmAmountSourceText(r)) + "</p>\n";
  if (!r.curve.empty()) {
    o += "<div class=\"legend\">";
    legendLine(o, hex(cx.k.muted), false, "PV（計画）");
    legendLine(o, hex(cx.k.accent), false, "EV（出来高）");
    legendLine(o, hex(cx.k.accent), true, "完了の予測（点線）");
    legendLine(o, hex(cx.k.ink), true, "基準日");
    o += "</div>\n" + curveSvg(cx, r);
  }
  const std::string ft = evmForecastText(r);
  const bool bad = !r.forecast.ok && !ft.empty() && ft[0] == '!';
  o += std::string(bad ? "<p class=\"warn\">" : "<p>") + esc(ft) + "</p>\n";
  for (const std::string& s : evmNotes(r)) o += "<p class=\"note\">・" + esc(s) + "</p>\n";
  o += "<p class=\"note\"><b>計算の前提</b></p>\n";
  for (const std::string& s : evmAssumptions(r)) o += "<p class=\"note\">・" + esc(s) + "</p>\n";
  return o + "</div>\n";
}

}  // namespace roadmap::report
