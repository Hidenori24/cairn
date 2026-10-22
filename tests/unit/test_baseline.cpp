// ベースライン比較（Issue #53）: 保存・読み込みの往復、壊れた設定、5 つの上限、計画との比較
#include <doctest/doctest.h>

#include <chrono>
#include <string>
#include <vector>

#include "roadmap/baseline.hpp"
#include "roadmap/clock.hpp"
#include "roadmap/commands.hpp"
#include "roadmap/project_io.hpp"
#include "roadmap/schedule.hpp"
#include "roadmap/text.hpp"

using namespace roadmap;

namespace {

Day D(const char* iso) { return *parseIso(iso); }

struct Spec {
  std::string id, name, parent, status = "To Do", type = "Task", resolved;
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
  if (!s.resolved.empty()) o.resolved = parseIso(s.resolved);
  o.durW = s.weeks;
  o.blocks = s.blocks;
  return o;
}

// 基準日 2026-10-19（月）。10 月の第 3〜5 週には休業日がない
struct Proj {
  std::vector<Issue> issues;
  ProjectMeta meta;
  Computed comp;
  Calendar cal = Calendar::fromText(defaultHolidaysText());
  Day base = D("2026-10-19");

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
    return p;
  }
  // issues を書き換えたあとに、日程を計算し直す
  void rebuild() {
    ScheduleOptions opt;
    opt.base = base;
    auto c = computeAll(issues, meta, cal, opt);
    REQUIRE(c.ok());
    comp = std::move(c).value();
    comp.issues = issues;
  }
  Baseline capture(const char* name = "キックオフ", const char* at = "2026-10-19T09:30") const {
    auto b = captureBaseline(issues, comp, name, at);
    REQUIRE(b.ok());
    return b.value();
  }
  BaselineCompare compare(const Baseline& b) const {
    auto r = compareBaseline(b, issues, comp, cal);
    REQUIRE(r.ok());
    return r.value();
  }
  const BaselineEntry& entry(const BaselineCompare& c, const char* id) const {
    const int i = comp.T.find(id);
    REQUIRE(i >= 0);
    return c.entries[static_cast<std::size_t>(i)];
  }
};

std::vector<Spec> basic() {
  std::vector<Spec> v;
  v.push_back({"1", "全体", "", "To Do", "Epic", "", 1, {}});
  v.push_back({"2", "遅れる", "1", "To Do", "Task", "", 1, {}});
  v.push_back({"3", "早まる", "1", "To Do", "Task", "", 1, {}});
  v.push_back({"4", "そのまま", "1", "To Do", "Task", "", 1, {}});
  v.push_back({"5", "消える", "1", "To Do", "Task", "", 1, {}});
  v.push_back({"7", "完了する", "1", "To Do", "Task", "", 1, {}});
  v.push_back({"9", "別の親", "", "To Do", "Epic", "", 1, {}});
  v.push_back({"8", "動かす", "1", "To Do", "Task", "", 1, {}});
  return v;
}

Issue* find(Proj& p, const char* id) {
  for (Issue& o : p.issues)
    if (o.id == id) return &o;
  return nullptr;
}

Baseline sample(const char* id, const char* name) {
  Baseline b;
  b.id = id;
  b.name = name;
  b.at = "2026-10-19T09:30";
  BaselineItem a;
  a.id = "10";
  a.st = 'p';
  a.start = D("2026-10-19");
  a.finish = D("2026-10-23");
  a.deadline = D("2026-10-30");
  a.title = "設計";
  BaselineItem c;
  c.id = "11";
  c.parent = true;
  c.start = D("2026-10-19");
  c.finish = D("2026-10-30");
  c.title = "まとめ";
  b.items = {a, c};
  return b;
}

}  // namespace

