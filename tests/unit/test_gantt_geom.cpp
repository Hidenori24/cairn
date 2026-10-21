// ガントの座標計算（画面と書き出しが共有する）
#include <doctest/doctest.h>

#include "roadmap/csv.hpp"
#include "roadmap/gantt_geom.hpp"
#include "roadmap/schedule.hpp"

using namespace roadmap;

namespace {

const char* const kCsv =
    "Issue Id,Issue Type,Summary,Parent,Assignee,Status,Priority,Resolved,Duration (weeks),Deadline,Outward issue link (Blocks)\n"
    "1,Epic,全体,,,,High,,,,\n"
    "2,Task,A,1,田中,Done,High,2026-10-13,1,,3\n"
    "3,Task,B,1,田中,In Progress,Highest,,2,2026-11-06,4\n"
    "4,Task,C,1,鈴木,To Do,Medium,,2,,5\n"
    "5,Milestone,完了,1,,,High,,0,,\n"
    "6,Task,単独,,鈴木,To Do,Medium,,1,,\n";

struct Fx {
  csv::ImportResult imp;
  Computed comp;
  Day base = 0;
  Fx() {
    auto r = csv::importCsv(kCsv);
    REQUIRE(r.ok());
    imp = std::move(r).value();
    ScheduleOptions opt;
    opt.base = *parseIso("2026-10-19");
    base = opt.base;
    auto c = computeAll(imp.issues, imp.meta, Calendar::fromText(defaultHolidaysText()), opt);
    REQUIRE(c.ok());
    comp = std::move(c).value();
  }
  int idx(const char* id) const { return comp.T.find(id); }
  std::vector<VisibleRow> allRows() const {
    std::vector<VisibleRow> v;
    for (int id : comp.T.dfs) v.push_back({id, comp.T.depth[static_cast<std::size_t>(id)], !comp.T.kids[static_cast<std::size_t>(id)].empty(), true});
    return v;
  }
};

}  // namespace

TEST_CASE("gantt_geom: 左端は lo を含む週の月曜の1週前") {
  const Day mon = *parseIso("2026-10-19");  // 月曜
  CHECK(ganttAxisStart(mon) == mon - 7);
  CHECK(dow(ganttAxisStart(mon)) == 1);
  CHECK(ganttAxisStart(mon + 3) == mon - 7);  // 木曜 → 同じ週
  CHECK(ganttAxisStart(mon + 6) == mon - 7);  // 日曜
  CHECK(ganttAxisStart(mon + 7) == mon);
}

TEST_CASE("gantt_geom: 日付 → x は左端からの日数 × 1日の幅") {
  GanttAxis ax;
  ax.rs = 100;
  ax.hi = 200;
  ax.px = 6;
  ax.left = 590;
  CHECK(ax.x(100) == doctest::Approx(590));
  CHECK(ax.x(101) == doctest::Approx(596));
  CHECK(ax.x(110) == doctest::Approx(650));
  CHECK(ax.x(99) == doctest::Approx(584));  // 左端より前は左へ
}

TEST_CASE("gantt_geom: 拡大率 → 1日の幅（日22・週6・月2.4・全体は幅に合わせて 0.9〜12）") {
  CHECK(ganttZoomPx("day", 1.f, 0, 100) == doctest::Approx(22));
  CHECK(ganttZoomPx("week", 1.f, 0, 100) == doctest::Approx(6));
  CHECK(ganttZoomPx("month", 1.f, 0, 100) == doctest::Approx(2.4));
  CHECK(ganttZoomPx("day", 2.f, 0, 100) == doctest::Approx(44));  // 倍率
  CHECK(ganttZoomPx("???", 1.f, 0, 100) == doctest::Approx(6));   // 不明は週
  CHECK(ganttZoomPx("all", 1.f, 500, 250) == doctest::Approx(2.0));
  CHECK(ganttZoomPx("all", 1.f, 50, 250) == doctest::Approx(0.9));    // 下限
  CHECK(ganttZoomPx("all", 1.f, 5000, 10) == doctest::Approx(12));    // 上限
  CHECK(ganttZoomPx("all", 1.f, 500, 0) > 0);                         // 0 日でも割り算で落ちない
}

