// 週報: 週の頭のスナップショットの自動取得と、「先週から変わったこと」の比べる元の選び方（Issue #70）
#include <doctest/doctest.h>

#include <algorithm>
#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "roadmap/clock.hpp"
#include "roadmap/commands.hpp"
#include "roadmap/csv.hpp"
#include "roadmap/load.hpp"
#include "roadmap/project_io.hpp"
#include "roadmap/schedule.hpp"
#include "roadmap/stats.hpp"
#include "roadmap/weekly.hpp"

using namespace roadmap;
using Json = nlohmann::ordered_json;

namespace {

Day D(const char* iso) { return *parseIso(iso); }

// 2026-10-19 は月曜。10/05・10/12・10/19・10/26 が週の頭
WeeklySnapshot snap(const char* iso) {
  WeeklySnapshot s;
  s.base = D(iso);
  return s;
}

WeeklyAuto autosOf(std::initializer_list<const char*> days) {
  WeeklyAuto a;
  for (const char* d : days) weeklyAutoPut(a, snap(d));
  return a;
}
WeeklyMemo manualOf(std::initializer_list<const char*> days) {
  WeeklyMemo m;
  for (const char* d : days) weeklyPutSnapshot(m, snap(d));
  return m;
}
// 選んだスナップショットへのポインタが、テストの間ずっと有効なように、一時の値を預かる
const WeeklyMemo& keep(WeeklyMemo m) {
  static std::deque<WeeklyMemo> pool;
  pool.push_back(std::move(m));
  return pool.back();
}
const WeeklyAuto& keep(WeeklyAuto a) {
  static std::deque<WeeklyAuto> pool;
  pool.push_back(std::move(a));
  return pool.back();
}
std::string iso(const WeeklySnapshot* s) { return s ? fmtIso(s->base) : std::string("(なし)"); }

WeeklyRange rangeOf(WeeklyPeriod p, const char* base, const char* from = "2026-10-19", const char* to = "2026-10-19") {
  return weeklyRange(D(base), p, D(from), D(to));
}

}  // namespace

// ---------------------------------------------------------------- 自動のスナップショットの入れ方
TEST_CASE("weeklyAuto: 同じ週は 1 つ（置き換え）、昇順、週の判定は月曜始まり") {
  WeeklyAuto a;
  CHECK(a.empty());
  CHECK_FALSE(weeklyAutoHasWeek(a, D("2026-10-21")));
  weeklyAutoPut(a, snap("2026-10-21"));  // 水
  CHECK(weeklyAutoHasWeek(a, D("2026-10-19")));  // 同じ週の月曜
  CHECK(weeklyAutoHasWeek(a, D("2026-10-25")));  // 同じ週の日曜
  CHECK_FALSE(weeklyAutoHasWeek(a, D("2026-10-18")));  // 前の週の日曜
  CHECK_FALSE(weeklyAutoHasWeek(a, D("2026-10-26")));  // 次の週の月曜
  weeklyAutoPut(a, snap("2026-10-23"));  // 同じ週 → 置き換え
  REQUIRE(a.snaps.size() == 1);
  CHECK(iso(&a.snaps[0]) == "2026-10-23");
  weeklyAutoPut(a, snap("2026-10-14"));  // 前の週 → 昇順の先頭へ
  weeklyAutoPut(a, snap("2026-10-28"));
  REQUIRE(a.snaps.size() == 3);
  CHECK(iso(&a.snaps[0]) == "2026-10-14");
  CHECK(iso(&a.snaps[1]) == "2026-10-23");
  CHECK(iso(&a.snaps[2]) == "2026-10-28");
  CHECK(weeklyAutoOfWeek(a, D("2026-10-19")) == &a.snaps[1]);
  CHECK(weeklyAutoOfWeek(a, D("2026-10-20")) != nullptr);  // 月曜でない日でも、その週のものを返す
  CHECK(weeklyAutoOfWeek(a, D("2026-11-02")) == nullptr);
}

TEST_CASE("weeklyAuto: 年またぎ（12/31 と 1/1 は同じ週、1/4 は次の週）") {
  WeeklyAuto a;
  weeklyAutoPut(a, snap("2026-12-31"));  // 木
  weeklyAutoPut(a, snap("2027-01-01"));  // 金。同じ週（12/28〜1/3）
  REQUIRE(a.snaps.size() == 1);
  CHECK(weeklyAutoHasWeek(a, D("2027-01-03")));
  CHECK_FALSE(weeklyAutoHasWeek(a, D("2027-01-04")));
  weeklyAutoPut(a, snap("2027-01-04"));
  CHECK(a.snaps.size() == 2);
}

TEST_CASE("weeklyAuto: 8 週まで。古いものから落とす") {
  WeeklyAuto a;
  static_assert(kWeeklyAutoMaxSnapshots == 8);
  for (int i = 0; i < 12; ++i) weeklyAutoPut(a, snap(fmtIso(D("2026-08-03") + i * 7).c_str()));
  REQUIRE(a.snaps.size() == kWeeklyAutoMaxSnapshots);
  CHECK(fmtIso(a.snaps.front().base) == fmtIso(D("2026-08-03") + 4 * 7));
  CHECK(fmtIso(a.snaps.back().base) == fmtIso(D("2026-08-03") + 11 * 7));
  // 順番が前後して入っても昇順
  WeeklyAuto b;
  weeklyAutoPut(b, snap("2026-10-19"));
  weeklyAutoPut(b, snap("2026-10-05"));
  weeklyAutoPut(b, snap("2026-10-12"));
  REQUIRE(b.snaps.size() == 3);
  CHECK(b.snaps[0].base < b.snaps[1].base);
  CHECK(b.snaps[1].base < b.snaps[2].base);
}

TEST_CASE("weeklyAuto: 課題の検査は手動と同じ（Issue Id の長さ・重複・5,000 件）") {
  WeeklyAuto a;
  WeeklySnapshot s = snap("2026-10-21");
  for (int i = 0; i < 5100; ++i) s.items.push_back({"I" + std::to_string(i), 't', std::nullopt, false});
  s.items.push_back({"I1", 'p', std::nullopt, false});               // 重複
  s.items.push_back({std::string(65, 'x'), 't', std::nullopt, false});  // 長すぎる
  weeklyAutoPut(a, std::move(s));
  REQUIRE(a.snaps.size() == 1);
  CHECK(a.snaps[0].items.size() == kWeeklyMaxSnapshotItems);
  CHECK(a.snaps[0].truncated);
}

TEST_CASE("weeklyAuto: 週の月曜が d 以前／d より前の一番新しいもの") {
  const WeeklyAuto a = autosOf({"2026-10-06", "2026-10-13", "2026-10-21"});  // 10/05・10/12・10/19 の週
  CHECK(iso(weeklyAutoAtOrBefore(a, D("2026-10-19"))) == "2026-10-21");  // 週の月曜 = 10/19 ≤ 10/19
  CHECK(iso(weeklyAutoAtOrBefore(a, D("2026-10-18"))) == "2026-10-13");
  CHECK(iso(weeklyAutoAtOrBefore(a, D("2026-10-04"))) == "(なし)");
  CHECK(iso(weeklyAutoBefore(a, D("2026-10-19"))) == "2026-10-13");  // 同じ週は含まない
  CHECK(iso(weeklyAutoBefore(a, D("2026-10-20"))) == "2026-10-13");
  CHECK(iso(weeklyAutoBefore(a, D("2026-10-26"))) == "2026-10-21");
  CHECK(iso(weeklyAutoBefore(a, D("2026-10-05"))) == "(なし)");
  CHECK(weeklyAutoAtOrBefore(WeeklyAuto{}, D("2026-10-19")) == nullptr);
}