TEST_CASE("baseline: 名前の下ごしらえ（制御文字・向きを変える文字を除き、40 文字まで）") {
  CHECK(baselineSanitizeName("キックオフ") == "キックオフ");
  CHECK(baselineSanitizeName("  前後の空白  ") == "前後の空白");
  CHECK(baselineSanitizeName(std::string("A\x01") + "B\tC\nD\x7F" + "E") == "ABCDE");
  CHECK(baselineSanitizeName("a\xE2\x80\xAE" "b\xE2\x81\xA6" "c") == "abc");  // U+202E, U+2066
  CHECK(baselineSanitizeName("\x01\x02") == "");
  CHECK(baselineSanitizeName(std::string("ok\xFF") + "x") == "ok\xEF\xBF\xBD" "x");  // 壊れた UTF-8 は U+FFFD
  std::string longName;
  for (int i = 0; i < 100; ++i) longName += "あ";
  const std::string cut = baselineSanitizeName(longName);
  CHECK(text::codepointCount(cut) == kBaselineMaxNameChars);
}

TEST_CASE("baseline: 保存した課題の写し（日付・状況・期限）") {
  auto specs = basic();
  specs[1].status = "In Progress";
  specs.push_back({"20", "マイルストーン", "1", "To Do", "Milestone", "", 0, {}});
  Proj p = Proj::make(specs);
  find(p, "3")->deadline = D("2026-11-06");  // 期限は変えても写しは変わらない（キャプチャ後）
  const Baseline b = p.capture();
  CHECK(b.name == "キックオフ");
  CHECK(b.at == "2026-10-19T09:30");
  CHECK(b.id.empty());  // id は baselineAdd が振る
  CHECK(b.items.size() == p.issues.size());
  const BaselineItem* two = nullptr;
  const BaselineItem* one = nullptr;
  for (const BaselineItem& it : b.items) {
    if (it.id == "2") two = &it;
    if (it.id == "1") one = &it;
  }
  REQUIRE(two);
  REQUIRE(one);
  CHECK(two->st == 'p');
  CHECK(two->start == D("2026-10-19"));
  CHECK(two->finish == D("2026-10-23"));
  CHECK(!two->deadline);
  CHECK(!two->parent);
  CHECK(two->title == "遅れる");
  CHECK(one->parent);
  CHECK(one->finish == D("2026-10-23"));  // 親のまとめ
}

TEST_CASE("baseline: 設定への書き出しと読み込みの往復") {
  BaselineSet set;
  const auto a = baselineAdd(set, sample("", "キックオフ"));
  const auto b = baselineAdd(set, sample("", "v1.0 合意"));
  REQUIRE(a.ok());
  REQUIRE(b.ok());
  CHECK(a.value() != b.value());
  CHECK(baselineSetShown(set, b.value()));
  const Json j = baselinesToJson(set);
  const BaselineSet back = baselinesFromJson(j);
  CHECK(back == set);
  CHECK(back.list.size() == 2);
  CHECK(back.shown == b.value());
  CHECK(back.list[0].items[0].deadline == D("2026-10-30"));
  CHECK(!back.list[0].items[1].deadline);
  CHECK(back.list[0].items[1].parent);
  // テキストにしても同じ
  CHECK(baselinesFromJson(Json::parse(j.dump())) == set);
}

TEST_CASE("baseline: project.settings.json に入り、共有・バックアップには入らず、未知のキーを壊さない") {
  FixedClock clock(LocalTime{2026, 10, 19, 9, 30, 0});
  ProjectRecord rec = newRecord("P", "P", "", "2026-10-19", clock);
  BaselineSet set;
  REQUIRE(baselineAdd(set, sample("", "キックオフ")).ok());
  rec.settings.baselines = set;
  rec.settings.unknown["futureKey"] = Json{{"x", 1}};
  const Json j = settingsToJson(rec);
  REQUIRE(j.contains("baselines"));
  CHECK(j["futureKey"]["x"] == 1);
  const ProjectRecord back = recordFromSettingsJson(j, "", "stem", clock);
  CHECK(back.settings.baselines == set);
  CHECK(back.settings.unknown["futureKey"]["x"] == 1);
  // 空なら書かない
  rec.settings.baselines = BaselineSet{};
  CHECK(!settingsToJson(rec).contains("baselines"));
  // 共有・バックアップの JSON には入れない（件名がファイルに残るため）
  rec.settings.baselines = set;
  const std::string share = exportShareJson({rec}, {}, clock);
  CHECK(share.find("baselines") == std::string::npos);
  CHECK(share.find("設計") == std::string::npos);
}