TEST_CASE("gantt_geom: 見出しの書き方・高さ・週ラベルの間引き") {
  CHECK(ganttStyleZoom("week", 0) == "week");
  CHECK(ganttStyleZoom("week", 20) == "day");    // Ctrl+ホイールで広げた
  CHECK(ganttStyleZoom("day", 5) == "week");
  CHECK(ganttStyleZoom("day", 2) == "month");
  CHECK(ganttHeaderH("day", 1.f) == doctest::Approx(46));
  CHECK(ganttHeaderH("week", 2.f) == doctest::Approx(92));
  CHECK(ganttHeaderH("month", 1.f) == doctest::Approx(30));
  CHECK(ganttHeaderH("all", 1.f) == doctest::Approx(30));
  CHECK(ganttWeekLabelEvery(20, 1.f) == 3);
  CHECK(ganttWeekLabelEvery(30, 1.f) == 2);
  CHECK(ganttWeekLabelEvery(42, 1.f) == 1);
}

TEST_CASE("gantt_geom: 時間軸の範囲（基準日+120 か各バー+21 日の遠い方）") {
  Fx f;
  const GanttDayRange g = ganttDayRange(f.comp.T, f.comp.R, f.base);
  CHECK(g.lo <= f.base);
  CHECK(g.hi >= f.base + 120);
  for (int id : f.comp.T.leafOrder) {
    CHECK(g.lo <= f.comp.R[static_cast<std::size_t>(id)].asapStart);
    CHECK(g.hi >= f.comp.R[static_cast<std::size_t>(id)].asapFinish + 21);
  }
  // 課題がなくても基準日から 120 日
  const Tree empty;
  const GanttDayRange e = ganttDayRange(empty, {}, f.base);
  CHECK(e.lo == f.base);
  CHECK(e.hi == f.base + 120);
}

TEST_CASE("gantt_geom: 行の形（作業・まとめ・マイルストーン・期限・余裕の影）") {
  Fx f;
  const Tree& T = f.comp.T;
  GanttAxis ax;
  ax.rs = ganttAxisStart(ganttDayRange(T, f.comp.R, f.base).lo);
  ax.px = 6;
  ax.left = 100;

  const int b = f.idx("3");
  const ScheduleRow& rb = f.comp.R[static_cast<std::size_t>(b)];
  const GanttRowShape sb = ganttRowShape(ax, T, rb, b, false);
  CHECK(sb.kind == GanttRowShape::Kind::Task);
  CHECK(sb.x0 == doctest::Approx(ax.x(rb.asapStart)));
  CHECK(sb.x1 - sb.x0 == doctest::Approx((rb.asapFinish - rb.asapStart + 1) * 6.0));
  CHECK(sb.hasDeadline);                           // B は期限あり
  CHECK(sb.dlX == doctest::Approx(ax.x(*T.ownDl[static_cast<std::size_t>(b)]) + 3.0));
  if (rb.alapFinish > rb.asapFinish) {
    CHECK(sb.hasSlack);
    CHECK(sb.slackX0 == doctest::Approx(ax.x(rb.asapFinish + 1)));
    CHECK(sb.slackX1 == doctest::Approx(ax.x(rb.alapFinish + 1)));
  }

  // 完了した作業には余裕の影が出ない
  const int a = f.idx("2");
  const GanttRowShape sa = ganttRowShape(ax, T, f.comp.R[static_cast<std::size_t>(a)], a, false);
  CHECK_FALSE(sa.hasSlack);
  CHECK_FALSE(sa.hasOver);

  // マイルストーンは最早完了の日の中央
  const int m = f.idx("5");
  const ScheduleRow& rm = f.comp.R[static_cast<std::size_t>(m)];
  const GanttRowShape sm = ganttRowShape(ax, T, rm, m, false);
  CHECK(sm.kind == GanttRowShape::Kind::Milestone);
  CHECK(sm.msX == doctest::Approx(ax.x(rm.asapFinish) + 3.0));

  // 親はまとめバー（配下の最早開始〜最早完了+1）
  const int e = f.idx("1");
  const ScheduleRow& re = f.comp.R[static_cast<std::size_t>(e)];
  const GanttRowShape se = ganttRowShape(ax, T, re, e, true);
  CHECK(se.kind == GanttRowShape::Kind::Summary);
  CHECK(se.x0 == doctest::Approx(ax.x(re.asapStart)));
  CHECK(se.x1 - se.x0 >= 3.0);

  // 最小幅 3
  const GanttSpan tiny = ganttTaskBar(ax, 500, 500);
  CHECK(tiny.xr - tiny.xl == doctest::Approx(6));  // 1 日 = 6
  GanttAxis thin = ax;
  thin.px = 0.9f;
  const GanttSpan tiny2 = ganttTaskBar(thin, 500, 500);
  CHECK(tiny2.xr - tiny2.xl == doctest::Approx(3));
}

