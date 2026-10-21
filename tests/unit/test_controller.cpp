#include <doctest/doctest.h>

#include "app_controller.hpp"
#include "roadmap/report.hpp"
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
  e.c.createProject(SampleKind::Hierarchy, "サンプル", "");
  REQUIRE(e.c.session());
  CHECK_FALSE(e.c.firstRun());
  CHECK(e.c.saveState() == SaveState::Saving);
  e.step(100);
  CHECK_FALSE(e.fs.exists("data/サンプル/project.csv"));
  e.step(400);
  CHECK(e.fs.exists("data/サンプル/project.csv"));
  CHECK(e.c.saveState() == SaveState::Saved);
  CHECK(e.c.savedAt() == "18:36");
  CHECK(e.c.prefs().currentProject == e.c.currentId());
  CHECK(e.fs.exists("roadmap_prefs.json"));
}

TEST_CASE("controller: 組込みソフトウェア開発の雛形は今日を基準日に、マイルストーン4つで作る") {
  Env e;
  e.c.start();
  e.c.createProject(SampleKind::EmbeddedSw, "組込み", "dev");
  Session* s = e.c.session();
  REQUIRE(s);
  CHECK(s->settings().key == "DEV");
  CHECK(s->settings().baseDate == "2026-10-19");  // 雛形は日付を持たないので、今日から積む
  CHECK(s->summary().total > 20);
  const Tree* T = s->tree();
  REQUIRE(T);
  int ms = 0;
  for (int id : T->dfs)
    if (T->isLeaf(id) && T->kind[id] == Kind::Ms) ++ms;
  CHECK(ms == 4);
  CHECK(s->error().empty());
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
  e.fs.put("data/P/project.csv", "Summary\nExcelで追加\n");
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
  CHECK(e.fs.exists("data/_削除済み/H のコピー.20261019-183600/project.csv"));
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
  e.c.createProject(SampleKind::Empty, "R", "");
  CHECK_FALSE(e.c.columnFilter().active());
  CHECK(e.c.currentId() != first);
}

TEST_CASE("controller: 保存先の切り替え（コピーする・しない・既定に戻す・決められているときは変えない）") {
  Env e;
  e.c.setDataDirInfo({"app/data/projects", false, DataDirSource::Default, false}, "app");
  e.c.start();
  e.c.createProject(SampleKind::Hierarchy, "H", "");
  e.c.createProject(SampleKind::Empty, "R", "");
  REQUIRE(e.c.flushNow());
  CHECK(e.c.defaultDir() == "app/data/projects");
  CHECK_FALSE(e.c.dataDirLocked());
  CHECK(e.c.countProjectsIn("data") == 2);

  // コピーして切り替える → 新しい場所に2件。設定には exe からの相対で覚える
  REQUIRE(e.c.switchDataDir("app/mine", true));
  CHECK(e.c.dataDir() == "app/mine");
  CHECK(e.c.projects().size() == 2);
  CHECK(e.c.countProjectsIn("app/mine") == 2);
  CHECK(e.c.countProjectsIn("data") == 2);  // 元の場所は消さない
  CHECK(e.c.prefs().dataDir == "mine");
  CHECK(e.c.dataDirInfo().source == DataDirSource::Prefs);
  CHECK(loadPrefs(e.fs, "roadmap_prefs.json").dataDir == "mine");
  REQUIRE(e.c.session());
  CHECK(e.c.session()->settings().name == "R");  // 開いていたプロジェクトを開き直す
  e.c.releaseFrame();

  // コピーしないで空の場所へ → はじめに
  REQUIRE(e.c.switchDataDir("D:/empty", false));
  CHECK(e.c.projects().empty());
  CHECK(e.c.firstRun());
  CHECK(e.c.session() == nullptr);
  CHECK(e.c.prefs().dataDir == "D:/empty");
  e.c.releaseFrame();

  // 同じプロジェクトがある場所へコピー → 場所の方を残す（増えない）
  e.c.createProject(SampleKind::Empty, "E", "");
  REQUIRE(e.c.switchDataDir("app/mine", true));
  CHECK(e.c.projects().size() == 3);
  REQUIRE(e.c.switchDataDir("app/mine", true));
  CHECK(e.c.projects().size() == 3);

  // 既定に戻す → 設定は空
  REQUIRE(e.c.switchDataDir(e.c.defaultDir(), false));
  CHECK(e.c.prefs().dataDir.empty());
  CHECK(e.c.dataDirInfo().source == DataDirSource::Default);

  // 書けない場所には切り替えない
  e.fs.setFailWrites(true);
  CHECK_FALSE(e.c.switchDataDir("X:/ro", true));
  CHECK(e.c.dataDir() == "app/data/projects");
  e.fs.setFailWrites(false);

  // 起動引数で決まっているときは変えない
  e.c.setDataDirInfo({"arg", false, DataDirSource::Arg, false}, "app");
  CHECK(e.c.dataDirLocked());
  CHECK_FALSE(e.c.switchDataDir("app/other", false));
}

