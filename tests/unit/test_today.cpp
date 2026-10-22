// 「今日・今週やること」の集計と、ずらす操作（Issue #72）
#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <string>
#include <utility>
#include <vector>

#include "roadmap/commands.hpp"
#include "roadmap/csv.hpp"
#include "roadmap/gen.hpp"
#include "roadmap/schedule.hpp"
#include "roadmap/today.hpp"

using namespace roadmap;

namespace {

Day D(const char* iso) { return *parseIso(iso); }

// CSV の 1 行ぶん
struct Rw {
  std::string id;
  std::string status = "To Do", asg, start, due, deadline, notBefore, resolved, parent, type = "Task", blocks, weeks = "1";
  Rw(std::string i) : id(std::move(i)) {}  // NOLINT
  Rw& st(std::string s) {
    status = std::move(s);
    return *this;
  }
  Rw& by(std::string a) {
    asg = std::move(a);
    return *this;
  }
  Rw& from(std::string s) {
    start = std::move(s);
    return *this;
  }
  Rw& dueOn(std::string s) {
    due = std::move(s);
    return *this;
  }
  Rw& dl(std::string s) {
    deadline = std::move(s);
    return *this;
  }
  Rw& nb(std::string s) {
    notBefore = std::move(s);
    return *this;
  }
  Rw& res(std::string s) {
    resolved = std::move(s);
    return *this;
  }
  Rw& under(std::string p) {
    parent = std::move(p);
    return *this;
  }
  Rw& ms() {
    type = "Milestone";
    weeks = "0";
    return *this;
  }
  Rw& days(int d) {  // 期間（営業日）
    weeks = std::to_string(d / 5.0);
    return *this;
  }
  Rw& blk(std::string b) {
    blocks = std::move(b);
    return *this;
  }
  Rw& done() { return st("Done"); }
  Rw& prog() { return st("In Progress"); }
};

struct Proj {
  std::vector<Issue> issues;
  ProjectMeta meta;
  Computed comp;
  Calendar cal;
  Day base = 0;
  Day today = 0;
  Result<TodayReport> run(const TodayFilter& f = {}) const { return computeToday(comp.T, issues, comp.R, cal, today, f); }
  TodayReport ok(const TodayFilter& f = {}) const {
    auto r = run(f);
    REQUIRE(r.ok());
    return std::move(r).value();
  }
  int idx(const std::string& id) const { return comp.T.find(id); }
};

// deadlineCol = true で Deadline 列を付ける（false なら Due date を期限として使う）
Proj make(const std::vector<Rw>& rows, const char* todayIso, const char* baseIso = nullptr, const std::string& holidays = "",
          bool deadlineCol = false) {
  std::string text = "Issue Id,Issue Type,Summary,Parent,Assignee,Status,Priority,Start date,Due date,";
  if (deadlineCol) text += "Deadline,";
  text += "Earliest start,Outward issue link (Blocks),Resolved,Duration (weeks)\n";
  for (const Rw& r : rows) {
    text += r.id + "," + r.type + ",課題" + r.id + "," + r.parent + "," + r.asg + "," + r.status + ",Medium," + r.start + "," + r.due + ",";
    if (deadlineCol) text += r.deadline + ",";
    text += r.notBefore + "," + r.blocks + "," + r.resolved + "," + r.weeks + "\n";
  }
  Proj p;
  p.cal = Calendar::fromText(holidays);
  if (!rows.empty()) {
    auto im = csv::importCsv(text);
    REQUIRE(im.ok());
    p.issues = std::move(im.value().issues);
    p.meta = std::move(im.value().meta);
  }
  p.today = D(todayIso);
  ScheduleOptions opt;
  opt.base = baseIso ? D(baseIso) : p.today;
  p.base = opt.base;
  auto c = computeAll(p.issues, p.meta, p.cal, opt);
  REQUIRE(c.ok());
  p.comp = std::move(c).value();
  return p;
}

bool has(const std::vector<std::string>& v, const char* id) { return std::find(v.begin(), v.end(), id) != v.end(); }
bool anywhere(const TodayReport& r, const char* id) {
  return has(r.late, id) || has(r.dueToday, id) || has(r.inProgress, id) || has(r.canStart, id) || has(r.milestones, id);
}
bool inWeek(const TodayReport& r, const char* id) {
  for (const TodayDay& d : r.week)
    if (has(d.ids, id)) return true;
  return false;
}

}  // namespace

// ---------------------------------------------------------------- 週の月曜

