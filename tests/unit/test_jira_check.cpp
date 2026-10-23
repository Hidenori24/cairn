#include <doctest/doctest.h>

#include "../test_util.hpp"
#include "roadmap/jira_check.hpp"

using namespace roadmap;

TEST_CASE("check-jira: golden の JIRA用CSVはすべて OK") {
  for (const char* s : {"scenario_demo", "hierarchy_demo", "leveling_demo"}) {
    CAPTURE(s);
    auto r = checkJiraCsv(testutil::readText(testutil::golden(std::string(s) + ".jira.csv")));
    CHECK(r.ok);
    CHECK(r.errors.empty());
    CHECK(r.report(s)[0].find("/ OK") != std::string::npos);
  }
}

TEST_CASE("check-jira: 各規則") {
  const std::string csv =
      "Issue Id,Issue Type,Summary,Parent,Epic Name,Priority,Labels,Original estimate,Start date,Due date,"
      "Outward issue link (Blocks),Comment,Deadline,Assignee,Foo\n"
      "1,Epic,E,,,Urgent,a b,1x,2026/10/19,2026-10-01,1,bad,,,\n"
      "1,Story,S,9,,,,1w 2d,2026-10-19,2026-10-01,7,19/Oct/26 10:30 AM;me;ok,,,\n"
      "3,Sub-task,,,,,,,,,,,,,\n";
  auto r = checkJiraCsv(csv);
  CHECK_FALSE(r.ok);
  const auto has = [&](const std::string& s) {
    for (const auto& e : r.errors)
      if (e == s) return true;
    return false;
  };
  CHECK(has("標準列 Assignee がアプリ独自列より後ろ"));
  CHECK(has("不明な列 Foo"));
  CHECK(has("Issue Id が重複"));
  CHECK(has("1: Epic に Epic Name がない"));
  CHECK(has("1: 優先度 Urgent"));
  CHECK(has("1: ラベルに空白 'a b'"));
  CHECK(has("1: 見積の書式 1x"));
  CHECK(has("1: Start date の書式 2026/10/19"));
  CHECK(has("1: Blocks 先 1 が不正"));
  CHECK(has("1: コメントの書式"));
  CHECK(has("1: Parent 9 が存在しない"));
  CHECK(has("1: Blocks 先 7 が不正"));
  CHECK(has("1: Start > Due"));
  CHECK(has("3: Summary が空か255文字超"));
  CHECK(has("3: Sub-task に Parent がない"));
  CHECK(r.report("x")[0] == "x: 3件 / 列 15 / NG " + std::to_string(r.errors.size()) + "件");
  CHECK(r.report("x").size() == 13);  // 問題点は最大12件
}

TEST_CASE("check-jira: 既知の追加列は警告して成功扱いにする") {
  const std::string csv =
      "Issue Id,Issue Type,Summary,Created,Updated,Sprint,Attachment,Relates,Outward issue link (Relates)\n"
      "1,Task,作業,2026-10-19,2026-10-20,Sprint 1,file.txt,2,ABC-2\n";
  const auto r = checkJiraCsv(csv);
  CHECK(r.ok);
  CHECK(r.errors.empty());
  CHECK(r.warnings.size() == 6);
  CHECK(r.report("x")[0] == "x: 1件 / 列 9 / OK（警告 6件）");
  CHECK(r.report("x").size() == 7);
  for (const auto& warning : r.warnings) CHECK(warning.find("JIRA が無視する可能性があります") != std::string::npos);
}

TEST_CASE("check-jira: 親の規則") {
  auto r = checkJiraCsv("Issue Id,Issue Type,Summary,Parent,Epic Name\n1,Epic,E,,E\n2,Sub-task,a,1,\n3,Task,b,2,\n4,Task,c,,\n5,Task,d,4,\n");
  REQUIRE_FALSE(r.ok);
  CHECK(r.errors == std::vector<std::string>{"2: Sub-task の親が Epic", "3: Sub-task の下に子（Parent=2）",
                                             "5: Task の親が Epic 以外（Task）"});
}