TEST_CASE("controller: 開いたら基準日を今日に合わせる（アプリの設定。プロジェクトごとに起動中1回）") {
  Env e;
  e.c.start();
  const std::string a = e.c.createProject(SampleKind::Empty, "A", "");
  e.c.session()->setBaseDate("2026-09-01");
  e.c.afterEdit();
  const std::string b = e.c.createProject(SampleKind::Empty, "B", "");
  REQUIRE(e.c.flushNow());
  // 設定がオフなら変えない
  e.c.openProject(a);
  CHECK(e.c.session()->settings().baseDate == "2026-09-01");
  e.c.prefs().autoBaseToday = true;
  e.c.openProject(b);
  e.c.openProject(a);
  CHECK(e.c.session()->settings().baseDate == "2026-10-19");
  CHECK(e.c.toasts().back().text.find("2026-09-01") != std::string::npos);  // 前の日付を知らせる
  // 同じ起動中は1回だけ（手で戻した日付をまた変えない）
  e.c.session()->setBaseDate("2026-09-01");
  e.c.afterEdit();
  e.c.openProject(b);
  e.c.openProject(a);
  CHECK(e.c.session()->settings().baseDate == "2026-09-01");
  // 新しい起動（作り直し）では、前回の日付から今日へ
  REQUIRE(e.c.flushNow());
  e.c.prefs().currentProject = a;
  e.c.savePrefs();
  AppController c2{e.fs, e.clk, "data", "roadmap_prefs.json"};
  c2.start();
  REQUIRE(c2.session());
  CHECK(c2.prefs().autoBaseToday);
  CHECK(c2.session()->settings().baseDate == "2026-10-19");
}

TEST_CASE("controller: 引き継ぎの設定は開いているプロジェクトへ渡り、追加直後の通知から Undo で戻せる") {
  Env e;
  e.c.start();
  e.c.createProject(SampleKind::Empty, "P", "");
  const auto a = e.c.session()->addIssue("", "", "親");
  e.c.session()->setField(a, [](Issue& o) { o.assignee = "山田"; o.prio = "High"; });
  e.c.afterEdit();
  e.c.select(a);
  // オフなら今までどおり
  const auto c0 = e.c.addChild();
  CHECK(e.c.session()->issue(c0)->assignee.empty());
  e.c.prefs().inherit.on = true;
  e.c.savePrefs();
  e.c.select(a);
  const auto c1 = e.c.addChild();
  CHECK(e.c.session()->issue(c1)->assignee == "山田");
  CHECK(e.c.session()->issue(c1)->prio == "High");
  CHECK(e.c.toasts().back().text.find("親から引き継ぎました") == 0);
  CHECK(e.c.toasts().back().action == NoticeAction::Undo);
  e.c.undo();
  CHECK_FALSE(e.c.session()->issue(c1));
  // プロジェクトを開き直しても有効
  e.c.openProject(e.c.currentId());
  CHECK(e.c.session()->issue(a));
  const auto c2 = e.c.session()->addIssue(a, "");
  CHECK(e.c.session()->issue(c2)->assignee == "山田");
}

