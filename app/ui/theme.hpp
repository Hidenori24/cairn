// 色トークン・密度（SPEC 10.12）
#pragma once

#include <imgui.h>

namespace roadmap::ui {

struct Tokens {
  ImU32 bg, surface, surface2, surface3, ink, muted, line, line2;
  ImU32 accent, accentInk, accentBg;
  ImU32 late, lateBg, tight, tightBg, tightEdge, ok, okBg;
  ImU32 c[8];  // ルート課題の色
  ImU32 hover, selbg, rowline, wkend, holbg, arrow;
  bool dark = false;
};

const Tokens& lightTokens();
const Tokens& darkTokens();

struct Metrics {
  float scale = 1.0f;   // 高DPI の倍率
  bool compact = false;
  float topBar() const { return (compact ? 44.f : 48.f) * scale; }
  float toolBar() const { return (compact ? 36.f : 40.f) * scale; }
  float rowH() const { return (compact ? 26.f : 30.f) * scale; }
  float s(float v) const { return v * scale; }
};

// ImGui のスタイルにトークンを反映する
void applyStyle(const Tokens& t, const Metrics& m);
ImVec4 v4(ImU32 c);
ImU32 withAlpha(ImU32 c, float a);

}  // namespace roadmap::ui
