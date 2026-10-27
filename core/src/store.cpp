#include "roadmap/store.hpp"
#include "roadmap/sha256.hpp"

#include <algorithm>
#include <map>

#include "roadmap/encoding.hpp"
#include "roadmap/text.hpp"

namespace roadmap {

std::string SyncReport::message(bool manual) const {
  std::vector<std::string> parts;
  if (loaded) parts.push_back("フォルダの更新を読み込み " + std::to_string(loaded) + "件");
  if (added) parts.push_back("フォルダから追加 " + std::to_string(added) + "件");
  if (wrote) parts.push_back("フォルダへ書き出し " + std::to_string(wrote) + "件");
  if (kept) parts.push_back("両方で変更があったため、フォルダ側の内容を「…（フォルダの版）」として残しました");
  if (!bad.empty()) parts.push_back("読めないCSV: " + text::join(bad, ", ") + "（上書きしていません）");
  if (parts.empty()) return manual ? "フォルダに新しい変更はありません" : "";
  return text::join(parts, " / ");
}

// ---------------------------------------------------------------- フォルダ名・添付・パスの長さ
namespace paths {
namespace {
bool isBadChar(char32_t c) {
  return c < 0x20 || c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|';
}
std::string replaceBad(const std::string& name) {
  std::string s;
  for (std::size_t i = 0; i < name.size();) {
    const std::size_t st = i;
    const char32_t c = text::decodeAt(name, i);
    if (isBadChar(c)) s += '_';
    else s.append(name, st, i - st);
  }
  return s;
}
bool isTrimChar(char32_t c) { return c == '.' || text::isJsSpace(c); }
// 前後の . と空白を削る（Windows は末尾の . と空白を勝手に削るので、残さない）
std::string trimDots(const std::string& s, bool head = true) {
  const std::u32string u = text::toU32(s);
  std::size_t a = 0, b = u.size();
  while (head && a < b && isTrimChar(u[a])) ++a;
  while (b > a && isTrimChar(u[b - 1])) --b;
  return text::fromU32(u.substr(a, b - a));
}
// 拡張子の位置（最後の . から。先頭の . や長すぎるものは拡張子とみなさない）。なければ npos
std::size_t extPos(const std::string& s) {
  const auto k = s.rfind('.');
  if (k == std::string::npos || k == 0) return std::string::npos;
  if (text::utf16Length(s.substr(k)) > 16) return std::string::npos;
  return k;
}
// maxUnits（UTF-16）以内に切る。拡張子は残す
std::string shorten(const std::string& name, std::size_t maxUnits) {
  if (text::utf16Length(name) <= maxUnits) return name;
  std::string ext, base = name;
  if (const auto k = extPos(name); k != std::string::npos) {
    ext = name.substr(k);
    base = name.substr(0, k);
  }
  std::size_t extU = text::utf16Length(ext);
  if (extU >= maxUnits) {
    ext.clear();
    extU = 0;
    base = name;
  }
  std::string b = trimDots(text::utf16Prefix(base, maxUnits - extU), false);
  if (b.empty()) b = "_";
  return b + ext;
}
}  // namespace

std::size_t utf16Units(const std::string& s) { return text::utf16Length(s); }

std::string safeName(const std::string& name, std::size_t maxUnits, const std::string& fallback) {
  std::string s = trimDots(replaceBad(name));
  s = trimDots(text::utf16Prefix(s, maxUnits), false);
  return s.empty() ? fallback : s;
}

std::string safeFileName(const std::string& name) {
  std::string s = trimDots(replaceBad(name));
  if (s.empty()) return "file";
  return shorten(s, kMaxFileNameUnits);
}

std::string uniqueName(const std::set<std::string>& usedLower, const std::string& base, bool keepExt) {
  if (!usedLower.count(text::toLower(base))) return base;
  std::string head = base, ext;
  if (keepExt)
    if (const auto k = extPos(base); k != std::string::npos) {
      head = base.substr(0, k);
      ext = base.substr(k);
    }
  for (int n = 2;; ++n) {
    const std::string cand = head + "-" + std::to_string(n) + ext;
    if (!usedLower.count(text::toLower(cand))) return cand;
  }
}

std::string attachmentDir(const std::string& projectDir, const std::string& issueId) {
  return joinPath(joinPath(projectDir, kAttachDir), safeName(issueId, kMaxFolderNameUnits, "issue"));
}

std::string fitFileName(const std::string& dir, const std::string& fileName, std::size_t maxPath) {
  const long long budget = static_cast<long long>(maxPath) - static_cast<long long>(utf16Units(dir)) - 1;
  if (budget < 1) return "";
  return shorten(fileName, static_cast<std::size_t>(budget));
}
}  // namespace paths

// ---------------------------------------------------------------- データフォルダの走査
namespace {
bool endsWith(const std::string& s, const std::string& suffix) {
  return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}
bool isReservedDir(const std::string& name) {
  const std::string low = text::toLower(name);
  return low == kDeletedDir || low == kBackupDir;
}
}  // namespace

Result<std::vector<FoundProject>> scanProjects(IFileStore& fs, const std::string& dir) {
  auto listed = fs.list(dir);
  if (!listed) return Error{listed.error()};
  struct Named {
    std::string name;
    std::int64_t mtime = 0;
  };
  std::map<std::string, Named> csvM, setM;  // 平置き。stem → ファイル
  std::vector<const FileInfo*> dirs;
  const std::string ext(kSettingsExt);
  for (const auto& f : *listed) {
    if (f.isDir) {
      dirs.push_back(&f);
      continue;
    }
    const std::string low = text::toLower(f.name);
    if (low.size() > ext.size() && endsWith(low, ext)) setM[f.name.substr(0, f.name.size() - ext.size())] = {f.name, f.mtime};
    else if (low.size() > 4 && endsWith(low, ".csv")) csvM[f.name.substr(0, f.name.size() - 4)] = {f.name, f.mtime};
  }
  std::vector<FoundProject> out;
  for (const auto& [stem, c] : csvM) {
    FoundProject p;
    p.layout = Layout::Flat;
    p.stem = stem;
    p.csvPath = joinPath(dir, c.name);
    const auto it = setM.find(stem);
    p.settingsPath = joinPath(dir, it == setM.end() ? stem + ext : it->second.name);
    p.mtime = std::max(c.mtime, it == setM.end() ? std::int64_t{0} : it->second.mtime);
    out.push_back(std::move(p));
  }
  // 1段下のフォルダ。project.csv があればそれ、なければ（取り込み用の入口として）名前の先頭の *.csv
  for (const FileInfo* d : dirs) {
    if (d->name.empty() || d->name[0] == '.' || isReservedDir(d->name)) continue;
    const std::string sub = joinPath(dir, d->name);
    auto in = fs.list(sub);
    if (!in) continue;
    const FileInfo *proj = nullptr, *set = nullptr;
    std::vector<const FileInfo*> others;
    for (const auto& g : *in) {
      if (g.isDir) continue;
      const std::string low = text::toLower(g.name);
      if (low == kProjectCsv) proj = &g;
      else if (low == kProjectSettings) set = &g;
      else if (low.size() > 4 && endsWith(low, ".csv")) others.push_back(&g);
    }
    std::sort(others.begin(), others.end(), [](const FileInfo* a, const FileInfo* b) { return text::toLower(a->name) < text::toLower(b->name); });
    const FileInfo* csv = proj ? proj : (others.empty() ? nullptr : others.front());
    if (!csv) continue;  // CSV のないフォルダはプロジェクトではない
    FoundProject p;
    p.layout = Layout::Folder;
    p.stem = d->name;
    p.csvOnlyFolder = proj == nullptr;
    p.csvPath = joinPath(sub, csv->name);
    p.settingsPath = joinPath(sub, set ? set->name : std::string(kProjectSettings));
    p.mtime = std::max(csv->mtime, set ? set->mtime : std::int64_t{0});
    out.push_back(std::move(p));
  }
  std::stable_sort(out.begin(), out.end(), [](const FoundProject& a, const FoundProject& b) {
    if (a.stem != b.stem) return a.stem < b.stem;
    return a.layout == Layout::Folder && b.layout == Layout::Flat;
  });
  return out;
}

// ---------------------------------------------------------------- Workspace
Workspace::Workspace(IFileStore& fs, const IClock& clock, std::string dataDir)
    : fs_(fs), clock_(clock), dir_(std::move(dataDir)) {}

ProjectEntry* Workspace::find(const std::string& id) {
  for (auto& e : entries_)
    if (e.id() == id) return &e;
  return nullptr;
}
const ProjectEntry* Workspace::find(const std::string& id) const {
  for (const auto& e : entries_)
    if (e.id() == id) return &e;
  return nullptr;
}

std::string Workspace::add(ProjectRecord rec, bool dirty) {
  if (rec.settings.id.empty() || find(rec.settings.id)) rec.settings.id = newProjectId(clock_);
  while (find(rec.settings.id)) rec.settings.id = newProjectId(clock_) + "x";
  ProjectEntry e;
  e.rec = std::move(rec);
  e.dirty = dirty;
  entries_.push_back(std::move(e));
  return entries_.back().id();
}

void Workspace::update(const std::string& id, ProjectRecord rec) {
  ProjectEntry* e = find(id);
  if (!e || frozen_.count(id)) return;
  const bool changed = signature(rec) != signature(e->rec);
  e->rec = std::move(rec);
  if (changed) e->dirty = true;
}

void Workspace::updateSettings(const std::string& id, ProjectSettings settings) {
  ProjectEntry* e = find(id);
  if (!e || frozen_.count(id)) return;
  ProjectRecord next = e->rec;  // csv も写るが、ここでは変えない
  next.settings = std::move(settings);
  const bool changed = settingsToJson(next) != settingsToJson(e->rec);
  e->rec = std::move(next);
  if (changed) e->settingsDirty = true;
}

bool Workspace::hasDirty() const {
  return std::any_of(entries_.begin(), entries_.end(), [](const ProjectEntry& e) { return e.dirty || e.settingsDirty; });
}

void Workspace::setFrozen(const std::string& id, bool frozen) {
  if (frozen) frozen_.insert(id);
  else frozen_.erase(id);
}

void Workspace::discardUnsaved(const std::string& id) {
  ProjectEntry* e = find(id);
  if (!e) return;
  e->dirty = false;
  e->settingsDirty = false;
  e->mtime = 0;  // フォルダのファイルの方が新しい扱いにして、次の sync() で読み直す
}

std::vector<std::string> Workspace::externallyChangedDirty() {
  std::vector<std::string> out;
  for (const auto& e : entries_) {
    if ((!e.dirty && !e.settingsDirty) || e.stem.empty() || frozen_.count(e.id())) continue;
    std::int64_t m = 0;
    if (auto a = fs_.stat(csvPathOf(e))) m = std::max(m, a->mtime);
    if (auto b = fs_.stat(settingsPathOf(e))) m = std::max(m, b->mtime);
    if (m > e.mtime) out.push_back(e.id());
  }
  return out;
}

std::string Workspace::signature(const ProjectRecord& rec) const { return rec.csv + "\n" + dumpJs(settingsToJson(rec)); }

std::string Workspace::safeStem(const std::string& name) { return paths::safeName(name, paths::kMaxFolderNameUnits, "project"); }

bool Workspace::stemTaken(const std::string& stem, const std::string& exceptId) const {
  const std::string low = text::toLower(stem);
  if (low == kDeletedDir || low == kBackupDir) return true;  // 予約された名前
  if (fileStems_.count(low)) return true;
  for (const auto& e : entries_)
    if (e.id() != exceptId && !e.stem.empty() && text::toLower(e.stem) == low) return true;
  // 手で置かれたフォルダ・ファイルも避ける
  return fs_.stat(joinPath(dir_, stem)).has_value() || fs_.stat(joinPath(dir_, stem + ".csv")).has_value();
}

std::string Workspace::freeStem(const std::string& name, const std::string& id) const {
  const std::string base = safeStem(name);
  std::string s = base;
  for (int n = 2; stemTaken(s, id); ++n) s = base + "-" + std::to_string(n);
  return s;
}

std::string Workspace::projectDir(const ProjectEntry& e) const {
  if (e.layout != Layout::Folder || e.stem.empty()) return "";
  return joinPath(dir_, e.stem);
}
std::string Workspace::csvPathOf(const ProjectEntry& e) const {
  return e.layout == Layout::Folder ? joinPath(projectDir(e), kProjectCsv) : joinPath(dir_, e.stem + ".csv");
}
std::string Workspace::settingsPathOf(const ProjectEntry& e) const {
  return e.layout == Layout::Folder ? joinPath(projectDir(e), kProjectSettings) : joinPath(dir_, e.stem + kSettingsExt);
}
std::string Workspace::backupDirOf(const ProjectEntry& e) const {
  return e.layout == Layout::Folder ? joinPath(projectDir(e), kProjectBackupDir) : joinPath(dir_, kBackupDir);
}

Status Workspace::backupOnce(const ProjectEntry& e) {
  const std::string path = csvPathOf(e);
  if (backedUp_.count(path)) return okStatus();
  auto st = fs_.stat(path);
  if (st && st->size > 0) {
    auto old = fs_.read(path);
    if (!old) return Error{old.error()};
    const std::string bdir = backupDirOf(e);
    if (auto s = fs_.makeDirs(bdir); !s) return s;
    if (auto s = fs_.writeAtomic(joinPath(bdir, e.stem + "." + clock_.stamp() + ".csv"), *old); !s) return s;
    pruneBackups(bdir);
  }
  backedUp_.insert(path);
  return okStatus();
}

void Workspace::pruneBackups(const std::string& bdir) {
  auto l = fs_.list(bdir);
  if (!l) return;
  std::vector<FileInfo> files;
  for (auto& f : *l)
    if (!f.isDir) files.push_back(f);
  if (files.size() <= kMaxBackups) return;
  // 古い順（時刻 → 名前）
  std::sort(files.begin(), files.end(), [](const FileInfo& a, const FileInfo& b) {
    return a.mtime != b.mtime ? a.mtime < b.mtime : a.name < b.name;
  });
  for (std::size_t i = 0; i + kMaxBackups < files.size(); ++i) (void)fs_.remove(joinPath(bdir, files[i].name));
}

Status Workspace::writeEntry(ProjectEntry& e) {
  if (frozen_.count(e.id())) return Error{"読み取り専用で開いているため、書き込みません"};
  if (e.stem.empty()) {
    e.stem = freeStem(e.rec.settings.name, e.id());
    e.layout = Layout::Folder;  // 新しく書くものはフォルダ構成
  }
  if (auto s = fs_.makeDirs(e.layout == Layout::Folder ? projectDir(e) : dir_); !s) return s;
  if (auto s = backupOnce(e); !s) return s;
  const std::string sig = signature(e.rec);
  if (writeGuard_ && !writeGuard_(e)) return Error{"ロックを確かめられないため、書き込みを見合わせました"};
  if (auto s = fs_.writeAtomic(csvPathOf(e), e.rec.csv); !s) return s;
  wroteInLastFlush_.insert(e.id());
  if (writeGuard_ && !writeGuard_(e)) return Error{"ロックを確かめられないため、書き込みを見合わせました"};  // CSV を書いたあとにも確かめる
  if (auto s = writeSettingsData(e); !s) return s;
  std::int64_t m = 0;
  if (auto a = fs_.stat(csvPathOf(e))) m = std::max(m, a->mtime);
  if (auto b = fs_.stat(settingsPathOf(e))) m = std::max(m, b->mtime);
  e.mtime = m;
  e.lastSig = sig;
  e.dirty = false;
  e.settingsDirty = false;
  fileStems_.insert(text::toLower(e.stem));
  return okStatus();
}

Status Workspace::writeSettingsData(ProjectEntry& e) {
  Json settings = settingsToJson(e.rec);
  std::string previous;
  if (auto old = fs_.read(settingsPathOf(e))) {
    auto manifest = Json::parse(*old, nullptr, false);
    if (manifest.is_object() && manifest.contains("personalFile") && manifest["personalFile"].is_string())
      previous = manifest["personalFile"].get<std::string>();
  }
  if (e.layout == Layout::Folder && !e.rec.settings.personal.empty()) {
    const std::string payload = dumpJs(e.rec.settings.personal);
    const std::string hash = sha256Hex(payload);
    const std::string folder = joinPath(projectDir(e), "personal-reports");
    if (auto status = fs_.makeDirs(folder); !status) return status;
    const std::string path = joinPath(folder, hash + ".json");
    auto existing = fs_.read(path);
    if (!existing || sha256Hex(*existing) != hash) {
      if (writeGuard_ && !writeGuard_(e)) return Error{"読み取り専用です"};
      if (auto status = fs_.writeAtomic(path, payload); !status) return status;
      wroteInLastFlush_.insert(e.id());
    }
    settings.erase("personal");
    settings["personalFile"] = hash;
  }
  if (writeGuard_ && !writeGuard_(e)) return Error{"読み取り専用です"};
  if (auto status = fs_.writeAtomic(settingsPathOf(e), dumpJs(settings)); !status) return status;
  wroteInLastFlush_.insert(e.id());
  // 新しいマニフェストの確定後にのみ、直前の全量データを片付ける。履歴は新しいデータにも全件含まれる。
  if (e.layout == Layout::Folder && isSha256Hex(previous) && settings.value("personalFile", "") != previous &&
      (!writeGuard_ || writeGuard_(e)))
    (void)fs_.remove(joinPath(joinPath(projectDir(e), "personal-reports"), previous + ".json"));
  return okStatus();
}

Status Workspace::writeSettingsOnly(ProjectEntry& e) {
  if (frozen_.count(e.id())) return Error{"読み取り専用で開いているため、書き込みません"};
  if (e.stem.empty()) return writeEntry(e);  // まだ書いていない（CSV もない）ものは全部書く
  const std::string sig = signature(e.rec);
  if (writeGuard_ && !writeGuard_(e)) return Error{"ロックを確かめられないため、書き込みを見合わせました"};
  if (auto s = writeSettingsData(e); !s) return s;
  std::int64_t m = 0;
  if (auto a = fs_.stat(csvPathOf(e))) m = std::max(m, a->mtime);
  if (auto b = fs_.stat(settingsPathOf(e))) m = std::max(m, b->mtime);
  e.mtime = m;
  e.lastSig = sig;
  e.settingsDirty = false;
  return okStatus();
}

Status Workspace::flush() {
  wroteInLastFlush_.clear();
  for (auto& e : entries_) {
    if (!e.dirty && !e.settingsDirty) continue;
    if (frozen_.count(e.id())) continue;
    if (!e.stem.empty() && e.lastSig == signature(e.rec)) {
      e.dirty = false;  // 内容が変わっていなければ書かない
      e.settingsDirty = false;
      continue;
    }
    if (auto s = e.dirty ? writeEntry(e) : writeSettingsOnly(e); !s) return s;
  }
  return okStatus();
}

Result<Workspace::FileRead> Workspace::readFiles(const FoundProject& f) {
  FileRead r;
  if (auto st = fs_.stat(f.csvPath); st && st->size > kMaxCsvBytes) {
    r.tooBig = true;
    return r;
  }
  auto c = fs_.read(f.csvPath);
  if (!c) return Error{c.error()};
  r.csv = encoding::decodeText(*c).text;
  r.settings = Json(nullptr);
  if (auto s = fs_.read(f.settingsPath)) {
    try {
      r.settings = Json::parse(encoding::decodeText(*s).text);
    } catch (const std::exception&) {
      r.settings = Json(nullptr);
    }
  }
  if (r.settings.is_object() && r.settings.contains("personalFile")) {
    const Json& ref = r.settings["personalFile"];
    if (f.layout != Layout::Folder || !ref.is_string() || !isSha256Hex(ref.get<std::string>()))
      return Error{"個人報告の参照が不正です"};
    const std::string hash = ref.get<std::string>();
    const std::string folder = f.settingsPath.substr(0, f.settingsPath.find_last_of('/'));
    auto payload = fs_.read(joinPath(joinPath(folder, "personal-reports"), hash + ".json"));
    if (!payload || sha256Hex(*payload) != hash) return Error{"個人報告ファイルを読めません。元のファイルを保持します"};
    Json data = Json::parse(*payload, nullptr, false);
    if (!data.is_object()) return Error{"個人報告データが不正です"};
    r.settings["personal"] = std::move(data);
    r.settings.erase("personalFile");
  }
  return r;
}

std::optional<ProjectRecord> Workspace::recFromFile(const FileRead& r, const std::string& stem, const ProjectRecord* base) {
  if (r.tooBig) return std::nullopt;
  if (!validateCsv(r.csv)) return std::nullopt;
  const Json& s = r.settings.is_object() ? r.settings : Json::object();
  ProjectRecord rec = recordFromSettingsJson(s, r.csv, stem, clock_);
  if (base) {
    // 設定ファイルに欠けている項目は手元の内容を使う
    const auto has = [&](const char* k) { return s.contains(k); };
    if (!has("id") || s["id"] == "") rec.settings.id = base->settings.id;
    if (!has("name") || s["name"] == "") rec.settings.name = base->settings.name;
    if (!has("key")) rec.settings.key = base->settings.key;
    if (!has("base") || s["base"] == "") rec.settings.baseDate = base->settings.baseDate;
    if (!has("hol")) rec.settings.holidays = base->settings.holidays;
    if (!has("scn") || s["scn"] == "") rec.settings.scenario = base->settings.scenario;
    if (!has("scenarios")) rec.scenarios = base->scenarios;
    if (!has("ui")) rec.settings.ui = base->settings.ui;
    if (!has("res")) rec.settings.resource = base->settings.resource;
  }
  return rec;
}

namespace {
// 対応づけの鍵。平置きの foo.csv とフォルダの foo/ は別のもの
std::string keyOf(Layout l, const std::string& stem) { return std::string(l == Layout::Flat ? "L:" : "F:") + text::toLower(stem); }
}  // namespace

Result<SyncReport> Workspace::sync() {
  SyncReport sum;
  if (auto s = fs_.makeDirs(dir_); !s) return Error{s.error()};
  auto scanned = scanProjects(fs_, dir_);
  if (!scanned) return Error{scanned.error()};
  const std::vector<FoundProject>& files = *scanned;
  fileStems_.clear();
  for (const auto& f : files) fileStems_.insert(text::toLower(f.stem));

  // 1) 対応づけ: 記録したフォルダ（ファイル）名 → settings の id
  std::map<std::string, const FoundProject*> byKey;
  for (const auto& f : files) byKey[keyOf(f.layout, f.stem)] = &f;
  std::vector<std::pair<std::string, const FoundProject*>> pairs;
  std::set<std::string> paired, usedKey;
  for (auto& e : entries_) {
    if (e.stem.empty()) continue;
    auto it = byKey.find(keyOf(e.layout, e.stem));
    if (it == byKey.end()) continue;
    pairs.emplace_back(e.id(), it->second);
    paired.insert(e.id());
    usedKey.insert(keyOf(it->second->layout, it->second->stem));
  }
  std::map<std::string, FileRead> cache;
  const auto rd = [&](const FoundProject& f) -> const FileRead* {
    const std::string k = keyOf(f.layout, f.stem);
    auto it = cache.find(k);
    if (it != cache.end()) return &it->second;
    auto r = readFiles(f);
    if (!r) return nullptr;
    return &cache.emplace(k, std::move(r).value()).first->second;
  };
  std::vector<const FoundProject*> fresh;
  for (const auto& f : files) {
    if (usedKey.count(keyOf(f.layout, f.stem))) continue;
    const FileRead* r = rd(f);
    if (!r) {
      sum.bad.push_back(f.stem);
      continue;
    }
    std::string sid;
    if (r->settings.is_object() && r->settings.contains("id") && r->settings["id"].is_string())
      sid = r->settings["id"].get<std::string>();
    ProjectEntry* local = sid.empty() ? nullptr : find(sid);
    if (local && !paired.count(sid)) {
      local->stem = f.stem;  // フォルダ名・ファイル名を変えられていても同じプロジェクト
      local->layout = f.layout;
      pairs.emplace_back(sid, &f);
      paired.insert(sid);
      usedKey.insert(keyOf(f.layout, f.stem));
    } else {
      fresh.push_back(&f);
    }
  }
  // 2) フォルダにだけあるものは、新しいプロジェクトとして追加
  std::set<std::string> entranceIds;
  for (const FoundProject* f : fresh) {
    const FileRead* r = rd(*f);
    auto rec = r ? recFromFile(*r, f->stem, nullptr) : std::nullopt;
    if (!rec) {
      sum.bad.push_back(f->stem);
      continue;
    }
    const std::string id = add(std::move(*rec), false);
    ProjectEntry* e = find(id);
    e->stem = f->stem;
    e->layout = f->layout;
    sum.added++;
    if (sum.firstAddedId.empty()) sum.firstAddedId = id;
    if (f->csvOnlyFolder) {
      // CSV だけを置いたフォルダ: project.csv と設定をそのフォルダに作る（元の CSV には触れない）。下の 4) で書く
      e->dirty = true;
      entranceIds.insert(id);
      continue;
    }
    e->mtime = f->mtime;
    e->lastSig = signature(e->rec);
    paired.insert(id);
  }
  // 3) 対応づいたもの: 新しい方を採る。両方変わっていたら、フォルダの版を別プロジェクトとして残す
  std::vector<std::string> toWrite;
  for (const auto& [id, f] : pairs) {
    ProjectEntry* e = find(id);
    if (!e) continue;
    const bool fileNew = f->mtime > e->mtime;
    if (fileNew && !e->dirty) {
      const FileRead* r = rd(*f);
      auto rec = r ? recFromFile(*r, f->stem, &e->rec) : std::nullopt;
      if (!rec) {
        sum.bad.push_back(f->stem);
        continue;
      }
      rec->settings.id = id;
      e->rec = std::move(*rec);
      e->mtime = f->mtime;
      e->lastSig = signature(e->rec);
      e->settingsDirty = false;  // ファイルの版を採る（設定だけの未保存の変更は、開き直したときの記録などでやり直す）
      sum.loaded++;
      sum.reloadedIds.push_back(id);
    } else if (e->dirty) {
      if (fileNew) {
        const FileRead* r = rd(*f);
        auto rec = r ? recFromFile(*r, f->stem, &e->rec) : std::nullopt;
        if (rec) {
          rec->settings.id.clear();
          rec->settings.name += "（フォルダの版）";
          toWrite.push_back(add(std::move(*rec), true));
          sum.kept++;
        }
      }
      toWrite.push_back(id);
    }
  }
  // 4) 手元にだけあるものはフォルダへ書き出す
  for (auto& e : entries_)
    if (!paired.count(e.id()) && !frozen_.count(e.id())) toWrite.push_back(e.id());  // 読み取り専用のものは、フォルダから消えていても書き戻さない
  for (const auto& id : toWrite) {
    ProjectEntry* e = find(id);
    if (!e) continue;
    if (auto s = writeEntry(*e); !s) return Error{s.error()};
    if (!entranceIds.count(id)) sum.wrote++;
  }
  return sum;
}

Status Workspace::remove(const std::string& id) {
  auto it = std::find_if(entries_.begin(), entries_.end(), [&](const ProjectEntry& e) { return e.id() == id; });
  if (it == entries_.end()) return Error{"プロジェクトが見つかりません"};
  if (frozen_.count(id)) return Error{"読み取り専用で開いているため、削除できません"};
  if (!it->stem.empty()) {
    const std::string ddir = joinPath(dir_, kDeletedDir);
    const std::string t = clock_.stamp();
    if (it->layout == Layout::Folder) {
      // フォルダごと（添付も）移す。同じ日時のものがあれば -2 …
      if (fs_.stat(projectDir(*it))) {
        // 編集中のロックはプロジェクトの中身ではない。削除済みのフォルダに持ち込まない
        (void)fs_.remove(lockPathIn(projectDir(*it)));
        if (auto s = fs_.makeDirs(ddir); !s) return s;
        std::string name = it->stem + "." + t;
        for (int n = 2; fs_.stat(joinPath(ddir, name)); ++n) name = it->stem + "." + t + "-" + std::to_string(n);
        if (auto s = fs_.move(projectDir(*it), joinPath(ddir, name)); !s) return s;
      }
    } else {
      if (auto s = fs_.makeDirs(ddir); !s) return s;
      for (const std::string& ext : {std::string(".csv"), std::string(kSettingsExt)}) {
        const std::string src = joinPath(dir_, it->stem + ext);
        if (!fs_.stat(src)) continue;
        auto data = fs_.read(src);
        if (!data) return Error{data.error()};
        if (auto s = fs_.writeAtomic(joinPath(ddir, it->stem + "." + t + ext), *data); !s) return s;
        if (auto s = fs_.remove(src); !s) return s;
      }
    }
    fileStems_.erase(text::toLower(it->stem));
  }
  entries_.erase(it);
  return okStatus();
}

std::vector<std::string> Workspace::deletedNames() const {
  std::vector<std::string> v;
  auto l = fs_.list(joinPath(dir_, kDeletedDir));
  if (!l) return v;
  for (const auto& f : *l)
    if (f.isDir) v.push_back(f.name);
  std::sort(v.begin(), v.end());
  return v;
}

Result<std::string> Workspace::restoreDeleted(const std::string& deletedName) {
  if (deletedName.empty() || deletedName == "." || deletedName == ".." || deletedName.find('/') != std::string::npos ||
      deletedName.find('\\') != std::string::npos)
    return Error{"名前が正しくありません"};
  const std::string src = joinPath(joinPath(dir_, kDeletedDir), deletedName);
  const auto st = fs_.stat(src);
  if (!st || !st->isDir) return Error{"「" + std::string(kDeletedDir) + "」に見つかりません: " + deletedName};
  // 「<プロジェクト>.<日時>」の日時を外した名前で戻す（使われていれば -2 …）
  const auto k = deletedName.rfind('.');
  const std::string stem = freeStem(k == std::string::npos || k == 0 ? deletedName : deletedName.substr(0, k), "");
  if (auto s = fs_.move(src, joinPath(dir_, stem)); !s) return Error{s.error()};
  fileStems_.insert(text::toLower(stem));
  return stem;
}

int Workspace::legacyCount() const {
  return static_cast<int>(std::count_if(entries_.begin(), entries_.end(),
                                        [](const ProjectEntry& e) { return e.layout == Layout::Flat && !e.stem.empty(); }));
}

Result<Workspace::MigrateReport> Workspace::migrateLegacy() {
  MigrateReport rep;
  if (legacyCount() == 0) return rep;
  // 未保存の編集を平置きに書いてから移す（失われない）
  if (auto s = flush(); !s) return Error{s.error()};

  // 1) 先に全部を控える。ここで失敗したら何も動かさない
  std::string bdir = joinPath(joinPath(dir_, kBackupDir), "移行前." + clock_.stamp());
  for (int n = 2; fs_.stat(bdir); ++n) bdir = joinPath(joinPath(dir_, kBackupDir), "移行前." + clock_.stamp() + "-" + std::to_string(n));
  if (auto s = fs_.makeDirs(bdir); !s) return Error{s.error()};
  for (const auto& e : entries_) {
    if (e.layout != Layout::Flat || e.stem.empty()) continue;
    for (const std::string& path : {csvPathOf(e), settingsPathOf(e)}) {
      if (!fs_.stat(path)) continue;
      auto data = fs_.read(path);
      if (!data) return Error{data.error()};
      if (auto s = fs_.writeAtomic(joinPath(bdir, path.substr(path.rfind('/') + 1)), *data); !s) return Error{s.error()};
    }
  }
  rep.backupDir = bdir;

  // 2) 1つずつ写す。写せたことを確かめてから平置きのファイルを消す
  std::set<std::string> usedFolders;  // 小文字
  if (auto l = fs_.list(dir_))
    for (const auto& f : *l)
      if (f.isDir) usedFolders.insert(text::toLower(f.name));
  usedFolders.insert(text::toLower(kDeletedDir));
  usedFolders.insert(text::toLower(kBackupDir));
  for (const auto& e : entries_)
    if (e.layout == Layout::Folder && !e.stem.empty()) usedFolders.insert(text::toLower(e.stem));

  for (auto& e : entries_) {
    if (e.layout != Layout::Flat || e.stem.empty()) continue;
    const std::string oldStem = e.stem;
    const std::string flatCsv = csvPathOf(e), flatSet = settingsPathOf(e);
    const auto fail = [&]() { rep.failed.push_back(oldStem); };
    auto csv = fs_.read(flatCsv);
    if (!csv) {
      fail();
      continue;
    }
    std::string settings;
    if (fs_.stat(flatSet)) {
      auto s = fs_.read(flatSet);
      if (!s) {
        fail();
        continue;
      }
      settings = std::move(s).value();
    } else {
      settings = dumpJs(settingsToJson(e.rec));  // 設定ファイルがなかったものは、id を固定するために作る
    }
    const std::string newStem = paths::uniqueName(usedFolders, oldStem);
    const std::string fdir = joinPath(dir_, newStem);
    const std::string newCsv = joinPath(fdir, kProjectCsv), newSet = joinPath(fdir, kProjectSettings);
    const auto rollback = [&]() {
      (void)fs_.remove(newCsv);
      (void)fs_.remove(newSet);
      (void)fs_.remove(fdir);
    };
    if (!fs_.makeDirs(fdir) || !fs_.writeAtomic(newCsv, *csv) || !fs_.writeAtomic(newSet, settings)) {
      rollback();
      fail();
      continue;
    }
    const auto backCsv = fs_.read(newCsv), backSet = fs_.read(newSet);
    if (!backCsv || *backCsv != *csv || !backSet || *backSet != settings) {
      rollback();
      fail();
      continue;
    }
    if (!fs_.remove(flatCsv)) {  // 消せなければ（他のアプリが開いている）、重複しないよう写しを戻す
      rollback();
      fail();
      continue;
    }
    if (fs_.stat(flatSet)) (void)fs_.remove(flatSet);
    usedFolders.insert(text::toLower(newStem));
    fileStems_.insert(text::toLower(newStem));
    e.stem = newStem;
    e.layout = Layout::Folder;
    std::int64_t m = 0;
    if (auto a = fs_.stat(newCsv)) m = std::max(m, a->mtime);
    if (auto b = fs_.stat(newSet)) m = std::max(m, b->mtime);
    e.mtime = m;
    rep.moved++;
  }
  return rep;
}

std::string defaultDataDir(const std::string& exeDir) { return joinPath(joinPath(exeDir, "data"), "projects"); }

DataDirChoice chooseDataDir(IFileStore& fs, const std::string& argDir, const std::string& envDir,
                            const std::string& prefDir, const std::string& exeDir, const std::string& fallbackDir) {
  if (!argDir.empty()) return {argDir, false, DataDirSource::Arg};
  if (!envDir.empty()) return {envDir, false, DataDirSource::Env};
  DataDirChoice c;
  if (!prefDir.empty()) {
    const std::string d = resolveDataDir(prefDir, exeDir);
    if (fs.probeWritable(d)) return {d, false, DataDirSource::Prefs};
    c.prefsDirUnavailable = true;
  }
  const std::string def = defaultDataDir(exeDir);
  if (fs.probeWritable(def)) {
    c.dir = def;
    c.source = DataDirSource::Default;
  } else {
    c.dir = fallbackDir;
    c.usedFallback = true;
    c.source = DataDirSource::Fallback;
  }
  return c;
}

namespace {
bool isAbsolutePath(const std::string& p) {
  return (!p.empty() && (p[0] == '/' || p[0] == '\\')) || (p.size() >= 2 && p[1] == ':');
}
// 比べるための形: 区切りを / に、末尾の / を除き、ASCII を小文字に（Windows のパスは大文字小文字を区別しない）
std::string pathKey(const std::string& p) {
  std::string k = text::replaceAll(p, "\\", "/");
  while (k.size() > 1 && k.back() == '/') k.pop_back();
  for (char& ch : k)
    if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
  return k;
}
}  // namespace

std::string storableDataDir(const std::string& dir, const std::string& exeDir) {
  if (exeDir.empty()) return dir;
  const std::string d = pathKey(dir), e = pathKey(exeDir);
  if (d == e) return ".";
  if (d.size() > e.size() && d.compare(0, e.size(), e) == 0 && d[e.size()] == '/') {
    // 元の文字（大文字小文字）を残し、区切りは / にする
    std::string rest = text::replaceAll(dir, "\\", "/");
    while (rest.size() > 1 && rest.back() == '/') rest.pop_back();
    return rest.substr(e.size() + 1);
  }
  return dir;
}

std::string resolveDataDir(const std::string& stored, const std::string& exeDir) {
  if (stored.empty() || isAbsolutePath(stored)) return stored;
  if (stored == ".") return exeDir;
  return joinPath(exeDir, stored);
}

Prefs loadPrefs(IFileStore& fs, const std::string& path) {
  auto r = fs.read(path);
  if (!r) return {};
  try {
    return prefsFromJson(Json::parse(encoding::decodeText(*r).text));
  } catch (const std::exception&) {
    return {};
  }
}

bool savePrefs(IFileStore& fs, const std::string& path, const Prefs& p) {
  return fs.writeAtomic(path, dumpJs(prefsToJson(p))).ok();
}

}  // namespace roadmap