// ---------------------------------------------------------------- 保存
namespace {
WeeklySnapshot snapWithItems(const char* isoDay, int n, std::size_t idLen = 0) {
  WeeklySnapshot s = snap(isoDay);
  s.tasks = n;
  for (int i = 0; i < n; ++i) {
    std::string id = "I" + std::to_string(i);
    if (idLen > id.size()) id.append(idLen - id.size(), 'x');
    s.items.push_back({id, i % 3 == 0 ? 'd' : i % 3 == 1 ? 'p' : 't', D("2026-12-25") - i % 30, i % 7 == 0});
  }
  return s;
}
}  // namespace

TEST_CASE("weeklyAuto: JSON の往復。\"weekly\" とは別のキー \"weeklyAuto\" に書く") {
  FixedClock clk(LocalTime{2026, 10, 21, 9, 0, 0});
  ProjectRecord rec;
  rec.settings.id = "p1";
  rec.settings.name = "P";
  rec.settings.baseDate = "2026-10-21";
  weeklyAutoPut(rec.settings.weeklyAuto, snapWithItems("2026-10-14", 5));
  weeklyAutoPut(rec.settings.weeklyAuto, snapWithItems("2026-10-21", 7));
  rec.settings.unknown["futureKey"] = Json{{"a", 1}};
  const Json j = settingsToJson(rec);
  REQUIRE(j.contains("weeklyAuto"));
  CHECK_FALSE(j.contains("weekly"));  // 手動が空なら "weekly" は書かない（0.9.x の読み込みに触れない）
  CHECK(j["futureKey"]["a"] == 1);
  CHECK(j["weeklyAuto"]["snaps"].size() == 2);
  const ProjectRecord back = recordFromSettingsJson(Json::parse(dumpJs(j)), "", "stem", clk);
  CHECK(back.settings.weeklyAuto == rec.settings.weeklyAuto);
  CHECK(back.settings.weekly.empty());
  CHECK(back.settings.unknown["futureKey"]["a"] == 1);
  CHECK_FALSE(back.settings.unknown.contains("weeklyAuto"));  // 既知のキーは unknown に入れない
  // 空なら書かない
  rec.settings.weeklyAuto = WeeklyAuto{};
  CHECK_FALSE(settingsToJson(rec).contains("weeklyAuto"));
  // 共有・バックアップの JSON には入れない
  rec.settings.weeklyAuto = back.settings.weeklyAuto;
  CHECK(dumpJs(recordToShareJson(rec)).find("weeklyAuto") == std::string::npos);
}

TEST_CASE("weeklyAuto: 手動の \"weekly\" は自動の有無で変わらない（0.9.x が読んで書き戻しても壊れない形）") {
  ProjectRecord rec;
  rec.settings.id = "p1";
  rec.settings.name = "P";
  rec.settings.baseDate = "2026-10-21";
  rec.settings.weekly.notes = "所感";
  weeklyPutSnapshot(rec.settings.weekly, snapWithItems("2026-10-16", 4));
  const Json without = settingsToJson(rec);
  weeklyAutoPut(rec.settings.weeklyAuto, snapWithItems("2026-10-21", 6));
  const Json with = settingsToJson(rec);
  CHECK(with["weekly"] == without["weekly"]);  // "weekly" の中は 1 バイトも変わらない
  CHECK_FALSE(with["weekly"].contains("auto"));
  CHECK_FALSE(with["weekly"].contains("kind"));
  // 自動のキーを除けば、残りの設定も同じ
  Json trimmed = with;
  trimmed.erase("weeklyAuto");
  CHECK(trimmed == without);
  // 0.9.x の読み込み（weeklyFromJson）が、自動の入った設定の "weekly" を同じに読む
  CHECK(weeklyFromJson(with["weekly"]) == rec.settings.weekly);
}

TEST_CASE("weeklyAuto: 旧形式（手動のみ）の設定を読んでも自動は空") {
  FixedClock clk(LocalTime{2026, 10, 21, 9, 0, 0});
  ProjectRecord rec;
  rec.settings.id = "p1";
  rec.settings.name = "P";
  rec.settings.baseDate = "2026-10-21";
  rec.settings.weekly.notes = "メモ";
  weeklyPutSnapshot(rec.settings.weekly, snapWithItems("2026-10-16", 4));
  const Json j = settingsToJson(rec);
  CHECK_FALSE(j.contains("weeklyAuto"));
  const ProjectRecord back = recordFromSettingsJson(j, "", "stem", clk);
  CHECK(back.settings.weekly == rec.settings.weekly);
  CHECK(back.settings.weeklyAuto.empty());
}

TEST_CASE("weeklyAuto: 壊れた値は読み飛ばし、未知のキーは壊さない") {
  const Json good = weeklyAutoToJson([] {
    WeeklyAuto a;
    weeklyAutoPut(a, snapWithItems("2026-10-14", 3));
    return a;
  }());
  const Json goodSnap = good["snaps"][0];

  // 型が違う
  for (const Json& bad : {Json("壊れた"), Json(5), Json(nullptr), Json::array(), Json{{"snaps", 3}}, Json{{"snaps", "x"}},
                           Json{{"snaps", Json::array({1, "a", nullptr, Json::array()})}}})
    CHECK(weeklyAutoFromJson(bad).empty());

  // 壊れたスナップショットだけを読み飛ばす
  Json mixed = Json{{"snaps", Json::array({goodSnap, Json{{"base", "壊れた"}}, Json{{"base", 5}}, Json{{"tasks", 3}},
                                           Json{{"base", "2026-02-30"}}, Json{{"base", "2026-10-21"}, {"tasks", -1}},
                                           Json{{"base", "2026-10-22"}, {"progress", "x"}}})}};
  WeeklyAuto a = weeklyAutoFromJson(mixed);
  REQUIRE(a.snaps.size() == 1);
  CHECK(a.snaps[0] == weeklyAutoFromJson(good).snaps[0]);

  // 壊れた項目だけを読み飛ばす（スナップショットは残る）
  Json items = goodSnap;
  items["items"] = Json::array({"I0\td\t2026-10-01\t0", 5, "壊れた", "I9\tx\t\t0", "I8\tp\t2026-13-01\t0", "I7\tp\t\t2", "I0\tp\t\t0", "I6\tt\t2026-10-05\t1"});
  a = weeklyAutoFromJson(Json{{"snaps", Json::array({items})}});
  REQUIRE(a.snaps.size() == 1);
  REQUIRE(a.snaps[0].items.size() == 2);  // 重複した I0 の 2 つ目も捨てる
  CHECK(a.snaps[0].items[0].id == "I0");
  CHECK(a.snaps[0].items[1].id == "I6");
  CHECK(a.snaps[0].items[1].late);

  // 未知のキー（自動の中にも、スナップショットの中にも）があっても読める
  Json extra = goodSnap;
  extra["future"] = Json{{"x", 1}};
  a = weeklyAutoFromJson(Json{{"snaps", Json::array({extra})}, {"futureKey", 7}});
  CHECK(a.snaps.size() == 1);

  // 同じ週が重なっていれば 1 つ、9 週あれば新しい 8 週
  Json many = Json{{"snaps", Json::array()}};
  for (int i = 0; i < 11; ++i) {
    Json s = goodSnap;
    s["base"] = fmtIso(D("2026-08-03") + i * 7);
    many["snaps"].push_back(s);
  }
  Json dup = goodSnap;
  dup["base"] = fmtIso(D("2026-08-03") + 10 * 7 + 2);  // 最後の週と同じ週
  many["snaps"].push_back(dup);
  a = weeklyAutoFromJson(many);
  REQUIRE(a.snaps.size() == kWeeklyAutoMaxSnapshots);
  CHECK(fmtIso(a.snaps.back().base) == fmtIso(D("2026-08-03") + 10 * 7 + 2));
  CHECK(fmtIso(a.snaps.front().base) == fmtIso(D("2026-08-03") + 3 * 7));
}

