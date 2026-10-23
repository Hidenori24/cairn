// 週次レポート（Issue #43）: 期間・各リストの抽出・Markdown と HTML のエスケープ・スナップショットと比較
#include <doctest/doctest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "roadmap/baseline.hpp"
#include "roadmap/clock.hpp"
#include "roadmap/filestore.hpp"
#include "roadmap/load.hpp"
#include "roadmap/project_io.hpp"
#include "roadmap/report.hpp"
#include "roadmap/schedule.hpp"
#include "roadmap/stats.hpp"
#include "roadmap/text.hpp"
#include "roadmap/weekly.hpp"

using namespace roadmap;

namespace {

Day D(const char* iso) { return *parseIso(iso); }

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
  Day base = D("2026-10-21");  // 水曜
  ProjectStats stats;
  LoadReport load;
  std::string name = "テストプロジェクト";

  static Proj make(const std::vector<Spec>& specs, const char* baseIso = "2026-10-21") {
    Proj p;
    p.meta.hasDeadlineCol = true;
    for (const Spec& s : specs) p.issues.push_back(mk(s));
    p.base = D(baseIso);
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
  WeeklyInput input(const WeeklySnapshot* prev = nullptr) const {
    WeeklyInput in(comp.T, issues, comp.R, cal, stats, load);
    in.base = base;
    in.projectName = name;
    in.prev = prev;
    return in;
  }
  WeeklyReport report(const WeeklyOptions& o = {}, const WeeklySnapshot* prev = nullptr) const { return buildWeeklyReport(input(prev), o); }
  int idx(const char* id) const { return comp.T.find(id); }
  const ScheduleRow& row(const char* id) const { return comp.R[static_cast<std::size_t>(idx(id))]; }
};

// 基本の課題群（基準日 2026-10-21 水。今週 10/19〜10/25、来週 10/26〜11/1）
std::vector<Spec> basic() {
  std::vector<Spec> v;
  v.push_back({"1", "全体", "", "To Do", "", "Epic", "", "", "2026-12-25", 1, {}});
  v.push_back({"2", "A", "田中", "Done", "1", "Task", "2026-10-19", "", "", 1, {}});  // 週の最初の日（月）= 期間内
  v.push_back({"3", "B", "田中", "Done", "1", "Task", "2026-10-25", "", "", 1, {}});  // 週の最後の日（日）= 期間内
  v.push_back({"4", "C", "鈴木", "Done", "1", "Task", "2026-10-18", "", "", 1, {}});  // 前の週の日曜 = 期間外
  v.push_back({"5", "D", "鈴木", "Done", "1", "Task", "2026-10-26", "", "", 1, {}});  // 次の週の月曜 = 期間外
  v.push_back({"6", "E", "田中", "Done", "1", "Task", "", "", "", 1, {}});            // 完了日なし
  v.push_back({"7", "F", "田中", "In Progress", "1", "Task", "", "", "", 1, {}});
  v.push_back({"8", "G", "鈴木", "To Do", "1", "Task", "", "2026-10-26", "", 1, {"9"}});
  v.push_back({"9", "H", "鈴木", "To Do", "1", "Task", "", "", "2026-11-06", 2, {"11"}});
  v.push_back({"10", "U", "", "To Do", "1", "Task", "", "", "", 1, {}});
  v.push_back({"11", "リリース", "", "To Do", "1", "Milestone", "", "", "2026-11-06", 0, {}});
  v.push_back({"12", "設計完了", "", "Done", "1", "Milestone", "2026-10-22", "", "2026-10-23", 0, {}});
  v.push_back({"13", "前期", "", "Done", "1", "Milestone", "2026-10-05", "", "", 0, {}});
  return v;
}
std::vector<std::string> didsOf(const Proj& p, const WeeklyList& l) {
  std::vector<std::string> v;
  for (const WeeklyItem& it : l.items) v.push_back(p.comp.T.name[static_cast<std::size_t>(it.index)]);
  return v;
}

bool has(const std::string& s, const std::string& needle) { return s.find(needle) != std::string::npos; }

// Markdown の 1 行の中の、打ち消されていない "|" の数
int rawPipes(const std::string& line) {
  int n = 0;
  for (std::size_t i = 0; i < line.size(); ++i) {
    if (line[i] == '\\') {
      ++i;
      continue;
    }
    if (line[i] == '|') ++n;
  }
  return n;
}

// 打ち消されていない "<" の数（Markdown の生の HTML・自動リンクの元）
int rawLt(const std::string& s) {
  int n = 0;
  for (std::size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '\\') {
      ++i;
      continue;
    }
    if (s[i] == '<') ++n;
  }
  return n;
}

std::vector<std::string> lines(const std::string& s) {
  std::vector<std::string> v;
  std::size_t from = 0;
  for (;;) {
    const std::size_t p = s.find('\n', from);
    if (p == std::string::npos) {
      v.push_back(s.substr(from));
      break;
    }
    v.push_back(s.substr(from, p - from));
    from = p + 1;
  }
  return v;
}

}  // namespace

// ---------------------------------------------------------------- 期間
TEST_CASE("weekly: 期間は月曜始まり。基準日が週の途中でも、月曜〜日曜") {
  const Day wed = D("2026-10-21");
  const auto tw = weeklyRange(wed, WeeklyPeriod::ThisWeek, 0, 0);
  CHECK(tw.from == D("2026-10-19"));
  CHECK(tw.to == D("2026-10-25"));
  CHECK(dow(tw.from) == 1);
  const auto lw = weeklyRange(wed, WeeklyPeriod::LastWeek, 0, 0);
  CHECK(lw.from == D("2026-10-12"));
  CHECK(lw.to == D("2026-10-18"));
  // 月曜・日曜が基準日でも同じ週
  CHECK(weeklyRange(D("2026-10-19"), WeeklyPeriod::ThisWeek, 0, 0).from == D("2026-10-19"));
  CHECK(weeklyRange(D("2026-10-25"), WeeklyPeriod::ThisWeek, 0, 0).from == D("2026-10-19"));
  CHECK(weeklyRange(D("2026-10-25"), WeeklyPeriod::ThisWeek, 0, 0).to == D("2026-10-25"));
  CHECK(weeklyRange(D("2026-10-26"), WeeklyPeriod::ThisWeek, 0, 0).from == D("2026-10-26"));
}

TEST_CASE("weekly: 年またぎの週") {
  const auto tw = weeklyRange(D("2026-12-31"), WeeklyPeriod::ThisWeek, 0, 0);  // 木曜
  CHECK(fmtIso(tw.from) == "2026-12-28");
  CHECK(fmtIso(tw.to) == "2027-01-03");
  const auto lw = weeklyRange(D("2027-01-01"), WeeklyPeriod::LastWeek, 0, 0);  // 金曜
  CHECK(fmtIso(lw.from) == "2026-12-21");
  CHECK(fmtIso(lw.to) == "2026-12-27");
  const auto next = weeklyRange(D("2026-12-31"), WeeklyPeriod::ThisWeek, 0, 0);
  CHECK(fmtIso(next.to + 1) == "2027-01-04");
}

TEST_CASE("weekly: 任意の範囲は入れ替え・上限") {
  const Day a = D("2026-10-10"), b = D("2026-10-20");
  CHECK(weeklyRange(0, WeeklyPeriod::Custom, a, b).from == a);
  CHECK(weeklyRange(0, WeeklyPeriod::Custom, b, a).from == a);  // 逆でも入れ替える
  CHECK(weeklyRange(0, WeeklyPeriod::Custom, b, a).to == b);
  CHECK(weeklyRange(0, WeeklyPeriod::Custom, a, a).to == a);  // 1 日
  const auto big = weeklyRange(0, WeeklyPeriod::Custom, a, a + 5000);
  CHECK(big.to - big.from + 1 == kWeeklyMaxRangeDays);
  // 極端な値でも壊れない
  const auto ext = weeklyRange(0, WeeklyPeriod::Custom, -1000000000, 1000000000);
  CHECK(ext.to - ext.from + 1 == kWeeklyMaxRangeDays);
}

TEST_CASE("weekly: 営業日数は休業日を除く（年末年始・祝日）") {
  Proj p = Proj::make(basic(), "2026-11-04");  // 水。11/3（火）は休業日
  const WeeklyReport r = p.report();
  CHECK(fmtIso(r.range.from) == "2026-11-02");
  CHECK(r.rangeWorkdays == 4);  // 月・水・木・金
  CHECK(r.nextWorkdays == 5);
  Proj y = Proj::make(basic(), "2026-12-30");
  const WeeklyReport ry = y.report();
  CHECK(fmtIso(ry.range.from) == "2026-12-28");
  CHECK(ry.rangeWorkdays == 1);  // 12/28 だけ（12/29〜1/3 は休業日）
  CHECK(fmtIso(ry.next.from) == "2027-01-04");
  CHECK(ry.nextWorkdays == 5);
}

// ---------------------------------------------------------------- 各リスト
TEST_CASE("weekly: 今週の完了は期間の両端を含み、前後の日・完了日なしは入れない") {
  Proj p = Proj::make(basic());
  const WeeklyReport r = p.report();
  REQUIRE_FALSE(r.empty);
  CHECK(didsOf(p, r.done) == std::vector<std::string>{"A", "B"});  // 完了日の順
  CHECK(r.done.total == 2);
  CHECK(r.summary.doneInPeriod == 2);
  CHECK(r.summary.doneNoDate == 1);  // E
  CHECK(fmtIso(*r.done.items[0].resolved) == "2026-10-19");
  CHECK(r.done.items[0].assignee == "田中");
  // 先週
  WeeklyOptions lw;
  lw.period = WeeklyPeriod::LastWeek;
  const WeeklyReport rl = p.report(lw);
  CHECK(didsOf(p, rl.done) == std::vector<std::string>{"C"});
  // 任意の範囲（週をまたぐ）
  WeeklyOptions cu;
  cu.period = WeeklyPeriod::Custom;
  cu.from = D("2026-10-18");
  cu.to = D("2026-10-26");
  CHECK(didsOf(p, p.report(cu).done) == std::vector<std::string>{"C", "A", "B", "D"});
  // 1 日だけ
  cu.from = cu.to = D("2026-10-25");
  CHECK(didsOf(p, p.report(cu).done) == std::vector<std::string>{"B"});
}

TEST_CASE("weekly: 進行中は担当・残り営業日・余裕") {
  Proj q = Proj::make(basic());
  const WeeklyReport r = q.report();
  REQUIRE(r.progress.items.size() == 1);
  const WeeklyItem& f = r.progress.items[0];
  CHECK(f.name == "F");
  CHECK(f.assignee == "田中");
  CHECK(f.done == false);
  CHECK(f.prog == true);
  CHECK(f.remainDays == q.cal.nd(q.base, f.finish));
  CHECK(f.floatDays == q.row("7").floatDays);
  CHECK(r.progress.total == 1);
}

TEST_CASE("weekly: 来週の予定は、来週に最早の開始・完了が来る未完了の作業") {
  Proj p = Proj::make(basic());
  const WeeklyReport r = p.report();
  CHECK(fmtIso(r.next.from) == "2026-10-26");
  CHECK(fmtIso(r.next.to) == "2026-11-01");
  CHECK(didsOf(p, r.nextStart) == std::vector<std::string>{"G"});
  // 完了: F（10/27 ごろ）と G（10/30）。完了日の順
  const std::vector<std::string> fin = didsOf(p, r.nextFinish);
  CHECK(std::find(fin.begin(), fin.end(), "G") != fin.end());
  for (std::size_t i = 1; i < r.nextFinish.items.size(); ++i) CHECK(r.nextFinish.items[i - 1].finish <= r.nextFinish.items[i].finish);
  for (const WeeklyItem& it : r.nextFinish.items) {
    CHECK(it.finish >= r.next.from);
    CHECK(it.finish <= r.next.to);
    CHECK_FALSE(it.done);
  }
  // マイルストーンは入らない
  for (const WeeklyItem& it : r.nextFinish.items) CHECK(p.comp.T.kind[static_cast<std::size_t>(it.index)] == Kind::Task);
  // 先週のレポートでは、次の期間は「今週」
  WeeklyOptions lw;
  lw.period = WeeklyPeriod::LastWeek;
  const WeeklyReport rl = p.report(lw);
  CHECK(fmtIso(rl.next.from) == "2026-10-19");
  CHECK(fmtIso(rl.next.to) == "2026-10-25");
}

TEST_CASE("weekly: 遅れ・余裕小は余裕が負または 10 日以内。先行を 1 つ付ける") {
  Proj p = Proj::make(basic());
  const WeeklyReport r = p.report();
  // G と H は期限（11/6）に間に合わない
  REQUIRE(p.row("9").floatDays < 0);
  const std::vector<std::string> names = didsOf(p, r.risks);
  CHECK(std::find(names.begin(), names.end(), "H") != names.end());
  CHECK(std::find(names.begin(), names.end(), "G") != names.end());
  CHECK(std::find(names.begin(), names.end(), "F") == names.end());  // 余裕が十分
  for (std::size_t i = 1; i < r.risks.items.size(); ++i) CHECK(r.risks.items[i - 1].floatDays <= r.risks.items[i].floatDays);
  for (const WeeklyItem& it : r.risks.items) {
    if (it.name == "H") {
      CHECK(it.predIndex == p.idx("8"));  // H の開始を決めているのは G
      CHECK(it.predName == "G");
      CHECK(it.predFinish == p.row("8").asapFinish);
    }
    if (it.name == "G") CHECK(it.predIndex == -1);  // 先行なし
  }
}

