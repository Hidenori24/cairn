// 進み方の推移（バーンダウン・リードタイム・滞留日数。Issue #54 の A）
#include <doctest/doctest.h>

#include <chrono>
#include <string>
#include <utility>
#include <vector>

#include "roadmap/csv.hpp"
#include "roadmap/gen.hpp"
#include "roadmap/schedule.hpp"
#include "roadmap/trends.hpp"

using namespace roadmap;

namespace {

Day D(const char* iso) { return *parseIso(iso); }

struct Rec {
  std::string id;
  std::string asg, status, created, updated, resolved, start, parent, type;
  Rec(std::string i, std::string st = "To Do", std::string cr = "", std::string rs = "")
      : id(std::move(i)), status(std::move(st)), created(std::move(cr)), resolved(std::move(rs)) {}
  Rec& by(std::string a) {
    asg = std::move(a);
    return *this;
  }
  Rec& upd(std::string u) {
    updated = std::move(u);
    return *this;
  }
  Rec& from(std::string s) {
    start = std::move(s);
    return *this;
  }
  Rec& under(std::string p) {
    parent = std::move(p);
    return *this;
  }
  Rec& ty(std::string t) {
    type = std::move(t);
    return *this;
  }
};

struct Proj {
  std::vector<Issue> issues;
  ProjectMeta meta;
  Computed comp;
  Day base = 0;
  Calendar cal;
  int idx(const std::string& id) const { return comp.T.find(id); }
  Result<TrendReport> run(const TrendFilter& f = {}) const {
    return computeTrends(comp.T, issues, meta, comp.R, cal, base, f);
  }
  TrendReport ok(const TrendFilter& f = {}) const {
    auto r = run(f);
    REQUIRE(r.ok());
    return std::move(r).value();
  }
};

// 列見出しを変えられるようにする（"Created" / "作成日" など）
Proj make(const std::vector<Rec>& rows, const char* baseIso, const Calendar& cal = Calendar(), const std::string& createdHdr = "Created",
          const std::string& updatedHdr = "Updated") {
  std::string text = "Issue Id,Issue Type,Summary,Parent,Assignee,Status,Priority,Start date,";
  if (!createdHdr.empty()) text += createdHdr + ",";
  if (!updatedHdr.empty()) text += updatedHdr + ",";
  text += "Resolved,Duration (weeks)\n";
  for (const Rec& r : rows) {
    text += r.id + "," + (r.type.empty() ? "Task" : r.type) + ",課題" + r.id + "," + r.parent + "," + r.asg + "," + r.status + ",Medium," + r.start + ",";
    if (!createdHdr.empty()) text += r.created + ",";
    if (!updatedHdr.empty()) text += r.updated + ",";
    text += r.resolved + ",1\n";
  }
  Proj p;
  p.cal = cal;
  if (!rows.empty()) {
    auto im = csv::importCsv(text);
    REQUIRE(im.ok());
    p.issues = std::move(im.value().issues);
    p.meta = std::move(im.value().meta);
  }
  ScheduleOptions opt;
  opt.base = D(baseIso);
  p.base = opt.base;
  auto c = computeAll(p.issues, p.meta, cal, opt);
  REQUIRE(c.ok());
  p.comp = std::move(c).value();
  return p;
}

// 営業日を k 日進めた日（土日を除く。休業日なし）
Day addBiz(Day d, int k) {
  const Calendar cal;
  return cal.workday(d, k);
}

std::string iso(Day d) { return fmtIso(d); }

}  // namespace

// ---------------------------------------------------------------- 階級

TEST_CASE("trends: 階級の境界値（0-1 / 2-4 / 5-9 / 10-19 / 20+）") {
  CHECK(trendBucket(-3) == 0);
  CHECK(trendBucket(0) == 0);
  CHECK(trendBucket(1) == 0);
  CHECK(trendBucket(2) == 1);
  CHECK(trendBucket(4) == 1);
  CHECK(trendBucket(5) == 2);
  CHECK(trendBucket(9) == 2);
  CHECK(trendBucket(10) == 3);
  CHECK(trendBucket(19) == 3);
  CHECK(trendBucket(20) == 4);
  CHECK(trendBucket(1000000) == 4);
  CHECK(std::string(trendBucketLabel(0)) == "0-1");
  CHECK(std::string(trendBucketLabel(4)) == "20+");
  CHECK(std::string(trendBucketLabel(5)).empty());
  CHECK(std::string(trendBucketLabel(-1)).empty());
}

// ---------------------------------------------------------------- 空・日付なし

