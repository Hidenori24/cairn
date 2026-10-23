#include "app_controller.hpp"

#include <algorithm>
#include <iterator>
#include <mutex>
#include <thread>
#include <unordered_set>

#include "roadmap/issue_name.hpp"
#include "roadmap/samples.hpp"
#include "roadmap/text.hpp"

namespace roadmap::app {

const char* viewKey(View v) {
  switch (v) {
    case View::Board: return "board";
    case View::List: return "list";
    case View::Goals: return "goals";
    case View::Stats: return "stats";
    case View::Today: return "today";
    default: return "gantt";
  }
}
View viewFromKey(const std::string& k) {
  if (k == "board") return View::Board;
  if (k == "list") return View::List;
  if (k == "goals") return View::Goals;
  if (k == "stats") return View::Stats;
  if (k == "today") return View::Today;
  return View::Gantt;
}

AppController::AppController(IFileStore& fs, const IClock& clock, std::string dataDir, std::string prefsPath)
    : fs_(fs), clock_(clock), ws_(std::make_unique<Workspace>(fs, clock, std::move(dataDir))), prefsPath_(std::move(prefsPath)) {
  installWriteGuard();
}

// GitHub の状態の確認を 1 件ずつ実行する。async = 別のスレッドで走らせ、tick が結果を拾う。そうでなければ start の中で実行する
class AppController::RemoteWorker {
 public:
  RemoteWorker(std::shared_ptr<IRemoteStatus> impl, bool async) : impl_(std::move(impl)), async_(async) {}
  ~RemoteWorker() {
    // 実行中のものはすぐ終わらせる（アプリの終了で待たされない）。中断は token で伝える（fetch に入る前の中断も取りこぼさない）。
    // cancel() は、token を見ずに眠っている窓口を起こす合図
    if (token_) token_->cancel();
    if (running_ && impl_) impl_->cancel();
    if (th_.joinable()) th_.join();
  }
  RemoteWorker(const RemoteWorker&) = delete;
  RemoteWorker& operator=(const RemoteWorker&) = delete;
  bool running() const { return running_; }
  void start(const RemoteJob& job) {
    running_ = true;
    key_ = job.key;
    sh_ = std::make_shared<Shared>();
    token_ = std::make_shared<CancelToken>();  // スレッドを起こす前に作って渡す
    if (!async_) {
      sh_->result.emplace(run(impl_, job.ref, *token_));
      sh_->done = true;
      return;
    }
    auto sh = sh_;
    auto impl = impl_;
    auto token = token_;
    const GitHubRef ref = job.ref;
    th_ = std::thread([sh, impl, token, ref] {
      auto r = run(impl, ref, *token);
      std::lock_guard<std::mutex> lk(sh->mu);
      sh->result.emplace(std::move(r));
      sh->done = true;
    });
  }
  // 終わっていれば結果を取り出して true（スレッドも片づける）
  bool poll(std::string& key, std::optional<Result<RemoteInfo>>& out) {
    if (!running_) return false;
    {
      std::lock_guard<std::mutex> lk(sh_->mu);
      if (!sh_->done) return false;
      out = std::move(sh_->result);
    }
    if (th_.joinable()) th_.join();
    key = key_;
    running_ = false;
    return true;
  }

