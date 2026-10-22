// 状況の件数の履歴（Issue #64。#54 の B）: 記録・保存・壊れた設定・累積フロー図の列
#include <doctest/doctest.h>

#include <memory>
#include <string>
#include <vector>

#include "roadmap/clock.hpp"
#include "roadmap/commands.hpp"
#include "roadmap/csv.hpp"
#include "roadmap/history.hpp"
#include "roadmap/project_io.hpp"
#include "roadmap/schedule.hpp"

using namespace roadmap;
using Json = nlohmann::ordered_json;

namespace {

Day D(const char* iso) { return *parseIso(iso); }

HistoryPoint P(const char* week, int todo, int doing, int done) {
  HistoryPoint p;
  p.weekStart = week;
  p.todo = todo;
  p.doing = doing;
  p.done = done;
  p.total = todo + doing + done;
  return p;
}

// 2026-10-19 は月曜
std::string mondayPlus(int weeks) { return fmtIso(D("2026-10-19") + weeks * 7); }

Json pj(const char* week, int todo, int doing, int done, int total) {
  return Json{{"week", week}, {"todo", todo}, {"doing", doing}, {"done", done}, {"total", total}};
}
Json hj(std::initializer_list<Json> pts) {
  Json a = Json::array();
  for (const Json& p : pts) a.push_back(p);
  return Json{{"points", a}};
}

struct Proj {
  std::vector<Issue> issues;
  ProjectMeta meta;
  Computed comp;
  Calendar cal = Calendar::fromText(defaultHolidaysText());
  static Proj fromCsv(const std::string& text) {
    Proj p;
    auto r = csv::importCsv(text);
    REQUIRE(r.ok());
    p.issues = std::move(r.value().issues);
    p.meta = std::move(r.value().meta);
    ScheduleOptions opt;
    opt.base = D("2026-10-19");
    auto c = computeAll(p.issues, p.meta, p.cal, opt);
    REQUIRE(c.ok());
    p.comp = std::move(c).value();
    p.comp.issues = p.issues;
    return p;
  }
};

const char* const kCsv =
    "Issue Id,Issue Type,Summary,Parent,Assignee,Status,Priority,Resolved,Duration (weeks)\n"
    "1,Epic,全体,,,,High,,\n"
    "2,Task,A,1,田中,Done,High,2026-10-13,1\n"
    "3,Task,B,1,田中,In Progress,Highest,,2\n"
    "4,Task,C,1,鈴木,To Do,Medium,,2\n"
    "5,Task,D,1,,To Do,Low,,1\n"
    "6,Milestone,完了,1,,,High,,0\n"
    "7,Task,単独,,鈴木,Done,Medium,2026-10-14,1\n";

}  // namespace

// ---------------------------------------------------------------- 週の計算
TEST_CASE("history: 週の月曜（月曜・日曜・年またぎ）") {
  CHECK(historyWeekStart(D("2026-10-19")) == "2026-10-19");  // 月曜
  CHECK(historyWeekStart(D("2026-10-25")) == "2026-10-19");  // 日曜
  CHECK(historyWeekStart(D("2026-10-21")) == "2026-10-19");
  CHECK(historyWeekStart(D("2027-01-01")) == "2026-12-28");  // 金曜。年をまたぐ
  CHECK(historyWeekStart(D("2026-12-28")) == "2026-12-28");
  CHECK(historyWeekStart(D("2028-02-29")) == "2028-02-28");  // うるう日（火曜）
}

// ---------------------------------------------------------------- 記録
TEST_CASE("history: 追加・同じ週は置き換え・昇順を保つ") {
  History h;
  CHECK(h.empty());
  REQUIRE(recordHistory(h, P("2026-10-26", 3, 1, 5)).ok());
  REQUIRE(recordHistory(h, P("2026-10-12", 5, 0, 1)).ok());  // 前の週は先頭に入る
  REQUIRE(recordHistory(h, P("2026-11-02", 2, 2, 5)).ok());
  REQUIRE(h.points.size() == 3);
  CHECK(h.points[0].weekStart == "2026-10-12");
  CHECK(h.points[1].weekStart == "2026-10-26");
  CHECK(h.points[2].weekStart == "2026-11-02");
  // 同じ週は置き換え（週の最後の状態が残る）
  REQUIRE(recordHistory(h, P("2026-10-26", 1, 1, 7)).ok());
  REQUIRE(h.points.size() == 3);
  CHECK(h.points[1] == P("2026-10-26", 1, 1, 7));
  // 週の間（19 日の週）にも入る
  REQUIRE(recordHistory(h, P("2026-10-19", 4, 0, 2)).ok());
  REQUIRE(h.points.size() == 4);
  CHECK(h.points[1].weekStart == "2026-10-19");
}