TEST_CASE("trends: 空のプロジェクトでも落ちない") {
  const Proj p = make({}, "2026-10-19");
  const TrendReport r = p.ok();
  CHECK(r.items == 0);
  CHECK(r.burn.weeks.empty());
  CHECK(r.burn.nowIndex == -1);
  CHECK(r.burn.remainingNow == 0);
  CHECK_FALSE(r.burn.forecast.ok);
  CHECK(r.burn.forecast.reason == ForecastReason::NoData);
  CHECK(r.lead.n == 0);
  CHECK(r.lead.median == 0);
  CHECK(r.lead.p90 == 0);
  CHECK(r.aging.n == 0);
  CHECK(r.aging.top.empty());
  CHECK_FALSE(r.hasCreatedColumn);
}

TEST_CASE("trends: Created 列がない CSV は、日付なしとして数えない") {
  const Proj p = make({Rec("1"), Rec("2", "Done", "", "2026-10-12"), Rec("3").upd("2026-10-13")}, "2026-10-19", Calendar(), "", "Updated");
  const TrendReport r = p.ok();
  CHECK(r.items == 3);
  CHECK_FALSE(r.hasCreatedColumn);
  CHECK(r.hasUpdatedColumn);
  CHECK(r.burn.noCreated == 3);
  CHECK(r.burn.weeks.empty());
  CHECK(r.lead.n == 0);
  CHECK(r.lead.noDates == 1);  // 完了だが作成日がない
  // 滞留: 2 は完了。1 は日付がなく数えない。3 は Updated がある
  CHECK(r.aging.n == 1);
  CHECK(r.aging.noDate == 1);
  REQUIRE(r.aging.top.size() == 1);
  CHECK(r.aging.top[0].id == "3");
}

TEST_CASE("trends: 日付のない課題は数えず、件数を返す") {
  const Proj p = make({Rec("1", "To Do", "2026-10-06"), Rec("2"), Rec("3", "Done", "", "2026-10-13"), Rec("4", "Done", "2026-10-06", "")}, "2026-10-19");
  const TrendReport r = p.ok();
  CHECK(r.hasCreatedColumn);
  CHECK(r.items == 4);
  CHECK(r.burn.noCreated == 2);   // 2 と 3
  CHECK(r.burn.noResolved == 1);  // 4（完了だが完了日がない）
  REQUIRE(r.burn.weeks.size() == 3);  // 10/05, 10/12, 10/19
  CHECK(r.burn.remainingNow == 1);    // 数えたのは 1 だけ
  CHECK(r.lead.n == 0);
  CHECK(r.lead.noDates == 2);  // 3（作成日なし）と 4（完了日なし）
  CHECK(r.aging.n == 1);       // 未完了の 1（Created から数える）
  CHECK(r.aging.noDate == 1);  // 2 は日付がない
}

TEST_CASE("trends: 列の見出しは日本語（作成日・更新日）でもよい") {
  const Proj p = make({Rec("1", "To Do", "2026-10-06").upd("2026-10-14")}, "2026-10-19", Calendar(), "作成日", "更新日");
  const TrendReport r = p.ok();
  CHECK(r.hasCreatedColumn);
  CHECK(r.hasUpdatedColumn);
  CHECK(r.burn.remainingNow == 1);
  REQUIRE(r.aging.top.size() == 1);
  CHECK(r.aging.top[0].days == 3);  // 10/14(水) → 10/19(月): 木・金・月
}

// ---------------------------------------------------------------- バーンダウン

TEST_CASE("trends: 週ごとの作った数・完了した数・残り（基準日の週は基準日まで、未来の作成日は除く）") {
  const Proj p = make({Rec("a", "Done", "2026-10-05", "2026-10-07"), Rec("b", "To Do", "2026-10-06"), Rec("c", "Done", "2026-10-13", "2026-10-18"),
                       Rec("d", "To Do", "2026-10-19"), Rec("e", "To Do", "2026-10-20")},
                      "2026-10-19");
  const TrendReport r = p.ok();
  REQUIRE(r.burn.weeks.size() == 3);
  CHECK(r.burn.nowIndex == 2);
  CHECK(r.burn.weeks[0].start == D("2026-10-05"));
  CHECK(r.burn.weeks[1].start == D("2026-10-12"));
  CHECK(r.burn.weeks[2].start == D("2026-10-19"));
  CHECK(r.burn.weeks[0].created == 2);
  CHECK(r.burn.weeks[0].resolved == 1);
  CHECK(r.burn.weeks[0].remaining == 1);
  CHECK(r.burn.weeks[1].created == 1);
  CHECK(r.burn.weeks[1].resolved == 1);  // 日曜 10/18 は 10/12 の週
  CHECK(r.burn.weeks[1].remaining == 1);
  CHECK(r.burn.weeks[2].created == 1);
  CHECK(r.burn.weeks[2].resolved == 0);
  CHECK(r.burn.weeks[2].remaining == 2);
  CHECK(r.burn.weeks[2].partial);
  CHECK_FALSE(r.burn.weeks[0].partial);
  CHECK(r.burn.remainingNow == 2);
  CHECK(r.burn.future == 1);  // e は作成日が基準日より先
}

