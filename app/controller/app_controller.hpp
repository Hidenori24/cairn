// AppController: 現在のプロジェクト・選択・絞り込み・保存・通知を持ち、UI の操作を core のコマンドに変換する。
// ImGui・Win32 の型は持たない（SPEC 3.1）。Linux でもビルド・テストできる。
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "roadmap/attachments.hpp"
#include "roadmap/links.hpp"
#include "roadmap/commands.hpp"
#include "roadmap/report.hpp"
#include "roadmap/store.hpp"
#include "roadmap/weekly.hpp"

namespace roadmap::app {

enum class View { Gantt, Board, List, Goals, Stats };
const char* viewKey(View v);  // "gantt" / "board" / "list" / "goals" / "stats"
View viewFromKey(const std::string& k);

enum class SaveState { Saved, Saving, Error };

struct Toast {
  int id = 0;
  std::string text;
  NoticeAction action = NoticeAction::None;
  std::string arg;  // 動作の引数（OpenFolder のフォルダ）
  std::int64_t expiresAt = 0;
};

enum class SampleKind { Empty, Hierarchy, EmbeddedSw };
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

  // ---- 報告（HTML）の書き出し（Issue #42）----
  // 既定のファイル名 "<プロジェクト名>_<今日>.html"
  std::string reportFileName() const;
  // 画面の状態（絞り込み・薄くする課題・選択・基準日）から HTML を組み立てる。ファイルには触れない。
  // プロジェクトを開いていない・範囲が「選んだ課題の配下」なのに課題が 1 つだけ選ばれていないときはエラー
  Result<std::string> buildReport(const ExportOptions& opt) const;
  // 組み立てて path に書く。成功したら「フォルダを開く」つきの通知、失敗したら理由を通知する（例外は出さない）
  Status exportReport(const std::string& path, const ExportOptions& opt);

  // ---- 週次レポート（Issue #43）----
  // プロジェクトに覚えている所感（次の週の下書きに前回の所感を残す）。開いていなければ空
  std::string weeklyNotes() const;
  void setWeeklyNotes(const std::string& notes);
  // 今の状態（統計・負荷・先週のスナップショット）から構造体を組み立てる。ファイルにも設定にも触れない
  Result<WeeklyReport> buildWeekly(const WeeklyOptions& opt) const;
  // Markdown（html = false）か HTML の文字列。副作用なし（プレビュー・コピーに使う）
  Result<std::string> renderWeekly(const WeeklyOptions& opt, bool html) const;
  // 既定のファイル名 "<プロジェクト名>_週報_<期間の最初の日>.md" / ".html"
  std::string weeklyFileName(const WeeklyOptions& opt, bool html) const;
  // 書き出した（コピー・保存した）ときに呼ぶ。所感とスナップショット（次回の「先週から変わったこと」）を設定に覚える
  void recordWeekly(const WeeklyOptions& opt);
  // 組み立てて path に書く。成功したら recordWeekly と「フォルダを開く」つきの通知、失敗したら理由を通知する（例外は出さない）
  Status exportWeekly(const std::string& path, const WeeklyOptions& opt, bool html);

  // ---- 状況の件数の履歴（累積フロー図。Issue #64。取り消しの対象外で、project.settings.json の "history" に保存する）----
  // 記録のきっかけ。Session::recordHistory を呼ぶのはここ（recordHistory）だけにまとめている
  enum class HistoryTrigger {
    Open,    // プロジェクトを開いたとき: その週の点がなければ追加（あれば何もしない＝開くたびに設定ファイルを書き換えない）
    Manual,  // 統計の「いま記録する」: 同じ週は置き換え、結果を通知する
    Weekly   // 週次レポートをコピー・保存したとき: 同じ週は置き換え（通知しない）
  };
  void recordHistory(HistoryTrigger why);

