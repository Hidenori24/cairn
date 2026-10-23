// マイルストーンの予測の推移（Issue #75）: スナップショットの "ms"・推移の列と点・前の記録からの動き・遅れの警告・文言
#include <doctest/doctest.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "roadmap/clock.hpp"
#include "roadmap/commands.hpp"
#include "roadmap/csv.hpp"
#include "roadmap/load.hpp"
#include "roadmap/mta.hpp"
#include "roadmap/report.hpp"
#include "roadmap/text.hpp"
#include "roadmap/project_io.hpp"
#include "roadmap/schedule.hpp"
#include "roadmap/stats.hpp"
#include "roadmap/weekly.hpp"

using namespace roadmap;
using Json = nlohmann::ordered_json;

namespace {

Day D(const char* iso) { return *parseIso(iso); }

// 架空のプロジェクト。マイルストーン 4（中間・期限あり）・5（リリース・期限あり）・6（完了済み）。基準日は 2026-10-19（月）
const char* const kCsv =
    "Issue Id,Issue Type,Summary,Parent,Assignee,Status,Priority,Resolved,Duration (weeks),Outward issue link (Blocks),Deadline\n"
    "1,Epic,全体,,,,High,,,,\n"
    "2,Task,設計,1,田中,In Progress,High,,1,3,\n"
    "3,Task,実装,1,田中,To Do,Medium,,2,4,\n"
    "4,Milestone,中間,1,,,High,,0,,2026-11-13\n"
    "5,Milestone,リリース,1,,,High,,0,,2026-12-04\n"
    "6,Milestone,完了済み,1,,Done,High,2026-10-14,0,,\n"
    "7,Task,検証,1,鈴木,To Do,Medium,,4,5,\n";

struct Proj {
  std::vector<Issue> issues;
  ProjectMeta meta;
  Computed comp;
  Calendar cal = Calendar::fromText(defaultHolidaysText());
  explicit Proj(const std::string& csvText = kCsv, const char* base = "2026-10-19") {
    auto r = csv::importCsv(csvText);
    REQUIRE(r.ok());
    issues = std::move(r.value().issues);
    meta = std::move(r.value().meta);
    ScheduleOptions opt;
    opt.base = D(base);
    auto c = computeAll(issues, meta, cal, opt);
    REQUIRE(c.ok());
    comp = std::move(c).value();
    comp.issues = issues;
  }
  int idx(const char* id) const { return comp.T.find(id); }
  Day finishOf(const char* id) const {
    REQUIRE(idx(id) >= 0);
    return comp.R[static_cast<std::size_t>(idx(id))].asapFinish;
  }
  // 営業日で k 日ずらす（負 = 前）
  Day wd(Day d, int k) const {
    const int step = k < 0 ? -1 : 1;
    for (int n = k < 0 ? -k : k; n > 0;) {
      d += step;
      if (cal.isWork(d)) --n;
    }
    return d;
  }
  Result<MtaReport> mta(const WeeklyAuto& a, const char* base = "2026-10-19", const BaselineCompare* bl = nullptr,
                        int lateDays = kMtaDefaultLateDays) const {
    MtaOptions o;
    o.lateDays = lateDays;
    return computeMta(comp.T, issues, comp.R, cal, a, D(base), bl, o);
  }
};

WeeklySnapItem msItem(const char* id, char st, Day finish, bool late = false) {
  WeeklySnapItem it;
  it.id = id;
  it.st = st;
  it.finish = finish;
  it.late = late;
  return it;
}

WeeklySnapshot snapOf(const char* iso, std::vector<WeeklySnapItem> ms) {
  WeeklySnapshot s;
  s.base = D(iso);
  s.milestones = std::move(ms);
  return s;
}

WeeklyAuto autoOf(std::vector<WeeklySnapshot> snaps) {
  WeeklyAuto a;
  for (WeeklySnapshot& s : snaps) weeklyAutoPut(a, std::move(s));
  return a;
}

const MtaSeries* seriesOf(const MtaReport& r, const char* id) {
  for (const MtaSeries& s : r.series)
    if (s.id == id) return &s;
  return nullptr;
}

}  // namespace

// ---------------------------------------------------------------- スナップショットの "ms"
TEST_CASE("mta: スナップショットにマイルストーンの状況と見込みの完了日が入る（作業の items には入れない）") {
  Proj p;
  const ProjectStats st = computeStats(p.comp.T, p.issues, p.comp.R, D("2026-10-19"));
  const WeeklySnapshot s = makeWeeklySnapshot(p.comp.T, p.issues, p.comp.R, st, D("2026-10-19"));
  CHECK(s.items.size() == 3);  // 作業 2・3・7 だけ（Epic・マイルストーンは入らない）
  for (const WeeklySnapItem& it : s.items) CHECK((it.id != "4" && it.id != "5" && it.id != "6" && it.id != "1"));
  REQUIRE(s.milestones.size() == 3);
  CHECK(s.milestones[0].id == "4");
  CHECK(s.milestones[0].st == 't');
  REQUIRE(s.milestones[0].finish);
  CHECK(*s.milestones[0].finish == p.finishOf("4"));
  CHECK(s.milestones[1].id == "5");
  CHECK(s.milestones[2].id == "6");
  CHECK(s.milestones[2].st == 'd');
  REQUIRE(s.milestones[2].finish);
  CHECK(fmtIso(*s.milestones[2].finish) == "2026-10-14");  // 完了済みは完了日
  CHECK_FALSE(s.milestones[2].late);
}