TEST_CASE("history: 壊れた点は断り、何も変えない") {
  History h;
  REQUIRE(recordHistory(h, P("2026-10-19", 1, 1, 1)).ok());
  const History before = h;
  HistoryPoint notMonday = P("2026-10-20", 1, 1, 1);
  CHECK_FALSE(recordHistory(h, notMonday).ok());
  HistoryPoint badDate = P("2026-13-40", 1, 1, 1);
  CHECK_FALSE(recordHistory(h, badDate).ok());
  CHECK_FALSE(recordHistory(h, P("", 1, 1, 1)).ok());
  HistoryPoint neg = P("2026-10-26", 1, 1, 1);
  neg.todo = -1;
  neg.total = 1;
  CHECK_FALSE(recordHistory(h, neg).ok());
  HistoryPoint mismatch = P("2026-10-26", 1, 1, 1);
  mismatch.total = 99;
  CHECK_FALSE(recordHistory(h, mismatch).ok());
  HistoryPoint huge = P("2026-10-26", 1, 1, 1);
  huge.todo = 2000000000;
  huge.doing = 2000000000;
  huge.total = 0;
  CHECK_FALSE(recordHistory(h, huge).ok());
  CHECK(h == before);
  CHECK(recordHistory(h, P("2026-10-26", 1, 1, 1)).ok());  // 正しい点は通る
}

TEST_CASE("history: 上限 104 週を超えたら古いものから落とす") {
  History h;
  for (int i = 0; i < 110; ++i) REQUIRE(recordHistory(h, P(mondayPlus(i).c_str(), i, 0, 0)).ok());
  REQUIRE(h.points.size() == kHistoryMaxWeeks);
  CHECK(h.points.front().weekStart == mondayPlus(6));  // 0〜5 が落ちた
  CHECK(h.points.back().weekStart == mondayPlus(109));
  // 上限いっぱいでも、同じ週の置き換えでは落とさない
  REQUIRE(recordHistory(h, P(mondayPlus(50).c_str(), 9, 9, 9)).ok());
  CHECK(h.points.size() == kHistoryMaxWeeks);
  CHECK(h.points.front().weekStart == mondayPlus(6));
  // 順序は昇順のまま
  for (std::size_t i = 1; i < h.points.size(); ++i) CHECK(h.points[i - 1].weekStart < h.points[i].weekStart);
}

TEST_CASE("history: 年またぎの週を順に記録できる") {
  History h;
  REQUIRE(recordHistory(h, P("2027-01-04", 1, 0, 0)).ok());
  REQUIRE(recordHistory(h, P("2026-12-28", 2, 0, 0)).ok());
  REQUIRE(recordHistory(h, P("2026-12-21", 3, 0, 0)).ok());
  REQUIRE(h.points.size() == 3);
  CHECK(h.points[0].weekStart == "2026-12-21");
  CHECK(h.points[1].weekStart == "2026-12-28");
  CHECK(h.points[2].weekStart == "2027-01-04");
  CHECK(historyHasWeek(h, D("2027-01-01")));  // 2026-12-28 の週
  CHECK(historyHasWeek(h, D("2027-01-03")));
  CHECK(historyHasWeek(h, D("2027-01-04")));
  CHECK_FALSE(historyHasWeek(h, D("2027-01-11")));
  CHECK_FALSE(historyHasWeek(h, D("2026-12-20")));  // 日曜は前の週
}

// ---------------------------------------------------------------- 取り込み（capture）
TEST_CASE("history: 現在の状況を数える（マイルストーンと親は数えない）") {
  Proj p = Proj::fromCsv(kCsv);
  const auto r = captureHistoryPoint(p.issues, p.comp, D("2026-10-21"));  // 水曜
  REQUIRE(r.ok());
  CHECK(r.value().weekStart == "2026-10-19");
  CHECK(r.value().done == 2);   // 2・7
  CHECK(r.value().doing == 1);  // 3
  CHECK(r.value().todo == 2);   // 4・5
  CHECK(r.value().total == 5);  // 6（マイルストーン）と 1（親）は数えない
}