  // ---- ベースライン（計画の日程の保存。Issue #53。取り消しの対象外で、project.settings.json に保存する）----
  // 保存するときの名前の初期値 "2026-10-19 の計画"
  std::string defaultBaselineName() const;
  // いまの日程を保存してそのまま表示する。5 つを超えるときは断り、理由を通知して Error を返す
  Result<std::string> saveBaseline(const std::string& name);
  bool renameBaseline(const std::string& id, const std::string& name);
  bool deleteBaseline(const std::string& id);
  // 表示するベースライン（空 = 表示しない）
  void showBaseline(const std::string& id);

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
  // 選んだ課題を画面の表示順（order。各画面の u.order）に A→B→C と順につなぐ / 選んだ課題どうしのつなぎを外す。Undo 1回
  ChainResult chainSelection(const std::vector<std::string>& order);
  int unchainSelection();
  // 課題を追加する（名前はアプリの設定「新しい課題の名前」に従う。選択は変えない）。afterId の後ろに入れる。作った id を返す
  std::string createIssue(std::string parentId, std::string afterId);
  // 設定に従った、parentId（空 = 最上位）の下に作る課題の名前。空 = 既定名
  std::string newIssueName(const std::string& parentId) const;
  std::string addSibling();  // カーソル行と同じ階層に追加（なければ最上位）
  std::string addChild();
  void undo();
  void redo();

  // ---- 添付ファイル（SPEC 8.9。取り消しの対象外。本体は <プロジェクト>/添付/<Issue Id>/）----
  // 添付できるか（プロジェクトを開いていて、フォルダ構成であること。平置きのプロジェクトには置き場がない）
  bool canAttach() const;
  std::string attachDisabledReason() const;  // canAttach() が false のときの理由（画面に出す）
  const std::vector<Attachment>& attachmentsOf(const std::string& issueId) const;
  std::size_t attachmentCount(const std::string& issueId) const;
  // ファイルまたはフォルダのパス（UTF-8）を課題に付ける。結果は通知にも出す。すぐ保存する
  AttachOutcome attachFiles(const std::string& issueId, const std::vector<std::string>& files);
  // 中身を直接付ける（クリップボードの画像など）
  bool attachData(const std::string& issueId, const std::string& name, const std::string& data);
  // クリップボードの画像（BMP ファイルのバイト列）を「貼り付け-<日時>.bmp」で付ける。断られたら false（通知を出す）
  bool attachClipboardImage(const std::string& issueId, const std::string& bmp);
  // 別の課題へ移す・コピーする（サムネイルのドラッグ）。先の課題が実在すること、自分自身でないこと、上限を見る。
  // 移せなければ false（通知を出す。ファイルもメタ情報も元のまま）。成功したら通知にも出す。すぐ保存する
  bool moveAttachment(const std::string& fromIssue, std::size_t index, const std::string& toIssue);
  bool copyAttachment(const std::string& fromIssue, std::size_t index, const std::string& toIssue);
  // 同じ課題の中で並べ替える（from を取り除いたあとの添字 to の位置へ）。保存する
  bool reorderAttachment(const std::string& issueId, std::size_t from, std::size_t to);
  // _削除済み へ移して外す。移せなければ false（通知を出す）
  bool removeAttachment(const std::string& issueId, std::size_t index);
  // 本体のパス（開く・サムネイル用）。なければ空
  std::string attachmentFile(const std::string& issueId, std::size_t index) const;
  bool attachmentExists(const std::string& issueId, std::size_t index) const;  // 本体がフォルダにあるか
  // 本体の中身（画像のサムネイル・プレビュー用）。読めなければエラー。1ファイルの上限を超えるものは読まない
  Result<std::string> attachmentBytes(const std::string& issueId, std::size_t index) const;
  // プロジェクトのフォルダ（平置き・まだ書いていないときは空）
  std::string currentProjectDir() const;

  // ---- 関連リンク（SPEC 8.10。Issue #60。取り消しの対象外。settings.json の "links" に保存する）----
  // 場所を指すだけで何もコピーしないので、添付と違い平置きのプロジェクトにも付けられる。
  // URL・パス・表示名は機微なので、通知にもログにも入れない（件数と理由だけを出す）
  const std::vector<Link>& linksOf(const std::string& issueId) const;
  std::size_t linkCountOf(const std::string& issueId) const;
  // 追加する。断られたら false で、理由を reason に入れる（通知は出さない。入力欄の下に出すため）。課題が実在すること
  bool addLink(const std::string& issueId, const std::string& title, const std::string& target, std::string* reason = nullptr);
  // 貼り付けたテキストの各行を 1 行 1 リンクとして追加する。結果は通知にも出す（追加した件数・断った件数と最初の理由）
  LinkPasteOutcome addLinksFromText(const std::string& issueId, const std::string& text);
  bool editLink(const std::string& issueId, std::size_t index, const std::string& title, const std::string& target,
                std::string* reason = nullptr);
  bool removeLink(const std::string& issueId, std::size_t index);
  bool reorderLink(const std::string& issueId, std::size_t from, std::size_t to);
  // 別の課題へ移す・コピーする（ドラッグ）。先の課題が実在すること。断られたら false（通知を出す。元のまま）
  bool moveLink(const std::string& fromIssue, std::size_t index, const std::string& toIssue);
  bool copyLink(const std::string& fromIssue, std::size_t index, const std::string& toIssue);

