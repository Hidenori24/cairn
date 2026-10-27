#include "roadmap/personal.hpp"
#include <cmath>
#include <set>
namespace roadmap {
static void normalize(Json& out) {
  if (!out.is_object()) out = Json::object();
  for (const char* key : {"current", "members", "reports"})
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
    if (current.value("deleted", false) || !current.contains("recorded") || current["recorded"] != state) {
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
