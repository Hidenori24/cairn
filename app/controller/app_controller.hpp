// AppController: 現在のプロジェクト・選択・絞り込み・保存・通知を持ち、UI の操作を core のコマンドに変換する。
// ImGui・Win32 の型は持たない（SPEC 3.1）。Linux でもビルド・テストできる。
#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "roadmap/attachments.hpp"
#include "roadmap/github_ref.hpp"
#include "roadmap/links.hpp"
#include "roadmap/lock.hpp"
#include "roadmap/commands.hpp"
#include "roadmap/report.hpp"
#include "roadmap/store.hpp"
#include "roadmap/weekly.hpp"

namespace roadmap::app {

enum class View { Gantt, Board, List, Goals, Stats, Today };
const char* viewKey(View v);  // "gantt" / "board" / "list" / "goals" / "stats" / "today"
View viewFromKey(const std::string& k);

enum class SaveState { Saved, Saving, Error };

struct Toast {
  int id = 0;
  std::string text;
  NoticeAction action = NoticeAction::None;
  std::string arg;  // 動作の引数（OpenFolder のフォルダ、Complete の課題の Issue Id）
  std::string project;  // Complete: 提案したプロジェクト（切り替わっていたら何もしない）
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
  ~AppController();

  // ---- 起動・周期処理 ----
  void start();                 // prefs を読み、フォルダを同期し、前回のプロジェクトを開く
  void tick();                  // 毎フレーム: 保存の予約・再試行・通知の期限
  void onActivated();           // ウィンドウがアクティブに戻った（3秒以上経っていれば同期）
  void syncFolder(bool manual); // フォルダの変更を読み込む
  bool flushNow();              // 終了時など、すぐに書く
  bool hasUnsaved() const;
  bool firstRun() const { return firstRun_; }

  // ---- 編集中のロック（SPEC 8.5。Issue #62）----
  // 開いているプロジェクトのフォルダに .roadmap.lock を作り、他の人のロックがあれば読み取り専用で開く。
  // 窓口（ILockStore）を渡さなければロックは使わない（今まで通り）。rng を渡さなければ OS の乱数を使う。machine はロックに入るマシン名
  enum class LockKind {
    None,    // ロックなし（使わない設定・窓口がない・平置き・まだフォルダがない）。編集できる
    Mine,    // 自分のロックを持っている。編集できる
    Other,   // 他の人のロックがある。読み取り専用
    Failed,  // ロックを作れない（権限なし・読み取り専用のフォルダ）。読み取り専用
    Lost     // 自分のロックを失った（引き継がれた・消された）。読み取り専用
  };
  struct LockStatus {
    LockKind kind = LockKind::None;
    std::string user, machine;       // Other・Lost: 持ち主（分かるとき）。画面にだけ出す（ログには出さない）
    std::int64_t openedAt = 0;       // 持ち主が開いた時刻（UTC のミリ秒）。0 = 不明
    std::int64_t heartbeatAt = 0;    // 持ち主の最後の更新（UTC のミリ秒）
    bool stale = false;              // Other: 古い（持ち主がいないようだ）。壊れたロックを含む
    bool broken = false;             // ロックのファイルの中身が読めない
    bool free = false;               // Other・Lost: ロックが解けている（「再確認」で編集できる）
    std::string reason;              // Failed・Lost: 理由
  };
  void setLockStore(ILockStore* store, IRandom* rng, std::string machine);
  bool lockEnabled() const { return lockStore_ != nullptr && prefs_.useLock; }
  const LockStatus& lockStatus() const { return lock_.status; }
  // 読み取り専用で開いているか。編集系の入口（コマンド・設定の変更・添付・リンク・履歴/スナップショットの記録・削除・置き換えの取り込み）は
  // すべて、これが真のあいだ何もせず断る。ファイルへも書かない（Session・Workspace でも断る）
  bool readOnly() const;
  // 画面上部の帯の文言（読み取り専用でなければ空）
  std::string readOnlyBand() const;
  // 持ち主が開いた時刻の表示（今日なら "14:05"、そうでなければ "10/18 14:05"。時間帯は setUtcOffsetFn の差）
  std::string lockSinceLabel() const;
  // 「再確認」: フォルダの変更を読み込み、ロックが解けていれば取って編集できる状態にする
  void recheckLock();
  // 「編集を引き継ぐ」: 他の人（古いものを含む）のロックを自分のものにする。相手の未保存の変更は失われる。成功したら true
  bool takeOverLock();
  // アプリの設定「ロックを使う」。切り替えると、開いているプロジェクトのロックを作る・外す
  void setUseLock(bool on);

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

