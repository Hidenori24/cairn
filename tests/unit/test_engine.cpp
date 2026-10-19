#include <doctest/doctest.h>

#include "../test_util.hpp"
#include "roadmap/csv.hpp"
#include "roadmap/schedule.hpp"

using namespace roadmap;
using testutil::D;

namespace {
struct Fixture {
  csv::ImportResult imp;
  Computed c;
  const ScheduleRow& row(const std::string& id) const { return c.R[c.T.find(id)]; }
};
Fixture run(const std::string& text, const std::string& base = "2026-10-19", ResourceLimit res = {},
            const std::string& scn = kStdScenario, const Calendar& cal = Calendar()) {
  auto r = csv::importCsv(text);
  if (!r.ok()) FAIL(r.error());
  ScheduleOptions o;
  o.base = D(base.c_str());
  o.resource = res;
  o.scenario = scn;
  auto c = computeAll(r->issues, r->meta, cal, o);
  if (!c.ok()) FAIL(c.error());
  return {std::move(r).value(), std::move(c).value()};
}
}  // namespace

TEST_CASE("engine: 空のプロジェクト") {
  auto t = buildTree({}, {});
  REQUIRE(t.ok());
  CHECK(t->leafOrder.empty());
  CHECK(t->scenarios == std::vector<std::string>{"標準"});
  auto R = computeSchedule(*t, {}, Calendar(), {});
  CHECK(R.empty());
  CHECK(summarize(*t, R).total == 0);
}

TEST_CASE("engine: 末端が1件・期限なし") {
  auto f = run("Summary,Duration (weeks)\nA,2\n");
  const auto& r = f.row("1");
  CHECK(r.leaf);
  CHECK(r.days == 10);
  CHECK(r.asapStart == D("2026-10-19"));
  CHECK(r.asapFinish == D("2026-10-30"));
  CHECK(r.alapFinish == r.asapFinish);
  CHECK(r.floatDays == 0);
  CHECK(r.status == "余裕小");
  REQUIRE(f.c.T.warns.size() == 1);
  CHECK(f.c.T.warns[0] == "期限（Deadline）が1件も無いため、余裕は0として表示します。期限を入れると逆算できます");
}

TEST_CASE("engine: 先行・最遅・余裕・親のまとめ・マイルストーン") {
  auto f = run("Issue Id,Issue Type,Summary,Parent,Outward issue link (Blocks),Deadline,Duration (weeks)\n"
               "1,Epic,E,,,2026-12-31,\n2,Task,A,1,3,,1\n3,Task,B,1,4,,2\n4,Milestone,M,1,,,0\n");
  const auto& a = f.row("2");
  const auto& b = f.row("3");
  const auto& m = f.row("4");
  CHECK(a.asapFinish == D("2026-10-23"));
  CHECK(b.asapStart == D("2026-10-26"));
  CHECK(b.asapFinish == D("2026-11-06"));
  CHECK(m.asapStart == b.asapFinish);  // 期間0のマイルストーンは営業日を消費しない
  CHECK(m.asapFinish == b.asapFinish);
  CHECK(m.alapFinish == D("2026-12-31"));
  CHECK(b.alapFinish == D("2026-12-31"));
  CHECK(f.c.T.kind[f.c.T.find("4")] == Kind::Ms);
  const auto& e = f.row("1");
  CHECK_FALSE(e.leaf);
  CHECK(e.n == 3);
  CHECK(e.asapStart == a.asapStart);
  CHECK(e.asapFinish == m.asapFinish);
  CHECK(e.floatDays == std::min({a.floatDays, b.floatDays, m.floatDays}));
}

TEST_CASE("engine: 完了・進行中・着手可能日") {
  auto f = run("Summary,Status,Start date,Resolved,Earliest start,Remaining (weeks),Duration (weeks)\n"
               "Done,Done,2026-10-01,2026-10-05,,,1\n"
               "Prog,In Progress,2026-10-12,,,0.4,2\n"
               "Prog2,In Progress,2026-10-12,,,,2\n"
               "NB,To Do,,,2026-11-01,,1\n");
  CHECK(f.row("1").done);
  CHECK(f.row("1").pinned);
  CHECK(f.row("1").asapFinish == D("2026-10-05"));
  CHECK(f.row("1").status == "完了");
  CHECK(f.row("2").pinned);
  CHECK(f.row("2").asapStart == D("2026-10-12"));
  CHECK(f.row("2").asapFinish == D("2026-10-20"));  // 残り 0.4週 = 2営業日（基準日から）
  CHECK(f.row("3").asapFinish == D("2026-10-23"));
  CHECK(f.row("4").asapStart == D("2026-11-02"));  // 日曜の着手可能日 → 翌営業日
}

TEST_CASE("engine: すべて完了") {
  auto f = run("Issue Id,Summary,Parent,Status\n1,P,,\n2,a,1,Done\n3,b,1,完了\n");
  CHECK(f.row("1").done);
  CHECK(f.row("1").status == "完了");
  CHECK(f.row("1").nDone == 2);
  CHECK(summarize(f.c.T, f.c.R).done == 2);
}