TEST_CASE("weekly: 余裕 10 日は含み、11 日は含まない") {
  // 1 つの作業の期限を動かして、余裕がちょうど 10 日・11 日になる日を探す
  std::vector<Spec> v;
  v.push_back({"1", "X", "田中", "To Do", "", "Task", "", "", "", 1, {}});
  Proj base = Proj::make(v);
  const Day f = base.row("1").asapFinish;
  for (int want : {9, 10, 11, 12}) {
    v[0].deadline = fmtIso(base.cal.workday(f, want));
    Proj p = Proj::make(v);
    REQUIRE(p.row("1").floatDays == want);
    const WeeklyReport r = p.report();
    CHECK((r.risks.total == 1) == (want <= 10));
  }
  // 余裕 0（期限ちょうど）と −1（遅れ）
  v[0].deadline = fmtIso(f);
  CHECK(Proj::make(v).report().risks.total == 1);
  v[0].deadline = fmtIso(base.cal.workday(f, -1));
  const Proj late = Proj::make(v);
  REQUIRE(late.row("1").floatDays < 0);
  CHECK(late.report().risks.total == 1);
}

TEST_CASE("weekly: マイルストーンは、期間内に達成したものと未達成のもの。期限と見込みの差を持つ") {
  Proj p = Proj::make(basic());
  const WeeklyReport r = p.report();
  REQUIRE(r.milestones.size() == 2);
  // 達成（期間内）が先
  CHECK(r.milestones[0].name == "設計完了");
  CHECK(r.milestones[0].achieved);
  CHECK(fmtIso(r.milestones[0].forecast) == "2026-10-22");
  CHECK(r.milestones[0].diffDays == 1);  // 期限 10/23 より 1 営業日早い
  CHECK(r.milestones[1].name == "リリース");
  CHECK_FALSE(r.milestones[1].achieved);
  REQUIRE(r.milestones[1].deadline);
  CHECK(fmtIso(*r.milestones[1].deadline) == "2026-11-06");
  CHECK(r.milestones[1].diffDays == p.cal.floatDays(r.milestones[1].forecast, D("2026-11-06")));
  CHECK(r.milestones[1].diffDays < 0);
  // 前の期の達成（10/5）は入らない
  for (const WeeklyMilestone& m : r.milestones) CHECK(m.name != "前期");
  CHECK(r.summary.milestones == 3);
  CHECK(r.summary.msDone == 2);
}

TEST_CASE("weekly: 担当別は名前順（順位にしない）で、未割当は最後。来週の負荷を持つ") {
  Proj p = Proj::make(basic());
  const WeeklyReport r = p.report();
  REQUIRE(r.assignees.size() == 3);
  CHECK(r.assignees[0].name == "田中");
  CHECK(r.assignees[1].name == "鈴木");
  CHECK(r.assignees[2].name == "");
  CHECK(r.assignees[0].doneInPeriod == 2);
  CHECK(r.assignees[0].inProgress == 1);
  CHECK(r.assignees[0].open == 1);
  CHECK(r.assignees[1].doneInPeriod == 0);
  CHECK(r.assignees[1].open == 2);
  CHECK(r.assignees[1].late >= 1);
  CHECK(r.assignees[2].open == 1);
  // 負荷（来週）: 鈴木は G が来週に 5 日分
  CHECK(r.assignees[1].loadNextPercent == 100);
  CHECK(r.assignees[0].loadNextPercent >= 0);
  CHECK(r.assigneesTotal == 3);
}

TEST_CASE("weekly: サマリーは統計と一致する") {
  Proj p = Proj::make(basic());
  const WeeklyReport r = p.report();
  CHECK(r.summary.tasks == p.stats.tasks);
  CHECK(r.summary.done == p.stats.done);
  CHECK(r.summary.late == p.stats.late);
  CHECK(r.summary.tight == p.stats.tight);
  CHECK(r.summary.progress == doctest::Approx(p.stats.progress()));
  CHECK(r.summary.hasFinish == p.stats.hasFinish);
  CHECK(r.summary.finish == p.stats.finish);
  CHECK_FALSE(r.summary.hasPrev);
  CHECK_FALSE(r.changes.available);
}

TEST_CASE("weekly: 上限を超えたら件数だけ数える") {
  std::vector<Spec> v;
  v.push_back({"1", "全体", "", "To Do", "", "Epic", "", "", "2026-12-25", 1, {}});
  for (int i = 0; i < 30; ++i) v.push_back({std::to_string(i + 2), "T" + std::to_string(i), "田中", "Done", "1", "Task", "2026-10-20", "", "", 1, {}});
  Proj p = Proj::make(v);
  WeeklyOptions o;
  o.maxItems = 10;
  const WeeklyReport r = p.report(o);
  CHECK(r.done.items.size() == 10);
  CHECK(r.done.total == 30);
  CHECK(r.done.omitted() == 20);
  const std::string md = weeklyMarkdown(r);
  CHECK(has(md, "ほか 20 件"));
  CHECK(has(md, "（30 件）"));
}

// ---------------------------------------------------------------- 空・完了日なし
TEST_CASE("weekly: 課題がないプロジェクト") {
  Computed c;
  auto t = buildTree({}, ProjectMeta{});
  REQUIRE(t.ok());
  c.T = std::move(t).value();
  const std::vector<Issue> issues;
  const std::vector<ScheduleRow> rows;
  const Calendar cal;
  const ProjectStats st = computeStats(c.T, issues, rows, D("2026-10-21"));
  const LoadReport ld = computeLoad(c.T, issues, rows, cal, D("2026-10-21"), ResourceLimit{});
  WeeklyInput in(c.T, issues, rows, cal, st, ld);
  in.base = D("2026-10-21");
  in.projectName = "空";
  const WeeklyReport r = buildWeeklyReport(in, WeeklyOptions{});
  CHECK(r.empty);
  CHECK(r.done.items.empty());
  CHECK(r.assignees.empty());
  const std::string md = weeklyMarkdown(r);
  CHECK(has(md, "課題がありません"));
  CHECK(has(md, "# 空 週次レポート"));
  const std::string html = weeklyHtml(r);
  CHECK(has(html, "課題がありません"));
  CHECK(makeWeeklySnapshot(in).items.empty());
}

TEST_CASE("weekly: 大きさの合わない入力は空として扱い、範囲外を読まない") {
  Proj p = Proj::make(basic());
  std::vector<ScheduleRow> shortRows(p.comp.R.begin(), p.comp.R.begin() + 3);
  WeeklyInput in(p.comp.T, p.issues, shortRows, p.cal, p.stats, p.load);
  in.base = p.base;
  CHECK(buildWeeklyReport(in, WeeklyOptions{}).empty);
  std::vector<Issue> few(p.issues.begin(), p.issues.begin() + 2);
  WeeklyInput in2(p.comp.T, few, p.comp.R, p.cal, p.stats, p.load);
  CHECK(buildWeeklyReport(in2, WeeklyOptions{}).empty);
  CHECK(makeWeeklySnapshot(in2).items.empty());
}

TEST_CASE("weekly: 全部完了・未完了なし") {
  std::vector<Spec> v;
  v.push_back({"1", "全体", "", "To Do", "", "Epic", "", "", "", 1, {}});
  v.push_back({"2", "A", "田中", "Done", "1", "Task", "2026-10-20", "", "", 1, {}});
  Proj p = Proj::make(v);
  const WeeklyReport r = p.report();
  CHECK(r.summary.done == 1);
  CHECK_FALSE(r.summary.hasFinish);
  CHECK(r.progress.items.empty());
  CHECK(r.risks.items.empty());
  CHECK(r.nextStart.items.empty());
  const std::string md = weeklyMarkdown(r);
  CHECK(has(md, "未完了の作業はありません"));
  CHECK(has(md, "- なし"));
}

TEST_CASE("weekly: 担当者なし・件名が空の課題") {
  std::vector<Spec> v = basic();
  v[1].name = "";
  Proj p = Proj::make(v);
  const WeeklyReport r = p.report();
  const std::string md = weeklyMarkdown(r);
  CHECK(has(md, "担当: 未割当"));
  CHECK(weeklyHtml(r).size() > 100);
}

// ---------------------------------------------------------------- Markdown のエスケープ
TEST_CASE("weekly: escapeMarkdown は構文になる文字を打ち消す") {
  CHECK(escapeMarkdown("a|b") == "a\\|b");
  CHECK(escapeMarkdown("[x](http://e.com)") == "\\[x\\](http://e.com)");
  CHECK(escapeMarkdown("`code`") == "\\`code\\`");
  CHECK(escapeMarkdown("<script>") == "\\<script\\>");
  CHECK(escapeMarkdown("A & B") == "A \\& B");
  CHECK(escapeMarkdown("a*b*c") == "a\\*b\\*c");
  CHECK(escapeMarkdown("snake_case") == "snake\\_case");
  CHECK(escapeMarkdown("~~x~~") == "\\~\\~x\\~\\~");
  CHECK(escapeMarkdown("back\\slash") == "back\\\\slash");
  // 行頭の # - + > と、番号つきリスト
  CHECK(escapeMarkdown("# 見出し") == "\\# 見出し");
  CHECK(escapeMarkdown("- 項目") == "\\- 項目");
  CHECK(escapeMarkdown("+ 項目") == "\\+ 項目");
  CHECK(escapeMarkdown("> 引用") == "\\> 引用");
  CHECK(escapeMarkdown("1. 手順") == "1\\. 手順");
  CHECK(escapeMarkdown("12) 手順") == "12\\) 手順");
  CHECK(escapeMarkdown("1. ") == "1\\.");  // 空白は前後を落とす
  CHECK(escapeMarkdown("1.2.3 件名") == "1.2.3 件名");  // 記号のあとが空白でなければ番号つきリストではない
  CHECK(escapeMarkdown("v1.2 は対象") == "v1.2 は対象");  // 行頭が数字ではない
  CHECK(escapeMarkdown("2026年") == "2026年");
  CHECK(escapeMarkdown("a-b") == "a-b");  // 行頭以外の - はそのまま
  CHECK(escapeMarkdown("C# 移行") == "C# 移行");
  CHECK(escapeMarkdown("見出し #") == "見出し \\#");  // 末尾の # は閉じ記号になりうる
  // 改行・タブ・制御文字
  CHECK(escapeMarkdown("a\nb\r\nc\td") == "a b  c d");
  CHECK(escapeMarkdown(std::string("a\0b", 3)) == "ab");
  CHECK(escapeMarkdown("a\xE2\x80\xAE" "b") == "ab");  // 向きを入れ替える制御文字
  CHECK(escapeMarkdown("a\xFF" "b") == "a\xEF\xBF\xBD" "b");
  CHECK(text::isValidUtf8(escapeMarkdown("\xE3\x81\xFF\xC0")));
  // 前後の空白（行頭の空白 4 つはコードブロックになる）
  CHECK(escapeMarkdown("    code") == "code");
  CHECK(escapeMarkdown("") == "");
  // 長すぎる文字列は切る
  const std::string longName(1000, 'a');
  const std::string e = escapeMarkdown(longName);
  CHECK(text::codepointCount(e) == kWeeklyMaxNameChars + 1);
  CHECK(e.substr(e.size() - 3) == "…");
  CHECK(text::isValidUtf8(escapeMarkdown(std::string(300, '\xE3') + "x")));
}

TEST_CASE("weekly: 件名・担当者名に Markdown の記号が入っても、表と箇条書きが壊れない") {
  std::vector<Spec> v = basic();
  v[1].name = "A|B [リンク](http://evil.example) `x` <b>太字</b> & #1\n2行目";
  v[1].who = "山田|太郎\n- 注入";
  v[2].name = "# 見出しにしたい";
  v[8].name = "> 引用にしたい | 表を崩す";
  v[8].who = "鈴木|2";
  Proj p = Proj::make(v);
  const WeeklyReport r = p.report();
  const std::string md = weeklyMarkdown(r);
  // 生の "<" は出力に残らない（所感の "<" も打ち消す）
  CHECK_FALSE(has(md, "<b>"));
  CHECK_FALSE(has(md, "<script"));
  // リンクは [ ] が打ち消されて、リンクにならない
  CHECK_FALSE(has(md, "[リンク]("));
  CHECK(has(md, "\\[リンク\\]"));
  // 改行が行を増やさない（「2行目」は同じ行の中）
  bool found = false;
  for (const std::string& ln : lines(md))
    if (has(ln, "2行目")) {
      found = true;
      CHECK(has(ln, "A\\|B"));
    }
  CHECK(found);
  // 件名の "# 見出しにしたい" は行頭にならない（表示 ID が先に付く）。見出しの行になるのは自分で作ったものだけ
  CHECK(has(md, "見出しにしたい"));
  CHECK_FALSE(has(md, "\n# 見出しにしたい"));
  CHECK(rawLt(md) == 0);
  CHECK(has(md, "\\> 引用にしたい \\| 表を崩す"));
  // 表の行: 打ち消されていない | の数は列の数 + 1
  int tables = 0;
  for (const std::string& ln : lines(md)) {
    if (ln.rfind("| ", 0) != 0) continue;
    ++tables;
    {
      const int pipes = rawPipes(ln);
      CHECK((pipes == 6 || pipes == 7));
    }
  }
  CHECK(tables > 0);
  // 担当者名の中の "- 注入" が箇条書きの行を増やさない
  for (const std::string& ln : lines(md)) CHECK_FALSE(ln.rfind("- 注入", 0) == 0);
  CHECK(has(md, "山田\\|太郎 - 注入"));
}