TEST_CASE("mta: \"ms\" の JSON の往復・空なら書かない・壊れた項目は読み飛ばす・未知のキーは壊さない") {
  WeeklySnapshot s = snapOf("2026-10-19", {msItem("4", 'p', D("2026-11-13"), true), msItem("6", 'd', D("2026-10-14"))});
  WeeklyAuto a;
  weeklyAutoPut(a, s);
  const Json j = weeklyAutoToJson(a);
  REQUIRE(j["snaps"][0].contains("ms"));
  CHECK(j["snaps"][0]["ms"].size() == 2);
  CHECK(weeklyAutoFromJson(j) == a);
  // マイルストーンがなければ "ms" は書かない（今までのバイト列と変わらない）
  WeeklyAuto none;
  weeklyAutoPut(none, snapOf("2026-10-19", {}));
  CHECK_FALSE(weeklyAutoToJson(none)["snaps"][0].contains("ms"));
  // 0.9.x が書いたスナップショット（"ms" なし）を読むと、マイルストーンは空
  CHECK(weeklyAutoFromJson(weeklyAutoToJson(none)).snaps[0].milestones.empty());
  // 壊れた項目だけを読み飛ばす（型・状況・日付・重複・Issue Id）
  Json bad = j;
  bad["snaps"][0]["ms"] = Json::array({"4\tp\t2026-11-13\t1", 5, "5\tx\t2026-11-13\t0", "5\tp\t2026-13-40\t0", "4\tt\t2026-11-20\t0", "\tt\t\t0",
                                        "7\tt\t\t0"});
  const WeeklyAuto back = weeklyAutoFromJson(bad);
  REQUIRE(back.snaps.size() == 1);
  REQUIRE(back.snaps[0].milestones.size() == 2);
  CHECK(back.snaps[0].milestones[0].id == "4");
  CHECK(back.snaps[0].milestones[0].late);
  CHECK(back.snaps[0].milestones[1].id == "7");
  CHECK_FALSE(back.snaps[0].milestones[1].finish);
  // "ms" が配列でなければ無視（スナップショット自体は読める）
  Json notArr = j;
  notArr["snaps"][0]["ms"] = "x";
  CHECK(weeklyAutoFromJson(notArr).snaps.size() == 1);
  // 未知のキーはそのまま書き戻す
  Json fut = j;
  fut["snaps"][0]["future"] = Json{{"a", 1}};
  const Json again = weeklyAutoToJson(weeklyAutoFromJson(fut));
  CHECK(again["snaps"][0]["future"]["a"] == 1);
  CHECK(again["snaps"][0]["ms"].size() == 2);
}

TEST_CASE("mta: スナップショットのマイルストーンは 500 件まで") {
  WeeklySnapshot s;
  s.base = D("2026-10-19");
  for (std::size_t i = 0; i < kWeeklyMaxSnapshotMilestones + 20; ++i)
    s.milestones.push_back(msItem(("M" + std::to_string(i)).c_str(), 't', D("2026-11-13")));
  WeeklyAuto a;
  weeklyAutoPut(a, std::move(s));
  CHECK(a.snaps[0].milestones.size() == kWeeklyMaxSnapshotMilestones);
}

TEST_CASE("mta: Session が開いたとき取る週の頭のスナップショットにマイルストーンが入る") {
  FixedClock clock(LocalTime{2026, 10, 21, 9, 30, 0});
  ProjectData d;
  auto r = csv::importCsv(kCsv);
  REQUIRE(r.ok());
  d.issues = std::move(r.value().issues);
  d.meta = std::move(r.value().meta);
  d.settings.baseDate = "2026-10-19";
  d.settings.holidays = defaultHolidaysText();
  Session s(d, &clock);
  REQUIRE(s.error().empty());
  const auto rec = s.recordWeeklyAuto(WeeklyAutoMode::IfMissing);
  REQUIRE(rec.ok());
  REQUIRE(s.weeklyAuto().snaps.size() == 1);
  CHECK(s.weeklyAuto().snaps[0].milestones.size() == 3);
  // 設定に書き出して読み直しても残る
  ProjectRecord prec;
  prec.settings.id = "p1";
  prec.settings.name = "P";
  prec.settings.weeklyAuto = s.weeklyAuto();
  const ProjectRecord back = recordFromSettingsJson(settingsToJson(prec), "", "stem", clock);
  CHECK(back.settings.weeklyAuto == s.weeklyAuto());
}

// ---------------------------------------------------------------- 推移
TEST_CASE("mta: しきい値は 1〜260 に収める") {
  CHECK(mtaClampLateDays(5) == 5);
  CHECK(mtaClampLateDays(0) == kMtaMinLateDays);
  CHECK(mtaClampLateDays(-3) == kMtaMinLateDays);
  CHECK(mtaClampLateDays(1) == 1);
  CHECK(mtaClampLateDays(260) == 260);
  CHECK(mtaClampLateDays(261) == kMtaMaxLateDays);
  CHECK(mtaClampLateDays(1000000) == kMtaMaxLateDays);
}

TEST_CASE("mta: 記録がないとき・1 週だけのときは線が引けない（嘘の線を引かない）") {
  Proj p;
  {
    const auto r = p.mta(WeeklyAuto{});
    REQUIRE(r.ok());
    CHECK(r->columns.empty());
    CHECK(r->recordedWeeks == 0);
    CHECK_FALSE(r->drawable());
    CHECK(r->warnings == 0);
    REQUIRE(r->series.size() == 3);  // いまのマイルストーン 3 つ（記録のない系列として）
    for (const MtaSeries& s : r->series) {
      CHECK(s.recorded == 0);
      CHECK(s.points.empty());
      CHECK_FALSE(s.warn);
    }
    CHECK(mtaPendingText(r->recordedWeeks) == "記録が溜まるとここに出ます（いま 0 週ぶん）");
  }
  {
    const auto r = p.mta(autoOf({snapOf("2026-10-19", {msItem("4", 't', p.finishOf("4"))})}));
    REQUIRE(r.ok());
    CHECK(r->recordedWeeks == 1);
    CHECK_FALSE(r->drawable());
    CHECK(mtaPendingText(r->recordedWeeks) == "記録が溜まるとここに出ます（いま 1 週ぶん）");
    REQUIRE(r->columns.size() == 2);  // 記録の週と「いま」
    CHECK(r->columns[0].recorded);
    CHECK(r->columns[1].now);
  }
  {
    // マイルストーンを含まない（0.9.x が取った）スナップショットは、週として数えない
    const auto r = p.mta(autoOf({snapOf("2026-10-12", {}), snapOf("2026-10-19", {})}));
    REQUIRE(r.ok());
    CHECK(r->recordedWeeks == 0);
    CHECK(r->columns.empty());
  }
}