TEST_CASE("today: 週の月曜（月曜始まり）") {
  CHECK(mondayOf(D("2026-10-19")) == D("2026-10-19"));  // 月
  CHECK(mondayOf(D("2026-10-21")) == D("2026-10-19"));  // 水
  CHECK(mondayOf(D("2026-10-24")) == D("2026-10-19"));  // 土
  CHECK(mondayOf(D("2026-10-25")) == D("2026-10-19"));  // 日は前の月曜の週
  CHECK(mondayOf(D("2026-10-26")) == D("2026-10-26"));
  CHECK(mondayOf(D("2027-01-01")) == D("2026-12-28"));  // 年またぎ
  CHECK(mondayOf(D("1970-01-01")) == D("1969-12-29"));  // 負の日でも
}

// ---------------------------------------------------------------- 遅れている

TEST_CASE("today: 遅れている — 期限を過ぎて未完了／完了の見込みが期限を過ぎる") {
  const Proj p = make({Rw("past").dueOn("2026-10-16").days(1),      // 昨日（金）までが期限で未完了
                       Rw("yest").dueOn("2026-10-18").days(1),      // 日曜が期限
                       Rw("slip").dueOn("2026-10-23").days(10),     // 見込みは 10/30 で期限を過ぎる
                       Rw("fine").dueOn("2026-10-23").days(3),      // 余裕がある
                       Rw("old").dueOn("2026-10-01").days(1).done().res("2026-10-01")},  // 完了済みは遅れでも出さない
                      "2026-10-19");
  const TodayReport r = p.ok();
  CHECK(has(r.late, "past"));
  CHECK(has(r.late, "yest"));
  CHECK(has(r.late, "slip"));
  CHECK_FALSE(has(r.late, "fine"));
  CHECK_FALSE(anywhere(r, "old"));
}

TEST_CASE("today: 今日が期限なら遅れではない。昨日の期限は遅れ") {
  const Proj p = make({Rw("dueToday").dueOn("2026-10-19").days(1), Rw("dueYest").dueOn("2026-10-16").days(1),
                       Rw("dueTmrw").dueOn("2026-10-20").days(2)},
                      "2026-10-19");
  const TodayReport r = p.ok();
  CHECK(has(r.dueToday, "dueToday"));
  CHECK_FALSE(has(r.late, "dueToday"));
  CHECK(has(r.late, "dueYest"));
  CHECK_FALSE(has(r.dueToday, "dueYest"));
  CHECK_FALSE(has(r.dueToday, "dueTmrw"));  // 明日の期限は「今日が期限」に出さない（2 日かかるので終了予定も明日）
  CHECK_FALSE(has(r.late, "dueTmrw"));
  CHECK(has(r.canStart, "dueTmrw"));        // 今日から始められる
}

// ---------------------------------------------------------------- 今日が期限・終了予定

TEST_CASE("today: 今日が期限・終了予定") {
  // 期限が今日／期限は先だが最早の完了が今日／明日が期限
  const Proj p = make({Rw("a").dueOn("2026-10-19").days(1), Rw("b").dueOn("2026-10-30").days(1), Rw("c").dueOn("2026-10-30").days(2),
                       Rw("d").dueOn("2026-10-30").days(1).nb("2026-10-20")},
                      "2026-10-19");
  const TodayReport r = p.ok();
  CHECK(has(r.dueToday, "a"));
  CHECK(has(r.dueToday, "b"));       // 終了予定が今日（期限は先）
  CHECK_FALSE(has(r.dueToday, "c")); // 終了予定は明日
  CHECK_FALSE(has(r.dueToday, "d")); // 着手は明日
}

TEST_CASE("today: 休業日 — 今日が休みでも期限が今日なら出る。営業日の判定を返す") {
  const Proj p = make({Rw("h").dueOn("2026-10-20").days(1), Rw("w").dueOn("2026-10-21").days(1)}, "2026-10-20", "2026-10-19", "2026-10-20");
  const TodayReport r = p.ok();
  CHECK_FALSE(r.todayIsWork);
  CHECK(has(r.dueToday, "h"));
  CHECK_FALSE(has(r.dueToday, "w"));
  CHECK(r.week[1].day == D("2026-10-20"));
  CHECK(r.week[1].holiday);
  CHECK(r.week[1].isToday);
  CHECK_FALSE(r.week[0].holiday);
  CHECK(make({}, "2026-10-24").ok().todayIsWork == false);  // 土曜
  CHECK(make({}, "2026-10-25").ok().todayIsWork == false);  // 日曜
  CHECK(make({}, "2026-10-19").ok().todayIsWork);
}