TEST_CASE("controller: 画面の拡大縮小（段階・上下限・100%に戻す・保存）") {
  Env e;
  e.c.start();
  CHECK(e.c.prefs().uiScale == 100);
  CHECK(e.c.stepUiScale(+1));
  CHECK(e.c.prefs().uiScale == 110);
  CHECK(e.c.stepUiScale(+1));
  CHECK(e.c.prefs().uiScale == 125);
  CHECK(loadPrefs(e.fs, "roadmap_prefs.json").uiScale == 125);  // すぐ保存する
  for (int i = 0; i < 20; ++i) e.c.stepUiScale(+1);
  CHECK(e.c.prefs().uiScale == 200);
  CHECK_FALSE(e.c.stepUiScale(+1));  // 上限では変わらない
  for (int i = 0; i < 20; ++i) e.c.stepUiScale(-1);
  CHECK(e.c.prefs().uiScale == 70);
  CHECK_FALSE(e.c.stepUiScale(-1));
  CHECK(e.c.setUiScale(100));
  CHECK(e.c.prefs().uiScale == 100);
  CHECK_FALSE(e.c.setUiScale(100));
  // 段階の間の値（古い設定ファイルなど）からでも、次の段階へ進む
  e.c.prefs().uiScale = 105;
  CHECK(e.c.stepUiScale(+1));
  CHECK(e.c.prefs().uiScale == 110);
  e.c.prefs().uiScale = 105;
  CHECK(e.c.stepUiScale(-1));
  CHECK(e.c.prefs().uiScale == 100);
  // 範囲外は丸める
  CHECK(e.c.setUiScale(999));
  CHECK(e.c.prefs().uiScale == 200);
}

TEST_CASE("controller: 選択を順につなぐ・外す（表示順・重複なし・循環は理由を出す・Undo 1回・保存）") {
  Env e;
  e.c.start();
  e.c.createProject(SampleKind::Empty, "P", "");
  Session* s = e.c.session();
  const auto a = s->addIssue("", "", "A");
  const auto b = s->addIssue("", a, "B");
  const auto c = s->addIssue("", b, "C");
  e.c.afterEdit();
  s->clearHistory();
  // 何も選んでいなければ何もしない
  CHECK_FALSE(e.c.chainSelection({a, b, c}).ok);
  // 3件を選ぶ（選んだ順は C,A,B）。画面の順（A,B,C）でつなぐ
  e.c.setSelection({c, a, b}, b);
  const auto r = e.c.chainSelection({a, b, c});
  CHECK(r.ok);
  CHECK(r.added == 2);
  CHECK(s->issue(a)->blocks == std::vector<std::string>{b});
  CHECK(s->issue(b)->blocks == std::vector<std::string>{c});
  // 通知（元に戻すつき）
  REQUIRE_FALSE(e.c.toasts().empty());
  CHECK(e.c.toasts().back().action == NoticeAction::Undo);
  // もう一度は何もしない（重複なし）。Undo も増えない
  const auto undoSize = s->undoSize();
  CHECK(e.c.chainSelection({a, b, c}).added == 0);
  CHECK(s->undoSize() == undoSize);
  // 保存される
  e.step(500);
  CHECK(e.c.saveState() == SaveState::Saved);
  // 1回の Undo で戻る
  e.c.undo();
  CHECK(s->issue(a)->blocks.empty());
  CHECK(s->issue(b)->blocks.empty());
  // 画面の順が C,B,A なら C→B→A
  CHECK(e.c.chainSelection({c, b, a}).ok);
  CHECK(s->issue(c)->blocks == std::vector<std::string>{b});
  // 逆向き（A→B）は循環になる。何も変えず、理由を出す
  e.c.setSelection({a, b}, b);
  const auto bad = e.c.chainSelection({a, b});
  CHECK_FALSE(bad.ok);
  CHECK(s->issue(a)->blocks.empty());
  CHECK(e.c.toasts().back().text.find("ループ") != std::string::npos);
  // 外す: 選んだ課題どうしのつなぎ
  e.c.setSelection({a, b, c}, b);
  CHECK(e.c.unchainSelection() == 2);
  CHECK(s->issue(c)->blocks.empty());
  CHECK(s->issue(b)->blocks.empty());
  e.c.undo();
  CHECK(s->issue(c)->blocks == std::vector<std::string>{b});
}

