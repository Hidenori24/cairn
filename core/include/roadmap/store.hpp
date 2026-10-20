// データフォルダの読み書きと競合の解決（SPEC 8章）
//   <data>/プロジェクト名.csv / プロジェクト名.settings.json / _バックアップ/ / _削除済み/
#pragma once

#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "roadmap/clock.hpp"
#include "roadmap/filestore.hpp"
#include "roadmap/project_io.hpp"

namespace roadmap {

inline constexpr const char* kBackupDir = "_バックアップ";
inline constexpr const char* kDeletedDir = "_削除済み";
inline constexpr const char* kSettingsExt = ".settings.json";
inline constexpr std::size_t kMaxBackups = 30;
inline constexpr std::uint64_t kMaxCsvBytes = 50ull * 1024 * 1024;

struct ProjectEntry {
  ProjectRecord rec;
  std::string stem;        // 対応するファイル名（まだ書いていなければ空）
  std::int64_t mtime = 0;  // 最後に自分が書いた/読んだ時点の mtime
  bool dirty = false;      // 未保存の編集がある
  std::string lastSig;     // 最後に書いた内容（CSV + 設定JSON）
  const std::string& id() const { return rec.settings.id; }
};

struct SyncReport {
  int loaded = 0, added = 0, wrote = 0, kept = 0;
  std::vector<std::string> bad;          // 読めないCSV（stem）
  std::vector<std::string> reloadedIds;  // ファイルから読み直したプロジェクト
  std::string firstAddedId;
  std::string message(bool manual = false) const;  // 通知の文言（なければ空）
};

class Workspace {
 public:
  Workspace(IFileStore& fs, const IClock& clock, std::string dataDir);

  const std::string& dataDir() const { return dir_; }
  std::vector<ProjectEntry>& entries() { return entries_; }
  const std::vector<ProjectEntry>& entries() const { return entries_; }
  ProjectEntry* find(const std::string& id);
  const ProjectEntry* find(const std::string& id) const;

  // 追加する（id が重なれば新しく振る）。追加した id を返す
  std::string add(ProjectRecord rec, bool dirty = true);
  // 内容を差し替える。変わっていれば dirty にする
  void update(const std::string& id, ProjectRecord rec);
  bool hasDirty() const;

  // 未保存のものを書く。内容が前回と同じなら書かない
  Status flush();
  // 起動時・アクティブに戻ったとき（8.5）
  Result<SyncReport> sync();
  // 削除（_削除済み へ移す）
  Status remove(const std::string& id);

  static std::string safeStem(const std::string& name);
  std::string freeStem(const std::string& name, const std::string& id) const;
  std::string signature(const ProjectRecord& rec) const;

 private:
  Status writeEntry(ProjectEntry& e);
  Status backupOnce(const std::string& stem);
  void pruneBackups();
  struct FileRead {
    std::string csv;
    Json settings;  // null = なし・壊れている
    bool tooBig = false;
  };
  Result<FileRead> readFiles(const std::string& stem);
  std::optional<ProjectRecord> recFromFile(const FileRead& r, const std::string& stem, const ProjectRecord* base);

  IFileStore& fs_;
  const IClock& clock_;
  std::string dir_;
  std::vector<ProjectEntry> entries_;
  std::set<std::string> fileStems_;  // フォルダにある stem（小文字）
  std::set<std::string> backedUp_;   // このプロセスでバックアップ済みの stem
};

// データフォルダの決定（8.1）: --data > ROADMAP_DATA > アプリの設定で選んだ場所 > <exe>/data/projects
// （設定の場所・既定の場所に書けなければ fallback）
enum class DataDirSource { Arg, Env, Prefs, Default, Fallback };
struct DataDirChoice {
  std::string dir;
  bool usedFallback = false;
  DataDirSource source = DataDirSource::Default;
  bool prefsDirUnavailable = false;  // 設定で選んだ場所に書けなかった（外したドライブなど）
};
DataDirChoice chooseDataDir(IFileStore& fs, const std::string& argDir, const std::string& envDir,
                            const std::string& prefDir, const std::string& exeDir, const std::string& fallbackDir);
std::string defaultDataDir(const std::string& exeDir);
// prefs に覚える形（exe のフォルダの中なら相対パス。フォルダごと移しても壊れない）と、その逆
std::string storableDataDir(const std::string& dir, const std::string& exeDir);
std::string resolveDataDir(const std::string& stored, const std::string& exeDir);

// roadmap_prefs.json の読み書き（書けなければ黙って失敗）
Prefs loadPrefs(IFileStore& fs, const std::string& path);
bool savePrefs(IFileStore& fs, const std::string& path, const Prefs& p);

}  // namespace roadmap