TEST_CASE("baseline: 壊れた設定は読み飛ばす（落ちない）") {
  const auto good = [] {
    BaselineSet s;
    REQUIRE(baselineAdd(s, sample("", "正常")).ok());
    return baselinesToJson(s);
  };
  // 型が違う
  CHECK(baselinesFromJson(Json(5)).list.empty());
  CHECK(baselinesFromJson(Json("x")).list.empty());
  CHECK(baselinesFromJson(Json::array()).list.empty());
  CHECK(baselinesFromJson(Json{{"list", 3}}).list.empty());
  CHECK(baselinesFromJson(Json{{"list", Json::array({1, "a", nullptr})}}).list.empty());
  {
    Json j = good();
    j["list"].push_back(Json{{"id", 5}, {"name", "idが数値"}, {"at", ""}, {"items", Json::array()}});
    j["list"].push_back(Json{{"id", "bx"}, {"name", 7}, {"at", ""}, {"items", Json::array()}});
    j["list"].push_back(Json{{"id", "by"}, {"name", "itemsが配列でない"}, {"at", ""}, {"items", "x"}});
    j["list"].push_back(Json{{"id", "bz"}, {"name", "atが壊れている"}, {"at", "2026-13-40T99:99"}, {"items", Json::array()}});
    j["list"].push_back(Json{{"id", ""}, {"name", "idが空"}, {"at", ""}, {"items", Json::array()}});
    j["list"].push_back(Json{{"id", "a b"}, {"name", "idに空白"}, {"at", ""}, {"items", Json::array()}});
    const BaselineSet s = baselinesFromJson(j);
    CHECK(s.list.size() == 1);
    CHECK(s.list[0].name == "正常");
  }
  // id の重複: 先のものを残す
  {
    Json j = good();
    Json dup = j["list"][0];
    dup["name"] = "重複";
    j["list"].push_back(dup);
    const BaselineSet s = baselinesFromJson(j);
    REQUIRE(s.list.size() == 1);
    CHECK(s.list[0].name == "正常");
  }
  // 課題の壊れた行だけ読み飛ばす（型・日付・状況・Issue Id・重複・欄の数）
  {
    Json j = good();
    Json& items = j["list"][0]["items"];
    items = Json::array();
    items.push_back("1\tp\t0\t2026-10-19\t2026-10-23\t\t正常");
    items.push_back(5);
    items.push_back("2\tp\t0\t2026-13-01\t2026-10-23\t\t日付が壊れている");
    items.push_back("3\tp\t0\t2026-10-19\t2026-02-30\t\t存在しない日付");
    items.push_back("4\tp\t0\t2026-10-19\t2026-10-23\tabc\t期限が壊れている");
    items.push_back("5\tx\t0\t2026-10-19\t2026-10-23\t\t状況が壊れている");
    items.push_back("\tp\t0\t2026-10-19\t2026-10-23\t\tidが空");
    items.push_back(std::string(65, 'a') + "\tp\t0\t2026-10-19\t2026-10-23\t\tidが長い");
    items.push_back("1\td\t0\t2026-10-20\t2026-10-24\t\t重複");
    items.push_back("6\tp\t0\t2026-10-19");
    items.push_back("7\tp\t2\t2026-10-19\t2026-10-23\t\t親の印が壊れている");
    items.push_back("8\tt\t1\t\t\t\t日付なしでもよい");
    const BaselineSet s = baselinesFromJson(j);
    REQUIRE(s.list.size() == 1);
    const auto& it = s.list[0].items;
    REQUIRE(it.size() == 2);
    CHECK(it[0].id == "1");
    CHECK(it[0].st == 'p');
    CHECK(it[0].title == "正常");
    CHECK(it[1].id == "8");
    CHECK(!it[1].start);
    CHECK(!it[1].finish);
    CHECK(it[1].parent);
  }
  // 件数超過: 5 つまで、課題は 5,000 件まで（truncated を立てる）
  {
    Json j = good();
    for (int i = 0; i < 9; ++i) {
      Json x = j["list"][0];
      x["id"] = "n" + std::to_string(i);
      j["list"].push_back(x);
    }
    CHECK(baselinesFromJson(j).list.size() == kBaselineMax);
    Json k = good();
    Json& items = k["list"][0]["items"];
    items = Json::array();
    for (std::size_t i = 0; i < kBaselineMaxItems + 20; ++i) items.push_back(std::to_string(i) + "\tt\t0\t2026-10-19\t2026-10-23\t\t");
    const BaselineSet s = baselinesFromJson(k);
    REQUIRE(s.list.size() == 1);
    CHECK(s.list[0].items.size() == kBaselineMaxItems);
    CHECK(s.list[0].truncated);
  }
  // 名前の制御文字・長さ・空
  {
    Json j = good();
    j["list"][0]["name"] = std::string("A\x01") + "\xE2\x80\xAE" + "B";
    CHECK(baselinesFromJson(j).list[0].name == "AB");
    j["list"][0]["name"] = "\x01\x02";
    const BaselineSet s = baselinesFromJson(j);
    REQUIRE(s.list.size() == 1);
    CHECK(!s.list[0].name.empty());  // 空の名前は既定の名前にする（データは捨てない）
    std::string longName;
    for (int i = 0; i < 80; ++i) longName += "あ";
    j["list"][0]["name"] = longName;
    CHECK(text::codepointCount(baselinesFromJson(j).list[0].name) == kBaselineMaxNameChars);
  }
  // shown: 存在しない id は捨てる
  {
    Json j = good();
    j["shown"] = "nothing";
    CHECK(baselinesFromJson(j).shown.empty());
    j["shown"] = 5;
    CHECK(baselinesFromJson(j).shown.empty());
    j["shown"] = j["list"][0]["id"];
    CHECK(baselinesFromJson(j).shown == j["list"][0]["id"].get<std::string>());
  }
}

