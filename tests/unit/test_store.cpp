#include <doctest/doctest.h>

#include <filesystem>
#include <random>

#include "../test_util.hpp"
#include "roadmap/commands.hpp"
#include "roadmap/store.hpp"

using namespace roadmap;

namespace {
const std::string kDir = "data";
FixedClock clk(LocalTime{2026, 10, 19, 18, 15, 0});

ProjectRecord rec(const std::string& name, const std::string& csvText = "Summary\nA\n") {
  return newRecord(name, "", csvText, "2026-10-19", clk);
}
}  // namespace

TEST_CASE("store: 書き込み・内容が同じなら書かない") {
  MemFileStore fs;
  Workspace ws(fs, clk, kDir);
  const auto id = ws.add(rec("案件A"));
  REQUIRE(ws.flush().ok());
  CHECK(fs.exists("data/案件A/project.csv"));  // 新しいプロジェクトはフォルダ構成
  CHECK(fs.exists("data/案件A/project.settings.json"));
  CHECK_FALSE(fs.exists("data/案件A.csv"));
  const int w = fs.writeCount();
  ws.find(id)->dirty = true;
  REQUIRE(ws.flush().ok());
  CHECK(fs.writeCount() == w);
  CHECK_FALSE(ws.hasDirty());
}

TEST_CASE("store: settings.json の形式と未知のキーの保持") {
  MemFileStore fs;
  fs.put("data/x.csv", "Summary\nA\n");
  fs.put("data/x.settings.json", "{\"app\":\"regroadmap\",\"version\":3,\"id\":\"pabc\",\"name\":\"名前\",\"key\":\"K\","
                                 "\"base\":\"2026-10-19\",\"hol\":\"2026-11-03\",\"scn\":\"標準\",\"scenarios\":[],"
                                 "\"res\":{\"on\":true,\"cap\":2},\"ui\":{\"zoom\":\"day\",\"mine\":1},\"extra\":{\"a\":1}}");
  Workspace ws(fs, clk, kDir);
  auto r = ws.sync();
  REQUIRE(r.ok());
  CHECK(r->added == 1);
  ProjectEntry* e = ws.find("pabc");
  REQUIRE(e);
  CHECK(e->rec.settings.name == "名前");
  CHECK(e->rec.settings.resource.on);
  CHECK(e->rec.settings.resource.cap == 2);
  CHECK(e->rec.settings.ui["zoom"] == "day");
  CHECK(e->rec.settings.ui["mine"] == 1);
  CHECK(e->rec.settings.ui["view"] == "gantt");  // 既定値で補う
  e->rec.settings.name = "新しい名前";
  e->dirty = true;
  REQUIRE(ws.flush().ok());
  const std::string out = fs.content("data/x.settings.json");
  CHECK(out.rfind("{\n \"app\": \"regroadmap\",\n \"version\": 3,\n \"id\": \"pabc\",\n \"name\": \"新しい名前\"", 0) == 0);
  CHECK(out.find("\"extra\": {\n  \"a\": 1\n }") != std::string::npos);
  CHECK(out.find("\"mine\": 1") != std::string::npos);
  CHECK(fs.exists("data/x.csv"));  // 名前を変えてもファイル名は変えない
}

TEST_CASE("store: バックアップはプロセスで最初の上書きだけ・30個まで") {
  MemFileStore fs;
  fs.put("data/a.csv", "Summary\nold\n");
  Workspace ws(fs, clk, kDir);
  REQUIRE(ws.sync().ok());
  const std::string id = ws.entries()[0].id();
  for (int i = 0; i < 3; ++i) {
    ProjectRecord r = ws.find(id)->rec;
    r.csv = "Summary\nnew" + std::to_string(i) + "\n";
    ws.update(id, r);
    REQUIRE(ws.flush().ok());
  }
  int backups = 0;
  for (const auto& p : fs.paths())
    if (p.rfind("data/_バックアップ/", 0) == 0) {
      ++backups;
      CHECK(fs.content(p) == "Summary\nold\n");
      CHECK(p == "data/_バックアップ/a.20261019-181500.csv");
    }
  CHECK(backups == 1);
  // 上限
  for (int i = 0; i < 40; ++i) fs.put("data/_バックアップ/z." + std::to_string(1000 + i) + ".csv", "x");
  fs.put("data/b.csv", "Summary\nb\n");
  REQUIRE(ws.sync().ok());
  ProjectEntry* b = nullptr;
  for (auto& e : ws.entries())
    if (e.stem == "b") b = &e;
  REQUIRE(b);
  ProjectRecord r = b->rec;
  r.csv = "Summary\nb2\n";
  ws.update(b->id(), r);
  REQUIRE(ws.flush().ok());
  backups = 0;
  for (const auto& p : fs.paths())
    if (p.rfind("data/_バックアップ/", 0) == 0) ++backups;
  CHECK(backups == 30);
  CHECK(fs.exists("data/_バックアップ/b.20261019-181500.csv"));  // 新しいものは残る
}