TEST_CASE("today: 年またぎ — 今週の列は年をまたぎ、休業日は印がつく") {
  // 2026-12-31（木）。週は 12/28〜1/1。1/1 は休業日
  const Proj p = make({Rw("ny").dueOn("2027-01-01").days(1), Rw("eve").dueOn("2026-12-31").days(1),
                       Rw("mon").dueOn("2026-12-28").days(1).done().res("2026-12-28"), Rw("next").dueOn("2027-01-04").days(1)},
                      "2026-12-31", nullptr, "2027-01-01");
  const TodayReport r = p.ok();
  CHECK(r.weekStart == D("2026-12-28"));
  CHECK(r.week[0].day == D("2026-12-28"));
  CHECK(r.week[3].day == D("2026-12-31"));
  CHECK(r.week[4].day == D("2027-01-01"));
  CHECK(r.week[4].holiday);
  CHECK(r.week[3].isToday);
  CHECK(has(r.dueToday, "eve"));
  CHECK(has(r.week[3].ids, "eve"));
  CHECK(has(r.week[4].ids, "ny"));  // 休業日が期限のものは、その休業日の列に出る
  CHECK_FALSE(inWeek(r, "next"));  // 来週（1/4）は今週の列に出ない
  CHECK_FALSE(inWeek(r, "mon"));   // 完了済みは列にも出ない
}

// ---------------------------------------------------------------- 進行中

TEST_CASE("today: 進行中は余裕の小さい順。遅れ・今日が期限は先にそちらの節へ") {
  const Proj p = make({Rw("p1").prog().from("2026-10-14").days(10).dl("2026-12-31"),   // 余裕あり
                       Rw("p2").prog().from("2026-10-14").days(10).dl("2026-11-06"),   // 余裕小
                       Rw("p3").prog().from("2026-10-14").days(10).dl("2026-10-20"),   // 遅れ
                       Rw("p4").prog().from("2026-10-15").days(3).dl("2026-12-31"),    // 今日が終了予定（10/15+2 営業日 = 10/19）
                       Rw("p5").prog()},                                               // 日付なしでも進行中は出す
                      "2026-10-19", nullptr, "", true);
  const TodayReport r = p.ok();
  REQUIRE(r.inProgress.size() >= 3);
  CHECK(has(r.late, "p3"));
  CHECK_FALSE(has(r.inProgress, "p3"));
  CHECK(has(r.dueToday, "p4"));
  CHECK_FALSE(has(r.inProgress, "p4"));
  CHECK(has(r.inProgress, "p5"));
  const auto pos = [&](const char* id) { return std::find(r.inProgress.begin(), r.inProgress.end(), id) - r.inProgress.begin(); };
  CHECK(pos("p2") < pos("p1"));  // 余裕の小さい順
}

// ---------------------------------------------------------------- 今日から始められる

TEST_CASE("today: 今日から始められる — 先行がすべて完了・最早の開始が今日以前・未着手") {
  const Proj p = make({Rw("pre").dueOn("2026-10-23").days(3).blk("after"),   // 先行（未完了）
                       Rw("after").dueOn("2026-10-30").days(3),              // 先行が未完了 → 始められない
                       Rw("free").dueOn("2026-10-30").days(3),               // 先行なし → 始められる
                       Rw("donePre").dueOn("2026-10-16").days(1).done().res("2026-10-16").blk("ready"),
                       Rw("ready").dueOn("2026-10-30").days(3)},             // 先行が完了 → 始められる
                      "2026-10-19");
  const TodayReport r = p.ok();
  CHECK(has(r.canStart, "pre"));
  CHECK_FALSE(has(r.canStart, "after"));
  CHECK(has(r.canStart, "free"));
  CHECK(has(r.canStart, "ready"));
  CHECK_FALSE(anywhere(r, "donePre"));
}