TEST_CASE("baseline: 保存は 5 つまで") {
  BaselineSet set;
  for (std::size_t i = 0; i < kBaselineMax; ++i) {
    const auto r = baselineAdd(set, sample("", ("b" + std::to_string(i)).c_str()));
    REQUIRE(r.ok());
  }
  CHECK(set.list.size() == kBaselineMax);
  const auto over = baselineAdd(set, sample("", "6 つ目"));
  CHECK(!over.ok());
  CHECK(over.error().find("5") != std::string::npos);  // 理由に上限を出す
  CHECK(set.list.size() == kBaselineMax);
  // 削除すれば保存できる。id は重ならない
  const std::string victim = set.list[2].id;
  CHECK(baselineRemove(set, victim));
  CHECK(!baselineRemove(set, victim));
  const auto again = baselineAdd(set, sample("", "6 つ目"));
  REQUIRE(again.ok());
  for (std::size_t i = 0; i < set.list.size(); ++i)
    for (std::size_t k = i + 1; k < set.list.size(); ++k) CHECK(set.list[i].id != set.list[k].id);
}

TEST_CASE("baseline: 名前の変更・削除・表示するものの選択") {
  BaselineSet set;
  const std::string a = baselineAdd(set, sample("", "A")).value();
  const std::string b = baselineAdd(set, sample("", "B")).value();
  CHECK(baselineRename(set, a, "  合意 \x01 版 ").ok());
  CHECK(baselineFind(set, a)->name == "合意  版");
  CHECK(!baselineRename(set, a, "\x01").ok());  // 空になる名前は断る
  CHECK(!baselineRename(set, "zzz", "x").ok());
  CHECK(baselineSetShown(set, a));
  CHECK(baselineShown(set) == baselineFind(set, a));
  CHECK(!baselineSetShown(set, "zzz"));
  CHECK(baselineShown(set) == baselineFind(set, a));
  CHECK(baselineSetShown(set, ""));
  CHECK(baselineShown(set) == nullptr);
  baselineSetShown(set, b);
  CHECK(baselineRemove(set, b));
  CHECK(baselineShown(set) == nullptr);  // 表示中のものを消したら表示も外れる
  CHECK(set.shown.empty());
}

