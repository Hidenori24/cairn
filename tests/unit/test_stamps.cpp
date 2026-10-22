// アプリで作った課題の作成日（Created）・更新日（Updated）の自動記録（Issue #66）
#include <doctest/doctest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "../test_util.hpp"
#include "roadmap/commands.hpp"
#include "roadmap/csv.hpp"
#include "roadmap/project_io.hpp"
#include "roadmap/samples.hpp"
#include "roadmap/trends.hpp"

using namespace roadmap;
using testutil::D;

namespace {
FixedClock clockAt(int y, int mo, int d) { return FixedClock(LocalTime{y, mo, d, 15, 5, 0}); }

ProjectData dataFrom(const std::string& csvText, const std::string& base = "2026-10-22") {
  ProjectData d;
  auto imp = csv::importCsv(csvText);
  if (!imp) FAIL(imp.error());
  d.issues = imp->issues;
  d.meta = imp->meta;
  d.settings.id = "p1";
  d.settings.name = "テスト";
  d.settings.baseDate = base;
  d.settings.holidays = defaultHolidaysText();
  d.settings.ui = defaultUi();
  return d;
}

// 列のない CSV（アプリで新しく作ったプロジェクトに近い）
const char* kPlain =
    "Issue Id,Issue Type,Summary,Parent,Status,Duration (weeks)\n"
    "1,Epic,E,,,\n2,Task,A,1,To Do,1\n3,Task,B,1,To Do,1\n4,Sub-task,C,3,To Do,1\n5,Epic,F,,,\n";

int colOf(const Session& s, const std::string& header) {
  const auto& h = s.meta().extraHeaders;
  for (std::size_t i = 0; i < h.size(); ++i)
    if (h[i] == header) return static_cast<int>(i);
  return -1;
}
bool hasCol(const Session& s, const std::string& header) { return colOf(s, header) >= 0; }
// 課題 id の列 header の値（列がなければ "<列なし>"）
std::string cellOf(const Session& s, const std::string& id, const std::string& header) {
  const int c = colOf(s, header);
  const Issue* o = s.issue(id);
  if (c < 0 || !o) return "<列なし>";
  return static_cast<std::size_t>(c) < o->extra.size() ? o->extra[static_cast<std::size_t>(c)] : "";
}
std::string created(const Session& s, const std::string& id) { return cellOf(s, id, "Created"); }
std::string updated(const Session& s, const std::string& id) { return cellOf(s, id, "Updated"); }
std::string appCsv(const Session& s) { return s.exportText(ExportKind::App); }
// 全課題の extra が見出しの数と揃っているか
bool aligned(const Session& s) {
  for (const auto& o : s.issues())
    if (o.extra.size() != s.meta().extraHeaders.size()) return false;
  return true;
}
}  // namespace

TEST_CASE("stamps: 作る経路ごとに Created が入る（追加・子・同じ階層）") {
  auto clk = clockAt(2026, 10, 22);
  Session s(dataFrom(kPlain), &clk);
  REQUIRE_FALSE(hasCol(s, "Created"));
  const auto top = s.addIssue("", "");
  CHECK(hasCol(s, "Created"));
  CHECK(created(s, top) == "2026-10-22");
  CHECK(aligned(s));
  CHECK(created(s, "2") == "");  // 既存の課題へは埋め戻さない
  CHECK(created(s, "1") == "");
  const auto kid = s.addIssue("1", "");
  CHECK(created(s, kid) == "2026-10-22");
  const auto sib = s.addIssue("1", "3", "兄弟");
  CHECK(created(s, sib) == "2026-10-22");
  // 作っただけでは Updated の列は作らない
  CHECK_FALSE(hasCol(s, "Updated"));
  // 列は 1 本だけ
  CHECK(std::count(s.meta().extraHeaders.begin(), s.meta().extraHeaders.end(), "Created") == 1);
  CHECK(aligned(s));
}

