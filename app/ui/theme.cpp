#include "ui/theme.hpp"
#include <algorithm>
#include <cmath>

#include "roadmap/theme_colors.hpp"

namespace roadmap::ui {

namespace {
constexpr ImU32 rgb(unsigned v) { return IM_COL32((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF, 0xFF); }
ImU32 rgba(const AlphaRgb& c) {
  return IM_COL32((c.rgb >> 16) & 0xFF, (c.rgb >> 8) & 0xFF, c.rgb & 0xFF, static_cast<int>(c.a * 255 + 0.5f));
}

// 色の値は core/theme_colors（HTML への書き出しと共有）。ここでは ImU32 に直すだけ
Tokens tokensFrom(const ThemeColors& k) {
  Tokens x{};
  x.bg = rgb(k.bg);
  x.surface = rgb(k.surface);
  x.surface2 = rgb(k.surface2);
  x.surface3 = rgb(k.surface3);
  x.ink = rgb(k.ink);
  x.muted = rgb(k.muted);
  x.line = rgb(k.line);
  x.line2 = rgb(k.line2);
  x.accent = rgb(k.accent);
  x.accentInk = rgb(k.accentInk);
  x.accentBg = rgb(k.accentBg);
  x.late = rgb(k.late);
  x.lateBg = rgb(k.lateBg);
  x.tight = rgb(k.tight);
  x.tightBg = rgb(k.tightBg);
  x.tightEdge = rgb(k.tightEdge);
  x.ok = rgb(k.ok);
  x.okBg = rgb(k.okBg);
  for (int i = 0; i < 8; ++i) x.c[i] = rgb(k.c[i]);
  x.hover = rgba(k.hover);
  x.selbg = rgba(k.selbg);
  x.rowline = rgba(k.rowline);
  x.wkend = rgba(k.wkend);
  x.holbg = rgba(k.holbg);
  x.arrow = rgb(k.arrow);
  x.dark = k.dark;
  return x;
}
}  // namespace

const Tokens& lightTokens() {
  static const Tokens t = tokensFrom(lightColors());
  return t;
}

const Tokens& darkTokens() {
  static const Tokens t = tokensFrom(darkColors());
  return t;
}

ImVec4 v4(ImU32 c) { return ImGui::ColorConvertU32ToFloat4(c); }

ImU32 withAlpha(ImU32 c, float a) {
  ImVec4 v = v4(c);
  v.w *= a;
  return ImGui::ColorConvertFloat4ToU32(v);
}

float motionStep(float current, bool target, float seconds, bool enabled) {
  const float goal = target ? 1.f : 0.f;
  if (!enabled) return goal;
  const float weight = 1.f - std::exp(-std::max(0.f, seconds) / 0.045f);
  return current + (goal - current) * weight;
}

void applyStyle(const Tokens& t, const Metrics& m) {
  ImGuiStyle s;
  ImGui::StyleColorsLight(&s);
  s.WindowRounding = 6;
  s.ChildRounding = 6;
  s.FrameRounding = 6;
  s.PopupRounding = 6;
  s.GrabRounding = 6;
  s.TabRounding = 6;
  s.ScrollbarRounding = 6;
  s.WindowBorderSize = 1;
  s.FrameBorderSize = 1;
  s.PopupBorderSize = 1;
  s.WindowPadding = ImVec2(12, 12);
  s.FramePadding = ImVec2(8, m.compact ? 3.f : 5.f);
  s.ItemSpacing = ImVec2(8, m.compact ? 4.f : 6.f);
  s.ItemInnerSpacing = ImVec2(6, 4);
  s.ScrollbarSize = 12;
  s.CellPadding = ImVec2(6, 3);
  auto* c = s.Colors;
  c[ImGuiCol_Text] = v4(t.ink);
  c[ImGuiCol_TextDisabled] = v4(t.muted);
  c[ImGuiCol_WindowBg] = v4(t.bg);
  c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_PopupBg] = v4(t.surface);
  c[ImGuiCol_Border] = v4(t.line);
  c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_FrameBg] = v4(t.surface);
  c[ImGuiCol_FrameBgHovered] = v4(t.surface2);
  c[ImGuiCol_FrameBgActive] = v4(t.surface2);
  c[ImGuiCol_TitleBg] = v4(t.surface);
  c[ImGuiCol_TitleBgActive] = v4(t.surface);
  c[ImGuiCol_MenuBarBg] = v4(t.surface);
  c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_ScrollbarGrab] = v4(t.line2);
  c[ImGuiCol_ScrollbarGrabHovered] = v4(t.muted);
  c[ImGuiCol_ScrollbarGrabActive] = v4(t.muted);
  c[ImGuiCol_CheckMark] = v4(t.accent);
  c[ImGuiCol_SliderGrab] = v4(t.accent);
  c[ImGuiCol_SliderGrabActive] = v4(t.accent);
  c[ImGuiCol_Button] = v4(t.surface);
  c[ImGuiCol_ButtonHovered] = v4(t.surface2);
  c[ImGuiCol_ButtonActive] = v4(t.surface3);
  c[ImGuiCol_Header] = v4(t.selbg);
  c[ImGuiCol_HeaderHovered] = v4(t.surface2);
  c[ImGuiCol_HeaderActive] = v4(t.selbg);
  c[ImGuiCol_Separator] = v4(t.line);
  c[ImGuiCol_SeparatorHovered] = v4(t.accent);
  c[ImGuiCol_SeparatorActive] = v4(t.accent);
  c[ImGuiCol_ResizeGrip] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_Tab] = v4(t.surface);
  c[ImGuiCol_TabHovered] = v4(t.surface2);
  c[ImGuiCol_TabActive] = v4(t.accentBg);
  c[ImGuiCol_TableHeaderBg] = v4(t.surface2);
  c[ImGuiCol_TableBorderStrong] = v4(t.line);
  c[ImGuiCol_TableBorderLight] = v4(t.line);
  c[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_TableRowBgAlt] = v4(t.rowline);
  c[ImGuiCol_TextSelectedBg] = v4(t.selbg);
  c[ImGuiCol_DragDropTarget] = v4(t.accent);
  c[ImGuiCol_NavHighlight] = v4(t.accent);
  c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, t.dark ? 0.55f : 0.35f);
  if (m.modern) {
    s.WindowRounding = 12;
    s.ChildRounding = 10;
    s.FrameRounding = 8;
    s.PopupRounding = 10;
    s.WindowPadding = ImVec2(16, 14);
    s.FramePadding = ImVec2(10, m.compact ? 3.f : 5.f);
    s.ItemSpacing = ImVec2(10, m.compact ? 4.f : 7.f);
    s.FrameBorderSize = 0;
    c[ImGuiCol_FrameBg] = v4(t.surface2);
    c[ImGuiCol_Button] = v4(t.surface2);
    c[ImGuiCol_ButtonHovered] = v4(t.accentBg);
    c[ImGuiCol_ButtonActive] = v4(t.selbg);
    c[ImGuiCol_HeaderHovered] = v4(t.accentBg);
  }
  s.ScaleAllSizes(m.scale);
  ImGui::GetStyle() = s;
}

}  // namespace roadmap::ui