TEST_CASE("baseline: 比較（遅れ・早まり・計画どおり・追加・削除・完了・階層の移動）") {
  Proj p = Proj::make(basic());
  const Baseline b = p.capture();

  // いまの見込みを変える
  find(p, "2")->durW = 2.0;               // 10/23 → 10/30（+5 営業日）
  find(p, "3")->durW = 0.6;               // 10/23 → 10/21（-2 営業日）
  find(p, "7")->status = "Done";          // 完了。完了日は 10/21（-2 営業日）
  find(p, "7")->resolved = D("2026-10-21");
  find(p, "8")->parent = "9";             // 階層を移動（日付はそのまま）
  p.issues.erase(std::remove_if(p.issues.begin(), p.issues.end(), [](const Issue& o) { return o.id == "5"; }), p.issues.end());
  p.issues.push_back(mk({"6", "新しい", "1", "To Do", "Task", "", 1, {}}));
  p.rebuild();

  const BaselineCompare c = p.compare(b);
  REQUIRE(c.entries.size() == p.issues.size());

  const BaselineEntry& late = p.entry(c, "2");
  CHECK(late.state == BaselineState::Late);
  CHECK(late.hasFinishDiff);
  CHECK(late.finishDiff == 5);
  CHECK(late.planFinish == D("2026-10-23"));
  CHECK(late.nowFinish == D("2026-10-30"));
  CHECK(late.startDiff == 0);
  CHECK(late.finishDiffers);

  const BaselineEntry& early = p.entry(c, "3");
  CHECK(early.state == BaselineState::Early);
  CHECK(early.finishDiff == -2);

  const BaselineEntry& same = p.entry(c, "4");
  CHECK(same.state == BaselineState::OnPlan);
  CHECK(same.finishDiff == 0);
  CHECK(!same.finishDiffers);

  const BaselineEntry& added = p.entry(c, "6");
  CHECK(added.state == BaselineState::Added);
  CHECK(!added.hasFinishDiff);

  const BaselineEntry& done = p.entry(c, "7");
  CHECK(done.done);
  CHECK(done.finishDiff == -2);  // 完了した課題も、実際の完了日で早い・遅いを見る
  CHECK(done.state == BaselineState::Early);

  const BaselineEntry& moved = p.entry(c, "8");
  CHECK(moved.state == BaselineState::OnPlan);  // 親が変わっても Issue Id で突き合わせる
  CHECK(moved.finishDiff == 0);

  // 親のまとめ（遅れた子がいる）
  const BaselineEntry& par = p.entry(c, "1");
  CHECK(par.state == BaselineState::Late);
  CHECK(par.finishDiff == 5);

  // 消えた課題
  REQUIRE(c.removed.size() == 1);
  CHECK(c.removed[0].id == "5");
  CHECK(c.removed[0].state == BaselineState::Removed);
  CHECK(c.removed[0].index == -1);
  CHECK(c.removed[0].title == "消える");  // 件名は保存しておいたものを出す

  // 集計は末端の課題だけを数える（親のまとめは二重に数えない）
  CHECK(c.sum.late == 1);
  CHECK(c.sum.early == 2);   // 3 と 7
  CHECK(c.sum.onPlan == 2);  // 4 と 8
  CHECK(c.sum.added == 1);
  CHECK(c.sum.removed == 1);
  CHECK(c.sum.doneNow == 1);
  CHECK(c.baselineName == "キックオフ");

  // 遅れの大きい順の上位
  REQUIRE(c.topLate.size() == 1);
  CHECK(c.topLate[0] == p.comp.T.find("2"));
  CHECK(c.lateTotal == 1);
}