TEST_CASE("weekly: 担当別の表の行は列の数どおり") {
  std::vector<Spec> v = basic();
  v[1].who = "a|b|c";
  Proj p = Proj::make(v);
  const std::string md = weeklyMarkdown(p.report());
  std::size_t head = 0;
  for (const std::string& ln : lines(md)) {
    if (ln.rfind("| 担当 |", 0) == 0) head = static_cast<std::size_t>(rawPipes(ln));
  }
  REQUIRE(head > 0);
  CHECK(head == 7);  // 6 列
  for (const std::string& ln : lines(md))
    if (has(ln, "a\\|b\\|c") && ln.rfind("| ", 0) == 0) CHECK(static_cast<std::size_t>(rawPipes(ln)) == head);
}

TEST_CASE("weekly: プロジェクト名と所感の扱い") {
  Proj p = Proj::make(basic());
  p.name = "# 名前|[x]";
  WeeklyOptions o;
  o.notes = "- 良かった点\n- 課題 <script>alert(1)</script>\n\n## お願い\n連絡はこちら";
  const std::string md = weeklyMarkdown(p.report(o));
  CHECK(md.rfind("# \\# 名前\\|\\[x\\] 週次レポート\n", 0) == 0);
  // 所感は Markdown のまま（箇条書き・見出しを書ける）。ただし "<" は打ち消す
  CHECK(has(md, "\n- 良かった点\n- 課題 \\<script>alert(1)\\</script>\n\n## お願い\n連絡はこちら"));
  CHECK(rawLt(md) == 0);
  // 所感が空なら見出しだけ
  const std::string md2 = weeklyMarkdown(p.report());
  CHECK(md2.size() >= 12);
  CHECK(md2.substr(md2.size() - std::string("## 所感・課題・お願い\n").size()) == "## 所感・課題・お願い\n");
}

TEST_CASE("weekly: 所感の下ごしらえ（制御文字・長さ・不正な UTF-8）") {
  CHECK(weeklySanitizeNotes("a\r\nb") == "a\nb");
  CHECK(weeklySanitizeNotes(std::string("a\0b\x01", 4)) == "ab");
  CHECK(weeklySanitizeNotes("a\tb") == "a\tb");
  CHECK(weeklySanitizeNotes("a\xE2\x80\xAE" "b") == "ab");
  CHECK(text::isValidUtf8(weeklySanitizeNotes("\xFF\xFE\xE3\x81")));
  const std::string big(kWeeklyMaxNoteBytes + 100, 'x');
  CHECK(weeklySanitizeNotes(big).size() == kWeeklyMaxNoteBytes);
  // 文字の途中では切らない（3 バイトの文字を並べて上限をまたぐ）
  std::string jp;
  while (jp.size() < kWeeklyMaxNoteBytes + 10) jp += "あ";
  const std::string cut = weeklySanitizeNotes(jp);
  CHECK(cut.size() <= kWeeklyMaxNoteBytes);
  CHECK(cut.size() % 3 == 0);
  CHECK(text::isValidUtf8(cut));
}

TEST_CASE("weekly: 項目ごとのオン・オフ") {
  Proj p = Proj::make(basic());
  WeeklyOptions o;
  const std::string all = weeklyMarkdown(p.report(o));
  for (const char* h : {"## サマリー", "## 今週の完了", "## 進行中", "## 来週の予定", "## 遅れ・余裕小", "## マイルストーン", "## 担当別", "## 所感・課題・お願い"})
    CHECK(has(all, h));
  o.sections = WeeklySections{};
  o.sections.summary = o.sections.done = o.sections.progress = o.sections.next = o.sections.risks = false;
  o.sections.milestones = o.sections.assignees = o.sections.notes = false;
  const std::string none = weeklyMarkdown(p.report(o));
  for (const char* h : {"## サマリー", "## 今週の完了", "## 進行中", "## 来週の予定", "## 遅れ・余裕小", "## マイルストーン", "## 担当別", "## 所感・課題・お願い"})
    CHECK_FALSE(has(none, h));
  CHECK(has(none, "週次レポート"));  // 表題と期間は残る
  o.sections.done = true;
  const std::string one = weeklyMarkdown(p.report(o));
  CHECK(has(one, "## 今週の完了"));
  CHECK_FALSE(has(one, "## 進行中"));
  CHECK(WeeklySections{}.any());
  WeeklySections off;
  off.summary = off.done = off.touched = off.progress = off.next = off.risks = off.milestones = off.assignees = off.changes = off.notes = off.evm = false;
  CHECK_FALSE(off.any());
  // 期間によって見出しが変わる
  o = WeeklyOptions{};
  o.period = WeeklyPeriod::LastWeek;
  const std::string lw = weeklyMarkdown(p.report(o));
  CHECK(has(lw, "## 先週の完了"));
  CHECK(has(lw, "## 今週の予定"));
  o.period = WeeklyPeriod::Custom;
  o.from = D("2026-10-01");
  o.to = D("2026-10-10");
  const std::string cu = weeklyMarkdown(p.report(o));
  CHECK(has(cu, "## 期間内の完了"));
  CHECK(has(cu, "## 次の7日間の予定"));
}

TEST_CASE("weekly: Markdown の見た目（改行コード・末尾・見出しの前後）") {
  Proj p = Proj::make(basic());
  const std::string md = weeklyMarkdown(p.report());
  CHECK(md.find('\r') == std::string::npos);
  CHECK(md.back() == '\n');
  CHECK(md[md.size() - 2] != '\n');  // 末尾に空行を残さない
  CHECK(md.find("\n\n\n") == std::string::npos);  // 空行は続けない
  CHECK(md.rfind("# テストプロジェクト 週次レポート\n", 0) == 0);
  CHECK(has(md, "期間: 2026/10/19(月) 〜 2026/10/25(日)（営業日 5 日）"));
  CHECK(has(md, "基準日: 2026/10/21(水)"));
  // 見出しの前は空行
  const std::vector<std::string> ls = lines(md);
  for (std::size_t i = 1; i < ls.size(); ++i)
    if (ls[i].rfind("## ", 0) == 0) CHECK(ls[i - 1].empty());
}

// ---------------------------------------------------------------- HTML
TEST_CASE("weekly: HTML は件名・担当者名・所感をエスケープし、外部のものを読まない") {
  std::vector<Spec> v = basic();
  v[1].name = "<script>alert(1)</script>\"onload=\"x";
  v[1].who = "<img src=x onerror=alert(1)>";
  v[8].name = "A & B <b>";
  Proj p = Proj::make(v);
  p.name = "<h1>名前</h1>";
  WeeklyOptions o;
  o.notes = "<script>evil()</script>\n2行目 & <b>x</b>";
  const std::string h = weeklyHtml(p.report(o));
  CHECK_FALSE(has(h, "<script"));
  CHECK_FALSE(has(h, "<img"));
  CHECK_FALSE(has(h, "<b>x"));
  CHECK_FALSE(has(h, "onerror=alert(1)>"));
  CHECK(has(h, "&lt;script&gt;alert(1)&lt;/script&gt;&quot;onload=&quot;x"));
  CHECK(has(h, "&lt;img src=x onerror=alert(1)&gt;"));
  CHECK(has(h, "A &amp; B &lt;b&gt;"));
  CHECK(has(h, "&lt;script&gt;evil()&lt;/script&gt;<br>\n2行目 &amp; &lt;b&gt;x&lt;/b&gt;"));
  CHECK(has(h, "<title>&lt;h1&gt;名前&lt;/h1&gt; - 週次レポート</title>"));
  // 外部のファイル・スクリプトを使わない（利用者のデータを含まない出力で確かめる）
  const std::string clean = weeklyHtml(Proj::make(basic()).report());
  for (const char* bad : {"<script", "<link", "src=", "href=", "http://", "https://", "@import", "url("}) CHECK_FALSE(has(clean, bad));
  CHECK(has(h, "<!doctype html>"));
  CHECK(has(h, "<meta charset=\"utf-8\">"));
  // 出力に生の "<" は、出力側で作ったタグだけ
  for (std::size_t q = h.find('<'); q != std::string::npos; q = h.find('<', q + 1)) {
    std::size_t e = q + 1;
    if (e < h.size() && h[e] == '/') ++e;
    std::string tag;
    while (e < h.size() && ((h[e] >= 'a' && h[e] <= 'z') || (h[e] >= '0' && h[e] <= '9') || h[e] == '!')) tag += h[e++];
    static const char* const ok[] = {"html", "head", "meta", "title", "style", "body", "div", "header", "main", "section", "footer", "h1", "h2",
                                     "h3", "p", "b", "span", "small", "table", "thead", "tbody", "tr", "th", "td", "br", "!doctype"};
    bool known = false;
    for (const char* k : ok) known |= tag == k;
    CHECK_MESSAGE(known, "未知のタグ: ", tag);
  }
}

TEST_CASE("weekly: HTML の項目ごとのオン・オフと、期間・営業日の表示") {
  Proj p = Proj::make(basic());
  FixedClock clk(LocalTime{2026, 10, 21, 9, 5, 0});
  WeeklyOptions o;
  const std::string h = weeklyHtml(p.report(o), &clk);
  CHECK(has(h, "<h2>サマリー</h2>"));
  CHECK(has(h, "<h2>今週の完了<small>2 件</small></h2>"));
  CHECK(has(h, "期間 <b>2026/10/19(月) 〜 2026/10/25(日)</b>（営業日 5 日）"));
  CHECK(has(h, "出力 <b>2026/10/21 09:05</b>"));
  CHECK(has(h, "<h2>所感・課題・お願い</h2>"));
  CHECK(has(h, "（未記入）"));
  o.sections.done = o.sections.risks = false;
  const std::string h2 = weeklyHtml(p.report(o));
  CHECK_FALSE(has(h2, "<h2>今週の完了"));
  CHECK_FALSE(has(h2, "<h2>遅れ・余裕小"));
  CHECK_FALSE(has(h2, "出力 <b>"));
  CHECK(has(h2, "<h2>進行中"));
  // 全部オフ
  o.sections = WeeklySections{};
  o.sections.summary = o.sections.done = o.sections.touched = o.sections.progress = o.sections.next = o.sections.risks = false;
  o.sections.milestones = o.sections.assignees = o.sections.changes = o.sections.notes = o.sections.evm = false;
  CHECK(has(weeklyHtml(p.report(o)), "出す項目が選ばれていません"));
}

// ---------------------------------------------------------------- スナップショット
TEST_CASE("weekly: スナップショットは課題ごとの状況・見込みの完了日・遅れを持つ") {
  Proj p = Proj::make(basic());
  const WeeklySnapshot s = makeWeeklySnapshot(p.input());
  CHECK(s.base == p.base);
  CHECK(s.tasks == p.stats.tasks);
  CHECK(s.done == p.stats.done);
  CHECK(s.finish == p.stats.finish);
  // 作業（葉でマイルストーンでない）だけ
  CHECK(s.items.size() == static_cast<std::size_t>(p.stats.tasks));
  const auto find = [&](const char* id) -> const WeeklySnapItem* {
    for (const WeeklySnapItem& it : s.items)
      if (it.id == id) return &it;
    return nullptr;
  };
  REQUIRE(find("2"));
  CHECK(find("2")->st == 'd');
  CHECK(find("2")->finish == D("2026-10-19"));  // 完了日
  CHECK_FALSE(find("2")->late);
  REQUIRE(find("6"));
  CHECK(find("6")->st == 'd');
  CHECK_FALSE(find("6")->finish.has_value());  // 完了日なし
  REQUIRE(find("7"));
  CHECK(find("7")->st == 'p');
  REQUIRE(find("9"));
  CHECK(find("9")->st == 't');
  CHECK(find("9")->late);
  CHECK(find("9")->finish == p.row("9").asapFinish);
  CHECK(find("1") == nullptr);   // 親
  CHECK(find("11") == nullptr);  // マイルストーン
}

TEST_CASE("weekly: スナップショットの往復（JSON）") {
  Proj p = Proj::make(basic());
  WeeklyMemo m;
  m.notes = "所感\n2行目 \"引用\" \\ 日本語";
  weeklyPutSnapshot(m, makeWeeklySnapshot(p.input()));
  const auto j = weeklyToJson(m);
  const WeeklyMemo back = weeklyFromJson(j);
  CHECK(back == m);
  // 文字列にして読み戻しても同じ
  const WeeklyMemo back2 = weeklyFromJson(nlohmann::ordered_json::parse(j.dump()));
  CHECK(back2 == m);
  CHECK(back2.snaps.size() == 1);
  CHECK(back2.snaps[0].items.size() == m.snaps[0].items.size());
  // 空
  CHECK(WeeklyMemo{}.empty());
  CHECK(weeklyFromJson(nlohmann::ordered_json()) == WeeklyMemo{});
}