TEST_CASE("stamps: アウトラインの貼り付け・テンプレートの適用で作った課題にも Created が入る") {
  auto clk = clockAt(2026, 10, 22);
  Session s(dataFrom(kPlain), &clk);
  const Outline o = parseOutline("設計\n  詳細\n実装\n");
  REQUIRE(o.items.size() == 3);
  CHECK(s.addOutline("1", o.items) == 3);
  int n = 0;
  for (const auto& it : s.issues()) {
    const bool isNew = it.id != "1" && it.id != "2" && it.id != "3" && it.id != "4" && it.id != "5";
    if (isNew) {
      ++n;
      CHECK(created(s, it.id) == "2026-10-22");
    } else {
      CHECK(created(s, it.id) == "");
    }
  }
  CHECK(n == 3);
  CHECK(aligned(s));

  clk.set(LocalTime{2026, 10, 23, 9, 0, 0});
  Session t(dataFrom(kPlain, "2026-10-23"), &clk);
  const Template& tpl = builtinTemplates()[2];
  const int made = t.applyTemplate({"2", "3"}, tpl);
  REQUIRE(made > 0);
  int fresh = 0;
  for (const auto& it : t.issues()) {
    if (it.id.size() == 1 && it.id[0] >= '1' && it.id[0] <= '5') {
      CHECK(created(t, it.id) == "");
      continue;
    }
    ++fresh;
    CHECK(created(t, it.id) == "2026-10-23");
  }
  CHECK(fresh == made);
  CHECK(aligned(t));
  // 親（テンプレートを適用した先）の課題自体は変えていないので Updated は付かない
  CHECK_FALSE(hasCol(t, "Updated"));
}

TEST_CASE("stamps: 新しい後続を作る（作成 + つなぐ）は Created と、つないだ両端の Updated") {
  auto clk = clockAt(2026, 10, 22);
  Session s(dataFrom(kPlain), &clk);
  s.beginBatch();
  const auto nid = s.addIssue("1", "2");
  CHECK(s.addLink("2", nid));
  s.endBatch();
  CHECK(created(s, nid) == "2026-10-22");
  CHECK(updated(s, "2") == "2026-10-22");  // 先行の側
  CHECK(updated(s, nid) == "2026-10-22");  // 後続の側
  CHECK(updated(s, "3") == "");
  CHECK(created(s, "2") == "");
  CHECK(s.undoSize() == 1);
  CHECK(s.undo());
  CHECK_FALSE(hasCol(s, "Created"));
  CHECK_FALSE(hasCol(s, "Updated"));
}

TEST_CASE("stamps: 内容を変える編集で Updated が入る（種類ごと）") {
  auto clk = clockAt(2026, 10, 22);
  Session s(dataFrom(kPlain), &clk);
  CHECK_FALSE(hasCol(s, "Updated"));
  // 件名
  CHECK(s.setName("2", "新しい名前"));
  CHECK(hasCol(s, "Updated"));
  CHECK(updated(s, "2") == "2026-10-22");
  CHECK(updated(s, "3") == "");
  CHECK(created(s, "2") == "<列なし>");  // 編集では Created の列を作らない
  CHECK(aligned(s));
  // 番号
  CHECK(s.setTag("3", "X-1"));
  CHECK(updated(s, "3") == "2026-10-22");
  // 状況
  CHECK(s.setStatus("4", "In Progress"));
  CHECK(updated(s, "4") == "2026-10-22");
  // 担当・優先度・説明・種別・ラベル・見積り・日付（setField）
  CHECK(s.setField("5", [](Issue& o) { o.assignee = "田中"; }));
  CHECK(updated(s, "5") == "2026-10-22");
  CHECK(s.setField("1", [](Issue& o) { o.prio = "High"; }));
  CHECK(updated(s, "1") == "2026-10-22");
  // 期間
  clk.set(LocalTime{2026, 10, 23, 9, 0, 0});
  CHECK(s.setDuration("2", 3));
  CHECK(updated(s, "2") == "2026-10-22");  // 基準日(10/22)より先にならない（Resolved と同じ min(今日, 基準日)）
  s.setBaseDate("2026-10-23");
  CHECK(s.setDuration("2", 4));
  CHECK(updated(s, "2") == "2026-10-23");
  CHECK(updated(s, "3") == "2026-10-22");  // ほかの課題は変わらない
  // コメント
  CHECK(s.addComment("3", "メモ", "自分"));
  CHECK(updated(s, "3") == "2026-10-23");
  CHECK(s.delComment("3", 0));
  CHECK(updated(s, "3") == "2026-10-23");
}