TEST_CASE("controller: 新しい課題の名前（アプリの設定。既定は今のまま）") {
  Env e;
  e.c.start();
  e.c.createProject(SampleKind::Empty, "P", "dev");
  // 既定: 今のまま
  CHECK(e.c.prefs().newNameFormat == "keep");
  const auto a = e.c.createIssue("", "");
  REQUIRE_FALSE(a.empty());
  CHECK(e.c.session()->issue(a)->summary == "新しい課題");
  const auto a2 = e.c.createIssue(a, "");
  CHECK(e.c.session()->issue(a2)->summary == "新しい課題");

  // 親の名前＋連番（追加を続けても、消したあとでも重ならない）
  e.c.session()->setName(a, "要件定義");
  e.c.afterEdit();
  e.c.prefs().newNameFormat = "parentSeq";
  const auto b1 = e.c.createIssue(a, "");
  const auto b2 = e.c.createIssue(a, b1);
  CHECK(e.c.session()->issue(b1)->summary == "要件定義の子 2");  // 先に「新しい課題」の子が1件ある
  CHECK(e.c.session()->issue(b2)->summary == "要件定義の子 3");
  e.c.session()->deleteIssues({a2});
  e.c.afterEdit();
  const auto b3 = e.c.createIssue(a, "");
  CHECK(e.c.session()->issue(b3)->summary == "要件定義の子 4");

  // 最上位は「新しい課題 n」
  const auto t = e.c.createIssue("", "");
  CHECK(e.c.session()->issue(t)->summary == "新しい課題 2");  // 最上位に「要件定義」が1件ある

  // 自由入力: 親の表示ID・日付（時計から）
  e.c.prefs().newNameFormat = "custom";
  e.c.prefs().newNameCustom = "{親ID}/{n} {日付}";
  const auto c1 = e.c.createIssue(a, "");
  CHECK(e.c.session()->issue(c1)->summary == "DEV-" + a + "/4 2026-10-19");

  // 追加（addSibling / addChild）も同じ名前になる
  e.c.prefs().newNameFormat = "parentDefault";
  e.c.select(b1);
  const auto s1 = e.c.addSibling();
  CHECK(e.c.session()->issue(s1)->summary == "要件定義／新しい課題");
  const auto s2 = e.c.addChild();
  CHECK(e.c.session()->issue(s2)->summary == "要件定義／新しい課題／新しい課題");  // s1 の子（addSibling が選択を移した）
}

TEST_CASE("controller: 平置きは自動では動かさず、知らせて、操作でフォルダ構成へ移す") {
  Env e;
  e.fs.put("data/古い.csv", "Summary\nA\n");
  e.fs.put("data/古い.settings.json", "{\"id\":\"pold\",\"name\":\"古い\"}");
  e.fs.put("data/二つ目.csv", "Summary\nB\n");
  e.c.start();
  CHECK(e.c.projects().size() == 2);
  CHECK(e.c.legacyCount() == 2);
  bool notified = false;
  for (const auto& t : e.c.toasts())
    if (t.text.find("平置き") != std::string::npos) notified = true;
  CHECK(notified);
  CHECK(e.fs.exists("data/古い.csv"));  // 動かしていない
  CHECK(e.c.countProjectsIn("data") == 2);

  // 開いて編集しても、平置きのまま保存する
  REQUIRE(e.c.openProject("pold"));
  e.c.session()->setBaseDate("2026-09-01");
  e.c.afterEdit();
  REQUIRE(e.c.flushNow());
  CHECK(e.fs.content("data/古い.settings.json").find("2026-09-01") != std::string::npos);
  CHECK_FALSE(e.fs.exists("data/古い/project.csv"));

  // 移す（開いているプロジェクトのまま）
  auto m = e.c.migrateLegacy();
  REQUIRE(m.ok());
  CHECK(m->moved == 2);
  CHECK(e.c.legacyCount() == 0);
  CHECK(e.fs.exists("data/古い/project.csv"));
  CHECK(e.fs.exists("data/二つ目/project.csv"));
  CHECK_FALSE(e.fs.exists("data/古い.csv"));
  CHECK(e.fs.exists("data/_バックアップ/移行前.20261019-183600/古い.csv"));
  CHECK(e.c.countProjectsIn("data") == 2);
  CHECK(e.c.currentId() == "pold");
  e.c.session()->setBaseDate("2026-09-02");
  e.c.afterEdit();
  REQUIRE(e.c.flushNow());
  CHECK(e.fs.content("data/古い/project.settings.json").find("2026-09-02") != std::string::npos);
  // 削除はフォルダごと
  CHECK(e.c.deleteCurrentProject());
  CHECK(e.fs.exists("data/_削除済み/古い.20261019-183600/project.csv"));
  CHECK_FALSE(e.fs.exists("data/古い/project.csv"));
}