TEST_CASE("trends: 週の境（月曜の 0 時と日曜の 23 時台。時刻つきの値でも日で数える）") {
  const Proj p = make({Rec("p", "To Do", "2026-10-11 23:59"), Rec("q", "To Do", "2026-10-12 00:00"), Rec("r", "To Do", "2026-10-18 23:59"),
                       Rec("s", "To Do", "2026-10-19 00:00")},
                      "2026-10-21");
  const TrendReport r = p.ok();
  REQUIRE(r.burn.weeks.size() == 3);
  CHECK(r.burn.weeks[0].start == D("2026-10-05"));
  CHECK(r.burn.weeks[0].created == 1);  // p（日曜）
  CHECK(r.burn.weeks[1].created == 2);  // q（月曜）と r（日曜）
  CHECK(r.burn.weeks[2].created == 1);  // s（月曜）
  CHECK(r.burn.weeks[2].remaining == 4);
}

TEST_CASE("trends: 年またぎ") {
  const Proj p = make({Rec("a", "Done", "2025-12-29", "2026-01-02"), Rec("b", "To Do", "2025-12-31"), Rec("c", "To Do", "2026-01-06")}, "2026-01-07");
  const TrendReport r = p.ok();
  REQUIRE(r.burn.weeks.size() == 2);
  CHECK(r.burn.weeks[0].start == D("2025-12-29"));
  CHECK(r.burn.weeks[1].start == D("2026-01-05"));
  CHECK(r.burn.weeks[0].created == 2);
  CHECK(r.burn.weeks[0].resolved == 1);
  CHECK(r.burn.weeks[0].remaining == 1);
  CHECK(r.burn.weeks[1].created == 1);
  CHECK(r.burn.weeks[1].remaining == 2);
}

TEST_CASE("trends: 26 週より前のものは、最初の週の残りに入る（作った数には入れない）") {
  const Proj p = make({Rec("old", "To Do", "2025-01-06"), Rec("older", "Done", "2024-12-02", "2025-02-03"), Rec("new", "To Do", "2026-10-13")}, "2026-10-19");
  const TrendReport r = p.ok();
  REQUIRE(r.burn.weeks.size() == static_cast<std::size_t>(kTrendPastWeeks + 1));
  CHECK(r.burn.weeks.front().start == D("2026-10-19") - 7 * kTrendPastWeeks);
  CHECK(r.burn.weeks.front().created == 0);
  CHECK(r.burn.weeks.front().resolved == 0);
  CHECK(r.burn.weeks.front().remaining == 1);  // old だけが残っている
  CHECK(r.burn.weeks.back().remaining == 2);
  CHECK(r.burn.remainingNow == 2);
}

TEST_CASE("trends: すべて作成日が基準日より先なら、週は出さない") {
  const Proj p = make({Rec("a", "To Do", "2026-11-02"), Rec("b", "To Do", "2026-12-01")}, "2026-10-19");
  const TrendReport r = p.ok();
  CHECK(r.burn.future == 2);
  CHECK(r.burn.weeks.empty());
  CHECK(r.burn.forecast.reason == ForecastReason::NoData);
}

TEST_CASE("trends: 完了日が作成日より前のものは、日付の矛盾として数えない") {
  const Proj p = make({Rec("a", "Done", "2026-10-13", "2026-10-09"), Rec("b", "To Do", "2026-10-13"), Rec("c", "Done", "2026-10-13", "2026-10-13")}, "2026-10-19");
  const TrendReport r = p.ok();
  CHECK(r.burn.inconsistent == 1);
  CHECK(r.lead.inconsistent == 1);
  CHECK(r.lead.n == 1);  // c（同じ日）は 0 営業日
  CHECK(r.lead.maxDays == 0);
  REQUIRE(r.burn.weeks.size() == 2);
  CHECK(r.burn.weeks[0].created == 2);  // b と c
  CHECK(r.burn.weeks[0].resolved == 1); // c
  CHECK(r.burn.remainingNow == 1);
}

TEST_CASE("trends: 完了日があっても状況が完了でない課題の完了日は数えない") {
  const Proj p = make({Rec("a", "In Progress", "2026-10-06", "2026-10-08")}, "2026-10-19");
  const TrendReport r = p.ok();
  CHECK(r.burn.remainingNow == 1);
  CHECK(r.lead.n == 0);
  CHECK(r.lead.noDates == 0);
}