TEST_CASE("history: 空のプロジェクトは 0 件の点を返す") {
  Proj p;
  ScheduleOptions opt;
  opt.base = D("2026-10-19");
  auto c = computeAll(p.issues, p.meta, p.cal, opt);
  REQUIRE(c.ok());
  p.comp = std::move(c).value();
  const auto r = captureHistoryPoint(p.issues, p.comp, D("2026-10-19"));
  REQUIRE(r.ok());
  CHECK(r.value() == P("2026-10-19", 0, 0, 0));
}

TEST_CASE("history: マイルストーンだけのプロジェクトは 0 件") {
  Proj p = Proj::fromCsv(
      "Issue Id,Issue Type,Summary,Parent,Status,Duration (weeks)\n"
      "1,Milestone,節目,,To Do,0\n");
  const auto r = captureHistoryPoint(p.issues, p.comp, D("2026-10-19"));
  REQUIRE(r.ok());
  CHECK(r.value().total == 0);
}

TEST_CASE("history: 課題の数と計算結果が合わないときはエラー") {
  Proj p = Proj::fromCsv(kCsv);
  std::vector<Issue> shorter(p.issues.begin(), p.issues.end() - 1);
  CHECK_FALSE(captureHistoryPoint(shorter, p.comp, D("2026-10-19")).ok());
}

// ---------------------------------------------------------------- 保存
TEST_CASE("history: 保存の往復と、空なら settings に書かない") {
  History h;
  REQUIRE(recordHistory(h, P("2026-10-12", 5, 0, 1)).ok());
  REQUIRE(recordHistory(h, P("2026-10-19", 3, 1, 2)).ok());
  const Json j = historyToJson(h);
  CHECK(historyFromJson(j) == h);
  CHECK(historyFromJson(Json::parse(j.dump())) == h);
  // 件数だけ。名前・担当者などの文字は入らない
  const std::string text = j.dump();
  CHECK(text.find("田中") == std::string::npos);

  FixedClock clk(LocalTime{2026, 10, 19, 9, 0, 0});
  ProjectRecord rec;
  rec.settings.id = "p1";
  rec.settings.name = "P";
  rec.settings.baseDate = "2026-10-19";
  rec.settings.history = h;
  rec.settings.unknown["futureKey"] = Json{{"a", 1}};
  const Json sj = settingsToJson(rec);
  REQUIRE(sj.contains("history"));
  CHECK(sj["futureKey"]["a"] == 1);
  const ProjectRecord back = recordFromSettingsJson(Json::parse(sj.dump()), "", "stem", clk);
  CHECK(back.settings.history == h);
  CHECK_FALSE(back.settings.unknown.contains("history"));  // 既知のキーは unknown に入れない
  CHECK(back.settings.unknown["futureKey"]["a"] == 1);
  // 空なら書かない
  rec.settings.history = History{};
  CHECK_FALSE(settingsToJson(rec).contains("history"));
  // 共有・バックアップの JSON には入れない
  rec.settings.history = h;
  CHECK(dumpJs(recordToShareJson(rec)).find("history") == std::string::npos);
}