TEST_CASE("store: _削除済み へフォルダごと移す") {
  MemFileStore fs;
  Workspace ws(fs, clk, kDir);
  const auto id = ws.add(rec("消す"));
  REQUIRE(ws.flush().ok());
  fs.put("data/消す/添付/10001/仕様.pdf", "pdf");  // 添付もいっしょに移る
  REQUIRE(ws.remove(id).ok());
  CHECK_FALSE(fs.exists("data/消す/project.csv"));
  CHECK_FALSE(fs.stat("data/消す").has_value());
  CHECK(fs.exists("data/_削除済み/消す.20261019-181500/project.csv"));
  CHECK(fs.exists("data/_削除済み/消す.20261019-181500/project.settings.json"));
  CHECK(fs.exists("data/_削除済み/消す.20261019-181500/添付/10001/仕様.pdf"));
  CHECK(ws.entries().empty());
  // 続けて同じ名前を作って消しても、先に消したものを上書きしない
  const auto id2 = ws.add(rec("消す"));
  REQUIRE(ws.flush().ok());
  REQUIRE(ws.remove(id2).ok());
  CHECK(fs.exists("data/_削除済み/消す.20261019-181500-2/project.csv"));
  CHECK(fs.exists("data/_削除済み/消す.20261019-181500/project.csv"));
}

TEST_CASE("store: 平置き（互換）の削除は今まで通り _削除済み へファイルを移す") {
  MemFileStore fs;
  fs.put("data/古い.csv", "Summary\nA\n");
  fs.put("data/古い.settings.json", "{\"id\":\"pold\",\"name\":\"古い\"}");
  Workspace ws(fs, clk, kDir);
  REQUIRE(ws.sync().ok());
  REQUIRE(ws.entries().size() == 1);
  CHECK(ws.entries()[0].layout == Layout::Flat);
  REQUIRE(ws.remove("pold").ok());
  CHECK_FALSE(fs.exists("data/古い.csv"));
  CHECK_FALSE(fs.exists("data/古い.settings.json"));
  CHECK(fs.exists("data/_削除済み/古い.20261019-181500.csv"));
  CHECK(fs.exists("data/_削除済み/古い.20261019-181500.settings.json"));
}

TEST_CASE("store: 削除したフォルダを戻す") {
  MemFileStore fs;
  Workspace ws(fs, clk, kDir);
  auto r0 = rec("戻す");
  r0.settings.id = "pback";
  const auto id = ws.add(r0);
  REQUIRE(ws.flush().ok());
  REQUIRE(ws.remove(id).ok());
  CHECK(ws.deletedNames() == std::vector<std::string>{"戻す.20261019-181500"});
  // 同じ名前のプロジェクトが先にできていたら、空いている名前で戻す
  ws.add(rec("戻す"));
  REQUIRE(ws.flush().ok());
  auto back = ws.restoreDeleted("戻す.20261019-181500");
  REQUIRE(back.ok());
  CHECK(*back == "戻す-2");
  CHECK(fs.exists("data/戻す-2/project.csv"));
  CHECK_FALSE(fs.exists("data/_削除済み/戻す.20261019-181500/project.csv"));
  CHECK(ws.deletedNames().empty());
  auto sr = ws.sync();
  REQUIRE(sr.ok());
  CHECK(sr->added == 1);
  REQUIRE(ws.find("pback"));  // 設定の id を保ったまま戻る
  CHECK(ws.find("pback")->stem == "戻す-2");
  CHECK_FALSE(ws.restoreDeleted("ない").ok());
  CHECK_FALSE(ws.restoreDeleted("../x").ok());
}

TEST_CASE("store: フォルダ名の衝突回避と安全なフォルダ名") {
  MemFileStore fs;
  Workspace ws(fs, clk, kDir);
  ws.add(rec("同じ"));
  ws.add(rec("同じ"));
  ws.add(rec("ＡＢ/C:?"));
  ws.add(rec("_削除済み"));  // 予約された名前とは別のフォルダにする
  ws.add(rec("同じ"));
  REQUIRE(ws.flush().ok());
  CHECK(fs.exists("data/同じ/project.csv"));
  CHECK(fs.exists("data/同じ-2/project.csv"));
  CHECK(fs.exists("data/同じ-3/project.csv"));
  CHECK(fs.exists("data/ＡＢ_C__/project.csv"));
  CHECK(fs.exists("data/_削除済み-2/project.csv"));
  CHECK(Workspace::safeStem("  ..a.b.. ") == "a.b");
  CHECK(Workspace::safeStem("") == "project");
  CHECK(Workspace::safeStem(std::string(100, 'x')).size() == 80);
  CHECK(Workspace::safeStem("a\\b/c:d*e?f\"g<h>i|j") == "a_b_c_d_e_f_g_h_i_j");
}

TEST_CASE("store: 平置きの stem と同じ名前の新しいプロジェクトは別の名前にする") {
  MemFileStore fs;
  fs.put("data/同じ.csv", "Summary\nA\n");
  Workspace ws(fs, clk, kDir);
  REQUIRE(ws.sync().ok());
  ws.add(rec("同じ"));
  REQUIRE(ws.flush().ok());
  CHECK(fs.exists("data/同じ-2/project.csv"));
  CHECK(fs.exists("data/同じ.csv"));
}