TEST_CASE("engine: 依存の循環と親子のループはエラー") {
  auto r = csv::importCsv("Issue Id,Summary,Outward issue link (Blocks)\n1,a,2\n2,b,1\n");
  REQUIRE(r.ok());
  auto t = buildTree(r->issues, r->meta);
  REQUIRE_FALSE(t.ok());
  CHECK(t.error() == "先行・後続の関係がループしています: 1, 2");
  std::vector<Issue> v(2);
  v[0].id = "1";
  v[0].summary = "a";
  v[0].parent = "2";
  v[1].id = "2";
  v[1].summary = "b";
  v[1].parent = "1";
  auto t2 = buildTree(v, {});
  REQUIRE_FALSE(t2.ok());
  CHECK(t2.error().rfind("親子関係がループしています: ", 0) == 0);
}

TEST_CASE("engine: 親への依存は配下の葉に展開・同じ配下の依存は無視") {
  auto f = run("Issue Id,Summary,Parent,Outward issue link (Blocks),Duration (weeks)\n"
               "1,P,,3,\n2,a,1,1,1\n3,b,,,1\n");
  const Tree& T = f.c.T;
  CHECK(T.preds[T.find("3")] == std::vector<int>{T.find("2")});
  REQUIRE(T.warns.size() >= 1);
  CHECK(T.warns[0] == "親子の間（または同じ配下）の依存関係は無視しました");
}

TEST_CASE("engine: 期限の継承（Deadline 列あり）と Due date 由来の期限（列なし）") {
  auto f = run("Issue Id,Summary,Parent,Deadline,Duration (weeks)\n1,P,,2026-12-01,\n2,a,1,2026-12-15,1\n3,b,1,2026-11-20,1\n");
  CHECK(f.c.T.dl[f.c.T.find("2")] == D("2026-12-01"));
  CHECK(f.c.T.dl[f.c.T.find("3")] == D("2026-11-20"));
  auto g = run("Issue Id,Summary,Parent,Due date,Outward issue link (Blocks),Duration (weeks)\n1,P,,2026-12-01,,\n2,a,1,2026-12-15,3,1\n3,b,1,2026-11-20,,1\n");
  CHECK_FALSE(g.c.T.ownDl[g.c.T.find("1")]);  // 親は Due date を期限にしない
  CHECK_FALSE(g.c.T.ownDl[g.c.T.find("2")]);  // 後続がある
  CHECK(g.c.T.ownDl[g.c.T.find("3")] == D("2026-11-20"));
}

TEST_CASE("engine: シナリオ") {
  const char* csvText = "Summary,Duration (weeks),Duration (weeks) @速い\nA,2,1\n";
  auto f = run(csvText, "2026-10-19", {}, "速い");
  CHECK(f.row("1").weeks == 1);
  CHECK(f.row("1").days == 5);
  CHECK(f.c.T.scenarios == std::vector<std::string>{"標準", "速い"});
  auto g = run(csvText);
  CHECK(g.row("1").weeks == 2);
}

TEST_CASE("engine: 資源制限（cap 1・2）・waited・freeStart・workload") {
  const char* csvText = "Summary,Assignee,Duration (weeks)\nA,田中,1\nB,田中,1\nC,田中,1\nD,,1\n";
  auto off = run(csvText);
  for (const char* id : {"1", "2", "3", "4"}) CHECK_FALSE(off.row(id).waited);
  auto one = run(csvText, "2026-10-19", {true, 1});
  CHECK(one.row("1").asapStart == D("2026-10-19"));
  CHECK(one.row("2").asapStart == D("2026-10-26"));
  CHECK(one.row("3").asapStart == D("2026-11-02"));
  CHECK(one.row("2").waited);
  CHECK(one.row("2").freeStart == D("2026-10-19"));
  CHECK_FALSE(one.row("4").waited);
  auto two = run(csvText, "2026-10-19", {true, 2});
  CHECK(two.row("2").asapStart == D("2026-10-19"));
  CHECK(two.row("3").asapStart == D("2026-10-26"));
  auto wl = workload(one.c.T, one.imp.issues, one.c.R, Calendar());
  REQUIRE(wl.size() == 1);
  CHECK(wl[0].name == "田中");
  CHECK(wl[0].n == 3);
  CHECK(wl[0].weeks == 3);
  CHECK(wl[0].peak == 3);  // 制限なしの開始で数える
  CHECK(wl[0].wait == 2);
  CHECK(wl[0].last == D("2026-11-06"));
}

TEST_CASE("engine: 余裕の表示") {
  ScheduleRow r;
  r.floatDays = -12;
  CHECK(floatText(r) == "−2週");
  r.floatDays = -1;
  CHECK(floatText(r) == "−1週");
  r.floatDays = 7;
  CHECK(floatText(r) == "7日");
  r.done = true;
  CHECK(floatText(r) == "完了");
}