TEST_CASE("trends: マイルストーンは数えない") {
  const Proj p = make({Rec("a", "To Do", "2026-10-06"), Rec("m", "To Do", "2026-10-06").ty("Milestone")}, "2026-10-19");
  const TrendReport r = p.ok();
  CHECK(r.items == 1);
  CHECK(r.burn.remainingNow == 1);
  CHECK(r.aging.n == 1);
}

// ---------------------------------------------------------------- ペース予測

namespace {

// 基準日 2026-10-19（月）。直近 4 週は 9/21・9/28・10/5・10/12 の週。
// open 件の未完了（9/1 作成）と、week（週の月曜）ごとの完了数
Proj paceProject(int open, const std::vector<std::pair<const char*, int>>& doneByWeek) {
  std::vector<Rec> rows;
  int n = 0;
  for (int i = 0; i < open; ++i) rows.push_back(Rec("o" + std::to_string(i), "To Do", "2026-09-01"));
  for (const auto& [wk, cnt] : doneByWeek)
    for (int i = 0; i < cnt; ++i) {
      const Day d = D(wk) + 2;  // 水曜に完了
      rows.push_back(Rec("d" + std::to_string(n++), "Done", "2026-09-01", iso(d)));
    }
  return make(rows, "2026-10-19");
}

}  // namespace

TEST_CASE("trends: ペース予測 — 直近 4 週の平均で残りを割る") {
  const Proj p = paceProject(8, {{"2026-10-12", 2}, {"2026-10-05", 2}});
  const TrendReport r = p.ok();
  const TrendForecast& f = r.burn.forecast;
  REQUIRE(f.ok);
  CHECK(f.reason == ForecastReason::None);
  CHECK(r.burn.remainingNow == 8);
  CHECK(f.windowDone == 4);
  CHECK(f.activeWeeks == 2);
  CHECK(f.perWeek == doctest::Approx(1.0));
  CHECK(f.weeksAhead == 8);
  CHECK(f.finishWeek == D("2026-10-19") + 7 * 8);
}

TEST_CASE("trends: ペース予測 — 割り切れないときは切り上げる／基準日の週の完了は平均に入れない") {
  // 直近 4 週で 6 件（週 3 + 3 → 平均 1.5）、残り 7 → 7 ÷ 1.5 = 4.67 → 5 週。基準日の週に 10 件完了していても平均は変わらない
  std::vector<Rec> rows;
  for (int i = 0; i < 7; ++i) rows.push_back(Rec("o" + std::to_string(i), "To Do", "2026-09-01"));
  int n = 0;
  for (const char* wk : {"2026-10-12", "2026-09-21"})
    for (int i = 0; i < 3; ++i) rows.push_back(Rec("d" + std::to_string(n++), "Done", "2026-09-01", iso(D(wk) + 1)));
  for (int i = 0; i < 10; ++i) rows.push_back(Rec("n" + std::to_string(n++), "Done", "2026-09-01", "2026-10-19"));
  const Proj p = make(rows, "2026-10-19");
  const TrendReport r = p.ok();
  REQUIRE(r.burn.forecast.ok);
  CHECK(r.burn.forecast.windowDone == 6);
  CHECK(r.burn.forecast.perWeek == doctest::Approx(1.5));
  CHECK(r.burn.remainingNow == 7);
  CHECK(r.burn.forecast.weeksAhead == 5);
}

TEST_CASE("trends: ペース予測を出さない — 完了のある週が 2 週未満") {
  const Proj p = paceProject(5, {{"2026-10-12", 4}});
  const TrendForecast f = p.ok().burn.forecast;
  CHECK_FALSE(f.ok);
  CHECK(f.reason == ForecastReason::FewActiveWeeks);
  CHECK(f.activeWeeks == 1);
  CHECK(f.windowDone == 4);
}

TEST_CASE("trends: ペース予測を出さない — 直近に完了がない（平均が 0）") {
  const Proj p = paceProject(5, {{"2026-09-14", 3}, {"2026-10-19", 2}});  // 4 週の外と、基準日の週だけ
  const TrendForecast f = p.ok().burn.forecast;
  CHECK_FALSE(f.ok);
  CHECK(f.reason == ForecastReason::NoCompletions);
  CHECK(f.windowDone == 0);
}