TEST_CASE("store: 外部変更の取り込み（3ケース）") {
  MemFileStore fs;
  Workspace ws(fs, clk, kDir);
  const auto id = ws.add(rec("P", "Summary\nA\n"));
  REQUIRE(ws.flush().ok());

  SUBCASE("ファイルだけ変わった → 読み直す") {
    fs.put("data/P/project.csv", "Summary\nExcelで編集\n");
    auto r = ws.sync();
    REQUIRE(r.ok());
    CHECK(r->loaded == 1);
    CHECK(r->reloadedIds == std::vector<std::string>{id});
    CHECK(ws.find(id)->rec.csv == "Summary\nExcelで編集\n");
    CHECK(r->message() == "フォルダの更新を読み込み 1件");
  }
  SUBCASE("手元だけ変わった → 書く") {
    ProjectRecord r0 = ws.find(id)->rec;
    r0.csv = "Summary\n手元\n";
    ws.update(id, r0);
    auto r = ws.sync();
    REQUIRE(r.ok());
    CHECK(r->wrote == 1);
    CHECK(fs.content("data/P/project.csv") == "Summary\n手元\n");
  }
  SUBCASE("両方変わった → フォルダの版を別プロジェクトに") {
    ProjectRecord r0 = ws.find(id)->rec;
    r0.csv = "Summary\n手元\n";
    ws.update(id, r0);
    fs.put("data/P/project.csv", "Summary\nフォルダ\n");
    auto r = ws.sync();
    REQUIRE(r.ok());
    CHECK(r->kept == 1);
    REQUIRE(ws.entries().size() == 2);
    CHECK(ws.entries()[1].rec.settings.name == "P（フォルダの版）");
    CHECK(ws.entries()[1].rec.csv == "Summary\nフォルダ\n");
    CHECK(fs.content("data/P/project.csv") == "Summary\n手元\n");
    CHECK(fs.exists("data/P（フォルダの版）/project.csv"));
  }
}

TEST_CASE("store: 不正なCSVは取り込まず上書きもしない") {
  MemFileStore fs;
  fs.put("data/bad.csv", "a,b\n1,2\n");
  Workspace ws(fs, clk, kDir);
  auto r = ws.sync();
  REQUIRE(r.ok());
  CHECK(r->bad == std::vector<std::string>{"bad"});
  CHECK(ws.entries().empty());
  CHECK(fs.content("data/bad.csv") == "a,b\n1,2\n");
  CHECK(r->message() == "読めないCSV: bad（上書きしていません）");
  // 既知のプロジェクトのファイルが壊れた場合も上書きしない
  const auto id = ws.add(rec("ok"));
  REQUIRE(ws.flush().ok());
  fs.put("data/ok.csv", "Issue Id,Summary,Outward issue link (Blocks)\n1,a,2\n2,b,1\n");
  auto r2 = ws.sync();
  REQUIRE(r2.ok());
  CHECK(r2->bad == std::vector<std::string>{"bad", "ok"});
  CHECK(ws.find(id)->rec.csv == "Summary\nA\n");
}

TEST_CASE("store: settings の id で対応づける・id の衝突は振り直す") {
  MemFileStore fs;
  Workspace ws(fs, clk, kDir);
  auto r0 = rec("A");
  r0.settings.id = "pfixed";
  ws.add(r0, false);
  // 別の名前のファイルに同じ id
  fs.put("data/renamed.csv", "Summary\nX\n");
  fs.put("data/renamed.settings.json", "{\"id\":\"pfixed\",\"name\":\"A\"}");
  auto r = ws.sync();
  REQUIRE(r.ok());
  REQUIRE(ws.entries().size() == 1);
  CHECK(ws.entries()[0].stem == "renamed");
  // フォルダにだけある、既存と同じ id のプロジェクト
  fs.put("data/other.csv", "Summary\nY\n");
  fs.put("data/other.settings.json", "{\"id\":\"pfixed\",\"name\":\"B\"}");
  auto r2 = ws.sync();
  REQUIRE(r2.ok());
  REQUIRE(ws.entries().size() == 2);
  CHECK(ws.entries()[1].id() != "pfixed");
}

TEST_CASE("store: 書き込みの失敗はメモリの内容を捨てない") {
  MemFileStore fs;
  Workspace ws(fs, clk, kDir);
  const auto id = ws.add(rec("A"));
  fs.setFailWrites(true);
  CHECK_FALSE(ws.flush().ok());
  CHECK(ws.find(id)->dirty);
  fs.setFailWrites(false);
  CHECK(ws.flush().ok());
  CHECK_FALSE(ws.find(id)->dirty);
}

TEST_CASE("store: StdFileStore の原子的な書き込み") {
  namespace fs = std::filesystem;
  const fs::path dir = fs::temp_directory_path() / ("roadmap_test_" + std::to_string(std::random_device{}()));
  fs::remove_all(dir);
  StdFileStore st;
  const std::string d = dir.string();
  REQUIRE(st.makeDirs(d).ok());
  const std::string p = joinPath(d, "日本語.csv");
  REQUIRE(st.writeAtomic(p, "one").ok());
  REQUIRE(st.writeAtomic(p, "two").ok());
  CHECK(*st.read(p) == "two");
  // 一時ファイルが残らない
  auto l = st.list(d);
  REQUIRE(l.ok());
  CHECK(l->size() == 1);
  // 書けない場所への書き込みは失敗し、元のファイルは残る
  CHECK_FALSE(st.writeAtomic(joinPath(joinPath(d, "ない"), "x.csv"), "x").ok());
  CHECK(*st.read(p) == "two");
  CHECK(st.probeWritable(d));
  fs::remove_all(dir);
}