TEST_CASE("weeklyAuto: 設定全体の壊れた \"weeklyAuto\" は落ちず、空になる") {
  FixedClock clk(LocalTime{2026, 10, 21, 9, 0, 0});
  Json j = Json::object();
  j["id"] = "p1";
  j["name"] = "P";
  for (const Json& bad : {Json("壊れた"), Json(1), Json::array({1, 2}), Json{{"snaps", Json::array({Json{{"base", 3}}})}}}) {
    j["weeklyAuto"] = bad;
    const ProjectRecord r = recordFromSettingsJson(j, "", "stem", clk);
    CHECK(r.settings.weeklyAuto.empty());
    CHECK_FALSE(r.settings.unknown.contains("weeklyAuto"));
  }
}

TEST_CASE("weeklyAuto: 5,000 件 × 8 週でもファイルは数 MB に収まる") {
  ProjectRecord rec;
  rec.settings.id = "p1";
  rec.settings.name = "P";
  rec.settings.baseDate = "2026-10-21";
  // 実際の Issue Id（"DEV-1234" ほどの長さ）
  for (int w = 0; w < 8; ++w) weeklyAutoPut(rec.settings.weeklyAuto, snapWithItems(fmtIso(D("2026-09-07") + w * 7).c_str(), 5000, 8));
  REQUIRE(rec.settings.weeklyAuto.snaps.size() == 8);
  for (const WeeklySnapshot& s : rec.settings.weeklyAuto.snaps) REQUIRE(s.items.size() == 5000);
  const std::string normal = dumpJs(settingsToJson(rec));
  MESSAGE("5,000 件 × 8 週（Id 8 バイト）: " << normal.size() << " バイト");
  CHECK(normal.size() < 2'500'000);
  // 上限いっぱい（Id 64 バイト）
  ProjectRecord big = rec;
  big.settings.weeklyAuto = WeeklyAuto{};
  for (int w = 0; w < 8; ++w) weeklyAutoPut(big.settings.weeklyAuto, snapWithItems(fmtIso(D("2026-09-07") + w * 7).c_str(), 5000, 64));
  const std::string worst = dumpJs(settingsToJson(big));
  MESSAGE("5,000 件 × 8 週（Id 64 バイト）: " << worst.size() << " バイト");
  CHECK(worst.size() < 6'000'000);
  // 読み戻せる
  FixedClock clk(LocalTime{2026, 10, 21, 9, 0, 0});
  const ProjectRecord back = recordFromSettingsJson(Json::parse(normal), "", "stem", clk);
  CHECK(back.settings.weeklyAuto == rec.settings.weeklyAuto);
}

// ---------------------------------------------------------------- 比べる元の選び方
TEST_CASE("weeklyChooseCompare: 今週は今週の頭の自動。なければ今週より前の手動・自動のうち新しいほう（同じ日なら自動）") {
  const char* base = "2026-10-21";  // 水。今週 10/19〜
  const WeeklyRange tw = rangeOf(WeeklyPeriod::ThisWeek, base);
  // 今週の頭がある → それといま
  {
    const WeeklyAuto a = autosOf({"2026-10-13", "2026-10-21"});
    const WeeklyCompare c = weeklyChooseCompare(keep(manualOf({"2026-10-16"})), a, D(base), WeeklyPeriod::ThisWeek, tw);
    REQUIRE(c);
    CHECK(c.fromAuto);
    CHECK(iso(c.from) == "2026-10-21");
    CHECK(c.to == nullptr);
  }
  // 今週の頭がない: 手動（10/16）のほうが新しい
  {
    const WeeklyCompare c = weeklyChooseCompare(keep(manualOf({"2026-10-16"})), keep(autosOf({"2026-10-14"})), D(base), WeeklyPeriod::ThisWeek, tw);
    REQUIRE(c);
    CHECK_FALSE(c.fromAuto);
    CHECK(iso(c.from) == "2026-10-16");
    CHECK(c.to == nullptr);
  }
  // 自動（10/16）のほうが新しい
  {
    const WeeklyCompare c = weeklyChooseCompare(keep(manualOf({"2026-10-14"})), keep(autosOf({"2026-10-16"})), D(base), WeeklyPeriod::ThisWeek, tw);
    REQUIRE(c);
    CHECK(c.fromAuto);
    CHECK(iso(c.from) == "2026-10-16");
  }
  // 同じ日なら自動
  {
    const WeeklyCompare c = weeklyChooseCompare(keep(manualOf({"2026-10-16"})), keep(autosOf({"2026-10-16"})), D(base), WeeklyPeriod::ThisWeek, tw);
    REQUIRE(c);
    CHECK(c.fromAuto);
  }
  // 手動だけ（従来の動き）: 今週より前の一番新しいものと、いま
  {
    const WeeklyCompare c = weeklyChooseCompare(keep(manualOf({"2026-10-09", "2026-10-16"})), WeeklyAuto{}, D(base), WeeklyPeriod::ThisWeek, tw);
    REQUIRE(c);
    CHECK_FALSE(c.fromAuto);
    CHECK(iso(c.from) == "2026-10-16");
  }
  // 今週に手動で書き出したものだけ → 元なし（従来どおり）
  CHECK_FALSE(weeklyChooseCompare(keep(manualOf({"2026-10-20"})), WeeklyAuto{}, D(base), WeeklyPeriod::ThisWeek, tw));
  // 何もない
  CHECK_FALSE(weeklyChooseCompare(WeeklyMemo{}, WeeklyAuto{}, D(base), WeeklyPeriod::ThisWeek, tw));
  // 基準日が日曜なら同じ週の頭。月曜なら次の週（前の週のものは「今週の頭」ではなく、前のもの）
  {
    const WeeklyCompare sun = weeklyChooseCompare(WeeklyMemo{}, keep(autosOf({"2026-10-21"})), D("2026-10-25"), WeeklyPeriod::ThisWeek,
                                                  rangeOf(WeeklyPeriod::ThisWeek, "2026-10-25"));
    REQUIRE(sun);
    CHECK(iso(sun.from) == "2026-10-21");
    CHECK(sun.to == nullptr);
    const WeeklyCompare mon = weeklyChooseCompare(WeeklyMemo{}, keep(autosOf({"2026-10-21", "2026-10-26"})), D("2026-10-26"), WeeklyPeriod::ThisWeek,
                                                  rangeOf(WeeklyPeriod::ThisWeek, "2026-10-26"));
    REQUIRE(mon);
    CHECK(iso(mon.from) == "2026-10-26");
  }
}