TEST_CASE("trends: ペース予測を出さない — 残りがない／先すぎる／データなし") {
  {
    const Proj p = paceProject(0, {{"2026-10-12", 2}, {"2026-10-05", 2}});
    CHECK(p.ok().burn.forecast.reason == ForecastReason::Finished);
    CHECK_FALSE(p.ok().burn.forecast.ok);
  }
  {
    const Proj p = paceProject(300, {{"2026-10-12", 1}, {"2026-10-05", 1}});  // 平均 0.5 件/週 → 600 週
    CHECK(p.ok().burn.forecast.reason == ForecastReason::TooFar);
  }
  {
    // ちょうど上限（104 週）は出す: 平均 1 件/週で残り 104
    const Proj p = paceProject(104, {{"2026-10-12", 2}, {"2026-10-05", 2}});
    const TrendForecast f = p.ok().burn.forecast;
    CHECK(f.ok);
    CHECK(f.weeksAhead == 104);
  }
  {
    const Proj p = make({}, "2026-10-19");
    CHECK(p.ok().burn.forecast.reason == ForecastReason::NoData);
  }
}

// ---------------------------------------------------------------- リードタイム

TEST_CASE("trends: リードタイムは営業日（作成日が休業日・週末でも落ちない）") {
  const Calendar hol = Calendar::fromText("2026-10-12");  // 月曜が休業日
  const Proj p = make({Rec("a", "Done", "2026-10-09", "2026-10-12"),   // 金 → 休業の月: 0
                       Rec("b", "Done", "2026-10-09", "2026-10-13"),   // 金 → 火: 休業の月をまたぐので 1
                       Rec("c", "Done", "2026-10-10", "2026-10-13"),   // 土 → 火: 1
                       Rec("d", "Done", "2026-10-12", "2026-10-14"),   // 休業日 → 水: 火・水 = 2
                       Rec("e", "Done", "2026-10-10", "2026-10-11"),   // 土 → 日: 0
                       Rec("f", "Done", "2026-10-14", "2026-10-14"),   // 同じ日: 0
                       Rec("g", "Done", "2026-10-05", "2026-10-16")},  // 月 → 次の金（休業日をはさむ）: 火〜金 + 火〜金 の 8... 下で確認
                      "2026-10-19", hol);
  const TrendReport r = p.ok();
  CHECK(r.lead.n == 7);
  CHECK(r.lead.inconsistent == 0);
  // 昇順: 0,0,0,1,1,2,8（g: 10/5 の翌日〜10/16 の営業日 = 10/6〜10/9 の 4 日 + 10/13〜10/16 の 4 日。10/12 は休業日）
  CHECK(r.lead.maxDays == 8);
  CHECK(r.lead.median == doctest::Approx(1.0));
  CHECK(r.lead.hist[0] == 5);  // 0,0,0,1,1
  CHECK(r.lead.hist[1] == 1);  // 2
  CHECK(r.lead.hist[2] == 1);  // 8
}

TEST_CASE("trends: 中央値と 90 パーセンタイル（nearest-rank）の端 — 件数 1・2・10・11") {
  const Day mon = D("2026-06-01");
  const auto lead = [&](std::vector<int> ks) {
    std::vector<Rec> rows;
    int n = 0;
    for (int k : ks) rows.push_back(Rec("x" + std::to_string(n++), "Done", iso(mon), iso(addBiz(mon, k))));
    return make(rows, "2026-10-19").ok().lead;
  };
  {
    const TrendLead l = lead({7});
    CHECK(l.n == 1);
    CHECK(l.median == doctest::Approx(7));
    CHECK(l.p90 == 7);
  }
  {
    const TrendLead l = lead({9, 3});
    CHECK(l.n == 2);
    CHECK(l.median == doctest::Approx(6));
    CHECK(l.p90 == 9);  // ceil(0.9 × 2) = 2 番目
  }
  {
    const TrendLead l = lead({10, 1, 2, 3, 4, 5, 6, 7, 8, 9});
    CHECK(l.n == 10);
    CHECK(l.median == doctest::Approx(5.5));
    CHECK(l.p90 == 9);  // ceil(0.9 × 10) = 9 番目（浮動小数点の誤差で 10 番目にならない）
  }
  {
    const TrendLead l = lead({1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11});
    CHECK(l.n == 11);
    CHECK(l.median == doctest::Approx(6));
    CHECK(l.p90 == 10);  // ceil(9.9) = 10 番目
  }
  {
    const TrendLead l = lead({});
    CHECK(l.n == 0);
    CHECK(l.p90 == 0);
  }
}

