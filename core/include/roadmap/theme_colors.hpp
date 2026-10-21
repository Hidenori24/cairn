// 画面（ImGui）と書き出し（HTML・SVG）が共有する色（SPEC 10.12）。値はここ1か所にあり、画面は ui/theme.cpp で ImU32 に直す。
#pragma once

namespace roadmap {

// 0xRRGGBB に不透明度を添えた色
struct AlphaRgb {
  unsigned rgb = 0;
  float a = 1.f;
};

struct ThemeColors {
  unsigned bg = 0, surface = 0, surface2 = 0, surface3 = 0, ink = 0, muted = 0, line = 0, line2 = 0;
  unsigned accent = 0, accentInk = 0, accentBg = 0;
  unsigned late = 0, lateBg = 0, tight = 0, tightBg = 0, tightEdge = 0, ok = 0, okBg = 0;
  unsigned c[8] = {};  // ルート課題の色
  AlphaRgb hover, selbg, rowline, wkend, holbg;
  unsigned arrow = 0;
  bool dark = false;
};

const ThemeColors& lightColors();
const ThemeColors& darkColors();

}  // namespace roadmap
