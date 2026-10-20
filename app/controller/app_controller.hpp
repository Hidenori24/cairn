// AppController: 現在のプロジェクト・選択・絞り込み・保存・通知を持ち、UI の操作を core のコマンドに変換する。
// ImGui・Win32 の型は持たない（SPEC 3.1）。Linux でもビルド・テストできる。
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "roadmap/commands.hpp"
#include "roadmap/store.hpp"

namespace roadmap::app {

enum class View { Gantt, Board, List, Goals };
const char* viewKey(View v);  // "gantt" / "board" / "list" / "goals"
View viewFromKey(const std::string& k);

enum class SaveState { Saved, Saving, Error };

struct Toast {
  int id = 0;
  std::string text;
  NoticeAction action = NoticeAction::None;
  std::int64_t expiresAt = 0;
};

enum class SampleKind { Empty, Hierarchy, Regulation };
enum class ImportMode { New, Replace };

struct ImportOutcome {
  bool ok = false;
  bool isJson = false;
  std::string error;
  // JSON（共有・バックアップ）で同じ id のプロジェクトがあるとき
  std::vector<std::string> conflictNames;
};

class AppController {
 public:
  AppController(IFileStore& fs, const IClock& clock, std::string dataDir, std::string prefsPath);

  // ---- 起動・周期処理 ----
  void start();                 // prefs を読み、フォルダを同期し、前回のプロジェクトを開く
  void tick();                  // 毎フレーム: 保存の予約・再試行・通知の期限
  void onActivated();           // ウィンドウがアクティブに戻った（3秒以上経っていれば同期）
  void syncFolder(bool manual); // フォルダの変更を読み込む
  bool flushNow();              // 終了時など、すぐに書く
  bool hasUnsaved() const;
  bool firstRun() const { return firstRun_; }

  // ---- プロジェクト ----
  Session* session() { return session_.get(); }
  const Session* session() const { return session_.get(); }
  const std::string& currentId() const { return currentId_; }
  struct ProjectItem {
    std::string id, name, key;
  };
  std::vector<ProjectItem> projects() const;
  bool openProject(const std::string& id);
  std::string createProject(SampleKind kind, const std::string& name, const std::string& key);
  void editProject(const std::string& name, const std::string& key);
  void duplicateProject();
  bool deleteCurrentProject();
  ImportOutcome importText(const std::string& text, const std::string& name, ImportMode mode);
  // JSON の取り込みを確定する（overwrite = 同じ id を上書き）
  bool confirmJsonImport(bool overwrite);
  std::string exportText(const std::string& kind) const;  // "jira" / "app" / "share" / "backup"
  std::string exportFileName(const std::string& kind) const;

  // ---- 表示状態（settings.ui。取り消しの対象外）----
  View view() const;
  void setView(View v);
  std::string uiStr(const char* key, const std::string& def) const;
  int uiInt(const char* key, int def) const;
  bool uiBool(const char* key, const char* sub, bool def) const;
  void setUi(const char* key, Json v);
  void setUiSub(const char* key, const std::string& sub, Json v);

  // ---- 選択（SPEC 10.4.1）----
  const std::vector<std::string>& selection() const { return sel_; }
  const std::string& cursor() const { return cursor_; }
  bool isSelected(const std::string& id) const;
  void select(const std::string& id);
  void toggleSelect(const std::string& id);
  void rangeSelect(const std::string& id, const std::vector<std::string>& order);
  void setSelection(std::vector<std::string> ids, const std::string& cursor);
  void clearSelection();
  void moveCursor(int dir, bool extend, const std::vector<std::string>& order);
  void pruneSelection();  // 計算や削除で消えた課題を外す
  // 選択のうち、祖先も選ばれているものを除いた「動かす単位」
  std::vector<std::string> selectionRoots() const;