TEST_CASE("trends: リードタイムの階級別の度数（境界 0,1,2,4,5,9,10,19,20,30）") {
  const Day mon = D("2026-06-01");
  std::vector<Rec> rows;
  int n = 0;
  for (int k : {0, 1, 2, 4, 5, 9, 10, 19, 20, 30}) rows.push_back(Rec("x" + std::to_string(n++), "Done", iso(mon), iso(addBiz(mon, k))));
  const TrendLead l = make(rows, "2026-10-19").ok().lead;
  CHECK(l.n == 10);
  CHECK(l.hist[0] == 2);
  CHECK(l.hist[1] == 2);
  CHECK(l.hist[2] == 2);
  CHECK(l.hist[3] == 2);
  CHECK(l.hist[4] == 2);
}

// ---------------------------------------------------------------- 滞留日数

TEST_CASE("trends: 滞留日数 — Updated → 開始日 → Created の順。未来の日付は 0 日") {
  const Proj p = make({Rec("A", "To Do", "2026-09-01").upd("2026-10-12"),                 // 10/13〜10/19 の営業日 = 5
                       Rec("B", "In Progress", "2026-09-01").from("2026-10-14"),          // 木・金・月 = 3
                       Rec("C", "To Do", "2026-09-21"),                                   // 4 週 = 20
                       Rec("D", "To Do", "2026-09-01").upd("2026-10-25"),                 // 未来 = 0
                       Rec("E"),                                                          // 日付なし
                       Rec("F", "Done", "2026-09-01", "2026-10-01"),                      // 完了は数えない
                       Rec("G", "To Do", "2026-09-01").upd("2026-10-15").from("2026-10-01"),  // Updated が先: 金・月 = 2
                       Rec("M", "To Do", "2026-09-01").ty("Milestone")},                  // マイルストーンは数えない
                      "2026-10-19");
  const TrendAging a = p.ok().aging;
  CHECK(a.n == 5);
  CHECK(a.noDate == 1);
  REQUIRE(a.top.size() == 5);
  CHECK(a.top[0].id == "C");
  CHECK(a.top[0].days == 20);
  CHECK(a.top[1].id == "A");
  CHECK(a.top[1].days == 5);
  CHECK(a.top[2].id == "B");
  CHECK(a.top[2].days == 3);
  CHECK(a.top[3].id == "G");
  CHECK(a.top[3].days == 2);
  CHECK(a.top[4].id == "D");
  CHECK(a.top[4].days == 0);
  CHECK(a.top[0].name == p.comp.T.name[static_cast<std::size_t>(p.idx("C"))]);
  CHECK(a.top[0].idx == p.idx("C"));
  CHECK(a.top[0].from == AgingFrom::Created);
  CHECK(a.top[0].last == D("2026-09-21"));
  CHECK(a.top[1].from == AgingFrom::Updated);
  CHECK(a.top[2].from == AgingFrom::Start);
  CHECK(a.top[2].last == D("2026-10-14"));
  CHECK(a.hist[0] == 1);  // D（0）
  CHECK(a.hist[1] == 2);  // G（2）・B（3）
  CHECK(a.hist[2] == 1);  // A（5）
  CHECK(a.hist[3] == 0);
  CHECK(a.hist[4] == 1);  // C（20）
}

TEST_CASE("trends: 滞留日数の階級別の度数と、上位 10 件（同じ日数は課題の並び順）") {
  std::vector<Rec> rows;
  // 日数 = 0..11 の 12 件（Updated を基準日から営業日ぶん戻す）
  const Day base = D("2026-10-19");
  for (int k = 0; k < 12; ++k) rows.push_back(Rec("t" + std::to_string(k), "To Do", "2026-01-05").upd(iso(addBiz(base, -k))));
  rows.push_back(Rec("tie1", "To Do", "2026-01-05").upd(iso(addBiz(base, -11))));
  const Proj p = make(rows, "2026-10-19");
  const TrendAging a = p.ok().aging;
  CHECK(a.n == 13);
  CHECK(a.hist[0] == 2);  // 0, 1
  CHECK(a.hist[1] == 3);  // 2, 3, 4
  CHECK(a.hist[2] == 5);  // 5..9
  CHECK(a.hist[3] == 3);  // 10, 11, 11
  CHECK(a.hist[4] == 0);
  REQUIRE(a.top.size() == static_cast<std::size_t>(kTrendTopN));
  CHECK(a.top[0].days == 11);
  CHECK(a.top[0].id == "t11");  // 同じ 11 日は並び順で t11 が先
  CHECK(a.top[1].id == "tie1");
  CHECK(a.top[2].days == 10);
  CHECK(a.top.back().days == 3);
}

TEST_CASE("trends: 滞留日数に休業日は数えない") {
  const Calendar hol = Calendar::fromText("2026-10-14 2026-10-15");
  const Proj p = make({Rec("A", "To Do", "2026-09-01").upd("2026-10-13")}, "2026-10-19", hol);  // 火 → 月: 水木休み、金・月 = 2
  const TrendAging a = p.ok().aging;
  REQUIRE(a.top.size() == 1);
  CHECK(a.top[0].days == 2);
}

