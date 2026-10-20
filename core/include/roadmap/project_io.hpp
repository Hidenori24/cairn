// プロジェクトの保存形式（付録B）: settings.json、共有・バックアップJSON、roadmap_prefs.json
#pragma once

#include <optional>
#include <random>
#include <string>
#include <vector>

#include "roadmap/clock.hpp"
#include "roadmap/csv.hpp"
#include "roadmap/model.hpp"
#include "roadmap/result.hpp"
#include "roadmap/templates.hpp"

namespace roadmap {

// JSON.stringify(…, null, 1) 相当（インデント1スペース。整数の double は小数点なし）
std::string dumpJs(const Json& j);
// 整数とみなせる double を整数に直す（JS の数値の書き方に合わせる）
Json normalizeNumbers(const Json& j);

Json defaultUi();
// 欠けているキーを既定値で補う（未知のキーは残す）
Json withDefaultUi(const Json& ui);

// JS版と同じ形式の id（"p" + 時刻の36進数 + 乱数4文字）
std::string newProjectId(const IClock& clock);

// 1プロジェクト = 設定 + CSV（アプリ形式）
struct ProjectRecord {
  ProjectSettings settings;
  std::string csv;
  std::vector<std::string> scenarios;  // settings.json の scenarios（CSVの @列と和集合をとる）
};

// settings.json（キー順: app, version, id, name, key, base, hol, scn, scenarios, res, ui, その他）
Json settingsToJson(const ProjectRecord& rec);
// 読む。欠けたキーは既定値（name がなければ stem、id がなければ新しく振る）
ProjectRecord recordFromSettingsJson(const Json& j, const std::string& csvText, const std::string& stem,
                                     const IClock& clock);

// ProjectRecord → ProjectData（CSV を取り込む）。CSV が読めない・計算できないときはエラー
struct Hydrated {
  ProjectData data;
  std::vector<std::string> warns;
};
Result<Hydrated> hydrate(const ProjectRecord& rec);
// 取り込む前の検査（読めないCSVは取り込まない・上書きしない。SPEC 8.5）
Status validateCsv(const std::string& csvText);
// ProjectData → ProjectRecord
ProjectRecord recordOf(const ProjectData& d);

ProjectRecord newRecord(const std::string& name, const std::string& key, const std::string& csvText,
                        const std::string& base, const IClock& clock);

// ---- 共有・バックアップJSON（B.2）----
Json recordToShareJson(const ProjectRecord& rec);  // { v, id, name, key, base, hol, scn, scenarios, ui, res, csv }
std::string exportShareJson(const std::vector<ProjectRecord>& recs, const std::vector<Template>& userTemplates,
                            const IClock& clock);
struct ParsedBackup {
  std::vector<ProjectRecord> projects;
  std::vector<Template> templates;
};
Result<ParsedBackup> parseBackup(const std::string& text, const IClock& clock);

// ---- roadmap_prefs.json（8.6・B.3）----
struct WindowPlacement {
  int x = 100, y = 100, w = 1440, h = 900;
  bool maximized = false;
};
struct Prefs {
  std::string theme = "auto";     // auto / light / dark
  std::string density = "normal";  // normal / compact
  std::string me = "自分";
  std::string currentProject;
  std::vector<Template> templates;  // 利用者のテンプレート
  WindowPlacement window;
  bool hintDismissed = false;
  // アプリの設定（端末ごと）
  std::string dataDir;       // 保存先。空 = 既定（exe の隣の data/projects）。exe のフォルダの中なら相対パス
  int uiScale = 100;         // 文字の大きさ（%、80〜200）
  int dimPercent = 30;       // 上の絞り込みで薄くするときの不透明度（%、10〜80）
  bool confirmDelete = false;  // 課題を削除する前に確かめる
  bool autoBaseToday = false;  // プロジェクトを開いたら基準日を今日に合わせる（起動中、プロジェクトごとに1回）
  Json unknown = Json::object();
};
Prefs prefsFromJson(const Json& j);
Json prefsToJson(const Prefs& p);

}  // namespace roadmap