TEST_CASE("weekly: 壊れた値は読み飛ばす（読み込み時の検証）") {
  using J = nlohmann::ordered_json;
  // 型が違う
  CHECK(weeklyFromJson(J(5)).empty());
  CHECK(weeklyFromJson(J("x")).empty());
  CHECK(weeklyFromJson(J::array()).empty());
  CHECK(weeklyFromJson(J{{"notes", 5}, {"snaps", "x"}}).empty());
  // 所感だけ生きる
  const WeeklyMemo a = weeklyFromJson(J{{"notes", "ok"}, {"snaps", J::array({5, "x", J::object()})}});
  CHECK(a.notes == "ok");
  CHECK(a.snaps.empty());
  // 日付が不正・存在しない日
  CHECK(weeklyFromJson(J{{"snaps", J::array({J{{"base", "2026-02-30"}}})}}).snaps.empty());
  CHECK(weeklyFromJson(J{{"snaps", J::array({J{{"base", "abc"}}})}}).snaps.empty());
  CHECK(weeklyFromJson(J{{"snaps", J::array({J{{"base", 20261021}}})}}).snaps.empty());
  CHECK(weeklyFromJson(J{{"snaps", J::array({J{{"tasks", 1}}})}}).snaps.empty());  // base がない
  // 数の範囲
  CHECK(weeklyFromJson(J{{"snaps", J::array({J{{"base", "2026-10-21"}, {"tasks", -1}}})}}).snaps.empty());
  CHECK(weeklyFromJson(J{{"snaps", J::array({J{{"base", "2026-10-21"}, {"tasks", 1.5}}})}}).snaps.empty());
  CHECK(weeklyFromJson(J{{"snaps", J::array({J{{"base", "2026-10-21"}, {"done", 99999999999LL}}})}}).snaps.empty());
  CHECK(weeklyFromJson(J{{"snaps", J::array({J{{"base", "2026-10-21"}, {"progress", "x"}}})}}).snaps.empty());
  const WeeklyMemo prog = weeklyFromJson(J{{"snaps", J::array({J{{"base", "2026-10-21"}, {"progress", 7.5}}})}});
  REQUIRE(prog.snaps.size() == 1);
  CHECK(prog.snaps[0].progress == 1.0);  // 0〜1 に丸める
  CHECK(weeklyFromJson(J{{"snaps", J::array({J{{"base", "2026-10-21"}, {"finish", "xx"}}})}}).snaps.empty());
  CHECK(weeklyFromJson(J{{"snaps", J::array({J{{"base", "2026-10-21"}, {"items", 5}}})}}).snaps.empty());
  // 壊れた項目だけ読み飛ばす
  const J items = J::array({"1\tp\t2026-11-01\t0", 5, "bad", "2\tx\t2026-11-01\t0", "3\tt\t2026-13-01\t0", "4\tt\t\t2", "\tt\t\t0",
                            "5\td\t2026-10-20\t1", "1\tt\t\t0", "6\tt\t\t1", "a\tb\tc\td\te"});
  const WeeklyMemo b = weeklyFromJson(J{{"snaps", J::array({J{{"base", "2026-10-21"}, {"items", items}}})}});
  REQUIRE(b.snaps.size() == 1);
  std::vector<std::string> ids;
  for (const WeeklySnapItem& it : b.snaps[0].items) ids.push_back(it.id);
  CHECK(ids == std::vector<std::string>{"1", "5", "6"});  // 重複の "1" は 1 つ目だけ
  CHECK(b.snaps[0].items[0].st == 'p');
  CHECK(b.snaps[0].items[1].st == 'd');
  CHECK_FALSE(b.snaps[0].items[1].late);  // 完了は遅れにしない
  CHECK(b.snaps[0].items[2].late);
  // 長すぎる id・制御文字の id
  const J bad = J::array({std::string(kWeeklyMaxIdBytes + 1, 'a') + "\tt\t\t0", std::string(kWeeklyMaxIdBytes, 'a') + "\tt\t\t0",
                          std::string("a\x01") + "b\tt\t\t0"});
  const WeeklyMemo c = weeklyFromJson(J{{"snaps", J::array({J{{"base", "2026-10-21"}, {"items", bad}}})}});
  REQUIRE(c.snaps.size() == 1);
  CHECK(c.snaps[0].items.size() == 1);
}

TEST_CASE("weekly: スナップショットの件数の上限") {
  using J = nlohmann::ordered_json;
  J items = J::array();
  for (std::size_t i = 0; i < kWeeklyMaxSnapshotItems + 50; ++i) items.push_back(std::to_string(i) + "\tt\t\t0");
  const WeeklyMemo m = weeklyFromJson(J{{"snaps", J::array({J{{"base", "2026-10-21"}, {"items", items}}})}});
  REQUIRE(m.snaps.size() == 1);
  CHECK(m.snaps[0].items.size() == kWeeklyMaxSnapshotItems);
  CHECK(m.snaps[0].truncated);
  // 書き込み側
  WeeklySnapshot s;
  s.base = D("2026-10-21");
  for (std::size_t i = 0; i < kWeeklyMaxSnapshotItems + 10; ++i) s.items.push_back({std::to_string(i), 't', std::nullopt, false});
  WeeklyMemo w;
  weeklyPutSnapshot(w, s);
  CHECK(w.snaps[0].items.size() == kWeeklyMaxSnapshotItems);
  CHECK(w.snaps[0].truncated);
  // 作業が上限より多いプロジェクトのスナップショット
  std::vector<Spec> v;
  v.push_back({"1", "全体", "", "To Do", "", "Epic", "", "", "2027-12-25", 1, {}});
  for (std::size_t i = 0; i < kWeeklyMaxSnapshotItems + 20; ++i) v.push_back({std::to_string(i + 2), "T", "", "To Do", "1", "Task", "", "", "", 1, {}});
  Proj p = Proj::make(v);
  const WeeklySnapshot big = makeWeeklySnapshot(p.input());
  CHECK(big.items.size() == kWeeklyMaxSnapshotItems);
  CHECK(big.truncated);
}

TEST_CASE("weekly: スナップショットは週ごとに 1 つ、古い週のものから捨てる") {
  WeeklyMemo m;
  const auto snap = [](const char* iso) {
    WeeklySnapshot s;
    s.base = D(iso);
    return s;
  };
  weeklyPutSnapshot(m, snap("2026-10-14"));  // 水
  weeklyPutSnapshot(m, snap("2026-10-16"));  // 同じ週（金）→ 置き換え
  REQUIRE(m.snaps.size() == 1);
  CHECK(fmtIso(m.snaps[0].base) == "2026-10-16");
  weeklyPutSnapshot(m, snap("2026-10-21"));
  weeklyPutSnapshot(m, snap("2026-10-28"));
  REQUIRE(m.snaps.size() == kWeeklyMaxSnapshots);
  CHECK(fmtIso(m.snaps[0].base) == "2026-10-21");
  CHECK(fmtIso(m.snaps[1].base) == "2026-10-28");
  // 前の週のスナップショット: 基準日の週より前のうち一番新しい
  CHECK(fmtIso(weeklyPreviousSnapshot(m, D("2026-10-28"))->base) == "2026-10-21");
  CHECK(fmtIso(weeklyPreviousSnapshot(m, D("2026-11-04"))->base) == "2026-10-28");
  CHECK(weeklyPreviousSnapshot(m, D("2026-10-21")) == nullptr);  // 同じ週・それより前のものはない
  CHECK(weeklyPreviousSnapshot(m, D("2026-10-25")) == nullptr);  // 日曜も同じ週
  CHECK(fmtIso(weeklyPreviousSnapshot(m, D("2026-10-26"))->base) == "2026-10-21");  // 月曜は次の週
  CHECK(weeklyPreviousSnapshot(WeeklyMemo{}, D("2026-10-21")) == nullptr);
  // 年またぎ: 12/31（木）の週と 1/4（月）の週は別
  WeeklyMemo y;
  weeklyPutSnapshot(y, snap("2026-12-31"));
  CHECK(weeklyPreviousSnapshot(y, D("2027-01-04")) != nullptr);
  CHECK(weeklyPreviousSnapshot(y, D("2027-01-03")) == nullptr);
}

TEST_CASE("weekly: settings.json の weekly は往復し、未知のキーを壊さず、壊れた値を読み飛ばす") {
  FixedClock clk(LocalTime{2026, 10, 21, 9, 0, 0});
  Proj p = Proj::make(basic());
  ProjectRecord rec;
  rec.settings.id = "p1";
  rec.settings.name = "P";
  rec.settings.baseDate = "2026-10-21";
  rec.settings.holidays = "";
  rec.settings.weekly.notes = "メモ";
  weeklyPutSnapshot(rec.settings.weekly, makeWeeklySnapshot(p.input()));
  rec.settings.unknown["futureKey"] = Json{{"a", 1}};
  const Json j = settingsToJson(rec);
  REQUIRE(j.contains("weekly"));
  CHECK(j["futureKey"]["a"] == 1);
  const ProjectRecord back = recordFromSettingsJson(j, "", "stem", clk);
  CHECK(back.settings.weekly == rec.settings.weekly);
  CHECK(back.settings.unknown["futureKey"]["a"] == 1);
  CHECK_FALSE(back.settings.unknown.contains("weekly"));  // 既知のキーは unknown に入れない
  // 文字列にして読み戻す
  const ProjectRecord back2 = recordFromSettingsJson(Json::parse(dumpJs(j)), "", "stem", clk);
  CHECK(back2.settings.weekly == rec.settings.weekly);
  // 空なら書かない
  ProjectRecord empty = rec;
  empty.settings.weekly = WeeklyMemo{};
  CHECK_FALSE(settingsToJson(empty).contains("weekly"));
  // 壊れた weekly
  Json broken = j;
  broken["weekly"] = "壊れた";
  CHECK(recordFromSettingsJson(broken, "", "stem", clk).settings.weekly.empty());
  broken["weekly"] = Json{{"notes", 5}, {"snaps", Json::array({1, 2})}};
  CHECK(recordFromSettingsJson(broken, "", "stem", clk).settings.weekly.empty());
}

// ---------------------------------------------------------------- 先週との比較
TEST_CASE("weekly: 先週との比較（完了・新たに遅れた・見込みの動き・増減）") {
  std::vector<Spec> v0;
  v0.push_back({"1", "全体", "", "To Do", "", "Epic", "", "", "2026-12-25", 1, {}});
  v0.push_back({"2", "X", "田中", "Done", "1", "Task", "2026-10-12", "", "", 1, {}});
  v0.push_back({"3", "Y", "田中", "To Do", "1", "Task", "", "", "", 1, {}});
  v0.push_back({"4", "P", "鈴木", "To Do", "1", "Task", "", "", "", 1, {"5"}});
  v0.push_back({"5", "Z", "鈴木", "To Do", "1", "Task", "", "", "", 1, {}});
  v0.push_back({"6", "W", "鈴木", "To Do", "1", "Task", "", "", "", 1, {}});
  v0.push_back({"7", "Q", "鈴木", "To Do", "1", "Task", "", "", "", 1, {}});
  Proj p0 = Proj::make(v0);
  const WeeklySnapshot snap = makeWeeklySnapshot(p0.input());

  std::vector<Spec> v1 = v0;
  v1[2].status = "Done";  // Y 完了
  v1[2].resolved = "2026-10-22";
  v1[3].weeks = 2;        // P が延びる → Z が後ろへ
  v1[5].deadline = "2026-10-22";  // W の期限が厳しくなる
  v1.erase(v1.begin() + 6);       // Q を削除
  v1.push_back({"8", "R", "田中", "To Do", "1", "Task", "", "", "", 1, {}});  // R を追加
  Proj p1 = Proj::make(v1);
  REQUIRE(p1.row("6").floatDays < 0);
  REQUIRE(p0.row("6").floatDays >= 0);
  const WeeklyReport r = p1.report({}, &snap);

  REQUIRE(r.changes.available);
  CHECK(r.changes.since == p0.base);
  CHECK(didsOf(p1, r.changes.newlyDone) == std::vector<std::string>{"Y"});
  CHECK(didsOf(p1, r.changes.newlyLate) == std::vector<std::string>{"W"});
  CHECK(r.changes.added == 1);    // R
  CHECK(r.changes.removed == 1);  // Q
  // 見込みの完了日が動いたのは P と Z。W は期限だけ変わったので動かない
  std::vector<std::string> moved;
  for (const WeeklyFinishMove& mv : r.changes.finishMoved) {
    moved.push_back(mv.name);
    CHECK(mv.shiftDays == p1.cal.floatDays(mv.from, mv.to));
    CHECK(mv.shiftDays > 0);
  }
  std::sort(moved.begin(), moved.end());
  CHECK(moved == std::vector<std::string>{"P", "Z"});
  CHECK(r.changes.finishMovedTotal == 2);
  // 動きの大きい順
  CHECK(r.changes.finishMoved[0].shiftDays >= r.changes.finishMoved[1].shiftDays);
  // サマリー
  CHECK(r.summary.hasPrev);
  CHECK(r.summary.doneDelta == 1);
  CHECK(r.summary.lateDelta == p1.stats.late - p0.stats.late);
  CHECK(r.summary.progressDelta == doctest::Approx(p1.stats.progress() - p0.stats.progress()));
  // Markdown・HTML に出る
  const std::string md = weeklyMarkdown(r);
  CHECK(has(md, "## 先週から変わったこと"));
  CHECK(has(md, "### 新しく完了した課題（1 件）"));
  CHECK(has(md, "### 新たに遅れた課題（1 件）"));
  CHECK(has(md, "増えた作業: 1 件 ／ 無くなった作業: 1 件"));
  CHECK(has(md, "進み: "));
  CHECK(has(md, "（先週から "));
  CHECK(has(weeklyHtml(r), "<h2>先週から変わったこと</h2>"));
  // 比較をオフにすると出ない。スナップショットがなければ出ない
  WeeklyOptions off;
  off.sections.changes = false;
  CHECK_FALSE(has(weeklyMarkdown(p1.report(off, &snap)), "先週から変わったこと"));
  CHECK_FALSE(has(weeklyMarkdown(p1.report()), "先週から変わったこと"));
  CHECK_FALSE(has(weeklyMarkdown(p1.report()), "先週から"));
}