TEST_CASE("mta: 週ごとの予測日と前の記録からの動き。記録のない週は gap（線を切る）") {
  Proj p;
  const Day x = p.finishOf("4");
  // 10/05・10/19 の週に記録（10/12 の週は開かなかった）。基準日は 10/26（月）で、その週の頭の記録もない
  const WeeklyAuto a = autoOf({snapOf("2026-10-05", {msItem("4", 'p', p.wd(x, -2))}), snapOf("2026-10-19", {msItem("4", 'p', p.wd(x, 1))})});
  const auto r = p.mta(a, "2026-10-26");
  REQUIRE(r.ok());
  REQUIRE(r->columns.size() == 5);
  CHECK(fmtIso(r->columns[0].week) == "2026-10-05");
  CHECK(r->columns[0].recorded);
  CHECK(fmtIso(r->columns[1].week) == "2026-10-12");
  CHECK_FALSE(r->columns[1].recorded);  // 記録なし
  CHECK(r->columns[2].recorded);
  CHECK(fmtIso(r->columns[3].week) == "2026-10-26");
  CHECK_FALSE(r->columns[3].recorded);  // 基準日の週の頭の記録もない
  CHECK(r->columns[4].now);
  CHECK(r->columns[4].recorded);
  CHECK(r->columns[4].week == D("2026-10-26"));
  CHECK(r->recordedWeeks == 2);
  CHECK(r->drawable());
  const MtaSeries* s = seriesOf(*r, "4");
  REQUIRE(s);
  REQUIRE(s->points.size() == 5);
  CHECK(s->points[0].has);
  CHECK(s->points[0].date == p.wd(x, -2));
  CHECK_FALSE(s->points[0].hasMove);  // 最初の点に動きはない
  CHECK_FALSE(s->points[1].has);      // gap
  REQUIRE(s->points[2].has);
  REQUIRE(s->points[2].hasMove);
  CHECK(s->points[2].move == 3);      // 営業日で +3（遅れ）
  CHECK(s->points[2].sinceWeeks == 2);
  CHECK_FALSE(s->points[2].late);     // 3 < 5
  CHECK_FALSE(s->points[3].has);
  REQUIRE(s->points[4].has);          // いま
  CHECK(s->points[4].date == x);
  CHECK(s->points[4].move == -1);     // 10/19 の記録（x+1）から x へ 1 営業日の前倒し
  CHECK(s->points[4].sinceWeeks == 1);
  CHECK(s->recorded == 2);
  REQUIRE(s->deadline);
  CHECK(fmtIso(*s->deadline) == "2026-11-13");
  CHECK_FALSE(s->plan);
  CHECK_FALSE(s->removed);
  CHECK(s->did == p.comp.T.did[static_cast<std::size_t>(p.idx("4"))]);
  CHECK(s->name == "中間");
}

TEST_CASE("mta: 遅れの警告はしきい値ちょうどで出る（4 営業日は出ない・5 営業日は出る）。しきい値は設定できる") {
  Proj p;
  const Day x = p.finishOf("4");
  const auto warnOf = [&](int daysBefore, int lateDays) {
    const WeeklyAuto a = autoOf({snapOf("2026-10-12", {msItem("4", 'p', p.wd(x, -daysBefore))}),
                                 snapOf("2026-10-19", {msItem("4", 'p', p.wd(x, -daysBefore))})});
    const auto r = p.mta(a, "2026-10-19", nullptr, lateDays);
    REQUIRE(r.ok());
    const MtaSeries* s = seriesOf(*r, "4");
    REQUIRE(s);
    return std::make_pair(s->warn, s->warnMove);
  };
  CHECK(warnOf(4, 5) == std::make_pair(false, 0));
  CHECK(warnOf(5, 5) == std::make_pair(true, 5));
  CHECK(warnOf(6, 5) == std::make_pair(true, 6));
  CHECK(warnOf(1, 1) == std::make_pair(true, 1));  // しきい値 1
  CHECK(warnOf(1, 2) == std::make_pair(false, 0));
  CHECK(warnOf(12, 10) == std::make_pair(true, 12));
  CHECK(warnOf(12, 13) == std::make_pair(false, 0));
  CHECK(warnOf(5, 0) == std::make_pair(true, 5));  // 範囲外のしきい値は収める（0 → 1）
  // 基準は「前の週以前の最新の記録」と「いま」。基準日の週の頭の記録とは比べない
  const WeeklyAuto a = autoOf({snapOf("2026-10-12", {msItem("4", 'p', p.wd(x, -6))}), snapOf("2026-10-19", {msItem("4", 'p', p.wd(x, -1))})});
  const auto r = p.mta(a, "2026-10-19");
  REQUIRE(r.ok());
  const MtaSeries* s = seriesOf(*r, "4");
  REQUIRE(s);
  CHECK(s->warn);
  CHECK(s->warnMove == 6);
  CHECK(s->warnFromWeek == D("2026-10-12"));
  CHECK(s->warnFrom == p.wd(x, -6));
  CHECK(s->warnTo == x);
  CHECK(s->warnWeeks == 1);
  CHECK(r->warnings == 1);
  // 週の頭の記録と比べると +1 だが、点の動きとしては +5（10/12 → 10/19）で、しきい値以上の遅れの印が付く
  REQUIRE(s->points.size() == 3);
  CHECK(s->points[1].move == 5);
  CHECK(s->points[1].late);
  CHECK(s->points[2].move == 1);
  CHECK_FALSE(s->points[2].late);
  CHECK(s->points[2].sinceWeeks == 0);  // 「いま」は同じ週の頭からの動き
}

TEST_CASE("mta: 前倒しは警告しない。前の週の記録がなければ、それより前の最新の記録と比べる（何週前かを言う）") {
  Proj p;
  const Day x = p.finishOf("4");
  {
    const WeeklyAuto a = autoOf({snapOf("2026-10-05", {msItem("4", 'p', p.wd(x, 8))}), snapOf("2026-10-12", {msItem("4", 'p', p.wd(x, 7))})});
    const auto r = p.mta(a, "2026-10-19");
    REQUIRE(r.ok());
    const MtaSeries* s = seriesOf(*r, "4");
    REQUIRE(s);
    CHECK_FALSE(s->warn);  // 7 営業日の前倒し
    CHECK(r->warnings == 0);
    REQUIRE(s->points.back().hasMove);
    CHECK(s->points.back().move == -7);
  }
  {
    // 基準日の週は 10/26。10/19 の週の記録（1 週前）と比べる
    const WeeklyAuto a = autoOf({snapOf("2026-10-05", {msItem("4", 'p', p.wd(x, -9))}), snapOf("2026-10-19", {msItem("4", 'p', p.wd(x, -9))})});
    const auto r = p.mta(a, "2026-10-26");
    REQUIRE(r.ok());
    const MtaSeries* s = seriesOf(*r, "4");
    REQUIRE(s);
    CHECK(s->warn);
    CHECK(s->warnFromWeek == D("2026-10-19"));
    CHECK(s->warnWeeks == 1);
  }
  {
    // 10/12・10/19 の週に（マイルストーンの）記録がない: 10/05 の記録から（基準日の週 10/26 から 3 週前）
    const WeeklyAuto a = autoOf({snapOf("2026-10-05", {msItem("4", 'p', p.wd(x, -9))}), snapOf("2026-10-12", {})});
    const auto r = p.mta(a, "2026-10-26");
    REQUIRE(r.ok());
    const MtaSeries* s = seriesOf(*r, "4");
    REQUIRE(s);
    CHECK(s->warn);
    CHECK(s->warnWeeks == 3);
    CHECK(s->warnFromWeek == D("2026-10-05"));
  }
}

