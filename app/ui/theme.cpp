#include "ui/theme.hpp"

namespace roadmap::ui {

namespace {
constexpr ImU32 rgb(unsigned v) { return IM_COL32((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF, 0xFF); }
constexpr ImU32 rgba(int r, int g, int b, float a) { return IM_COL32(r, g, b, static_cast<int>(a * 255 + 0.5f)); }
}  // namespace

const Tokens& lightTokens() {
  static const Tokens t = [] {
    Tokens x{};
    x.bg = rgb(0xf5f6f8);
    x.surface = rgb(0xffffff);
    x.surface2 = rgb(0xf4f5f7);
    x.surface3 = rgb(0xe9ecf0);
    x.ink = rgb(0x1a2230);
    x.muted = rgb(0x667085);
    x.line = rgb(0xe4e7ec);
    x.line2 = rgb(0xcfd4dc);
    x.accent = rgb(0x2563eb);
    x.accentInk = rgb(0xffffff);
    x.accentBg = rgb(0xe8f0ff);
    x.late = rgb(0xd92d20);
    x.lateBg = rgb(0xfdecea);
    x.tight = rgb(0xb54708);
    x.tightBg = rgb(0xfff1dc);
    x.tightEdge = rgb(0xe08a1e);
    x.ok = rgb(0x12805c);
    x.okBg = rgb(0xe3f5ec);
    const unsigned c[8] = {0x7d8798, 0xb5667f, 0x5b86d6, 0x3f8f88, 0x8c78c6, 0xa38a4a, 0x4793b3, 0xa56db0};
    for (int i = 0; i < 8; ++i) x.c[i] = rgb(c[i]);
    x.hover = rgba(26, 34, 48, .04f);
    x.selbg = rgba(37, 99, 235, .10f);
    x.rowline = rgba(26, 34, 48, .06f);
    x.wkend = rgba(26, 34, 48, .035f);
    x.holbg = rgba(26, 34, 48, .06f);
    x.arrow = rgb(0x98a2b3);
    x.dark = false;
    return x;
  }();
  return t;
}

const Tokens& darkTokens() {
  static const Tokens t = [] {
    Tokens x{};
    x.bg = rgb(0x0e1116);
    x.surface = rgb(0x161b22);
    x.surface2 = rgb(0x1b222b);
    x.surface3 = rgb(0x252d38);
    x.ink = rgb(0xe6e9ee);
    x.muted = rgb(0x8b95a5);
    x.line = rgb(0x262e39);
    x.line2 = rgb(0x37414e);
    x.accent = rgb(0x6ea0ff);
    x.accentInk = rgb(0x0b1220);
    x.accentBg = rgb(0x1a2a48);
    x.late = rgb(0xff7b6f);
    x.lateBg = rgb(0x3a1d1b);
    x.tight = rgb(0xf0b34a);
    x.tightBg = rgb(0x382a10);
    x.tightEdge = rgb(0xf0b34a);
    x.ok = rgb(0x58c987);
    x.okBg = rgb(0x16301f);
    const unsigned c[8] = {0x8f9aae, 0xd0809b, 0x6c98ea, 0x4fb0a7, 0xa891e0, 0xc5a85c, 0x5db0d2, 0xc68bd0};
    for (int i = 0; i < 8; ++i) x.c[i] = rgb(c[i]);
    x.hover = rgba(230, 233, 238, .05f);
    x.selbg = rgba(110, 160, 255, .16f);
    x.rowline = rgba(230, 233, 238, .06f);
    x.wkend = rgba(230, 233, 238, .035f);
    x.holbg = rgba(230, 233, 238, .07f);
    x.arrow = rgb(0x667085);
    x.dark = true;
    return x;
  }();
  return t;
}

ImVec4 v4(ImU32 c) { return ImGui::ColorConvertU32ToFloat4(c); }

ImU32 withAlpha(ImU32 c, float a) {
  ImVec4 v = v4(c);
  v.w *= a;
  return ImGui::ColorConvertFloat4ToU32(v);
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
  s.ScaleAllSizes(m.scale);
  ImGui::GetStyle() = s;
}

}  // namespace roadmap::ui