TEST_CASE("stamps: 期限を初めて設定するときの Due date → Deadline 列への移し替えは、他の課題を更新扱いにしない") {
  const char* due =
      "Issue Id,Issue Type,Summary,Parent,Status,Due date,Duration (weeks)\n"
      "1,Epic,E,,,2026-12-18,\n2,Task,A,1,To Do,2026-12-04,1\n3,Task,B,1,To Do,2026-12-11,1\n";
  auto clk = clockAt(2026, 10, 22);
  Session s(dataFrom(due), &clk);
  CHECK_FALSE(s.meta().hasDeadlineCol);
  CHECK(s.setDeadline("2", D("2026-12-01")));
  CHECK(s.meta().hasDeadlineCol);
  CHECK(s.issue("3")->deadline.has_value());  // 移し替えは起きている
  CHECK(updated(s, "2") == "2026-10-22");
  CHECK(updated(s, "3") == "");  // けれど編集したのは 2 だけ
  CHECK(updated(s, "1") == "");
}

TEST_CASE("stamps: 着手可能日・親の付け替え・インデント・一括") {
  auto clk = clockAt(2026, 10, 22);
  Session s(dataFrom(kPlain), &clk);
  CHECK(s.setDeadline("2", D("2026-12-01")));
  CHECK(updated(s, "2") == "2026-10-22");
  CHECK(updated(s, "3") == "");
  // 着手可能日
  CHECK(s.setEarliest("4", D("2026-11-30")));
  CHECK(s.issue("4")->notBefore == D("2026-11-30"));
  CHECK(updated(s, "4") == "2026-10-22");
  CHECK(updated(s, "3") == "");  // 親は変えていない
  // 付け替え: 動かした課題だけ（親・子・兄弟は変えない）
  clk.set(LocalTime{2026, 10, 24, 9, 0, 0});
  s.setBaseDate("2026-10-24");
  CHECK(s.reparent("4", "5"));
  CHECK(updated(s, "4") == "2026-10-24");
  CHECK(updated(s, "5") == "");
  CHECK(updated(s, "3") == "");
  // インデント・アウトデント・移動
  clk.set(LocalTime{2026, 10, 25, 9, 0, 0});
  s.setBaseDate("2026-10-25");
  CHECK(s.indent("3"));
  CHECK(updated(s, "3") == "2026-10-25");
  CHECK(updated(s, "2") == "2026-10-22");
  // 一括: 変わった課題すべて
  clk.set(LocalTime{2026, 10, 26, 9, 0, 0});
  s.setBaseDate("2026-10-26");
  CHECK(s.bulk({"1", "2", "5"}, [](Issue& o) { o.assignee = "鈴木"; }));
  CHECK(updated(s, "1") == "2026-10-26");
  CHECK(updated(s, "2") == "2026-10-26");
  CHECK(updated(s, "5") == "2026-10-26");
  CHECK(updated(s, "4") == "2026-10-24");
  CHECK(s.undoSize() > 0);
}

TEST_CASE("stamps: 1 回の操作で変わった課題すべてに入り、Undo 1 回で全部戻る") {
  auto clk = clockAt(2026, 10, 22);
  Session s(dataFrom(kPlain), &clk);
  const std::string before = appCsv(s);
  s.beginBatch();
  for (const char* id : {"2", "3", "4"}) CHECK(s.setStatus(id, "In Progress"));
  s.endBatch();
  CHECK(s.undoSize() == 1);
  for (const char* id : {"2", "3", "4"}) CHECK(updated(s, id) == "2026-10-22");
  CHECK(updated(s, "1") == "");
  CHECK(s.undo());
  CHECK_FALSE(hasCol(s, "Updated"));
  CHECK(appCsv(s) == before);
  CHECK(s.redo());
  for (const char* id : {"2", "3", "4"}) CHECK(updated(s, id) == "2026-10-22");
  CHECK(aligned(s));
}

