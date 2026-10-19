// データモデル（SPEC 4章）
#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "roadmap/calendar.hpp"

namespace roadmap {

using Json = nlohmann::ordered_json;

inline constexpr const char* kStdScenario = "標準";

struct Comment {
  std::string at;  // "YYYY-MM-DDTHH:MM"（空なら日時なし）
  std::string who;
  std::string text;
  bool operator==(const Comment&) const = default;
};

// シナリオ名 → 期間(週)。出現順（JS のオブジェクトのキー順）を保つ
class ScenarioWeeks {
 public:
  const double* find(std::string_view name) const {
    for (const auto& [k, v] : items_)
      if (k == name) return &v;
    return nullptr;
  }
  void set(const std::string& name, double w) {
    for (auto& [k, v] : items_)
      if (k == name) {
        v = w;
        return;
      }
    items_.emplace_back(name, w);
  }
  bool erase(std::string_view name) {
    for (auto it = items_.begin(); it != items_.end(); ++it)
      if (it->first == name) {
        items_.erase(it);
        return true;
      }
    return false;
  }
  bool empty() const { return items_.empty(); }
  const std::vector<std::pair<std::string, double>>& items() const { return items_; }
  bool operator==(const ScenarioWeeks&) const = default;

 private:
  std::vector<std::pair<std::string, double>> items_;
};

struct Issue {
  std::string id;
  std::string key;
  std::string type = "Task";
  std::string summary;
  std::string parent;
  std::string prio;
  std::string status;
  std::string assignee;
  OptDay start, due, deadline, resolved, notBefore;
  std::optional<double> durW, estW, remW;
  ScenarioWeeks alt;
  std::vector<std::string> labels;
  std::string desc;
  std::vector<std::string> blocks;  // この課題が「ブロックする」課題の id（= 後続）
  std::vector<Comment> comments;
  std::string epicName;
  std::vector<std::string> extra;  // 未知の列の値（meta.extraHeaders と同じ並び）
  bool operator==(const Issue&) const = default;
};

struct ProjectMeta {
  bool hasDeadlineCol = false;
  std::vector<std::string> scenarios;     // 「標準」以外のシナリオ名（出現順）
  std::vector<std::string> extraHeaders;  // 未知の列の見出し
  bool operator==(const ProjectMeta&) const = default;
};

struct ResourceLimit {
  bool on = false;
  int cap = 1;
  bool operator==(const ResourceLimit&) const = default;
};

// CSV に入らない設定（付録B.1 の settings.json と対応）
struct ProjectSettings {
  std::string id, name, key;
  std::string baseDate;  // "YYYY-MM-DD"
  std::string holidays;  // 休業日テキスト
  std::string scenario = kStdScenario;
  ResourceLimit resource;
  Json ui = Json::object();     // 表示状態。未知のキーも保持
  Json unknown = Json::object();  // settings.json の未知のキー（書き戻す）
};

struct ProjectData {
  std::vector<Issue> issues;
  ProjectMeta meta;
  ProjectSettings settings;
};

// ---- 判定（SPEC 4.3〜4.5）----
struct TagName {
  std::string tag;
  std::string name;
};
// summary が "[tag] name" の形なら分ける（/^\[([^\]]+)\]\s*(.*)$/ を手書きで）
TagName tagOf(std::string_view summary);

bool isEpicType(std::string_view type);
bool isMilestoneText(std::string_view s);  // "milestone" / "マイルストーン" を含む（大文字小文字不問）
inline bool isEpic(const Issue& o) { return isEpicType(o.type); }
bool isMilestone(const Issue& o);  // 種別かラベルが Milestone

enum class Cat { Todo, Prog, Done };
Cat statusCat(std::string_view status);
const char* catName(Cat c);  // "todo" / "prog" / "done"

inline const char* const kPriorities[] = {"Highest", "High", "Medium", "Low", "Lowest"};
int prioRank(std::string_view prio);  // 0〜4、不明は 2

// 新規課題の id（数字だけの id の最大値 + 1）
std::string nextIssueId(const std::vector<Issue>& issues);

}  // namespace roadmap