  // ---- 週の頭のスナップショット（自動。「先週から変わったこと」の比べる元。Issue #70。取り消しの対象外で、"weeklyAuto" に保存する）----
  // 取るきっかけ。Session::recordWeeklyAuto を呼ぶのはここ（recordWeeklyAuto）だけにまとめている。
  // 週報のコピー・保存（recordWeekly）は手動のスナップショットを残すだけで、自動のものは置き換えない
  enum class WeeklyAutoTrigger {
    Open,   // プロジェクトを開いたとき: その週のものがなければ取る（あれば何もしない＝開くたびに設定ファイルを書き換えない）
    Rebase  // 開いた直後に基準日を今日へ合わせたとき: 開いたときに取ったものを、合わせたあとの日程で取り直す
  };
  void recordWeeklyAuto(WeeklyAutoTrigger why);

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

  // ---- GitHub の PR・Issue の状態の確認（SPEC 8.11。Issue #61。確認結果はメモリ上のみで、保存しない）----
  // 取得の窓口を差し替える（時計・ファイルと同じ流儀。Win32 は gh、テストは固定の応答）。async = 別のスレッドで 1 件ずつ実行し、
  // 結果を tick() が拾う。false は tick() の中で順に実行する（テスト用）。差し替えると、確認中のものは中断し、確認結果は捨てる
  void setRemoteStatus(std::shared_ptr<IRemoteStatus> impl, bool async = true);
  bool remoteAvailable() const { return remote_ != nullptr; }
  // 閉じた日時（UTC）を暦の日にするときの、その時刻に適用される UTC からの差（分。日本時間は 540。夏時間のある時間帯は時期で変わる）。
  // 時間帯の規則は OS が持つので、プラットフォームが注入する（Win32 は SystemTimeToTzSpecificLocalTime）。既定は UTC（0）
  void setUtcOffsetFn(UtcOffsetFn fn) { utcOffsetFn_ = std::move(fn); }
  // 差が一定の時間帯（テスト用）
  void setUtcOffsetMinutes(int minutes) {
    utcOffsetFn_ = [minutes](std::int64_t) { return minutes; };
  }
  struct RemoteView {
    enum class Phase { None, Checking, Done, Failed };
    Phase phase = Phase::None;  // None = GitHub のリンクでない・まだ確認していない
    RemoteInfo info;            // Done のとき
    std::string checkedAt;      // 確認した日時 "YYYY-MM-DDTHH:MM"（Done・Failed）
    std::string error;          // Failed のとき（理由。URL は入れない）
  };
  RemoteView remoteViewOf(const std::string& target) const;
  std::size_t githubLinkCount(const std::string& issueId) const;
  bool remoteBusy() const;  // 確認中・順番待ちがある
  // 課題の GitHub のリンクを順に確認する（index を指定すればそのリンクだけ）。確認を頼んだリンクの数を返す（0 なら理由を通知）。
  // 確認が終わって、その課題の GitHub のリンクがすべて閉じていれば、完了の提案を通知する（アプリの設定で自動なら、そのまま完了にする）
  int checkRemote(const std::string& issueId, std::optional<std::size_t> index = std::nullopt);
  // 閉じた GitHub のリンクを根拠に課題を完了にする（Undo 1回）。提案の通知の「完了にする」と自動の完了が使う。
  // プロジェクトが切り替わった・課題がない・リンクの状態が変わって提案が成立しないときは、何も変えず理由を通知して false
  bool completeFromRemote(const std::string& project, const std::string& issueId, bool automatic);

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
  // 読み取り専用のときに編集系の入口が呼ぶ。読み取り専用なら理由を通知して false（announce が false なら通知しない）
  bool canEdit(bool announce = true);
  // フレームの終わりに呼ぶ。描画中に差し替えたプロジェクト・計算結果をここで捨てる
  // （それまでは古いものを残し、描画中の参照が壊れないようにする）
  void releaseFrame();