TEST_CASE("today: 先行が親のとき、配下がすべて完了するまで始められない") {
  // k1・k2 が X を、親 P が Y をブロックする（親への依存は配下のすべての葉に展開される）
  const Proj p = make({Rw("P").dueOn("2026-11-30").blk("Y"), Rw("k1").under("P").dueOn("2026-10-30").days(2).done().res("2026-10-15").blk("X"),
                       Rw("k2").under("P").dueOn("2026-10-30").days(2).blk("X"), Rw("X").dueOn("2026-11-30").days(2),
                       Rw("Y").dueOn("2026-11-30").days(2)},
                      "2026-10-19");
  CHECK_FALSE(has(p.ok().canStart, "X"));  // k2 が未完了
  CHECK_FALSE(has(p.ok().canStart, "Y"));
  CHECK(has(p.ok().canStart, "k2"));
  const Proj q = make({Rw("P").dueOn("2026-11-30").blk("Y"), Rw("k1").under("P").dueOn("2026-10-30").days(2).done().res("2026-10-15").blk("X"),
                       Rw("k2").under("P").dueOn("2026-10-30").days(2).done().res("2026-10-16").blk("X"), Rw("X").dueOn("2026-11-30").days(2),
                       Rw("Y").dueOn("2026-11-30").days(2)},
                      "2026-10-19");
  CHECK(has(q.ok().canStart, "X"));
  CHECK(has(q.ok().canStart, "Y"));
  CHECK_FALSE(anywhere(q.ok(), "P"));  // 親のまとめ行は出さない
}

TEST_CASE("today: 着手可能日・開始日・基準日が先なら、まだ始められない") {
  const Proj p = make({Rw("nbTomorrow").dueOn("2026-11-06").days(3).nb("2026-10-20"), Rw("nbToday").dueOn("2026-11-06").days(3).nb("2026-10-19"),
                       Rw("nbPast").dueOn("2026-11-06").days(3).nb("2026-10-01"),
                       Rw("startFuture").dueOn("2026-11-06").days(3).from("2026-10-26"),   // 開始日が来週
                       Rw("startToday").dueOn("2026-11-06").days(3).from("2026-10-19")},
                      "2026-10-19");
  const TodayReport r = p.ok();
  CHECK_FALSE(has(r.canStart, "nbTomorrow"));
  CHECK(has(r.canStart, "nbToday"));
  CHECK(has(r.canStart, "nbPast"));
  CHECK_FALSE(has(r.canStart, "startFuture"));
  CHECK(has(r.canStart, "startToday"));
  // 基準日が今日より先: 最早の開始は基準日以降なので、始められるものはない
  const Proj q = make({Rw("a").dueOn("2026-11-06").days(3)}, "2026-10-19", "2026-10-21");
  CHECK_FALSE(has(q.ok().canStart, "a"));
}

TEST_CASE("today: 日付のない未着手の課題はどの節にも出ない。進行中だけ出す") {
  const Proj p = make({Rw("none"), Rw("onlyDur").days(3), Rw("prog").prog(), Rw("doneNone").done()}, "2026-10-19");
  const TodayReport r = p.ok();
  CHECK_FALSE(anywhere(r, "none"));
  CHECK_FALSE(anywhere(r, "onlyDur"));
  CHECK(has(r.inProgress, "prog"));
  CHECK_FALSE(anywhere(r, "doneNone"));
  CHECK_FALSE(inWeek(r, "none"));
  CHECK(r.openAll == 3);  // none, onlyDur, prog
}

// ---------------------------------------------------------------- マイルストーン

TEST_CASE("today: 今日のマイルストーン") {
  const Proj p = make({Rw("m1").ms().dueOn("2026-10-19"), Rw("m2").ms().dueOn("2026-10-20"), Rw("m3").ms().dueOn("2026-10-16"),
                       Rw("m4").ms().dueOn("2026-10-19").done().res("2026-10-19"), Rw("t").dueOn("2026-10-19").days(1)},
                      "2026-10-19");
  const TodayReport r = p.ok();
  CHECK(has(r.milestones, "m1"));
  CHECK_FALSE(anywhere(r, "m2"));            // 明日のマイルストーンは出さない
  CHECK(has(r.late, "m3"));                  // 過ぎたマイルストーンは遅れ
  CHECK_FALSE(has(r.milestones, "m3"));
  CHECK_FALSE(anywhere(r, "m4"));            // 完了済み
  CHECK(has(r.dueToday, "t"));
  CHECK_FALSE(has(r.milestones, "t"));
  CHECK_FALSE(has(r.canStart, "m1"));        // マイルストーンは「始められる」に出さない
  CHECK(inWeek(r, "m1"));                    // 週の列には出る（◇）
}