TEST_CASE("baseline: 遅れの上位は大きい順で 10 件まで（同じ大きさは表の順）") {
  std::vector<Spec> specs;
  specs.push_back({"1", "全体", "", "To Do", "Epic", "", 1, {}});
  for (int i = 0; i < 14; ++i) specs.push_back({std::to_string(10 + i), "t" + std::to_string(i), "1", "To Do", "Task", "", 1, {}});
  Proj p = Proj::make(specs);
  const Baseline b = p.capture();
  // 1 番目と 2 番目だけ大きく遅らせ、残りは 1 日ずつ遅らせる
  for (Issue& o : p.issues) {
    if (o.id == "10") o.durW = 3.0;
    else if (o.id == "11") o.durW = 2.0;
    else if (o.id != "1") o.durW = 1.2;  // 6 営業日（+1）
  }
  p.rebuild();
  Proj& q = p;
  const BaselineCompare c = q.compare(b);
  CHECK(c.lateTotal == 14);
  REQUIRE(c.topLate.size() == kBaselineTopN);
  CHECK(c.topLate[0] == q.comp.T.find("10"));  // +10
  CHECK(c.topLate[1] == q.comp.T.find("11"));  // +5
  CHECK(c.topLate[2] == q.comp.T.find("12"));  // +1 の中で表の順
  CHECK(c.topLate[3] == q.comp.T.find("13"));
  CHECK(q.entry(c, "10").finishDiff == 10);
  CHECK(q.entry(c, "11").finishDiff == 5);
  CHECK(q.entry(c, "12").finishDiff == 1);
}

TEST_CASE("baseline: 営業日の差が 0 でも日付が違うとき、区別して返す") {
  Proj p = Proj::make(basic());
  Baseline b = p.capture();
  // 計画の完了日を土曜日にした（手で書き換えた設定）: 金曜の見込みとの営業日の差は 0
  for (BaselineItem& it : b.items)
    if (it.id == "4") it.finish = D("2026-10-24");
  const BaselineCompare c = p.compare(b);
  const BaselineEntry& e = p.entry(c, "4");
  CHECK(e.finishDiff == 0);
  CHECK(e.finishDiffers);
  CHECK(e.state == BaselineState::OnPlan);
  CHECK(c.sum.sameDayDiffers == 1);
  // 計画が月曜、見込みが金曜: 営業日の差は 4
  for (BaselineItem& it : b.items)
    if (it.id == "4") it.finish = D("2026-10-19");
  const BaselineCompare c2 = p.compare(b);
  CHECK(p.entry(c2, "4").finishDiff == 4);
  CHECK(p.entry(c2, "4").state == BaselineState::Late);
}

TEST_CASE("baseline: 計画に日付がない課題は比較しない") {
  Proj p = Proj::make(basic());
  Baseline b = p.capture();
  for (BaselineItem& it : b.items)
    if (it.id == "4") it.start = it.finish = std::nullopt;
  const BaselineCompare c = p.compare(b);
  const BaselineEntry& e = p.entry(c, "4");
  CHECK(e.state == BaselineState::NoDate);
  CHECK(!e.hasFinishDiff);
  CHECK(!e.hasStartDiff);
  CHECK(c.sum.noDate == 1);
}

TEST_CASE("baseline: 休業日をまたぐ営業日の差") {
  // 11/3（文化の日）は休業日。10/30（金）→ 11/4（水）は 11/2 と 11/4 の 2 営業日
  Calendar cal = Calendar::fromText(defaultHolidaysText());
  CHECK(baselineWorkdayDiff(cal, D("2026-10-30"), D("2026-11-04")) == 2);
  CHECK(baselineWorkdayDiff(cal, D("2026-11-04"), D("2026-10-30")) == -2);
  CHECK(baselineWorkdayDiff(cal, D("2026-10-23"), D("2026-10-23")) == 0);
  CHECK(baselineWorkdayDiff(cal, D("2026-10-23"), D("2026-10-24")) == 0);  // 金 → 土
  CHECK(baselineWorkdayDiff(cal, D("2026-10-23"), D("2026-10-26")) == 1);  // 金 → 月
  CHECK(baselineWorkdayDiff(cal, D("1969-12-26"), D("1970-01-05")) == 6);  // 負の日付でも
  // 長い期間でも速い
  CHECK(baselineWorkdayDiff(cal, D("2026-01-01"), D("2126-01-01")) > 2000);
}

