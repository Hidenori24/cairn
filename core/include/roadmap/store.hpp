// データフォルダの読み書きと競合の解決（SPEC 8章）
//   <data>/<プロジェクト>/project.csv / project.settings.json / 添付/<Issue Id>/ / バックアップ/
//   <data>/_削除済み/<プロジェクト>.<日時>/        （削除したプロジェクトをフォルダごと）
//   互換読み: <data>/プロジェクト名.csv / .settings.json / _バックアップ/（以前の平置き）
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

inline constexpr const char* kBackupDir = "_バックアップ";  // 平置き用（全プロジェクト共通）と、移行前の控え
inline constexpr const char* kDeletedDir = "_削除済み";
inline constexpr const char* kSettingsExt = ".settings.json";
inline constexpr const char* kProjectCsv = "project.csv";  // フォルダ構成の CSV（原本）
inline constexpr const char* kProjectSettings = "project.settings.json";
inline constexpr const char* kProjectBackupDir = "バックアップ";  // フォルダ構成: そのプロジェクトの分だけ
inline constexpr const char* kAttachDir = "添付";                  // フォルダ構成: 添付/<Issue Id>/（#19 で使う）
inline constexpr std::size_t kMaxBackups = 30;
inline constexpr std::uint64_t kMaxCsvBytes = 50ull * 1024 * 1024;

// プロジェクトの置き方。Folder = <data>/<stem>/project.csv（新しい構成）、Flat = <data>/<stem>.csv（以前の平置き）
enum class Layout { Folder, Flat };

// フォルダ名・添付の置き場所・パスの長さの規則（SPEC 8.2）。ファイルシステムには触れない純粋な関数
namespace paths {
// Windows の既定の上限（260 文字）に、一時ファイル（.tmpNNNNNN）の分の余裕を引いた長さ。UTF-16 の単位で数える
inline constexpr std::size_t kMaxPathUnits = 240;
inline constexpr std::size_t kMaxFolderNameUnits = 80;
inline constexpr std::size_t kMaxFileNameUnits = 200;

std::size_t utf16Units(const std::string& s);
// \ / : * ? " < > | と制御文字を _ に、前後の . と空白を削り、maxUnits（UTF-16）以内にする。空なら fallback
std::string safeName(const std::string& name, std::size_t maxUnits, const std::string& fallback);
// 添付のファイル名など。長ければ拡張子を残して切る
std::string safeFileName(const std::string& name);
// used（小文字）にある名前と重ならないようにする。重なれば -2、-3 … を付ける（keepExt なら拡張子の前に）
std::string uniqueName(const std::set<std::string>& usedLower, const std::string& base, bool keepExt = false);
// <プロジェクトのフォルダ>/添付/<Issue Id>（Issue Id はフォルダ名に使える形にする）
std::string attachmentDir(const std::string& projectDir, const std::string& issueId);
// dir に置いたときのパスが maxPath 単位に収まるよう、拡張子を残してファイル名を切る。入る余地がなければ空
std::string fitFileName(const std::string& dir, const std::string& fileName, std::size_t maxPath = kMaxPathUnits);
}  // namespace paths

// データフォルダにあるプロジェクトの見つかり方（sync と、保存先の候補の数え上げで使う）
struct FoundProject {
  Layout layout = Layout::Folder;
  std::string stem;          // フォルダ名（Flat ではファイル名の stem）
  std::string csvPath;       // 読む CSV
  std::string settingsPath;  // 設定の JSON（なくてもよい）
  std::int64_t mtime = 0;    // CSV と設定の新しい方
  bool csvOnlyFolder = false;  // project.csv がなく、別名の CSV だけを置いたフォルダ（取り込み用の入口）
};
// 直下の *.csv（平置き）と、1段下のフォルダ（project.csv を持つもの、または CSV を置いたもの）。
// _削除済み・_バックアップ・「.」で始まるフォルダは見ない。フォルダの中の添付などは見ない
Result<std::vector<FoundProject>> scanProjects(IFileStore& fs, const std::string& dir);

struct ProjectEntry {
  ProjectRecord rec;
  std::string stem;        // 対応するフォルダ名（Flat ではファイル名の stem。まだ書いていなければ空）
  Layout layout = Layout::Folder;
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
  // 削除（_削除済み へ移す。フォルダ構成ならフォルダごと、平置きなら CSV と設定のファイル）
  Status remove(const std::string& id);
  // _削除済み にあるフォルダの名前（<プロジェクト>.<日時>）。名前順
  std::vector<std::string> deletedNames() const;
  // _削除済み のフォルダをデータフォルダへ戻す（名前が使われていれば -2 …）。戻した先のフォルダ名を返す。
  // 取り込みは次の sync() で行う
  Result<std::string> restoreDeleted(const std::string& deletedName);

  // 平置きのまま読んでいるプロジェクトの数（フォルダ構成へ移せるもの）
  int legacyCount() const;
  struct MigrateReport {
    int moved = 0;
    std::string backupDir;            // 移す前の控え（_バックアップ/移行前.<日時>）。移すものがなければ空
    std::vector<std::string> failed;  // 移せなかったプロジェクト（平置きのまま残してある）
  };
  // 平置きをフォルダ構成へ移す。先に全部を _バックアップ/移行前.<日時>/ へ控え、バイト列は変えずに写し、
  // 写せたことを確かめてから平置きのファイルを消す。控えに失敗したら何も動かさない
  Result<MigrateReport> migrateLegacy();

  static std::string safeStem(const std::string& name);
  std::string freeStem(const std::string& name, const std::string& id) const;
  std::string signature(const ProjectRecord& rec) const;

  // 置き場所（Folder: <data>/<stem>/…、Flat: <data>/<stem>.…）
  std::string projectDir(const ProjectEntry& e) const;  // 添付などを置くフォルダ。Flat は空
  std::string csvPathOf(const ProjectEntry& e) const;
  std::string settingsPathOf(const ProjectEntry& e) const;

 private:
  Status writeEntry(ProjectEntry& e);
  Status backupOnce(const ProjectEntry& e);
  std::string backupDirOf(const ProjectEntry& e) const;
  void pruneBackups(const std::string& bdir);
  bool stemTaken(const std::string& stem, const std::string& exceptId) const;
  struct FileRead {
    std::string csv;
    Json settings;  // null = なし・壊れている
    bool tooBig = false;
  };
  Result<FileRead> readFiles(const FoundProject& f);
  std::optional<ProjectRecord> recFromFile(const FileRead& r, const std::string& stem, const ProjectRecord* base);

  IFileStore& fs_;
  const IClock& clock_;
  std::string dir_;
  std::vector<ProjectEntry> entries_;
  std::set<std::string> fileStems_;  // フォルダにある stem（小文字。平置きもフォルダ構成も）
  std::set<std::string> backedUp_;   // このプロセスでバックアップ済みの CSV のパス
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