TEST_CASE("gantt_geom: 依存の矢印の経路（右へ → 縦 → 右へ / 迂回）") {
  GanttArrowEnds e;
  e.x0 = 100;
  e.y0 = 13;
  e.x1 = 200;
  e.y1 = 65;
  auto p = ganttArrowRoute(e, 26.f, 1.f);
  REQUIRE(p.size() == 4);
  CHECK(p[0].x == doctest::Approx(100));
  CHECK(p[1].x == doctest::Approx(106));
  CHECK(p[2].x == doctest::Approx(106));
  CHECK(p[2].y == doctest::Approx(65));
  CHECK(p[3].x == doctest::Approx(200));
  CHECK(p[3].y == doctest::Approx(65));
  // 終点が近い・左にある: 半行ぶん縦に出て迂回する（6 点）
  e.x1 = 105;
  p = ganttArrowRoute(e, 26.f, 1.f);
  REQUIRE(p.size() == 6);
  CHECK(p[2].y == doctest::Approx(13 + 13));  // 下向き → 半行下
  CHECK(p[3].x == doctest::Approx(99));
  CHECK(p[5].x == doctest::Approx(105));
  e.y1 = -20;  // 上向き
  p = ganttArrowRoute(e, 26.f, 1.f);
  CHECK(p[2].y == doctest::Approx(13 - 13));
  // どの線分も水平か垂直（切り出しの前提）
  for (std::size_t i = 0; i + 1 < p.size(); ++i) CHECK((p[i].x == p[i + 1].x || p[i].y == p[i + 1].y));
}

TEST_CASE("gantt_geom: 矢印の集約（同じ行 → 行は 1 本）・折りたたみは親の行へ") {
  Fx f;
  const Tree& T = f.comp.T;
  // 全部開いた状態: 2→3, 3→4, 4→5（葉どうし）
  const auto rows = f.allRows();
  const auto rowOf = ganttRowIndex(T, rows);
  auto links = ganttLinks(T, f.comp.R, rowOf, 0, static_cast<int>(rows.size()));
  CHECK(links.size() == 3);
  for (const auto& l : links) CHECK(l.a != l.b);
  for (std::size_t i = 1; i < links.size(); ++i) CHECK((links[i - 1].a < links[i].a || (links[i - 1].a == links[i].a && links[i - 1].b < links[i].b)));

  // Epic（1）を畳んだ行だけ: 配下どうしの矢印は同じ行になるので出ない
  std::vector<VisibleRow> folded;
  for (const auto& r : rows)
    if (T.depth[static_cast<std::size_t>(r.id)] == 0) folded.push_back(r);
  const auto rowOf2 = ganttRowIndex(T, folded);
  CHECK(ganttLinks(T, f.comp.R, rowOf2, 0, static_cast<int>(folded.size())).empty());
  CHECK(ganttVisibleRow(T, rowOf2, f.idx("3")) == rowOf2[static_cast<std::size_t>(f.idx("1"))]);
  CHECK(ganttVisibleRow(T, rowOf2, f.idx("6")) == rowOf2[static_cast<std::size_t>(f.idx("6"))]);

  // 行の範囲の外にかかるものは数えない
  CHECK(ganttLinks(T, f.comp.R, rowOf, 100, 200).empty());
}