  // ---- テンプレート ----
  std::vector<Template> templates() const;
  std::string saveTemplate(const std::string& name, const std::string& desc, std::vector<TemplateItem> items);
  void deleteTemplate(const std::string& id);

  // ---- 端末ごとの設定 ----
  Prefs& prefs() { return prefs_; }
  const Prefs& prefs() const { return prefs_; }
  void savePrefs();
  void setTheme(const std::string& t);
  void setDensity(const std::string& d);
  // 画面全体の拡大縮小（%）。段階は 70 80 90 100 110 125 150 175 200。変わったら true（すぐ保存する）
  static constexpr int kUiScaleSteps[] = {70, 80, 90, 100, 110, 125, 150, 175, 200};
  bool stepUiScale(int dir);  // dir > 0: 次の段階へ / dir < 0: 前の段階へ
  bool setUiScale(int pct);
  void dismissHint();

  // ---- 通知・保存状態 ----
  const std::vector<Toast>& toasts() const { return toasts_; }
  void toast(const std::string& text, NoticeAction a = NoticeAction::None, const std::string& arg = {});
  void dismissToast(int id);
  void runToastAction(int id);
  SaveState saveState() const { return saveState_; }
  const std::string& saveError() const { return saveError_; }
  const std::string& savedAt() const { return savedAt_; }
  void retrySave();
  const std::string& dataDir() const { return ws_->dataDir(); }
  bool dataDirFallback = false;  // 既定の場所に書けなかった
  Workspace& workspace() { return *ws_; }

  // ---- 保存先（アプリの設定）----
  // 起動時に、決めた保存先とその由来・exe のフォルダを渡す（画面の表示と、設定に覚える相対パスのため）
  void setDataDirInfo(const DataDirChoice& c, std::string exeDir);
  const DataDirChoice& dataDirInfo() const { return dirInfo_; }
  bool dataDirLocked() const;  // 起動引数・環境変数で決まっている（画面からは変えない）
  std::string defaultDir() const;
  int countProjectsIn(const std::string& dir);  // その場所にあるプロジェクトの数（フォルダ構成も平置きも）
  // 平置き（以前の保存形式）のまま読んでいるプロジェクトの数。自動では動かさず、利用者の操作で移す
  int legacyCount() const { return ws_->legacyCount(); }
  // 平置きをフォルダ構成へ移す（移す前に _バックアップ/移行前.<日時>/ へ控える）。結果は通知にも出す
  Result<Workspace::MigrateReport> migrateLegacy();
  // 保存先を切り替える。copy = 手元のプロジェクトを新しい場所にも書く（同じものがあれば場所の方を残す）。
  // 今の場所に書けないとき・新しい場所に書けないときは切り替えない
  Status switchDataDir(const std::string& dir, bool copy);
  const std::string& prefsPath() const { return prefsPath_; }
  const IClock& clock() const { return clock_; }

  // 編集の後に呼ぶ（UI は操作のたびに呼ばなくてもよい。tick でも拾う）
  void afterEdit();
  // フレームの終わりに呼ぶ。描画中に差し替えたプロジェクト・計算結果をここで捨てる
  // （それまでは古いものを残し、描画中の参照が壊れないようにする）
  void releaseFrame();

 private:
  void openSession(const std::string& id, bool keepHistory);
  void applyAutoBaseDate();  // アプリの設定「開いたら基準日を今日に」（利用者が開いたときだけ）
  void scheduleSave();
  void absorbNotices();
  std::int64_t now() const { return clock_.monotonicMs(); }

  IFileStore& fs_;
  const IClock& clock_;
  std::unique_ptr<Workspace> ws_;
  std::set<std::string> baseAdjusted_;  // この起動中に基準日を今日に合わせたプロジェクト
  DataDirChoice dirInfo_;
  std::string exeDir_;
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