TEST_CASE("weekly: 比べる相手が空・一部だけのとき") {
  Proj p = Proj::make(basic());
  WeeklySnapshot empty;
  empty.base = D("2026-10-14");
  const WeeklyReport r = p.report({}, &empty);
  CHECK(r.changes.available);
  CHECK(r.changes.added == p.stats.tasks);
  CHECK(r.changes.finishMoved.empty());
  WeeklySnapshot part = empty;
  part.truncated = true;
  const WeeklyReport rt = p.report({}, &part);
  CHECK(rt.changes.truncated);
  CHECK(rt.changes.added == 0);  // 一部だけなので「増えた」は数えない
  CHECK(has(weeklyMarkdown(rt), "一部の課題だけです"));
}

// ---------------------------------------------------------------- ファイル
TEST_CASE("weekly: 既定のファイル名と保存先の検査") {
  CHECK(weeklyFileName("案件A", D("2026-10-19"), false) == "案件A_週報_2026-10-19.md");
  CHECK(weeklyFileName("案件A", D("2026-10-19"), true) == "案件A_週報_2026-10-19.html");
  CHECK(weeklyFileName("a/b:c*d", D("2026-10-19"), false) == "a_b_c_d_週報_2026-10-19.md");
  CHECK(weeklyFileName("", D("2026-10-19"), false) == "報告_週報_2026-10-19.md");
  CHECK(weeklyFileName("CON", D("2026-10-19"), false) == "_CON_週報_2026-10-19.md");
  CHECK(weeklyFileName(std::string(300, 'a'), D("2026-10-19"), true).size() < 100);
  CHECK(checkWeeklyPath("out/r.md", false).ok());
  CHECK(checkWeeklyPath("out/r.MD", false).ok());
  CHECK(checkWeeklyPath("out/r.markdown", false).ok());
  CHECK(checkWeeklyPath("out/r.txt", false).ok());
  CHECK_FALSE(checkWeeklyPath("out/r.html", false).ok());
  CHECK(checkWeeklyPath("out/r.html", true).ok());
  CHECK(checkWeeklyPath("out/r.htm", true).ok());
  CHECK_FALSE(checkWeeklyPath("out/r.md", true).ok());
  CHECK_FALSE(checkWeeklyPath("", false).ok());
  CHECK_FALSE(checkWeeklyPath("out/", false).ok());
  CHECK_FALSE(checkWeeklyPath("out/.md", false).ok());
  CHECK_FALSE(checkWeeklyPath("a\n.md", false).ok());
  CHECK_FALSE(checkWeeklyPath("a\xFF.md", false).ok());
  CHECK_FALSE(checkWeeklyPath(std::string(40000, 'a') + ".md", false).ok());
  CHECK_FALSE(checkWeeklyPath("out/r.exe", false).ok());
}

TEST_CASE("weekly: writeWeekly は検査して IFileStore に書く（失敗は戻り値）") {
  MemFileStore fs;
  CHECK(fs.makeDirs("out").ok());
  CHECK(writeWeekly(fs, "out/r.md", "# x\n", false).ok());
  CHECK(fs.content("out/r.md") == "# x\n");
  const int w = fs.writeCount();
  CHECK_FALSE(writeWeekly(fs, "out/r.html", "x", false).ok());
  CHECK_FALSE(writeWeekly(fs, "out/r.exe", "x", true).ok());
  CHECK(fs.writeCount() == w);
  CHECK(fs.makeDirs("dir.md").ok());
  CHECK_FALSE(writeWeekly(fs, "dir.md", "x", false).ok());
  fs.setFailWrites(true);
  const auto fail = writeWeekly(fs, "out/r2.md", "x", false);
  CHECK_FALSE(fail.ok());
  CHECK_FALSE(fail.error().empty());
  CHECK_FALSE(fs.exists("out/r2.md"));
}

TEST_CASE("weekly: 文言の補助") {
  CHECK(cleanText("a\nb\tc\x01" "d") == "a b cd");
  Proj p = Proj::make(basic());
  const WeeklyReport r = p.report();
  const std::string md = weeklyMarkdown(r);
  CHECK(has(md, "- 進み: "));
  CHECK(has(md, "- 作業: 全体 9 件（完了 5・進行中 1・未着手 3）"));
  CHECK(has(md, "完了日のない完了済みの作業が 1 件あります"));
  CHECK(has(md, "**遅れ** "));
  CHECK(has(md, "名前順です（順位ではありません）"));
}

// ---- Copilot の指摘への対応（#46） ----
TEST_CASE("weekly: 所感の \\< は、直前のバックslash の数が偶数のときだけ打ち消す（奇数なら すでに打ち消されている）") {
  Proj p = Proj::make(basic());
  const auto notesOf = [&](const std::string& n) {
    WeeklyOptions o;
    o.notes = n;
    const std::string md = weeklyMarkdown(p.report(o));
    const std::size_t at = md.find("## 所感・課題・お願い");
    REQUIRE(at != std::string::npos);
    return md.substr(at);
  };
  // 打ち消されていない "<" が残らない（rawLt は直前の \ を 1 つ数えて飛ばす見方と同じ）
  CHECK(rawLt(notesOf("<img src=x onerror=alert(1)>")) == 0);       // 0 個（偶数）→ 足す
  CHECK(rawLt(notesOf("\\<img src=x onerror=alert(1)>")) == 0);     // 1 個（奇数）→ すでに打ち消し済み。足さない
  CHECK(rawLt(notesOf("\\\\<img src=x onerror=alert(1)>")) == 0);   // 2 個（偶数）→ 足す
  CHECK(rawLt(notesOf("\\\\\\<img src=x>")) == 0);                  // 3 個（奇数）
  // 具体的な出力: 奇数ならそのまま、偶数なら 1 つ足す
  CHECK(has(notesOf("\\<b>"), "\\<b>"));
  CHECK_FALSE(has(notesOf("\\<b>"), "\\\\<b>"));
  CHECK(has(notesOf("\\\\<b>"), "\\\\\\<b>"));
}

TEST_CASE("weekly: 最後の列（26 週より先をまとめたもの）は、来週の負荷として使わない") {
  Proj p = Proj::make(basic());
  LoadReport ld = p.load;
  REQUIRE(ld.weekStarts.size() >= 2);
  REQUIRE(ld.weekStarts[1] == D("2026-10-26"));  // 来週の月曜
  ld.weekStarts.resize(2);                       // 今週と来週だけにして、最後（来週）の列を「まとめた列」にする
  ld.lastFolded = true;
  WeeklyInput in(p.comp.T, p.issues, p.comp.R, p.cal, p.stats, ld);
  in.base = p.base;
  const WeeklyReport r = buildWeeklyReport(in, WeeklyOptions{});
  for (const auto& a : r.assignees) CHECK(a.loadNextPercent == -1);  // 不明（「-」）
  // まとめていなければ、これまでどおり出る
  ld.lastFolded = false;
  WeeklyInput in2(p.comp.T, p.issues, p.comp.R, p.cal, p.stats, ld);
  in2.base = p.base;
  const WeeklyReport r2 = buildWeeklyReport(in2, WeeklyOptions{});
  bool any = false;
  for (const auto& a : r2.assignees) any = any || a.loadNextPercent >= 0;
  CHECK(any);
}

TEST_CASE("weekly: 休業日の変更で見込みの完了日が動いても、営業日の差が 0 なら「変わらず」とは書かず、日付の変化を出す") {
  std::vector<Spec> v;
  v.push_back({"1", "全体", "", "To Do", "", "Epic", "", "", "2026-12-25", 1, {}});
  v.push_back({"2", "A", "田中", "To Do", "1", "Task", "", "2026-10-26", "", 1, {}});  // 来週の月曜から
  Proj p0 = Proj::make(v);
  const WeeklySnapshot snap = makeWeeklySnapshot(p0.input());
  // 前の週の記録の見込みの完了日を、いまの見込みの翌日（非営業日）にする: 日付は違うが営業日の差は 0
  WeeklySnapshot prev = snap;
  REQUIRE_FALSE(prev.items.empty());
  Proj p1 = Proj::make(v);
  const Day d = p1.row("2").asapFinish + 1;  // 完了日（金曜）の翌日（土曜）
  REQUIRE_FALSE(p1.cal.isWork(d));
  for (auto& it : prev.items)
    if (it.finish) it.finish = d;
  const WeeklyReport r = p1.report({}, &prev);
  REQUIRE(r.changes.available);
  bool found = false;
  for (const WeeklyFinishMove& mv : r.changes.finishMoved)
    if (mv.name == "A") {
      found = true;
      CHECK(mv.from != mv.to);
      CHECK(mv.shiftDays == 0);  // 営業日の差は 0
    }
  CHECK(found);            // 日付が違うので、隠さず出す
  CHECK(r.changes.finishMovedTotal >= 1);
  const std::string md = weeklyMarkdown(r);
  CHECK(has(md, "A"));
  CHECK_FALSE(has(md, "変わらず"));
  CHECK_FALSE(has(weeklyHtml(r), "変わらず"));
}

TEST_CASE("weekly: Issue Id が長すぎて入らない課題があるときは、スナップショットを不完全にして、増えた作業を数えない") {
  std::vector<Spec> v;
  v.push_back({"1", "全体", "", "To Do", "", "Epic", "", "", "2026-12-25", 1, {}});
  const std::string longId(kWeeklyMaxIdBytes + 10, 'x');
  v.push_back({longId, "長い", "田中", "To Do", "1", "Task", "", "", "", 1, {}});
  v.push_back({"3", "短い", "田中", "To Do", "1", "Task", "", "", "", 1, {}});
  Proj p = Proj::make(v);
  const WeeklySnapshot snap = makeWeeklySnapshot(p.input());
  CHECK(snap.truncated);                          // 入らなかったので不完全
  CHECK(snap.items.size() == 1);                  // 短い id のものだけ
  // 変わっていないプロジェクトを比べても、長い id の課題が「増えた」にならない
  const WeeklyReport r = p.report({}, &snap);
  REQUIRE(r.changes.available);
  CHECK(r.changes.added == 0);
}

TEST_CASE("weekly: 課題がないプロジェクトでも、所感を出す項目がオンなら、Markdown と HTML の両方に所感を入れる") {
  Computed c;
  auto t = buildTree({}, ProjectMeta{});
  REQUIRE(t.ok());
  c.T = std::move(t).value();
  const std::vector<Issue> issues;
  const std::vector<ScheduleRow> rows;
  const Calendar cal;
  const ProjectStats st = computeStats(c.T, issues, rows, D("2026-10-21"));
  const LoadReport ld = computeLoad(c.T, issues, rows, cal, D("2026-10-21"), ResourceLimit{});
  WeeklyInput in(c.T, issues, rows, cal, st, ld);
  in.base = D("2026-10-21");
  WeeklyOptions o;
  o.notes = "来週は設計を始める";
  const WeeklyReport r = buildWeeklyReport(in, o);
  REQUIRE(r.empty);
  const std::string md = weeklyMarkdown(r);
  CHECK(has(md, "課題がありません"));
  CHECK(has(md, "## 所感・課題・お願い"));
  CHECK(has(md, "来週は設計を始める"));
  const std::string html = weeklyHtml(r);
  CHECK(has(html, "課題がありません"));
  CHECK(has(html, "来週は設計を始める"));
  // 所感の項目をオフにしたら出ない
  o.sections.notes = false;
  const WeeklyReport off = buildWeeklyReport(in, o);
  CHECK_FALSE(has(weeklyMarkdown(off), "来週は設計を始める"));
  CHECK_FALSE(has(weeklyHtml(off), "来週は設計を始める"));
}