TEST_CASE("history: 壊れた設定は落ちず、読み飛ばす") {
  // 型が違う
  CHECK(historyFromJson(Json(5)).empty());
  CHECK(historyFromJson(Json("x")).empty());
  CHECK(historyFromJson(Json::array()).empty());
  CHECK(historyFromJson(Json(nullptr)).empty());
  CHECK(historyFromJson(Json::object()).empty());
  CHECK(historyFromJson(Json{{"points", 3}}).empty());
  CHECK(historyFromJson(Json{{"points", Json::array({1, "a", nullptr, Json::array()})}}).empty());
  // 良い点と壊れた点が混ざる
  const History h = historyFromJson(hj({
      pj("2026-10-05", 1, 0, 0, 1),       // 良い
      pj("2026-10-12", 1, 0, 0, 5),       // 合計の食い違い
      pj("2026-10-13", 1, 0, 0, 1),       // 月曜でない
      pj("2026-13-01", 1, 0, 0, 1),       // 日付が壊れている
      pj("2026-10-19", -1, 0, 0, -1),     // 負数
      pj("2026-10-19", 2, 0, 0, 2),       // 良い
      pj("2026-10-19", 3, 0, 0, 3),       // 重複（同じ週。先のものを残す）
      pj("2026-10-12", 4, 0, 0, 4),       // 順序が逆
      pj("2026-10-26", 1, 1, 1, 3),       // 良い
  }));
  REQUIRE(h.points.size() == 3);
  CHECK(h.points[0] == P("2026-10-05", 1, 0, 0));
  CHECK(h.points[1] == P("2026-10-19", 2, 0, 0));
  CHECK(h.points[2] == P("2026-10-26", 1, 1, 1));
  // 欄の型が違う・欠けている
  Json bad = hj({pj("2026-10-05", 1, 0, 0, 1)});
  bad["points"][0]["todo"] = "1";
  CHECK(historyFromJson(bad).empty());
  bad = hj({pj("2026-10-05", 1, 0, 0, 1)});
  bad["points"][0]["todo"] = 1.5;
  CHECK(historyFromJson(bad).empty());
  bad = hj({pj("2026-10-05", 1, 0, 0, 1)});
  bad["points"][0].erase("total");
  CHECK(historyFromJson(bad).empty());
  bad = hj({pj("2026-10-05", 1, 0, 0, 1)});
  bad["points"][0].erase("week");
  CHECK(historyFromJson(bad).empty());
  bad = hj({pj("2026-10-05", 1, 0, 0, 1)});
  bad["points"][0]["week"] = 20261005;
  CHECK(historyFromJson(bad).empty());
}

TEST_CASE("history: 巨大な値・多すぎる点") {
  // 件数が大きすぎる
  CHECK(historyFromJson(hj({pj("2026-10-05", 2000000000, 0, 0, 2000000000)})).empty());
  Json big = hj({pj("2026-10-05", 1, 0, 0, 1)});
  big["points"][0]["todo"] = 9223372036854775807LL;
  big["points"][0]["total"] = 9223372036854775807LL;
  CHECK(historyFromJson(big).empty());
  big["points"][0]["todo"] = 1e30;
  CHECK(historyFromJson(big).empty());
  // 合計がオーバーフローする組み合わせ
  CHECK(historyFromJson(hj({pj("2026-10-05", 9999999, 9999999, 9999999, 29999997)})).points.size() == 1);
  CHECK(historyFromJson(hj({pj("2026-10-05", 10000001, 0, 0, 10000001)})).empty());
  // 200 週ぶんあっても、新しい 104 週だけを持つ
  Json a = Json::array();
  for (int i = 0; i < 200; ++i) a.push_back(pj(mondayPlus(i).c_str(), i, 0, 0, i));
  const History h = historyFromJson(Json{{"points", a}});
  REQUIRE(h.points.size() == kHistoryMaxWeeks);
  CHECK(h.points.front().weekStart == mondayPlus(96));
  CHECK(h.points.back().weekStart == mondayPlus(199));
  // 数が多くても（良い点がない）落ちない
  Json many = Json::array();
  for (int i = 0; i < 20000; ++i) many.push_back(Json(i));
  CHECK(historyFromJson(Json{{"points", many}}).empty());
}

// ---------------------------------------------------------------- 累積フロー図の列
TEST_CASE("history: cfdSeries — 空・1 点・連続") {
  History h;
  CfdSeries s = cfdSeries(h, D("2026-10-19"));
  CHECK(s.weeks.empty());
  CHECK(s.recorded == 0);
  CHECK_FALSE(s.drawable());

  REQUIRE(recordHistory(h, P("2026-10-19", 3, 1, 1)).ok());
  s = cfdSeries(h, D("2026-10-21"));
  REQUIRE(s.weeks.size() == 1);
  CHECK(s.recorded == 1);
  CHECK_FALSE(s.drawable());
  CHECK_FALSE(s.weeks[0].gap);

  REQUIRE(recordHistory(h, P("2026-10-26", 2, 2, 1)).ok());
  s = cfdSeries(h, D("2026-10-28"));
  REQUIRE(s.weeks.size() == 2);
  CHECK(s.recorded == 2);
  CHECK(s.drawable());
  CHECK(s.weeks[0].start == D("2026-10-19"));
  CHECK(s.weeks[1].start == D("2026-10-26"));
  CHECK(s.weeks[1].todo == 2);
  CHECK(s.weeks[1].doing == 2);
  CHECK(s.weeks[1].done == 1);
  CHECK(s.weeks[1].total == 5);
  CHECK(s.maxTotal == 5);
}