TEST_CASE("mta: 達成済みは達成した点で線を止める。いま達成済みなら警告しない") {
  Proj p;
  // 6 は完了済み（完了日 10/14）。10/05 の週は未達、10/12 の週は達成、10/19 の週は達成のまま
  const WeeklyAuto a = autoOf({snapOf("2026-10-05", {msItem("6", 'p', D("2026-10-16"))}), snapOf("2026-10-12", {msItem("6", 'd', D("2026-10-14"))}),
                               snapOf("2026-10-19", {msItem("6", 'd', D("2026-10-14"))})});
  const auto r = p.mta(a, "2026-10-19");
  REQUIRE(r.ok());
  const MtaSeries* s = seriesOf(*r, "6");
  REQUIRE(s);
  REQUIRE(s->points.size() == 4);
  CHECK(s->points[0].has);
  CHECK_FALSE(s->points[0].achieved);
  CHECK(s->points[1].has);
  CHECK(s->points[1].achieved);
  CHECK(s->points[1].move == -2);  // 10/16 → 10/14
  CHECK(s->points[1].date == D("2026-10-14"));
  CHECK_FALSE(s->points[2].has);  // 達成のあとは線を止める
  CHECK(s->points[2].ended);
  CHECK_FALSE(s->points[3].has);  // いまも
  CHECK(s->points[3].ended);
  CHECK(s->achievedNow);
  CHECK_FALSE(s->warn);
  CHECK(mtaDateText(s->points[1]) == "10/14（達成）");
  CHECK(mtaDateText(s->points[2]) == "達成済み");
}

TEST_CASE("mta: 削除された課題・あとから足された課題は Issue Id で追う") {
  Proj p;
  const Day x = p.finishOf("4");
  // 99 は今の課題にない（削除された）。5 は最初の記録のあとに足された（10/05 の記録にない）
  const WeeklyAuto a = autoOf({snapOf("2026-10-05", {msItem("4", 'p', p.wd(x, 1)), msItem("99", 't', D("2026-11-27"))}),
                               snapOf("2026-10-12", {msItem("4", 'p', p.wd(x, 1)), msItem("99", 't', D("2026-11-27")), msItem("5", 't', D("2026-12-04"))}),
                               snapOf("2026-10-19", {msItem("4", 'p', x), msItem("5", 't', D("2026-12-11"))})});
  const auto r = p.mta(a, "2026-10-19");
  REQUIRE(r.ok());
  REQUIRE(r->series.size() == 4);  // いまの 3 つ → 削除された 1 つ
  const MtaSeries* gone = seriesOf(*r, "99");
  REQUIRE(gone);
  CHECK(r->series.back().id == "99");
  CHECK(gone->removed);
  CHECK(gone->index == -1);
  CHECK(gone->recorded == 2);
  CHECK(gone->name.empty());
  CHECK_FALSE(gone->points.back().has);  // いまの列に点はない
  CHECK_FALSE(gone->warn);
  CHECK_FALSE(gone->deadline);
  CHECK(mtaSeriesName(*gone) == "(削除された) 99");
  const MtaSeries* added = seriesOf(*r, "5");
  REQUIRE(added);
  CHECK_FALSE(added->points[0].has);  // 最初の記録にはなかった（点を作らない）
  CHECK(added->points[1].has);
  CHECK_FALSE(added->points[1].hasMove);
  CHECK(added->points[2].has);
  CHECK(added->points[2].move == 5);  // 12/04 → 12/11
  CHECK(added->points[2].late);
  CHECK(added->recorded == 2);  // 「いま」の点は数えない
  // 記録のない（いまの点だけの）マイルストーン 6 は、点が「いま」だけ
  const MtaSeries* fresh = seriesOf(*r, "6");
  REQUIRE(fresh);
  CHECK(fresh->recorded == 0);
  CHECK(fresh->points.back().has);
  CHECK_FALSE(fresh->points.back().hasMove);
}

TEST_CASE("mta: 基準日より先の週の記録は警告の比べる元にしない。横軸は kMtaMaxWeeks 週までで新しいほうを残す") {
  Proj p;
  const Day x = p.finishOf("4");
  {
    const WeeklyAuto a = autoOf({snapOf("2026-10-12", {msItem("4", 'p', p.wd(x, -9))}), snapOf("2026-11-02", {msItem("4", 'p', p.wd(x, -9))})});
    const auto r = p.mta(a, "2026-10-19");  // 時計が進んだあとで基準日を戻した場合など
    REQUIRE(r.ok());
    const MtaSeries* s = seriesOf(*r, "4");
    REQUIRE(s);
    CHECK(s->warn);
    CHECK(s->warnFromWeek == D("2026-10-12"));  // 11/02 の週ではない
  }
  {
    WeeklyAuto a;  // weeklyAutoPut を通さずに、古い週を混ぜる（壊れた・古い設定）
    a.snaps.push_back(snapOf("2024-01-08", {msItem("4", 'p', D("2024-02-01"))}));
    a.snaps.push_back(snapOf("2026-10-05", {msItem("4", 'p', p.wd(x, 1))}));
    a.snaps.push_back(snapOf("2026-10-12", {msItem("4", 'p', p.wd(x, 1))}));
    const auto r = p.mta(a, "2026-10-19");
    REQUIRE(r.ok());
    CHECK(r->truncatedWeeks);
    CHECK(static_cast<int>(r->columns.size()) == kMtaMaxWeeks + 1);  // 52 週 + いま
    CHECK(r->columns.front().week == D("2026-10-19") - 51 * 7);
    CHECK(r->recordedWeeks == 2);  // 落とした週の記録は数えない
    const MtaSeries* s = seriesOf(*r, "4");
    REQUIRE(s);
    CHECK(s->points.size() == r->columns.size());
    CHECK(s->recorded == 2);
  }
}

