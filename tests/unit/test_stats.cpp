#include <doctest/doctest.h>

#include "roadmap/csv.hpp"
#include "roadmap/schedule.hpp"
#include "roadmap/stats.hpp"

using namespace roadmap;

namespace {

const char* const kCsv =
    "Issue Id,Issue Type,Summary,Parent,Assignee,Status,Priority,Resolved,Duration (weeks),Outward issue link (Blocks)\n"
    "1,Epic,全体,,,,High,,,\n"
    "2,Task,A,1,田中,Done,High,2026-10-13,1,\n"
    "3,Task,B,1,田中,In Progress,Highest,,2,4\n"
    "4,Task,C,1,鈴木,To Do,Medium,,2,6\n"
    "5,Task,D,1,,To Do,Low,,1,6\n"
    "6,Milestone,完了,1,,,High,,0,\n"
    "7,Task,単独,,鈴木,To Do,Medium,,1,\n";

struct Fixture {
  csv::ImportResult imp;
  Computed comp;
  ProjectStats st;
  Fixture() {
    auto r = csv::importCsv(kCsv);
    REQUIRE(r.ok());
    imp = std::move(r).value();
    ScheduleOptions opt;
    opt.base = *parseIso("2026-10-19");  // 月曜
    auto c = computeAll(imp.issues, imp.meta, Calendar::fromText(defaultHolidaysText()), opt);
    REQUIRE(c.ok());
    comp = std::move(c).value();
    st = computeStats(comp.T, imp.issues, comp.R, opt.base);
  }
};

}  // namespace

TEST_CASE("stats: 件数・週・内訳（マイルストーンは作業に数えない）") {
  Fixture f;
  const ProjectStats& s = f.st;
  CHECK(s.tasks == 5);
  CHECK(s.done == 1);
  CHECK(s.prog == 1);
  CHECK(s.todo == 3);
  CHECK(s.milestones == 1);
  CHECK(s.msDone == 0);
  CHECK(s.weeks == doctest::Approx(7.0));
  CHECK(s.weeksDone == doctest::Approx(1.0));
  CHECK(s.progress() == doctest::Approx(1.0 / 7.0));
  CHECK(s.unassigned == 1);  // D
  CHECK(s.noDeadline == 4);  // 期限が無いので未完了の4件
  CHECK(s.late + s.tight + s.ok == 4);
  int hist = 0;
  for (int h : s.floatHist) hist += h;
  CHECK(hist == 4);
  CHECK(s.hasFinish);
  CHECK(s.finish >= *parseIso("2026-10-19"));
  // 優先度: 完了は High、未完了は Highest 1・Medium 2・Low 1
  CHECK(s.prioDone[1] == 1);
  CHECK(s.prioOpen[0] == 1);
  CHECK(s.prioOpen[2] == 2);
  CHECK(s.prioOpen[3] == 1);
}

TEST_CASE("stats: 担当別（未完了の週が多い順・未割当は最後）とルート別") {
  Fixture f;
  const ProjectStats& s = f.st;
  REQUIRE(s.byAssignee.size() == 3);
  CHECK(s.byAssignee[0].name == "鈴木");
  CHECK(s.byAssignee[0].n == 2);
  CHECK(s.byAssignee[0].weeksOpen() == doctest::Approx(3.0));
  CHECK(s.byAssignee[1].name == "田中");
  CHECK(s.byAssignee[1].nDone == 1);
  CHECK(s.byAssignee[1].nProg == 1);
  CHECK(s.byAssignee[1].progress() == doctest::Approx(1.0 / 3.0));
  CHECK(s.byAssignee[2].name.empty());
  CHECK(s.byAssignee[2].n == 1);

  REQUIRE(s.byRoot.size() == 2);
  CHECK(s.byRoot[0].n == 4);
  CHECK(s.byRoot[0].nDone == 1);
  CHECK(s.byRoot[1].name == "親のない単独の課題");
  CHECK(s.byRoot[1].n == 1);
}

TEST_CASE("stats: 週ごとの推移（過去は Resolved、先は最早の完了）") {
  Fixture f;
  const ProjectStats& s = f.st;
  REQUIRE(s.weeksTimeline.size() > static_cast<std::size_t>(kPastWeeks) + 1);
  CHECK(s.nowIndex == kPastWeeks);
  CHECK(s.weeksTimeline[static_cast<std::size_t>(s.nowIndex)].start == *parseIso("2026-10-19"));
  CHECK(s.weeksTimeline[static_cast<std::size_t>(s.nowIndex) - 1].actual == 1);  // 10/13 は前の週
  int actual = 0, forecast = 0;
  for (const auto& w : s.weeksTimeline) {
    actual += w.actual;
    forecast += w.forecast;
  }
  CHECK(actual == 1);
  CHECK(forecast == 4);  // 未完了の作業の数
  for (std::size_t k = 0; k < s.weeksTimeline.size(); ++k) {
    if (static_cast<int>(k) < s.nowIndex) CHECK(s.weeksTimeline[k].forecast == 0);
    else CHECK(s.weeksTimeline[k].actual == 0);
  }
}

TEST_CASE("stats: 余裕の階級") {
  CHECK(floatBucket(-1) == 0);
  CHECK(floatBucket(0) == 1);
  CHECK(floatBucket(5) == 1);
  CHECK(floatBucket(6) == 2);
  CHECK(floatBucket(10) == 2);
  CHECK(floatBucket(11) == 3);
  CHECK(floatBucket(20) == 3);
  CHECK(floatBucket(21) == 4);
}

TEST_CASE("stats: 空のプロジェクトでも壊れない") {
  csv::ImportResult imp;  // 課題なし
  ScheduleOptions opt;
  opt.base = *parseIso("2026-10-19");
  auto c = computeAll(imp.issues, imp.meta, Calendar::fromText(defaultHolidaysText()), opt);
  REQUIRE(c.ok());
  const ProjectStats s = computeStats(c->T, imp.issues, c->R, opt.base);
  CHECK(s.tasks == 0);
  CHECK(s.progress() == 0);
  CHECK_FALSE(s.hasFinish);
  CHECK(s.byAssignee.empty());
  CHECK(s.weeksTimeline.size() == static_cast<std::size_t>(kPastWeeks) + 1);
}