// ---------------------------------------------------------------- EVM（Issue #65）
TEST_CASE("weekly: 「EVM（出来高管理）」の節は、ベースラインがあるときだけ、SPI・ES の遅れ・SV を出す") {
  CHECK(WeeklySections{}.evm);
  Proj a = Proj::make(basic());
  auto b = captureBaseline(a.issues, a.comp, "キッ<b>クオフ", "2026-10-19T09:30");
  REQUIRE(b.ok());
  Baseline bl = b.value();
  bl.id = "b1";
  // 計画のあとに、F（7）の期間を延ばして完了を遅らせる。基準日は 10/21
  WeeklyInput in = a.input();
  in.evmBaseline = &bl;
  const WeeklyReport r = buildWeeklyReport(in, WeeklyOptions{});
  REQUIRE(r.evm.available);
  REQUIRE(r.evm.report.ok());
  CHECK(r.evm.report.baselineName == "キッ<b>クオフ");
  const std::string md = weeklyMarkdown(r);
  CHECK(has(md, "## EVM（出来高管理）"));
  CHECK(has(md, "SPI"));
  CHECK(has(md, "SV"));
  CHECK(has(md, "ES"));
  CHECK(has(md, "BAC"));
  CHECK(has(md, "営業日"));
  CHECK_FALSE(has(md, "キッ<b>"));  // ベースライン名の < は打ち消す
  const std::string html = weeklyHtml(r);
  CHECK(has(html, "EVM（出来高管理）"));
  CHECK(has(html, "SPI（EV ÷ PV）"));
  CHECK_FALSE(has(html, "キッ<b>"));
  CHECK(has(html, "&lt;b&gt;"));

  // オフ・ベースラインなしでは出さない
  WeeklyOptions off;
  off.sections.evm = false;
  CHECK_FALSE(has(weeklyMarkdown(buildWeeklyReport(in, off)), "EVM"));
  CHECK_FALSE(has(weeklyHtml(buildWeeklyReport(in, off)), "EVM"));
  const WeeklyReport none = buildWeeklyReport(a.input(), WeeklyOptions{});
  CHECK_FALSE(none.evm.available);
  CHECK_FALSE(has(weeklyMarkdown(none), "EVM"));
  CHECK_FALSE(has(weeklyHtml(none), "EVM"));
  // 項目の一つとして数える
  WeeklySections only;
  only.summary = only.done = only.progress = only.next = only.risks = only.milestones = only.assignees = only.changes = only.notes = only.baseline = false;
  only.evm = false;
  CHECK_FALSE(only.any());
  only.evm = true;
  CHECK(only.any());
}

TEST_CASE("weekly: EVM だけがオンでベースラインがないときは、「選ばれていません」になる（Copilot の指摘 #93）") {
  Proj a = Proj::make(basic());
  WeeklyOptions o;
  o.sections.summary = o.sections.done = o.sections.progress = o.sections.next = o.sections.risks = false;
  o.sections.milestones = o.sections.assignees = o.sections.changes = o.sections.notes = o.sections.baseline = o.sections.touched = false;
  o.sections.evm = true;
  const WeeklyReport r = buildWeeklyReport(a.input(), o);  // ベースラインなし
  CHECK_FALSE(r.evm.available);
  CHECK_FALSE(r.sections.any());
  CHECK(has(weeklyHtml(r), "出す項目が選ばれていません"));
}

TEST_CASE("weekly: EVM の節は、指標を出せないとき（保存日より前）は案内を出す。課題がないときも落ちない") {
  Proj a = Proj::make(basic());
  auto b = captureBaseline(a.issues, a.comp, "後の計画", "2026-10-28T09:30");  // 基準日 10/21 より後に保存
  REQUIRE(b.ok());
  Baseline bl = b.value();
  bl.id = "b1";
  WeeklyInput in = a.input();
  in.evmBaseline = &bl;
  const WeeklyReport r = buildWeeklyReport(in, WeeklyOptions{});
  REQUIRE(r.evm.available);
  CHECK_FALSE(r.evm.report.ok());
  const std::string md = weeklyMarkdown(r);
  CHECK(has(md, "## EVM（出来高管理）"));
  CHECK(has(md, "より前なので、指標は出せません"));
  CHECK(has(weeklyHtml(r), "より前なので、指標は出せません"));
  // 課題が 0 件
  Proj e = Proj::make({});
  WeeklyInput ie = e.input();
  ie.evmBaseline = &bl;
  const WeeklyReport re = buildWeeklyReport(ie, WeeklyOptions{});
  CHECK(re.empty);
  CHECK_FALSE(has(weeklyMarkdown(re), "SPI"));
}

// ---------------------------------------------------------------- ベースラインとの差（Issue #53）
TEST_CASE("weekly: 「ベースラインとの差」の節はオプション（既定オフ）で、遅れ・早まり・計画になかった・消えた課題を出す") {
  CHECK_FALSE(WeeklySections{}.baseline);
  Proj a = Proj::make(basic());
  auto b = captureBaseline(a.issues, a.comp, "キックオフ", "2026-10-19T09:30");
  REQUIRE(b.ok());
  // 計画を変える: H を長く（遅れ）、F を短く（早まり）、U を消し、新しい課題を足す
  auto specs = basic();
  specs[8].weeks = 3;                 // H（9）: 2 週 → 3 週
  specs[6].weeks = 0.4;               // F（7）: 1 週 → 2 営業日
  specs.erase(specs.begin() + 9);     // U（10）を消す
  specs.push_back({"14", "新しい <b>課題</b>", "鈴木", "To Do", "1", "Task", "", "", "", 1, {}});
  Proj p = Proj::make(specs);
  auto cmp = compareBaseline(b.value(), p.issues, p.comp, p.cal);
  REQUIRE(cmp.ok());
  WeeklyInput in = p.input();
  in.baseline = &cmp.value();

  WeeklyOptions o;
  o.sections.baseline = true;
  const WeeklyReport r = buildWeeklyReport(in, o);
  REQUIRE(r.baseline.available);
  CHECK(r.baseline.name == "キックオフ");
  CHECK(r.baseline.sum.late >= 1);
  CHECK(r.baseline.sum.added == 1);
  CHECK(r.baseline.sum.removed == 1);
  REQUIRE(!r.baseline.late.items.empty());
  CHECK(r.baseline.late.items[0].name == "H");
  CHECK(r.baseline.late.items[0].diffDays > 0);
  REQUIRE(r.baseline.added.items.size() == 1);
  CHECK(r.baseline.added.items[0].name == "新しい <b>課題</b>");
  REQUIRE(r.baseline.removed.items.size() == 1);
  CHECK(r.baseline.removed.items[0].name == "U");
  bool sawEarly = false;
  for (const auto& it : r.baseline.early.items) sawEarly = sawEarly || it.name == "F";
  CHECK(sawEarly);

  const std::string md = weeklyMarkdown(r);
  CHECK(has(md, "## ベースラインとの差"));
  CHECK(has(md, "キックオフ"));
  CHECK(has(md, "H"));
  CHECK(has(md, "+"));                     // 遅れの営業日
  CHECK_FALSE(has(md, "<b>"));              // 件名の < はエスケープ済み
  const std::string html = weeklyHtml(r);
  CHECK(has(html, "ベースラインとの差"));
  CHECK(has(html, "キックオフ"));
  CHECK_FALSE(has(html, "<b>課題</b>"));    // HTML にもそのまま入らない

  // オフ（既定）なら出ない。比較がなければ、オンでも出ない
  CHECK_FALSE(has(weeklyMarkdown(buildWeeklyReport(in, WeeklyOptions{})), "ベースラインとの差"));
  WeeklyInput none = p.input();
  const WeeklyReport noCmp = buildWeeklyReport(none, o);
  CHECK_FALSE(noCmp.baseline.available);
  CHECK_FALSE(has(weeklyMarkdown(noCmp), "ベースラインとの差"));
  CHECK_FALSE(has(weeklyHtml(noCmp), "ベースラインとの差"));
  // 項目の一つとして数える（ほかを全部オフにしても、これだけオンなら出す項目がある）
  WeeklySections only;
  only.summary = only.done = only.touched = only.progress = only.next = only.risks = only.milestones = only.assignees = only.changes = only.notes = false;
  only.evm = false;
  CHECK_FALSE(only.any());
  only.baseline = true;
  CHECK(only.any());
}

TEST_CASE("weekly: ベースラインとの差の一覧は件数の上限で切り、営業日の差が 0 で日付だけ違う課題を数える") {
  Proj a = Proj::make(basic());
  auto b = captureBaseline(a.issues, a.comp, "合意", "2026-10-19T09:30");
  REQUIRE(b.ok());
  Baseline base = b.value();
  // 全部の計画を 1 週間前にして、遅れの課題を増やす。課題 "3"（完了日が日曜）だけは計画を土曜日にして、
  // 営業日の差が 0 のまま日付だけ違うようにする
  for (BaselineItem& it : base.items) {
    if (!it.finish) continue;
    const std::size_t k = static_cast<std::size_t>(a.idx(it.id.c_str()));
    if (it.id == "3") it.finish = a.comp.R[k].asapFinish - 1;
    else it.finish = *it.finish - 7;
  }
  auto cmp = compareBaseline(base, a.issues, a.comp, a.cal);
  REQUIRE(cmp.ok());
  const BaselineEntry& three = cmp.value().entries[static_cast<std::size_t>(a.idx("3"))];
  CHECK(three.finishDiff == 0);
  CHECK(three.finishDiffers);
  CHECK(cmp.value().sum.sameDayDiffers >= 1);
  WeeklyInput in = a.input();
  in.baseline = &cmp.value();
  WeeklyOptions o;
  o.sections.baseline = true;
  o.maxItems = 1;
  const WeeklyReport r = buildWeeklyReport(in, o);
  REQUIRE(r.baseline.available);
  CHECK(r.baseline.late.items.size() == 1);
  CHECK(r.baseline.late.total > 1);
  CHECK(r.baseline.late.omitted() == r.baseline.late.total - 1);
  const std::string md = weeklyMarkdown(r);
  CHECK(has(md, "ほか"));
  CHECK(has(md, "営業日の差が 0"));
  CHECK(has(weeklyHtml(r), "営業日の差が 0"));
}

TEST_CASE("weekly: 保存後に全課題を削除しても、ベースラインとの差（消えた課題）を Markdown・HTML の両方に出す") {
  Proj a = Proj::make(basic());
  auto b = captureBaseline(a.issues, a.comp, "キックオフ", "2026-10-19T09:30");
  REQUIRE(b.ok());
  // 課題が 0 件のプロジェクト
  Computed c;
  auto t = buildTree({}, ProjectMeta{});
  REQUIRE(t.ok());
  c.T = std::move(t).value();
  const std::vector<Issue> issues;
  const std::vector<ScheduleRow> rows;
  const Calendar cal = Calendar::fromText(defaultHolidaysText());
  auto cmp = compareBaseline(b.value(), issues, c, cal);
  REQUIRE(cmp.ok());
  REQUIRE(cmp.value().sum.removed >= 1);
  const ProjectStats st = computeStats(c.T, issues, rows, D("2026-10-21"));
  const LoadReport ld = computeLoad(c.T, issues, rows, cal, D("2026-10-21"), ResourceLimit{});
  WeeklyInput in(c.T, issues, rows, cal, st, ld);
  in.base = D("2026-10-21");
  in.baseline = &cmp.value();
  WeeklyOptions o;
  o.sections.baseline = true;
  const WeeklyReport r = buildWeeklyReport(in, o);
  REQUIRE(r.empty);
  REQUIRE(r.baseline.available);
  CHECK(r.baseline.name == "キックオフ");
  CHECK(r.baseline.removed.total == cmp.value().sum.removed);
  CHECK_FALSE(r.baseline.removed.items.empty());
  const std::string md = weeklyMarkdown(r);
  CHECK(has(md, "課題がありません"));
  CHECK(has(md, "## ベースラインとの差"));
  CHECK(has(md, "消えた課題"));
  const std::string html = weeklyHtml(r);
  CHECK(has(html, "課題がありません"));
  CHECK(has(html, "ベースラインとの差"));
  CHECK(has(html, "消えた課題"));
  // オフ（既定）なら出ない。比較がなければ出ない
  CHECK_FALSE(has(weeklyMarkdown(buildWeeklyReport(in, WeeklyOptions{})), "ベースラインとの差"));
  WeeklyInput none(c.T, issues, rows, cal, st, ld);
  none.base = in.base;
  CHECK_FALSE(buildWeeklyReport(none, o).baseline.available);
  // 入力の食い違い（課題と日程の数が合わない）では、比較を作らない
  Proj p = Proj::make(basic());
  std::vector<Issue> shorter(p.issues.begin(), p.issues.begin() + 2);
  WeeklyInput bad(p.comp.T, shorter, p.comp.R, p.cal, p.stats, p.load);
  bad.base = p.base;
  auto cmp2 = compareBaseline(b.value(), p.issues, p.comp, p.cal);
  REQUIRE(cmp2.ok());
  bad.baseline = &cmp2.value();
  const WeeklyReport rb = buildWeeklyReport(bad, o);
  CHECK(rb.empty);
  CHECK_FALSE(rb.baseline.available);
}