 private:
  struct Shared {
    std::mutex mu;
    bool done = false;
    std::optional<Result<RemoteInfo>> result;
  };
  // スレッドの境界で例外を止める（窓口の実装が投げても、プロセスを落とさない）
  static Result<RemoteInfo> run(const std::shared_ptr<IRemoteStatus>& impl, const GitHubRef& ref, const CancelToken& token) {
    try {
      return impl->fetch(ref, token);
    } catch (...) {
      return Error{"確認中に予期しないエラーが起きました"};
    }
  }
  std::shared_ptr<IRemoteStatus> impl_;
  bool async_ = true;
  bool running_ = false;
  std::string key_;
  std::shared_ptr<Shared> sh_;
  std::shared_ptr<CancelToken> token_;
  std::thread th_;
};

AppController::~AppController() {
  // 終了時に、自分のロックなら消す（保存は呼び出し側が flushNow で済ませる）
  if (lockStore_ && lock_.status.kind == LockKind::Mine) {
    const std::string path = lockPath(lock_.projectId);
    if (!path.empty()) (void)releaseLock(*lockStore_, path, lock_.mine.token);
  }
}

// ---------------------------------------------------------------- 保存先（アプリの設定）
void AppController::setDataDirInfo(const DataDirChoice& c, std::string exeDir) {
  dirInfo_ = c;
  exeDir_ = std::move(exeDir);
  dataDirFallback = c.usedFallback;
}

bool AppController::dataDirLocked() const {
  return dirInfo_.source == DataDirSource::Arg || dirInfo_.source == DataDirSource::Env;
}

std::string AppController::defaultDir() const { return defaultDataDir(exeDir_); }

int AppController::countProjectsIn(const std::string& dir) {
  auto l = scanProjects(fs_, dir);
  return l ? static_cast<int>(l->size()) : 0;
}

Result<Workspace::MigrateReport> AppController::migrateLegacy() {
  afterEdit();
  if (const Status g = lockGate(); !g) {  // 未保存の編集を書く前に、ロックを確かめる
    toast("フォルダ構成へ移せませんでした（元のファイルはそのままです）: " + g.error());
    return Error{g.error()};
  }
  auto r = ws_->migrateLegacy();
  lastSync_ = now();
  if (r && r->moved > 0 && session_ && lockEnabled() && lock_.status.kind == LockKind::None) {
    // 開いているプロジェクトがフォルダ構成になったら、ここからロックを取る（平置きにはロックがない）
    lock_.projectId.clear();
    enterLock();
    applyReadOnly();
  }
  if (!r) {
    saveState_ = SaveState::Error;
    saveError_ = r.error();
    toast("フォルダ構成へ移せませんでした（元のファイルはそのままです）: " + r.error());
    return r;
  }
  saveState_ = SaveState::Saved;
  saveError_.clear();
  savedAt_ = clock_.hhmm();
  saveDueAt_ = -1;
  if (r->moved > 0) toast(std::to_string(r->moved) + "件をフォルダ構成に移しました（移す前の控え: 「" + std::string(kBackupDir) + "」の「移行前…」）");
  if (!r->failed.empty()) toast("移せなかったプロジェクト（平置きのまま残してあります）: " + text::join(r->failed, ", "));
  return r;
}

Status AppController::switchDataDir(const std::string& dirIn, bool copy) {
  const std::string dir = text::trim(dirIn);
  if (dir.empty()) return Error{"保存先が空です"};
  if (dataDirLocked()) return Error{"起動引数か環境変数で保存先が決まっているため、変えられません"};
  if (!fs_.probeWritable(dir)) return Error{"この場所には書き込めません: " + dir};
  // 今の場所に書き終えてから切り替える（書けなければ切り替えない）
  if (!flushNow()) return Error{"今の保存先に保存できないため、切り替えません: " + saveError_};
  std::vector<ProjectRecord> recs;
  if (copy)
    for (const auto& e : ws_->entries()) recs.push_back(e.rec);
  auto next = std::make_unique<Workspace>(fs_, clock_, dir);
  auto r = next->sync();
  if (!r) return Error{r.error()};
  int copied = 0, skipped = 0;
  struct ToCopy {
    std::string id;
    AttachmentMap attachments;
  };
  std::vector<ToCopy> withAttachments;
  for (auto& rec : recs) {
    if (next->find(rec.settings.id)) {
      ++skipped;  // 同じプロジェクトが新しい場所にもある → 場所の方を残す
      continue;
    }
    if (!rec.settings.attachments.empty()) withAttachments.push_back({rec.settings.id, rec.settings.attachments});
    next->add(std::move(rec), true);
    ++copied;
  }
  if (auto w = next->flush(); !w) return Error{"新しい場所に書き込めません: " + w.error()};  // ここで新しい場所のフォルダ名が決まる
  // 添付の本体もコピーする（設定にはメタ情報しか入っていない。本体を置いていかないと、切り替え後に開けなくなる）。
  // 1つでもコピーできなければ切り替えず、今の保存先を使い続ける（新しい場所に書いた添付は消す）
  std::vector<std::string> writtenAttachments;
  const auto undoAttachments = [&] {
    for (const auto& path : writtenAttachments) (void)fs_.remove(path);  // 消せなくても元の保存先は無傷
  };
  for (const auto& item : withAttachments) {
    const ProjectEntry* from = ws_->find(item.id);
    const ProjectEntry* to = next->find(item.id);
    if (!from || !to) continue;
    const std::string fromDir = ws_->projectDir(*from), toDir = next->projectDir(*to);
    if (fromDir.empty() || toDir.empty()) continue;
    for (const auto& [issueId, list] : item.attachments)
      for (const auto& a : list) {
        const std::string src = attachmentPath(fromDir, issueId, a);
        if (!fs_.stat(src)) continue;  // もともと本体がない（失われている）ものは、メタ情報だけ移る
        const std::string dst = attachmentPath(toDir, issueId, a);
        auto data = fs_.read(src);
        if (!data) {
          undoAttachments();
          return Error{"添付のファイルを読めないため、切り替えません（今の保存先を使い続けます）"};
        }
        if (auto m = fs_.makeDirs(paths::attachmentDir(toDir, issueId)); !m) {
          undoAttachments();
          return Error{"添付のフォルダを新しい場所に作れないため、切り替えません（今の保存先を使い続けます）: " + m.error()};
        }
        if (auto w = fs_.writeAtomic(dst, *data); !w) {
          undoAttachments();
          return Error{"添付のファイルを新しい場所にコピーできないため、切り替えません（今の保存先を使い続けます）: " + w.error()};
        }
        writtenAttachments.push_back(dst);
      }
  }

  leaveLock();  // 古い保存先のロックを外す（新しい場所で開き直すときに取る）
  ws_ = std::move(next);
  installWriteGuard();
  lastSync_ = now();
  const bool isDefault = !exeDir_.empty() && storableDataDir(dir, defaultDir()) == ".";
  dirInfo_ = {dir, false, isDefault ? DataDirSource::Default : DataDirSource::Prefs, false};
  dataDirFallback = false;
  prefs_.dataDir = isDefault ? "" : storableDataDir(dir, exeDir_);
  savePrefs();
  // 開いていたプロジェクトは捨てずにフレームの終わりまで残す（描画中の参照のため）
  if (session_) retiredSessions_.push_back(std::move(session_));
  currentId_.clear();
  sel_.clear();
  cursor_.clear();
  anchor_.clear();
  colFilter_ = ColumnFilter{};
  saveState_ = SaveState::Saved;
  saveError_.clear();
  saveDueAt_ = -1;
  firstRun_ = ws_->entries().empty();
  if (!firstRun_) {
    openSession(ws_->find(prefs_.currentProject) ? prefs_.currentProject : ws_->entries().front().id(), false);
    applyAutoBaseDate();
  }
  std::string msg = "保存先を切り替えました";
  if (copied) msg += "（" + std::to_string(copied) + "件をコピー";
  if (skipped) msg += std::string(copied ? "・" : "（") + std::to_string(skipped) + "件は新しい場所の方を残しました";
  if (copied || skipped) msg += "）";
  toast(msg);
  return Unit{};
}

// ---------------------------------------------------------------- 起動・周期処理
void AppController::start() {
  prefs_ = loadPrefs(fs_, prefsPath_);
  auto r = ws_->sync();
  lastSync_ = now();
  if (!r) {
    saveState_ = SaveState::Error;
    saveError_ = r.error();
  } else {
    const std::string msg = r->message();
    if (!r->bad.empty() && !msg.empty()) toast(msg);
    // 以前の平置きは自動では動かさない。移せることだけ知らせる
    if (ws_->legacyCount() > 0)
      toast("以前の保存形式（平置き）のプロジェクトが " + std::to_string(ws_->legacyCount()) +
            "件あります。「アプリの設定」の保存先から、プロジェクトごとのフォルダ構成に移せます");
  }
  if (ws_->entries().empty()) {
    firstRun_ = true;
    return;
  }
  const std::string want = ws_->find(prefs_.currentProject) ? prefs_.currentProject : ws_->entries().front().id();
  openSession(want, false);
  applyAutoBaseDate();
}

void AppController::applyAutoBaseDate() {
  if (!prefs_.autoBaseToday || !session_ || currentId_.empty() || readOnly()) return;
  if (!baseAdjusted_.insert(currentId_).second) return;  // 起動中に1回だけ（手で戻した日付は変えない）
  const std::string today = clock_.todayIso();
  const std::string prev = session_->settings().baseDate;
  if (prev == today) return;
  session_->setBaseDate(today);
  recordWeeklyAuto(WeeklyAutoTrigger::Rebase);  // 開いたときに取ったものは古い基準日の日程なので、合わせたあとの日程で取り直す
  afterEdit();
  toast("基準日を今日（" + today + "）にしました。前の日付は " + prev + "（アプリの設定で切り替えられます）");
}

void AppController::absorbNotices() {
  if (!session_) return;
  for (auto& n : session_->takeNotices()) toast(n.text, n.action);
}

// ---- 添付ファイル ----
std::string AppController::currentProjectDir() const {
  const ProjectEntry* e = ws_->find(currentId_);
  return e ? ws_->projectDir(*e) : "";
}
bool AppController::canAttach() const {
  if (!session_) return false;
  const ProjectEntry* e = ws_->find(currentId_);
  return e && e->layout == Layout::Folder;
}
std::string AppController::attachDisabledReason() const {
  if (!session_) return "プロジェクトを開いてから添付してください";
  if (!canAttach())
    return "このプロジェクトは以前の保存形式（1つのCSVを置く形）なので、添付を置く場所がありません。"
           "アプリの設定でフォルダ構成へ移してください";
  return "";
}
const std::vector<Attachment>& AppController::attachmentsOf(const std::string& issueId) const {
  static const std::vector<Attachment> none;
  if (!session_) return none;
  const auto& m = session_->attachments();
  const auto it = m.find(issueId);
  return it == m.end() ? none : it->second;
}
std::size_t AppController::attachmentCount(const std::string& issueId) const { return attachmentsOf(issueId).size(); }

AttachOutcome AppController::attachFiles(const std::string& issueId, const std::vector<std::string>& files) {
  AttachOutcome out;
  if (!canEdit()) return out;
  if (!canAttach()) {
    toast(attachDisabledReason());
    return out;
  }
  if (!verifyLockBeforeWrite()) return out;  // 本体を書く前に、引き継がれていないか確かめる
  if (currentProjectDir().empty() && !flushNow()) {  // まだ書いていないプロジェクトは、先にフォルダを作る
    toast("保存できないため、添付を置けません");
    return out;
  }
  const std::string dir = currentProjectDir();
  if (dir.empty()) return out;
  out = roadmap::attachFiles(fs_, clock_, dir, session_->attachments(), issueId, files, prefs_.me);
  if (!out.added.empty()) {
    session_->touchAttachments();
    flushNow();
  }
  std::string msg;
  if (!out.added.empty()) msg = std::to_string(out.added.size()) + " 件を添付しました";
  if (!out.rejected.empty()) {
    const AttachRejected& r = out.rejected.front();
    if (!msg.empty()) msg += "。";
    msg += std::to_string(out.rejected.size()) + " 件は付けられませんでした（" + (r.name.empty() ? "" : r.name + ": ") + r.reason + "）";
  }
  if (!msg.empty()) toast(msg);
  return out;
}

bool AppController::attachData(const std::string& issueId, const std::string& name, const std::string& data) {
  if (!canEdit()) return false;
  if (!canAttach()) {
    toast(attachDisabledReason());
    return false;
  }
  if (!verifyLockBeforeWrite()) return false;
  if (currentProjectDir().empty() && !flushNow()) return false;
  auto r = roadmap::attachData(fs_, clock_, currentProjectDir(), session_->attachments(), issueId, name, data, prefs_.me);
  if (!r) {
    toast("添付できませんでした（" + r.error() + "）");
    return false;
  }
  session_->touchAttachments();
  flushNow();
  toast("1 件を添付しました");
  return true;
}

bool AppController::attachClipboardImage(const std::string& issueId, const std::string& bmp) {
  if (!canEdit()) return false;
  if (!canAttach()) {
    toast(attachDisabledReason());
    return false;
  }
  if (!session_->issue(issueId)) {
    toast("添付する課題がありません");
    return false;
  }
  // BMP のファイルとして最低限の形（"BM" + ヘッダ）。プラットフォームが作った値だが、壊れたものは付けない
  if (bmp.size() < 26 || bmp.compare(0, 2, "BM") != 0) {
    toast("クリップボードの画像を読めませんでした");
    return false;
  }
  return attachData(issueId, "貼り付け-" + clock_.stamp() + ".bmp", bmp);
}

bool AppController::moveAttachment(const std::string& fromIssue, std::size_t index, const std::string& toIssue) {
  if (!canEdit()) return false;
  if (!canAttach()) {
    toast(attachDisabledReason());
    return false;
  }
  if (!session_->issue(toIssue)) {
    toast("移す先の課題がありません");
    return false;
  }
  if (!verifyLockBeforeWrite()) return false;
  auto r = roadmap::moveAttachment(fs_, currentProjectDir(), session_->attachments(), fromIssue, index, toIssue);
  if (!r) {
    toast("添付を移せませんでした（" + r.error() + "）");
    return false;
  }
  session_->touchAttachments();
  flushNow();
  toast("添付を別の課題へ移しました");
  return true;
}

bool AppController::copyAttachment(const std::string& fromIssue, std::size_t index, const std::string& toIssue) {
  if (!canEdit()) return false;
  if (!canAttach()) {
    toast(attachDisabledReason());
    return false;
  }
  if (!session_->issue(toIssue)) {
    toast("コピー先の課題がありません");
    return false;
  }
  if (!verifyLockBeforeWrite()) return false;
  auto r = roadmap::copyAttachment(fs_, clock_, currentProjectDir(), session_->attachments(), fromIssue, index, toIssue, prefs_.me);
  if (!r) {
    toast("添付をコピーできませんでした（" + r.error() + "）");
    return false;
  }
  session_->touchAttachments();
  flushNow();
  toast("添付を別の課題へコピーしました");
  return true;
}

bool AppController::reorderAttachment(const std::string& issueId, std::size_t from, std::size_t to) {
  if (!session_ || !canEdit()) return false;
  auto s = roadmap::reorderAttachment(session_->attachments(), issueId, from, to);
  if (!s) return false;
  if (from == to) return true;
  session_->touchAttachments();
  flushNow();
  return true;
}

bool AppController::removeAttachment(const std::string& issueId, std::size_t index) {
  if (!session_ || !canEdit() || !verifyLockBeforeWrite()) return false;
  auto s = roadmap::removeAttachment(fs_, currentProjectDir(), session_->attachments(), issueId, index);
  if (!s) {
    toast("添付を外せませんでした（" + s.error() + "）");
    return false;
  }
  session_->touchAttachments();
  flushNow();
  toast("添付を外しました（ファイルは「" + std::string(kAttachDeleted) + "」フォルダに移してあります）");
  return true;
}

std::string AppController::attachmentFile(const std::string& issueId, std::size_t index) const {
  const auto& list = attachmentsOf(issueId);
  const std::string dir = currentProjectDir();
  if (dir.empty() || index >= list.size()) return "";
  return attachmentPath(dir, issueId, list[index]);
}

bool AppController::attachmentExists(const std::string& issueId, std::size_t index) const {
  const std::string p = attachmentFile(issueId, index);
  return !p.empty() && fs_.stat(p).has_value();
}

Result<std::string> AppController::attachmentBytes(const std::string& issueId, std::size_t index) const {
  const std::string p = attachmentFile(issueId, index);
  if (p.empty()) return Error{"添付がありません"};
  const auto st = fs_.stat(p);
  if (!st) return Error{"ファイルが見つかりません"};
  if (st->size > kMaxAttachFileBytes) return Error{"大きすぎて読めません"};
  return fs_.read(p);
}

const std::vector<Link>& AppController::linksOf(const std::string& issueId) const {
  static const std::vector<Link> none;
  if (!session_) return none;
  const auto& m = session_->links();
  const auto it = m.find(issueId);
  return it == m.end() ? none : it->second;
}
std::size_t AppController::linkCountOf(const std::string& issueId) const { return linksOf(issueId).size(); }

bool AppController::addLink(const std::string& issueId, const std::string& title, const std::string& target, std::string* reason) {
  const auto fail = [&](const std::string& why) {
    if (reason) *reason = why;
    return false;
  };
  if (!session_) return fail("プロジェクトを開いてからリンクを付けてください");
  if (readOnly()) return fail("読み取り専用で開いているため、リンクを付けられません");
  if (!session_->issue(issueId)) return fail("リンクを付ける課題がありません");
  auto r = roadmap::linkAdd(session_->links(), clock_, issueId, title, target);
  if (!r) return fail(r.error());
  session_->touchLinks();
  afterEdit();
  return true;
}

LinkPasteOutcome AppController::addLinksFromText(const std::string& issueId, const std::string& text) {
  LinkPasteOutcome out;
  if (!canEdit()) return out;
  if (!session_ || !session_->issue(issueId)) {
    toast("リンクを付ける課題がありません");
    return out;
  }
  out = roadmap::linkAddLines(session_->links(), clock_, issueId, text);
  if (!out.added.empty()) {
    session_->touchLinks();
    afterEdit();
  }
  // 行の中身は通知に出さない（件数と、最初に断った理由だけ）
  std::string msg;
  if (!out.added.empty()) msg = std::to_string(out.added.size()) + " 件のリンクを追加しました";
  const std::size_t refused = out.rejected.size() + out.skipped;
  if (refused > 0) {
    if (!msg.empty()) msg += "。";
    msg += std::to_string(refused) + " 行は追加できませんでした";
    if (!out.rejected.empty()) msg += "（" + std::to_string(out.rejected.front().line) + " 行目: " + out.rejected.front().reason + "）";
    else msg += "（一度に読むのは " + std::to_string(kMaxPasteLinkLines) + " 行までです）";
  }
  if (msg.empty()) msg = "追加できる行がありませんでした（1 行に 1 つ、URL かパスを入れてください）";
  toast(msg);
  return out;
}

bool AppController::editLink(const std::string& issueId, std::size_t index, const std::string& title, const std::string& target,
                             std::string* reason) {
  if (readOnly()) {
    if (reason) *reason = "読み取り専用で開いているため、リンクを直せません";
    return false;
  }
  if (!session_ || !session_->issue(issueId)) {
    if (reason) *reason = "リンクを直す課題がありません";
    return false;
  }
  auto s = roadmap::linkEdit(session_->links(), issueId, index, title, target);
  if (!s) {
    if (reason) *reason = s.error();
    return false;
  }
  session_->touchLinks();
  afterEdit();
  return true;
}

bool AppController::removeLink(const std::string& issueId, std::size_t index) {
  if (!session_ || !canEdit()) return false;
  auto s = roadmap::linkRemove(session_->links(), issueId, index);
  if (!s) return false;
  session_->touchLinks();
  afterEdit();
  toast("リンクを外しました");
  return true;
}

bool AppController::reorderLink(const std::string& issueId, std::size_t from, std::size_t to) {
  if (!session_ || !canEdit()) return false;
  auto s = roadmap::linkReorder(session_->links(), issueId, from, to);
  if (!s) return false;
  if (from == to) return true;
  session_->touchLinks();
  afterEdit();
  return true;
}

bool AppController::moveLink(const std::string& fromIssue, std::size_t index, const std::string& toIssue) {
  if (!canEdit()) return false;
  if (!session_ || !session_->issue(toIssue)) {
    toast("移す先の課題がありません");
    return false;
  }
  auto r = roadmap::linkMove(session_->links(), fromIssue, index, toIssue);
  if (!r) {
    toast("リンクを移せませんでした（" + r.error() + "）");
    return false;
  }
  session_->touchLinks();
  afterEdit();
  toast("リンクを別の課題へ移しました");
  return true;
}

bool AppController::copyLink(const std::string& fromIssue, std::size_t index, const std::string& toIssue) {
  if (!canEdit()) return false;
  if (!session_ || !session_->issue(toIssue)) {
    toast("コピー先の課題がありません");
    return false;
  }
  auto r = roadmap::linkCopy(session_->links(), clock_, fromIssue, index, toIssue);
  if (!r) {
    toast("リンクをコピーできませんでした（" + r.error() + "）");
    return false;
  }
  session_->touchLinks();
  afterEdit();
  toast("リンクを別の課題へコピーしました");
  return true;
}

void AppController::afterEdit() {
  if (!session_) return;
  absorbNotices();
  if (session_->version() != seenVersion_) {
    seenVersion_ = session_->version();
    // 読み取り専用のあいだは、表示の状態の変更もメモリ上だけ。Workspace へ渡さない（ファイルへ書かない）
    if (readOnly()) return;
    if (session_->contentVersion() != seenContentVersion_) {
      seenContentVersion_ = session_->contentVersion();
      ws_->update(currentId_, recordOf(session_->data()));
    } else {
      // 課題を編集していない間の変更（履歴の記録・表示状態など）は設定だけを書く。元の CSV を再エクスポートして
      // 形を変えたり、取り込み時に読み飛ばした行を消したりしない
      ws_->updateSettings(currentId_, session_->data().settings);
    }
    pruneSelection();
    if (ws_->hasDirty()) scheduleSave();
  }
}

void AppController::scheduleSave() {
  saveDueAt_ = now() + 400;
  if (saveState_ != SaveState::Error) saveState_ = SaveState::Saving;
}

void AppController::tick() {
  afterEdit();
  pumpRemote();
  const std::int64_t t = now();
  if (saveDueAt_ >= 0 && t >= saveDueAt_) {
    auto s = flushGuarded();
    if (s) {
      saveState_ = SaveState::Saved;
      saveError_.clear();
      savedAt_ = clock_.hhmm();
      retryStep_ = 0;
      saveDueAt_ = -1;
    } else {
      saveState_ = SaveState::Error;
      saveError_ = s.error();
      static const int kRetry[] = {2000, 5000, 10000};
      saveDueAt_ = t + kRetry[std::min(retryStep_, 2)];
      ++retryStep_;
    }
  }
  tickLock();
  toasts_.erase(std::remove_if(toasts_.begin(), toasts_.end(), [&](const Toast& x) { return x.expiresAt <= t; }),
                toasts_.end());
}

void AppController::retrySave() {
  retryStep_ = 0;
  saveDueAt_ = now();
  tick();
}

bool AppController::flushNow() {
  afterEdit();
  auto s = flushGuarded();
  if (!s) {
    saveState_ = SaveState::Error;
    saveError_ = s.error();
    return false;
  }
  saveState_ = SaveState::Saved;
  savedAt_ = clock_.hhmm();
  saveDueAt_ = -1;
  tickLock();  // 最初の保存でフォルダができたら、ここからロックを取る
  return true;
}

bool AppController::hasUnsaved() const {
  if (session_ && session_->version() != seenVersion_) return true;
  return ws_->hasDirty();
}

void AppController::onActivated() {
  if (now() - lastSync_ > 3000) syncFolder(false);
}

void AppController::syncFolder(bool manual) { runSync(manual, true); }

void AppController::runSync(bool manual, bool announce) {
  afterEdit();
  if (const Status g = lockGate(); !g) {  // 未保存の編集を書く前に、ロックを確かめる
    saveState_ = SaveState::Error;
    saveError_ = g.error();
    return;
  }
  guardLost_.reset();
  auto r = ws_->sync();
  lastSync_ = now();
  if (!r) {
    if (handleGuardLoss()) return;  // 書く直前の検査で引き継ぎが分かった: 読み取り専用に切り替えた
    saveState_ = SaveState::Error;
    saveError_ = r.error();
    return;
  }
  if (r->wrote) {
    saveState_ = SaveState::Saved;
    saveError_.clear();
    savedAt_ = clock_.hhmm();
    saveDueAt_ = -1;
  }
  if (std::find(r->reloadedIds.begin(), r->reloadedIds.end(), currentId_) != r->reloadedIds.end())
    openSession(currentId_, false);
  if (!session_ && !ws_->entries().empty()) {
    firstRun_ = false;
    openSession(!r->firstAddedId.empty() ? r->firstAddedId : ws_->entries().front().id(), false);
  }
  const std::string msg = r->message(manual);
  if (announce && !msg.empty()) toast(msg);
}

// ---------------------------------------------------------------- プロジェクト
std::vector<AppController::ProjectItem> AppController::projects() const {
  std::vector<ProjectItem> v;
  for (const auto& e : ws_->entries()) v.push_back({e.id(), e.rec.settings.name, e.rec.settings.key});
  return v;
}

void AppController::openSession(const std::string& id, bool keepHistory) {
  (void)keepHistory;
  const ProjectEntry* e = ws_->find(id);
  if (!e) return;
  auto h = hydrate(e->rec);
  if (!h) {
    toast("このプロジェクトを開けません: " + h.error());
    return;
  }
  if (currentId_ != id) dropRemoteQueue();  // 別のプロジェクトの順番待ちの確認は捨てる（結果は表示のために残す）
  if (session_) retiredSessions_.push_back(std::move(session_));
  session_ = std::make_unique<Session>(std::move(h->data), &clock_, std::move(h->warns));
  session_->setInherit(prefs_.inherit);
  if (!session_->error().empty()) toast("このプロジェクトを計算できません: " + session_->error());
  absorbNotices();
  seenVersion_ = session_->version();
  seenContentVersion_ = session_->contentVersion();
  if (currentId_ != id) colFilter_ = ColumnFilter{};  // 列の値はプロジェクトごとに違う
  currentId_ = id;
  if (lock_.projectId != id) {  // 別のプロジェクトを開いた: 前のロックを外し、これのロックを取る（読み直しでは何もしない）
    leaveLock();
    enterLock();
  }
  applyReadOnly();
  firstRun_ = false;
  sel_.clear();
  cursor_.clear();
  anchor_.clear();
  if (prefs_.currentProject != id) {
    prefs_.currentProject = id;
    savePrefs();
  }
  recordHistory(HistoryTrigger::Open);  // 開いたとき、その週の点がなければ記録する（Issue #64）
  recordWeeklyAuto(WeeklyAutoTrigger::Open);  // 開いたとき、その週の頭のスナップショットがなければ取る（Issue #70）
}

bool AppController::openProject(const std::string& id) {
  if (!ws_->find(id)) return false;
  afterEdit();
  openSession(id, false);
  applyAutoBaseDate();
  return session_ && currentId_ == id;
}

std::string AppController::createProject(SampleKind kind, const std::string& nameIn, const std::string& key) {
  afterEdit();
  std::string csvText = csv::emptyCsv(), base = clock_.todayIso();
  if (kind == SampleKind::Hierarchy) {
    csvText = std::string(samples::hierarchyCsv());
    base = samples::kSampleBase;
  } else if (kind == SampleKind::EmbeddedSw) {
    csvText = std::string(samples::embeddedSwCsv());  // 雛形は今日を基準日にする
  }
  const std::string name = nameIn.empty() ? "新しいプロジェクト" : nameIn;
  const std::string id = ws_->add(newRecord(name, text::toUpper(key), csvText, base, clock_), true);
  openSession(id, false);
  scheduleSave();
  toast("「" + name + "」を作りました");
  return id;
}

void AppController::editProject(const std::string& name, const std::string& key) {
  if (!session_ || !canEdit()) return;
  session_->setNameKey(name, key);
  afterEdit();
}

void AppController::duplicateProject() {
  if (!session_) return;
  afterEdit();
  ProjectRecord rec = recordOf(session_->data());
  rec.settings.id.clear();
  rec.settings.name += " のコピー";
  rec.settings.attachments.clear();  // 添付の本体は複製しない（メタ情報だけ残ると、無いファイルを指してしまう）
  const std::string id = ws_->add(std::move(rec), true);
  openSession(id, false);
  scheduleSave();
  toast("複製しました");
}

bool AppController::deleteCurrentProject() {
  if (!session_ || !canEdit()) return false;
  const ProjectEntry* e = ws_->find(currentId_);
  const std::string stem = e ? e->stem : "";
  const bool folder = !e || e->layout == Layout::Folder;
  if (!verifyLockBeforeWrite()) return false;  // 引き継いだ人のフォルダを動かさない
  // 自分のロックは、フォルダごと動かす前に外す（_削除済み に持ち込まない）。動かせなければ取り直す
  const bool wasMine = lock_.status.kind == LockKind::Mine;
  if (wasMine) {
    const std::string path = lockPath(currentId_);
    if (!path.empty()) (void)releaseLock(*lockStore_, path, lock_.mine.token);
  }
  auto s = ws_->remove(currentId_);
  if (!s) {
    if (wasMine) {
      const std::string path = lockPath(currentId_);
      if (!path.empty()) tryAcquire(path);
      applyReadOnly();
    }
    toast("フォルダのファイルを移せませんでした: " + s.error());
    return false;
  }
  lock_ = LockState{};
  if (!stem.empty())
    toast(folder ? "フォルダ「" + stem + "」を「_削除済み」に移しました" : "フォルダの「" + stem + ".csv」を「_削除済み」に移しました");
  retiredSessions_.push_back(std::move(session_));
  currentId_.clear();
  if (ws_->entries().empty()) {
    firstRun_ = true;
    return true;
  }
  openSession(ws_->entries().front().id(), false);
  return true;
}

ImportOutcome AppController::importText(const std::string& textIn, const std::string& name, ImportMode mode) {
  ImportOutcome out;
  const std::string t = text::trimStart(textIn);
  if (!t.empty() && t[0] == '{') {
    out.isJson = true;
    auto b = parseBackup(t, clock_);
    if (!b) {
      out.error = b.error();
      return out;
    }
    for (const auto& p : b->projects)
      if (!p.settings.id.empty() && ws_->find(p.settings.id)) out.conflictNames.push_back(p.settings.name);
    pendingJson_ = std::move(b).value();
    if (out.conflictNames.empty()) confirmJsonImport(false);
    out.ok = true;
    return out;
  }
  if (t.size() > kMaxCsvBytes) {
    out.error = "ファイルが大きすぎます（50MB超）";
    return out;
  }
  auto imp = csv::importCsv(textIn);
  if (!imp) {
    out.error = imp.error();
    return out;
  }
  if (auto tr = buildTree(imp->issues, imp->meta); !tr) {
    out.error = tr.error();
    return out;
  }
  afterEdit();
  if (mode == ImportMode::Replace && session_) {
    if (!canEdit()) {
      out.error = "読み取り専用で開いているため、置き換えられません";
      return out;
    }
    session_->replaceIssues(imp->issues, imp->meta, imp->warns);
    afterEdit();
  } else {
    const std::size_t n = imp->issues.size();
    const std::string id =
        ws_->add(newRecord(name.empty() ? "取り込んだCSV" : name, "", textIn, clock_.todayIso(), clock_), true);
    openSession(id, false);
    scheduleSave();
    toast(std::to_string(n) + "件を読み込みました");
  }
  out.ok = true;
  return out;
}

bool AppController::confirmJsonImport(bool overwrite) {
  if (!pendingJson_) return false;
  afterEdit();
  ParsedBackup b = std::move(*pendingJson_);
  pendingJson_.reset();
  std::string first;
  for (auto& p : b.projects) {
    bool same = !p.settings.id.empty() && ws_->find(p.settings.id);
    std::string id;
    bool overwritten = false;
    if (same && overwrite) {
      const std::string tid = p.settings.id;
      if (tid == currentId_) {
        // 開いているプロジェクト: 読み取り専用なら上書きしない。編集できるなら、いつもの保存（ロックの検査つき）で書く
        if (!readOnly()) {
          ws_->update(tid, p);
          ws_->find(tid)->dirty = true;
          id = tid;
          overwritten = true;
        }
      } else if (!lockEnabled() || lockPath(tid).empty()) {
        ws_->update(tid, p);
        ws_->find(tid)->dirty = true;
        id = tid;
        overwritten = true;
      } else {
        // 開いていないプロジェクト: そのロックを取って、書き終えるまで持つ（取れなければ上書きしない。確かめられないときも同じ）
        const std::string path = lockPath(tid);
        const AcquireResult acq = acquireLock(*lockStore_, clock_, lockRandom(), path, lockWho());
        if (acq.kind == AcquireKind::Acquired) {
          ws_->update(tid, p);
          ws_->find(tid)->dirty = true;
          const Status w = ws_->flush();  // 取っている間に書く
          (void)releaseLock(*lockStore_, path, acq.lock.token);
          if (!w) toast("書き込めませんでした（あとで保存し直します）: " + w.error());
          id = tid;
          overwritten = true;
        }
      }
      if (!overwritten) {
        // 他の人が編集中（またはロックを確かめられない）のプロジェクトは上書きしない（別のプロジェクトとして読み込む）
        p.settings.name += "（読み込み）";
        toast("「" + p.settings.name + "」と同じプロジェクトを他の人が編集中か、ロックを確かめられないため、上書きせず別のプロジェクトとして読み込みました");
      }
    }
    if (!overwritten) {
      p.settings.id.clear();
      id = ws_->add(std::move(p), true);
    }
    if (first.empty()) first = id;
  }
  std::unordered_set<std::string> have;
  for (const auto& t : prefs_.templates) have.insert(t.id);
  int added = 0;
  for (auto& t : b.templates)
    if (!have.count(t.id)) {
      prefs_.templates.push_back(std::move(t));
      ++added;
    }
  if (added) savePrefs();
  if (!first.empty()) openSession(first, false);
  scheduleSave();
  toast(std::to_string(b.projects.size()) + "件のプロジェクトを読み込みました" +
        (added ? "（テンプレート " + std::to_string(added) + "件を含む）" : ""));
  return true;
}

std::string AppController::exportText(const std::string& kind) const {
  if (kind == "backup") {
    std::vector<ProjectRecord> recs;
    for (const auto& e : ws_->entries())
      recs.push_back(session_ && e.id() == currentId_ ? recordOf(session_->data()) : e.rec);
    return exportShareJson(recs, prefs_.templates, clock_);
  }
  if (!session_) return "";
  if (kind == "share") return exportShareJson({recordOf(session_->data())}, prefs_.templates, clock_);
  return session_->exportText(kind == "jira" ? ExportKind::Jira : ExportKind::App);
}

std::string AppController::exportFileName(const std::string& kind) const {
  if (kind == "backup") return "roadmap_backup_" + clock_.stamp() + ".json";
  const std::string name = session_ ? Workspace::safeStem(session_->settings().name) : "project";
  if (kind == "share") return name + ".json";
  if (kind == "jira") return name + "_jira.csv";
  return name + ".csv";
}

// ---------------------------------------------------------------- 報告（HTML）の書き出し
namespace {
// パスの親のフォルダ（区切りは / か \\）。ファイル名だけなら空。ルート直下はルート自身
std::string parentFolder(const std::string& path) {
  const std::size_t p = path.find_last_of("/\\");
  if (p == std::string::npos) return {};
  if (p == 0) return path.substr(0, 1);
  if (path[p - 1] == ':') return path.substr(0, p + 1);  // "C:\\x.html" → "C:\\"
  return path.substr(0, p);
}
}  // namespace

std::string AppController::reportFileName() const {
  return roadmap::reportFileName(session_ ? session_->settings().name : std::string(), clock_.todayIso());
}

Result<std::string> AppController::buildReport(const ExportOptions& opt) const {
  const Session* s = session_.get();
  if (!s || !s->tree() || !s->rows()) return Error{"プロジェクトが開かれていません"};
  ExportInput in(*s->tree(), s->viewIssues(), *s->rows(), s->calendar());
  in.baseline = s->baselineCompare();
  in.evmBaseline = evmPickBaseline(s->baselines(), std::string());  // 表示中のベースライン、なければ一番新しいもの（Issue #65）
  in.evm = s->evmSettings();
  in.history = &s->history();
  in.base = s->baseDay();
  in.meta = &s->meta();
  in.projectName = s->settings().name;
  in.resource = s->settings().resource;
  in.clock = &clock_;
  if (opt.scope == ReportScope::Current) {
    in.currentRows = visibleRows();
    in.dimmed = dimmedSet();
    in.dimAlpha = static_cast<float>(prefs_.dimPercent) / 100.f;
    if (filterActive()) in.filterLabel = filterLabel();
  } else if (opt.scope == ReportScope::Subtree) {
    if (sel_.size() != 1) return Error{"配下を書き出すには、課題を1つだけ選んでください"};
    in.subtreeRoot = s->tree()->find(sel_[0]);
    if (in.subtreeRoot < 0) return Error{"選んだ課題が見つかりません"};
  }
  return exportHtml(in, opt);
}

Status AppController::exportReport(const std::string& path, const ExportOptions& opt) {
  const auto html = buildReport(opt);
  if (!html.ok()) {
    toast("書き出せません: " + html.error());
    return Error{html.error()};
  }
  const Status st = writeReport(fs_, path, html.value());
  if (!st.ok()) {
    toast("書き出せません: " + st.error());
    return st;
  }
  const std::string folder = parentFolder(path);
  const std::size_t slash = path.find_last_of("/\\");
  const std::string file = slash == std::string::npos ? path : path.substr(slash + 1);
  toast("書き出しました: " + file, folder.empty() ? NoticeAction::None : NoticeAction::OpenFolder, folder);
  return okStatus();
}

// ---------------------------------------------------------------- 週次レポート
std::string AppController::weeklyNotes() const { return session_ ? session_->weekly().notes : std::string(); }

void AppController::setWeeklyNotes(const std::string& notes) {
  if (!session_ || readOnly()) return;  // 読み取り専用のあいだは覚えない（書き出しは使える）
  session_->setWeeklyNotes(notes);
  afterEdit();
}

Result<WeeklyReport> AppController::buildWeekly(const WeeklyOptions& opt) const {
  const Session* s = session_.get();
  if (!s || !s->tree() || !s->rows()) return Error{"プロジェクトが開かれていません"};
  const Tree& T = *s->tree();
  const std::vector<Issue>& issues = s->viewIssues();
  const std::vector<ScheduleRow>& rows = *s->rows();
  const Day base = s->baseDay();
  const ProjectStats stats = computeStats(T, issues, rows, base);
  const LoadReport load = computeLoad(T, issues, rows, s->calendar(), base, s->settings().resource);
  WeeklyInput in(T, issues, rows, s->calendar(), stats, load);
  in.base = base;
  in.projectName = s->settings().name;
  in.memo = &s->weekly();  // 比べる元は期間に合わせて選ぶ（手動と自動。weeklyChooseCompare）
  in.autos = &s->weeklyAuto();
  in.baseline = s->baselineCompare();
  in.evmBaseline = evmPickBaseline(s->baselines(), std::string());
  in.evm = s->evmSettings();
  in.meta = &s->meta();  // 作成日・更新日の列（今週触った課題。Issue #71）
  return buildWeeklyReport(in, opt);
}

Result<std::string> AppController::renderWeekly(const WeeklyOptions& opt, bool html) const {
  const auto r = buildWeekly(opt);
  if (!r.ok()) return Error{r.error()};
  return html ? weeklyHtml(r.value(), &clock_) : weeklyMarkdown(r.value());
}

std::string AppController::weeklyFileName(const WeeklyOptions& opt, bool html) const {
  const Day base = session_ ? session_->baseDay() : clock_.today();
  return roadmap::weeklyFileName(session_ ? session_->settings().name : std::string(),
                                 weeklyRange(base, opt.period, opt.from, opt.to).from, html);
}

void AppController::recordWeekly(const WeeklyOptions& opt) {
  Session* s = session_.get();
  if (!s || !s->tree() || !s->rows() || readOnly()) return;  // 読み取り専用のあいだは、所感もスナップショットも記録しない
  const Tree& T = *s->tree();
  const std::vector<Issue>& issues = s->viewIssues();
  const std::vector<ScheduleRow>& rows = *s->rows();
  const Day base = s->baseDay();
  const ProjectStats stats = computeStats(T, issues, rows, base);
  const LoadReport load = computeLoad(T, issues, rows, s->calendar(), base, s->settings().resource);
  WeeklyInput in(T, issues, rows, s->calendar(), stats, load);
  in.base = base;
  WeeklySnapshot snap = makeWeeklySnapshot(in);
  s->setWeeklyNotes(opt.notes);
  s->putWeeklySnapshot(std::move(snap));
  recordHistory(HistoryTrigger::Weekly);  // 週報を出した週の件数も覚える（Issue #64）
  afterEdit();
}

Status AppController::exportWeekly(const std::string& path, const WeeklyOptions& opt, bool html) {
  const auto text = renderWeekly(opt, html);
  if (!text.ok()) {
    toast("書き出せません: " + text.error());
    return Error{text.error()};
  }
  const Status st = writeWeekly(fs_, path, text.value(), html);
  if (!st.ok()) {
    toast("書き出せません: " + st.error());
    return st;
  }
  recordWeekly(opt);
  const std::string folder = parentFolder(path);
  const std::size_t slash = path.find_last_of("/\\");
  const std::string file = slash == std::string::npos ? path : path.substr(slash + 1);
  toast("書き出しました: " + file, folder.empty() ? NoticeAction::None : NoticeAction::OpenFolder, folder);
  return okStatus();
}

void AppController::releaseFrame() {
  retiredSessions_.clear();
  if (session_) session_->releaseFrame();
}

// ---------------------------------------------------------------- 状況の件数の履歴
// 履歴を書く入口はここだけ。読み取り専用（他の人のロック。Issue #62）のときは何もしない
void AppController::recordHistory(HistoryTrigger why) {
  Session* s = session_.get();
  if (!s) return;
  if (readOnly()) {
    if (why == HistoryTrigger::Manual) canEdit();  // 手で押したときだけ理由を出す（開いたときや週報の書き出しでは黙る）
    return;
  }
  const bool manual = why == HistoryTrigger::Manual;
  const auto r = s->recordHistory(why == HistoryTrigger::Open ? HistoryMode::IfMissing : HistoryMode::Replace);
  if (!r.ok()) {
    if (manual) toast("記録できません: " + r.error());
    return;
  }
  if (r.value() == HistoryResult::Recorded || r.value() == HistoryResult::Replaced) afterEdit();
  if (!manual) return;
  switch (r.value()) {
    case HistoryResult::Recorded: toast("今週の件数を記録しました"); break;
    case HistoryResult::Replaced: toast("今週の記録を更新しました"); break;
    case HistoryResult::Unchanged: toast("今週の記録から変わりはありません"); break;
    default: break;
  }
}

// ---------------------------------------------------------------- 週の頭のスナップショット
// 自動のスナップショットを取る入口はここだけ。読み取り専用（他の人のロック。Issue #62）のときは何もしない。
// 保存は設定だけを書く（元の CSV は書き換えない。afterEdit が選ぶ）
void AppController::recordWeeklyAuto(WeeklyAutoTrigger why) {
  Session* s = session_.get();
  if (!s || readOnly()) return;
  if (why == WeeklyAutoTrigger::Rebase && weeklyAutoOpenedId_ != currentId_) return;  // 開いたときに取っていなければ、取り直すものがない
  const auto r = s->recordWeeklyAuto(why == WeeklyAutoTrigger::Open ? WeeklyAutoMode::IfMissing : WeeklyAutoMode::Replace);
  weeklyAutoOpenedId_ = why == WeeklyAutoTrigger::Open && r.ok() && r.value() == WeeklyAutoResult::Recorded ? currentId_ : std::string();
  if (!r.ok()) return;  // 日程を計算できないときは取らない（通知もしない。週報のほうで比べる元がなくなるだけ）
  if (r.value() == WeeklyAutoResult::Recorded || r.value() == WeeklyAutoResult::Replaced) afterEdit();
}

// ---------------------------------------------------------------- ベースライン
std::string AppController::defaultBaselineName() const { return clock_.todayIso() + " の計画"; }

Result<std::string> AppController::saveBaseline(const std::string& name) {
  if (!session_) return Error{"プロジェクトが開かれていません"};
  if (!canEdit()) return Error{"読み取り専用で開いているため、保存できません"};
  auto r = session_->addBaseline(name);
  if (!r.ok()) {
    toast("ベースラインを保存できません: " + r.error());
    return r;
  }
  afterEdit();
  const Baseline* b = baselineFind(session_->baselines(), r.value());
  toast("ベースライン「" + (b ? b->name : name) + "」を保存しました。ガント・リスト・統計に計画との差を出します");
  return r;
}

bool AppController::renameBaseline(const std::string& id, const std::string& name) {
  if (!session_ || !canEdit()) return false;
  const Status st = session_->renameBaseline(id, name);
  if (!st.ok()) {
    toast("名前を変えられません: " + st.error());
    return false;
  }
  afterEdit();
  return true;
}

bool AppController::deleteBaseline(const std::string& id) {
  if (!session_ || !canEdit()) return false;
  const Baseline* b = baselineFind(session_->baselines(), id);
  const std::string name = b ? b->name : std::string();
  if (!session_->removeBaseline(id)) return false;
  afterEdit();
  toast("ベースライン「" + name + "」を削除しました");
  return true;
}

void AppController::showBaseline(const std::string& id) {
  if (!session_) return;
  if (session_->showBaseline(id)) afterEdit();
}

void AppController::setEvmSettings(const EvmSettings& s) {
  if (!session_ || readOnly()) return;
  const EvmSettings before = session_->evmSettings();
  session_->setEvmSettings(s);
  if (!(session_->evmSettings() == before)) afterEdit();
}

// ---------------------------------------------------------------- 表示状態
View AppController::view() const { return viewFromKey(uiStr("view", "gantt")); }
void AppController::setView(View v) { setUi("view", viewKey(v)); }

std::string AppController::uiStr(const char* key, const std::string& def) const {
  if (!session_) return def;
  const Json& ui = session_->settings().ui;
  if (ui.contains(key) && ui[key].is_string()) return ui[key].get<std::string>();
  return def;
}
int AppController::uiInt(const char* key, int def) const {
  if (!session_) return def;
  const Json& ui = session_->settings().ui;
  if (ui.contains(key) && ui[key].is_number()) {
    const double d = ui[key].get<double>();
    if (!(d >= -2147483648.0 && d <= 2147483647.0)) return def;  // 範囲外・NaN は int にキャストしない（未定義動作）
    return static_cast<int>(d);
  }
  return def;
}
bool AppController::uiBool(const char* key, const char* sub, bool def) const {
  if (!session_) return def;
  const Json& ui = session_->settings().ui;
  if (!ui.contains(key) || !ui[key].is_object() || !ui[key].contains(sub) || !ui[key][sub].is_boolean()) return def;
  return ui[key][sub].get<bool>();
}
void AppController::setUi(const char* key, Json v) {
  if (!session_) return;
  if (session_->ui().contains(key) && session_->ui()[key] == v) return;
  session_->ui()[key] = std::move(v);
  session_->touchUi();
  afterEdit();
}
void AppController::setUiSub(const char* key, const std::string& sub, Json v) {
  if (!session_) return;
  Json& ui = session_->ui();
  if (!ui.contains(key) || !ui[key].is_object()) ui[key] = Json::object();
  if (ui[key].contains(sub) && ui[key][sub] == v) return;
  ui[key][sub] = std::move(v);
  session_->touchUi();
  afterEdit();
}

// ---------------------------------------------------------------- 選択
bool AppController::isSelected(const std::string& id) const {
  return std::find(sel_.begin(), sel_.end(), id) != sel_.end();
}
void AppController::select(const std::string& id) {
  sel_ = {id};
  cursor_ = anchor_ = id;
}
void AppController::toggleSelect(const std::string& id) {
  auto it = std::find(sel_.begin(), sel_.end(), id);
  if (it != sel_.end()) {
    sel_.erase(it);
    cursor_ = sel_.empty() ? "" : sel_.back();
  } else {
    sel_.push_back(id);
    cursor_ = anchor_ = id;
  }
}
void AppController::rangeSelect(const std::string& id, const std::vector<std::string>& order) {
  const std::string a = !anchor_.empty() ? anchor_ : (!cursor_.empty() ? cursor_ : id);
  const auto ia = std::find(order.begin(), order.end(), a);
  const auto ib = std::find(order.begin(), order.end(), id);
  if (ia == order.end() || ib == order.end()) {
    select(id);
    return;
  }
  const auto lo = std::min(ia, ib), hi = std::max(ia, ib);
  sel_.assign(lo, hi + 1);
  cursor_ = id;
  anchor_ = a;
}
void AppController::setSelection(std::vector<std::string> ids, const std::string& cursor) {
  sel_ = std::move(ids);
  cursor_ = cursor;
  if (anchor_.empty() || !isSelected(anchor_)) anchor_ = cursor;
}
void AppController::clearSelection() {
  sel_.clear();
  cursor_.clear();
  anchor_.clear();
}
void AppController::moveCursor(int dir, bool extend, const std::vector<std::string>& order) {
  if (order.empty()) return;
  auto it = std::find(order.begin(), order.end(), cursor_);
  std::ptrdiff_t i = it == order.end() ? (dir > 0 ? -1 : static_cast<std::ptrdiff_t>(order.size())) : it - order.begin();
  i = std::clamp<std::ptrdiff_t>(i + dir, 0, static_cast<std::ptrdiff_t>(order.size()) - 1);
  const std::string& id = order[static_cast<std::size_t>(i)];
  if (extend) rangeSelect(id, order);
  else select(id);
}
void AppController::pruneSelection() {
  if (!session_) {
    clearSelection();
    return;
  }
  sel_.erase(std::remove_if(sel_.begin(), sel_.end(), [&](const std::string& id) { return !session_->issue(id); }),
             sel_.end());
  if (!cursor_.empty() && !session_->issue(cursor_)) cursor_ = sel_.empty() ? "" : sel_.back();
  if (!anchor_.empty() && !session_->issue(anchor_)) anchor_ = cursor_;
}
std::vector<std::string> AppController::selectionRoots() const {
  std::vector<std::string> out;
  if (!session_ || !session_->tree()) return out;
  const Tree& T = *session_->tree();
  std::unordered_set<int> s;
  for (const auto& id : sel_)
    if (int i = T.find(id); i >= 0) s.insert(i);
  // 配列の順に（並び順を保つ）
  for (int i = 0; i < T.n; ++i) {
    if (!s.count(i)) continue;
    bool anc = false;
    for (int p = T.parentOf[i]; p >= 0 && !anc; p = T.parentOf[p]) anc = s.count(p) > 0;
    if (!anc) out.push_back(T.ids[i]);
  }
  return out;
}

Filter AppController::hideFilter() const {
  Filter f;
  f.q = filter_.q;
  return f;
}
Filter AppController::dimFilter() const {
  Filter f = filter_;
  f.q.clear();
  return f;
}
std::vector<VisibleRow> AppController::visibleRows() const {
  return session_ ? session_->visibleRows(hideFilter(), &colFilter_) : std::vector<VisibleRow>{};
}
std::vector<int> AppController::visibleLeaves() const {
  return session_ ? session_->visibleLeaves(hideFilter(), &colFilter_) : std::vector<int>{};
}
std::vector<char> AppController::dimmedSet() const {
  const Filter d = dimFilter();
  if (!session_ || !d.active()) return {};
  auto m = session_->matchSet(d);
  for (auto& x : m) x = !x;
  return m;
}

std::string AppController::filterLabel() const {
  std::vector<std::string> v;
  if (!filter_.text_q_empty()) v.push_back("検索");
  if (colFilter_.active()) v.push_back("列");
  if (filter_.asg != AsgFilter::All) v.push_back("担当");
  if (filter_.st != StFilter::All) v.push_back("状況");
  if (!filter_.pr.empty()) v.push_back("優先度");
  switch (filter_.flt) {
    case FltFilter::Late: v.push_back("遅れ"); break;
    case FltFilter::Tight: v.push_back("余裕小"); break;
    case FltFilter::Done: v.push_back("完了"); break;
    case FltFilter::Open: v.push_back("未完了"); break;
    default: break;
  }
  return text::join(v, "・");
}

// ---------------------------------------------------------------- 選択への操作
void AppController::indentSelection() {
  if (!session_ || !canEdit()) return;
  session_->beginBatch();
  for (const auto& id : selectionRoots()) session_->indent(id);
  session_->endBatch();
  afterEdit();
}
void AppController::outdentSelection() {
  if (!session_ || !canEdit()) return;
  auto roots = selectionRoots();
  session_->beginBatch();
  // 下から順に（元の親の直後へ入れるため、並びが逆にならないように）
  for (auto it = roots.rbegin(); it != roots.rend(); ++it) session_->outdent(*it);
  session_->endBatch();
  afterEdit();
}
void AppController::moveSelection(int dir) {
  if (!session_ || !canEdit()) return;
  auto roots = selectionRoots();
  session_->beginBatch();
  if (dir > 0) std::reverse(roots.begin(), roots.end());
  for (const auto& id : roots) session_->moveIssue(id, dir);
  session_->endBatch();
  afterEdit();
}
ChainResult AppController::chainSelection(const std::vector<std::string>& order) {
  if (!session_ || !canEdit()) return {};
  const auto r = session_->chainIssues(sel_, order);
  afterEdit();
  return r;
}
int AppController::unchainSelection() {
  if (!session_ || !canEdit()) return 0;
  const int n = session_->unchainIssues(sel_);
  afterEdit();
  return n;
}
void AppController::deleteSelection() {
  if (!session_ || sel_.empty() || !canEdit()) return;
  session_->deleteIssues(sel_);
  afterEdit();
}
void AppController::setStatusFor(const std::vector<std::string>& ids, const std::string& st) {
  if (!session_ || !canEdit()) return;
  session_->beginBatch();
  for (const auto& id : ids)
    if (const Tree* T = session_->tree(); T && T->find(id) >= 0 && T->isLeaf(T->find(id))) session_->setStatus(id, st);
  session_->endBatch();
  afterEdit();
}
void AppController::setPrioFor(const std::vector<std::string>& ids, const std::string& pr) {
  if (!session_ || !canEdit()) return;
  session_->bulk(ids, [&](Issue& o) { o.prio = pr; });
  afterEdit();
}
void AppController::setAssigneeFor(const std::vector<std::string>& ids, const std::string& who) {
  if (!session_ || !canEdit()) return;
  const std::string w = text::trim(who);
  session_->bulk(ids, [&](Issue& o) { o.assignee = w; });
  afterEdit();
}
std::string AppController::newIssueName(const std::string& parentId) const {
  if (!session_) return "";
  NewNameInput in;
  in.mode = newNameModeFromKey(prefs_.newNameFormat);
  if (in.mode == NewNameMode::Keep) return "";
  in.custom = prefs_.newNameCustom;
  in.today = clock_.todayIso();
  const Tree* T = session_->tree();
  const int p = parentId.empty() || !T ? -1 : T->find(parentId);
  if (p >= 0) {
    in.hasParent = true;
    in.parentName = T->name[p];
    in.parentDid = T->did[p];
  }
  // 兄弟の名前（番号タグ [T01-1] を除いた表示名。タグがなければ件名と同じ）
  const std::string par = p >= 0 ? parentId : std::string();
  for (const Issue& o : session_->issues()) {
    if (o.parent != par) continue;
    const int k = T ? T->find(o.id) : -1;
    in.siblings.push_back(k >= 0 ? T->name[k] : o.summary);
  }
  return makeNewIssueName(in);
}
std::string AppController::createIssue(std::string parentId, std::string afterId) {
  if (!session_ || !canEdit()) return "";
  const std::string id = session_->addIssue(parentId, afterId, newIssueName(parentId));
  afterEdit();
  return id;
}
std::string AppController::addSibling() {
  if (!session_ || !canEdit()) return "";
  std::string id;
  if (cursor_.empty() || !session_->issue(cursor_)) id = createIssue("", "");
  else id = createIssue(session_->issue(cursor_)->parent, cursor_);
  if (!id.empty()) select(id);
  return id;
}
std::string AppController::addChild() {
  if (!session_ || !canEdit()) return "";
  if (cursor_.empty()) return addSibling();
  const std::string id = createIssue(cursor_, "");
  if (!id.empty()) select(id);
  return id;
}
void AppController::undo() {
  if (!session_ || !canEdit(false)) return;
  if (session_->undo()) afterEdit();
}
void AppController::redo() {
  if (!session_ || !canEdit(false)) return;
  if (session_->redo()) afterEdit();
}

// ---------------------------------------------------------------- テンプレート
std::vector<Template> AppController::templates() const {
  std::vector<Template> v = builtinTemplates();
  v.insert(v.end(), prefs_.templates.begin(), prefs_.templates.end());
  return v;
}
std::string AppController::saveTemplate(const std::string& nameIn, const std::string& desc,
                                        std::vector<TemplateItem> items) {
  const std::string name = text::trim(nameIn);
  if (name.empty() || items.empty()) return "";
  Template t;
  t.id = "u:" + newProjectId(clock_).substr(1);
  t.name = name;
  t.desc = desc.empty() ? std::to_string(items.size()) + "件の子タスク" : desc;
  t.items = std::move(items);
  prefs_.templates.push_back(t);
  savePrefs();
  toast("テンプレート「" + name + "」を保存しました");
  return t.id;
}
void AppController::deleteTemplate(const std::string& id) {
  auto& v = prefs_.templates;
  v.erase(std::remove_if(v.begin(), v.end(), [&](const Template& t) { return t.id == id; }), v.end());
  savePrefs();
}

// ---------------------------------------------------------------- prefs・通知
void AppController::savePrefs() {
  if (session_) session_->setInherit(prefs_.inherit);  // 追加の引き継ぎの設定を、開いているプロジェクトへ渡す
  (void)roadmap::savePrefs(fs_, prefsPath_, prefs_);
}
void AppController::setTheme(const std::string& t) {
  prefs_.theme = t;
  savePrefs();
}
bool AppController::setUiScale(int pct) {
  pct = std::clamp(pct, kUiScaleSteps[0], kUiScaleSteps[std::size(kUiScaleSteps) - 1]);
  if (pct == prefs_.uiScale) return false;
  prefs_.uiScale = pct;
  savePrefs();
  return true;
}

bool AppController::stepUiScale(int dir) {
  if (dir == 0) return false;
  int next = prefs_.uiScale;
  if (dir > 0) {
    for (int v : kUiScaleSteps)
      if (v > prefs_.uiScale) {
        next = v;
        break;
      }
  } else {
    for (auto it = std::rbegin(kUiScaleSteps); it != std::rend(kUiScaleSteps); ++it)
      if (*it < prefs_.uiScale) {
        next = *it;
        break;
      }
  }
  return setUiScale(next);
}

void AppController::setDensity(const std::string& d) {
  prefs_.density = d;
  savePrefs();
}
void AppController::dismissHint() {
  prefs_.hintDismissed = true;
  savePrefs();
}

void AppController::toast(const std::string& text, NoticeAction a, const std::string& arg) {
  Toast t;
  t.id = ++toastSeq_;
  t.text = text;
  t.action = a;
  t.arg = arg;
  t.expiresAt = now() + (a == NoticeAction::None ? 3600 : 7000);
  toasts_.push_back(std::move(t));
  if (toasts_.size() > 6) toasts_.erase(toasts_.begin());
}
void AppController::dismissToast(int id) {
  toasts_.erase(std::remove_if(toasts_.begin(), toasts_.end(), [&](const Toast& t) { return t.id == id; }),
                toasts_.end());
}
void AppController::runToastAction(int id) {
  for (const auto& t : toasts_) {
    if (t.id != id) continue;
    if (t.action == NoticeAction::Undo) {
      dismissToast(id);
      undo();
      return;
    }
    if (t.action == NoticeAction::Complete) {
      const std::string project = t.project, issueId = t.arg;  // dismissToast で t が消える前に写す
      dismissToast(id);
      completeFromRemote(project, issueId, false);
      return;
    }
  }
}

// ---------------------------------------------------------------- GitHub の状態の確認
namespace {
constexpr std::size_t kLabelNameChars = 30;  // 通知に入れる件名の長さ（文字）
constexpr std::size_t kMaxRemoteReasonBytes = 300;
}  // namespace

void AppController::setRemoteStatus(std::shared_ptr<IRemoteStatus> impl, bool async) {
  worker_.reset();  // 実行中のものは中断して片づける
  remoteQueue_.clear();
  remoteActive_.clear();
  remoteBatches_.clear();
  remoteDone_.clear();
  remote_ = std::move(impl);
  remoteAsync_ = async;
  if (remote_) worker_ = std::make_unique<RemoteWorker>(remote_, async);
}

std::string AppController::issueLabel(const std::string& issueId) const {
  std::string did = issueId, name;
  if (session_) {
    const Tree* T = session_->tree();
    const int idx = T ? T->find(issueId) : -1;
    if (idx >= 0) {
      did = T->did[idx];
      name = T->name[idx];
    } else if (const Issue* o = session_->issue(issueId)) {
      name = o->summary;
    }
  }
  const std::u32string u = text::toU32(name);
  if (u.size() > kLabelNameChars) name = text::fromU32(std::u32string_view(u).substr(0, kLabelNameChars)) + "…";
  return "#" + did + " 『" + name + "』";
}

AppController::RemoteView AppController::remoteViewOf(const std::string& target) const {
  RemoteView v;
  const LinkClass k = classifyLink(target);
  if (!linkKindGitHub(k.kind)) return v;
  const auto ref = parseGitHubUrl(k.target);
  if (!ref) return v;
  const std::string key = ref->canonicalUrl();
  if (remoteActive_.count(key)) {
    v.phase = RemoteView::Phase::Checking;
    return v;
  }
  const auto it = remoteDone_.find(key);
  if (it == remoteDone_.end()) return v;
  v.checkedAt = it->second.checkedAt;
  if (it->second.failed) {
    v.phase = RemoteView::Phase::Failed;
    v.error = it->second.error;
  } else {
    v.phase = RemoteView::Phase::Done;
    v.info = it->second.info;
  }
  return v;
}

std::size_t AppController::githubLinkCount(const std::string& issueId) const {
  std::size_t n = 0;
  for (const Link& l : linksOf(issueId))
    if (linkKindGitHub(classifyLink(l.target).kind)) ++n;
  return n;
}

bool AppController::remoteBusy() const { return !remoteActive_.empty(); }

RemoteStates AppController::remoteStates() const {
  RemoteStates st;
  for (const auto& [key, e] : remoteDone_) {
    // 失敗したものと、確認中（順番待ちを含む）のものは「未確認」と同じ扱い。確認中は以前の結果が残っているが、
    // 新しい結果が届くまでは古いので、完了の提案・了承・自動の完了の根拠にしない
    if (e.failed || remoteActive_.count(key)) continue;
    st[key] = e.info;
  }
  return st;
}

int AppController::checkRemote(const std::string& issueId, std::optional<std::size_t> index) {
  if (!session_ || !session_->issue(issueId)) {
    toast("確認する課題がありません");
    return 0;
  }
  if (!remote_) {
    toast("この環境では GitHub の状態を確認できません");
    return 0;
  }
  const auto& list = linksOf(issueId);
  std::vector<RemoteJob> jobs;
  std::vector<std::string> keys;
  for (std::size_t i = 0; i < list.size(); ++i) {
    if (index && *index != i) continue;
    const LinkClass k = classifyLink(list[i].target);
    if (!linkKindGitHub(k.kind)) continue;
    auto ref = parseGitHubUrl(k.target);
    if (!ref) continue;
    const std::string key = ref->canonicalUrl();
    if (std::find(keys.begin(), keys.end(), key) != keys.end()) continue;  // 同じ PR・Issue は 1 回
    keys.push_back(key);
    jobs.push_back({key, *ref});
  }
  if (keys.empty()) {
    toast("確認できる GitHub の PR・Issue のリンクがありません");
    return 0;
  }
  // この課題の古い提案は取り下げる（新しい結果が届いたら、改めて出る）
  toasts_.erase(std::remove_if(toasts_.begin(), toasts_.end(),
                               [&](const Toast& t) {
                                 return t.action == NoticeAction::Complete && t.project == currentId_ && t.arg == issueId;
                               }),
                toasts_.end());
  for (RemoteJob& j : jobs)
    if (remoteActive_.insert(j.key).second) remoteQueue_.push_back(std::move(j));  // 確認中のものは、その結果を待つ
  remoteBatches_.push_back({currentId_, issueId, keys});
  return static_cast<int>(keys.size());
}

void AppController::dropRemoteQueue() {
  for (const RemoteJob& j : remoteQueue_) remoteActive_.erase(j.key);
  remoteQueue_.clear();
  remoteBatches_.clear();
}

void AppController::pumpRemote() {
  if (!worker_) return;
  bool changed = false;
  while (true) {
    if (worker_->running()) {
      std::string key;
      std::optional<Result<RemoteInfo>> res;
      if (!worker_->poll(key, res)) break;  // まだ実行中
      RemoteEntry e;
      e.checkedAt = clock_.nowDT();
      if (!res || !res->ok()) {
        e.failed = true;
        e.error = res ? res->error() : std::string("結果を受け取れませんでした");
      } else if (res->value().state == RemoteState::Unknown) {
        e.failed = true;
        e.error = res->value().reason.empty() ? std::string("状態を判定できませんでした") : res->value().reason;
      } else {
        e.info = res->value();
      }
      if (e.error.size() > kMaxRemoteReasonBytes) e.error = text::sanitizeUtf8(e.error.substr(0, kMaxRemoteReasonBytes)) + "…";
      remoteDone_[key] = std::move(e);
      remoteActive_.erase(key);
      changed = true;
    }
    if (remoteQueue_.empty()) break;
    RemoteJob job = std::move(remoteQueue_.front());
    remoteQueue_.pop_front();
    worker_->start(job);
    changed = true;
    if (remoteAsync_) break;  // 結果は次の tick 以降に拾う
  }
  if (changed) finishRemoteBatches();
}

void AppController::finishRemoteBatches() {
  std::vector<RemoteBatch> pending, finished;
  for (RemoteBatch& b : remoteBatches_) {
    const bool waiting =
        std::any_of(b.keys.begin(), b.keys.end(), [&](const std::string& k) { return remoteActive_.count(k) != 0; });
    (waiting ? pending : finished).push_back(std::move(b));
  }
  remoteBatches_ = std::move(pending);
  for (const RemoteBatch& b : finished) {
    if (!session_ || b.project != currentId_) continue;  // 別のプロジェクトに切り替わった（結果は表示のために残してある）
    const Issue* issue = session_->issue(b.issueId);
    if (!issue) continue;
    int failures = 0;
    std::string firstError;
    for (const std::string& k : b.keys) {
      const auto it = remoteDone_.find(k);
      if (it != remoteDone_.end() && it->second.failed && failures++ == 0) firstError = it->second.error;
    }
    if (failures > 0) {
      // URL・リポジトリ名は出さない（件数と理由だけ）
      toast("GitHub の状態を確認できませんでした（" + (b.keys.size() > 1 ? std::to_string(failures) + " 件。" : std::string()) +
            firstError + "）");
      continue;
    }
    if (readOnly()) continue;  // 読み取り専用のあいだは、完了の提案も自動の完了もしない（状態はリンクの横に出ている）
    const auto proposal = proposeCompletion(*issue, session_->links(), remoteStates(), utcOffsetFn_);
    if (!proposal) continue;  // まだ閉じていないものがある（状態はリンクの横に出ている）
    if (prefs_.autoCompleteGitHub) {
      completeFromRemote(b.project, b.issueId, true);
      continue;
    }
    // 古い提案は置き換える
    toasts_.erase(std::remove_if(toasts_.begin(), toasts_.end(),
                                 [&](const Toast& t) {
                                   return t.action == NoticeAction::Complete && t.project == b.project && t.arg == b.issueId;
                                 }),
                  toasts_.end());
    std::string msg = issueLabel(b.issueId) + "のリンクがすべて閉じています。完了にしますか？";
    if (!proposal->caution.empty()) msg += "（" + proposal->caution + "）";
    toast(msg, NoticeAction::Complete, b.issueId);
    toasts_.back().project = b.project;
    toasts_.back().expiresAt = now() + 20000;  // 判断に時間がかかるので長めに出す
  }
}

bool AppController::completeFromRemote(const std::string& project, const std::string& issueId, bool automatic) {
  if (readOnly()) {  // 提案もしない（読み取り専用で開いているあいだは変えられない）
    if (!automatic) canEdit();
    return false;
  }
  if (!session_ || currentId_ != project) {
    toast("プロジェクトが切り替わったため、完了にしませんでした");
    return false;
  }
  const Issue* issue = session_->issue(issueId);
  if (!issue) {
    toast("課題が見つからないため、完了にしませんでした");
    return false;
  }
  const auto proposal = proposeCompletion(*issue, session_->links(), remoteStates(), utcOffsetFn_);
  if (!proposal) {
    toast("リンクの状態が変わったため、完了にしませんでした");
    return false;
  }
  const Day day = std::min(proposal->resolved, clock_.today());  // 先の日付（時計のずれ）は今日にする
  const std::string label = issueLabel(issueId);
  const bool ok = session_->completeIssue(issueId, day);
  afterEdit();
  if (!ok) return false;
  toast(label + (automatic ? "のリンクがすべて閉じていたので、自動で完了にしました（完了日 " : "を完了にしました（完了日 ") + fmtIso(day) + "）",
        NoticeAction::Undo);
  return true;
}

// ---------------------------------------------------------------- 編集中のロック（Issue #62）
// ロックの内容（利用者名・マシン名）は画面にだけ出し、ログには出さない
namespace {
std::string pad2(int v) { return (v < 10 ? "0" : "") + std::to_string(v); }

std::string holderLabel(const AppController::LockStatus& st) {
  std::string who = st.user.empty() ? "だれか" : st.user + "さん";
  if (!st.machine.empty()) who += "（" + st.machine + "）";
  return who;
}

void fillHolder(AppController::LockStatus& st, const LockInfo& info) {
  st.user = info.user;
  st.machine = info.machine;
  st.openedAt = info.openedAt;
  st.heartbeatAt = info.heartbeatAt;
}
}  // namespace

void AppController::setLockStore(ILockStore* store, IRandom* rng, std::string machine) {
  lockStore_ = store;
  lockRng_ = rng;
  machine_ = std::move(machine);
  if (session_ && lockStore_ && prefs_.useLock) {
    // 開いたあとに窓口を渡されたときも、いまのプロジェクトのロックを取る
    lock_.projectId.clear();
    enterLock();
    applyReadOnly();
  }
}

IRandom& AppController::lockRandom() {
  if (lockRng_) return *lockRng_;
  if (!ownRng_) ownRng_ = std::make_unique<SystemRandom>();
  return *ownRng_;
}

LockWho AppController::lockWho() const {
  LockWho w;
  w.user = text::trim(prefs_.me);
  w.machine = machine_;
  return w;
}

std::string AppController::lockPath(const std::string& id) const {
  const ProjectEntry* e = ws_->find(id);
  if (!e || e->layout != Layout::Folder) return "";
  const std::string dir = ws_->projectDir(*e);
  return dir.empty() ? std::string() : lockPathIn(dir);
}

bool AppController::readOnly() const {
  const LockKind k = lock_.status.kind;
  return k == LockKind::Other || k == LockKind::Failed || k == LockKind::Lost;
}

bool AppController::canEdit(bool announce) {
  if (!readOnly()) return true;
  if (announce) {
    const std::string msg = "読み取り専用で開いているため、変更できません（画面上部の「再確認」「編集を引き継ぐ」で切り替えられます）";
    if (toasts_.empty() || toasts_.back().text != msg) toast(msg);
  }
  return false;
}

void AppController::toastLong(const std::string& text) {
  toast(text);
  toasts_.back().expiresAt = now() + 15000;  // 読み取り専用に切り替わった理由は、見落とさないよう長めに出す
}

void AppController::applyReadOnly() {
  const bool ro = readOnly();
  if (session_) session_->setReadOnly(ro);
  if (!lock_.projectId.empty()) ws_->setFrozen(lock_.projectId, ro);
}

void AppController::tryAcquire(const std::string& path) {
  const AcquireResult r = acquireLock(*lockStore_, clock_, lockRandom(), path, lockWho());
  LockStatus st;
  switch (r.kind) {
    case AcquireKind::Acquired:
      st.kind = LockKind::Mine;
      lock_.mine = r.lock;
      lock_.beatWarned = false;
      break;
    case AcquireKind::Held:
      st.kind = LockKind::Other;
      st.stale = r.stale;
      st.broken = r.broken;
      fillHolder(st, r.lock);
      break;
    case AcquireKind::Failed:
      st.kind = LockKind::Failed;
      st.reason = r.error;
      break;
  }
  lock_.status = st;
  lock_.lastCheck = now();
}

void AppController::enterLock() {
  lock_ = LockState{};
  lock_.projectId = currentId_;
  if (!lockEnabled() || !session_) return;
  const ProjectEntry* e = ws_->find(currentId_);
  if (!e || e->layout != Layout::Folder) return;  // 平置きはプロジェクトのフォルダがないので、ロックしない
  const std::string path = lockPath(currentId_);
  if (path.empty()) {
    lock_.pending = true;  // まだ書いていない新しいプロジェクト。最初の保存のあとで取る
    return;
  }
  tryAcquire(path);
}

void AppController::leaveLock() {
  if (lockStore_ && lock_.status.kind == LockKind::Mine) {
    const std::string path = lockPath(lock_.projectId);
    if (!path.empty()) {
      // 未保存の編集は、ロックを持っているうちに書く（各ファイルの直前にも検査する）
      const ProjectEntry* e = ws_->find(lock_.projectId);
      if (e && (e->dirty || e->settingsDirty)) {
        guardLost_.reset();
        (void)ws_->flush();
        guardLost_.reset();
      }
      // 書けなかった（引き継がれていた・確かめられない・書き込みの失敗）ときは、前のプロジェクトを書かずに、
      // 未保存の編集を別のプロジェクトとして残す（切り替えたあとの保存で、前のプロジェクトを新しい持ち主の上に書かない）
      const std::string old = lock_.projectId;
      const std::string kept = preserveUnsaved(old);
      if (!kept.empty()) toastLong("保存できなかったため、あなたの未保存の編集は「" + kept + "」として残しました");
      (void)releaseLock(*lockStore_, path, lock_.mine.token);
    }
  }
  if (!lock_.projectId.empty()) ws_->setFrozen(lock_.projectId, false);
  lock_ = LockState{};
}

Status AppController::lockGate() {
  if (!lockEnabled() || lock_.status.kind != LockKind::Mine || !session_) return okStatus();
  const ProjectEntry* e = ws_->find(currentId_);
  if (!e || (!e->dirty && !e->settingsDirty)) return okStatus();
  const std::string path = lockPath(currentId_);
  if (path.empty()) return okStatus();
  const OwnershipResult o = checkOwnership(*lockStore_, path, lock_.mine.token);
  if (o.kind == Ownership::Mine) return okStatus();
  // 確かめられないとき（ネットワークの不調など）は、失ったとは決めず、保存を見合わせて待つ（再試行する）
  if (o.kind == Ownership::Unreadable) return Error{"ロックを確かめられないため、保存を見合わせています（" + o.error + "）"};
  loseLock(o);
  return okStatus();
}

Status AppController::flushGuarded() {
  if (const Status g = lockGate(); !g) return g;
  guardLost_.reset();
  if (!ws_->externallyChangedDirty().empty()) {
    // フォルダのファイルが自分の記録より新しい（ロックを使わない古い版が書いたなど）。そのまま書くと上書きしてしまうので、
    // 外部変更の取り込み（8.5）に回す。両方変わっていれば、フォルダの版を別のプロジェクトとして残して、手元を書く
    runSync(false, true);
    if (saveState_ == SaveState::Error) return Error{saveError_};
    return okStatus();
  }
  const ProjectEntry* cur = ws_->find(currentId_);
  const bool wroteCurrent = cur && (cur->dirty || cur->settingsDirty) && lock_.status.kind == LockKind::Mine;
  const Status s = ws_->flush();
  if (!s) {
    if (handleGuardLoss()) return okStatus();  // 書く直前の検査で引き継ぎが分かった: 書かず、読み取り専用に切り替えた
    return s;
  }
  if (wroteCurrent) {
    // 書き終えたあとにも確かめる。検査と書き込みの間に引き継がれていたら、混ざった可能性を知らせて読み取り専用にする
    const std::string path = lockPath(currentId_);
    if (!path.empty()) {
      const OwnershipResult o = checkOwnership(*lockStore_, path, lock_.mine.token);
      if (o.kind == Ownership::Other || o.kind == Ownership::Missing || o.kind == Ownership::Broken) loseLock(o, true);
    }
  }
  return okStatus();
}

bool AppController::writeAllowed(const ProjectEntry& e) {
  if (!lockEnabled() || lock_.status.kind != LockKind::Mine || e.id() != lock_.projectId) return true;
  const std::string path = lockPath(e.id());
  if (path.empty()) return true;
  const OwnershipResult o = checkOwnership(*lockStore_, path, lock_.mine.token);
  if (o.kind == Ownership::Mine) return true;
  if (o.kind != Ownership::Unreadable) guardLost_ = o;  // 引き継がれた・消された・壊れた。確かめられないだけなら、再試行に任せる
  return false;
}

bool AppController::handleGuardLoss() {
  if (!guardLost_) return false;
  const OwnershipResult o = *guardLost_;
  guardLost_.reset();
  loseLock(o);
  return true;
}

void AppController::installWriteGuard() {
  ws_->setWriteGuard([this](const ProjectEntry& e) { return writeAllowed(e); });
}

bool AppController::verifyLockBeforeWrite() {
  if (!lockEnabled() || lock_.status.kind != LockKind::Mine || !session_) return true;
  const std::string path = lockPath(currentId_);
  if (path.empty()) return true;
  const OwnershipResult o = checkOwnership(*lockStore_, path, lock_.mine.token);
  if (o.kind == Ownership::Mine) return true;
  if (o.kind == Ownership::Unreadable) {
    toast("ロックを確かめられないため、書き込みを見合わせました（" + o.error + "）");
    return false;
  }
  loseLock(o);  // 引き継がれていた: 何も書かず、読み取り専用に切り替える
  return false;
}

std::string AppController::preserveUnsaved(const std::string& id) {
  std::string kept;
  if (const ProjectEntry* e = ws_->find(id); e && (e->dirty || e->settingsDirty)) {
    ProjectRecord rec = e->rec;
    rec.settings.id.clear();
    rec.settings.name += "（未保存の編集）";
    rec.settings.attachments.clear();  // 添付の本体は複製しない
    kept = rec.settings.name;
    ws_->add(std::move(rec), true);  // 別のプロジェクトとして残す（次の保存で書く）
  }
  ws_->discardUnsaved(id);
  return kept;
}

void AppController::loseLock(const OwnershipResult& o, bool afterWrite) {
  afterEdit();  // 手元の最新の編集を Workspace に入れる（まだ自分のロックの状態のうちに）
  const std::string kept = preserveUnsaved(currentId_);
  LockStatus st;
  st.kind = LockKind::Lost;
  std::string why;
  switch (o.kind) {
    case Ownership::Other:
      fillHolder(st, o.current);
      st.stale = lockIsStale(o.current, clock_.epochMs());
      why = afterWrite ? "保存の途中で" + holderLabel(st) + "が編集を引き継ぎました。あなたの保存と相手の保存が混ざった可能性があるため、読み取り専用に切り替えました。内容を確かめてください"
                       : holderLabel(st) + "がこのプロジェクトの編集を引き継いだため、保存せずに読み取り専用に切り替えました";
      break;
    case Ownership::Missing:
      st.free = true;
      st.reason = "ロックのファイルがなくなりました";
      why = afterWrite ? "保存の途中でロックのファイルがなくなりました。読み取り専用に切り替えました。内容を確かめてください"
                       : "ロックのファイルがなくなったため、保存せずに読み取り専用に切り替えました";
      break;
    default:
      st.broken = true;
      st.stale = true;
      st.reason = "ロックのファイルが壊れています";
      why = afterWrite ? "保存の途中でロックのファイルが壊れました。読み取り専用に切り替えました。内容を確かめてください"
                       : "ロックのファイルが壊れていたため、保存せずに読み取り専用に切り替えました";
      break;
  }
  lock_.status = st;
  lock_.mine = LockInfo{};
  lock_.lastCheck = now();
  applyReadOnly();
  runSync(false, false);  // 別のプロジェクトを書き、このプロジェクトはフォルダの版に読み直す
  toastLong(why + (kept.empty() ? std::string() : "。あなたの未保存の編集は「" + kept + "」として残しました"));
}

void AppController::tickLock() {
  if (!lockStore_) return;
  const std::int64_t t = now();
  if (lock_.pending && session_) {
    const ProjectEntry* e = ws_->find(lock_.projectId);
    if (!e) {
      lock_.pending = false;
    } else if (!e->stem.empty() && !e->dirty && !e->settingsDirty && lockEnabled()) {
      const std::string path = lockPath(lock_.projectId);
      lock_.pending = false;
      if (!path.empty()) {
        tryAcquire(path);
        applyReadOnly();
      }
    }
  }
  LockStatus& st = lock_.status;
  if (st.kind == LockKind::Other && !st.broken && !st.free) {
    LockInfo h;
    h.heartbeatAt = st.heartbeatAt;
    st.stale = lockIsStale(h, clock_.epochMs());
  }
  if (!lockEnabled() || !session_) return;
  if (t - lock_.lastCheck < kLockHeartbeatMs) return;
  const std::string path = lockPath(lock_.projectId);
  if (path.empty()) return;
  if (st.kind == LockKind::Mine) {
    lock_.lastCheck = t;
    const OwnershipResult o = heartbeat(*lockStore_, clock_, path, lock_.mine);
    if (o.kind == Ownership::Mine) {
      lock_.beatWarned = false;
    } else if (o.kind == Ownership::Unreadable) {
      if (!lock_.beatWarned) {
        lock_.beatWarned = true;
        toast("ロックを更新できません（共有フォルダに接続できていない可能性があります）。保存のたびに確かめます");
      }
    } else {
      loseLock(o);  // 引き継がれた・消された・壊された
    }
  } else if (st.kind == LockKind::Other || st.kind == LockKind::Lost) {
    // 持ち主の様子を見直す（ロックが解けたら、帯の文言が変わる）
    lock_.lastCheck = t;
    const OwnershipResult o = checkOwnership(*lockStore_, path, "");
    if (o.kind == Ownership::Missing) {
      st.free = true;
    } else if (o.kind == Ownership::Other) {
      st.free = false;
      st.broken = false;
      fillHolder(st, o.current);
      st.stale = lockIsStale(o.current, clock_.epochMs());
    } else if (o.kind == Ownership::Broken) {
      st.free = false;
      st.broken = true;
      st.stale = true;
    }
  }
}

void AppController::recheckLock() {
  if (!lockEnabled() || !session_) return;
  if (lock_.status.kind == LockKind::None || lock_.status.kind == LockKind::Mine) return;
  const std::string path = lockPath(currentId_);
  if (path.empty()) return;
  tryAcquire(path);  // ロックが解けていれば取れる（取れなければ、いまの持ち主が分かる）
  lock_.projectId = currentId_;
  applyReadOnly();
  runSync(false, false);  // 取ったあとで、フォルダの最新を読む（相手が最後に保存した内容で編集を始める）
  const LockStatus& st = lock_.status;
  if (st.kind == LockKind::Mine) toast("ロックを取りました。編集できます");
  else if (st.kind == LockKind::Other) toast(st.free ? "ロックが解けました" : holderLabel(st) + "がまだ編集中です");
  else toast("ロックを取れませんでした: " + st.reason);
}

bool AppController::takeOverLock() {
  if (!lockEnabled() || !session_) return false;
  if (lock_.status.kind == LockKind::Mine || lock_.status.kind == LockKind::None) return true;
  const std::string path = lockPath(currentId_);
  if (path.empty()) return false;
  const AcquireResult r = takeOver(*lockStore_, clock_, lockRandom(), path, lockWho());
  if (r.kind == AcquireKind::Acquired) {
    LockStatus st;
    st.kind = LockKind::Mine;
    lock_.status = st;
    lock_.mine = r.lock;
    lock_.beatWarned = false;
    lock_.lastCheck = now();
    applyReadOnly();
    runSync(false, false);  // 引き継いだ時点のフォルダの内容を読み直してから編集する
    toast("編集を引き継ぎました");
    return true;
  }
  if (r.kind == AcquireKind::Held) {  // 同時に引き継ごうとした別の人に先を越された
    LockStatus st;
    st.kind = LockKind::Other;
    st.stale = r.stale;
    st.broken = r.broken;
    fillHolder(st, r.lock);
    lock_.status = st;
    applyReadOnly();
    toast(holderLabel(st) + "が先に引き継ぎました");
    return false;
  }
  toast("編集を引き継げませんでした: " + r.error);
  return false;
}

void AppController::setUseLock(bool on) {
  if (prefs_.useLock == on) return;
  if (on && session_) flushNow();  // ロックなしで編集した内容を、ロックを取る前に書く
  prefs_.useLock = on;
  savePrefs();
  if (!session_) return;
  if (!on) {
    leaveLock();
    lock_.projectId = currentId_;
    applyReadOnly();
    return;
  }
  enterLock();
  applyReadOnly();
  if (readOnly()) runSync(false, false);
}

std::string AppController::lockSinceLabel() const {
  const std::int64_t at = lock_.status.openedAt;
  if (at <= 0) return "";
  const int offMin = utcOffsetFn_ ? utcOffsetFn_(at / 1000) : 0;
  const std::int64_t local = at + static_cast<std::int64_t>(offMin) * 60000;
  std::int64_t days = local / 86400000;
  std::int64_t ms = local % 86400000;
  if (ms < 0) {
    ms += 86400000;
    --days;
  }
  const int hh = static_cast<int>(ms / 3600000), mm = static_cast<int>((ms / 60000) % 60);
  std::string out = pad2(hh) + ":" + pad2(mm);
  if (fmtIso(static_cast<Day>(days)) != clock_.todayIso()) {
    const Ymd ymd = civilFromDays(static_cast<Day>(days));
    out = std::to_string(ymd.m) + "/" + std::to_string(ymd.d) + " " + out;
  }
  return out;
}

std::string AppController::readOnlyBand() const {
  const LockStatus& st = lock_.status;
  switch (st.kind) {
    case LockKind::Other: {
      if (st.free) return "編集していた人はいなくなりました（ロックが解けています）。「再確認」で編集できます";
      if (st.broken) return "ロックのファイルが壊れています（持ち主がいないようです）。読み取り専用で開いています";
      const std::string since = lockSinceLabel();
      if (st.stale) {
        const std::int64_t idleMin = std::max<std::int64_t>(0, (clock_.epochMs() - st.heartbeatAt) / 60000);
        return holderLabel(st) + "の編集中の記録が残っています" + (since.empty() ? "" : "（" + since + " から") +
               (since.empty() ? "（" : "、") + std::to_string(idleMin) +
               "分前から更新がありません）。持ち主がいないようです。読み取り専用で開いています";
      }
      return holderLabel(st) + "が" + (since.empty() ? "" : " " + since + " から") + "編集中です。読み取り専用で開いています";
    }
    case LockKind::Failed:
      return "ロックを作れないため、読み取り専用で開いています（" + st.reason + "）";
    case LockKind::Lost: {
      if (st.free) return "ロックのファイルがなくなったため、読み取り専用に切り替えました。「再確認」で編集できます";
      if (st.broken) return "ロックのファイルが壊れたため、読み取り専用に切り替えました。「編集を引き継ぐ」で編集できます";
      const std::string since = lockSinceLabel();
      return holderLabel(st) + "が編集を引き継いだため、読み取り専用に切り替えました" + (since.empty() ? "" : "（" + since + " から）");
    }
    default:
      return "";
  }
}

}  // namespace roadmap::app