TEST_CASE("controller: 報告（HTML）の書き出し — 範囲・既定のファイル名・保存・通知") {
  Env e;
  e.c.start();
  // プロジェクトを開く前は組み立てられない（落ちない）
  CHECK_FALSE(e.c.buildReport(ExportOptions{}).ok());
  e.c.createProject(SampleKind::Hierarchy, "階層/デモ", "DEV");
  REQUIRE(e.c.session());
  // 既定のファイル名は <プロジェクト名>_<今日>.html（使えない文字は _）
  CHECK(e.c.reportFileName() == "階層_デモ_2026-10-19.html");

  ExportOptions all;
  auto r = e.c.buildReport(all);
  REQUIRE(r.ok());
  CHECK(r.value().find("<h1>階層/デモ</h1>") != std::string::npos);
  CHECK(r.value().find("出力 <b>2026/10/19 18:36</b>") != std::string::npos);  // 出力日時は IClock から
  const std::size_t allRows = r.value().size();

  // 今の絞り込みのまま: 絞り込みに合う行だけ（絞り込みの種類の名前は出るが、検索語は書かない）
  e.c.filter().q = "ZZZ-存在しない-検索語";
  ExportOptions cur;
  cur.scope = ReportScope::Current;
  auto rc = e.c.buildReport(cur);
  REQUIRE(rc.ok());
  CHECK(rc.value().find("表示範囲: 画面の表示のまま（絞り込み: 検索）") != std::string::npos);
  CHECK(rc.value().find("ZZZ-存在しない-検索語") == std::string::npos);
  CHECK(rc.value().find("表示する課題がありません") != std::string::npos);
  CHECK(rc.value().size() < allRows);
  e.c.clearFilter();

  // 選んだ課題の配下: 1 つ選んでいないと断る
  ExportOptions sub;
  sub.scope = ReportScope::Subtree;
  e.c.clearSelection();
  auto none = e.c.buildReport(sub);
  CHECK_FALSE(none.ok());
  CHECK_FALSE(none.error().empty());
  const std::string root = e.c.session()->tree()->ids[static_cast<std::size_t>(e.c.session()->tree()->roots[0])];
  e.c.select(root);
  auto rs = e.c.buildReport(sub);
  REQUIRE(rs.ok());
  CHECK(rs.value().find("とその配下") != std::string::npos);

  // 保存: 書いて、「フォルダを開く」つきで通知する
  CHECK(e.fs.makeDirs("out").ok());
  auto st = e.c.exportReport("out/報告.html", all);
  CHECK(st.ok());
  REQUIRE(e.fs.exists("out/報告.html"));
  CHECK(e.fs.content("out/報告.html").compare(0, 15, "<!doctype html>") == 0);
  REQUIRE_FALSE(e.c.toasts().empty());
  CHECK(e.c.toasts().back().action == NoticeAction::OpenFolder);
  CHECK(e.c.toasts().back().arg == "out");
  CHECK(e.c.toasts().back().text.find("書き出しました") == 0);

  // 失敗は戻り値と通知で返す（落とさない・古いファイルを壊さない）
  const std::string before = e.fs.content("out/報告.html");
  e.fs.setFailWrites(true);
  auto bad = e.c.exportReport("out/報告.html", all);
  CHECK_FALSE(bad.ok());
  CHECK(e.c.toasts().back().text.find("書き出せません") == 0);
  CHECK(e.c.toasts().back().action == NoticeAction::None);
  CHECK(e.fs.content("out/報告.html") == before);
  e.fs.setFailWrites(false);
  // 拡張子が違う保存先は断る
  CHECK_FALSE(e.c.exportReport("out/報告.exe", all).ok());
  CHECK_FALSE(e.fs.exists("out/報告.exe"));
  CHECK(e.c.toasts().back().text.find("書き出せません") == 0);
  // 通知から「フォルダを開く」は UI の役目。コントローラの runToastAction は何もしない（落ちない）
  const int id = e.c.toasts().back().id;
  e.c.runToastAction(id);
}