TEST_CASE("stamps: 依存の追加・外す・先行の入力・削除は両端の課題に入る") {
  auto clk = clockAt(2026, 10, 22);
  Session s(dataFrom(kPlain), &clk);
  CHECK(s.addLink("2", "3"));
  CHECK(updated(s, "2") == "2026-10-22");
  CHECK(updated(s, "3") == "2026-10-22");
  CHECK(updated(s, "4") == "");
  clk.set(LocalTime{2026, 10, 23, 9, 0, 0});
  s.setBaseDate("2026-10-23");
  // 「先行」列の入力（4 の先行を 2 にする = 2.blocks に 4 を足す）。入力した側の 4 も更新される
  const PredPlan pp = s.setPredecessors("4", "2");
  CHECK(pp.ok);
  CHECK(updated(s, "4") == "2026-10-23");
  CHECK(updated(s, "2") == "2026-10-23");
  CHECK(updated(s, "3") == "2026-10-22");
  clk.set(LocalTime{2026, 10, 24, 9, 0, 0});
  s.setBaseDate("2026-10-24");
  CHECK(s.removeLink("2", "3"));
  CHECK(updated(s, "2") == "2026-10-24");
  CHECK(updated(s, "3") == "2026-10-24");
  CHECK(updated(s, "4") == "2026-10-23");
  // つなぎを外す（選んだ課題どうし）
  clk.set(LocalTime{2026, 10, 25, 9, 0, 0});
  s.setBaseDate("2026-10-25");
  CHECK(s.unchainIssues({"2", "4"}) == 1);
  CHECK(updated(s, "2") == "2026-10-25");
  CHECK(updated(s, "4") == "2026-10-25");
  // 削除: つなぎが残った課題（blocks が変わる）に入る。消えた課題は無関係
  CHECK(s.addLink("2", "3"));
  clk.set(LocalTime{2026, 10, 26, 9, 0, 0});
  s.setBaseDate("2026-10-26");
  CHECK(s.deleteIssues({"3"}));
  CHECK(updated(s, "2") == "2026-10-26");
  CHECK(updated(s, "1") == "");
}

TEST_CASE("stamps: 内容の変わらない操作（並べ替え・折りたたみ・設定・添付・リンク）では何も増えない") {
  auto clk = clockAt(2026, 10, 22);
  Session s(dataFrom(kPlain), &clk);
  const std::string before = appCsv(s);
  CHECK(s.moveIssue("3", -1));  // 兄弟の並べ替え
  CHECK_FALSE(hasCol(s, "Updated"));
  CHECK(s.moveIssue("3", 1));
  s.setCollapsed("1", true);
  s.setCollapsed("1", false);
  s.touchUi();
  s.setBaseDate("2026-10-23");
  s.setBaseDate("2026-10-22");
  s.setHolidays("2026-11-03");
  s.setHolidays(defaultHolidaysText());
  s.setResource({true, 2});
  s.setResource({false, 1});
  s.attachments()["2"].push_back({"a.txt", 3, "2026-10-22T10:00", "me", ""});
  s.touchAttachments();
  s.setNameKey("名前", "dev");
  CHECK_FALSE(hasCol(s, "Updated"));
  CHECK_FALSE(hasCol(s, "Created"));
  // 同じ値の代入（変わらない）
  CHECK(s.setField("2", [](Issue&) {}));
  CHECK(s.setName("2", "A"));  // 件名を同じにする
  CHECK_FALSE(hasCol(s, "Updated"));
  CHECK(appCsv(s) == before);  // 書き出しも元のまま
}

TEST_CASE("stamps: 着手可能日が動かせず元に戻ったときは何も記録しない") {
  auto clk = clockAt(2026, 10, 22);
  Session s(dataFrom(kPlain), &clk);
  CHECK(s.addLink("2", "3"));
  const auto n = s.undoSize();
  const std::string snap = appCsv(s);
  // 3 は 2 の後ろ。基準日より前の着手可能日にはならず、巻き戻される
  s.setEarliest("3", D("2026-10-01"));
  CHECK(appCsv(s) == snap);
  CHECK(s.undoSize() == n);
}

