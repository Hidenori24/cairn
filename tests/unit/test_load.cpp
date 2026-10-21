// 担当者別の負荷（週×担当）と、過去の完了（担当者別）の集計
#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "roadmap/csv.hpp"
#include "roadmap/load.hpp"
#include "roadmap/schedule.hpp"

using namespace roadmap;

namespace {

struct Row {
  std::string id, asg, weeks, status, resolved, type;
  Row(std::string i, std::string a, std::string w = "1", std::string st = "To Do", std::string rs = "", std::string ty = "Task")
      : id(std::move(i)), asg(std::move(a)), weeks(std::move(w)), status(std::move(st)), resolved(std::move(rs)), type(std::move(ty)) {}
};

struct Proj {
  csv::ImportResult imp;
  Computed comp;
  Day base = 0;
  int idx(const std::string& id) const { return comp.T.find(id); }
};

Proj make(const std::vector<Row>& rows, const char* baseIso, const Calendar& cal) {
  std::string text =
      "Issue Id,Issue Type,Summary,Parent,Assignee,Status,Priority,Resolved,Duration (weeks),Outward issue link (Blocks)\n";
  for (const Row& r : rows)
    text += r.id + "," + r.type + ",課題" + r.id + ",," + r.asg + "," + r.status + ",Medium," + r.resolved + "," + r.weeks + ",\n";
  Proj p;
  if (!rows.empty()) {  // 行がない CSV は取り込めないので、課題なしのまま
    auto im = csv::importCsv(text);
    REQUIRE(im.ok());
    p.imp = std::move(im).value();
  }
  ScheduleOptions opt;
  opt.base = *parseIso(baseIso);
  p.base = opt.base;
  auto c = computeAll(p.imp.issues, p.imp.meta, cal, opt);
  REQUIRE(c.ok());
  p.comp = std::move(c).value();
  return p;
}

LoadReport run(const Proj& p, const Calendar& cal, ResourceLimit rl = {}) {
  return computeLoad(p.comp.T, p.imp.issues, p.comp.R, cal, p.base, rl);
}

Day day(const char* iso) { return *parseIso(iso); }

}  // namespace

TEST_CASE("load: 営業日へ均等に広げる（週またぎ・週の頭から）") {
  const Calendar cal;
  const Proj p = make({{"1", "田中", "2"}}, "2026-10-19", cal);  // 月曜 10 日分 → 10/19〜10/30
  const LoadReport r = run(p, cal);
  REQUIRE(r.weekStarts.size() == 2);
  CHECK(r.weekStarts[0] == day("2026-10-19"));
  CHECK(r.weekStarts[1] == day("2026-10-26"));
  CHECK(r.workdays == std::vector<int>{5, 5});
  CHECK_FALSE(r.lastFolded);
  CHECK(r.cap == 1);
  REQUIRE(r.load.size() == 1);
  const LoadRow& w = r.load[0];
  CHECK(w.name == "田中");
  REQUIRE(w.cells.size() == 2);
  CHECK(w.cells[0].days == doctest::Approx(5.0));
  CHECK(w.cells[0].ratio == doctest::Approx(1.0));
  CHECK(w.cells[1].ratio == doctest::Approx(1.0));
  CHECK(w.cells[0].n == 1);
  REQUIRE(w.cells[0].ids.size() == 1);
  CHECK(w.cells[0].ids[0] == p.idx("1"));
  CHECK(w.totalDays == doctest::Approx(10.0));
  CHECK(w.peak == doctest::Approx(1.0));
}

TEST_CASE("load: 基準日が週の途中なら、その週の残りと次の週に分かれる（分母は週全体の営業日）") {
  const Calendar cal;
  const Proj p = make({{"1", "田中", "1"}}, "2026-10-21", cal);  // 水曜 → 水木金 + 月火
  const LoadReport r = run(p, cal);
  REQUIRE(r.weekStarts.size() == 2);
  CHECK(r.weekStarts[0] == day("2026-10-19"));  // 基準日の週の月曜
  const LoadRow& w = r.load.at(0);
  CHECK(w.cells[0].days == doctest::Approx(3.0));
  CHECK(w.cells[0].ratio == doctest::Approx(0.6));
  CHECK(w.cells[1].days == doctest::Approx(2.0));
  CHECK(w.cells[1].ratio == doctest::Approx(0.4));
}

