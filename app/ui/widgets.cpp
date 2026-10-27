#include "ui/widgets.hpp"

#include <algorithm>

#include "roadmap/text.hpp"
#include "ui/icons.hpp"

namespace roadmap::ui {

ImU32 blendColor(ImU32 a, ImU32 b, float amount) {
  const ImVec4 x = v4(a), y = v4(b);
  const float t = std::clamp(amount, 0.f, 1.f);
  return ImGui::ColorConvertFloat4ToU32(ImVec4(x.x+(y.x-x.x)*t, x.y+(y.y-x.y)*t,
      x.z+(y.z-x.z)*t, x.w+(y.w-x.w)*t));
}

float hoverAmount(UiState& u, const char* id, bool hovered) {
  if (!u.m.animate) return hovered ? 1.f : 0.f;
  ImGuiStorage* state = ImGui::GetStateStorage();
  const ImGuiID key = ImGui::GetID(id);
  const float next = motionStep(state->GetFloat(key, 0.f), hovered, ImGui::GetIO().DeltaTime, u.m.animate);
  state->SetFloat(key, next);
  return next;
}

RiskColor riskColor(const UiState& u, Risk r) {
  const Tokens& t = *u.t;
  switch (r) {
    case Risk::Done: return {t.muted, t.surface3};
    case Risk::Late: return {t.late, t.lateBg};
    case Risk::Tight: return {t.tight, t.tightBg};
    default: return {t.ok, t.okBg};
  }
}

ImU32 rootColor(const UiState& u, int idx) { return u.t->c[((idx % 8) + 8) % 8]; }

std::string initials(const std::string& nameIn) {
  const std::string n = text::trim(nameIn);
  if (n.empty()) return "";
  std::size_t i = 0;
  const char32_t c = text::decodeAt(n, i);
  if (c >= 0x3000 && c <= 0x9FFF) return n.substr(0, i);
  return text::toUpper(text::utf16Prefix(n, 2));
}

std::string ellipsize(const std::string& s, float maxW) {
  if (ImGui::CalcTextSize(s.c_str()).x <= maxW) return s;
  const std::u32string u = text::toU32(s);
  std::size_t lo = 0, hi = u.size();
  while (lo < hi) {
    const std::size_t mid = (lo + hi + 1) / 2;
    const std::string t = text::fromU32(u.substr(0, mid)) + "…";
    if (ImGui::CalcTextSize(t.c_str()).x <= maxW) lo = mid;
    else hi = mid - 1;
  }
  return text::fromU32(u.substr(0, lo)) + "…";
}

std::vector<std::string> ancestorNames(const Tree& T, int idx) {
  std::vector<std::string> out;
  // 循環があっても止まるよう、たどる回数に上限を置く
  for (int a = T.parentOf[idx], guard = 0; a >= 0 && guard < T.n; a = T.parentOf[a], ++guard) out.insert(out.begin(), T.name[a]);
  return out;
}

std::string ancestorLabel(const Tree& T, int idx, float maxW) {
  const std::vector<std::string> names = ancestorNames(T, idx);
  if (names.empty()) return "";
  const auto kept = text::ancestorPath(
      names, [](const std::string& s) { return ImGui::CalcTextSize(s.c_str()).x; }, maxW);
  return ellipsize(text::ancestorPathText(names, kept), maxW);
}

void tip(const char* t) {
  if (t && *t && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", t);
}
void tip(const std::string& t) { tip(t.c_str()); }

bool iconButton(UiState& u, const char* id, const char* icon, const char* tipText, bool enabled, bool active) {
  const float sz = u.m.s(28);
  ImGui::PushID(id);
  if (!enabled) ImGui::BeginDisabled();
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const bool pressed = ImGui::InvisibleButton("##b", ImVec2(sz, sz));
  const bool hov = ImGui::IsItemHovered();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  if (active) dl->AddRectFilled(p, ImVec2(p.x + sz, p.y + sz), u.t->accentBg, u.m.s(6));
  else {
    const float fade = hoverAmount(u, "##hover", hov);
    if (fade > 0.001f) dl->AddRectFilled(p, ImVec2(p.x + sz, p.y + sz), withAlpha(u.t->surface3, fade), u.m.s(u.m.modern ? 8.f : 6.f));
  }
  const ImU32 col = !enabled ? withAlpha(u.t->muted, 0.5f) : (active ? u.t->accent : u.t->ink);
  drawIcon(dl, icon, ImVec2(p.x + u.m.s(6), p.y + u.m.s(6)), u.m.s(16), col);
  if (!enabled) ImGui::EndDisabled();
  tip(tipText);
  ImGui::PopID();
  return pressed && enabled;
}

bool iconTextButton(UiState& u, const char* id, const char* icon, const char* label, bool primary, bool active,
                    bool enabled) {
  ImGui::PushID(id);
  const ImVec2 ts = ImGui::CalcTextSize(label);
  const float h = u.m.s(28);
  const float iconW = icon ? u.m.s(20) : 0;
  const float w = ts.x + iconW + u.m.s(20);
  const ImVec2 p = ImGui::GetCursorScreenPos();
  if (!enabled) ImGui::BeginDisabled();
  const bool pressed = ImGui::InvisibleButton("##b", ImVec2(w, h));
  const bool hov = ImGui::IsItemHovered();
  if (!enabled) ImGui::EndDisabled();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImU32 bg = 0, fg = u.t->ink, border = 0;
  if (primary) {
    bg = hov ? withAlpha(u.t->accent, 0.88f) : u.t->accent;
    fg = u.t->accentInk;
  } else if (active) {
    bg = u.t->accentBg;
    fg = u.t->accent;
  } else {
    bg = blendColor(u.t->surface, u.m.modern ? u.t->accentBg : u.t->surface3, hoverAmount(u, "##hover", hov));
    border = u.t->line2;
  }
  if (!enabled) fg = withAlpha(fg, 0.5f);
  dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), bg, u.m.s(6));
  if (border) dl->AddRect(p, ImVec2(p.x + w, p.y + h), border, u.m.s(6));
  float x = p.x + u.m.s(10);
  if (icon) {
    drawIcon(dl, icon, ImVec2(x, p.y + (h - u.m.s(16)) / 2), u.m.s(16), fg);
    x += iconW;
  }
  dl->AddText(ImVec2(x, p.y + (h - ts.y) / 2), fg, label);
  ImGui::PopID();
  return pressed && enabled;
}