TEST_CASE("store: データフォルダの決定・prefs") {
  MemFileStore fs;
  CHECK(chooseDataDir(fs, "arg", "env", "pref", "exe", "fb").dir == "arg");
  CHECK(chooseDataDir(fs, "arg", "env", "pref", "exe", "fb").source == DataDirSource::Arg);
  CHECK(chooseDataDir(fs, "", "env", "pref", "exe", "fb").dir == "env");
  CHECK(chooseDataDir(fs, "", "env", "pref", "exe", "fb").source == DataDirSource::Env);
  // 設定で選んだ場所（相対なら exe のフォルダから）
  CHECK(chooseDataDir(fs, "", "", "D:/共有/projects", "exe", "fb").dir == "D:/共有/projects");
  CHECK(chooseDataDir(fs, "", "", "mine", "exe", "fb").dir == "exe/mine");
  CHECK(chooseDataDir(fs, "", "", "mine", "exe", "fb").source == DataDirSource::Prefs);
  CHECK(chooseDataDir(fs, "", "", "", "exe", "fb").dir == "exe/data/projects");
  CHECK(chooseDataDir(fs, "", "", "", "exe", "fb").source == DataDirSource::Default);
  CHECK(defaultDataDir("exe") == "exe/data/projects");
  fs.setFailWrites(true);
  auto c = chooseDataDir(fs, "", "", "", "exe", "fb");
  CHECK(c.dir == "fb");
  CHECK(c.usedFallback);
  CHECK(c.source == DataDirSource::Fallback);
  // 設定の場所に書けない（外したドライブなど）→ 既定へ。そのことを画面に出せるように印を付ける
  auto d = chooseDataDir(fs, "", "", "X:/gone", "exe", "fb");
  CHECK(d.prefsDirUnavailable);
  CHECK(d.dir == "fb");
  fs.setFailWrites(false);
  // prefs に覚える形: exe のフォルダの中なら相対（フォルダごと移しても壊れない）
  CHECK(storableDataDir("C:/app/data/projects", "C:/app") == "data/projects");
  CHECK(storableDataDir("C:\\App\\data\\x", "c:/app/") == "data/x");
  CHECK(storableDataDir("C:/app", "C:/app") == ".");
  CHECK(storableDataDir("C:/apple/x", "C:/app") == "C:/apple/x");
  CHECK(storableDataDir("D:/共有", "C:/app") == "D:/共有");
  CHECK(storableDataDir("/home/a/p", "/home/a") == "p");
  CHECK(resolveDataDir("data/x", "C:/app") == "C:/app/data/x");
  CHECK(resolveDataDir(".", "C:/app") == "C:/app");
  CHECK(resolveDataDir("D:/共有", "C:/app") == "D:/共有");
  CHECK(resolveDataDir("\\\\srv\\share", "C:/app") == "\\\\srv\\share");
  CHECK(resolveDataDir("/abs", "C:/app") == "/abs");
  CHECK(resolveDataDir("", "C:/app").empty());
  Prefs p;
  p.me = "田中";
  p.unknown["future"] = true;
  REQUIRE(savePrefs(fs, "roadmap_prefs.json", p));
  p.dataDir = "data/mine";
  p.uiScale = 125;
  p.dimPercent = 45;
  p.confirmDelete = true;
  p.autoBaseToday = true;
  CHECK(p.newNameFormat == "keep");  // 既定は今のまま
  p.newNameFormat = "custom";
  p.newNameCustom = "{親}の子 {n}";
  p.inherit.on = true;
  p.inherit.assignee = false;
  p.inherit.duration = false;
  REQUIRE(savePrefs(fs, "roadmap_prefs.json", p));
  Prefs q = loadPrefs(fs, "roadmap_prefs.json");
  CHECK(q.newNameFormat == "custom");
  CHECK(q.newNameCustom == "{親}の子 {n}");
  CHECK(q.me == "田中");
  CHECK(q.dataDir == "data/mine");
  CHECK(q.uiScale == 125);
  CHECK(q.dimPercent == 45);
  CHECK(q.confirmDelete);
  CHECK(q.autoBaseToday);
  CHECK(q.inherit.on);
  CHECK_FALSE(q.inherit.assignee);
  CHECK(q.inherit.prio);
  CHECK_FALSE(q.inherit.duration);
  CHECK_FALSE(Prefs{}.inherit.on);  // 既定はオフ
  CHECK(q.unknown.size() == 1);
  CHECK(q.unknown["future"] == true);
  CHECK(loadPrefs(fs, "ない.json").theme == "auto");
  // 範囲外の値は丸める
  REQUIRE(fs.writeAtomic("bad.json", R"({"uiScale": 900, "dimPercent": -5, "dataDir": 3})"));
  const Prefs b = loadPrefs(fs, "bad.json");
  CHECK(b.uiScale == 200);
  REQUIRE(fs.writeAtomic("low.json", R"({"uiScale": 10})"));
  CHECK(loadPrefs(fs, "low.json").uiScale == 70);
  CHECK(b.dimPercent == 10);
  CHECK(b.dataDir.empty());
  // 新しい課題の名前: 知らない形式は今のまま、長すぎる自由入力は切る
  REQUIRE(fs.writeAtomic("nn.json", R"({"newNameFormat": "zzz", "newNameCustom": 5})"));
  const Prefs n1 = loadPrefs(fs, "nn.json");
  CHECK(n1.newNameFormat == "keep");
  CHECK(n1.newNameCustom == Prefs{}.newNameCustom);
  REQUIRE(fs.writeAtomic("nn2.json", "{\"newNameCustom\": \"" + std::string(500, 'a') + "\"}"));
  CHECK(loadPrefs(fs, "nn2.json").newNameCustom.size() == 200);
}

