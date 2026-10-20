// 画面全体（SPEC 10章）。毎フレーム AppController の状態を読んで描く（即時モード）。
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <imgui.h>

#include "app_controller.hpp"
#include "ui/theme.hpp"

namespace roadmap::ui {

// プラットフォーム（Win32 など）が UI に提供する機能
class IPlatform {
 public:
  virtual ~IPlatform() = default;
  // ファイルを選んで中身（UTF-8 に直したもの）と名前を返す
  virtual std::optional<std::pair<std::string, std::string>> openTextFile() = 0;
  // 保存ダイアログ。成功したら true
  virtual bool saveTextFile(const std::string& suggestedName, const std::string& content) = 0;
  virtual void openFolder(const std::string& path) = 0;
  // フォルダを選ぶ（start = 最初に開く場所）。やめたら nullopt
  virtual std::optional<std::string> pickFolder(const std::string& start) = 0;
  virtual bool systemDark() = 0;
  virtual void requestQuit() = 0;
};

enum class Dlg {
  None, NewProject, EditProject, ConfirmDelete, Import, Export, Folder, Settings, Workload, Warnings, Shortcuts,
  TplApply, TplSave, Conflict, ConfirmQuit, AppSettings, ConfirmIssueDelete
};

enum class EditField { None, Name, Assignee, Dur };

struct UiState {
  Metrics m;
  const Tokens* t = &lightTokens();
  ImFont* fontBody = nullptr;
  ImFont* fontMono = nullptr;
  ImFont* fontBold = nullptr;
  ImFont* fontLarge = nullptr;
  bool imeComposing = false;       // IME の変換中（プラットフォームが設定）
  int imeCooldown = 0;             // 変換を確定した直後のフレーム数（Enter を無視）

  // その場編集
  EditField editField = EditField::None;
  std::string editId;
  std::string editBuf;
  bool editFresh = false;          // 追加直後の行
  bool editFocus = false;          // 次のフレームでフォーカスを入れる
  std::string editStartValue;
  // 編集を始めてから入力欄がアクティブになるまでに打った文字（落とさずに入れる。B13）
  bool editActive = false;
  int editArmFrame = 0;  // 編集を始めたフレーム
  std::vector<ImWchar> editPendingChars;

  // メニュー・ダイアログ
  Dlg dlg = Dlg::None;
  bool dlgOpening = false;
  std::string ctxId;               // 右クリックした課題
  bool ctxOpen = false;
  std::string statusMenuId;        // 状況ピルの小メニュー
  bool statusMenuOpen = false;
  bool projMenu = false, moreMenu = false, goalPop = false;
  bool paletteOpen = false;
  bool paletteFocus = false;
  std::string paletteQuery;
  int paletteSel = 0;
  bool focusSearch = false;

  // 詳細パネル
  bool detailOpen = false;
  int detailTab = 0;  // 0=詳細 1=依存関係 2=コメント

  // ダイアログの入力
  std::string dName, dKey, dText, dText2, dSearch;
  int dChoice = 0, dChoice2 = 0;
  std::vector<std::string> dTargets;  // テンプレの追加先など
  std::string exportKind = "jira";
  std::string importName;
  bool importReplace = false;
  std::string importError;
  std::vector<std::string> conflictNames;

  // ガント
  bool scrollToBase = true;
  std::string scrolledProject;
  std::string lastZoom;
  struct BarDrag {
    std::string id;
    bool resize = false;
    float startX = 0;
    Day origStart = 0, origFinish = 0;
    bool active = false;
  } barDrag;
  struct RowDrag {
    std::string id;
    bool active = false;
  } rowDrag;
  struct ColDrag {
    std::string col;
    float startX = 0;
    float startW = 0;
    bool active = false;
  } colDrag;

  // リスト
  std::string sortKey;
  std::string openColFilter;  // 次のフレームで開く列の絞り込み（列のキー）
  // アプリの設定: 選んだ新しい保存先（切り替えの確認中）
  std::string pendingDir, pendingError;
  bool pendingCopy = true;
  int pendingCount = 0;
  std::vector<std::string> pendingDelete;  // 削除の確認ダイアログで消す課題
  int sortDir = 0;  // 0=解除 1=昇順 -1=降順

  // 画面に並んでいる順（Shift 選択・矢印用）
  std::vector<std::string> order;
  // フォントに足りない文字（プラットフォームがフォントを作り直す）
  std::set<ImWchar> missingGlyphs;
  bool fontsDirty = false;
};

struct Frame {
  app::AppController& c;
  UiState& u;
  IPlatform& p;
};

// 1フレーム分を描く（ImGui::NewFrame と Render の間で呼ぶ）
void drawApp(app::AppController& c, UiState& u, IPlatform& p);
// 画面の文言に使っている文字（tools/gen_ui_glyphs.py が作る一覧）。フォントの作成時に必ず入れる
const std::vector<ImWchar>& uiGlyphs();
// 画面の文字のうち、フォントにないものを集める（プラットフォームがフォントを作り直す判断に使う）
void collectGlyphs(const app::AppController& c, std::set<ImWchar>& out);

}  // namespace roadmap::ui
