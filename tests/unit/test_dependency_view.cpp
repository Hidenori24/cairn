// 依存の見せ方（Issue #10）: たどる・日付の衝突・クリティカル・付け替え
#include <doctest/doctest.h>

#include <algorithm>

#include "roadmap/commands.hpp"
#include "roadmap/dependency_view.hpp"
#include "roadmap/project_io.hpp"

using namespace roadmap;

namespace {
FixedClock clk(LocalTime{2026, 10, 19, 9, 0, 0});

Session fromCsv(const std::string& text) {
  ProjectData d;
  auto imp = csv::importCsv(text);
  if (!imp) FAIL(imp.error());
  d.issues = imp->issues;
  d.meta = imp->meta;
  d.settings.baseDate = "2026-10-19";
  d.settings.ui = defaultUi();
  return Session(d, &clk);
}

// 1 親 ─ 2 A / 7 X ／ 3 B / 4 C / 5 D / 6 E。つなぎ: A→B→C→D、B→E
Session chain() {
  Session s = fromCsv(
      "Issue Id,Issue Type,Summary,Parent,Assignee,Status,Priority,Labels,Deadline,Duration (weeks)\n"
      "1,Epic,親,,,,,,,\n"
      "2,Task,A,1,,To Do,,,,1\n"
      "3,Task,B,,,To Do,,,,1\n"
      "4,Task,C,,,To Do,,,,1\n"
      "5,Task,D,,,To Do,,,,1\n"
      "6,Task,E,,,To Do,,,,1\n"
      "7,Task,X,1,,To Do,,,,1\n");
  REQUIRE(s.addLink("2", "3"));
  REQUIRE(s.addLink("3", "4"));
  REQUIRE(s.addLink("4", "5"));
  REQUIRE(s.addLink("3", "6"));
  return s;
}

std::vector<std::string> marked(const Session& s, const std::vector<char>& m) {
  std::vector<std::string> r;
  for (int i = 0; i < s.tree()->n; ++i)
    if (m[static_cast<std::size_t>(i)]) r.push_back(s.tree()->ids[static_cast<std::size_t>(i)]);
  std::sort(r.begin(), r.end());
  return r;
}
using V = std::vector<std::string>;

bool linked(const Session& s, const std::string& from, const std::string& to) {
  const Issue* o = s.issue(from);
  return o && std::find(o->blocks.begin(), o->blocks.end(), to) != o->blocks.end();
}
}  // namespace

TEST_CASE("dependency_view: たどる = 先行・後続をすべて（兄弟の枝は含めない）") {
  Session s = chain();
  const Tree& T = *s.tree();
  // C: 先行 B・A、後続 D。B の別の後続 E と、関係のない X は含めない。A の親は薄くしない
  CHECK(marked(s, traceDependencies(T, T.find("4"))) == V{"1", "2", "3", "4", "5"});
  // A: 後続は B・C・D・E すべて
  CHECK(marked(s, traceDependencies(T, T.find("2"))) == V{"1", "2", "3", "4", "5", "6"});
  // 一番後ろ: 先行だけ
  CHECK(marked(s, traceDependencies(T, T.find("5"))) == V{"1", "2", "3", "4", "5"});
  // E: 先行 B・A だけ（C・D は別の枝）
  CHECK(marked(s, traceDependencies(T, T.find("6"))) == V{"1", "2", "3", "6"});
}

TEST_CASE("dependency_view: たどる = 親を選ぶと配下の葉からたどる") {
  Session s = chain();
  const Tree& T = *s.tree();
  // 親(1) の配下は A と X。A から後ろへ全部、X は単独
  CHECK(marked(s, traceDependencies(T, T.find("1"))) == V{"1", "2", "3", "4", "5", "6", "7"});
}

TEST_CASE("dependency_view: つながりのない課題・範囲外") {
  Session s = chain();
  const Tree& T = *s.tree();
  CHECK(marked(s, traceDependencies(T, T.find("7"))) == V{"1", "7"});  // X は自分と親だけ
  CHECK(marked(s, traceDependencies(T, -1)).empty());
  CHECK(marked(s, traceDependencies(T, T.n)).empty());
}

TEST_CASE("dependency_view: 日付の衝突 = 後続が先行の完了より前（同じ日も）に始まる") {
  ScheduleRow a, b;
  a.asapStart = 90;
  a.asapFinish = 100;
  b.asapStart = 101;
  CHECK_FALSE(isDateConflict(a, b, false));  // 翌日から始まる
  b.asapStart = 100;
  CHECK(isDateConflict(a, b, false));  // 先行が終わる日に始まる
  b.asapStart = 95;
  CHECK(isDateConflict(a, b, false));
  // 期間 0 のマイルストーンは先行の完了日と同じ日に置ける
  b.asapStart = 100;
  CHECK_FALSE(isDateConflict(a, b, true));
  b.asapStart = 99;
  CHECK(isDateConflict(a, b, true));
  // 後続が完了していれば数えない
  b.asapStart = 95;
  b.done = true;
  CHECK_FALSE(isDateConflict(a, b, false));
}