TEST_CASE("mta: 表示中のベースラインの計画の完了日と、期限を系列に持つ") {
  Proj p;
  BaselineCompare bc;
  bc.entries.resize(static_cast<std::size_t>(p.comp.T.n));
  bc.entries[static_cast<std::size_t>(p.idx("4"))].planFinish = D("2026-11-06");
  const auto r = p.mta(autoOf({}), "2026-10-19", &bc);
  REQUIRE(r.ok());
  const MtaSeries* s = seriesOf(*r, "4");
  REQUIRE(s);
  REQUIRE(s->plan);
  CHECK(fmtIso(*s->plan) == "2026-11-06");
  REQUIRE(s->deadline);
  const MtaSeries* s5 = seriesOf(*r, "5");
  REQUIRE(s5);
  CHECK_FALSE(s5->plan);
  // 課題の数が合わないベースラインの比較は使わない
  BaselineCompare wrong;
  wrong.entries.resize(2);
  const auto r2 = p.mta(autoOf({}), "2026-10-19", &wrong);
  REQUIRE(r2.ok());
  CHECK_FALSE(seriesOf(*r2, "4")->plan);
}

TEST_CASE("mta: 木と課題・日程の数が合わないときはエラー。課題が 0 件でも落ちない") {
  Proj p;
  std::vector<ScheduleRow> shortRows(p.comp.R.begin(), p.comp.R.end() - 1);
  CHECK_FALSE(computeMta(p.comp.T, p.issues, shortRows, p.cal, WeeklyAuto{}, D("2026-10-19"), nullptr).ok());
  std::vector<Issue> fewer(p.issues.begin(), p.issues.end() - 1);
  CHECK_FALSE(computeMta(p.comp.T, fewer, p.comp.R, p.cal, WeeklyAuto{}, D("2026-10-19"), nullptr).ok());
  const Tree empty;
  const auto r = computeMta(empty, {}, {}, p.cal,
                            autoOf({snapOf("2026-10-12", {msItem("9", 't', D("2026-11-06"))}), snapOf("2026-10-19", {msItem("9", 't', D("2026-11-13"))})}),
                            D("2026-10-19"), nullptr);
  REQUIRE(r.ok());
  REQUIRE(r->series.size() == 1);
  CHECK(r->series[0].removed);
  CHECK(r->drawable());
}

// ---------------------------------------------------------------- 文言
TEST_CASE("mta: 動き・警告・列の文言") {
  MtaPoint first;
  first.has = true;
  first.date = D("2026-11-13");
  CHECK(mtaDateText(first) == "11/13");
  CHECK(mtaMoveText(first) == "-");
  MtaPoint none;
  CHECK(mtaDateText(none) == "-");
  CHECK(mtaMoveText(none) == "-");
  MtaPoint same = first;
  same.hasMove = true;
  same.sinceWeeks = 1;
  CHECK(mtaMoveText(same) == "変わらず");
  MtaPoint later = same;
  later.move = 3;
  CHECK(mtaMoveText(later) == "+3 営業日（遅れ）");
  later.late = true;
  later.move = 6;
  CHECK(mtaMoveText(later) == "! +6 営業日（遅れ）");
  MtaPoint earlier = same;
  earlier.move = -2;
  CHECK(mtaMoveText(earlier) == "-2 営業日（前倒し）");
  earlier.sinceWeeks = 3;
  CHECK(mtaMoveText(earlier) == "-2 営業日（前倒し）・3 週前の記録から");
  earlier.sinceWeeks = 0;
  CHECK(mtaMoveText(earlier) == "-2 営業日（前倒し）・週の頭から");

  MtaColumn week;
  week.week = D("2026-10-19");
  week.recorded = true;
  CHECK(mtaColumnText(week) == "10/19 の週");
  MtaColumn now;
  now.week = D("2026-10-21");
  now.now = true;
  now.recorded = true;
  CHECK(mtaColumnText(now) == "いま（10/21）");

  MtaSeries s;
  s.id = "4";
  s.did = "DEV-4";
  s.name = "中間";
  s.index = 3;
  s.warn = true;
  s.warnMove = 6;
  s.warnFromWeek = D("2026-10-12");
  s.warnFrom = D("2026-11-13");
  s.warnTo = D("2026-11-23");
  s.warnWeeks = 1;
  CHECK(mtaSeriesName(s) == "DEV-4 中間");
  CHECK(mtaWarnText(s) == "! DEV-4 中間: 完了の見込みが 10/12 の週の記録から 6 営業日遅れました（11/13 → 11/23）");
  s.warnWeeks = 2;
  CHECK(mtaWarnText(s) == "! DEV-4 中間: 完了の見込みが 10/12 の週の記録（2 週前）から 6 営業日遅れました（11/13 → 11/23）");
  CHECK(mtaRefLineText(s).empty());
  s.deadline = D("2026-11-20");
  CHECK(mtaRefLineText(s) == "期限 11/20");
  s.plan = D("2026-11-13");
  CHECK(mtaRefLineText(s) == "期限 11/20・計画 11/13");
  s.plan = D("2026-11-20");
  CHECK(mtaRefLineText(s) == "期限・計画 11/20");
  s.deadline.reset();
  CHECK(mtaRefLineText(s) == "計画 11/20");
}

// ---------------------------------------------------------------- 図の共通部分
TEST_CASE("mta: 系列の線種・印は 6 種類を順に使い、隣どうしは違う（色だけに頼らない）") {
  for (std::size_t k = 0; k < 12; ++k) {
    const MtaStyle a = mtaStyleOf(k), b = mtaStyleOf(k + 1);
    CHECK(a.color >= 0);
    CHECK(a.color < 8);
    CHECK((a.marker != b.marker));
    CHECK((a.dash != b.dash || a.gap != b.gap));
    CHECK(std::string(a.name).size() > 0);
    // 6 つめまでは、印も線種もすべて違う
    if (k < 6)
      for (std::size_t j = k + 1; j < 6; ++j) {
        CHECK((a.marker != mtaStyleOf(j).marker));
        CHECK((a.dash != mtaStyleOf(j).dash || a.gap != mtaStyleOf(j).gap));
      }
  }
  CHECK(mtaStyleOf(0).dash == 0);                       // 最初は実線
  CHECK(mtaStyleOf(6).marker == mtaStyleOf(0).marker);  // 7 つめからは繰り返す
}

