// 共通の部品（ボタン・チップ・ピル・種別アイコンなど）
#pragma once

#include <string>

#include <imgui.h>

#include "roadmap/engine.hpp"
#include "ui/app_ui.hpp"

namespace roadmap::ui {

struct RiskColor {
  ImU32 fg, bg;
};
RiskColor riskColor(const UiState& u, Risk r);
ImU32 rootColor(const UiState& u, int idx);

std::string initials(const std::string& name);
// 表示幅に収まるよう末尾を「…」で切る
std::string ellipsize(const std::string& s, float maxW);

// アイコンだけのボタン（ツールチップつき）
bool iconButton(UiState& u, const char* id, const char* icon, const char* tip, bool enabled = true, bool active = false);
// アイコン + 文字のボタン
bool iconTextButton(UiState& u, const char* id, const char* icon, const char* label, bool primary = false,
                    bool active = false, bool enabled = true);
// 小さなチップ（表示のみ / 押せる）
void chip(UiState& u, const std::string& text, ImU32 fg, ImU32 bg);
bool chipButton(UiState& u, const char* id, const std::string& text, ImU32 fg, ImU32 bg, bool dim = false);
void floatChip(UiState& u, const ScheduleRow& r);
std::string floatLabel(const ScheduleRow& r);
// 状況のピル。押されたら true
bool statusPill(UiState& u, const char* id, const std::string& status, bool clickable);
void typeIcon(UiState& u, const std::string& type);
void avatar(UiState& u, const std::string& name);
void tip(const char* text);
void tip(const std::string& text);

// IME の変換中でない Enter（SPEC 付録D-1）
bool enterPressed(const UiState& u);
// 入力欄にフォーカスがないときのキー
bool globalKey(ImGuiKey k);

// 右寄せのためのスペーサ（残り幅 w を空ける）
void spacerTo(float w);
const char* prioMark(const std::string& prio);
ImU32 prioColor(const UiState& u, const std::string& prio);

}  // namespace roadmap::ui