TEST_CASE("today: 1 つの課題は 1 つの節にしか入らない") {
  const Proj p = make({Rw("a").dueOn("2026-10-16").days(1).prog().from("2026-10-14"), Rw("b").prog().dueOn("2026-10-19").from("2026-10-19").days(1),
                       Rw("c").dueOn("2026-10-19").days(1), Rw("d").dueOn("2026-11-19").days(1), Rw("e").ms().dueOn("2026-10-19")},
                      "2026-10-19");
  const TodayReport r = p.ok();
  std::vector<std::string> all;
  for (const auto* v : {&r.late, &r.dueToday, &r.inProgress, &r.canStart, &r.milestones}) all.insert(all.end(), v->begin(), v->end());
  std::vector<std::string> sorted = all;
  std::sort(sorted.begin(), sorted.end());
  CHECK(std::adjacent_find(sorted.begin(), sorted.end()) == sorted.end());
  CHECK(static_cast<int>(all.size()) == r.total());
  CHECK(has(r.late, "a"));
  CHECK(has(r.dueToday, "b"));
}

// ---------------------------------------------------------------- 担当の絞り込み

TEST_CASE("today: 担当の絞り込み（自分／全員）") {
  const Proj p = make({Rw("t1").by("田中").dueOn("2026-10-19").days(1), Rw("s1").by("佐藤").dueOn("2026-10-19").days(1),
                       Rw("n1").dueOn("2026-10-19").days(1), Rw("t2").by("田中").prog().dueOn("2026-12-31").days(3), Rw("s2").by("佐藤").prog().dueOn("2026-12-31").days(3),
                       Rw("tl").by("田中").dueOn("2026-10-12").days(1)},
                      "2026-10-19");
  const TodayReport all = p.ok();
  CHECK(all.total() == 6);
  CHECK(all.openAll == 6);
  TodayFilter f;
  f.byAssignee = true;
  f.assignee = "田中";
  const TodayReport mine = p.ok(f);
  CHECK(has(mine.dueToday, "t1"));
  CHECK_FALSE(has(mine.dueToday, "s1"));
  CHECK_FALSE(has(mine.dueToday, "n1"));  // 担当なしは「自分」には含めない
  CHECK(has(mine.inProgress, "t2"));
  CHECK(has(mine.late, "tl"));
  CHECK(mine.total() == 3);
  CHECK(mine.openAll == 6);  // 絞り込む前の件数（案内用）
  CHECK(inWeek(mine, "t1"));
  CHECK_FALSE(inWeek(mine, "s1"));
  // 前後の空白は無視する
  f.assignee = "  田中 ";
  CHECK(p.ok(f).total() == 3);
  // 該当なし
  f.assignee = "いない人";
  const TodayReport none = p.ok(f);
  CHECK(none.total() == 0);
  CHECK(none.openAll == 6);
  // 名前が空なら担当なしの課題が対象（全員にはならない）
  f.assignee = "";
  CHECK(has(p.ok(f).dueToday, "n1"));
  CHECK(p.ok(f).total() == 1);
}

// ---------------------------------------------------------------- 今週

TEST_CASE("today: 今週の日ごとの列と週のサマリー") {
  // 2026-10-21（水）。週は 10/19〜10/23。列は期限の日（期限がなければ最早の完了の日）
  const Proj p = make({Rw("mon").dueOn("2026-10-19").days(1),
                       Rw("tue").dueOn("2026-10-20").days(1),
                       Rw("wed").dueOn("2026-10-21").days(1),
                       Rw("thu").dueOn("2026-10-22").days(1),
                       Rw("fri").dueOn("2026-10-23").days(1),
                       Rw("sat").dueOn("2026-10-24").days(10),               // 土曜が期限（見込みは先で遅れ）→ 金曜の列
                       Rw("next").dueOn("2026-10-26").days(1).from("2026-10-26").nb("2026-10-26"),   // 来週
                       Rw("last").dueOn("2026-10-14").days(1),               // 先週が期限 → 列に出ない
                       Rw("done").dueOn("2026-10-22").days(1).done().res("2026-10-20")},
                      "2026-10-21", "2026-10-19");
  const TodayReport r = p.ok();
  CHECK(r.weekStart == D("2026-10-19"));
  for (int i = 0; i < kTodayWeekDays; ++i) CHECK(r.week[static_cast<std::size_t>(i)].day == D("2026-10-19") + i);
  CHECK(r.week[2].isToday);
  CHECK(has(r.week[0].ids, "mon"));
  CHECK(has(r.week[1].ids, "tue"));
  CHECK(has(r.week[2].ids, "wed"));
  CHECK(has(r.week[3].ids, "thu"));
  CHECK(has(r.week[4].ids, "fri"));
  CHECK(has(r.week[4].ids, "sat"));
  CHECK_FALSE(inWeek(r, "next"));
  CHECK_FALSE(inWeek(r, "last"));
  CHECK_FALSE(inWeek(r, "done"));
  // サマリー: 列に載る課題の数・うち遅れ（mon・tue は期限を過ぎ、sat は見込みが期限を過ぎる）・うち余裕小。列に載らない遅れは別に数える
  CHECK(r.summary.completing == 6);
  CHECK(r.summary.late == 3);
  CHECK(r.summary.tight == 3);  // wed・thu・fri
  CHECK(r.summary.lateElsewhere == 1);  // last
  int placed = 0;
  for (const auto& d : r.week) placed += static_cast<int>(d.ids.size());
  CHECK(placed == r.summary.completing);
}