TEST_CASE("weeklyChooseCompare: 先週は先週の頭と今週の頭。先週の頭がなければ、より前で一番近い自動") {
  const char* base = "2026-10-21";
  const WeeklyRange lw = rangeOf(WeeklyPeriod::LastWeek, base);
  // 先週の頭と今週の頭
  {
    const WeeklyAuto a = autosOf({"2026-10-06", "2026-10-13", "2026-10-21"});
    const WeeklyCompare c = weeklyChooseCompare(keep(manualOf({"2026-10-16"})), a, D(base), WeeklyPeriod::LastWeek, lw);
    REQUIRE(c);
    CHECK(c.fromAuto);
    CHECK(iso(c.from) == "2026-10-13");
    CHECK(iso(c.to) == "2026-10-21");
  }
  // 先週の頭がない → より前で一番近い自動（10/06）。今週の頭はある
  {
    const WeeklyCompare c = weeklyChooseCompare(WeeklyMemo{}, keep(autosOf({"2026-10-06", "2026-10-21"})), D(base), WeeklyPeriod::LastWeek, lw);
    REQUIRE(c);
    CHECK(iso(c.from) == "2026-10-06");
    CHECK(iso(c.to) == "2026-10-21");
  }
  // 今週の頭がない → 終わりは「いま」
  {
    const WeeklyCompare c = weeklyChooseCompare(WeeklyMemo{}, keep(autosOf({"2026-10-13"})), D(base), WeeklyPeriod::LastWeek, lw);
    REQUIRE(c);
    CHECK(iso(c.from) == "2026-10-13");
    CHECK(c.to == nullptr);
  }
  // 先週の頭以前の自動がない（今週の頭だけ）→ 手動があれば従来どおり、なければ元なし
  CHECK_FALSE(weeklyChooseCompare(WeeklyMemo{}, keep(autosOf({"2026-10-21"})), D(base), WeeklyPeriod::LastWeek, lw));
  {
    const WeeklyCompare c = weeklyChooseCompare(keep(manualOf({"2026-10-16"})), keep(autosOf({"2026-10-21"})), D(base), WeeklyPeriod::LastWeek, lw);
    REQUIRE(c);
    CHECK_FALSE(c.fromAuto);
    CHECK(iso(c.from) == "2026-10-16");
    CHECK(c.to == nullptr);
  }
  // 手動だけ（従来の動き）
  {
    const WeeklyCompare c = weeklyChooseCompare(keep(manualOf({"2026-10-16"})), WeeklyAuto{}, D(base), WeeklyPeriod::LastWeek, lw);
    REQUIRE(c);
    CHECK_FALSE(c.fromAuto);
    CHECK(c.to == nullptr);
  }
  CHECK_FALSE(weeklyChooseCompare(WeeklyMemo{}, WeeklyAuto{}, D(base), WeeklyPeriod::LastWeek, lw));
}

TEST_CASE("weeklyChooseCompare: 任意の範囲は、始まり以前と終わり以前で一番新しい自動（終わりが今週に届けば、いま）") {
  const char* base = "2026-10-21";
  const WeeklyAuto a = autosOf({"2026-10-06", "2026-10-13", "2026-10-21"});  // 10/05・10/12・10/19 の週
  const auto choose = [&](const char* from, const char* to, const WeeklyMemo& m = keep(WeeklyMemo{}), const WeeklyAuto* autos = nullptr) {
    return weeklyChooseCompare(m, autos ? *autos : a, D(base), WeeklyPeriod::Custom, rangeOf(WeeklyPeriod::Custom, base, from, to));
  };
  {
    const WeeklyCompare c = choose("2026-10-05", "2026-10-14");  // 10/05 の週の頭 → 10/12 の週の頭
    REQUIRE(c);
    CHECK(iso(c.from) == "2026-10-06");
    CHECK(iso(c.to) == "2026-10-13");
  }
  {
    const WeeklyCompare c = choose("2026-10-14", "2026-10-27");  // 終わりが今週より先 → いま
    REQUIRE(c);
    CHECK(iso(c.from) == "2026-10-13");
    CHECK(c.to == nullptr);
  }
  {
    const WeeklyCompare c = choose("2026-10-14", "2026-10-21");  // 終わりが今週にある → いま
    REQUIRE(c);
    CHECK(c.to == nullptr);
  }
  {
    const WeeklyCompare c = choose("2026-10-06", "2026-10-18");  // 終わりは日曜 → 10/12 の週の頭
    REQUIRE(c);
    CHECK(iso(c.from) == "2026-10-06");
    CHECK(iso(c.to) == "2026-10-13");
  }
  // 逆順の指定は weeklyRange が入れ替える
  {
    const WeeklyCompare c = choose("2026-10-14", "2026-10-05");
    REQUIRE(c);
    CHECK(iso(c.from) == "2026-10-06");
    CHECK(iso(c.to) == "2026-10-13");
  }
  // 始まりと終わりが同じスナップショット → 元なし（何も変わっていないとは言えない）
  CHECK_FALSE(choose("2026-10-13", "2026-10-14"));
  // 始まり以前の自動がない → 手動があれば従来どおり、なければ元なし
  const WeeklyAuto late = autosOf({"2026-10-21"});
  CHECK_FALSE(choose("2026-10-05", "2026-10-11", {}, &late));
  {
    const WeeklyCompare c = choose("2026-10-05", "2026-10-11", keep(manualOf({"2026-10-16"})), &late);
    REQUIRE(c);
    CHECK_FALSE(c.fromAuto);
    CHECK(c.to == nullptr);
  }
}

