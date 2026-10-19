#include "roadmap/store.hpp"

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
  if (!e) return;
  const bool changed = signature(rec) != signature(e->rec);
  e->rec = std::move(rec);
  if (changed) e->dirty = true;
}

bool Workspace::hasDirty() const {
  return std::any_of(entries_.begin(), entries_.end(), [](const ProjectEntry& e) { return e.dirty; });
}

std::string Workspace::signature(const ProjectRecord& rec) const { return rec.csv + "\n" + dumpJs(settingsToJson(rec)); }

std::string Workspace::safeStem(const std::string& name) {
  std::string s;
  for (std::size_t i = 0; i < name.size();) {
    const std::size_t st = i;
    const char32_t c = text::decodeAt(name, i);
    if (c < 0x20 || c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
      s += '_';
    else
      s.append(name, st, i - st);
  }
  // 前後の . と空白を削る
  std::u32string u = text::toU32(s);
  std::size_t a = 0, b = u.size();
  while (a < b && (u[a] == '.' || text::isJsSpace(u[a]))) ++a;
  while (b > a && (u[b - 1] == '.' || text::isJsSpace(u[b - 1]))) --b;
  s = text::utf16Prefix(text::fromU32(u.substr(a, b - a)), 80);
  return s.empty() ? "project" : s;
}

std::string Workspace::freeStem(const std::string& name, const std::string& id) const {
  const std::string base = safeStem(name);
  std::set<std::string> used = fileStems_;
  for (const auto& e : entries_)
    if (e.id() != id && !e.stem.empty()) used.insert(text::toLower(e.stem));
  std::string s = base;
  for (int n = 2; used.count(text::toLower(s)); ++n) s = base + "-" + std::to_string(n);
  return s;
}

Status Workspace::backupOnce(const std::string& stem) {
  if (backedUp_.count(stem)) return okStatus();
  const std::string path = joinPath(dir_, stem + ".csv");
  auto st = fs_.stat(path);
  if (st && st->size > 0) {
    auto old = fs_.read(path);
    if (!old) return Error{old.error()};
    const std::string bdir = joinPath(dir_, kBackupDir);
    if (auto s = fs_.makeDirs(bdir); !s) return s;
    if (auto s = fs_.writeAtomic(joinPath(bdir, stem + "." + clock_.stamp() + ".csv"), *old); !s) return s;
    pruneBackups();
  }
  backedUp_.insert(stem);
  return okStatus();
}

void Workspace::pruneBackups() {
  const std::string bdir = joinPath(dir_, kBackupDir);
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
  if (e.stem.empty()) e.stem = freeStem(e.rec.settings.name, e.id());
  if (auto s = fs_.makeDirs(dir_); !s) return s;
  if (auto s = backupOnce(e.stem); !s) return s;
  const std::string sig = signature(e.rec);
  if (auto s = fs_.writeAtomic(joinPath(dir_, e.stem + ".csv"), e.rec.csv); !s) return s;
  if (auto s = fs_.writeAtomic(joinPath(dir_, e.stem + kSettingsExt), dumpJs(settingsToJson(e.rec))); !s) return s;
  std::int64_t m = 0;
  if (auto a = fs_.stat(joinPath(dir_, e.stem + ".csv"))) m = std::max(m, a->mtime);
  if (auto b = fs_.stat(joinPath(dir_, e.stem + kSettingsExt))) m = std::max(m, b->mtime);
  e.mtime = m;
  e.lastSig = sig;
  e.dirty = false;
  fileStems_.insert(text::toLower(e.stem));
  return okStatus();
}

Status Workspace::flush() {
  for (auto& e : entries_) {
    if (!e.dirty) continue;
    if (!e.stem.empty() && e.lastSig == signature(e.rec)) {
      e.dirty = false;  // 内容が変わっていなければ書かない
      continue;
    }
    if (auto s = writeEntry(e); !s) return s;
  }
  return okStatus();
}

Result<Workspace::FileRead> Workspace::readFiles(const std::string& stem) {
  FileRead r;
  const std::string csvPath = joinPath(dir_, stem + ".csv");
  if (auto st = fs_.stat(csvPath); st && st->size > kMaxCsvBytes) {
    r.tooBig = true;
    return r;
  }
  auto c = fs_.read(csvPath);
  if (!c) return Error{c.error()};
  r.csv = encoding::decodeText(*c).text;
  r.settings = Json(nullptr);
  if (auto s = fs_.read(joinPath(dir_, stem + kSettingsExt))) {
    try {
      r.settings = Json::parse(encoding::decodeText(*s).text);
    } catch (const std::exception&) {
      r.settings = Json(nullptr);
    }
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

Result<SyncReport> Workspace::sync() {
  SyncReport sum;
  if (auto s = fs_.makeDirs(dir_); !s) return Error{s.error()};
  auto listed = fs_.list(dir_);
  if (!listed) return Error{listed.error()};
  struct Ent {
    std::string stem;
    std::int64_t mtime;
  };
  std::map<std::string, std::int64_t> csvM, setM;
  for (const auto& f : *listed) {
    if (f.isDir) continue;
    const std::string low = text::toLower(f.name);
    const std::string ext(kSettingsExt);
    if (low.size() > ext.size() && low.compare(low.size() - ext.size(), ext.size(), ext) == 0)
      setM[f.name.substr(0, f.name.size() - ext.size())] = f.mtime;
    else if (low.size() > 4 && low.compare(low.size() - 4, 4, ".csv") == 0)
      csvM[f.name.substr(0, f.name.size() - 4)] = f.mtime;
  }
  std::vector<Ent> files;
  for (const auto& [stem, m] : csvM) {
    auto it = setM.find(stem);
    files.push_back({stem, std::max(m, it == setM.end() ? 0 : it->second)});
  }
  fileStems_.clear();
  for (const auto& f : files) fileStems_.insert(text::toLower(f.stem));

  // 1) 対応づけ: 記録したファイル名 → settings の id
  std::map<std::string, const Ent*> byStem;
  for (const auto& f : files) byStem[text::toLower(f.stem)] = &f;
  std::vector<std::pair<std::string, const Ent*>> pairs;
  std::set<std::string> paired, usedStem;
  for (auto& e : entries_) {
    if (e.stem.empty()) continue;
    auto it = byStem.find(text::toLower(e.stem));
    if (it == byStem.end()) continue;
    pairs.emplace_back(e.id(), it->second);
    paired.insert(e.id());
    usedStem.insert(it->second->stem);
  }
  std::map<std::string, FileRead> cache;
  const auto rd = [&](const std::string& stem) -> const FileRead* {
    auto it = cache.find(stem);
    if (it != cache.end()) return &it->second;
    auto r = readFiles(stem);
    if (!r) return nullptr;
    return &cache.emplace(stem, std::move(r).value()).first->second;
  };
  std::vector<const Ent*> fresh;
  for (const auto& f : files) {
    if (usedStem.count(f.stem)) continue;
    const FileRead* r = rd(f.stem);
    if (!r) {
      sum.bad.push_back(f.stem);
      continue;
    }
    std::string sid;
    if (r->settings.is_object() && r->settings.contains("id") && r->settings["id"].is_string())
      sid = r->settings["id"].get<std::string>();
    ProjectEntry* local = sid.empty() ? nullptr : find(sid);
    if (local && !paired.count(sid)) {
      local->stem = f.stem;
      pairs.emplace_back(sid, &f);
      paired.insert(sid);
      usedStem.insert(f.stem);
    } else {
      fresh.push_back(&f);
    }
  }
  // 2) フォルダにだけあるCSVは、新しいプロジェクトとして追加
  for (const Ent* f : fresh) {
    const FileRead* r = rd(f->stem);
    auto rec = r ? recFromFile(*r, f->stem, nullptr) : std::nullopt;
    if (!rec) {
      sum.bad.push_back(f->stem);
      continue;
    }
    const std::string id = add(std::move(*rec), false);
    ProjectEntry* e = find(id);
    e->stem = f->stem;
    e->mtime = f->mtime;
    e->lastSig = signature(e->rec);
    paired.insert(id);
    sum.added++;
    if (sum.firstAddedId.empty()) sum.firstAddedId = id;
  }
  // 3) 対応づいたもの: 新しい方を採る。両方変わっていたら、フォルダの版を別プロジェクトとして残す
  std::vector<std::string> toWrite;
  for (const auto& [id, f] : pairs) {
    ProjectEntry* e = find(id);
    if (!e) continue;
    const bool fileNew = f->mtime > e->mtime;
    if (fileNew && !e->dirty) {
      const FileRead* r = rd(f->stem);
      auto rec = r ? recFromFile(*r, f->stem, &e->rec) : std::nullopt;
      if (!rec) {
        sum.bad.push_back(f->stem);
        continue;
      }
      rec->settings.id = id;
      e->rec = std::move(*rec);
      e->mtime = f->mtime;
      e->lastSig = signature(e->rec);
      sum.loaded++;
      sum.reloadedIds.push_back(id);
    } else if (e->dirty) {
      if (fileNew) {
        const FileRead* r = rd(f->stem);
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
    if (!paired.count(e.id())) toWrite.push_back(e.id());
  for (const auto& id : toWrite) {
    ProjectEntry* e = find(id);
    if (!e) continue;
    if (auto s = writeEntry(*e); !s) return Error{s.error()};
    sum.wrote++;
  }
  return sum;
}

Status Workspace::remove(const std::string& id) {
  auto it = std::find_if(entries_.begin(), entries_.end(), [&](const ProjectEntry& e) { return e.id() == id; });
  if (it == entries_.end()) return Error{"プロジェクトが見つかりません"};
  if (!it->stem.empty()) {
    const std::string ddir = joinPath(dir_, kDeletedDir);
    if (auto s = fs_.makeDirs(ddir); !s) return s;
    const std::string t = clock_.stamp();
    for (const std::string& ext : {std::string(".csv"), std::string(kSettingsExt)}) {
      const std::string src = joinPath(dir_, it->stem + ext);
      if (!fs_.stat(src)) continue;
      auto data = fs_.read(src);
      if (!data) return Error{data.error()};
      if (auto s = fs_.writeAtomic(joinPath(ddir, it->stem + "." + t + ext), *data); !s) return s;
      if (auto s = fs_.remove(src); !s) return s;
    }
    fileStems_.erase(text::toLower(it->stem));
  }
  entries_.erase(it);
  return okStatus();
}

DataDirChoice chooseDataDir(IFileStore& fs, const std::string& argDir, const std::string& envDir,
                            const std::string& exeDir, const std::string& fallbackDir) {
  if (!argDir.empty()) return {argDir, false};
  if (!envDir.empty()) return {envDir, false};
  const std::string def = joinPath(joinPath(exeDir, "data"), "projects");
  if (fs.probeWritable(def)) return {def, false};
  return {fallbackDir, true};
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
