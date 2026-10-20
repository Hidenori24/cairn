#include <doctest/doctest.h>

#include "app_controller.hpp"
#include "roadmap/samples.hpp"

using namespace roadmap;
using namespace roadmap::app;

namespace {
struct Env {
  MemFileStore fs;
  FixedClock clk{LocalTime{2026, 10, 19, 18, 36, 0}};
  AppController c{fs, clk, "data", "roadmap_prefs.json"};
  void step(std::int64_t ms) {
    clk.advanceMs(ms);
    c.tick();
  }
};
}  // namespace

TEST_CASE("controller: 初回（フォルダが空）→ サンプルを作る → 400ms 後に保存") {
  Env e;
  e.c.start();
  CHECK(e.c.firstRun());
  CHECK(e.c.session() == nullptr);
  e.c.createProject(SampleKind::Regulation, "サンプル（法規対応）", "");
  REQUIRE(e.c.session());
  CHECK_FALSE(e.c.firstRun());
  CHECK(e.c.saveState() == SaveState::Saving);
  e.step(100);
  CHECK_FALSE(e.fs.exists("data/サンプル（法規対応）.csv"));
  e.step(400);
  CHECK(e.fs.exists("data/サンプル（法規対応）.csv"));
  CHECK(e.c.saveState() == SaveState::Saved);
  CHECK(e.c.savedAt() == "18:36");
  CHECK(e.c.prefs().currentProject == e.c.currentId());
  CHECK(e.fs.exists("roadmap_prefs.json"));
}

TEST_CASE("controller: 編集 → 保存・失敗したら 2→5→10秒で再試行") {
  Env e;
  e.c.start();
  e.c.createProject(SampleKind::Empty, "P", "dev");
  e.step(500);
  CHECK(e.c.session()->settings().key == "DEV");
  e.fs.setFailWrites(true);
  e.c.session()->addIssue("", "", "A");
  e.c.afterEdit();
  e.step(500);
  CHECK(e.c.saveState() == SaveState::Error);
  CHECK_FALSE(e.c.saveError().empty());
  CHECK(e.c.hasUnsaved());
  e.step(1900);
  CHECK(e.c.saveState() == SaveState::Error);
  e.fs.setFailWrites(false);
  e.step(200);  // 2秒後の再試行
  CHECK(e.c.saveState() == SaveState::Saved);
  CHECK_FALSE(e.c.hasUnsaved());
}

TEST_CASE("controller: 選択（クリック・Ctrl・Shift・矢印）と一括操作は Undo 1回") {
  Env e;
  e.c.start();
  e.c.createProject(SampleKind::Hierarchy, "H", "");
  Session* s = e.c.session();
  std::vector<std::string> order;
  for (const auto& r : s->visibleRows(e.c.filter())) order.push_back(s->issues()[r.id].id);
  e.c.select(order[2]);
  e.c.rangeSelect(order[5], order);
  CHECK(e.c.selection().size() == 4);
  e.c.toggleSelect(order[3]);
  CHECK(e.c.selection().size() == 3);
  e.c.moveCursor(1, false, order);
  CHECK(e.c.selection().size() == 1);
  e.c.moveCursor(1, true, order);
  CHECK(e.c.selection().size() == 2);
  // 一括の状況変更
  const auto before = s->exportText(ExportKind::App);
  e.c.setStatusFor({"12", "13", "14"}, "Done");
  CHECK(s->issue("13")->status == "Done");
  e.c.undo();
  CHECK(s->exportText(ExportKind::App) == before);
  e.c.setSelection({"12", "13"}, "13");
  e.c.indentSelection();
  CHECK(s->issue("13")->parent == "12");
  e.c.undo();
  CHECK(s->exportText(ExportKind::App) == before);
}