// ---------------------------------------------------------------- レポートへの反映
namespace {

struct Spec {
  std::string id, name, who, status = "To Do", parent, type = "Task", resolved, earliest, deadline;
  double weeks = 1;
  std::vector<std::string> blocks;
};

Issue mk(const Spec& s) {
  Issue o;
  o.id = s.id;
  o.key = "P-" + s.id;
  o.type = s.type;
  o.summary = s.name;
  o.parent = s.parent;
  o.status = s.status;
  o.assignee = s.who;
  if (!s.resolved.empty()) o.resolved = parseIso(s.resolved);
  if (!s.earliest.empty()) o.notBefore = parseIso(s.earliest);
  if (!s.deadline.empty()) o.deadline = parseIso(s.deadline);
  o.durW = s.weeks;
  o.blocks = s.blocks;
  return o;
}

struct Proj {
  std::vector<Issue> issues;
  ProjectMeta meta;
  Computed comp;
  Calendar cal = Calendar::fromText(defaultHolidaysText());
  Day base = D("2026-10-21");
  ProjectStats stats;
  LoadReport load;
  static Proj make(const std::vector<Spec>& specs) {
    Proj p;
    p.meta.hasDeadlineCol = true;
    for (const Spec& s : specs) p.issues.push_back(mk(s));
    ScheduleOptions opt;
    opt.base = p.base;
    auto c = computeAll(p.issues, p.meta, p.cal, opt);
    REQUIRE(c.ok());
    p.comp = std::move(c).value();
    p.comp.issues = p.issues;
    p.stats = computeStats(p.comp.T, p.issues, p.comp.R, p.base);
    p.load = computeLoad(p.comp.T, p.issues, p.comp.R, p.cal, p.base, ResourceLimit{});
    return p;
  }
  WeeklyInput input() const {
    WeeklyInput in(comp.T, issues, comp.R, cal, stats, load);
    in.base = base;
    in.projectName = "テスト";
    return in;
  }
  WeeklySnapshot snapshotAt(const char* isoDay) const {
    WeeklySnapshot s = makeWeeklySnapshot(input());
    s.base = D(isoDay);
    return s;
  }
};

bool has(const std::string& s, const std::string& needle) { return s.find(needle) != std::string::npos; }

std::vector<std::string> namesOf(const Proj& p, const WeeklyList& l) {
  std::vector<std::string> v;
  for (const WeeklyItem& it : l.items) v.push_back(p.comp.T.name[static_cast<std::size_t>(it.index)]);
  return v;
}

// 先々週の末の状態
std::vector<Spec> stateA() {
  std::vector<Spec> v;
  v.push_back({"1", "全体", "", "To Do", "", "Epic", "", "", "2026-12-25", 1, {}});
  v.push_back({"2", "X", "田中", "To Do", "1", "Task", "", "", "", 1, {}});
  v.push_back({"3", "Y", "田中", "To Do", "1", "Task", "", "", "", 1, {}});
  v.push_back({"4", "Q", "鈴木", "To Do", "1", "Task", "", "", "", 1, {}});  // この後、削除する
  return v;
}
// 先週の頭（= 前の週の終わり）。X が完了、S が増えた
std::vector<Spec> stateB() {
  std::vector<Spec> v = stateA();
  v[1].status = "Done";
  v[1].resolved = "2026-10-14";
  v.push_back({"5", "S", "鈴木", "To Do", "1", "Task", "", "", "", 1, {}});
  return v;
}
// 今週の頭: Y が完了、Z が増えた
std::vector<Spec> stateC() {
  std::vector<Spec> v = stateB();
  v[2].status = "Done";
  v[2].resolved = "2026-10-20";
  v.push_back({"6", "Z", "田中", "To Do", "1", "Task", "", "", "", 1, {}});
  return v;
}
// いま: S が完了し、Q は削除済み、W が増えた
std::vector<Spec> stateNow() {
  std::vector<Spec> v = stateC();
  v.erase(v.begin() + 3);  // Q を削除
  v[3].status = "Done";   // S
  v[3].resolved = "2026-10-21";
  v.push_back({"7", "W", "鈴木", "To Do", "1", "Task", "", "", "", 1, {}});
  return v;
}

WeeklyReport reportWith(const Proj& p, const WeeklyMemo& m, const WeeklyAuto& a, WeeklyPeriod period = WeeklyPeriod::ThisWeek,
                        Day from = 0, Day to = 0) {
  WeeklyInput in = p.input();
  in.memo = &m;
  in.autos = &a;
  WeeklyOptions o;
  o.period = period;
  o.from = from;
  o.to = to;
  return buildWeeklyReport(in, o);
}

}  // namespace

TEST_CASE("weekly(自動): 今週は今週の頭と今を比べる。週報を一度も保存していなくても出る") {
  const Proj now = Proj::make(stateNow());
  WeeklyAuto a;
  weeklyAutoPut(a, Proj::make(stateC()).snapshotAt("2026-10-19"));
  const WeeklyReport r = reportWith(now, WeeklyMemo{}, a);
  REQUIRE(r.changes.available);
  CHECK(r.changes.since == D("2026-10-19"));
  CHECK(r.changes.sinceAuto);
  CHECK_FALSE(r.changes.hasUntil);
  CHECK(namesOf(now, r.changes.newlyDone) == std::vector<std::string>{"S"});  // 今週の頭のあとに完了
  CHECK(r.changes.added == 1);    // W
  CHECK(r.changes.removed == 1);  // Q
  CHECK(r.summary.hasPrev);
  CHECK(r.summary.prevAuto);
  CHECK(r.summary.doneDelta == 1);
  const std::string md = weeklyMarkdown(r);
  CHECK(has(md, "## 先週から変わったこと"));
  CHECK(has(md, "自動で記録した"));
  CHECK(has(md, "### 新しく完了した課題（1 件）"));
  CHECK(has(md, "増えた作業: 1 件 ／ 無くなった作業: 1 件"));
  CHECK(has(weeklyHtml(r), "<h2>先週から変わったこと</h2>"));
  CHECK(has(weeklyHtml(r), "自動で記録した"));
}

TEST_CASE("weekly(自動): 先週は先週の頭と今週の頭を比べる（いまの値ではない）") {
  const Proj now = Proj::make(stateNow());
  WeeklyAuto a;
  weeklyAutoPut(a, Proj::make(stateA()).snapshotAt("2026-10-06"));
  weeklyAutoPut(a, Proj::make(stateB()).snapshotAt("2026-10-13"));   // 先週の頭
  weeklyAutoPut(a, Proj::make(stateC()).snapshotAt("2026-10-19"));   // 今週の頭
  const WeeklyReport r = reportWith(now, WeeklyMemo{}, a, WeeklyPeriod::LastWeek);
  REQUIRE(r.changes.available);
  CHECK(r.changes.since == D("2026-10-13"));
  CHECK(r.changes.sinceAuto);
  CHECK(r.changes.hasUntil);
  CHECK(r.changes.until == D("2026-10-19"));
  // 先週の頭 → 今週の頭で Y が完了し、Z が増えた。いまの完了（S）や増えた W は入らない
  CHECK(namesOf(now, r.changes.newlyDone) == std::vector<std::string>{"Y"});
  CHECK(r.changes.added == 1);    // Z
  CHECK(r.changes.removed == 0);  // Q は両方にある（いまは削除済みでも、この間には消えていない）
  CHECK_FALSE(r.summary.hasPrev);  // いまとの増減はサマリーに出さない
  const std::string md = weeklyMarkdown(r);
  CHECK(has(md, "## 先週から変わったこと"));
  CHECK(has(md, "2026/10/13(火)"));
  CHECK(has(md, "2026/10/19(月)"));
  CHECK(has(md, "いまの値です"));
  CHECK_FALSE(has(md, "（先週から "));
  CHECK(has(weeklyHtml(r), "<h2>先週から変わったこと</h2>"));
}

TEST_CASE("weekly(自動): 先週の頭がなければ、より前で一番近い自動と今週の頭を比べる") {
  const Proj now = Proj::make(stateNow());
  WeeklyAuto a;
  weeklyAutoPut(a, Proj::make(stateA()).snapshotAt("2026-10-06"));
  weeklyAutoPut(a, Proj::make(stateC()).snapshotAt("2026-10-19"));
  const WeeklyReport r = reportWith(now, WeeklyMemo{}, a, WeeklyPeriod::LastWeek);
  REQUIRE(r.changes.available);
  CHECK(r.changes.since == D("2026-10-06"));
  CHECK(r.changes.until == D("2026-10-19"));
  const auto done = namesOf(now, r.changes.newlyDone);
  CHECK(done == std::vector<std::string>{"X", "Y"});  // A → C で X と Y が完了
}