  // ---- 絞り込み ----
  // 上のバーの条件（filter）: 検索語は一致しない課題を隠し、それ以外（担当・状況・優先度・遅れ/余裕小）は薄くする。
  // 列見出しの条件（columnFilter）: 一致しない課題を隠す
  Filter& filter() { return filter_; }
  const Filter& filter() const { return filter_; }
  ColumnFilter& columnFilter() { return colFilter_; }
  const ColumnFilter& columnFilter() const { return colFilter_; }
  Filter hideFilter() const;  // 隠す条件（検索語だけ）
  Filter dimFilter() const;   // 薄くする条件（検索語以外）
  bool filterActive() const { return filter_.active() || colFilter_.active(); }
  void clearFilter() { filter_ = Filter{}, colFilter_ = ColumnFilter{}; }
  std::string filterLabel() const;  // 効いている条件の名前
  // 表示する行・末端（隠す条件と列の条件を当てたもの）
  std::vector<VisibleRow> visibleRows() const;
  std::vector<int> visibleLeaves() const;
  // 薄くする課題（添字ごと。薄くする条件がなければ空）
  std::vector<char> dimmedSet() const;

  // ---- 選択への操作（Undo 1回で戻る）----
  void indentSelection();
  void outdentSelection();
  void moveSelection(int dir);
  void deleteSelection();
  void setStatusFor(const std::vector<std::string>& ids, const std::string& st);
  void setPrioFor(const std::vector<std::string>& ids, const std::string& pr);
  void setAssigneeFor(const std::vector<std::string>& ids, const std::string& who);
  std::string addSibling();  // カーソル行と同じ階層に追加（なければ最上位）
  std::string addChild();
  void undo();
  void redo();

  // ---- テンプレート ----
  std::vector<Template> templates() const;
  std::string saveTemplate(const std::string& name, const std::string& desc, std::vector<TemplateItem> items);
  void deleteTemplate(const std::string& id);

  // ---- 端末ごとの設定 ----
  Prefs& prefs() { return prefs_; }
  void savePrefs();
  void setTheme(const std::string& t);
  void setDensity(const std::string& d);
  void dismissHint();

  // ---- 通知・保存状態 ----
  const std::vector<Toast>& toasts() const { return toasts_; }
  void toast(const std::string& text, NoticeAction a = NoticeAction::None);
  void dismissToast(int id);
  void runToastAction(int id);
  SaveState saveState() const { return saveState_; }
  const std::string& saveError() const { return saveError_; }
  const std::string& savedAt() const { return savedAt_; }
  void retrySave();
  const std::string& dataDir() const { return ws_.dataDir(); }
  bool dataDirFallback = false;  // 既定の場所に書けなかった
  Workspace& workspace() { return ws_; }
  const IClock& clock() const { return clock_; }

  // 編集の後に呼ぶ（UI は操作のたびに呼ばなくてもよい。tick でも拾う）
  void afterEdit();
  // フレームの終わりに呼ぶ。描画中に差し替えたプロジェクト・計算結果をここで捨てる
  // （それまでは古いものを残し、描画中の参照が壊れないようにする）
  void releaseFrame();

 private:
  void openSession(const std::string& id, bool keepHistory);
  void scheduleSave();
  void absorbNotices();
  std::int64_t now() const { return clock_.monotonicMs(); }

  IFileStore& fs_;
  const IClock& clock_;
  Workspace ws_;
  std::string prefsPath_;
  Prefs prefs_;
  std::unique_ptr<Session> session_;
  std::vector<std::unique_ptr<Session>> retiredSessions_;
  std::string currentId_;
  std::uint64_t seenVersion_ = 0;
  bool firstRun_ = false;

  std::vector<std::string> sel_;
  std::string cursor_, anchor_;
  Filter filter_;
  ColumnFilter colFilter_;

  std::vector<Toast> toasts_;
  int toastSeq_ = 0;

  SaveState saveState_ = SaveState::Saved;
  std::string saveError_, savedAt_;
  std::int64_t saveDueAt_ = -1;
  int retryStep_ = 0;
  std::int64_t lastSync_ = 0;

  std::optional<ParsedBackup> pendingJson_;
};

}  // namespace roadmap::app