TEST_CASE("load: 休業日は広げる先にも分母にも数えない") {
  const Calendar cal(std::vector<Day>{day("2026-10-21")});  // 水曜が休み
  const Proj p = make({{"1", "田中", "1"}}, "2026-10-19", cal);  // 月火木金 + 翌月
  const LoadReport r = run(p, cal);
  REQUIRE(r.weekStarts.size() == 2);
  CHECK(r.workdays == std::vector<int>{4, 5});
  const LoadRow& w = r.load.at(0);
  CHECK(w.cells[0].days == doctest::Approx(4.0));
  CHECK(w.cells[0].ratio == doctest::Approx(1.0));
  CHECK(w.cells[1].days == doctest::Approx(1.0));
  CHECK(w.cells[1].ratio == doctest::Approx(0.2));
}

TEST_CASE("load: 休業日だけの週は営業日 0・割合 0 で、0 除算にならない") {
  const Calendar cal(std::vector<Day>{day("2026-10-26"), day("2026-10-27"), day("2026-10-28"), day("2026-10-29"), day("2026-10-30")});
  const Proj p = make({{"1", "田中", "2"}}, "2026-10-19", cal);  // 10/19〜10/23 と 11/2〜11/6
  const LoadReport r = run(p, cal);
  REQUIRE(r.weekStarts.size() == 3);
  CHECK(r.workdays == std::vector<int>{5, 0, 5});
  const LoadRow& w = r.load.at(0);
  CHECK(w.cells[1].days == doctest::Approx(0.0));
  CHECK(w.cells[1].ratio == doctest::Approx(0.0));
  CHECK(w.cells[1].n == 0);
  CHECK(w.cells[1].ids.empty());
  CHECK(w.cells[2].ratio == doctest::Approx(1.0));
}

TEST_CASE("load: 担当なしは最後の行（合計が大きくても）。ほかは合計が大きい順") {
  const Calendar cal;
  const Proj p = make({{"1", "鈴木", "1"}, {"2", "田中", "1"}, {"3", "田中", "1"}, {"4", "", "1"}, {"5", "", "1"}, {"6", "", "1"}}, "2026-10-19", cal);
  const LoadReport r = run(p, cal);
  REQUIRE(r.load.size() == 3);
  CHECK(r.load[0].name == "田中");
  CHECK(r.load[0].cells[0].ratio == doctest::Approx(2.0));  // 同時に 2 件 = 200%
  CHECK(r.load[1].name == "鈴木");
  CHECK(r.load[2].name.empty());
  CHECK(r.load[2].cells[0].ratio == doctest::Approx(3.0));
  CHECK(r.load[2].cells[0].n == 3);
}

TEST_CASE("load: 完了済み・マイルストーンは負荷に数えない") {
  const Calendar cal;
  const Proj p = make({{"1", "田中", "1", "Done", "2026-10-20"}, {"2", "田中", "1"}, {"3", "田中", "0", "To Do", "", "Milestone"}}, "2026-10-19", cal);
  const LoadReport r = run(p, cal);
  REQUIRE(r.load.size() == 1);
  CHECK(r.load[0].cells[0].n == 1);
  CHECK(r.load[0].cells[0].ratio == doctest::Approx(1.0));
  CHECK(r.load[0].cells[0].ids.at(0) == p.idx("2"));

  const Proj onlyDone = make({{"1", "田中", "1", "Done", "2026-10-20"}, {"2", "", "0", "To Do", "", "Milestone"}}, "2026-10-19", cal);
  const LoadReport e = run(onlyDone, cal);
  CHECK(e.weekStarts.empty());
  CHECK(e.load.empty());
}

