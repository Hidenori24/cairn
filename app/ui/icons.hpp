// アイコン（JS版 ui.jsx の PATHS と同じ SVG パスを ImDrawList で描く。SPEC 14-9）
#pragma once

#include <imgui.h>

namespace roadmap::ui {

// name: "plus" "undo" "gantt" … 。size は論理ピクセル（24単位の viewBox を縮める）
void drawIcon(ImDrawList* dl, const char* name, ImVec2 topLeft, float size, ImU32 col, float stroke = 2.0f);
bool hasIcon(const char* name);

}  // namespace roadmap::ui