TEST_CASE("history: cfdSeries — 間の空き週・先頭・末尾は gap（嘘の線を作らない）") {
  History h;
  REQUIRE(recordHistory(h, P("2026-10-05", 5, 0, 0)).ok());
  REQUIRE(recordHistory(h, P("2026-10-26", 3, 1, 1)).ok());  // 12・19 の週は記録なし
  // 基準日が最後の記録の週と同じ → 末尾の gap なし
  CfdSeries s = cfdSeries(h, D("2026-10-27"));
  REQUIRE(s.weeks.size() == 4);
  CHECK_FALSE(s.weeks[0].gap);
  CHECK(s.weeks[1].gap);
  CHECK(s.weeks[2].gap);
  CHECK_FALSE(s.weeks[3].gap);
  CHECK(s.weeks[1].total == 0);
  CHECK(s.recorded == 2);
  // 基準日が 2 週あとなら、末尾も gap（まだ記録していない週）
  s = cfdSeries(h, D("2026-11-10"));
  REQUIRE(s.weeks.size() == 6);
  CHECK(s.weeks[4].gap);
  CHECK(s.weeks[5].gap);
  CHECK(s.weeks[5].start == D("2026-11-09"));
  CHECK(s.recorded == 2);
  // 基準日が最初の記録より前でも、最初の記録より前は含めない（最後の記録の週まで）
  s = cfdSeries(h, D("2026-09-01"));
  REQUIRE(s.weeks.size() == 4);
  CHECK(s.weeks.front().start == D("2026-10-05"));
  CHECK(s.weeks.back().start == D("2026-10-26"));
}

TEST_CASE("history: cfdSeries — 年またぎ・上限・遠い基準日") {
  History h;
  REQUIRE(recordHistory(h, P("2026-12-21", 1, 0, 0)).ok());
  REQUIRE(recordHistory(h, P("2027-01-04", 1, 0, 0)).ok());
  CfdSeries s = cfdSeries(h, D("2027-01-05"));
  REQUIRE(s.weeks.size() == 3);
  CHECK(s.weeks[1].gap);
  CHECK(s.weeks[1].start == D("2026-12-28"));

  // 基準日が 10 年先でも列は 104 週まで（時間もかからない）
  s = cfdSeries(h, D("2037-01-05"));
  CHECK(s.weeks.size() == kHistoryMaxWeeks);
  CHECK(s.weeks.back().start == D("2037-01-05"));
  for (const CfdWeek& w : s.weeks) CHECK(w.gap);  // 古い記録は窓の外
  CHECK(s.recorded == 0);

  // 104 週ぶんの記録 + 基準日が 5 週先 → 窓は新しい 104 週
  History full;
  for (int i = 0; i < 110; ++i) REQUIRE(recordHistory(full, P(mondayPlus(i).c_str(), 1, 0, 0)).ok());
  s = cfdSeries(full, D("2026-10-19") + 114 * 7);
  CHECK(s.weeks.size() == kHistoryMaxWeeks);
  CHECK(s.weeks.back().start == D("2026-10-19") + 114 * 7);
}

TEST_CASE("history: 画面と HTML で共有する文言") {
  CHECK(cfdPendingText(0) == "記録が溜まるとここに出ます（いま 0 週ぶん）");
  CHECK(cfdPendingText(1) == "記録が溜まるとここに出ます（いま 1 週ぶん）");
  CfdWeek w;
  w.start = D("2026-10-19");
  w.todo = 3;
  w.doing = 1;
  w.done = 5;
  w.total = 9;
  const std::string t = cfdWeekText(w);
  CHECK(t.find("10/19") != std::string::npos);
  CHECK(t.find("未着手 3 件") != std::string::npos);
  CHECK(t.find("進行中 1 件") != std::string::npos);
  CHECK(t.find("完了 5 件") != std::string::npos);
  CHECK(t.find("合計 9 件") != std::string::npos);
  w.gap = true;
  const std::string g = cfdWeekText(w);
  CHECK(g.find("記録なし") != std::string::npos);
  CHECK(g.find("未着手") == std::string::npos);
}

// ---------------------------------------------------------------- Session
namespace {
struct SessionFix {
  FixedClock clock{LocalTime{2026, 10, 21, 9, 30, 0}};  // 水曜
  ProjectData d;
  std::unique_ptr<Session> s;
  SessionFix() {
    auto r = csv::importCsv(kCsv);
    REQUIRE(r.ok());
    d.issues = std::move(r.value().issues);
    d.meta = std::move(r.value().meta);
    d.settings.baseDate = "2026-10-19";
    d.settings.holidays = defaultHolidaysText();
    s = std::make_unique<Session>(d, &clock);
    REQUIRE(s->error().empty());
  }
};
}  // namespace

