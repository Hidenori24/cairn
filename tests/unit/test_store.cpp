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
  CHECK(fs.exists("data/案件A.csv"));
  CHECK(fs.exists("data/案件A.settings.json"));
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

TEST_CASE("store: _削除済み への移動") {
  MemFileStore fs;
  Workspace ws(fs, clk, kDir);
  const auto id = ws.add(rec("消す"));
  REQUIRE(ws.flush().ok());
  REQUIRE(ws.remove(id).ok());
  CHECK_FALSE(fs.exists("data/消す.csv"));
  CHECK(fs.exists("data/_削除済み/消す.20261019-181500.csv"));
  CHECK(fs.exists("data/_削除済み/消す.20261019-181500.settings.json"));
  CHECK(ws.entries().empty());
}

TEST_CASE("store: stem の衝突回避と安全なファイル名") {
  MemFileStore fs;
  Workspace ws(fs, clk, kDir);
  ws.add(rec("同じ"));
  ws.add(rec("同じ"));
  ws.add(rec("ＡＢ/C:?"));
  REQUIRE(ws.flush().ok());
  CHECK(fs.exists("data/同じ.csv"));
  CHECK(fs.exists("data/同じ-2.csv"));
  CHECK(fs.exists("data/ＡＢ_C__.csv"));
  CHECK(Workspace::safeStem("  ..a.b.. ") == "a.b");
  CHECK(Workspace::safeStem("") == "project");
  CHECK(Workspace::safeStem(std::string(100, 'x')).size() == 80);
}

TEST_CASE("store: 外部変更の取り込み（3ケース）") {
  MemFileStore fs;
  Workspace ws(fs, clk, kDir);
  const auto id = ws.add(rec("P", "Summary\nA\n"));
  REQUIRE(ws.flush().ok());

  SUBCASE("ファイルだけ変わった → 読み直す") {
    fs.put("data/P.csv", "Summary\nExcelで編集\n");
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
    CHECK(fs.content("data/P.csv") == "Summary\n手元\n");
  }
  SUBCASE("両方変わった → フォルダの版を別プロジェクトに") {
    ProjectRecord r0 = ws.find(id)->rec;
    r0.csv = "Summary\n手元\n";
    ws.update(id, r0);
    fs.put("data/P.csv", "Summary\nフォルダ\n");
    auto r = ws.sync();
    REQUIRE(r.ok());
    CHECK(r->kept == 1);
    REQUIRE(ws.entries().size() == 2);
    CHECK(ws.entries()[1].rec.settings.name == "P（フォルダの版）");
    CHECK(ws.entries()[1].rec.csv == "Summary\nフォルダ\n");
    CHECK(fs.content("data/P.csv") == "Summary\n手元\n");
    CHECK(fs.exists("data/P（フォルダの版）.csv"));
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
  CHECK(chooseDataDir(fs, "arg", "env", "exe", "fb").dir == "arg");
  CHECK(chooseDataDir(fs, "", "env", "exe", "fb").dir == "env");
  CHECK(chooseDataDir(fs, "", "", "exe", "fb").dir == "exe/data/projects");
  fs.setFailWrites(true);
  auto c = chooseDataDir(fs, "", "", "exe", "fb");
  CHECK(c.dir == "fb");
  CHECK(c.usedFallback);
  fs.setFailWrites(false);
  Prefs p;
  p.me = "田中";
  p.unknown["future"] = true;
  REQUIRE(savePrefs(fs, "roadmap_prefs.json", p));
  Prefs q = loadPrefs(fs, "roadmap_prefs.json");
  CHECK(q.me == "田中");
  CHECK(q.unknown["future"] == true);
  CHECK(loadPrefs(fs, "ない.json").theme == "auto");
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