// ---------------------------------------------------------------- 今週触った課題（Issue #71）
namespace {

// Created・Updated の列を持たせて、課題ごとの値を入れる（空文字 = 値なし）
void stampIssue(Proj& p, const char* id, const char* created, const char* updated) {
  if (p.meta.extraHeaders.empty()) p.meta.extraHeaders = {"Created", "Updated"};
  for (Issue& o : p.issues) o.extra.resize(2);
  for (Issue& o : p.issues)
    if (o.id == id) {
      o.extra[0] = created;
      o.extra[1] = updated;
    }
}

WeeklyInput touchedInput(const Proj& p, const WeeklySnapshot* prev = nullptr) {
  WeeklyInput in = p.input(prev);
  in.meta = &p.meta;
  return in;
}

WeeklyReport touchedReport(const Proj& p, const WeeklyOptions& o = {}, const WeeklySnapshot* prev = nullptr) {
  return buildWeeklyReport(touchedInput(p, prev), o);
}

std::vector<std::string> touchedNames(const Proj& p, const WeeklyTouched& t) {
  std::vector<std::string> v;
  for (const WeeklyTouchedGroup& g : t.groups)
    for (const WeeklyTouchedItem& it : g.items) v.push_back(p.comp.T.name[static_cast<std::size_t>(it.item.index)]);
  return v;
}

// Markdown の「- <ID> <件名>（…）」の行（件名で探す）
std::string touchedLine(const std::string& md, const std::string& name) {
  for (const std::string& l : lines(md))
    if (l.rfind("- ", 0) == 0 && l.find(" " + name + "（") != std::string::npos) return l;
  return {};
}

const WeeklyTouchedItem* touchedItem(const WeeklyReport& r, const std::string& name) {
  for (const WeeklyTouchedGroup& g : r.touched.groups)
    for (const WeeklyTouchedItem& it : g.items)
      if (it.item.name == name) return &it;
  return nullptr;
}

}  // namespace

TEST_CASE("weekly touched: 期間の端を含み、完了・マイルストーン・親のまとまり自体は含めない") {
  Proj p = Proj::make(basic());  // 今週 10/19(月)〜10/25(日)
  stampIssue(p, "1", "", "2026-10-21");   // 親（Epic）: 数えない
  stampIssue(p, "2", "", "2026-10-20");   // 完了: 「今週の完了」に出るので重複しない
  stampIssue(p, "7", "", "2026-10-19");   // 期間の最初の日（含む）
  stampIssue(p, "8", "", "2026-10-25");   // 期間の最後の日（含む）
  stampIssue(p, "9", "", "2026-10-18");   // 前の週の日曜（含まない）
  stampIssue(p, "10", "", "2026-10-26");  // 次の週の月曜（含まない）
  stampIssue(p, "11", "", "2026-10-21");  // マイルストーン: 数えない
  const WeeklyReport r = touchedReport(p);
  REQUIRE(r.touched.available);
  CHECK(r.touched.hasColumn);
  CHECK(r.touched.total == 2);
  CHECK(r.touched.omitted() == 0);
  CHECK(touchedNames(p, r.touched) == std::vector<std::string>{"F", "G"});
  // 未完了の作業は F・G・H・U。どれも日付は読める（期間外のものは数えない）ので、更新日のないものは 0 件
  CHECK(r.touched.noStamp == 0);
  const WeeklyTouchedItem* f = touchedItem(r, "F");
  REQUIRE(f != nullptr);
  CHECK(f->on == D("2026-10-19"));
  CHECK_FALSE(f->byCreated);
  CHECK(f->item.prog);
  CHECK_FALSE(f->item.done);
  // 完了した課題は「今週の完了」に出ていて、触った課題には出ない
  CHECK(didsOf(p, r.done).size() == 2);
  CHECK(touchedItem(r, "A") == nullptr);
}

TEST_CASE("weekly touched: 更新日のない（読めない）未完了の作業は数えず、件数を注記する") {
  Proj p = Proj::make(basic());
  stampIssue(p, "7", "", "2026-10-21");
  stampIssue(p, "8", "", "");              // 空
  stampIssue(p, "9", "", "いつか");        // 読めない
  stampIssue(p, "10", "", "2026-10-01");   // 期間外（更新日はある）
  stampIssue(p, "2", "", "");              // 完了は数えない
  const WeeklyReport r = touchedReport(p);
  CHECK(r.touched.total == 1);
  CHECK(r.touched.noStamp == 2);
  const std::string md = weeklyMarkdown(r);
  CHECK(has(md, "更新日のない 2 件は数えていません"));
  CHECK(has(weeklyHtml(r), "更新日のない 2 件は数えていません"));
  // 0 件なら注記を出さない
  stampIssue(p, "8", "", "2026-10-02");
  stampIssue(p, "9", "", "2026-10-02");
  CHECK_FALSE(has(weeklyMarkdown(touchedReport(p)), "更新日のない"));
}

TEST_CASE("weekly touched: 年またぎ・JIRA の日時の書式・休業日（土日）の更新も期間に入る") {
  std::vector<Spec> v;
  v.push_back({"1", "全体", "", "To Do", "", "Epic", "", "", "", 1, {}});
  v.push_back({"2", "大晦日", "田中", "In Progress", "1", "Task", "", "", "", 1, {}});
  v.push_back({"3", "元日", "田中", "To Do", "1", "Task", "", "", "", 1, {}});
  v.push_back({"4", "日曜", "鈴木", "To Do", "1", "Task", "", "", "", 1, {}});
  v.push_back({"5", "月曜", "鈴木", "To Do", "1", "Task", "", "", "", 1, {}});
  Proj p = Proj::make(v, "2027-01-06");  // 水曜。今週 2027-01-04〜01-10、先週 2026-12-28〜2027-01-03
  stampIssue(p, "2", "", "31/Dec/26 11:30 PM");  // JIRA の日時の書式
  stampIssue(p, "3", "", "2027-01-01");           // 元日（休業日）
  stampIssue(p, "4", "", "2027-01-03");           // 日曜（先週の最後の日）
  stampIssue(p, "5", "", "2027-01-04");           // 今週の最初の日
  WeeklyOptions last;
  last.period = WeeklyPeriod::LastWeek;
  const WeeklyReport rl = touchedReport(p, last);
  CHECK(rl.range.from == D("2026-12-28"));
  CHECK(rl.range.to == D("2027-01-03"));
  CHECK(touchedNames(p, rl.touched) == std::vector<std::string>{"大晦日", "元日", "日曜"});
  const WeeklyReport rt = touchedReport(p);
  CHECK(touchedNames(p, rt.touched) == std::vector<std::string>{"月曜"});
  WeeklyOptions cu;
  cu.period = WeeklyPeriod::Custom;
  cu.from = D("2026-12-31");
  cu.to = D("2027-01-01");
  CHECK(touchedNames(p, touchedReport(p, cu).touched) == std::vector<std::string>{"大晦日", "元日"});
}

TEST_CASE("weekly touched: 空・列がない・meta がない・項目オフ") {
  Proj p = Proj::make(basic());
  // 列がない: 節は出るが、出せない理由を書く
  const WeeklyReport none = touchedReport(p);
  CHECK(none.touched.available);
  CHECK_FALSE(none.touched.hasColumn);
  CHECK(none.touched.total == 0);
  const std::string md0 = weeklyMarkdown(none);
  CHECK(has(md0, "## 今週触った課題"));
  CHECK(has(md0, "Updated（更新日）"));
  CHECK(has(weeklyHtml(none), "今週触った課題"));
  // 列はあるが期間に入る課題がない
  stampIssue(p, "7", "", "2026-09-01");
  const WeeklyReport empty = touchedReport(p);
  CHECK(empty.touched.hasColumn);
  CHECK(empty.touched.total == 0);
  CHECK(empty.touched.groups.empty());
  const std::string md = weeklyMarkdown(empty);
  CHECK(has(md, "## 今週触った課題（0 件）"));
  CHECK(has(weeklyHtml(empty), "今週触った課題"));
  // meta を渡さない（従来の呼び方）: 節は出ない
  const WeeklyReport legacy = p.report();
  CHECK_FALSE(legacy.touched.available);
  CHECK_FALSE(has(weeklyMarkdown(legacy), "触った課題"));
  CHECK_FALSE(has(weeklyHtml(legacy), "触った課題"));
  // 項目をオフにしたら出ない（計算もしない）
  WeeklyOptions off;
  off.sections.touched = false;
  const WeeklyReport o = touchedReport(p, off);
  CHECK_FALSE(o.touched.available);
  CHECK_FALSE(has(weeklyMarkdown(o), "触った課題"));
  CHECK_FALSE(has(weeklyHtml(o), "触った課題"));
  // 期間の言い方
  WeeklyOptions lw;
  lw.period = WeeklyPeriod::LastWeek;
  CHECK(has(weeklyMarkdown(touchedReport(p, lw)), "## 先週触った課題"));
  WeeklyOptions cu;
  cu.period = WeeklyPeriod::Custom;
  cu.from = D("2026-10-01");
  cu.to = D("2026-10-07");
  CHECK(has(weeklyMarkdown(touchedReport(p, cu)), "## 期間内に触った課題"));
}

TEST_CASE("weekly touched: 項目は既定でオン。any() に入る") {
  CHECK(WeeklySections{}.touched);
  WeeklySections only;
  only.summary = only.done = only.progress = only.next = only.risks = only.milestones = only.assignees = only.changes = only.notes = false;
  only.baseline = only.evm = false;
  only.touched = false;
  CHECK_FALSE(only.any());
  only.touched = true;
  CHECK(only.any());
}

TEST_CASE("weekly touched: 親のまとまりごとに、ルート / 親の順で並べ、親のない課題は最後") {
  std::vector<Spec> v;
  v.push_back({"1", "ルートA", "", "To Do", "", "Epic", "", "", "", 1, {}});
  v.push_back({"2", "親A1", "", "To Do", "1", "Story", "", "", "", 1, {}});
  v.push_back({"3", "子A1a", "田中", "In Progress", "2", "Task", "", "", "", 1, {}});
  v.push_back({"4", "単独", "鈴木", "To Do", "", "Task", "", "", "", 1, {}});
  v.push_back({"5", "ルートB", "", "To Do", "", "Epic", "", "", "", 1, {}});
  v.push_back({"6", "子Bb", "鈴木", "To Do", "5", "Task", "", "", "", 1, {}});
  v.push_back({"7", "子A1b", "田中", "To Do", "2", "Task", "", "", "", 1, {}});
  Proj p = Proj::make(v);
  for (const char* id : {"3", "4", "6", "7"}) stampIssue(p, id, "", "2026-10-21");
  const WeeklyReport r = touchedReport(p);
  REQUIRE(r.touched.groups.size() == 3);
  CHECK(r.touched.total == 4);
  // ルートA / 親A1 の下に 2 件、ルートB の下に 1 件、最後に親のない課題
  CHECK(has(r.touched.groups[0].path, "ルートA"));
  CHECK(has(r.touched.groups[0].path, "親A1"));
  CHECK(r.touched.groups[0].path.find("ルートA") < r.touched.groups[0].path.find("親A1"));
  CHECK(r.touched.groups[0].items.size() == 2);
  CHECK(has(r.touched.groups[1].path, "ルートB"));
  CHECK_FALSE(has(r.touched.groups[1].path, "親A1"));
  CHECK(r.touched.groups[1].items.size() == 1);
  CHECK(r.touched.groups[2].parent == -1);
  CHECK(r.touched.groups[2].path.empty());
  CHECK(r.touched.groups[2].items[0].item.name == "単独");
  const std::string md = weeklyMarkdown(r);
  CHECK(has(md, "### "));
  CHECK(has(md, "親のない課題"));
  CHECK(md.find("ルートB") < md.find("親のない課題"));
  const std::string html = weeklyHtml(r);
  CHECK(has(html, "親のない課題"));
}

TEST_CASE("weekly touched: 上限を超えたら、新しく触った順に残し、ほか N 件を出す") {
  std::vector<Spec> v;
  v.push_back({"1", "全体", "", "To Do", "", "Epic", "", "", "", 1, {}});
  for (int i = 2; i <= 6; ++i) v.push_back({std::to_string(i), "課題" + std::to_string(i), "田中", "To Do", "1", "Task", "", "", "", 1, {}});
  Proj p = Proj::make(v);
  stampIssue(p, "2", "", "2026-10-19");
  stampIssue(p, "3", "", "2026-10-23");
  stampIssue(p, "4", "", "2026-10-21");
  stampIssue(p, "5", "", "2026-10-25");
  stampIssue(p, "6", "", "2026-10-22");
  WeeklyOptions o;
  o.maxItems = 3;
  const WeeklyReport r = touchedReport(p, o);
  CHECK(r.touched.total == 5);
  CHECK(r.touched.shown() == 3);
  CHECK(r.touched.omitted() == 2);
  // 一番新しい 3 件（10/25・10/23・10/22）。表示は Tree の順
  CHECK(touchedNames(p, r.touched) == std::vector<std::string>{"課題3", "課題5", "課題6"});
  CHECK(has(weeklyMarkdown(r), "- ほか 2 件"));
  CHECK(has(weeklyHtml(r), "ほか 2 件"));
  CHECK(has(weeklyMarkdown(r), "## 今週触った課題（5 件）"));
  // 件数が収まるなら出さない
  o.maxItems = 5;
  CHECK_FALSE(has(weeklyMarkdown(touchedReport(p, o)), "ほか"));
}