TEST_CASE("mta: 縦軸は点・期限・計画を含み、目盛りは昇順で範囲に収まる") {
  Proj p;
  const Day x = p.finishOf("4");
  const WeeklyAuto a = autoOf({snapOf("2026-10-05", {msItem("4", 'p', p.wd(x, 10))}), snapOf("2026-10-12", {msItem("4", 'p', p.wd(x, 5))})});
  const auto r = p.mta(a);
  REQUIRE(r.ok());
  const std::size_t i4 = static_cast<std::size_t>(seriesOf(*r, "4") - r->series.data());
  const MtaYAxis ax = mtaYAxis(*r, {i4});
  REQUIRE(ax.ok);
  const Day dl = D("2026-11-13");
  const Day lo = std::min({x, p.wd(x, 10), dl}), hi = std::max({x, p.wd(x, 10), dl});
  CHECK(ax.lo < lo);
  CHECK(ax.hi > hi);
  REQUIRE(ax.ticks.size() >= 3);
  CHECK(ax.ticks.size() <= 6);
  for (std::size_t k = 0; k < ax.ticks.size(); ++k) {
    CHECK(ax.ticks[k] >= ax.lo);
    CHECK(ax.ticks[k] <= ax.hi);
    if (k > 0) CHECK(ax.ticks[k] > ax.ticks[k - 1]);
  }
  // 何も選ばない・点がないときは ok = false
  CHECK_FALSE(mtaYAxis(*r, {}).ok);
  const auto none = p.mta(WeeklyAuto{});
  REQUIRE(none.ok());
  CHECK_FALSE(mtaYAxis(*none, {0}).ok);
  // 範囲外の添字は無視する
  CHECK_FALSE(mtaYAxis(*r, {999}).ok);
  // 同じ日だけ（幅 0）でも幅を持たせる
  const WeeklyAuto flat = autoOf({snapOf("2026-10-12", {msItem("6", 'p', D("2026-11-13"))}), snapOf("2026-10-19", {msItem("6", 'p', D("2026-11-13"))})});
  const auto rf = p.mta(flat);
  REQUIRE(rf.ok());
  const std::size_t i6 = static_cast<std::size_t>(seriesOf(*rf, "6") - rf->series.data());
  const MtaYAxis af = mtaYAxis(*rf, {i6});
  REQUIRE(af.ok);
  CHECK(af.hi - af.lo >= 7);
}

TEST_CASE("mta: 初めに図に出す系列は、警告のあるもの → 記録のあるもの → それ以外の順で最大 6 本") {
  Proj p;
  const Day x = p.finishOf("4");
  // 5 に記録があり警告なし、4 は警告あり、6（完了済み）は記録なし
  const WeeklyAuto a = autoOf({snapOf("2026-10-12", {msItem("4", 'p', p.wd(x, -8)), msItem("5", 't', p.finishOf("5"))}),
                               snapOf("2026-10-19", {msItem("4", 'p', p.wd(x, -8)), msItem("5", 't', p.finishOf("5"))})});
  const auto r = p.mta(a);
  REQUIRE(r.ok());
  const std::vector<std::size_t> sh = mtaDefaultShown(*r);
  REQUIRE(sh.size() == 3);
  CHECK(r->series[sh[0]].id == "4");
  CHECK(r->series[sh[1]].id == "5");
  CHECK(r->series[sh[2]].id == "6");
  // 7 本以上あっても 6 本まで
  MtaReport many;
  for (int i = 0; i < 10; ++i) {
    MtaSeries s;
    s.id = std::to_string(i);
    many.series.push_back(s);
  }
  CHECK(mtaDefaultShown(many).size() == 6);
}

// ---------------------------------------------------------------- HTML 書き出し・週次レポート
namespace {

bool has(const std::string& s, const std::string& needle) { return s.find(needle) != std::string::npos; }

// 4 が 7 営業日遅れた記録（10/05・10/12 の週。基準日 10/19 のいまとの差で警告になる）
WeeklyAuto lateAuto(const Proj& p) {
  const Day x = p.finishOf("4");
  return autoOf({snapOf("2026-10-05", {msItem("4", 'p', p.wd(x, -7)), msItem("5", 't', p.finishOf("5"))}),
                 snapOf("2026-10-12", {msItem("4", 'p', p.wd(x, -7)), msItem("5", 't', p.finishOf("5"))})});
}

std::string exportOf(const Proj& p, const WeeklyAuto* a, int lateDays = kMtaDefaultLateDays, bool stats = true) {
  FixedClock clk(LocalTime{2026, 10, 19, 18, 36, 0});
  ExportInput in(p.comp.T, p.issues, p.comp.R, p.cal);
  in.base = D("2026-10-19");
  in.meta = &p.meta;
  in.projectName = "テスト";
  in.clock = &clk;
  in.weeklyAuto = a;
  in.mtaLateDays = lateDays;
  ExportOptions o;
  o.gantt = false;
  o.list = false;
  o.stats = stats;
  return exportHtml(in, o);
}

}  // namespace

TEST_CASE("mta: HTML 書き出しの統計に、推移の図と表・遅れの警告を出す（スクリプトなし。警告は統計の上）") {
  Proj p;
  const WeeklyAuto a = lateAuto(p);
  const auto r = p.mta(a);
  REQUIRE(r.ok());
  REQUIRE(r->warnings == 1);
  const std::string warn = mtaWarnText(*seriesOf(*r, "4"));
  const std::string html = exportOf(p, &a);
  for (const char* want : {"マイルストーンの予測の推移", "10/5 の週", "10/12 の週", "いま（10/19）", "前の点からの動き", "期限 11/13", "stroke-dasharray",
                           "記録なし（線を引かない）", "遅れの警告"}) {
    INFO("missing: " << want);
    CHECK(has(html, want));
  }
  CHECK(has(html, warn));
  CHECK(html.find(warn) < html.find("余裕の分布"));  // 警告は統計の上（各節より前）
  CHECK_FALSE(has(html, "<script"));
  CHECK(text::isValidUtf8(html));
  // 記録を渡さなければ出ない・統計を出さないときも出ない
  CHECK_FALSE(has(exportOf(p, nullptr), "マイルストーンの予測の推移"));
  CHECK_FALSE(has(exportOf(p, &a, kMtaDefaultLateDays, false), "マイルストーンの予測の推移"));
  // しきい値を 8 にすると警告は出ない（図は出る）
  const std::string lenient = exportOf(p, &a, 8);
  CHECK_FALSE(has(lenient, warn));
  CHECK_FALSE(has(lenient, "遅れの警告"));
  CHECK(has(lenient, "マイルストーンの予測の推移"));
  // ダークでも出る
  FixedClock clk(LocalTime{2026, 10, 19, 18, 36, 0});
  ExportInput in(p.comp.T, p.issues, p.comp.R, p.cal);
  in.base = D("2026-10-19");
  in.meta = &p.meta;
  in.clock = &clk;
  in.weeklyAuto = &a;
  ExportOptions dk;
  dk.gantt = false;
  dk.list = false;
  dk.dark = true;
  CHECK(has(exportHtml(in, dk), "マイルストーンの予測の推移"));
}