TEST_CASE("stamps: 同じ日に何度編集しても Updated は同じ。日が変われば進み、Created は変わらない") {
  auto clk = clockAt(2026, 10, 22);
  Session s(dataFrom(kPlain), &clk);
  const auto nid = s.addIssue("1", "");
  CHECK(s.setName(nid, "一回目"));
  CHECK(s.setName(nid, "二回目"));
  CHECK(updated(s, nid) == "2026-10-22");
  CHECK(created(s, nid) == "2026-10-22");
  clk.set(LocalTime{2026, 10, 27, 9, 0, 0});
  s.setBaseDate("2026-10-27");
  CHECK(s.setName(nid, "三回目"));
  CHECK(updated(s, nid) == "2026-10-27");
  CHECK(created(s, nid) == "2026-10-22");
}

TEST_CASE("stamps: 日付は min(今日, 基準日)。Resolved と同じ決め方") {
  auto clk = clockAt(2026, 10, 22);
  Session s(dataFrom(kPlain, "2026-10-19"), &clk);  // 基準日が今日より前
  const auto nid = s.addIssue("1", "");
  CHECK(created(s, nid) == "2026-10-19");
  CHECK(s.setStatus(nid, "Done"));
  CHECK(s.issue(nid)->resolved == D("2026-10-19"));
  CHECK(updated(s, nid) == "2026-10-19");  // 作成日 → 更新日 → 完了日の順がずれない
  auto clk2 = clockAt(2026, 10, 10);  // 基準日が今日より先
  Session t(dataFrom(kPlain, "2026-10-19"), &clk2);
  const auto m = t.addIssue("1", "");
  CHECK(created(t, m) == "2026-10-10");
}

TEST_CASE("stamps: Undo/Redo で Created/Updated も一緒に戻る") {
  auto clk = clockAt(2026, 10, 22);
  Session s(dataFrom(kPlain), &clk);
  const std::string base = appCsv(s);
  const auto nid = s.addIssue("1", "", "追加");
  const std::string afterAdd = appCsv(s);
  CHECK(s.setName("2", "変更"));
  const std::string afterEdit = appCsv(s);
  CHECK(afterEdit != afterAdd);
  CHECK(updated(s, "2") == "2026-10-22");
  CHECK(s.undo());
  CHECK(appCsv(s) == afterAdd);
  CHECK_FALSE(hasCol(s, "Updated"));
  CHECK(created(s, nid) == "2026-10-22");
  CHECK(s.undo());
  CHECK(appCsv(s) == base);
  CHECK_FALSE(hasCol(s, "Created"));
  CHECK(s.redo());
  CHECK(appCsv(s) == afterAdd);
  CHECK(s.redo());
  CHECK(appCsv(s) == afterEdit);
  CHECK(aligned(s));
}

TEST_CASE("stamps: Created/Updated が 1 件もない CSV は今までと同じバイト列で書き出される") {
  for (const char* name : {"scenario_demo", "hierarchy_demo", "leveling_demo"}) {
    CAPTURE(name);
    const std::string text = testutil::readText(testutil::golden(std::string("samples/") + name + ".csv"));
    auto imp = csv::importCsv(text);
    REQUIRE(imp.ok());
    const std::string direct = exportAppCsv(imp->issues, imp->meta);
    auto clk = clockAt(2026, 10, 22);
    Session s(dataFrom(text, "2026-10-19"), &clk);
    CHECK(appCsv(s) == direct);  // 取り込み → 書き出し（編集なし）
    CHECK(s.version() >= 0);
    // 編集して Undo で戻したら、元のバイト列に戻る（列も消える）
    const auto id = s.issues().front().id;
    CHECK(s.setField(id, [](Issue& o) { o.assignee = "テスト担当"; }));
    CHECK(appCsv(s) != direct);
    CHECK(s.undo());
    CHECK(appCsv(s) == direct);
  }
}

TEST_CASE("stamps: 置き換えの取り込みでは Created を埋めない") {
  auto clk = clockAt(2026, 10, 22);
  Session s(dataFrom(kPlain), &clk);
  auto imp = csv::importCsv(kPlain);
  REQUIRE(imp.ok());
  s.replaceIssues(imp->issues, imp->meta, {});
  CHECK_FALSE(hasCol(s, "Created"));
  CHECK_FALSE(hasCol(s, "Updated"));
  CHECK(appCsv(s) == exportAppCsv(imp->issues, imp->meta));
}