TEST_CASE("load: 期間 0 の作業は 1 日として扱う") {
  const Calendar cal;
  Proj p = make({{"1", "田中", "1"}}, "2026-10-19", cal);
  p.comp.R[static_cast<std::size_t>(p.idx("1"))].days = 0;  // エンジンは 1 日以上にするが、0 が来ても落ちない
  const LoadReport r = run(p, cal);
  REQUIRE(r.load.size() == 1);
  CHECK(r.load[0].cells[0].days == doctest::Approx(1.0));
  CHECK(r.load[0].cells[0].ratio == doctest::Approx(0.2));
}

TEST_CASE("load: 基準日の週より前に始まっている作業は、基準日の週からのぶんだけ数える") {
  const Calendar cal;
  Proj p = make({{"1", "田中", "1"}}, "2026-10-19", cal);
  p.comp.R[static_cast<std::size_t>(p.idx("1"))].asapStart = day("2026-10-12");  // 先週の月曜から 10/23 まで = 営業日 10 日に 5 日分
  const LoadReport r = run(p, cal);
  REQUIRE(r.weekStarts.size() == 1);
  CHECK(r.load.at(0).cells[0].days == doctest::Approx(2.5));
  CHECK(r.load.at(0).cells[0].ratio == doctest::Approx(0.5));
}

TEST_CASE("load: 資源制限をオンにすると分母に cap を掛ける（100% = cap 件が同時に動く）") {
  const Calendar cal;
  const Proj p = make({{"1", "田中", "1"}, {"2", "田中", "1"}}, "2026-10-19", cal);  // 同じ週に 2 件
  const LoadReport off = run(p, cal, {false, 2});  // オフのときは cap を見ない
  CHECK(off.cap == 1);
  CHECK(off.load.at(0).cells[0].ratio == doctest::Approx(2.0));
  const LoadReport on2 = run(p, cal, {true, 2});
  CHECK(on2.cap == 2);
  CHECK(on2.load.at(0).cells[0].ratio == doctest::Approx(1.0));
  CHECK(on2.load.at(0).cells[0].days == doctest::Approx(10.0));  // 営業日数そのものは変わらない
  const LoadReport on0 = run(p, cal, {true, 0});  // 0 以下は 1 として扱う
  CHECK(on0.cap == 1);
  CHECK(on0.load.at(0).cells[0].ratio == doctest::Approx(2.0));
}

TEST_CASE("load: 26 週より先は最後の週にまとめる（分母もまとめた範囲の営業日）") {
  const Calendar cal;
  const Proj p = make({{"1", "田中", "30"}}, "2026-10-19", cal);  // 30 週
  const LoadReport r = run(p, cal);
  REQUIRE(r.weekStarts.size() == static_cast<std::size_t>(kLoadWeeks));
  CHECK(r.lastFolded);
  CHECK(r.workdays.front() == 5);
  CHECK(r.workdays.back() == 25);  // 25 週目〜30 週目の 5 週ぶん
  const LoadRow& w = r.load.at(0);
  CHECK(w.cells.front().ratio == doctest::Approx(1.0));
  CHECK(w.cells.back().days == doctest::Approx(25.0));
  CHECK(w.cells.back().ratio == doctest::Approx(1.0));
  CHECK(w.totalDays == doctest::Approx(150.0));
}

TEST_CASE("load: 26 週ちょうどなら、まとめない") {
  const Calendar cal;
  const Proj p = make({{"1", "田中", "26"}}, "2026-10-19", cal);
  const LoadReport r = run(p, cal);
  REQUIRE(r.weekStarts.size() == static_cast<std::size_t>(kLoadWeeks));
  CHECK_FALSE(r.lastFolded);
  CHECK(r.workdays.back() == 5);
}

TEST_CASE("load: セルの課題は上限件数まで（全件の数は n）") {
  const Calendar cal;
  std::vector<Row> rows;
  for (int i = 1; i <= 10; ++i) rows.push_back({std::to_string(i), "田中", "1"});
  const Proj p = make(rows, "2026-10-19", cal);
  const LoadReport r = run(p, cal);
  const LoadCell& c = r.load.at(0).cells[0];
  CHECK(c.n == 10);
  REQUIRE(c.ids.size() == static_cast<std::size_t>(kLoadCellIds));
  for (int id : c.ids) {
    CHECK(id >= 0);
    CHECK(id < p.comp.T.n);
  }
  CHECK(c.ratio == doctest::Approx(10.0));
}

