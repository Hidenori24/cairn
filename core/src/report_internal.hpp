// 報告の書き出しの内部（report*.cpp だけが使う）
#pragma once

#include <cmath>
#include <string>
#include <string_view>
#include <vector>

#include "roadmap/report.hpp"
#include "roadmap/theme_colors.hpp"

namespace roadmap::report {

// 数値 → 文字列（小数 1 桁まで。ロケールに依存しない。"-0" は "0"）
std::string n1(double v);
std::string n0(double v);
// 不透明度 0〜1 → "0.35"（小数 2 桁まで）
std::string alphaStr(double a);
// 0xRRGGBB → "#rrggbb"
std::string hex(unsigned rgb);
inline std::string esc(std::string_view s) { return escapeXml(s); }

// 文字の幅の見積り（px）: ASCII は 0.58em、それ以外（全角）は 1em。表示に使うフォントは分からないので、ざっくり
double textW(std::string_view s, double px);
// 幅 maxW に収まるよう、収まらなければ末尾を「…」にする（コードポイント単位）
std::string clipText(std::string_view s, double maxW, double px);

// 背景 bg に不透明度 a で fg を重ねた色（不透明度の指定を持たない道具でも同じ色に見える）
inline unsigned mix(unsigned fg, double a, unsigned bg) {
  const auto ch = [&](int sh) {
    const double f = static_cast<double>((fg >> sh) & 0xFF), b = static_cast<double>((bg >> sh) & 0xFF);
    return static_cast<unsigned>(std::lround(f * a + b * (1.0 - a)));
  };
  return (ch(16) << 16) | (ch(8) << 8) | ch(0);
}

// 描画の道具: 色は 0xRRGGBB の hex 文字列で渡す
struct TextStyle {
  double size = 0;          // 0 = 親のまま
  bool bold = false;
  const char* anchor = "";  // "" / "middle" / "end"
  std::string fill;         // 空 = 親のまま
  double opacity = 1;
};
void addRect(std::string& o, double x, double y, double w, double h, const std::string& fill, double rx = 0, double opacity = 1,
             std::string_view extra = {});
void addText(std::string& o, double x, double y, std::string_view s, const TextStyle& st = {});
// <text> の開始と終了（中に <tspan> を入れるとき）
void openText(std::string& o, double x, double y, const TextStyle& st = {});

// 出力全体で共有するもの
struct Ctx {
  const ExportInput& in;
  const ExportOptions& opt;
  const ThemeColors& k;
  bool dark = false;
  std::vector<VisibleRow> rows;  // 書き出す行（範囲に従う）
  const BaselineCompare* bl = nullptr;  // 使えるベースラインとの比較（課題の数が合うときだけ）
  std::string fg(unsigned rgb) const { return hex(rgb); }
  std::string ink() const { return hex(k.ink); }
  std::string muted() const { return hex(k.muted); }
  std::string line() const { return hex(k.line); }
  std::string accent() const { return hex(k.accent); }
  std::string late() const { return hex(k.late); }
  std::string tight() const { return hex(k.tight); }
  std::string ok() const { return hex(k.ok); }
};

// 各節（HTML の断片）
std::string renderGantt(const Ctx& cx);
std::string renderStats(const Ctx& cx);
// 統計の「推移」（バーンダウン・リードタイム・滞留日数。Issue #54 の A）。renderStats の最後に足す
std::string renderTrends(const Ctx& cx);
// マイルストーンの予測の推移（Issue #75）。遅れの警告（統計の上。なければ空）と、推移の図・表（renderTrends の最後。weeklyAuto がなければ空）
std::string renderMtaBand(const Ctx& cx);
std::string renderMtaCard(const Ctx& cx);
std::string renderList(const Ctx& cx);

// ページ全体の CSS（週次レポートの HTML も同じ見た目にするため共有する）と、出力日時の "2026/10/19 18:36"
std::string pageCss(const ThemeColors& k);
std::string dateTimeText(const IClock& clk);

// 状況の分類に応じた文字色
unsigned catInk(const ThemeColors& k, Cat c);
// 余裕の見出し（"完了" "−2週" "5日"）。遅れは先頭に "!" を付ける（色だけに頼らない）
std::string floatLabel(const ScheduleRow& r);
unsigned riskInk(const ThemeColors& k, Risk r);

}  // namespace roadmap::report
