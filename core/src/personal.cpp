#include "roadmap/personal.hpp"
#include <cmath>
#include <algorithm>
#include "roadmap/text.hpp"
#include <set>
namespace roadmap {
static void normalize(Json& out) {
  if (!out.is_object()) out = Json::object();
  for (const char* key : {"current", "members", "reports", "drafts"})
    if (!out.contains(key) || !out[key].is_object()) out[key] = Json::object();
  if (!out.contains("history") || !out["history"].is_array()) out["history"] = Json::array();
}
Json personalFromJson(const Json& source) {
  Json out = source;
  normalize(out);
  return out;
}
std::optional<double> personalPercent(const Json& data, const std::string& id) {
  if (!data.is_object() || !data.contains("current") || !data["current"].is_object()) return {};
  const auto it = data["current"].find(id);
  if (it == data["current"].end() || !it->is_object() || !it->contains("percent") || !(*it)["percent"].is_number()) return {};
  const double p = (*it)["percent"].get<double>();
  return std::isfinite(p) && p >= 0 && p <= 100 ? std::optional<double>(p) : std::nullopt;
}
bool setPersonalPercent(Json& data, const std::string& id, std::optional<double> value) {
  if (id.empty() || (value && (!std::isfinite(*value) || *value < 0 || *value > 100))) return false;
  if (personalPercent(data, id) == value) return false;
  normalize(data);
  auto& item = data["current"][id];
  if (!item.is_object()) item = Json::object();
  item["percent"] = value ? Json(*value) : Json(nullptr);
  return true;
}
void recordPersonal(Json& data, const std::vector<Issue>& issues, const IClock& clock) {
  normalize(data);
  std::set<std::string> present;
  for (const auto& issue : issues) {
    present.insert(issue.id);
    const auto percent = personalPercent(data, issue.id);
    Json state{{"id", issue.id}, {"name", issue.summary}, {"assignee", issue.assignee}, {"status", issue.status},
      {"resolved", issue.resolved ? Json(fmtIso(*issue.resolved)) : Json(nullptr)},
      {"percent", percent ? Json(*percent) : Json(nullptr)}};
    auto& current = data["current"][issue.id];
    // recorded は進捗入力直後にも変更前と比較できる写し。
    if (!current.is_object()) current = Json::object();
    if ((current.contains("deleted") && current["deleted"] == true) || !current.contains("recorded") || current["recorded"] != state) {
      Json event = state;
      event["at"] = clock.nowDT();
      event["utc"] = clock.isoUtc();
      data["history"].push_back(event);
      current = state;
      current["at"] = clock.nowDT();
      current["recorded"] = state;
    }
  }
  for (auto it = data["current"].begin(); it != data["current"].end(); ++it) {
    if (present.count(it.key()) || !it->is_object() || (it->contains("deleted") && (*it)["deleted"] == true)) continue;
    (*it)["deleted"] = true;
    data["history"].push_back(Json{{"id", it.key()}, {"deleted", true}, {"at", clock.nowDT()}, {"utc", clock.isoUtc()}});
  }
}
}