TEST_CASE("store: 共有JSONの書き出しと取り込み") {
  auto r = rec("共有", "Summary\nA\n");
  Template t{"u:1", false, "雛形", "", {{"a", 1.0, 0, {}, "", {}, ""}}};
  Template b{"b:doc", true, "組み込み", "", {{"a", 1.0, 0, {}, "", {}, ""}}};
  const std::string j = exportShareJson({r}, {t, b}, clk);
  CHECK(j.rfind("{\n \"app\": \"regroadmap\",\n \"version\": 3,\n \"exported\": \"", 0) == 0);
  auto p = parseBackup(j, clk);
  REQUIRE(p.ok());
  REQUIRE(p->projects.size() == 1);
  CHECK(p->projects[0].settings.name == "共有");
  CHECK(p->projects[0].settings.id == r.settings.id);
  CHECK(p->templates.size() == 1);  // b: で始まるものは取り込まない
  CHECK(parseBackup("{x", clk).error() == "JSONを読み取れません");
  CHECK(parseBackup("{\"app\":\"other\"}", clk).error() == "このアプリのバックアップ・共有ファイルではありません");
  CHECK(parseBackup("{\"app\":\"regroadmap\",\"projects\":[{}]}", clk).error() == "プロジェクトが含まれていません");
}

// ---------------------------------------------------------------- プロジェクトごとのフォルダ構成（Issue #24）

TEST_CASE("store: フォルダ構成のバックアップはプロジェクトごと・30個まで") {
  MemFileStore fs;
  fs.put("data/a/project.csv", "Summary\nold\n");
  fs.put("data/b/project.csv", "Summary\nB\n");
  for (int i = 0; i < 40; ++i) fs.put("data/a/バックアップ/a." + std::to_string(1000 + i) + ".csv", "x");
  for (int i = 0; i < 5; ++i) fs.put("data/b/バックアップ/b." + std::to_string(1000 + i) + ".csv", "x");
  Workspace ws(fs, clk, kDir);
  REQUIRE(ws.sync().ok());
  REQUIRE(ws.entries().size() == 2);
  for (int round = 0; round < 3; ++round) {
    ProjectRecord r = ws.entries()[0].rec;
    r.csv = "Summary\nnew" + std::to_string(round) + "\n";
    ws.update(ws.entries()[0].id(), r);
    REQUIRE(ws.flush().ok());
  }
  int a = 0, b = 0;
  for (const auto& p : fs.paths()) {
    if (p.rfind("data/a/バックアップ/", 0) == 0) ++a;
    if (p.rfind("data/b/バックアップ/", 0) == 0) ++b;
  }
  CHECK(a == 30);  // a だけ数えて古いものから消す
  CHECK(b == 5);   // b のバックアップは巻き込まない
  CHECK(fs.exists("data/a/バックアップ/a.20261019-181500.csv"));
  CHECK(fs.content("data/a/バックアップ/a.20261019-181500.csv") == "Summary\nold\n");
  CHECK_FALSE(fs.stat("data/_バックアップ").has_value());  // 全体用のフォルダは作らない
}

TEST_CASE("store: フォルダの読み取り（project.csv・CSVだけのフォルダ・予約フォルダ・添付）") {
  MemFileStore fs;
  fs.put("data/A/project.csv", "Summary\nA\n");
  fs.put("data/A/project.settings.json", "{\"id\":\"pa\",\"name\":\"プロジェクトA\"}");
  fs.put("data/A/添付/10001/図.png", "png");  // 添付の中は見ない
  fs.put("data/A/添付/10001/memo.csv", "Summary\nz\n");
  fs.put("data/取り込み用/export.csv", "Summary\nX\n");  // CSV だけを置いたフォルダ
  fs.put("data/_削除済み/Z.20261019-000000/project.csv", "Summary\nZ\n");
  fs.put("data/_バックアップ/Y.1.csv", "Summary\nY\n");
  fs.put("data/空/メモ.txt", "メモ");
  fs.put("data/単体.csv", "Summary\nS\n");  // CSV 単体のファイル
  Workspace ws(fs, clk, kDir);
  auto r = ws.sync();
  REQUIRE(r.ok());
  CHECK(r->added == 3);
  REQUIRE(ws.entries().size() == 3);
  const ProjectEntry* a = ws.find("pa");
  REQUIRE(a);
  CHECK(a->stem == "A");
  CHECK(a->layout == Layout::Folder);
  CHECK(a->rec.settings.name == "プロジェクトA");
  for (const auto& e : ws.entries()) {
    if (e.stem == "取り込み用") {
      CHECK(e.layout == Layout::Folder);
      CHECK(e.rec.settings.name == "取り込み用");  // 名前はフォルダ名
    }
    if (e.stem == "単体") CHECK(e.layout == Layout::Flat);
  }
  // CSV だけのフォルダは、取り込んだ時点で project.csv と設定をそのフォルダに作る（元の CSV には触れない）
  CHECK(fs.exists("data/取り込み用/project.csv"));
  CHECK(fs.exists("data/取り込み用/project.settings.json"));
  CHECK(fs.content("data/取り込み用/export.csv") == "Summary\nX\n");
  // もう一度読んでも増えない
  auto r2 = ws.sync();
  REQUIRE(r2.ok());
  CHECK(r2->added == 0);
  CHECK(ws.entries().size() == 3);
  Workspace ws2(fs, clk, kDir);
  REQUIRE(ws2.sync().ok());
  CHECK(ws2.entries().size() == 3);
}