TEST_CASE("weekly(自動): 任意の範囲") {
  const Proj now = Proj::make(stateNow());
  WeeklyAuto a;
  weeklyAutoPut(a, Proj::make(stateA()).snapshotAt("2026-10-06"));
  weeklyAutoPut(a, Proj::make(stateB()).snapshotAt("2026-10-13"));
  weeklyAutoPut(a, Proj::make(stateC()).snapshotAt("2026-10-19"));
  // 10/05〜10/14: A → B
  WeeklyReport r = reportWith(now, WeeklyMemo{}, a, WeeklyPeriod::Custom, D("2026-10-05"), D("2026-10-14"));
  REQUIRE(r.changes.available);
  CHECK(r.changes.since == D("2026-10-06"));
  CHECK(r.changes.until == D("2026-10-13"));
  CHECK(namesOf(now, r.changes.newlyDone) == std::vector<std::string>{"X"});
  // 10/12〜来週: B → いま
  r = reportWith(now, WeeklyMemo{}, a, WeeklyPeriod::Custom, D("2026-10-12"), D("2026-10-28"));
  REQUIRE(r.changes.available);
  CHECK(r.changes.since == D("2026-10-13"));
  CHECK_FALSE(r.changes.hasUntil);
  CHECK(r.summary.hasPrev);
  // 範囲の始まり以前の自動がない → 元なし（セクションが出ない）
  r = reportWith(now, WeeklyMemo{}, a, WeeklyPeriod::Custom, D("2026-09-01"), D("2026-09-30"));
  CHECK_FALSE(r.changes.available);
  CHECK_FALSE(has(weeklyMarkdown(r), "先週から変わったこと"));
}

TEST_CASE("weekly(自動): 手動だけのときは従来どおり（前回の週報との比較）。自動があれば自動を優先する") {
  const Proj now = Proj::make(stateNow());
  WeeklyMemo manual;
  weeklyPutSnapshot(manual, Proj::make(stateB()).snapshotAt("2026-10-16"));
  {
    const WeeklyReport r = reportWith(now, manual, WeeklyAuto{});
    REQUIRE(r.changes.available);
    CHECK(r.changes.since == D("2026-10-16"));
    CHECK_FALSE(r.changes.sinceAuto);
    CHECK_FALSE(r.changes.hasUntil);
    CHECK(r.summary.hasPrev);
    CHECK_FALSE(r.summary.prevAuto);
    CHECK(has(weeklyMarkdown(r), "に書き出したときの状態"));
    CHECK_FALSE(has(weeklyMarkdown(r), "自動で記録した"));
    // 先週にしても同じ（従来は期間によらず前回と比べる）
    const WeeklyReport rl = reportWith(now, manual, WeeklyAuto{}, WeeklyPeriod::LastWeek);
    REQUIRE(rl.changes.available);
    CHECK(rl.changes.since == D("2026-10-16"));
  }
  {
    WeeklyAuto a;
    weeklyAutoPut(a, Proj::make(stateC()).snapshotAt("2026-10-19"));
    const WeeklyReport r = reportWith(now, manual, a);
    REQUIRE(r.changes.available);
    CHECK(r.changes.sinceAuto);
    CHECK(r.changes.since == D("2026-10-19"));
  }
  // 元がどちらにもない
  CHECK_FALSE(reportWith(now, WeeklyMemo{}, WeeklyAuto{}).changes.available);
  // 従来の WeeklyInput::prev の直接指定はそのまま使える
  {
    WeeklyInput in = now.input();
    const WeeklySnapshot s = Proj::make(stateB()).snapshotAt("2026-10-16");
    in.prev = &s;
    const WeeklyReport r = buildWeeklyReport(in, {});
    REQUIRE(r.changes.available);
    CHECK(r.changes.since == D("2026-10-16"));
    CHECK_FALSE(r.changes.sinceAuto);
  }
}

TEST_CASE("weekly(自動): 終わりがスナップショットのとき、完了日は保存した値を使う（あとで完了日を変えても先週の週報は変わらない）") {
  // 先週の頭（B）→ 今週の頭（C）で、Y が 10/21、Q が 10/20 に完了した（保存された完了日）
  std::vector<Spec> b = stateB();
  std::vector<Spec> c = stateB();
  c[2].status = "Done";  // Y
  c[2].resolved = "2026-10-21";
  c[3].status = "Done";  // Q
  c[3].resolved = "2026-10-20";
  // いま: 完了日をあとで書き換えた（Y は 10/19、Q は 10/22。順番も逆になる）
  std::vector<Spec> n = c;
  n[2].resolved = "2026-10-19";
  n[3].resolved = "2026-10-22";
  const Proj now = Proj::make(n);
  WeeklyAuto a;
  weeklyAutoPut(a, Proj::make(b).snapshotAt("2026-10-13"));
  weeklyAutoPut(a, Proj::make(c).snapshotAt("2026-10-19"));
  WeeklyOptions o;
  o.period = WeeklyPeriod::LastWeek;
  o.sections.summary = o.sections.done = o.sections.progress = o.sections.next = o.sections.risks = false;
  o.sections.milestones = o.sections.assignees = o.sections.notes = false;
  WeeklyInput in = now.input();
  const WeeklyMemo none;
  in.memo = &none;
  in.autos = &a;
  const WeeklyReport r = buildWeeklyReport(in, o);
  REQUIRE(r.changes.available);
  REQUIRE(r.changes.hasUntil);
  REQUIRE(r.changes.newlyDone.items.size() == 2);
  // 並べ替えも保存した完了日の順（Q 10/20 → Y 10/21）
  CHECK(namesOf(now, r.changes.newlyDone) == std::vector<std::string>{"Q", "Y"});
  CHECK(r.changes.newlyDone.items[0].resolved == D("2026-10-20"));
  CHECK(r.changes.newlyDone.items[1].resolved == D("2026-10-21"));
  const std::string md = weeklyMarkdown(r);
  CHECK(has(md, "完了 10/20(火)"));
  CHECK(has(md, "完了 10/21(水)"));
  CHECK_FALSE(has(md, "完了 10/19(月)"));
  CHECK_FALSE(has(md, "10/22(木)"));
  CHECK(md.find("Q") < md.find("Y"));
  const std::string html = weeklyHtml(r);
  CHECK(has(html, "10/20(火)"));
  CHECK(has(html, "10/21(水)"));
  CHECK_FALSE(has(html, ">10/19(月)<"));
  CHECK(has(html, ">10/20(火)<"));
  CHECK_FALSE(has(html, "10/22(木)"));
  // 終わりが「いま」のときは、いまの完了日（従来どおり）
  WeeklyAuto head;
  weeklyAutoPut(head, Proj::make(b).snapshotAt("2026-10-19"));
  WeeklyInput in2 = now.input();
  in2.memo = &none;
  in2.autos = &head;
  WeeklyOptions cur = o;
  cur.period = WeeklyPeriod::ThisWeek;
  const WeeklyReport rn = buildWeeklyReport(in2, cur);
  REQUIRE(rn.changes.newlyDone.items.size() == 2);
  CHECK(rn.changes.newlyDone.items[0].resolved == D("2026-10-19"));
  CHECK(rn.changes.newlyDone.items[1].resolved == D("2026-10-22"));
}

TEST_CASE("weekly(自動): 比べる終わりが一部だけのスナップショットなら、増減は数えない") {
  const Proj now = Proj::make(stateNow());
  WeeklyAuto a;
  weeklyAutoPut(a, Proj::make(stateB()).snapshotAt("2026-10-13"));
  WeeklySnapshot c = Proj::make(stateC()).snapshotAt("2026-10-19");
  c.truncated = true;
  weeklyAutoPut(a, c);
  const WeeklyReport r = reportWith(now, WeeklyMemo{}, a, WeeklyPeriod::LastWeek);
  REQUIRE(r.changes.available);
  CHECK(r.changes.truncated);
  CHECK(r.changes.added == 0);
  CHECK(r.changes.removed == 0);
}