TEST_CASE("history: Session — 開いたときの自動記録は、同じ週なら何もしない") {
  SessionFix f;
  CHECK(f.s->history().empty());
  const std::uint64_t v0 = f.s->version();
  const auto r1 = f.s->recordHistory(HistoryMode::IfMissing);
  REQUIRE(r1.ok());
  CHECK(r1.value() == HistoryResult::Recorded);
  REQUIRE(f.s->history().points.size() == 1);
  CHECK(f.s->history().points[0] == P("2026-10-19", 2, 1, 2));
  CHECK(f.s->version() > v0);
  // 同じ週にもう一度 → 何もしない（保存も予約しない）
  const std::uint64_t v1 = f.s->version();
  const auto r2 = f.s->recordHistory(HistoryMode::IfMissing);
  REQUIRE(r2.ok());
  CHECK(r2.value() == HistoryResult::AlreadyHave);
  CHECK(f.s->version() == v1);
  CHECK(f.s->history().points.size() == 1);
  // 週が変わったら追加
  f.clock.set(LocalTime{2026, 10, 28, 9, 30, 0});
  const auto r3 = f.s->recordHistory(HistoryMode::IfMissing);
  REQUIRE(r3.ok());
  CHECK(r3.value() == HistoryResult::Recorded);
  CHECK(f.s->history().points.size() == 2);
  CHECK(f.s->history().points[1].weekStart == "2026-10-26");
}

TEST_CASE("history: Session — 手動の記録は同じ週を置き換え、変わらなければ保存しない") {
  SessionFix f;
  REQUIRE(f.s->recordHistory(HistoryMode::IfMissing).ok());
  const std::uint64_t v0 = f.s->version();
  // 変化なし → 置き換えても版は進まない
  const auto same = f.s->recordHistory(HistoryMode::Replace);
  REQUIRE(same.ok());
  CHECK(same.value() == HistoryResult::Unchanged);
  CHECK(f.s->version() == v0);
  // 状況を変えてから記録 → 置き換わる
  REQUIRE(f.s->setField("4", [](Issue& o) { o.status = "Done"; o.resolved = parseIso("2026-10-21"); }));
  const std::uint64_t v1 = f.s->version();
  const auto rep = f.s->recordHistory(HistoryMode::Replace);
  REQUIRE(rep.ok());
  CHECK(rep.value() == HistoryResult::Replaced);
  CHECK(f.s->version() > v1);
  REQUIRE(f.s->history().points.size() == 1);
  CHECK(f.s->history().points[0].done == 3);
  CHECK(f.s->history().points[0].todo == 1);
  // 元に戻す（Undo）の対象外
  CHECK(f.s->undo());
  CHECK(f.s->history().points.size() == 1);
  CHECK(f.s->history().points[0].done == 3);
}

TEST_CASE("history: Session — 作業が 0 件の自動記録はしない（手動はする）") {
  FixedClock clock(LocalTime{2026, 10, 21, 9, 30, 0});
  ProjectData d;
  d.settings.baseDate = "2026-10-19";
  d.settings.holidays = defaultHolidaysText();
  Session s(d, &clock);
  REQUIRE(s.error().empty());
  const auto a = s.recordHistory(HistoryMode::IfMissing);
  REQUIRE(a.ok());
  CHECK(a.value() == HistoryResult::Skipped);
  CHECK(s.history().empty());
  const auto m = s.recordHistory(HistoryMode::Replace);
  REQUIRE(m.ok());
  CHECK(m.value() == HistoryResult::Recorded);
  CHECK(s.history().points.size() == 1);
}

TEST_CASE("history: Session — 設定に書き出して読み直すと残る") {
  SessionFix f;
  REQUIRE(f.s->recordHistory(HistoryMode::IfMissing).ok());
  const ProjectRecord rec = recordOf(f.s->data());
  const Json j = settingsToJson(rec);
  REQUIRE(j.contains("history"));
  const ProjectRecord back = recordFromSettingsJson(Json::parse(j.dump()), rec.csv, "x", f.clock);
  CHECK(back.settings.history == f.s->history());
}