TEST_CASE("store: フォルダ構成で不正なCSVは取り込まず上書きもしない") {
  MemFileStore fs;
  fs.put("data/bad/project.csv", "a,b\n1,2\n");
  Workspace ws(fs, clk, kDir);
  auto r = ws.sync();
  REQUIRE(r.ok());
  CHECK(r->bad == std::vector<std::string>{"bad"});
  CHECK(ws.entries().empty());
  CHECK(fs.content("data/bad/project.csv") == "a,b\n1,2\n");
  CHECK_FALSE(fs.exists("data/bad/project.settings.json"));
  const auto id = ws.add(rec("ok"));
  REQUIRE(ws.flush().ok());
  fs.put("data/ok/project.csv", "Issue Id,Summary,Outward issue link (Blocks)\n1,a,2\n2,b,1\n");
  auto r2 = ws.sync();
  REQUIRE(r2.ok());
  CHECK(r2->bad == std::vector<std::string>{"bad", "ok"});
  CHECK(ws.find(id)->rec.csv == "Summary\nA\n");
}

TEST_CASE("store: フォルダ構成の id による対応づけ（フォルダ名を手で変えても同じプロジェクト）") {
  MemFileStore fs;
  Workspace ws(fs, clk, kDir);
  auto r0 = rec("元の名前");
  r0.settings.id = "pfix";
  ws.add(r0);
  REQUIRE(ws.flush().ok());
  // エクスプローラーでフォルダ名を変えた
  REQUIRE(fs.move("data/元の名前", "data/変えた").ok());
  auto r = ws.sync();
  REQUIRE(r.ok());
  CHECK(ws.entries().size() == 1);
  CHECK(ws.find("pfix")->stem == "変えた");
  // 編集して書くと、新しいフォルダ名に書く
  ProjectRecord e = ws.find("pfix")->rec;
  e.csv = "Summary\n編集\n";
  ws.update("pfix", e);
  REQUIRE(ws.flush().ok());
  CHECK(fs.content("data/変えた/project.csv") == "Summary\n編集\n");
  CHECK_FALSE(fs.exists("data/元の名前/project.csv"));
}

TEST_CASE("store: 平置きの読み込みと、フォルダ構成との共存") {
  MemFileStore fs;
  fs.put("data/古い.csv", "Summary\nOld\n");
  fs.put("data/古い.settings.json", "{\"id\":\"pold\",\"name\":\"古い\"}");
  Workspace ws(fs, clk, kDir);
  REQUIRE(ws.sync().ok());
  CHECK(ws.legacyCount() == 1);
  const auto nid = ws.add(rec("新しい"));
  REQUIRE(ws.flush().ok());
  CHECK(fs.exists("data/新しい/project.csv"));
  CHECK(ws.legacyCount() == 1);  // 自動では動かさない
  // 平置きを編集すると、平置きのまま書く（バックアップは全体用の _バックアップ/）
  ProjectRecord r = ws.find("pold")->rec;
  r.csv = "Summary\nEdited\n";
  ws.update("pold", r);
  REQUIRE(ws.flush().ok());
  CHECK(fs.content("data/古い.csv") == "Summary\nEdited\n");
  CHECK(fs.exists("data/_バックアップ/古い.20261019-181500.csv"));
  CHECK_FALSE(fs.exists("data/古い/project.csv"));
  Workspace ws2(fs, clk, kDir);
  REQUIRE(ws2.sync().ok());
  CHECK(ws2.entries().size() == 2);
  CHECK(ws2.find(nid)->layout == Layout::Folder);
  CHECK(ws2.find("pold")->layout == Layout::Flat);
}

TEST_CASE("store: 平置きをフォルダ構成へ移す（先にバックアップ・バイト列は変えない）") {
  MemFileStore fs;
  const std::string csvA = "Summary\r\nA,\"複数\n行\"\r\n";  // 改行コードもそのまま移る
  const std::string setA = "{\n \"id\": \"pa\",\n \"name\": \"案件A\",\n \"extra\": {\"k\": 1}\n}";
  fs.put("data/案件A.csv", csvA);
  fs.put("data/案件A.settings.json", setA);
  fs.put("data/B.csv", "Summary\nB\n");  // 設定ファイルなし
  Workspace ws(fs, clk, kDir);
  REQUIRE(ws.sync().ok());
  const auto nid = ws.add(rec("新規"));  // 最初からフォルダ構成のものは対象外
  REQUIRE(ws.flush().ok());
  REQUIRE(ws.legacyCount() == 2);

  auto m = ws.migrateLegacy();
  REQUIRE(m.ok());
  CHECK(m->moved == 2);
  CHECK(m->failed.empty());
  CHECK(m->backupDir == "data/_バックアップ/移行前.20261019-181500");
  // 先にバックアップ
  CHECK(fs.content("data/_バックアップ/移行前.20261019-181500/案件A.csv") == csvA);
  CHECK(fs.content("data/_バックアップ/移行前.20261019-181500/案件A.settings.json") == setA);
  CHECK(fs.content("data/_バックアップ/移行前.20261019-181500/B.csv") == "Summary\nB\n");
  // 移った先（中身は1バイトも変えない）
  CHECK(fs.content("data/案件A/project.csv") == csvA);
  CHECK(fs.content("data/案件A/project.settings.json") == setA);
  CHECK(fs.content("data/B/project.csv") == "Summary\nB\n");
  CHECK_FALSE(fs.exists("data/案件A.csv"));
  CHECK_FALSE(fs.exists("data/案件A.settings.json"));
  CHECK_FALSE(fs.exists("data/B.csv"));
  CHECK(ws.legacyCount() == 0);
  CHECK(ws.find("pa")->layout == Layout::Folder);
  CHECK(ws.find("pa")->stem == "案件A");
  CHECK(ws.find(nid)->layout == Layout::Folder);
  // 移したあとの同期で、増えたり読み直したりしない
  auto s = ws.sync();
  REQUIRE(s.ok());
  CHECK(s->added == 0);
  CHECK(s->loaded == 0);
  CHECK(ws.entries().size() == 3);
  // もう一度呼んでも何も起きない（バックアップも増やさない）
  const auto before = fs.paths().size();
  auto m2 = ws.migrateLegacy();
  REQUIRE(m2.ok());
  CHECK(m2->moved == 0);
  CHECK(fs.paths().size() == before);
  // 新しく起動しても同じ
  Workspace ws2(fs, clk, kDir);
  REQUIRE(ws2.sync().ok());
  CHECK(ws2.entries().size() == 3);
  CHECK(ws2.legacyCount() == 0);
}

