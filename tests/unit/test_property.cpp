// 性質のテスト（SPEC 12.4）。乱数のシードは固定。失敗したらシードを表示する
#include <doctest/doctest.h>

#include <chrono>
#include <random>

#include "roadmap/commands.hpp"
#include "roadmap/gen.hpp"
#include "roadmap/project_io.hpp"
#include "roadmap/schedule.hpp"

using namespace roadmap;

namespace {
FixedClock clk(LocalTime{2026, 10, 19, 9, 0, 0});

ProjectData dataOf(const Generated& g) {
  ProjectData d;
  d.issues = g.issues;
  d.meta = g.meta;
  d.settings.id = "p";
  d.settings.name = "gen";
  d.settings.baseDate = "2026-10-19";
  d.settings.holidays = defaultHolidaysText();
  d.settings.ui = defaultUi();
  return d;
}
}  // namespace

TEST_CASE("性質: 往復の安定 export(import(export(x))) == export(x)") {
  for (std::uint64_t seed = 1; seed <= 30; ++seed) {
    CAPTURE(seed);
    auto g = generateProject({60, seed});
    const std::string a = exportAppCsv(g.issues, g.meta);
    auto imp = csv::importCsv(a);
    REQUIRE(imp.ok());
    CHECK(imp->warns.empty());
    CHECK(exportAppCsv(imp->issues, imp->meta) == a);
  }
}

TEST_CASE("性質: Undo の完全性（任意の操作列のあと全部戻すと初期状態）") {
  for (std::uint64_t seed = 1; seed <= 25; ++seed) {
    CAPTURE(seed);
    auto g = generateProject({40, seed});
    Session s(dataOf(g), &clk);
    REQUIRE(s.error().empty());
    const std::string initial = s.exportText(ExportKind::App);
    std::mt19937_64 rng(seed * 7919);
    const auto pick = [&]() { return s.issues()[rng() % s.issues().size()].id; };
    for (int step = 0; step < 60 && !s.issues().empty(); ++step) {
      switch (rng() % 14) {
        case 0: s.addIssue(rng() % 2 ? pick() : "", rng() % 2 ? pick() : ""); break;
        case 1: s.deleteIssues({pick()}); break;
        case 2: s.moveIssue(pick(), rng() % 2 ? 1 : -1); break;
        case 3: s.indent(pick()); break;
        case 4: s.outdent(pick()); break;
        case 5: s.relocate(pick(), pick(), static_cast<RelocateWhere>(rng() % 3)); break;
        case 6: s.setStatus(pick(), rng() % 2 ? "Done" : "In Progress"); break;
        case 7: s.setDuration(pick(), static_cast<double>(rng() % 6) * 0.5); break;
        case 8: s.addLink(pick(), pick()); break;
        case 9: s.setName(pick(), "名" + std::to_string(step)); break;
        case 10: s.setDeadline(pick(), daysFromCivil(2026, 12, 1) + static_cast<int>(rng() % 60)); break;
        case 11: s.bulk({pick(), pick()}, [](Issue& o) { o.prio = "Low"; }); break;
        case 12: s.applyTemplate({pick()}, builtinTemplates()[rng() % 4]); break;
        default: s.setEarliest(pick(), daysFromCivil(2026, 11, 2) + static_cast<int>(rng() % 30)); break;
      }
      REQUIRE(s.error().empty());
    }
    while (s.undo()) {
    }
    CHECK(s.exportText(ExportKind::App) == initial);
  }
}

TEST_CASE("性質: 計算の不変条件・資源制限の単調性") {
  for (std::uint64_t seed = 1; seed <= 30; ++seed) {
    CAPTURE(seed);
    auto g = generateProject({80, seed});
    const Calendar cal = Calendar::fromText(defaultHolidaysText());
    ScheduleOptions o;
    o.base = daysFromCivil(2026, 10, 19);
    auto off = computeAll(g.issues, g.meta, cal, o);
    REQUIRE(off.ok());
    const Tree& T = off->T;
    for (int id : T.leafOrder) {
      const auto& r = off->R[id];
      CHECK_FALSE(r.waited);
      if (r.done) continue;
      CHECK(r.asapStart <= r.asapFinish);
      CHECK(r.alapStart <= r.alapFinish);
      CHECK((r.floatDays >= 0) == (r.alapFinish >= r.asapFinish));
      // 実績で固定された課題（進行中で開始日が基準日以前）は先行を待たない（JS版と同じ）
      for (int p : T.preds[id]) {
        if (off->R[p].done || r.pinned) continue;
        const bool zw = T.kind[id] == Kind::Ms && r.weeks == 0;
        if (zw) CHECK(r.asapStart >= off->R[p].asapFinish);
        else CHECK(r.asapStart > off->R[p].asapFinish);
      }
    }
    for (int cap : {1, 2}) {
      o.resource = {true, cap};
      auto on = computeAll(g.issues, g.meta, cal, o);
      REQUIRE(on.ok());
      for (int id : T.leafOrder) {
        CHECK(on->R[id].asapFinish >= off->R[id].asapFinish);
        CHECK(on->R[id].freeStart == off->R[id].asapStart);
      }
    }
  }
}

TEST_CASE("性質: フィルタの整合（可視な末端の祖先はすべて可視）") {
  for (std::uint64_t seed = 1; seed <= 20; ++seed) {
    CAPTURE(seed);
    auto g = generateProject({80, seed});
    Session s(dataOf(g), &clk);
    REQUIRE(s.tree());
    const Tree& T = *s.tree();
    std::vector<Filter> fs(5);
    fs[1].q = "設計";
    fs[2].asg = AsgFilter::Name;
    fs[2].asgName = "田中";
    fs[3].st = StFilter::Open;
    fs[3].flt = FltFilter::Tight;
    fs[4].pr = "high";
    fs[4].asg = AsgFilter::Unassigned;
    for (const auto& f : fs) {
      const auto vis = visSet(T, s.issues(), *s.rows(), f);
      for (int id = 0; id < T.n; ++id)
        if (vis[id])
          for (int p = T.parentOf[id]; p >= 0; p = T.parentOf[p]) CHECK(vis[p]);
    }
  }
}

TEST_CASE("性能: 5,000件の計算（リリースビルドで 50ms / 制限ありで 200ms 以内）") {
  auto g = generateProject({5000, 42});
  const Calendar cal = Calendar::fromText(defaultHolidaysText());
  ScheduleOptions o;
  o.base = daysFromCivil(2026, 10, 19);
  const auto t0 = std::chrono::steady_clock::now();
  auto c = computeAll(g.issues, g.meta, cal, o);
  const auto t1 = std::chrono::steady_clock::now();
  REQUIRE(c.ok());
  o.resource = {true, 1};
  auto c2 = computeAll(g.issues, g.meta, cal, o);
  const auto t2 = std::chrono::steady_clock::now();
  REQUIRE(c2.ok());
  const auto ms1 = std::chrono::duration<double, std::milli>(t1 - t0).count();
  const auto ms2 = std::chrono::duration<double, std::milli>(t2 - t1).count();
  MESSAGE("5000件: 制限なし " << ms1 << "ms / 制限あり " << ms2 << "ms");
#if defined(NDEBUG) && !defined(ROADMAP_SANITIZED)
  CHECK(ms1 < 50);
  CHECK(ms2 < 200);
#endif
}