TEST_CASE("weekly touched: 既定の上限は 50 件") {
  std::vector<Spec> v;
  v.push_back({"1", "全体", "", "To Do", "", "Epic", "", "", "", 1, {}});
  for (int i = 2; i <= 61; ++i) v.push_back({std::to_string(i), "T" + std::to_string(i), "", "To Do", "1", "Task", "", "", "", 1, {}});
  Proj p = Proj::make(v);
  for (int i = 2; i <= 61; ++i) stampIssue(p, std::to_string(i).c_str(), "", "2026-10-20");
  const WeeklyReport r = touchedReport(p);
  CHECK(kWeeklyMaxTouched == 50);
  CHECK(r.touched.total == 60);
  CHECK(r.touched.shown() == 50);
  CHECK(r.touched.omitted() == 10);
  CHECK(has(weeklyMarkdown(r), "ほか 10 件"));
  // 同じ日なら Tree の順で先に出る
  CHECK(touchedItem(r, "T2") != nullptr);
  CHECK(touchedItem(r, "T61") == nullptr);
}

TEST_CASE("weekly touched: 新しく作った課題（Created が期間内）。Updated がなくても Created が期間内なら入る") {
  Proj p = Proj::make(basic());
  stampIssue(p, "7", "2026-10-20", "2026-10-21");   // Updated も Created も期間内
  stampIssue(p, "8", "2026-10-22", "");             // Created だけ（アプリで作った課題）
  stampIssue(p, "9", "2026-10-01", "2026-10-21");   // 以前に作って、今週更新
  stampIssue(p, "10", "2026-10-01", "");            // Created だけで期間外: 更新日がない
  const WeeklyReport r = touchedReport(p);
  CHECK(r.touched.total == 3);
  CHECK(r.touched.noStamp == 1);
  const WeeklyTouchedItem* f = touchedItem(r, "F");
  const WeeklyTouchedItem* g = touchedItem(r, "G");
  const WeeklyTouchedItem* h = touchedItem(r, "H");
  REQUIRE((f && g && h));
  CHECK(f->created);
  CHECK(f->on == D("2026-10-21"));
  CHECK_FALSE(f->byCreated);
  CHECK(g->created);
  CHECK(g->byCreated);
  CHECK(g->on == D("2026-10-22"));
  CHECK_FALSE(h->created);
  const std::string md = weeklyMarkdown(r);
  CHECK(has(touchedLine(md, "F"), "・更新日 10/21(水)・新規作成）"));
  CHECK(has(touchedLine(md, "G"), "・作成日 10/22(木)・新規作成）"));  // 更新日がなく、作成日が期間内
  CHECK(has(touchedLine(md, "H"), "・更新日 10/21(水)・更新）"));
}

TEST_CASE("weekly touched: 比べるスナップショットがなければ「更新」とだけ出す。コメントは日付が期間内のときだけ") {
  Proj p = Proj::make(basic());
  stampIssue(p, "7", "", "2026-10-21");
  stampIssue(p, "8", "", "2026-10-22");
  stampIssue(p, "9", "", "2026-10-23");
  for (Issue& o : p.issues) {
    if (o.id == "7") o.comments.push_back({"2026-10-20T09:00", "田中", "確認しました"});  // 期間内
    if (o.id == "8") o.comments.push_back({"2026-10-12T09:00", "田中", "前の週"});         // 期間外
    if (o.id == "9") o.comments.push_back({"", "田中", "日時なし"});                        // 日付なし
  }
  const WeeklyReport r = touchedReport(p);
  REQUIRE(r.touched.total == 3);
  CHECK_FALSE(r.touched.hasBasis);
  const WeeklyTouchedItem* f = touchedItem(r, "F");
  const WeeklyTouchedItem* g = touchedItem(r, "G");
  const WeeklyTouchedItem* h = touchedItem(r, "H");
  REQUIRE((f && g && h));
  CHECK(f->commented);
  CHECK_FALSE(g->commented);
  CHECK_FALSE(h->commented);
  for (const WeeklyTouchedItem* it : {f, g, h}) {
    CHECK_FALSE(it->statusChanged);
    CHECK_FALSE(it->finishMoved);
  }
  const std::string md = weeklyMarkdown(r);
  CHECK(has(touchedLine(md, "G"), "・更新日 10/22(木)・更新）"));
  CHECK(has(touchedLine(md, "F"), "・更新日 10/21(水)・更新・コメントあり）"));
  CHECK(has(touchedLine(md, "H"), "・更新日 10/23(金)・更新）"));
  CHECK(has(md, "比べるスナップショットがない"));
  CHECK(has(md, "コメントあり"));
  // 「コメントあり」は期間内のコメントがある 1 件だけ
  std::size_t n = 0;
  for (std::size_t at = md.find("コメントあり"); at != std::string::npos; at = md.find("コメントあり", at + 1)) ++n;
  CHECK(n == 1);
}

TEST_CASE("weekly touched: スナップショットがあれば、状況と完了見込み日の差を添える") {
  Proj p = Proj::make(basic());
  stampIssue(p, "7", "", "2026-10-21");   // F: 先週は未着手 → いま進行中
  stampIssue(p, "8", "", "2026-10-21");   // G: 見込みが動いた
  stampIssue(p, "9", "", "2026-10-21");   // H: 変わっていない
  stampIssue(p, "10", "", "2026-10-21");  // U: スナップショットにない課題
  WeeklySnapshot s;
  s.base = D("2026-10-19");
  const auto fin = [&](const char* id) { return p.row(id).asapFinish; };
  s.items.push_back({"7", 't', fin("7"), false});
  s.items.push_back({"8", 't', fin("8") - 7, false});  // 1 週間前の見込み
  s.items.push_back({"9", 't', fin("9"), false});
  const WeeklyReport r = touchedReport(p, {}, &s);
  REQUIRE(r.touched.total == 4);
  CHECK(r.touched.hasBasis);
  CHECK(r.touched.since == s.base);
  CHECK_FALSE(r.touched.hasUntil);
  const WeeklyTouchedItem* f = touchedItem(r, "F");
  const WeeklyTouchedItem* g = touchedItem(r, "G");
  const WeeklyTouchedItem* h = touchedItem(r, "H");
  const WeeklyTouchedItem* u = touchedItem(r, "U");
  REQUIRE((f && g && h && u));
  CHECK(f->statusChanged);
  CHECK(f->statusFrom == 't');
  CHECK(f->statusTo == 'p');
  CHECK_FALSE(f->finishMoved);
  CHECK_FALSE(g->statusChanged);
  CHECK(g->finishMoved);
  CHECK(g->finishFrom == fin("8") - 7);
  CHECK(g->finishTo == fin("8"));
  CHECK(g->shiftDays > 0);  // 遅くなった
  CHECK_FALSE(h->statusChanged);
  CHECK_FALSE(h->finishMoved);
  CHECK_FALSE(u->statusChanged);
  CHECK_FALSE(u->finishMoved);
  const std::string md = weeklyMarkdown(r);
  CHECK(has(touchedLine(md, "F"), "・更新日 10/21(水)・状況 未着手 → 進行中）"));
  CHECK(has(touchedLine(md, "G"), "・完了見込み "));
  CHECK(has(touchedLine(md, "G"), "営業日遅くなった）"));
  CHECK(has(touchedLine(md, "H"), "・更新日 10/21(水)・更新）"));
  CHECK(has(touchedLine(md, "U"), "・更新日 10/21(水)・更新）"));
  CHECK(has(md, "状況・日程の変化は、"));
  CHECK(has(md, "2026/10/19(月) に書き出したときの状態との比較です"));
  const std::string html = weeklyHtml(r);
  CHECK(has(html, "状況 未着手 → 進行中"));
  // 新規 + 状況の変化は並べて出す
  stampIssue(p, "7", "2026-10-20", "2026-10-21");
  const WeeklyReport r2 = touchedReport(p, {}, &s);
  CHECK(has(touchedLine(weeklyMarkdown(r2), "F"), "・更新日 10/21(水)・新規作成・状況 未着手 → 進行中）"));
}

TEST_CASE("weekly touched: 自動のスナップショット（週の頭）があれば期間に合わせて比べる。先週は先週の頭から今週の頭まで") {
  Proj p = Proj::make(basic());
  stampIssue(p, "7", "", "2026-10-14");  // 先週に更新
  WeeklySnapshot head;
  head.base = D("2026-10-12");  // 先週の頭
  head.items.push_back({"7", 't', p.row("7").asapFinish, false});
  WeeklySnapshot cur;
  cur.base = D("2026-10-19");  // 今週の頭
  cur.items.push_back({"7", 'p', p.row("7").asapFinish, false});
  WeeklyAuto autos;
  autos.snaps = {head, cur};
  WeeklyInput in = touchedInput(p);
  in.autos = &autos;
  WeeklyOptions lw;
  lw.period = WeeklyPeriod::LastWeek;
  const WeeklyReport r = buildWeeklyReport(in, lw);
  REQUIRE(r.touched.total == 1);
  CHECK(r.touched.hasBasis);
  CHECK(r.touched.sinceAuto);
  CHECK(r.touched.hasUntil);
  CHECK(r.touched.until == cur.base);
  const WeeklyTouchedItem* f = touchedItem(r, "F");
  REQUIRE(f != nullptr);
  CHECK(f->statusChanged);
  CHECK(f->statusTo == 'p');
  CHECK(has(weeklyMarkdown(r), "自動で記録した週の頭の状態"));
}

TEST_CASE("weekly touched: 件名・親・担当は Markdown と HTML でエスケープする") {
  std::vector<Spec> v;
  v.push_back({"1", "親 <b>[x]</b>", "", "To Do", "", "Epic", "", "", "", 1, {}});
  v.push_back({"2", "# 見出し|表 <script>alert(1)</script> *強調*", "田中 <i>", "In Progress", "1", "Task", "", "", "", 1, {}});
  Proj p = Proj::make(v);
  stampIssue(p, "2", "", "2026-10-21");
  const WeeklyReport r = touchedReport(p);
  REQUIRE(r.touched.total == 1);
  const std::string md = weeklyMarkdown(r);
  CHECK(rawLt(md) == 0);
  for (const std::string& l : lines(md))
    if (has(l, "script")) CHECK(rawPipes(l) == 0);
  CHECK(has(md, "\\<script\\>"));
  CHECK(has(md, "\\*強調\\*"));
  CHECK(has(md, "親 \\<b\\>\\[x\\]\\</b\\>"));
  const std::string html = weeklyHtml(r);
  CHECK_FALSE(has(html, "<script>alert"));
  CHECK_FALSE(has(html, "<b>[x]"));
  CHECK_FALSE(has(html, "<i>"));
  CHECK(has(html, "&lt;script&gt;"));
}

TEST_CASE("weekly touched: 節の順は 完了 → 触った課題 → 進行中 → 来週の予定 → 遅れ") {
  Proj p = Proj::make(basic());
  stampIssue(p, "7", "", "2026-10-21");
  const WeeklyReport r = touchedReport(p);
  const std::string md = weeklyMarkdown(r);
  const std::size_t a = md.find("## 今週の完了"), b = md.find("## 今週触った課題"), c = md.find("## 進行中"), d = md.find("## 来週の予定"),
                    e = md.find("## 遅れ・余裕小");
  REQUIRE((a != std::string::npos && b != std::string::npos && c != std::string::npos && d != std::string::npos && e != std::string::npos));
  CHECK(a < b);
  CHECK(b < c);
  CHECK(c < d);
  CHECK(d < e);
  const std::string html = weeklyHtml(r);
  const std::size_t ha = html.find("<h2>今週の完了"), hb = html.find("<h2>今週触った課題"), hc = html.find("<h2>進行中"), hd = html.find("<h2>来週の予定"),
                    he = html.find("<h2>遅れ・余裕小");
  REQUIRE((ha != std::string::npos && hb != std::string::npos && hc != std::string::npos && hd != std::string::npos && he != std::string::npos));
  CHECK(ha < hb);
  CHECK(hb < hc);
  CHECK(hc < hd);
  CHECK(hd < he);
}

TEST_CASE("weekly touched: 入力の食い違いは空のレポートのまま（節は出さない）") {
  Proj p = Proj::make(basic());
  stampIssue(p, "7", "", "2026-10-21");
  std::vector<Issue> few(p.issues.begin(), p.issues.begin() + 2);
  WeeklyInput bad(p.comp.T, few, p.comp.R, p.cal, p.stats, p.load);
  bad.base = p.base;
  bad.meta = &p.meta;
  const WeeklyReport r = buildWeeklyReport(bad, WeeklyOptions{});
  CHECK(r.empty);
  CHECK_FALSE(r.touched.available);
  CHECK_FALSE(has(weeklyMarkdown(r), "触った課題"));
  CHECK_FALSE(has(weeklyHtml(r), "触った課題"));
}