TEST_CASE("dependency_view: 日付の衝突 = 進行中の後続が先行より前に始まっている") {
  Session s = fromCsv(
      "Issue Id,Summary,Status,Start date,Duration (weeks)\n"
      "1,先行,To Do,,2\n"
      "2,進行中の後続,In Progress,2026-10-19,1\n"
      "3,ふつうの後続,To Do,,1\n");
  REQUIRE(s.addLink("1", "2"));
  REQUIRE(s.addLink("1", "3"));
  const auto& R = *s.rows();
  const Tree& T = *s.tree();
  CHECK(isDateConflict(R[T.find("1")], R[T.find("2")], false));       // 開始済みなので先行を待てない
  CHECK_FALSE(isDateConflict(R[T.find("1")], R[T.find("3")], false));  // 先行の後ろに並ぶ
}

TEST_CASE("dependency_view: クリティカル = 未完了どうしで余裕が 0 以下") {
  ScheduleRow a, b;
  a.floatDays = 0;
  b.floatDays = 0;
  CHECK(isCriticalLink(a, b));
  b.floatDays = -3;  // 遅れも含む
  CHECK(isCriticalLink(a, b));
  b.floatDays = 2;
  CHECK_FALSE(isCriticalLink(a, b));
  b.floatDays = 0;
  a.done = true;
  CHECK_FALSE(isCriticalLink(a, b));
  a.done = false;
  b.done = true;
  CHECK_FALSE(isCriticalLink(a, b));
}

TEST_CASE("dependency_view: クリティカル = 期限ちょうどの連鎖") {
  // 2つが2週ぶん続き、期限がその終わり。別の1件は期限まで余裕がある
  Session s = fromCsv(
      "Issue Id,Summary,Status,Deadline,Duration (weeks)\n"
      "1,A,To Do,,1\n"
      "2,B,To Do,2026-10-30,1\n"
      "3,C,To Do,2026-12-25,1\n");
  REQUIRE(s.addLink("1", "2"));
  REQUIRE(s.addLink("1", "3"));
  const auto& R = *s.rows();
  const Tree& T = *s.tree();
  CHECK(R[T.find("2")].floatDays == 0);
  CHECK(isCriticalLink(R[T.find("1")], R[T.find("2")]));
  CHECK_FALSE(isCriticalLink(R[T.find("1")], R[T.find("3")]));
}

TEST_CASE("relink: 後続の付け替え = 元を外して新しい相手につなぐ（Undo 1回）") {
  Session s = chain();
  const std::size_t u0 = s.undoSize();
  REQUIRE(s.relinkIssue("2", "3", "2", "7"));  // A→B を A→X へ
  CHECK_FALSE(linked(s, "2", "3"));
  CHECK(linked(s, "2", "7"));
  CHECK(s.undoSize() == u0 + 1);
  REQUIRE(s.undo());
  CHECK(linked(s, "2", "3"));
  CHECK_FALSE(linked(s, "2", "7"));
}

TEST_CASE("relink: 先行の付け替え") {
  Session s = chain();
  REQUIRE(s.relinkIssue("3", "4", "6", "4"));  // B→C を E→C へ
  CHECK_FALSE(linked(s, "3", "4"));
  CHECK(linked(s, "6", "4"));
  CHECK(linked(s, "3", "6"));  // ほかのつなぎは残る
}

TEST_CASE("relink: 循環・重複・親子・存在しない元は何も変えない") {
  Session s = chain();
  const std::size_t u0 = s.undoSize();
  CHECK_FALSE(s.relinkIssue("3", "4", "3", "2"));  // B→A は A→B と循環
  CHECK_FALSE(s.relinkIssue("2", "3", "2", "3"));  // 同じ相手
  CHECK_FALSE(s.relinkIssue("3", "4", "3", "6"));  // B→E はすでにある
  CHECK_FALSE(s.relinkIssue("3", "4", "3", "3"));  // 自分自身
  CHECK_FALSE(s.relinkIssue("2", "5", "2", "7"));  // 元のつなぎがない
  CHECK_FALSE(s.relinkIssue("2", "3", "2", "1"));  // A→親(1)。A は親の配下なので親子の間
  CHECK(s.undoSize() == u0);
  CHECK(linked(s, "3", "4"));
  CHECK(linked(s, "2", "3"));
  // 元のつなぎが外れることで循環しなくなる付け替えは通る: C→D を D の先行から B→D へ
  REQUIRE(s.relinkIssue("4", "5", "3", "5"));
  CHECK(linked(s, "3", "5"));
}

TEST_CASE("previewLink: 付け替えのプレビューは元のつなぎを外した状態で計算する") {
  Session s = fromCsv(
      "Issue Id,Summary,Status,Duration (weeks)\n"
      "1,A,To Do,2\n2,B,To Do,1\n3,C,To Do,1\n");
  REQUIRE(s.addLink("1", "2"));
  const Tree& T = *s.tree();
  const Day c0 = (*s.rows())[T.find("3")].asapStart;
  // 1→2 を 1→3 に付け替えると、C が A の後ろに回って遅れる
  const LinkPreview pv = s.previewLink("1", "3", "1", "2");
  CHECK(pv.ok);
  CHECK(pv.block == LinkBlock::None);
  bool cLater = false, bEarlier = false;
  for (const auto& sh : pv.shifts) {
    if (sh.idx == T.find("3") && sh.newStart > c0) cLater = true;
    if (sh.idx == T.find("2") && sh.newStart < sh.oldStart) bEarlier = true;
  }
  CHECK(cLater);
  CHECK(bEarlier);  // 外した B は先行を待たなくなる
  // 元のつなぎを外した形では「すでにある」ではなくなる
  CHECK(s.previewLink("1", "2").block == LinkBlock::Already);
  CHECK(s.previewLink("1", "2", "1", "2").block == LinkBlock::None);
}