TEST_CASE("mta: HTML 書き出し — 記録が 2 週に満たなければ案内だけ（嘘の線を引かない）") {
  Proj p;
  const Day x = p.finishOf("4");
  const WeeklyAuto none;
  const WeeklyAuto one = autoOf({snapOf("2026-10-19", {msItem("4", 'p', x)})});
  const std::string h0 = exportOf(p, &none);
  CHECK(has(h0, "記録が溜まるとここに出ます（いま 0 週ぶん）"));
  const std::string h1 = exportOf(p, &one);
  CHECK(has(h1, "記録が溜まるとここに出ます（いま 1 週ぶん）"));
  CHECK_FALSE(has(h1, "aria-label=\"マイルストーンの予測の推移\""));  // 図（SVG）は描かない
  CHECK_FALSE(has(h1, "遅れの警告"));
}

TEST_CASE("mta: HTML 書き出し — 件名に危険な文字があっても壊れず、スクリプトが現れない") {
  std::string csv = kCsv;
  const std::size_t at = csv.find("中間");
  REQUIRE(at != std::string::npos);
  csv.replace(at, std::string("中間").size(), "<script>alert(1)</script>&'");
  Proj p(csv);
  const WeeklyAuto a = lateAuto(p);
  const std::string html = exportOf(p, &a);
  CHECK_FALSE(has(html, "<script"));
  CHECK(has(html, "&lt;script&gt;alert(1)&lt;/script&gt;"));
  CHECK(text::isValidUtf8(html));
}

namespace {
WeeklyReport weeklyOf(const Proj& p, const WeeklyAuto* a, const WeeklyOptions& opt = {}, int lateDays = kMtaDefaultLateDays) {
  const ProjectStats stats = computeStats(p.comp.T, p.issues, p.comp.R, D("2026-10-19"));
  const LoadReport load = computeLoad(p.comp.T, p.issues, p.comp.R, p.cal, D("2026-10-19"), ResourceLimit{});
  WeeklyInput in(p.comp.T, p.issues, p.comp.R, p.cal, stats, load);
  in.base = D("2026-10-19");
  in.projectName = "テスト";
  in.autos = a;
  in.mtaLateDays = lateDays;
  return buildWeeklyReport(in, opt);
}
}  // namespace

TEST_CASE("mta: 週次レポート — 警告は「遅れ・余裕小」に、推移の表は「マイルストーン」に出る（Markdown・HTML とも）") {
  Proj p;
  const WeeklyAuto a = lateAuto(p);
  const WeeklyReport r = weeklyOf(p, &a);
  REQUIRE(r.mtaAvailable);
  CHECK(r.mta.warnings == 1);
  CHECK(r.mta.drawable());
  const std::string warn = mtaWarnText(*seriesOf(r.mta, "4"));
  const std::string md = weeklyMarkdown(r);
  CHECK(has(md, "マイルストーンの予測の遅れ（1 件）"));
  CHECK(has(md, escapeMarkdown(mtaSeriesName(*seriesOf(r.mta, "4"))) + ": " + escapeMarkdown(mtaWarnTail(*seriesOf(r.mta, "4")))));
  CHECK(has(md, "予測の推移"));
  CHECK(has(md, "| マイルストーン | 週 | 予測日 | 前の点からの動き |"));
  CHECK(has(md, "10/5 の週"));
  CHECK(has(md, "いま（10/19）"));
  CHECK(md.find("マイルストーンの予測の遅れ") > md.find("遅れ・余裕小"));
  CHECK(md.find("マイルストーンの予測の遅れ") < md.find("\n## マイルストーン\n"));  // 遅れ・余裕小の節の中
  const std::string html = weeklyHtml(r);
  CHECK(has(html, "マイルストーンの予測の遅れ"));
  CHECK(has(html, escapeXml(warn)));
  CHECK(has(html, "前の点からの動き"));
  CHECK_FALSE(has(html, "<script"));
  // 節を外せば出ない
  WeeklyOptions noRisks;
  noRisks.sections.risks = false;
  const std::string md2 = weeklyMarkdown(weeklyOf(p, &a, noRisks));
  CHECK_FALSE(has(md2, "マイルストーンの予測の遅れ"));
  CHECK(has(md2, "予測の推移"));
  WeeklyOptions noMs;
  noMs.sections.milestones = false;
  const std::string md3 = weeklyMarkdown(weeklyOf(p, &a, noMs));
  CHECK(has(md3, "マイルストーンの予測の遅れ"));
  CHECK_FALSE(has(md3, "予測の推移"));
  // しきい値 8 なら警告なし（推移の表は出る）
  const std::string md4 = weeklyMarkdown(weeklyOf(p, &a, {}, 8));
  CHECK_FALSE(has(md4, "マイルストーンの予測の遅れ"));
  CHECK(has(md4, "予測の推移"));
  // 記録（autos）を渡さなければ何も出ない
  const WeeklyReport none = weeklyOf(p, nullptr);
  CHECK_FALSE(none.mtaAvailable);
  CHECK_FALSE(has(weeklyMarkdown(none), "予測の推移"));
  CHECK_FALSE(has(weeklyHtml(none), "予測の推移"));
}

TEST_CASE("mta: 週次レポート — 記録が 2 週に満たなければ案内だけ。件名のエスケープ") {
  std::string csv = kCsv;
  const std::size_t at = csv.find("中間");
  csv.replace(at, std::string("中間").size(), "<b>*中間*</b>");
  Proj p(csv);
  const WeeklyAuto one = autoOf({snapOf("2026-10-19", {msItem("4", 'p', p.finishOf("4"))})});
  const WeeklyReport r = weeklyOf(p, &one);
  REQUIRE(r.mtaAvailable);
  const std::string md = weeklyMarkdown(r);
  CHECK(has(md, "記録が溜まるとここに出ます（いま 1 週ぶん）"));
  CHECK_FALSE(has(md, "| マイルストーン | 週 |"));
  // 2 週ぶんあるとき、件名は Markdown・HTML とも安全に出る
  const Day x = p.finishOf("4");
  const WeeklyAuto two = autoOf({snapOf("2026-10-05", {msItem("4", 'p', p.wd(x, -9))}), snapOf("2026-10-12", {msItem("4", 'p', p.wd(x, -9))})});
  const WeeklyReport r2 = weeklyOf(p, &two);
  const std::string md2 = weeklyMarkdown(r2);
  CHECK_FALSE(has(md2, "<b>*"));
  const std::string html2 = weeklyHtml(r2);
  CHECK_FALSE(has(html2, "<b>*"));
  CHECK(has(html2, "&lt;b&gt;*中間*&lt;/b&gt;"));
}