TEST_CASE("trends: 担当者と、表示用の件名") {
  const Proj p = make({Rec("A", "To Do", "2026-09-01").by("田中").upd("2026-10-12")}, "2026-10-19");
  const TrendAging a = p.ok().aging;
  REQUIRE(a.top.size() == 1);
  CHECK(a.top[0].assignee == "田中");
}

// ---------------------------------------------------------------- 絞り込み

TEST_CASE("trends: 担当者・ルート課題で絞る") {
  const Proj p = make({Rec("1", "To Do", "2026-10-01").ty("Epic"),
                       Rec("2", "To Do", "2026-10-02").by("田中").under("1").upd("2026-10-12"),
                       Rec("3", "Done", "2026-10-02", "2026-10-09").by("鈴木").under("1"),
                       Rec("4", "To Do", "2026-10-01").ty("Epic"),
                       Rec("5", "To Do", "2026-10-06").by("田中").under("4").upd("2026-10-15"),
                       Rec("6", "To Do", "2026-10-07").upd("2026-10-14")},  // 単独・担当なし
                      "2026-10-19");
  CHECK(p.ok().items == 4);
  {
    TrendFilter f;
    f.who = TrendFilter::Who::Name;
    f.name = "田中";
    const TrendReport r = p.ok(f);
    CHECK(r.items == 2);
    CHECK(r.burn.remainingNow == 2);
    CHECK(r.aging.n == 2);
    CHECK(r.lead.n == 0);
  }
  {
    TrendFilter f;
    f.who = TrendFilter::Who::Name;
    f.name = "  鈴木 ";  // 前後の空白は無視
    const TrendReport r = p.ok(f);
    CHECK(r.items == 1);
    CHECK(r.lead.n == 1);
    CHECK(r.aging.n == 0);
  }
  {
    TrendFilter f;
    f.who = TrendFilter::Who::Unassigned;
    const TrendReport r = p.ok(f);
    CHECK(r.items == 1);
    CHECK(r.aging.top.size() == 1);
    CHECK(r.aging.top[0].id == "6");
  }
  {
    TrendFilter f;
    f.root = p.idx("1");
    const TrendReport r = p.ok(f);
    CHECK(r.items == 2);
    CHECK(r.burn.remainingNow == 1);
    CHECK(r.lead.n == 1);
  }
  {
    TrendFilter f;
    f.root = TrendFilter::kStandalone;
    const TrendReport r = p.ok(f);
    CHECK(r.items == 1);
    CHECK(r.aging.top.size() == 1);
  }
  {
    TrendFilter f;  // 担当者とルートの両方
    f.who = TrendFilter::Who::Name;
    f.name = "田中";
    f.root = p.idx("4");
    CHECK(p.ok(f).items == 1);
  }
  {
    TrendFilter f;  // 該当なしでも落ちない
    f.who = TrendFilter::Who::Name;
    f.name = "いない人";
    const TrendReport r = p.ok(f);
    CHECK(r.items == 0);
    CHECK(r.burn.weeks.empty());
    CHECK(r.burn.forecast.reason == ForecastReason::NoData);
  }
}

TEST_CASE("trends: 入力が食い違っていたり、根の指定が範囲外ならエラー（例外にしない）") {
  const Proj p = make({Rec("1", "To Do", "2026-10-01")}, "2026-10-19");
  TrendFilter f;
  f.root = 99;
  CHECK_FALSE(p.run(f).ok());
  f.root = -7;
  CHECK_FALSE(p.run(f).ok());
  const std::vector<Issue> none;
  CHECK_FALSE(computeTrends(p.comp.T, none, p.meta, p.comp.R, p.cal, p.base).ok());
  const std::vector<ScheduleRow> noRows;
  CHECK_FALSE(computeTrends(p.comp.T, p.issues, p.meta, noRows, p.cal, p.base).ok());
}

TEST_CASE("trends: 極端な日付でも落ちない・遅くならない") {
  const auto t0 = std::chrono::steady_clock::now();
  const Proj p = make({Rec("a", "To Do", "0001-01-01").upd("1900-01-01"), Rec("b", "Done", "2026-10-01", "9999-12-31"),
                       Rec("c", "Done", "1990-01-01", "9998-01-01"), Rec("d", "To Do", "9999-12-31")},
                      "2026-10-19");
  const TrendReport r = p.ok();
  const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  CHECK(r.items == 4);
  CHECK(r.burn.weeks.size() <= static_cast<std::size_t>(kTrendPastWeeks + 1));
  CHECK(ms < 3000);
}