TEST_CASE("today: 期限がなければ最早の完了の日の列に出る") {
  const Proj p = make({Rw("pre").days(2).dueOn("2026-11-30").blk("noDue"), Rw("noDue").days(1)}, "2026-10-19");
  const TodayReport r = p.ok();
  CHECK(has(r.week[2].ids, "noDue"));  // 先行（10/19〜10/20）のあと 10/21 に終わる
}

TEST_CASE("today: 今週の列の並び（余裕の小さい順 → 木の順）") {
  const Proj p = make({Rw("a").dueOn("2026-10-21").days(1), Rw("b").dueOn("2026-10-21").days(3), Rw("c").dueOn("2026-10-21").days(3)},
                      "2026-10-19");
  const TodayReport r = p.ok();
  REQUIRE(r.week[2].ids.size() == 3);
  CHECK(r.week[2].ids[0] == "b");  // 余裕 0（3 日かかる）
  CHECK(r.week[2].ids[1] == "c");  // 同じ余裕なら木の順
  CHECK(r.week[2].ids[2] == "a");  // 余裕 2
}

TEST_CASE("today: 進行中の帯 — 週をはみ出す印") {
  const Proj p = make({Rw("in").prog().from("2026-10-20").days(2).dl("2026-12-31"),     // 火〜水
                       Rw("long").prog().from("2026-10-14").days(15).dl("2026-12-31"),  // 先週から来週まで
                       Rw("todo").dl("2026-12-31").days(3)},
                      "2026-10-21", nullptr, "", true);
  const TodayReport r = p.ok();
  REQUIRE(r.bands.size() == 2);
  const auto band = [&](const char* id) -> const TodayBand& {
    for (const auto& b : r.bands)
      if (b.id == id) return b;
    FAIL("帯がない: " << id);
    return r.bands[0];
  };
  CHECK(band("in").first == 1);
  CHECK(band("in").last == 2);
  CHECK_FALSE(band("in").cutLeft);
  CHECK_FALSE(band("in").cutRight);
  CHECK(band("long").first == 0);
  CHECK(band("long").last == 4);
  CHECK(band("long").cutLeft);
  CHECK(band("long").cutRight);
  CHECK(r.bands[0].first <= r.bands[1].first);  // 開始の早い順
}

TEST_CASE("today: 今日が日曜なら、その週は前の月曜から") {
  const Proj p = make({Rw("fri").dueOn("2026-10-23").days(1)}, "2026-10-25", "2026-10-19");
  const TodayReport r = p.ok();
  CHECK(r.weekStart == D("2026-10-19"));
  CHECK_FALSE(r.todayIsWork);
  CHECK_FALSE(r.week[0].isToday);
  CHECK_FALSE(r.week[4].isToday);  // 日曜は列にない
}

// ---------------------------------------------------------------- エラー・空

TEST_CASE("today: 空のプロジェクト・添字の食い違い") {
  const Proj e = make({}, "2026-10-19");
  const TodayReport r = e.ok();
  CHECK(r.total() == 0);
  CHECK(r.openAll == 0);
  CHECK(r.bands.empty());
  const Proj p = make({Rw("a").dueOn("2026-10-19").days(1)}, "2026-10-19");
  std::vector<Issue> shorter = p.issues;
  shorter.pop_back();
  CHECK_FALSE(computeToday(p.comp.T, shorter, p.comp.R, p.cal, p.today).ok());
  std::vector<ScheduleRow> rows = p.comp.R;
  rows.pop_back();
  CHECK_FALSE(computeToday(p.comp.T, p.issues, rows, p.cal, p.today).ok());
}

// ---------------------------------------------------------------- 5,000 件