TEST_CASE("store: 移行は、フォルダ名が使われていれば別の名前にし、未保存の編集も先に書く") {
  MemFileStore fs;
  fs.put("data/X.csv", "Summary\nflat\n");
  fs.put("data/X.settings.json", "{\"id\":\"pflat\",\"name\":\"X\"}");
  fs.put("data/X/project.csv", "Summary\nfolder\n");
  fs.put("data/X/project.settings.json", "{\"id\":\"pfolder\",\"name\":\"X2\"}");
  Workspace ws(fs, clk, kDir);
  REQUIRE(ws.sync().ok());
  REQUIRE(ws.entries().size() == 2);
  ProjectRecord r = ws.find("pflat")->rec;
  r.csv = "Summary\n未保存の編集\n";
  ws.update("pflat", r);
  auto m = ws.migrateLegacy();
  REQUIRE(m.ok());
  CHECK(m->moved == 1);
  CHECK(ws.find("pflat")->stem == "X-2");
  CHECK(fs.content("data/X-2/project.csv") == "Summary\n未保存の編集\n");  // 編集を失わない
  CHECK(fs.content("data/X/project.csv") == "Summary\nfolder\n");
  CHECK_FALSE(fs.exists("data/X.csv"));
}

TEST_CASE("store: 移行に失敗したら平置きのファイルを消さない") {
  MemFileStore fs;
  fs.put("data/A.csv", "Summary\nA\n");
  Workspace ws(fs, clk, kDir);
  REQUIRE(ws.sync().ok());
  fs.setFailWrites(true);
  auto m = ws.migrateLegacy();
  CHECK_FALSE(m.ok());
  fs.setFailWrites(false);
  CHECK(fs.content("data/A.csv") == "Summary\nA\n");
  CHECK(ws.legacyCount() == 1);
  CHECK(ws.entries()[0].layout == Layout::Flat);
  // 書けるようになったら移せる
  auto m2 = ws.migrateLegacy();
  REQUIRE(m2.ok());
  CHECK(m2->moved == 1);
  CHECK(fs.exists("data/A/project.csv"));
}

TEST_CASE("paths: フォルダ名・添付の置き場所・パスの長さ") {
  using namespace roadmap::paths;
  // 添付の置き場所: <プロジェクトのフォルダ>/添付/<Issue Id>
  CHECK(attachmentDir("data/案件A", "10042") == "data/案件A/添付/10042");
  CHECK(attachmentDir("data/案件A", "a/b:c") == "data/案件A/添付/a_b_c");  // フォルダ名に使えない文字は _
  CHECK(attachmentDir("data/案件A", "") == "data/案件A/添付/issue");
  CHECK(utf16Units(attachmentDir("d", std::string(200, '1'))) == utf16Units("d/添付/") + 80);  // 80文字まで
  // 同名は -2・-3
  std::set<std::string> used{"a", "a-2"};
  CHECK(uniqueName(used, "A") == "A-3");
  CHECK(uniqueName(used, "b") == "b");
  // ファイル名の安全化
  CHECK(safeFileName("a:b?.txt") == "a_b_.txt");
  CHECK(safeFileName("  .. ") == "file");
  const std::string longSafe = safeFileName(std::string(300, 'x') + ".pdf");
  CHECK(utf16Units(longSafe) <= 200);
  CHECK(longSafe.substr(longSafe.size() - 4) == ".pdf");
  // パスの長さ（Windows の 260 文字に収める）。UTF-16 の単位で数える
  CHECK(utf16Units("data/😀") == 7);
  const std::string dir = "C:/app/data/projects/" + std::string(60, 'p') + "/添付/10001";
  CHECK(fitFileName(dir, "short.txt") == "short.txt");
  const std::string fit = fitFileName(dir, std::string(300, 'n') + ".xlsx");
  CHECK(utf16Units(dir) + 1 + utf16Units(fit) <= kMaxPathUnits);
  CHECK(fit.size() > 100);
  CHECK(fit.substr(fit.size() - 5) == ".xlsx");  // 拡張子は残す
  (void)fitFileName(dir, std::string(300, '\xe3'));  // 不正な UTF-8 でも落ちない
  // 絵文字（2単位）を途中で切らない
  std::string emoji;
  for (int i = 0; i < 200; ++i) emoji += "😀";
  const std::string f2 = fitFileName(dir, emoji + ".txt");
  CHECK(utf16Units(dir) + 1 + utf16Units(f2) <= kMaxPathUnits);
  CHECK(f2.substr(f2.size() - 4) == ".txt");
  CHECK(f2.size() > 4);
  // フォルダだけで長すぎて、ファイル名の入る余地がない → 空
  CHECK(fitFileName(std::string(250, 'd'), "a.txt").empty());
  // 末尾のドット・空白は Windows が削るので残さない
  const std::string f3 = fitFileName(std::string(200, 'd'), std::string(100, 'a') + ". .txt");
  CHECK(utf16Units(std::string(200, 'd')) + 1 + utf16Units(f3) <= kMaxPathUnits);
  CHECK(f3.back() != '.');
  CHECK(f3.back() != ' ');
}