namespace {
// JIRA の書式の Created / Updated を持つ CSV
const char* kJira =
    "Issue Id,Issue Type,Summary,Parent,Status,Created,Updated,Resolved,Duration (weeks)\n"
    "1,Epic,E,,,19/Oct/26 9:00 AM,20/Oct/26 3:15 PM,,\n"
    "2,Task,A,1,To Do,19/Oct/26 9:00 AM,21/Oct/26 11:05 AM,,1\n"
    "3,Task,B,1,To Do,,,,1\n";
}  // namespace

TEST_CASE("stamps: JIRA の書式の Created は上書きしない。Updated は書式を保つ") {
  auto clk = clockAt(2026, 10, 22);
  Session s(dataFrom(kJira), &clk);
  const std::size_t headers = s.meta().extraHeaders.size();
  CHECK(created(s, "2") == "19/Oct/26 9:00 AM");
  CHECK(s.setName("2", "編集"));
  CHECK(created(s, "2") == "19/Oct/26 9:00 AM");  // 上書きしない
  CHECK(updated(s, "2") == "22/Oct/26 3:05 PM");  // JIRA の書式のまま（時刻は IClock）
  CHECK(updated(s, "1") == "20/Oct/26 3:15 PM");  // 触っていない課題はそのまま
  CHECK(s.meta().extraHeaders.size() == headers);  // 列は増えない
  // 列があっても空だった課題（3）も、Created は埋めない（作った課題ではない）
  CHECK(s.setName("3", "編集B"));
  CHECK(created(s, "3") == "");
  CHECK(updated(s, "3") == "22/Oct/26 3:05 PM");
  // 新しく作った課題の Created は Resolved と同じ書式の日付
  const auto nid = s.addIssue("1", "");
  CHECK(created(s, nid) == "2026-10-22");
  CHECK(updated(s, nid) == "");
  // 取り込んだ値は parseDate で読める（推移の入力として正しい）
  CHECK(csv::parseDate(created(s, "2")) == D("2026-10-19"));
  CHECK(csv::parseDate(updated(s, "2")) == D("2026-10-22"));
}

TEST_CASE("stamps: 日付だけの Updated の列は日付の書式のまま。別名の見出しも同じ列として使う") {
  const char* alias =
      "Issue Id,Issue Type,Summary,Parent,Status,作成日,更新日,Duration (weeks)\n"
      "1,Epic,E,,,2026-10-01,2026-10-02,\n2,Task,A,1,To Do,2026-10-01,,1\n";
  auto clk = clockAt(2026, 10, 22);
  Session s(dataFrom(alias), &clk);
  CHECK(s.setName("2", "編集"));
  CHECK(s.meta().extraHeaders.size() == 2);
  CHECK(cellOf(s, "2", "更新日") == "2026-10-22");
  CHECK(cellOf(s, "2", "作成日") == "2026-10-01");
  const auto nid = s.addIssue("1", "");
  CHECK(cellOf(s, nid, "作成日") == "2026-10-22");
  CHECK(cellOf(s, nid, "更新日") == "");
  CHECK_FALSE(hasCol(s, "Created"));
  CHECK_FALSE(hasCol(s, "Updated"));
}

TEST_CASE("stamps: 未知の列が他にあっても位置を動かさず、末尾に足す") {
  const char* withExtra =
      "Issue Id,Issue Type,Summary,Parent,Status,Memo,Outward issue link (Relates),Duration (weeks)\n"
      "1,Epic,E,,,メモ1,,\n2,Task,A,1,To Do,メモ2,3,1\n3,Task,B,1,To Do,,,1\n";
  auto clk = clockAt(2026, 10, 22);
  Session s(dataFrom(withExtra), &clk);
  const auto beforeHeaders = s.meta().extraHeaders;
  const auto nid = s.addIssue("1", "");
  CHECK(s.setName("2", "編集"));
  const auto& h = s.meta().extraHeaders;
  REQUIRE(h.size() == beforeHeaders.size() + 2);
  CHECK(std::equal(beforeHeaders.begin(), beforeHeaders.end(), h.begin()));
  CHECK(h[h.size() - 2] == "Created");
  CHECK(h[h.size() - 1] == "Updated");
  CHECK(cellOf(s, "2", "Memo") == "メモ2");
  CHECK(cellOf(s, nid, "Memo") == "");
  CHECK(aligned(s));
  // 保存して読み直しても同じ
  auto again = csv::importCsv(appCsv(s));
  REQUIRE(again.ok());
  CHECK(again->meta.extraHeaders == h);
  CHECK(again->issues[1].extra == s.issues()[1].extra);
}