// ---------------------------------------------------------------- Session（開いたときの自動取得）
namespace {
const char* const kCsv =
    "Issue Id,Issue Type,Summary,Parent,Assignee,Status,Priority,Resolved,Duration (weeks)\n"
    "1,Epic,全体,,,,High,,\n"
    "2,Task,A,1,田中,Done,High,2026-10-13,1\n"
    "3,Task,B,1,田中,In Progress,Highest,,2\n"
    "4,Task,C,1,鈴木,To Do,Medium,,2\n"
    "5,Task,D,1,,To Do,Low,,1\n"
    "6,Milestone,完了,1,,,High,,0\n"
    "7,Task,単独,,鈴木,Done,Medium,2026-10-14,1\n";

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

const WeeklySnapItem* itemOf(const WeeklySnapshot& s, const char* id) {
  for (const WeeklySnapItem& it : s.items)
    if (it.id == id) return &it;
  return nullptr;
}
}  // namespace

TEST_CASE("weeklyAuto: Session — 開いたとき、その週のものがなければ取り、同じ週は何もしない") {
  SessionFix f;
  CHECK(f.s->weeklyAuto().empty());
  const std::uint64_t v0 = f.s->version();
  const auto r1 = f.s->recordWeeklyAuto(WeeklyAutoMode::IfMissing);
  REQUIRE(r1.ok());
  CHECK(r1.value() == WeeklyAutoResult::Recorded);
  REQUIRE(f.s->weeklyAuto().snaps.size() == 1);
  const WeeklySnapshot& s = f.s->weeklyAuto().snaps[0];
  CHECK(s.base == D("2026-10-21"));  // 取った日（IClock の今日）
  CHECK(s.items.size() == 5);        // 作業だけ（親とマイルストーンは入らない）
  CHECK(s.tasks == 5);
  REQUIRE(itemOf(s, "2"));
  CHECK(itemOf(s, "2")->st == 'd');
  REQUIRE(itemOf(s, "3"));
  CHECK(itemOf(s, "3")->st == 'p');
  REQUIRE(itemOf(s, "4"));
  CHECK(itemOf(s, "4")->st == 't');
  CHECK(itemOf(s, "1") == nullptr);
  CHECK(itemOf(s, "6") == nullptr);
  CHECK(f.s->version() > v0);
  CHECK(f.s->weekly().empty());  // 手動のほうには入らない
  // 同じ週にもう一度 → 何もしない（保存も予約しない）
  const std::uint64_t v1 = f.s->version();
  const WeeklyAuto before = f.s->weeklyAuto();
  const auto r2 = f.s->recordWeeklyAuto(WeeklyAutoMode::IfMissing);
  REQUIRE(r2.ok());
  CHECK(r2.value() == WeeklyAutoResult::AlreadyHave);
  CHECK(f.s->version() == v1);
  CHECK(f.s->weeklyAuto() == before);
  // 同じ週の別の日でも何もしない（週は月曜始まり）
  f.clock.set(LocalTime{2026, 10, 25, 23, 0, 0});  // 日曜
  CHECK(f.s->recordWeeklyAuto(WeeklyAutoMode::IfMissing).value() == WeeklyAutoResult::AlreadyHave);
  f.clock.set(LocalTime{2026, 10, 19, 0, 0, 0});  // 月曜
  CHECK(f.s->recordWeeklyAuto(WeeklyAutoMode::IfMissing).value() == WeeklyAutoResult::AlreadyHave);
  CHECK(f.s->weeklyAuto() == before);
  // 週が変われば追加
  f.clock.set(LocalTime{2026, 10, 28, 9, 30, 0});
  const auto r3 = f.s->recordWeeklyAuto(WeeklyAutoMode::IfMissing);
  REQUIRE(r3.ok());
  CHECK(r3.value() == WeeklyAutoResult::Recorded);
  REQUIRE(f.s->weeklyAuto().snaps.size() == 2);
  CHECK(f.s->weeklyAuto().snaps[1].base == D("2026-10-28"));
  CHECK(f.s->weeklyAuto().snaps[0] == before.snaps[0]);  // 前の週のものは変わらない
}

TEST_CASE("weeklyAuto: Session — 作業が 0 件のプロジェクトには足さない") {
  FixedClock clock(LocalTime{2026, 10, 21, 9, 30, 0});
  ProjectData d;
  d.settings.baseDate = "2026-10-19";
  d.settings.holidays = defaultHolidaysText();
  Session s(d, &clock);
  REQUIRE(s.error().empty());
  const std::uint64_t v0 = s.version();
  const auto a = s.recordWeeklyAuto(WeeklyAutoMode::IfMissing);
  REQUIRE(a.ok());
  CHECK(a.value() == WeeklyAutoResult::Skipped);
  CHECK(s.weeklyAuto().empty());
  CHECK(s.version() == v0);
}

TEST_CASE("weeklyAuto: Session — 取り直し（Replace）は、変わらなければ保存を予約しない。Undo の対象外") {
  SessionFix f;
  REQUIRE(f.s->recordWeeklyAuto(WeeklyAutoMode::IfMissing).ok());
  const std::uint64_t v0 = f.s->version();
  const auto same = f.s->recordWeeklyAuto(WeeklyAutoMode::Replace);
  REQUIRE(same.ok());
  CHECK(same.value() == WeeklyAutoResult::Unchanged);
  CHECK(f.s->version() == v0);
  REQUIRE(f.s->setField("4", [](Issue& o) { o.status = "Done"; o.resolved = parseIso("2026-10-21"); }));
  const auto rep = f.s->recordWeeklyAuto(WeeklyAutoMode::Replace);
  REQUIRE(rep.ok());
  CHECK(rep.value() == WeeklyAutoResult::Replaced);
  REQUIRE(f.s->weeklyAuto().snaps.size() == 1);
  CHECK(itemOf(f.s->weeklyAuto().snaps[0], "4")->st == 'd');
  // Undo は課題の編集だけを戻し、スナップショットには触れない
  CHECK(f.s->undo());
  REQUIRE(f.s->weeklyAuto().snaps.size() == 1);
  CHECK(itemOf(f.s->weeklyAuto().snaps[0], "4")->st == 'd');
}

TEST_CASE("weeklyAuto: Session — 手動（週報の保存）は自動を置き換えず、自動は手動を置き換えない") {
  SessionFix f;
  REQUIRE(f.s->recordWeeklyAuto(WeeklyAutoMode::IfMissing).ok());
  const WeeklyAuto autoBefore = f.s->weeklyAuto();
  // 手動の保存（同じ週のスナップショット）
  WeeklySnapshot m = snap("2026-10-21");
  m.tasks = 99;
  f.s->putWeeklySnapshot(m);
  REQUIRE(f.s->weekly().snaps.size() == 1);
  CHECK(f.s->weekly().snaps[0].tasks == 99);
  CHECK(f.s->weeklyAuto() == autoBefore);  // 自動はそのまま
  // 週が変わって自動を取っても、手動は変わらない
  f.clock.set(LocalTime{2026, 10, 28, 9, 30, 0});
  REQUIRE(f.s->recordWeeklyAuto(WeeklyAutoMode::IfMissing).ok());
  REQUIRE(f.s->weekly().snaps.size() == 1);
  CHECK(f.s->weekly().snaps[0].tasks == 99);
  CHECK(f.s->weeklyAuto().snaps.size() == 2);
}