TEST_CASE("gantt_geom: 矢印の両端（マイルストーンは菱形の外側）") {
  Fx f;
  const Tree& T = f.comp.T;
  GanttAxis ax;
  ax.rs = ganttAxisStart(ganttDayRange(T, f.comp.R, f.base).lo);
  ax.px = 6;
  ax.left = 0;
  const int c = f.idx("4"), m = f.idx("5");
  const GanttArrowEnds e = ganttArrowEnds(ax, T, f.comp.R, c, m, 10, 40, 1.f);
  CHECK(e.x0 == doctest::Approx(ax.x(f.comp.R[static_cast<std::size_t>(c)].asapFinish + 1)));
  CHECK(e.x1 == doctest::Approx(ax.x(f.comp.R[static_cast<std::size_t>(m)].asapStart) + 3.0 - 7.0));
  CHECK(e.y0 == doctest::Approx(10));
  CHECK(e.y1 == doctest::Approx(40));
}

TEST_CASE("gantt_geom: 色の判断（Epic 色・余裕・担当者のハッシュ）") {
  CHECK(ganttColorByFromKey("root") == GanttColorBy::Root);
  CHECK(ganttColorByFromKey("float") == GanttColorBy::Float);
  CHECK(ganttColorByFromKey("asg") == GanttColorBy::Assignee);
  CHECK(ganttColorByFromKey("???") == GanttColorBy::Root);
  CHECK(std::string(ganttColorByKey(GanttColorBy::Assignee)) == "asg");

  auto c = ganttBarColor(GanttColorBy::Float, Risk::Late, 3, "");
  CHECK(c.kind == GanttBarColor::Kind::Late);
  c = ganttBarColor(GanttColorBy::Float, Risk::Tight, 3, "");
  CHECK(c.kind == GanttBarColor::Kind::TightEdge);
  c = ganttBarColor(GanttColorBy::Float, Risk::Ok, 3, "");
  CHECK(c.kind == GanttBarColor::Kind::Accent);
  c = ganttBarColor(GanttColorBy::Float, Risk::Done, 3, "");
  CHECK(c.kind == GanttBarColor::Kind::Palette);
  CHECK(c.index == 0);
  c = ganttBarColor(GanttColorBy::Root, Risk::Ok, 5, "x");
  CHECK(c.index == 5);
  // 担当者は前後の空白を除いた名前のハッシュ mod 8（h = h*31 + ch）
  c = ganttBarColor(GanttColorBy::Assignee, Risk::Ok, 5, "  ab ");
  CHECK(c.index == static_cast<int>((('a' * 31u) + 'b') % 8));
  CHECK(nameHash("ab") == 'a' * 31u + 'b');
  CHECK(nameHash("") == 0);
}

TEST_CASE("gantt_geom: ルート課題の色番号（子を持つか Epic のルートだけ 1〜7）") {
  Fx f;
  const auto m = rootIndexMap(f.comp.T, f.imp.issues);
  CHECK(m[static_cast<std::size_t>(f.idx("1"))] == 1);
  CHECK(m[static_cast<std::size_t>(f.idx("3"))] == 1);  // 配下は根と同じ
  CHECK(m[static_cast<std::size_t>(f.idx("6"))] == 0);  // 子を持たない単独の作業
}