TEST_CASE("load: 空のプロジェクト・担当者がいないときも壊れない") {
  const Calendar cal;
  const Proj empty = make({}, "2026-10-19", cal);
  const LoadReport r = run(empty, cal);
  CHECK(r.weekStarts.empty());
  CHECK(r.load.empty());
  CHECK(r.done.empty());
  CHECK(r.doneWeekStarts.size() == static_cast<std::size_t>(kPastWeeks));

  const Proj none = make({{"1", "", "1"}}, "2026-10-19", cal);  // 担当なしだけ
  const LoadReport q = run(none, cal);
  REQUIRE(q.load.size() == 1);
  CHECK(q.load[0].name.empty());
  CHECK(loadPercent(q.load[0].cells[0].ratio) == 100);
}

TEST_CASE("load: 割合 → パーセント（四捨五入・巨大な値は丸める）") {
  CHECK(loadPercent(0) == 0);
  CHECK(loadPercent(0.8) == 80);
  CHECK(loadPercent(0.996) == 100);
  CHECK(loadPercent(1.204) == 120);
  CHECK(loadPercent(-3) == 0);
  CHECK(loadPercent(1e300) == 99999);
}

TEST_CASE("done: 過去 8 週の完了を担当者ごとに数える（基準日の週・先・完了日なしは数えない）") {
  const Calendar cal;
  const Proj p = make(
      {{"1", "Bob", "2", "Done", "2026-10-13"},       // 直前の週
       {"2", "Bob", "1", "Done", "2026-10-18"},       // 日曜 = 直前の週の最後
       {"3", "Alice", "3", "Done", "2026-08-24"},     // 8 週前の月曜 = いちばん古い週
       {"4", "Alice", "1", "Done", "2026-08-23"},     // その前の日曜 = 範囲の外
       {"5", "Alice", "1", "Done", "2026-10-19"},     // 基準日の週 = 含まない
       {"6", "Bob", "1", "Done", ""},                 // 完了日なし
       {"7", "", "1.5", "Done", "2026-09-10"},        // 担当なし
       {"8", "Bob", "1", "Done", "2026-10-25"},       // 先の日付
       {"9", "Bob", "0", "Done", "2026-10-13", "Milestone"},  // マイルストーンは数えない
       {"10", "Bob", "1", "To Do", ""}},
      "2026-10-19", cal);
  const LoadReport r = run(p, cal);
  REQUIRE(r.doneWeekStarts.size() == static_cast<std::size_t>(kPastWeeks));
  CHECK(r.doneWeekStarts.front() == day("2026-08-24"));
  CHECK(r.doneWeekStarts.back() == day("2026-10-12"));
  REQUIRE(r.done.size() == 3);
  CHECK(r.done[0].name == "Alice");  // 名前順。担当なしは最後
  CHECK(r.done[1].name == "Bob");
  CHECK(r.done[2].name.empty());
  CHECK(r.done[0].n == 1);
  CHECK(r.done[0].weeks == doctest::Approx(3.0));
  CHECK(r.done[0].perWeek.front() == 1);
  CHECK(r.done[1].n == 2);
  CHECK(r.done[1].weeks == doctest::Approx(3.0));
  CHECK(r.done[1].avgWeeks() == doctest::Approx(1.5));
  CHECK(r.done[1].perWeek.back() == 2);
  CHECK(r.done[2].n == 1);
  CHECK(r.done[2].weeks == doctest::Approx(1.5));
  CHECK(r.done[2].perWeek.size() == static_cast<std::size_t>(kPastWeeks));
  CHECK(r.done[2].perWeek[2] == 1);  // 9/10(木) は 9/7 の週 = 3 番目
}

TEST_CASE("done: 完了がなければ空") {
  const Calendar cal;
  const Proj p = make({{"1", "Bob", "1"}}, "2026-10-19", cal);
  const LoadReport r = run(p, cal);
  CHECK(r.done.empty());
  CHECK(DoneRow{}.avgWeeks() == 0);
}