TEST_CASE("baseline: 表示の文言") {
  BaselineEntry e;
  e.state = BaselineState::Late;
  e.hasFinishDiff = true;
  e.finishDiff = 3;
  CHECK(baselineDiffText(e) == "+3日");
  CHECK(baselineDiffMark(e) == "!");
  e.state = BaselineState::Early;
  e.finishDiff = -2;
  CHECK(baselineDiffText(e) == "-2日");
  CHECK(baselineDiffMark(e).empty());
  e.state = BaselineState::OnPlan;
  e.finishDiff = 0;
  CHECK(baselineDiffText(e) == "±0日");
  e.finishDiffers = true;
  CHECK(baselineDiffText(e) == "±0日");
  CHECK(baselineDiffNote(e).find("日付") != std::string::npos);  // 休業日・土日をまたぐ日付の違い
  e.finishDiffers = false;
  CHECK(baselineDiffNote(e).empty());
  e.state = BaselineState::Added;
  e.hasFinishDiff = false;
  CHECK(baselineDiffText(e) == "新規");
  CHECK(baselineColumnValue(e) == "計画になかった");
  e.state = BaselineState::NoDate;
  CHECK(baselineDiffText(e) == "-");
  CHECK(baselineColumnValue(e) == "比較なし");
  CHECK(std::string(baselineStateText(BaselineState::Removed)) == "消えた");
}

TEST_CASE("baseline: Session から保存・選択・削除し、再読み込みで残る") {
  FixedClock clock(LocalTime{2026, 10, 19, 9, 30, 0});
  ProjectData d;
  d.meta.hasDeadlineCol = true;
  d.settings.baseDate = "2026-10-19";
  d.settings.holidays = defaultHolidaysText();
  for (const Spec& s : basic()) d.issues.push_back(mk(s));
  Session s(d, &clock);
  REQUIRE(s.error().empty());
  CHECK(s.baselineCompare() == nullptr);
  const auto r = s.addBaseline("キックオフ");
  REQUIRE(r.ok());
  CHECK(s.baselines().list.size() == 1);
  REQUIRE(s.shownBaseline() != nullptr);  // 保存したものをそのまま表示する
  CHECK(s.shownBaseline()->name == "キックオフ");
  CHECK(s.shownBaseline()->at == "2026-10-19T09:30");
  const std::uint64_t v0 = s.version();
  CHECK(s.setField("2", [](Issue& o) { o.durW = 2.0; }));
  CHECK(s.version() > v0);
  const BaselineCompare* c = s.baselineCompare();
  REQUIRE(c != nullptr);
  CHECK(c->sum.late == 1);
  // 元に戻しても（Undo）ベースラインは消えない
  CHECK(s.undo());
  REQUIRE(s.baselineCompare() != nullptr);
  CHECK(s.baselineCompare()->sum.late == 0);
  CHECK(s.baselines().list.size() == 1);
  // 設定に書き出し → 読み直し
  ProjectRecord rec = recordOf(s.data());
  const Json j = settingsToJson(rec);
  const ProjectRecord back = recordFromSettingsJson(j, rec.csv, "x", clock);
  CHECK(back.settings.baselines == s.baselines());
  // 6 つ目は断る
  for (int i = 0; i < 4; ++i) CHECK(s.addBaseline("n" + std::to_string(i)).ok());
  const auto over = s.addBaseline("6");
  CHECK(!over.ok());
  CHECK(s.baselines().list.size() == kBaselineMax);
  // 削除
  const std::string id = s.baselines().list[0].id;
  CHECK(s.removeBaseline(id));
  CHECK(s.baselines().list.size() == kBaselineMax - 1);
  // 表示を外す
  CHECK(s.showBaseline(""));
  CHECK(s.baselineCompare() == nullptr);
}

