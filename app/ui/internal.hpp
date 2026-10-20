// UI の各ファイルで共有する関数（app/ui の中だけで使う）
#pragma once

#include <functional>
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
void drawStatsView(Frame& f, ImVec2 size);
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
// 描画が終わってから実行する（行・カードの描画の途中で課題の数を変える操作に使う）
inline void deferAfterFrame(Frame& f, std::function<void()> fn) { f.u.deferred.push_back(std::move(fn)); }
// 課題を削除する（アプリの設定で「確かめる」なら確認ダイアログを出す）
void requestDelete(Frame& f, std::vector<std::string> ids);
// 上の絞り込みで薄くするときの不透明度（アプリの設定）
inline float dimAlpha(const app::AppController& c) { return static_cast<float>(c.prefs().dimPercent) / 100.f; }
// 選択をテンプレの追加先にしてダイアログを開く
void openTplApply(Frame& f);
void openTplSave(Frame& f, const std::string& id);
// クリップボードの複数行を、選んだ課題の子としてまとめて作る（プレビューのダイアログを開く）
void startPasteChildren(Frame& f);

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
// 見出しの行（列名の右の漏斗で列の絞り込み）
void drawTableHeader(Frame& f, ImVec2 pos, float width, float h, const std::vector<ColDef>& cols, bool sortable);

// 上の絞り込みに一致しない課題を薄く描く。作ってから壊すまでに描いた頂点の不透明度に a を掛ける
class FadeScope {
 public:
  FadeScope(ImDrawList* dl, bool on, float a = 0.3f) : dl_(on ? dl : nullptr), start_(dl->VtxBuffer.Size), a_(a) {}
  ~FadeScope() {
    if (!dl_) return;
    for (int i = start_; i < dl_->VtxBuffer.Size; ++i) {
      ImU32& c = dl_->VtxBuffer[i].col;
      const float al = static_cast<float>((c & IM_COL32_A_MASK) >> IM_COL32_A_SHIFT) * a_;
      c = (c & ~IM_COL32_A_MASK) | (static_cast<ImU32>(al) << IM_COL32_A_SHIFT);
    }
  }
  FadeScope(const FadeScope&) = delete;
  FadeScope& operator=(const FadeScope&) = delete;

 private:
  ImDrawList* dl_;
  int start_;
  float a_;
};
// 薄くする行か（その場編集中の行は薄くしない）
inline bool isDimmed(const UiState& u, const std::vector<char>& dim, int id, const std::string& sid) {
  return !dim.empty() && dim[id] && u.editId != sid;
}

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