TEST_CASE("weeklyAuto: Session — 8 週を超えたら古いものから落とす。年またぎ") {
  SessionFix f;
  for (int i = 0; i < 10; ++i) {
    const Day d = D("2026-10-19") + i * 7;
    const std::string t = fmtIso(d);
    f.clock.set(LocalTime{std::stoi(t.substr(0, 4)), std::stoi(t.substr(5, 2)), std::stoi(t.substr(8, 2)), 9, 0, 0});
    const auto r = f.s->recordWeeklyAuto(WeeklyAutoMode::IfMissing);
    REQUIRE(r.ok());
    CHECK(r.value() == WeeklyAutoResult::Recorded);
  }
  REQUIRE(f.s->weeklyAuto().snaps.size() == kWeeklyAutoMaxSnapshots);
  CHECK(f.s->weeklyAuto().snaps.front().base == D("2026-10-19") + 2 * 7);
  CHECK(f.s->weeklyAuto().snaps.back().base == D("2026-10-19") + 9 * 7);  // 12/21。年内
  // 年またぎ: 12/31 に取れば、12/28 の週。1/1 は同じ週なので何もしない。1/4 は次の週
  SessionFix g;
  g.clock.set(LocalTime{2026, 12, 31, 9, 0, 0});
  CHECK(g.s->recordWeeklyAuto(WeeklyAutoMode::IfMissing).value() == WeeklyAutoResult::Recorded);
  g.clock.set(LocalTime{2027, 1, 1, 9, 0, 0});
  CHECK(g.s->recordWeeklyAuto(WeeklyAutoMode::IfMissing).value() == WeeklyAutoResult::AlreadyHave);
  g.clock.set(LocalTime{2027, 1, 4, 9, 0, 0});
  CHECK(g.s->recordWeeklyAuto(WeeklyAutoMode::IfMissing).value() == WeeklyAutoResult::Recorded);
  CHECK(g.s->weeklyAuto().snaps.size() == 2);
}

TEST_CASE("weeklyAuto: Session — 設定に書き出して読み直すと残る") {
  SessionFix f;
  REQUIRE(f.s->recordWeeklyAuto(WeeklyAutoMode::IfMissing).ok());
  const ProjectRecord rec = recordOf(f.s->data());
  const Json j = settingsToJson(rec);
  REQUIRE(j.contains("weeklyAuto"));
  CHECK_FALSE(j.contains("weekly"));
  const ProjectRecord back = recordFromSettingsJson(Json::parse(dumpJs(j)), rec.csv, "x", f.clock);
  CHECK(weeklyAutoToJson(back.settings.weeklyAuto) == weeklyAutoToJson(f.s->weeklyAuto()));  // 進みは小数 4 桁に丸めて保存する
  CHECK(back.settings.weeklyAuto.snaps.size() == 1);
  CHECK(back.settings.weeklyAuto.snaps[0].items == f.s->weeklyAuto().snaps[0].items);
  // 件名・担当者名は設定に入らない（Issue Id・状況・日付・遅れだけ）
  const std::string text = dumpJs(j["weeklyAuto"]);
  CHECK(text.find("田中") == std::string::npos);
  CHECK(text.find("単独") == std::string::npos);
}

// ---------------------------------------------------------------- 未知のキーの保持
TEST_CASE("weeklyAuto: 未知のキー（weeklyAuto の直下と各スナップショットの中）は、読み込み→書き出しで消えない") {
  FixedClock clk(LocalTime{2026, 10, 21, 9, 0, 0});
  Json snapJson = weeklyAutoToJson([] {
    WeeklyAuto a;
    weeklyAutoPut(a, snapWithItems("2026-10-14", 3));
    weeklyAutoPut(a, snapWithItems("2026-10-21", 2));
    return a;
  }())["snaps"][0];
  Json snap2 = snapJson;
  snap2["base"] = "2026-10-21";
  snapJson["future"] = Json{{"x", 1}, {"y", Json::array({1, 2})}};
  snap2["other"] = "文字";
  Json auto_ = Json{{"snaps", Json::array({snapJson, snap2})}, {"futureKey", Json{{"a", 1}}}, {"flag", true}};
  Json settings = Json::object();
  settings["id"] = "p1";
  settings["name"] = "P";
  settings["weeklyAuto"] = auto_;
  const ProjectRecord rec = recordFromSettingsJson(Json::parse(dumpJs(settings)), "", "stem", clk);
  REQUIRE(rec.settings.weeklyAuto.snaps.size() == 2);
  // 読み込めるだけでなく、書き出しても値が残る
  const Json out = Json::parse(dumpJs(settingsToJson(rec)));
  REQUIRE(out.contains("weeklyAuto"));
  CHECK(out["weeklyAuto"]["futureKey"] == Json{{"a", 1}});
  CHECK(out["weeklyAuto"]["flag"] == true);
  REQUIRE(out["weeklyAuto"]["snaps"].size() == 2);
  CHECK(out["weeklyAuto"]["snaps"][0]["future"] == Json{{"x", 1}, {"y", Json::array({1, 2})}});
  CHECK(out["weeklyAuto"]["snaps"][1]["other"] == "文字");
  CHECK_FALSE(out["weeklyAuto"]["snaps"][1].contains("future"));
  // もう一度往復しても同じ
  const ProjectRecord rec2 = recordFromSettingsJson(out, "", "stem", clk);
  CHECK(settingsToJson(rec2)["weeklyAuto"] == out["weeklyAuto"]);
  // 既知のキーは未知のキーとして二重に書かない
  CHECK(out["weeklyAuto"]["snaps"][0]["base"] == "2026-10-14");
  CHECK(out["weeklyAuto"]["snaps"][0]["items"].size() == 3);
  // 壊れたスナップショットが混じっても、残りと weeklyAuto 直下の未知のキーは残る
  settings["weeklyAuto"] = Json{{"snaps", Json::array({Json{{"base", 5}, {"future", 1}}, snapJson})}, {"futureKey", 7}};
  const ProjectRecord rec3 = recordFromSettingsJson(settings, "", "stem", clk);
  const Json out3 = settingsToJson(rec3);
  CHECK(out3["weeklyAuto"]["futureKey"] == 7);
  REQUIRE(out3["weeklyAuto"]["snaps"].size() == 1);
  CHECK(out3["weeklyAuto"]["snaps"][0]["future"]["x"] == 1);
  // スナップショットが 1 つもなくても、未知のキーだけは書き戻す
  settings["weeklyAuto"] = Json{{"snaps", Json::array()}, {"futureKey", 7}};
  const ProjectRecord rec4 = recordFromSettingsJson(settings, "", "stem", clk);
  CHECK(rec4.settings.weeklyAuto.snaps.empty());
  CHECK(settingsToJson(rec4)["weeklyAuto"]["futureKey"] == 7);
}

TEST_CASE("weeklyAuto: 同じ週を取り直しても、そのスナップショットの未知のキーは残る") {
  SessionFix f;
  REQUIRE(f.s->recordWeeklyAuto(WeeklyAutoMode::IfMissing).ok());
  f.s->data().settings.weeklyAuto.snaps[0].extra["future"] = 5;
  REQUIRE(f.s->setField("4", [](Issue& o) { o.status = "Done"; o.resolved = parseIso("2026-10-21"); }));
  const auto rep = f.s->recordWeeklyAuto(WeeklyAutoMode::Replace);
  REQUIRE(rep.ok());
  CHECK(rep.value() == WeeklyAutoResult::Replaced);
  CHECK(f.s->weeklyAuto().snaps[0].extra["future"] == 5);
}