TEST_CASE("filestore: move（ファイルとフォルダ）とフォルダの stat") {
  MemFileStore fs;
  fs.put("w/a/x.txt", "x");
  fs.put("w/a/sub/y.txt", "y");
  REQUIRE(fs.makeDirs("w/a/空").ok());
  REQUIRE(fs.stat("w/a").has_value());
  CHECK(fs.stat("w/a")->isDir);
  CHECK_FALSE(fs.stat("w/ない").has_value());
  REQUIRE(fs.move("w/a", "w/b").ok());
  CHECK(fs.exists("w/b/x.txt"));
  CHECK(fs.exists("w/b/sub/y.txt"));
  CHECK_FALSE(fs.stat("w/a").has_value());
  CHECK(fs.stat("w/b/空").has_value());
  REQUIRE(fs.move("w/b/x.txt", "w/z.txt").ok());
  CHECK(fs.exists("w/z.txt"));
  CHECK_FALSE(fs.exists("w/b/x.txt"));
  CHECK_FALSE(fs.move("w/ない", "w/q").ok());
  CHECK_FALSE(fs.move("w/b", "w/z.txt").ok());  // 行き先にあれば失敗（上書きしない）
  fs.setFailWrites(true);
  CHECK_FALSE(fs.move("w/b", "w/c").ok());
}

TEST_CASE("filestore: StdFileStore の move（フォルダごと）") {
  namespace fs = std::filesystem;
  const fs::path dir = fs::temp_directory_path() / ("roadmap_move_" + std::to_string(std::random_device{}()));
  fs::remove_all(dir);
  StdFileStore st;
  const std::string d = dir.string();
  REQUIRE(st.makeDirs(joinPath(d, "案件/添付/1")).ok());
  REQUIRE(st.writeAtomic(joinPath(d, "案件/project.csv"), "csv").ok());
  REQUIRE(st.writeAtomic(joinPath(d, "案件/添付/1/メモ.txt"), "memo").ok());
  REQUIRE(st.makeDirs(joinPath(d, "_削除済み")).ok());
  REQUIRE(st.move(joinPath(d, "案件"), joinPath(d, "_削除済み/案件.1")).ok());
  CHECK_FALSE(st.stat(joinPath(d, "案件")).has_value());
  CHECK(*st.read(joinPath(d, "_削除済み/案件.1/添付/1/メモ.txt")) == "memo");
  CHECK(st.stat(joinPath(d, "_削除済み/案件.1"))->isDir);
  CHECK_FALSE(st.move(joinPath(d, "ない"), joinPath(d, "x")).ok());
  fs::remove_all(dir);
}

TEST_CASE("store: 実際のファイルシステム（StdFileStore）でフォルダ構成の保存・削除・移行") {
  namespace fs = std::filesystem;
  const fs::path dir = fs::temp_directory_path() / ("roadmap_ws_" + std::to_string(std::random_device{}()));
  fs::remove_all(dir);
  StdFileStore st;
  const std::string d = dir.string();
  REQUIRE(st.makeDirs(d).ok());
  REQUIRE(st.writeAtomic(joinPath(d, "旧.csv"), "Summary\nOld\n").ok());  // 平置き
  {
    Workspace ws(st, clk, d);
    REQUIRE(ws.sync().ok());
    const auto id = ws.add(rec("日本語の案件"));
    REQUIRE(ws.flush().ok());
    CHECK(fs::exists(dir / fs::path(std::u8string(u8"日本語の案件")) / "project.csv"));
    CHECK(fs::exists(dir / fs::path(std::u8string(u8"日本語の案件")) / "project.settings.json"));
    // 添付の場所（#19 で使う）を作って、フォルダごと削除 → 戻す
    const std::string att = paths::attachmentDir(joinPath(d, "日本語の案件"), "10001");
    REQUIRE(st.makeDirs(att).ok());
    REQUIRE(st.writeAtomic(joinPath(att, "図.png"), "png").ok());
    REQUIRE(ws.remove(id).ok());
    CHECK_FALSE(fs::exists(dir / fs::path(std::u8string(u8"日本語の案件"))));
    const auto names = ws.deletedNames();
    REQUIRE(names.size() == 1);
    auto back = ws.restoreDeleted(names[0]);
    REQUIRE(back.ok());
    CHECK(*back == "日本語の案件");
    CHECK(*st.read(joinPath(paths::attachmentDir(joinPath(d, *back), "10001"), "図.png")) == "png");
    // 移行
    REQUIRE(ws.sync().ok());
    CHECK(ws.legacyCount() == 1);
    auto m = ws.migrateLegacy();
    REQUIRE(m.ok());
    CHECK(m->moved == 1);
    CHECK_FALSE(fs::exists(dir / fs::path(std::u8string(u8"旧.csv"))));
    CHECK(*st.read(joinPath(d, "旧/project.csv")) == "Summary\nOld\n");
    CHECK(*st.read(joinPath(m->backupDir, "旧.csv")) == "Summary\nOld\n");
  }
  {
    // 起動し直しても同じ（2件・平置きなし）
    Workspace ws(st, clk, d);
    auto r = ws.sync();
    REQUIRE(r.ok());
    CHECK(ws.entries().size() == 2);
    CHECK(ws.legacyCount() == 0);
    CHECK(r->added == 2);
    CHECK(r->bad.empty());
  }
  fs::remove_all(dir);
}
