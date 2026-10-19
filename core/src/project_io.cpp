#include "roadmap/project_io.hpp"

#include <algorithm>
#include <cmath>

#include "roadmap/engine.hpp"
#include "roadmap/text.hpp"

namespace roadmap {

Json normalizeNumbers(const Json& j) {
  if (j.is_number_float()) {
    const double v = j.get<double>();
    if (std::isfinite(v) && std::floor(v) == v && std::abs(v) < 9e15) return static_cast<long long>(v);
    return j;
  }
  if (j.is_object()) {
    Json o = Json::object();
    for (auto it = j.begin(); it != j.end(); ++it) o[it.key()] = normalizeNumbers(it.value());
    return o;
  }
  if (j.is_array()) {
    Json a = Json::array();
    for (const auto& x : j) a.push_back(normalizeNumbers(x));
    return a;
  }
  return j;
}

std::string dumpJs(const Json& j) {
  return normalizeNumbers(j).dump(1, ' ', false, Json::error_handler_t::replace);
}

Json defaultUi() {
  Json ui = Json::object();
  ui["collapsed"] = Json::object();
  ui["hide"] = Json::object();
  ui["zoom"] = "week";
  ui["level"] = 0;
  ui["view"] = "gantt";
  ui["lane"] = "none";
  ui["colorBy"] = "root";
  ui["cols"] = Json{{"asg", true}, {"status", true}, {"dur", true}, {"float", true}};
  ui["nameW"] = 300;
  return ui;
}

Json withDefaultUi(const Json& in) {
  Json ui = defaultUi();
  if (in.is_object())
    for (auto it = in.begin(); it != in.end(); ++it) ui[it.key()] = it.value();
  if (!ui["cols"].is_object()) ui["cols"] = defaultUi()["cols"];
  if (!ui["zoom"].is_string()) ui["zoom"] = "week";
  if (!ui["collapsed"].is_object()) ui["collapsed"] = Json::object();
  if (!ui["hide"].is_object()) ui["hide"] = Json::object();
  return ui;
}

std::string newProjectId(const IClock& clock) {
  static std::mt19937_64 rng{std::random_device{}()};
  const auto b36 = [](unsigned long long v) {
    std::string s;
    do {
      s.insert(s.begin(), "0123456789abcdefghijklmnopqrstuvwxyz"[v % 36]);
      v /= 36;
    } while (v);
    return s;
  };
  std::string r;
  for (int i = 0; i < 4; ++i) r += "0123456789abcdefghijklmnopqrstuvwxyz"[rng() % 36];
  return "p" + b36(static_cast<unsigned long long>(clock.epochMs())) + r;
}

namespace {
const char* const kKnownSettingsKeys[] = {"app", "version", "id", "name", "key", "base", "hol", "scn", "scenarios", "res", "ui"};
bool isKnownKey(const std::string& k) {
  for (const char* x : kKnownSettingsKeys)
    if (k == x) return true;
  return false;
}
std::string str(const Json& j, const char* k) {
  if (j.is_object() && j.contains(k) && j[k].is_string()) return j[k].get<std::string>();
  return "";
}
bool hasStr(const Json& j, const char* k) { return j.is_object() && j.contains(k) && j[k].is_string(); }
Json resJson(const ResourceLimit& r) { return Json{{"on", r.on}, {"cap", r.cap}}; }
ResourceLimit resFrom(const Json& j) {
  ResourceLimit r;
  if (!j.is_object()) return r;
  if (j.contains("on") && j["on"].is_boolean()) r.on = j["on"].get<bool>();
  if (j.contains("cap") && j["cap"].is_number()) r.cap = std::max(1, static_cast<int>(j["cap"].get<double>()));
  return r;
}
}  // namespace

Json settingsToJson(const ProjectRecord& rec) {
  const ProjectSettings& s = rec.settings;
  Json j = Json::object();
  j["app"] = "regroadmap";
  j["version"] = 3;
  j["id"] = s.id;
  j["name"] = s.name;
  j["key"] = s.key;
  j["base"] = s.baseDate;
  j["hol"] = s.holidays;
  j["scn"] = s.scenario;
  j["scenarios"] = rec.scenarios;
  j["res"] = resJson(s.resource);
  j["ui"] = s.ui;
  if (s.unknown.is_object())
    for (auto it = s.unknown.begin(); it != s.unknown.end(); ++it)
      if (!isKnownKey(it.key())) j[it.key()] = it.value();
  return j;
}

ProjectRecord recordFromSettingsJson(const Json& s, const std::string& csvText, const std::string& stem,
                                     const IClock& clock) {
  ProjectRecord r;
  r.csv = csvText;
  ProjectSettings& p = r.settings;
  p.id = str(s, "id");
  if (p.id.empty()) p.id = newProjectId(clock);
  p.name = str(s, "name");
  if (p.name.empty()) p.name = stem;
  p.key = str(s, "key");
  p.baseDate = str(s, "base");
  if (p.baseDate.empty()) p.baseDate = clock.todayIso();
  p.holidays = hasStr(s, "hol") ? s["hol"].get<std::string>() : defaultHolidaysText();
  p.scenario = str(s, "scn");
  if (p.scenario.empty()) p.scenario = kStdScenario;
  if (s.is_object() && s.contains("scenarios") && s["scenarios"].is_array())
    for (const auto& x : s["scenarios"])
      if (x.is_string()) r.scenarios.push_back(x.get<std::string>());
  p.resource = resFrom(s.is_object() && s.contains("res") ? s["res"] : Json());
  p.ui = withDefaultUi(s.is_object() && s.contains("ui") ? s["ui"] : Json());
  if (s.is_object())
    for (auto it = s.begin(); it != s.end(); ++it)
      if (!isKnownKey(it.key())) p.unknown[it.key()] = it.value();
  return r;
}

Status validateCsv(const std::string& csvText) {
  if (csv::parse(csvText).rows.empty()) return okStatus();
  auto imp = csv::importCsv(csvText);
  if (!imp) return Error{imp.error()};
  auto t = buildTree(imp->issues, imp->meta);
  if (!t) return Error{t.error()};
  return okStatus();
}

Result<Hydrated> hydrate(const ProjectRecord& rec) {
  Hydrated h;
  h.data.settings = rec.settings;
  h.data.settings.ui = withDefaultUi(rec.settings.ui);
  if (!csv::parse(rec.csv).rows.empty()) {
    auto imp = csv::importCsv(rec.csv);
    if (!imp) return Error{imp.error()};
    h.data.issues = std::move(imp->issues);
    h.data.meta = std::move(imp->meta);
    h.warns = std::move(imp->warns);
  } else {
    h.data.meta.hasDeadlineCol = true;
  }
  for (const auto& s : rec.scenarios)
    if (std::find(h.data.meta.scenarios.begin(), h.data.meta.scenarios.end(), s) == h.data.meta.scenarios.end())
      h.data.meta.scenarios.push_back(s);
  return h;
}

ProjectRecord recordOf(const ProjectData& d) {
  ProjectRecord r;
  r.settings = d.settings;
  r.scenarios = d.meta.scenarios;
  r.csv = csv::exportCsv(d.issues, d.meta, {});
  return r;
}

ProjectRecord newRecord(const std::string& name, const std::string& key, const std::string& csvText,
                        const std::string& base, const IClock& clock) {
  ProjectRecord r;
  r.settings.id = newProjectId(clock);
  r.settings.name = name;
  r.settings.key = key;
  r.settings.baseDate = base.empty() ? clock.todayIso() : base;
  r.settings.holidays = defaultHolidaysText();
  r.settings.scenario = kStdScenario;
  r.settings.ui = defaultUi();
  r.csv = csvText;
  return r;
}

Json recordToShareJson(const ProjectRecord& rec) {
  const ProjectSettings& s = rec.settings;
  Json j = Json::object();
  j["v"] = 3;
  j["id"] = s.id;
  j["name"] = s.name;
  j["key"] = s.key;
  j["base"] = s.baseDate;
  j["hol"] = s.holidays;
  j["scn"] = s.scenario;
  j["scenarios"] = rec.scenarios;
  j["ui"] = s.ui;
  j["res"] = resJson(s.resource);
  j["csv"] = rec.csv;
  return j;
}

std::string exportShareJson(const std::vector<ProjectRecord>& recs, const std::vector<Template>& userTemplates,
                            const IClock& clock) {
  Json j = Json::object();
  j["app"] = "regroadmap";
  j["version"] = 3;
  j["exported"] = clock.isoUtc();
  Json arr = Json::array();
  for (const auto& r : recs) arr.push_back(recordToShareJson(r));
  j["projects"] = std::move(arr);
  j["templates"] = templatesToJson(userTemplates);
  return dumpJs(j);
}

Result<ParsedBackup> parseBackup(const std::string& textIn, const IClock& clock) {
  Json o;
  try {
    o = Json::parse(textIn);
  } catch (const std::exception&) {
    return Error{"JSONを読み取れません"};
  }
  if (!o.is_object() || str(o, "app") != "regroadmap" || !o.contains("projects") || !o["projects"].is_array())
    return Error{"このアプリのバックアップ・共有ファイルではありません"};
  ParsedBackup b;
  for (const auto& p : o["projects"]) {
    if (!p.is_object() || !hasStr(p, "csv") || p["csv"].get<std::string>().empty()) continue;
    ProjectRecord r = recordFromSettingsJson(p, p["csv"].get<std::string>(), "取り込んだプロジェクト", clock);
    r.settings.id = str(p, "id");  // 空なら呼び出し側で振る
    b.projects.push_back(std::move(r));
  }
  if (b.projects.empty()) return Error{"プロジェクトが含まれていません"};
  if (o.contains("templates") && o["templates"].is_array())
    for (const auto& t : o["templates"]) {
      auto tpl = templateFromJson(t);
      if (tpl && tpl->id.rfind("b:", 0) != 0) {
        tpl->builtin = false;
        b.templates.push_back(std::move(*tpl));
      }
    }
  return b;
}

Prefs prefsFromJson(const Json& j) {
  Prefs p;
  if (!j.is_object()) return p;
  if (hasStr(j, "theme")) p.theme = j["theme"].get<std::string>();
  if (hasStr(j, "density")) p.density = j["density"].get<std::string>();
  if (hasStr(j, "me")) p.me = j["me"].get<std::string>();
  if (hasStr(j, "currentProject")) p.currentProject = j["currentProject"].get<std::string>();
  if (j.contains("templates")) p.templates = templatesFromJson(j["templates"]);
  if (j.contains("window") && j["window"].is_object()) {
    const auto& w = j["window"];
    const auto gi = [&](const char* k, int& out) {
      if (w.contains(k) && w[k].is_number()) out = static_cast<int>(w[k].get<double>());
    };
    gi("x", p.window.x);
    gi("y", p.window.y);
    gi("w", p.window.w);
    gi("h", p.window.h);
    if (w.contains("maximized") && w["maximized"].is_boolean()) p.window.maximized = w["maximized"].get<bool>();
  }
  if (j.contains("hintDismissed") && j["hintDismissed"].is_boolean()) p.hintDismissed = j["hintDismissed"].get<bool>();
  for (auto it = j.begin(); it != j.end(); ++it) {
    const std::string& k = it.key();
    if (k != "theme" && k != "density" && k != "me" && k != "currentProject" && k != "templates" && k != "window" &&
        k != "hintDismissed")
      p.unknown[k] = it.value();
  }
  return p;
}

Json prefsToJson(const Prefs& p) {
  Json j = Json::object();
  j["theme"] = p.theme;
  j["density"] = p.density;
  j["me"] = p.me;
  j["currentProject"] = p.currentProject;
  j["templates"] = templatesToJson(p.templates);
  j["window"] = Json{{"x", p.window.x}, {"y", p.window.y}, {"w", p.window.w}, {"h", p.window.h}, {"maximized", p.window.maximized}};
  j["hintDismissed"] = p.hintDismissed;
  if (p.unknown.is_object())
    for (auto it = p.unknown.begin(); it != p.unknown.end(); ++it) j[it.key()] = it.value();
  return j;
}

}  // namespace roadmap
