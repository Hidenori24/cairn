// UI の各ファイルで共有する関数（app/ui の中だけで使う）
#pragma once

#include <string>
#include <vector>

#include "roadmap/commands.hpp"
#include "ui/app_ui.hpp"
#include "ui/strings.hpp"
#include "ui/widgets.hpp"

namespace roadmap::ui {

// ---- ビュー ----
void drawListView(Frame& f, ImVec2 size);
void drawGanttView(Frame& f, ImVec2 size);
void drawBoardView(Frame& f, ImVec2 size);
void drawGoalsView(Frame& f, ImVec2 size);
void drawDetailPanel(Frame& f, ImVec2 size);
void drawPalette(Frame& f);
void drawDialogs(Frame& f);
void drawGoalPopover(Frame& f);

// ---- 共通の操作 ----
void openDialog(Frame& f, Dlg d);
void openContext(Frame& f, const std::string& id);
void openStatusMenu(Frame& f, const std::string& id);
void openDetail(Frame& f, const std::string& id, int tab = 0);
// 行のクリック（Ctrl・Shift を見て選択を変える）
void rowClick(Frame& f, const std::string& id);
// その場編集を始める（fresh = 追加直後の行）
void beginEdit(Frame& f, const std::string& id, EditField field, bool fresh = false);
// その場編集の入力欄を描く（カーソル位置に、幅 w で）。終わったら false
bool drawInlineEdit(Frame& f, float w);
bool editing(const UiState& u, const std::string& id, EditField field);
// 選択をテンプレの追加先にしてダイアログを開く
void openTplApply(Frame& f);
void openTplSave(Frame& f, const std::string& id);

// 件名セル（開閉・種別・表示ID・件名・コメント数・ホバー時の小ボタン）
struct NameCellOpts {
  bool showHoverButtons = true;
  bool indent = true;
};
void drawNameCell(Frame& f, const VisibleRow& r, ImVec2 pos, ImVec2 size, const NameCellOpts& o = {});

// 表の列（リストとガントの左の表で共有）
enum class Col { Grip, Name, Type, Status, Asg, Prio, Start, Finish, Dur, Float, Deadline };
struct ColDef {
  Col col;
  float w;
};
// 1行を描き、クリック・ダブルクリック・右クリックを扱う。flat = 並べ替え中（インデントなし）
void drawTableRow(Frame& f, const VisibleRow& r, ImVec2 pos, float width, const std::vector<ColDef>& cols, bool flat,
                  bool highlight = false);
// 見出しの行
void drawTableHeader(Frame& f, ImVec2 pos, float width, float h, const std::vector<ColDef>& cols, bool sortable);

// ルート課題の色番号（子を持つか Epic のルートだけ 1〜7、ほかは 0。JS版 rootIndexMap）
std::vector<int> rootIndexMap(const Tree& T, const std::vector<Issue>& issues);
// 担当者名のハッシュ（h = h*31 + ch を32bit符号なしで）
unsigned nameHash(const std::string& s);
// ゴール（子を持たず、種別かラベルが Milestone）
bool isGoal(const Tree& T, const std::vector<Issue>& issues, int id);
// 世代名（最上位・子・孫・ひ孫・玄孫・第N世代）
std::string generationName(int depth);
// 状況の分類での表示色
ImU32 catColor(const UiState& u, Cat c);

}  // namespace roadmap::ui