TEST_CASE("baseline: 5,000 件でも重くならない") {
  std::vector<Spec> specs;
  for (int g = 0; g < 10; ++g) specs.push_back({"g" + std::to_string(g), "g", "", "To Do", "Epic", "", 1, {}});
  for (int i = 0; i < 4990; ++i)
    specs.push_back({std::to_string(i), "課題" + std::to_string(i), "g" + std::to_string(i % 10), i % 7 == 0 ? "Done" : "To Do", "Task",
                     i % 7 == 0 ? "2026-10-20" : "", 1.0 + static_cast<double>(i % 5), {}});
  Proj p = Proj::make(specs);
  const Baseline b = p.capture();
  CHECK(b.items.size() == 5000);
  CHECK(!b.truncated);
  for (Issue& o : p.issues)
    if (o.durW) *o.durW += 1;
  p.rebuild();
  Proj& q = p;
  const auto t1 = std::chrono::steady_clock::now();
  BaselineSet set;
  REQUIRE(baselineAdd(set, b).ok());
  const Json j = baselinesToJson(set);
  const BaselineSet back = baselinesFromJson(Json::parse(j.dump()));
  REQUIRE(back.list.size() == 1);
  CHECK(back.list[0].items.size() == 5000);
  const BaselineCompare c = q.compare(back.list[0]);
  const auto t2 = std::chrono::steady_clock::now();
  CHECK(c.entries.size() == 5000);
  CHECK(c.sum.late > 0);
  const auto ms = [](auto a, auto b2) { return std::chrono::duration_cast<std::chrono::milliseconds>(b2 - a).count(); };
  // 計算（スケジュールの再計算）を除いた、保存・読み込み・比較の時間
  CHECK(ms(t1, t2) < 3000);
  // 5,001 件目以降は持たない
  specs.push_back({"extra", "余分", "g0", "To Do", "Task", "", 1, {}});
  Proj big = Proj::make(specs);
  const Baseline bb = big.capture();
  CHECK(bb.items.size() == kBaselineMaxItems);
  CHECK(bb.truncated);
}

TEST_CASE("baseline: 一覧の要素の id をそのまま渡して削除しても、表示の解除と維持が正しい（参照が erase で変わらない）") {
  // 表示中のものを、一覧の要素への参照で削除すると、表示が解除される
  {
    BaselineSet set;
    const std::string a = baselineAdd(set, sample("", "A")).value();
    const std::string b = baselineAdd(set, sample("", "B")).value();
    REQUIRE(baselineSetShown(set, a));
    CHECK(baselineRemove(set, set.list[0].id));  // 参照のまま渡す
    CHECK(set.shown.empty());
    REQUIRE(set.list.size() == 1);
    CHECK(set.list[0].id == b);
  }
  // 別のものを表示中なら、先頭を消しても表示は維持される（次の要素の id に変わった参照で取り違えない）
  {
    BaselineSet set;
    const std::string a = baselineAdd(set, sample("", "A")).value();
    const std::string b = baselineAdd(set, sample("", "B")).value();
    REQUIRE(baselineSetShown(set, b));
    CHECK(baselineRemove(set, set.list[0].id));
    CHECK(set.shown == b);
    CHECK(baselineShown(set) != nullptr);
    (void)a;
  }
  // 表示中のものを消すと、その次の要素が表示になることはない
  {
    BaselineSet set;
    for (const char* n : {"A", "B", "C"}) REQUIRE(baselineAdd(set, sample("", n)).ok());
    REQUIRE(baselineSetShown(set, set.list[1].id));
    CHECK(baselineRemove(set, set.list[1].id));
    CHECK(set.shown.empty());
    CHECK(baselineShown(set) == nullptr);
    CHECK(set.list.size() == 2);
  }
}

TEST_CASE("baseline: Session から一覧の id をそのまま渡して削除しても表示が正しく外れる") {
  FixedClock clock(LocalTime{2026, 10, 19, 9, 30, 0});
  ProjectData d;
  d.meta.hasDeadlineCol = true;
  d.settings.baseDate = "2026-10-19";
  d.settings.holidays = defaultHolidaysText();
  for (const Spec& s : basic()) d.issues.push_back(mk(s));
  Session s(d, &clock);
  REQUIRE(s.addBaseline("A").ok());
  const auto second = s.addBaseline("B");  // 表示は B
  REQUIRE(second.ok());
  CHECK(s.removeBaseline(s.baselines().list[0].id));
  REQUIRE(s.shownBaseline() != nullptr);
  CHECK(s.shownBaseline()->name == "B");
  CHECK(s.removeBaseline(s.baselines().list[0].id));  // 表示中の B
  CHECK(s.shownBaseline() == nullptr);
  CHECK(s.baselines().shown.empty());
  CHECK(s.baselineCompare() == nullptr);
}