TEST_CASE("today: 5,000 件を短い時間で集計する") {
  auto g = generateProject({5000, 7});
  const Day today = D("2026-10-19");
  for (std::size_t i = 0; i < g.issues.size(); ++i) {
    Issue& s = g.issues[i];
    if (i % 9 == 0 && statusCat(s.status) != Cat::Done) s.deadline = today + static_cast<Day>(i % 7) - 3;
  }
  Proj p;
  p.issues = std::move(g.issues);
  p.meta = std::move(g.meta);
  p.base = today;
  p.today = today;
  ScheduleOptions opt;
  opt.base = today;
  auto c = computeAll(p.issues, p.meta, p.cal, opt);
  REQUIRE(c.ok());
  p.comp = std::move(c).value();
  REQUIRE(p.comp.T.n == 5000);

  const auto t0 = std::chrono::steady_clock::now();
  const auto res = p.run();
  const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  MESSAGE("today 5000 件: " << ms << " ms");
  REQUIRE(res.ok());
  CHECK(res->total() > 0);
  CHECK(res->openAll > 0);
  std::vector<std::string> all;
  for (const auto* v : {&res->late, &res->dueToday, &res->inProgress, &res->canStart, &res->milestones}) all.insert(all.end(), v->begin(), v->end());
  std::sort(all.begin(), all.end());
  CHECK(std::adjacent_find(all.begin(), all.end()) == all.end());
  CHECK(ms < 500);  // 目標は Release で 50 ms 以内（Debug・サニタイザ付きでも 500 ms は超えない）
}

// ---------------------------------------------------------------- ずらす

TEST_CASE("today: 明日にずらす — 開始・期限は営業日で後ろへ、着手可能日は明日以降") {
  const Calendar cal;
  Issue o;
  o.id = "1";
  o.due = D("2026-10-19");
  const ShiftEdit e = planShift(o, Cat::Todo, cal, D("2026-10-19"), ShiftTo::Tomorrow);
  CHECK(e.changed);
  CHECK(e.target == D("2026-10-20"));
  CHECK(e.days == 1);
  CHECK(e.due == D("2026-10-20"));
  CHECK(e.notBefore == D("2026-10-20"));
  CHECK_FALSE(e.start.has_value());
  CHECK_FALSE(e.deadline.has_value());
}

TEST_CASE("today: 金曜の明日は月曜。休業日は飛ばす") {
  const Calendar cal = Calendar::fromText("2026-10-26");
  Issue o;
  o.due = D("2026-10-23");
  o.start = D("2026-10-21");
  const ShiftEdit e = planShift(o, Cat::Todo, cal, D("2026-10-23"), ShiftTo::Tomorrow);
  CHECK(e.target == D("2026-10-27"));  // 月曜 10/26 は休業日
  CHECK(e.due == D("2026-10-27"));
  CHECK(e.start == D("2026-10-27"));  // 21 + 1 営業日 = 22 だが、ずらす先より前には置かない
  CHECK(e.notBefore == D("2026-10-27"));
}

TEST_CASE("today: 来週にずらす — 5 営業日あと。過ぎた日付は来週の最初の営業日に揃える") {
  const Calendar cal;
  Issue o;
  o.start = D("2026-10-14");
  o.due = D("2026-10-21");
  o.deadline = D("2026-10-30");
  o.notBefore = D("2026-11-09");  // すでに先
  const ShiftEdit e = planShift(o, Cat::Todo, cal, D("2026-10-21"), ShiftTo::NextWeek);
  CHECK(e.target == D("2026-10-26"));  // 来週の月曜
  CHECK(e.days == 5);
  CHECK(e.start == D("2026-10-26"));   // 14 + 5 営業日 = 21 だが来週の最初の営業日に揃える
  CHECK(e.due == D("2026-10-28"));     // 21 + 5 営業日
  CHECK(e.deadline == D("2026-11-06"));
  CHECK(e.notBefore == D("2026-11-09"));  // すでに後ろならそのまま
}

TEST_CASE("today: 進行中は開始と着手可能日を変えない。完了は何も変えない") {
  const Calendar cal;
  Issue o;
  o.start = D("2026-10-14");
  o.due = D("2026-10-19");
  const ShiftEdit e = planShift(o, Cat::Prog, cal, D("2026-10-19"), ShiftTo::Tomorrow);
  CHECK(e.changed);
  CHECK(e.start == D("2026-10-14"));
  CHECK(e.due == D("2026-10-20"));
  CHECK_FALSE(e.notBefore.has_value());
  const ShiftEdit d = planShift(o, Cat::Done, cal, D("2026-10-19"), ShiftTo::Tomorrow);
  CHECK_FALSE(d.changed);
  CHECK(d.due == o.due);
  // 進行中で日付がなければ変えるものがない
  Issue none;
  CHECK_FALSE(planShift(none, Cat::Prog, cal, D("2026-10-19"), ShiftTo::Tomorrow).changed);
  // 未着手で日付がなくても着手可能日は入る
  CHECK(planShift(none, Cat::Todo, cal, D("2026-10-19"), ShiftTo::Tomorrow).notBefore == D("2026-10-20"));
}

