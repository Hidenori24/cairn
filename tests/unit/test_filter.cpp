#include <doctest/doctest.h>

#include "roadmap/commands.hpp"
#include "roadmap/project_io.hpp"

using namespace roadmap;

namespace {
FixedClock clk(LocalTime{2026, 10, 19, 9, 0, 0});
Session make() {
  ProjectData d;
  auto imp = csv::importCsv(
      "Issue Id,Issue Type,Summary,Parent,Assignee,Status,Priority,Labels,Deadline,Duration (weeks)\n"
      "1,Epic,設計まわり,,,,,,2026-11-06,\n"
      "2,Task,画面,1,田中,To Do,High,UI,,1\n"
      "3,Task,DB,1,,In Progress,,,,4\n"
      "4,Epic,別件,,,,,,,\n"
      "5,Task,資料,4,佐藤,Done,Low,docs,,1\n"
      "6,Task,画面の試験,4,田中,To Do,Medium,,,1\n");
  if (!imp) FAIL(imp.error());
  d.issues = imp->issues;
  d.meta = imp->meta;
  d.settings.baseDate = "2026-10-19";
  d.settings.ui = defaultUi();
  return Session(d, &clk);
}
std::vector<std::string> ids(const Session& s, const Filter& f) {
  std::vector<std::string> v;
  for (const auto& r : s.visibleRows(f)) v.push_back(s.issues()[r.id].id);
  return v;
}
}  // namespace

TEST_CASE("filter: 絞り込みなし・折りたたみ・hide・level") {
  Session s = make();
  Filter f;
  CHECK(ids(s, f) == std::vector<std::string>{"1", "2", "3", "4", "5", "6"});
  s.setCollapsed("1", true);
  CHECK(ids(s, f) == std::vector<std::string>{"1", "4", "5", "6"});
  f.q = "画面";  // 絞り込み中は折りたたみを無視
  CHECK(ids(s, f) == std::vector<std::string>{"1", "2", "4", "6"});
  f = {};
  s.ui()["hide"]["4"] = true;
  CHECK(ids(s, f) == std::vector<std::string>{"1"});
  CHECK(s.visibleLeaves(f).size() == 2);  // 折りたたみは関係ない・hide は効く
  s.ui()["hide"] = Json::object();
  s.setCollapsed("1", false);
  s.ui()["level"] = 1;
  CHECK(ids(s, f) == std::vector<std::string>{"1", "4"});
}

TEST_CASE("filter: 検索語（祖先の一致・ID・ラベル・担当者・大文字小文字）") {
  Session s = make();
  Filter f;
  f.q = "設計";  // 親が一致 → 配下は全部
  CHECK(ids(s, f) == std::vector<std::string>{"1", "2", "3"});
  f.q = "ui";
  CHECK(ids(s, f) == std::vector<std::string>{"1", "2"});
  f.q = " 佐藤 ";
  CHECK(ids(s, f) == std::vector<std::string>{"4", "5"});
  f.q = "6";
  CHECK(ids(s, f) == std::vector<std::string>{"4", "6"});
}

TEST_CASE("filter: 担当・状況・優先度・flt の組み合わせ") {
  Session s = make();
  Filter f;
  f.asg = AsgFilter::Assigned;
  CHECK(ids(s, f) == std::vector<std::string>{"1", "2", "4", "5", "6"});
  f.asg = AsgFilter::Unassigned;
  CHECK(ids(s, f) == std::vector<std::string>{"1", "3"});
  f.asg = AsgFilter::Name;
  f.asgName = "田中";
  CHECK(ids(s, f) == std::vector<std::string>{"1", "2", "4", "6"});
  f.st = StFilter::Todo;
  CHECK(ids(s, f) == std::vector<std::string>{"1", "2", "4", "6"});
  f = {};
  f.st = StFilter::Open;
  CHECK(ids(s, f) == std::vector<std::string>{"1", "2", "3", "4", "6"});
  f.st = StFilter::Prog;
  CHECK(ids(s, f) == std::vector<std::string>{"1", "3"});
  f.st = StFilter::Done;
  CHECK(ids(s, f) == std::vector<std::string>{"4", "5"});
  f = {};
  f.pr = "medium";  // 空の優先度は Medium 扱い
  CHECK(ids(s, f) == std::vector<std::string>{"1", "3", "4", "6"});
  f = {};
  f.flt = FltFilter::Late;  // 3 は 4週で 11/6 に間に合わない
  CHECK(ids(s, f) == std::vector<std::string>{"1", "3"});
  f.flt = FltFilter::Done;
  CHECK(ids(s, f) == std::vector<std::string>{"4", "5"});
  f.flt = FltFilter::Open;
  f.q = "画面";
  f.asg = AsgFilter::Name;
  f.asgName = "田中";
  f.pr = "high";
  CHECK(ids(s, f) == std::vector<std::string>{"1", "2"});
  CHECK(f.active());
  CHECK_FALSE(Filter{}.active());
}