namespace roadmap {
namespace {
std::string str(const Json& item, const char* key) {
  return item.is_object() && item.contains(key) && item[key].is_string() ? item[key].get<std::string>() : "";
}
std::string nextId(const Json& object, const char* prefix) {
  std::size_t n = object.size() + 1;
  while (object.contains(std::string(prefix) + std::to_string(n))) ++n;
  return std::string(prefix) + std::to_string(n);
}
bool matchesMember(const Json& member, const std::string& name) {
  if (str(member, "name") == name) return true;
  if (member.is_object() && member.contains("aliases") && member["aliases"].is_array())
    for (const auto& alias : member["aliases"]) if (alias == name) return true;
  return false;
}
}
std::string addPersonalMember(Json& data, const std::string& raw) {
  const std::string name = text::trim(raw);
  if (name.empty()) return {};
  normalize(data);
  for (auto it = data["members"].begin(); it != data["members"].end(); ++it)
    if (matchesMember(*it, name)) return it.key();
  const std::string id = nextId(data["members"], "m");
  data["members"][id] = Json{{"name", name}, {"enabled", true}, {"aliases", Json::array({name})}};
  return id;
}
void syncPersonalMembers(Json& data, const std::vector<Issue>& issues) {
  normalize(data);
  for (const auto& issue : issues) if (!issue.assignee.empty()) addPersonalMember(data, issue.assignee);
}
bool renamePersonalMember(Json& data, const std::string& id, const std::string& raw) {
  const std::string name = text::trim(raw);
  normalize(data);
  if (name.empty() || !data["members"].contains(id)) return false;
  for (auto it = data["members"].begin(); it != data["members"].end(); ++it)
    if (it.key() != id && matchesMember(*it, name)) return false;
  auto& member = data["members"][id];
  if (!member.is_object()) return false;
  if (!member.contains("aliases") || !member["aliases"].is_array()) member["aliases"] = Json::array();
  member["aliases"].push_back(str(member, "name"));
  member["aliases"].push_back(name);
  member["name"] = name;
  return true;
}
std::optional<double> plannedPercent(const BaselineItem& item, Day day, const Calendar& cal) {
  if (!item.start || !item.finish || *item.finish < *item.start || item.parent) return {};
  if (day < *item.start) return 0;
  if (day >= *item.finish) return 100;
  const int total = cal.nd(*item.start, *item.finish);
  if (total <= 0) return {};
  return std::clamp(100.0 * cal.nd(*item.start, day) / total, 0.0, 100.0);
}
int progressTone(std::optional<double> actual, std::optional<double> planned, double margin) {
  if (!actual || !planned) return 0;
  const double difference = *actual - *planned;
  return difference < -std::max(0.0, margin) ? -1 : difference > std::max(0.0, margin) ? 1 : 0;
}
const Json* previousPersonalReport(const Json& data, const std::string& member, Day before) {
  if (!data.is_object() || !data.contains("reports") || !data["reports"].is_object()) return nullptr;
  const Json* best = nullptr;
  std::string bestDate, bestAt;
  for (const auto& report : data["reports"]) {
    const std::string date = str(report, "date"), at = str(report, "savedAt");
    const auto day = parseIso(date);
    if (str(report, "memberId") == member && day && *day < before &&
        (!best || date > bestDate || (date == bestDate && at >= bestAt))) {
      best = &report; bestDate = date; bestAt = at;
    }
  }
  return best;
}
std::string personalDraftKey(const std::string& member, Day date) { return member + ":" + fmtIso(date); }
Result<Json> makePersonalDraft(const Json& data, const std::vector<Issue>& issues, const std::string& member,
                              Day date, Day from, Day to, const Baseline* baseline, const Calendar& cal,
                              const std::string& legacyNotes) {
  if (from > to || to > date) return Error{"対象期間は報告日以前で、開始日から終了日の順にしてください"};
  if (!data.is_object() || !data.contains("members") || !data["members"].contains(member)) return Error{"対象者を選んでください"};
  const auto& person = data["members"][member];
  const auto* previous = previousPersonalReport(data, member, date);
  Json report{{"memberId", member}, {"memberName", str(person, "name")}, {"date", fmtIso(date)},
    {"from", fmtIso(from)}, {"to", fmtIso(to)}, {"baselineId", baseline ? baseline->id : ""},
    {"baselineName", baseline ? baseline->name : ""}, {"tasks", Json::array()}, {"extra", Json::array()},
    {"blockers", ""}, {"next", ""}};
  if (!previous && !legacyNotes.empty()) report["extra"].push_back(Json{{"name", "以前の担当者別所感"}, {"status", "In Progress"}, {"comment", legacyNotes}});
  std::set<std::string> parents;
  for (const auto& task : issues) if (!task.parent.empty()) parents.insert(task.parent);
  for (const auto& task : issues) {
    if (parents.count(task.id) || !matchesMember(person, task.assignee)) continue;
    const Cat cat = statusCat(task.status);
    if (cat == Cat::Todo || (cat == Cat::Done && (!task.resolved || *task.resolved < from || *task.resolved > to))) continue;
    auto actual = cat == Cat::Done ? std::optional<double>(100) : personalPercent(data, task.id);
    std::optional<double> planned;
    Json plan = nullptr;
    if (baseline) for (const auto& item : baseline->items) if (item.id == task.id) {
      planned = plannedPercent(item, to, cal);
      plan = Json{{"start", fmtIso(item.start)}, {"finish", fmtIso(item.finish)}};
      break;
    }
    Json prev = nullptr;
    if (previous && previous->contains("tasks") && (*previous)["tasks"].is_array())
      for (const auto& item : (*previous)["tasks"]) if (str(item, "id") == task.id && item.contains("actual")) prev = item["actual"];
    report["tasks"].push_back(Json{{"id", task.id}, {"name", task.summary}, {"assignee", task.assignee},
      {"status", task.status}, {"actual", actual ? Json(*actual) : Json(nullptr)},
      {"planned", planned ? Json(*planned) : Json(nullptr)}, {"previous", prev}, {"plan", plan},
      {"resolved", fmtIso(task.resolved)}, {"comment", ""}});
  }
  return report;
}
Result<std::string> savePersonalReport(Json& data, Json draft, const IClock& clock) {
  const auto date = parseIso(str(draft, "date")), from = parseIso(str(draft, "from")), to = parseIso(str(draft, "to"));
  if (!date || !from || !to || *from > *to || *to > *date || str(draft, "memberId").empty() ||
      !draft.contains("tasks") || !draft["tasks"].is_array()) return Error{"個人報告の期間・対象者が不正です"};
  normalize(data);
  const std::string id = nextId(data["reports"], "r");
  draft["id"] = id;
  draft["savedAt"] = clock.nowDT();
  draft["savedUtc"] = clock.isoUtc();
  data["reports"][id] = std::move(draft);
  return id;
}
}