TEST_CASE("today: ずらす操作は Undo 1 回で戻り、今日の節から外れる") {
  Proj p = make({Rw("a").dueOn("2026-10-30").days(2), Rw("b").dueOn("2026-10-30").days(2)}, "2026-10-19");
  FixedClock clk{LocalTime{2026, 10, 19, 9, 0, 0}};
  ProjectData pd;
  pd.issues = p.issues;
  pd.meta = p.meta;
  pd.settings.baseDate = "2026-10-19";
  Session s(std::move(pd), &clk);
  REQUIRE(s.tree() != nullptr);
  const Day today = D("2026-10-19");
  const auto sec = [&] { return computeToday(*s.tree(), s.viewIssues(), *s.rows(), s.calendar(), today).value(); };
  REQUIRE(has(sec().canStart, "a"));
  const std::vector<Issue> before = s.issues();
  const std::size_t undos = s.undoSize();

  const ShiftResult r = shiftIssue(s, "a", ShiftTo::Tomorrow, today);
  CHECK(r.ok);
  CHECK(r.target == D("2026-10-20"));
  CHECK(s.undoSize() == undos + 1);  // Undo 1 回ぶん
  CHECK(s.issue("a")->notBefore == D("2026-10-20"));
  CHECK(s.issue("a")->due == D("2026-11-02"));  // 期限 10/30（金）の次の営業日
  CHECK_FALSE(has(sec().canStart, "a"));
  CHECK(has(sec().canStart, "b"));
  CHECK(s.undo());
  CHECK(s.issues() == before);
  CHECK(has(sec().canStart, "a"));
}

TEST_CASE("today: ずらせないとき（親・完了・存在しない）は何も変えず通知する") {
  Proj p = make({Rw("P").dueOn("2026-11-30"), Rw("k").under("P").dueOn("2026-10-30"), Rw("d").done().res("2026-10-15").dueOn("2026-10-15")}, "2026-10-19");
  FixedClock clk{LocalTime{2026, 10, 19, 9, 0, 0}};
  ProjectData pd;
  pd.issues = p.issues;
  pd.meta = p.meta;
  pd.settings.baseDate = "2026-10-19";
  Session s(std::move(pd), &clk);
  const std::vector<Issue> before = s.issues();
  const std::size_t undos = s.undoSize();
  const Day today = D("2026-10-19");
  for (const char* id : {"P", "d", "nothing"}) {
    const ShiftResult r = shiftIssue(s, id, ShiftTo::Tomorrow, today);
    CHECK_FALSE(r.ok);
    CHECK_FALSE(s.takeNotices().empty());
  }
  CHECK(s.issues() == before);
  CHECK(s.undoSize() == undos);
}

TEST_CASE("today: 先行が終わるまで待つときは通知に出す。後続が動いたら数える") {
  Proj p = make({Rw("pre").dueOn("2026-10-23").days(5).blk("mid"), Rw("mid").dueOn("2026-11-06").days(2).blk("post"),
                 Rw("post").dueOn("2026-11-20").days(2)},
                "2026-10-19");
  FixedClock clk{LocalTime{2026, 10, 19, 9, 0, 0}};
  ProjectData pd;
  pd.issues = p.issues;
  pd.meta = p.meta;
  pd.settings.baseDate = "2026-10-19";
  Session s(std::move(pd), &clk);
  const Day today = D("2026-10-19");
  // mid は pre（10/19〜10/23）が終わるまで始められない。「明日」にずらしても 10/26 から
  const ShiftResult r = shiftIssue(s, "mid", ShiftTo::Tomorrow, today);
  CHECK(r.ok);
  CHECK(r.waited);
  const auto notes = s.takeNotices();
  REQUIRE_FALSE(notes.empty());
  bool mentionsWait = false;
  for (const auto& n : notes) mentionsWait |= n.text.find("先行") != std::string::npos;
  CHECK(mentionsWait);
  // pre を 1 週間ずらすと、後続（mid・post）の日程が動く
  const ShiftResult r2 = shiftIssue(s, "pre", ShiftTo::NextWeek, today);
  CHECK(r2.ok);
  CHECK(r2.moved >= 2);
  CHECK(s.undo());
  CHECK(s.undo());
}