// ---------------------------------------------------------------- Copilot の指摘（#94）への回帰テスト
TEST_CASE("mta: 完了日のない完了済みのマイルストーンは、達成した日として記録せず、点にもしない") {
  std::string csv = kCsv;
  const std::size_t at = csv.find("2026-10-14");  // 6（完了済み）の Resolved
  REQUIRE(at != std::string::npos);
  csv.erase(at, std::string("2026-10-14").size());
  Proj p(csv);
  const ProjectStats st = computeStats(p.comp.T, p.issues, p.comp.R, D("2026-10-19"));
  const WeeklySnapshot s = makeWeeklySnapshot(p.comp.T, p.issues, p.comp.R, st, D("2026-10-19"));
  const WeeklySnapItem* six = nullptr;
  for (const WeeklySnapItem& it : s.milestones)
    if (it.id == "6") six = &it;
  REQUIRE(six);
  CHECK(six->st == 'd');
  CHECK_FALSE(six->finish);  // asapFinish（期限や基準日の前の営業日）を達成日にしない
  // いまの点も作らない（過去の記録があっても、達成として描かない）
  const WeeklyAuto a = autoOf({snapOf("2026-10-05", {msItem("6", 'p', D("2026-10-16"))}), snapOf("2026-10-12", {msItem("6", 'p', D("2026-10-16"))})});
  const auto r = p.mta(a);
  REQUIRE(r.ok());
  const MtaSeries* m6 = seriesOf(*r, "6");
  REQUIRE(m6);
  CHECK_FALSE(m6->points.back().has);
  CHECK(m6->achievedNow);
  CHECK_FALSE(m6->warn);
}

TEST_CASE("mta: 作業が 5,000 件を超えても、あとに続くマイルストーンはスナップショットに入る") {
  std::string csv = "Issue Id,Issue Type,Summary,Parent,Status,Duration (weeks)\n";
  for (int i = 1; i <= 5001; ++i) csv += std::to_string(i) + ",Task,T" + std::to_string(i) + ",,To Do,1\n";
  csv += "9000,Milestone,最後の節目,,,0\n";
  Proj p(csv);
  const ProjectStats st = computeStats(p.comp.T, p.issues, p.comp.R, D("2026-10-19"));
  const WeeklySnapshot s = makeWeeklySnapshot(p.comp.T, p.issues, p.comp.R, st, D("2026-10-19"));
  CHECK(s.items.size() == kWeeklyMaxSnapshotItems);
  CHECK(s.truncated);
  REQUIRE(s.milestones.size() == 1);
  CHECK(s.milestones[0].id == "9000");
}

TEST_CASE("mta: 週次レポートの警告は、上限で省いた件数を警告の節にも「ほか N 件」と出す（推移の節を外しても）") {
  Proj p;
  const Day x = p.finishOf("4");
  const WeeklyAuto a = autoOf({snapOf("2026-10-05", {msItem("4", 'p', p.wd(x, -7)), msItem("5", 't', p.wd(p.finishOf("5"), -7))}),
                               snapOf("2026-10-12", {msItem("4", 'p', p.wd(x, -7)), msItem("5", 't', p.wd(p.finishOf("5"), -7))})});
  WeeklyOptions o;
  o.maxItems = 1;
  o.sections.milestones = false;  // 推移の節は出さない
  const WeeklyReport r = weeklyOf(p, &a, o);
  REQUIRE(r.mtaAvailable);
  CHECK(r.mta.warnings == 2);
  CHECK(r.mta.series.size() == 1);
  CHECK(r.mtaWarnOmitted == 1);
  const std::string md = weeklyMarkdown(r);
  CHECK(has(md, "マイルストーンの予測の遅れ（2 件）"));
  CHECK(has(md, "ほか 1 件"));
  CHECK_FALSE(has(md, "予測の推移"));
  const std::string html = weeklyHtml(r);
  CHECK(has(html, "ほか 1 件"));
}

TEST_CASE("mta: 課題を全部削除したあとも、削除されたマイルストーンの記録は週次レポート・HTML 書き出しに出る") {
  Proj p;
  const WeeklyAuto a = autoOf({snapOf("2026-10-05", {msItem("9", 't', D("2026-11-06"))}), snapOf("2026-10-12", {msItem("9", 't', D("2026-11-13"))})});
  {
    const Tree empty;
    const std::vector<Issue> noIssues;
    const std::vector<ScheduleRow> noRows;
    const ProjectStats stats;
    const LoadReport load;
    WeeklyInput in(empty, noIssues, noRows, p.cal, stats, load);
    in.base = D("2026-10-19");
    in.autos = &a;
    const WeeklyReport r = buildWeeklyReport(in, {});
    CHECK(r.empty);
    REQUIRE(r.mtaAvailable);
    const std::string md = weeklyMarkdown(r);
    CHECK(has(md, "課題がありません"));
    CHECK(has(md, "(削除された) 9"));
    CHECK(has(md, "予測の推移"));
    CHECK(has(weeklyHtml(r), "(削除された) 9"));
    WeeklyOptions off;
    off.sections.milestones = false;
    CHECK_FALSE(has(weeklyMarkdown(buildWeeklyReport(in, off)), "(削除された) 9"));
  }
  // 作業が 0 件（マイルストーンだけ）の HTML 書き出しでも、推移の節を出す
  Proj onlyMs("Issue Id,Issue Type,Summary,Parent,Status,Priority,Resolved,Duration (weeks),Deadline\n1,Milestone,節目,,,High,,0,2026-11-13\n");
  const WeeklyAuto b = autoOf({snapOf("2026-10-05", {msItem("1", 't', D("2026-11-06")), msItem("9", 't', D("2026-11-06"))}),
                               snapOf("2026-10-12", {msItem("1", 't', D("2026-11-13")), msItem("9", 't', D("2026-11-13"))})});
  const std::string html = exportOf(onlyMs, &b);
  CHECK(has(html, "マイルストーンの予測の推移"));
  CHECK(has(html, "(削除された) 9"));
}