TEST_CASE("stamps: 関連リンク（Relates）の追加も課題の編集として Updated が入る") {
  auto clk = clockAt(2026, 10, 22);
  Session s(dataFrom(kPlain), &clk);
  CHECK(s.addOtherLink("2", "Relates", true, "PRJ-1"));
  CHECK(updated(s, "2") == "2026-10-22");
  CHECK(updated(s, "3") == "");  // 列を足しただけの課題は更新扱いにしない
  CHECK(updated(s, "1") == "");
  CHECK(aligned(s));
}

TEST_CASE("stamps: 完了から戻したとき（再オープン）は Resolved が消える") {
  auto clk = clockAt(2026, 10, 22);
  Session s(dataFrom(kPlain), &clk);
  CHECK(s.setStatus("2", "Done"));
  CHECK(s.issue("2")->resolved == D("2026-10-22"));
  // 完了 → 別の完了の状況: 完了日は残す
  CHECK(s.setStatus("2", "Closed"));
  CHECK(s.issue("2")->resolved == D("2026-10-22"));
  // 完了 → 進行中: 消える
  CHECK(s.setStatus("2", "In Progress"));
  CHECK_FALSE(s.issue("2")->resolved.has_value());
  // 開始日の自動設定は今までどおり
  CHECK(s.issue("2")->start == D("2026-10-22"));
  // もう一度完了にすると、その日が入る
  clk.set(LocalTime{2026, 10, 24, 9, 0, 0});
  s.setBaseDate("2026-10-24");
  CHECK(s.setStatus("2", "Done"));
  CHECK(s.issue("2")->resolved == D("2026-10-24"));
  // 未着手 → 進行中では、手で入れた完了日には触らない（完了からの戻りではない）
  CHECK(s.setField("3", [](Issue& o) { o.resolved = D("2026-10-01"); }));
  CHECK(s.setStatus("3", "In Progress"));
  CHECK(s.issue("3")->resolved == D("2026-10-01"));
  // Undo で完了日も戻る
  CHECK(s.setStatus("2", "To Do"));
  CHECK_FALSE(s.issue("2")->resolved.has_value());
  CHECK(s.undo());
  CHECK(s.issue("2")->resolved == D("2026-10-24"));
  CHECK(s.issue("2")->status == "Done");
  // 一括で状況を戻しても同じ（Undo 1 回）
  s.beginBatch();
  CHECK(s.setStatus("2", "To Do"));
  s.endBatch();
  CHECK_FALSE(s.issue("2")->resolved.has_value());
}

TEST_CASE("stamps: アプリで作って完了までやると、推移（作成・リードタイム・滞留）が出る") {
  auto clk = clockAt(2026, 10, 22);
  Session s(dataFrom(kPlain), &clk);
  const auto a = s.addIssue("1", "", "作る A");
  const auto b = s.addIssue("1", a, "作る B");
  clk.set(LocalTime{2026, 10, 26, 9, 0, 0});
  s.setBaseDate("2026-10-26");
  CHECK(s.setStatus(a, "Done"));
  CHECK(s.setName(b, "B の編集"));
  auto run = [&]() {
    return computeTrends(*s.tree(), s.viewIssues(), s.meta(), *s.rows(), s.calendar(), s.baseDay(), {});
  };
  auto r = run();
  REQUIRE(r.ok());
  CHECK(r->hasCreatedColumn);
  CHECK(r->hasUpdatedColumn);
  // 「CSV に Created の列がないので…」の注記は出ない。取り込み済みの課題（作成日なし）は数えない注記だけ残る
  for (const auto& n : trendNotes(*r)) CHECK(n.find("列がない") == std::string::npos);
  CHECK(r->lead.n == 1);          // a: 10/22 → 10/26
  CHECK(r->aging.n >= 1);         // b: Updated から基準日
  CHECK(r->burn.noCreated == 3);  // 作業は 2・4・5（取り込み済み。埋め戻さない）
}