 private:
  void openSession(const std::string& id, bool keepHistory);
  void applyAutoBaseDate();  // アプリの設定「開いたら基準日を今日に」（利用者が開いたときだけ）
  void scheduleSave();
  void absorbNotices();
  // ---- ロック ----
  std::string lockPath(const std::string& id) const;  // 平置き・まだ書いていないときは空
  LockWho lockWho() const;
  IRandom& lockRandom();
  void enterLock();                                    // currentId_ のロックを取る（読み取り専用になることもある）
  void leaveLock();                                    // いまのロックを外す（自分のものなら、未保存を書いてから消す）
  void tryAcquire(const std::string& path);
  void setHolder(const LockInfo& info);
  void applyReadOnly();                                // Session と Workspace に読み取り専用を伝える
  void loseLock(const OwnershipResult& o);             // 保存の前の検査・ハートビートで失ったとき
  void tickLock();
  bool lockedElsewhere(const std::string& id);         // 他の人の新しいロックがあるプロジェクトか（JSON の上書きの取り込みで見る）
  Status lockGate();                                   // 書く前の検査。失ったら読み取り専用に切り替える。確かめられなければエラー
  Status flushGuarded();                               // ロックの検査と、フォルダ側の変更の検査を通してから書く
  void runSync(bool manual, bool announce);
  void toastLong(const std::string& text);

  std::int64_t now() const { return clock_.monotonicMs(); }

  IFileStore& fs_;
  const IClock& clock_;
  std::unique_ptr<Workspace> ws_;
  std::set<std::string> baseAdjusted_;  // この起動中に基準日を今日に合わせたプロジェクト
  std::string weeklyAutoOpenedId_;      // 開いた直後に週の頭のスナップショットを取ったプロジェクト（基準日を合わせたら取り直す。Issue #70）
  DataDirChoice dirInfo_;
  std::string exeDir_;
  std::string prefsPath_;
  Prefs prefs_;
  std::unique_ptr<Session> session_;
  std::vector<std::unique_ptr<Session>> retiredSessions_;
  std::string currentId_;
  std::uint64_t seenVersion_ = 0;
  std::uint64_t seenContentVersion_ = 0;
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

  // ---- 編集中のロック（Issue #62）----
  struct LockState {
    LockStatus status;
    std::string projectId;     // どのプロジェクトのロックか
    LockInfo mine;             // kind == Mine のとき自分のロック
    bool pending = false;      // まだフォルダがない新しいプロジェクト（最初の保存のあとで取る）
    std::int64_t lastCheck = 0;  // 最後にハートビート（または持ち主の確認）をした時刻（monotonic）
    bool beatWarned = false;
  };
  ILockStore* lockStore_ = nullptr;
  IRandom* lockRng_ = nullptr;
  std::unique_ptr<IRandom> ownRng_;
  std::string machine_;
  LockState lock_;

  // ---- GitHub の状態の確認（メモリ上のみ）----
  struct RemoteEntry {
    RemoteInfo info;
    std::string checkedAt;
    std::string error;
    bool failed = false;
  };
  struct RemoteJob {
    std::string key;  // GitHubRef::canonicalUrl()
    GitHubRef ref;
  };
  struct RemoteBatch {
    std::string project, issueId;
    std::vector<std::string> keys;
  };
  class RemoteWorker;
  void pumpRemote();                                // tick から: 終わった確認を拾い、次を始める
  void finishRemoteBatches();                       // 全部終わったバッチを評価する（完了の提案・失敗の通知）
  void dropRemoteQueue();                           // 順番待ちとバッチを捨てる（プロジェクトの切り替え）
  std::string issueLabel(const std::string& issueId) const;  // "#表示ID 『件名の先頭 30 文字』"
  RemoteStates remoteStates() const;
  std::shared_ptr<IRemoteStatus> remote_;
  bool remoteAsync_ = true;
  UtcOffsetFn utcOffsetFn_;
  std::map<std::string, RemoteEntry> remoteDone_;
  std::deque<RemoteJob> remoteQueue_;
  std::set<std::string> remoteActive_;  // 順番待ち・実行中の鍵
  std::vector<RemoteBatch> remoteBatches_;
  std::unique_ptr<RemoteWorker> worker_;
};

}  // namespace roadmap::app
