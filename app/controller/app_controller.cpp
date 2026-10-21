#include "app_controller.hpp"

#include <algorithm>
#include <iterator>
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
    default: return "gantt";
  }
}
View viewFromKey(const std::string& k) {
  if (k == "board") return View::Board;
  if (k == "list") return View::List;
  if (k == "goals") return View::Goals;
  if (k == "stats") return View::Stats;
  return View::Gantt;
}

AppController::AppController(IFileStore& fs, const IClock& clock, std::string dataDir, std::string prefsPath)
    : fs_(fs), clock_(clock), ws_(std::make_unique<Workspace>(fs, clock, std::move(dataDir))), prefsPath_(std::move(prefsPath)) {}

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
  auto r = ws_->migrateLegacy();
  lastSync_ = now();
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

  ws_ = std::move(next);
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
  if (!prefs_.autoBaseToday || !session_ || currentId_.empty()) return;
  if (!baseAdjusted_.insert(currentId_).second) return;  // 起動中に1回だけ（手で戻した日付は変えない）
  const std::string today = clock_.todayIso();
  const std::string prev = session_->settings().baseDate;
  if (prev == today) return;
  session_->setBaseDate(today);
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
  if (!canAttach()) {
    toast(attachDisabledReason());
    return out;
  }
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
  if (!canAttach()) {
    toast(attachDisabledReason());
    return false;
  }
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

bool AppController::removeAttachment(const std::string& issueId, std::size_t index) {
  if (!session_) return false;
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

void AppController::afterEdit() {
  if (!session_) return;
  absorbNotices();
  if (session_->version() != seenVersion_) {
    seenVersion_ = session_->version();
    ws_->update(currentId_, recordOf(session_->data()));
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
  const std::int64_t t = now();
  if (saveDueAt_ >= 0 && t >= saveDueAt_) {
    auto s = ws_->flush();
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
  auto s = ws_->flush();
  if (!s) {
    saveState_ = SaveState::Error;
    saveError_ = s.error();
    return false;
  }
  saveState_ = SaveState::Saved;
  savedAt_ = clock_.hhmm();
  saveDueAt_ = -1;
  return true;
}

bool AppController::hasUnsaved() const {
  if (session_ && session_->version() != seenVersion_) return true;
  return ws_->hasDirty();
}

void AppController::onActivated() {
  if (now() - lastSync_ > 3000) syncFolder(false);
}

void AppController::syncFolder(bool manual) {
  afterEdit();
  auto r = ws_->sync();
  lastSync_ = now();
  if (!r) {
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
  if (!msg.empty()) toast(msg);
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
  if (session_) retiredSessions_.push_back(std::move(session_));
  session_ = std::make_unique<Session>(std::move(h->data), &clock_, std::move(h->warns));
  session_->setInherit(prefs_.inherit);
  if (!session_->error().empty()) toast("このプロジェクトを計算できません: " + session_->error());
  absorbNotices();
  seenVersion_ = session_->version();
  if (currentId_ != id) colFilter_ = ColumnFilter{};  // 列の値はプロジェクトごとに違う
  currentId_ = id;
  firstRun_ = false;
  sel_.clear();
  cursor_.clear();
  anchor_.clear();
  if (prefs_.currentProject != id) {
    prefs_.currentProject = id;
    savePrefs();
  }
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
  if (!session_) return;
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
  if (!session_) return false;
  const ProjectEntry* e = ws_->find(currentId_);
  const std::string stem = e ? e->stem : "";
  const bool folder = !e || e->layout == Layout::Folder;
  auto s = ws_->remove(currentId_);
  if (!s) {
    toast("フォルダのファイルを移せませんでした: " + s.error());
    return false;
  }
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
    const bool same = !p.settings.id.empty() && ws_->find(p.settings.id);
    std::string id;
    if (same && overwrite) {
      id = p.settings.id;
      ws_->update(id, p);
      ws_->find(id)->dirty = true;
    } else {
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
  in.base = s->baseDay();
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

void AppController::releaseFrame() {
  retiredSessions_.clear();
  if (session_) session_->releaseFrame();
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
  if (ui.contains(key) && ui[key].is_number()) return static_cast<int>(ui[key].get<double>());
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
  if (!session_) return;
  session_->beginBatch();
  for (const auto& id : selectionRoots()) session_->indent(id);
  session_->endBatch();
  afterEdit();
}
void AppController::outdentSelection() {
  if (!session_) return;
  auto roots = selectionRoots();
  session_->beginBatch();
  // 下から順に（元の親の直後へ入れるため、並びが逆にならないように）
  for (auto it = roots.rbegin(); it != roots.rend(); ++it) session_->outdent(*it);
  session_->endBatch();
  afterEdit();
}
void AppController::moveSelection(int dir) {
  if (!session_) return;
  auto roots = selectionRoots();
  session_->beginBatch();
  if (dir > 0) std::reverse(roots.begin(), roots.end());
  for (const auto& id : roots) session_->moveIssue(id, dir);
  session_->endBatch();
  afterEdit();
}
ChainResult AppController::chainSelection(const std::vector<std::string>& order) {
  if (!session_) return {};
  const auto r = session_->chainIssues(sel_, order);
  afterEdit();
  return r;
}
int AppController::unchainSelection() {
  if (!session_) return 0;
  const int n = session_->unchainIssues(sel_);
  afterEdit();
  return n;
}
void AppController::deleteSelection() {
  if (!session_ || sel_.empty()) return;
  session_->deleteIssues(sel_);
  afterEdit();
}
void AppController::setStatusFor(const std::vector<std::string>& ids, const std::string& st) {
  if (!session_) return;
  session_->beginBatch();
  for (const auto& id : ids)
    if (const Tree* T = session_->tree(); T && T->find(id) >= 0 && T->isLeaf(T->find(id))) session_->setStatus(id, st);
  session_->endBatch();
  afterEdit();
}
void AppController::setPrioFor(const std::vector<std::string>& ids, const std::string& pr) {
  if (!session_) return;
  session_->bulk(ids, [&](Issue& o) { o.prio = pr; });
  afterEdit();
}
void AppController::setAssigneeFor(const std::vector<std::string>& ids, const std::string& who) {
  if (!session_) return;
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
  if (!session_) return "";
  const std::string id = session_->addIssue(parentId, afterId, newIssueName(parentId));
  afterEdit();
  return id;
}
std::string AppController::addSibling() {
  if (!session_) return "";
  std::string id;
  if (cursor_.empty() || !session_->issue(cursor_)) id = createIssue("", "");
  else id = createIssue(session_->issue(cursor_)->parent, cursor_);
  if (!id.empty()) select(id);
  return id;
}
std::string AppController::addChild() {
  if (!session_) return "";
  if (cursor_.empty()) return addSibling();
  const std::string id = createIssue(cursor_, "");
  if (!id.empty()) select(id);
  return id;
}
void AppController::undo() {
  if (session_ && session_->undo()) afterEdit();
}
void AppController::redo() {
  if (session_ && session_->redo()) afterEdit();
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
  for (const auto& t : toasts_)
    if (t.id == id && t.action == NoticeAction::Undo) {
      dismissToast(id);
      undo();
      return;
    }
}

}  // namespace roadmap::app