// ---------------------------------------------------------------- 5,000 件

TEST_CASE("trends: 5,000 件を短い時間で集計する") {
  auto g = generateProject({5000, 7});
  g.meta.extraHeaders = {"Created", "Updated"};
  const Day base = D("2026-10-19");
  for (std::size_t i = 0; i < g.issues.size(); ++i) {
    Issue& s = g.issues[i];
    const Day created = base - 400 + static_cast<Day>((i * 37) % 380);
    const Day updated = created + static_cast<Day>((i * 11) % 300);
    s.extra = {iso(created) + " 10:00", iso(updated)};
    if (statusCat(s.status) == Cat::Done) s.resolved = created + static_cast<Day>(i % 40);
  }
  Proj p;
  p.issues = std::move(g.issues);
  p.meta = std::move(g.meta);
  p.base = base;
  ScheduleOptions opt;
  opt.base = base;
  auto c = computeAll(p.issues, p.meta, p.cal, opt);
  REQUIRE(c.ok());
  p.comp = std::move(c).value();
  REQUIRE(p.comp.T.n == 5000);

  const auto t0 = std::chrono::steady_clock::now();
  const auto res = p.run();
  const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  MESSAGE("trends 5000 件: " << ms << " ms");
  REQUIRE(res.ok());
  CHECK(res->items > 0);
  CHECK(res->burn.weeks.size() == static_cast<std::size_t>(kTrendPastWeeks + 1));
  CHECK(res->aging.top.size() == static_cast<std::size_t>(kTrendTopN));
  CHECK(ms < 1000);  // 目標は Release で 100 ms 以内（Debug・サニタイザ付きでも 1 秒は超えない）
}

// ---------------------------------------------------------------- 文言

TEST_CASE("trends: ペース予測の文と、数えなかったものの注記") {
  {
    const TrendReport r = paceProject(8, {{"2026-10-12", 2}, {"2026-10-05", 2}}).ok();
    const std::string t = trendForecastText(r.burn.forecast, r.burn.remainingNow);
    CHECK(t.find("週 1 件") != std::string::npos);
    CHECK(t.find("残り 8 件") != std::string::npos);
    CHECK(t.find("12/14(月) の週（8 週後）") != std::string::npos);
    CHECK(t.rfind("!", 0) == std::string::npos);  // 予測が出るときは「!」なし
  }
  {
    const TrendReport r = paceProject(5, {{"2026-10-12", 4}}).ok();
    const std::string t = trendForecastText(r.burn.forecast, r.burn.remainingNow);
    CHECK(t.rfind("! ", 0) == 0);  // 出せない理由には「!」を付ける（色だけに頼らない）
    CHECK(t.find("1 週") != std::string::npos);
  }
  {
    const TrendReport r = paceProject(5, {}).ok();
    CHECK(trendForecastText(r.burn.forecast, 5).rfind("! ", 0) == 0);
    TrendForecast f;
    f.reason = ForecastReason::Finished;
    CHECK(trendForecastText(f, 0).find("残りがない") != std::string::npos);
    f.reason = ForecastReason::NoData;
    CHECK_FALSE(trendForecastText(f, 0).empty());
    f.reason = ForecastReason::TooFar;
    f.perWeek = 0.5;
    CHECK(trendForecastText(f, 300).find("週 0.5 件") != std::string::npos);
  }
  {
    const Proj p = make({Rec("1", "To Do", "2026-10-06"), Rec("2"), Rec("3", "Done", "2026-10-06", ""), Rec("4", "Done", "2026-10-13", "2026-10-09"),
                         Rec("5", "To Do", "2026-11-01")},
                        "2026-10-19");
    const std::vector<std::string> notes = trendNotes(p.ok());
    std::string all;
    for (const auto& n : notes) all += n + "\n";
    CHECK(all.find("作成日（Created）のない 1 件は数えていません") != std::string::npos);
    CHECK(all.find("完了日（Resolved）のない完了済みの 1 件") != std::string::npos);
    CHECK(all.find("1 件は完了日が作成日より前") != std::string::npos);
    CHECK(all.find("1 件は作成日が基準日より先") != std::string::npos);
    CHECK(all.find("滞留に数えていません") != std::string::npos);
  }
  {
    const Proj p = make({Rec("1", "To Do", "2026-10-06")}, "2026-10-19");
    CHECK(trendNotes(p.ok()).empty());
    const Proj q = make({Rec("1")}, "2026-10-19", Calendar(), "", "");
    const std::vector<std::string> n = trendNotes(q.ok());
    REQUIRE_FALSE(n.empty());
    CHECK(n[0].find("Created（作成日）の列がない") != std::string::npos);
  }
}
