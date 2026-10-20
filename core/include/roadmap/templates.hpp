// テンプレート（子タスクの雛形。SPEC 9.4・付録A）
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "roadmap/engine.hpp"
#include "roadmap/model.hpp"

namespace roadmap {

struct TemplateItem {
  std::string n;                 // 名前（{名前} は追加先の表示名に置き換える）
  std::optional<double> w;       // 週（なしは1）
  int lv = 0;                    // 入れ子の深さ（0=直下の子）
  std::vector<int> after;        // 先行する項目の番号
  std::string t;                 // 種別（省略可）
  std::vector<std::string> lab;  // ラベル
  std::string d;                 // 説明
};

struct Template {
  std::string id;  // "b:…"（組み込み）/ "u:…"（利用者）
  bool builtin = false;
  std::string name;
  std::string desc;
  std::vector<TemplateItem> items;
};

const std::vector<Template>& builtinTemplates();

Json templateToJson(const Template& t);
std::optional<Template> templateFromJson(const Json& j);  // id・name・items が必要
std::vector<Template> templatesFromJson(const Json& arr);
Json templatesToJson(const std::vector<Template>& v);

// 課題の配下（子・孫…）を雛形の項目にする。番号タグは外し、先行関係は項目の番号に直す
std::vector<TemplateItem> templateItemsFrom(const Tree& T, const std::vector<Issue>& issues, int id, bool withWeeks);

}  // namespace roadmap