TEST_CASE("controller: CSV・JSON の取り込みと競合") {
  Env e;
  e.c.start();
  auto r = e.c.importText("Summary\nA\nB\n", "取り込み", ImportMode::New);
  CHECK(r.ok);
  REQUIRE(e.c.session());
  CHECK(e.c.session()->issues().size() == 2);
  auto bad = e.c.importText("x,y\n1,2\n", "", ImportMode::New);
  CHECK_FALSE(bad.ok);
  CHECK(bad.error == "「Summary」列が見つかりません。1行目に列名があるCSVを読み込んでください。");
  const std::string share = e.c.exportText("share");
  auto j = e.c.importText(share, "", ImportMode::New);
  CHECK(j.isJson);
  REQUIRE(j.conflictNames.size() == 1);
  CHECK(e.c.confirmJsonImport(false));
  CHECK(e.c.projects().size() == 2);
  auto rep = e.c.importText("Summary\nZ\n", "", ImportMode::Replace);
  CHECK(rep.ok);
  CHECK(e.c.session()->issues().size() == 1);
  e.c.undo();
  CHECK(e.c.session()->issues().size() == 2);
}

TEST_CASE("controller: 外部変更の取り込み（アクティブに戻ったとき）") {
  Env e;
  e.c.start();
  e.c.createProject(SampleKind::Empty, "P", "");
  e.step(500);
  e.fs.put("data/P.csv", "Summary\nExcelで追加\n");
  e.clk.advanceMs(1000);
  e.c.onActivated();  // 3秒未満なので何もしない
  CHECK(e.c.session()->issues().empty());
  e.clk.advanceMs(3000);
  e.c.onActivated();
  REQUIRE(e.c.session());
  CHECK(e.c.session()->issues().size() == 1);
  CHECK_FALSE(e.c.session()->canUndo());
}

TEST_CASE("controller: 削除・複製・テンプレートの保存") {
  Env e;
  e.c.start();
  e.c.createProject(SampleKind::Hierarchy, "H", "");
  e.step(500);
  e.c.duplicateProject();
  CHECK(e.c.session()->settings().name == "H のコピー");
  e.step(500);
  CHECK(e.c.deleteCurrentProject());
  CHECK(e.c.projects().size() == 1);
  CHECK(e.fs.exists("data/_削除済み/H のコピー.20261019-183600.csv"));
  const Session* s = e.c.session();
  auto items = templateItemsFrom(*s->tree(), s->issues(), s->find("11"), true);
  const auto id = e.c.saveTemplate("実装の雛形", "", items);
  CHECK(id.rfind("u:", 0) == 0);
  CHECK(e.c.templates().size() == builtinTemplates().size() + 1);
  e.c.deleteTemplate(id);
  CHECK(e.c.templates().size() == builtinTemplates().size());
}

TEST_CASE("controller: 上の絞り込みは薄くするだけ・検索語と列の絞り込みは隠す") {
  Env e;
  e.c.start();
  e.c.createProject(SampleKind::Hierarchy, "H", "");
  Session* s = e.c.session();
  const std::size_t all = e.c.visibleRows().size();
  CHECK(e.c.dimmedSet().empty());
  e.c.filter().st = StFilter::Done;  // 状況で絞る → 行は減らず、一致しない末端が薄くなる
  CHECK(e.c.visibleRows().size() == all);
  const auto dim = e.c.dimmedSet();
  REQUIRE(dim.size() == static_cast<std::size_t>(s->tree()->n));
  int nDim = 0;
  for (int id : s->tree()->leafOrder) nDim += dim[id] ? 1 : 0;
  CHECK(nDim > 0);
  CHECK(nDim < static_cast<int>(s->tree()->leafOrder.size()));
  CHECK(e.c.filterActive());
  // 列の絞り込みは隠す
  e.c.columnFilter().toggle("status", "Done");
  CHECK(e.c.visibleRows().size() < all);
  for (int id : e.c.visibleLeaves()) CHECK(columnValue("status", s->issues()[id], (*s->rows())[id]) != "Done");
  CHECK(e.c.filterLabel() == "列・状況");
  e.c.clearFilter();
  CHECK(e.c.visibleRows().size() == all);
  CHECK_FALSE(e.c.filterActive());
  // 別のプロジェクトを開くと列の絞り込みは解除
  e.c.columnFilter().toggle("asg", kUnassignedValue);
  const std::string first = e.c.currentId();
  e.c.createProject(SampleKind::Regulation, "R", "");
  CHECK_FALSE(e.c.columnFilter().active());
  CHECK(e.c.currentId() != first);
}