void chip(UiState& u, const std::string& s, ImU32 fg, ImU32 bg) {
  const ImVec2 ts = ImGui::CalcTextSize(s.c_str());
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const float h = ts.y + u.m.s(4), w = ts.x + u.m.s(12);
  ImGui::Dummy(ImVec2(w, h));
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), bg, h / 2);
  dl->AddText(ImVec2(p.x + u.m.s(6), p.y + u.m.s(2)), fg, s.c_str());
}

bool chipButton(UiState& u, const char* id, const std::string& s, ImU32 fg, ImU32 bg, bool dim) {
  ImGui::PushID(id);
  const ImVec2 ts = ImGui::CalcTextSize(s.c_str());
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const float h = u.m.s(24), w = ts.x + u.m.s(16);
  const bool pressed = ImGui::InvisibleButton("##c", ImVec2(w, h));
  const bool hov = ImGui::IsItemHovered();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const float a = dim ? 0.45f : 1.0f;
  dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), withAlpha(bg, hov ? 1.0f : a), h / 2);
  dl->AddText(ImVec2(p.x + u.m.s(8), p.y + (h - ts.y) / 2), withAlpha(fg, a), s.c_str());
  ImGui::PopID();
  return pressed;
}

std::string floatLabel(const ScheduleRow& r) { return floatText(r); }

void floatChip(UiState& u, const ScheduleRow& r) {
  const auto rc = riskColor(u, riskOf(r.floatDays, r.done));
  chip(u, floatText(r), rc.fg, rc.bg);
}

bool statusPill(UiState& u, const char* id, const std::string& status, bool clickable) {
  const Cat c = statusCat(status);
  ImU32 fg = u.t->muted, bg = u.t->surface3;
  if (c == Cat::Prog) {
    fg = u.t->accent;
    bg = u.t->accentBg;
  } else if (c == Cat::Done) {
    fg = u.t->ok;
    bg = u.t->okBg;
  }
  const std::string label = status.empty() ? "To Do" : status;
  if (!clickable) {
    chip(u, label, fg, bg);
    return false;
  }
  return chipButton(u, id, label, fg, bg);
}

void typeIcon(UiState& u, const std::string& type) {
  struct T {
    const char* key;
    const char* jp;
    const char* glyph;
    int color;
  };
  static const T kTypes[] = {{"epic", "エピック", "E", 4}, {"story", "ストーリー", "S", 3}, {"sub", "サブ", "s", 6},
                             {"bug", "バグ", "B", 1},      {"milestone", "マイルストーン", "◆", 5}, {"task", "タスク", "✓", 2}};
  const char* g = "•";
  ImU32 col = u.t->muted;
  for (const auto& x : kTypes)
    if (text::icontains(type, x.key) || type.find(x.jp) != std::string::npos) {
      g = x.glyph;
      col = u.t->c[x.color];
      break;
    }
  const float sz = u.m.s(16);
  const ImVec2 p = ImGui::GetCursorScreenPos();
  ImGui::Dummy(ImVec2(sz, sz));
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled(p, ImVec2(p.x + sz, p.y + sz), withAlpha(col, 0.18f), u.m.s(4));
  const ImVec2 ts = ImGui::CalcTextSize(g);
  dl->AddText(ImVec2(p.x + (sz - ts.x) / 2, p.y + (sz - ts.y) / 2), col, g);
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", type.c_str());
}

void avatar(UiState& u, const std::string& name) {
  if (name.empty()) return;
  const std::string ini = initials(name);
  const float sz = u.m.s(20);
  const ImVec2 p = ImGui::GetCursorScreenPos();
  ImGui::Dummy(ImVec2(sz, sz));
  unsigned h = 0;
  for (unsigned char ch : name) h = h * 31 + ch;
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->AddCircleFilled(ImVec2(p.x + sz / 2, p.y + sz / 2), sz / 2, withAlpha(u.t->c[h % 8], 0.9f));
  const ImVec2 ts = ImGui::CalcTextSize(ini.c_str());
  dl->AddText(ImVec2(p.x + (sz - ts.x) / 2, p.y + (sz - ts.y) / 2), IM_COL32_WHITE, ini.c_str());
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", name.c_str());
}

bool enterPressed(const UiState& u) {
  if (u.imeComposing || u.imeCooldown > 0) return false;
  return ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
}

bool globalKey(ImGuiKey k) { return !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(k, true); }

void spacerTo(float w) {
  const float avail = ImGui::GetContentRegionAvail().x;
  if (avail > w) ImGui::Dummy(ImVec2(avail - w, 1));
  ImGui::SameLine(0, 0);
}

const char* prioMark(const std::string& prio) {
  switch (prioRank(prio)) {
    case 0: return "▲▲";
    case 1: return "▲";
    case 3: return "▼";
    case 4: return "▼▼";
    default: return "＝";
  }
}

ImU32 prioColor(const UiState& u, const std::string& prio) {
  const int r = prioRank(prio);
  if (r <= 1) return u.t->late;
  if (r == 2) return u.t->tight;
  return u.t->accent;
}

}  // namespace roadmap::ui
