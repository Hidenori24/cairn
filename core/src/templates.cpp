#include "roadmap/templates.hpp"

#include <algorithm>

namespace roadmap {

const std::vector<Template>& builtinTemplates() {
  static const std::vector<Template> kList = [] {
    const std::vector<std::string> ph{"期間仮置き"};
    const auto it = [&](const char* n, double w, std::vector<int> after) {
      return TemplateItem{n, w, 0, std::move(after), "", ph, ""};
    };
    return std::vector<Template>{
        {"b:doc", true, "外部認証向けの文書対応", "内部レビュー → 英訳 → 外部提出 → 外部指摘の対応",
         {it("内部レビューと指摘修正", 1, {}), it("英訳", 1, {0}), it("外部認証機関への提出", 0.2, {1}),
          it("外部指摘への対応とクローズ", 2, {2})}},
        {"b:eval", true, "評価の実施と報告", "評価方案 → 評価実施 → 報告書 → 提出と指摘クローズ",
         {it("評価方案の作成", 1, {}), it("評価の実施", 1, {0}), it("評価報告書の作成", 1, {1}),
          it("評価報告書の提出と指摘クローズ", 1, {2})}},
        {"b:review", true, "作成 → レビュー → 承認", "成果物をつくり、レビューして承認を得る基本形",
         {it("{名前}の作成", 2, {}), it("レビュー", 1, {0}), it("指摘の修正", 1, {1}), it("承認", 0.2, {2})}},
        {"b:publish", true, "公開用の原稿づくり", "原稿作成 → レビュー → 校了 → 公開（印刷）",
         {it("公開用原稿の作成", 2, {}), it("レビュー", 1, {0}), it("校了", 0.4, {1}), it("公開・印刷", 0.4, {2})}},
    };
  }();
  return kList;
}

namespace {
Json numJson(double v) {
  // JS の JSON.stringify と同じく、整数は小数点なしで書く
  if (v == static_cast<double>(static_cast<long long>(v)) && v > -1e15 && v < 1e15) return static_cast<long long>(v);
  return v;
}
}  // namespace

Json templateToJson(const Template& t) {
  Json j = Json::object();
  j["id"] = t.id;
  if (t.builtin) j["builtin"] = true;
  j["name"] = t.name;
  j["desc"] = t.desc;
  Json items = Json::array();
  for (const auto& it : t.items) {
    Json x = Json::object();
    x["n"] = it.n;
    x["w"] = it.w ? numJson(*it.w) : Json(nullptr);
    x["lv"] = it.lv;
    x["after"] = it.after;
    x["lab"] = it.lab;
    if (!it.t.empty()) x["t"] = it.t;
    if (!it.d.empty()) x["d"] = it.d;
    items.push_back(std::move(x));
  }
  j["items"] = std::move(items);
  return j;
}

std::optional<Template> templateFromJson(const Json& j) {
  if (!j.is_object() || !j.contains("id") || !j["id"].is_string() || !j.contains("name") || !j["name"].is_string() ||
      j["name"].get<std::string>().empty() || !j.contains("items") || !j["items"].is_array())
    return std::nullopt;
  Template t;
  t.id = j["id"].get<std::string>();
  if (t.id.empty()) return std::nullopt;
  t.builtin = j.value("builtin", false);
  t.name = j["name"].get<std::string>();
  if (j.contains("desc") && j["desc"].is_string()) t.desc = j["desc"].get<std::string>();
  for (const auto& x : j["items"]) {
    if (!x.is_object()) continue;
    TemplateItem it;
    if (x.contains("n") && x["n"].is_string()) it.n = x["n"].get<std::string>();
    if (x.contains("w") && x["w"].is_number()) it.w = x["w"].get<double>();
    if (x.contains("lv") && x["lv"].is_number()) it.lv = x["lv"].get<int>();
    if (x.contains("after") && x["after"].is_array())
      for (const auto& a : x["after"])
        if (a.is_number_integer()) it.after.push_back(a.get<int>());
    if (x.contains("t") && x["t"].is_string()) it.t = x["t"].get<std::string>();
    if (x.contains("lab") && x["lab"].is_array())
      for (const auto& l : x["lab"])
        if (l.is_string()) it.lab.push_back(l.get<std::string>());
    if (x.contains("d") && x["d"].is_string()) it.d = x["d"].get<std::string>();
    t.items.push_back(std::move(it));
  }
  return t;
}

std::vector<Template> templatesFromJson(const Json& arr) {
  std::vector<Template> out;
  if (!arr.is_array()) return out;
  for (const auto& j : arr)
    if (auto t = templateFromJson(j)) out.push_back(std::move(*t));
  return out;
}

Json templatesToJson(const std::vector<Template>& v) {
  Json a = Json::array();
  for (const auto& t : v) a.push_back(templateToJson(t));
  return a;
}

std::vector<TemplateItem> templateItemsFrom(const Tree& T, const std::vector<Issue>& issues, int id, bool withWeeks) {
  std::vector<char> under(T.n, 0);
  {
    std::vector<int> st{id};
    while (!st.empty()) {
      const int x = st.back();
      st.pop_back();
      for (int k : T.kids[x]) {
        under[k] = 1;
        st.push_back(k);
      }
    }
  }
  std::vector<int> ids;
  for (int x : T.dfs)
    if (under[x]) ids.push_back(x);
  std::vector<int> at(T.n, -1);
  for (std::size_t i = 0; i < ids.size(); ++i) at[ids[i]] = static_cast<int>(i);
  std::vector<TemplateItem> out;
  for (int x : ids) {
    const Issue& o = issues[x];
    const bool leaf = T.kids[x].empty();
    TemplateItem it;
    it.n = T.name[x];
    if (leaf && withWeeks) it.w = o.durW ? *o.durW : (o.estW ? *o.estW : 1.0);
    else if (leaf) it.w = 1.0;
    it.lv = T.depth[x] - T.depth[id] - 1;
    for (int m : ids) {
      const auto& b = issues[m].blocks;
      if (std::find(b.begin(), b.end(), T.ids[x]) != b.end()) it.after.push_back(at[m]);
    }
    it.lab = o.labels;
    if (isMilestoneText(o.type)) it.t = "Milestone";
    it.d = o.desc;
    out.push_back(std::move(it));
  }
  return out;
}

}  // namespace roadmap
