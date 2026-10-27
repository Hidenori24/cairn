#include <doctest/doctest.h>

#include "../test_util.hpp"
#include "roadmap/commands.hpp"
#include "roadmap/personal.hpp"
#include "roadmap/project_io.hpp"
#include "roadmap/samples.hpp"

using namespace roadmap;
using testutil::D;

namespace {
FixedClock clockAt(int y, int mo, int d) { return FixedClock(LocalTime{y, mo, d, 10, 30, 0}); }

ProjectData dataFrom(const std::string& csvText, const std::string& base = "2026-10-19") {
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

const char* kTree =
    "Issue Id,Issue Type,Summary,Parent,Duration (weeks)\n"
    "1,Epic,E,,\n2,Task,[B-1] A,1,1\n3,Task,B,1,1\n4,Sub-task,C,3,1\n5,Epic,F,,\n";

std::vector<std::string> order(const Session& s) {
  std::vector<std::string> v;
  for (const auto& o : s.issues()) v.push_back(o.id);
  return v;
}
std::string lastNotice(Session& s) {
  auto n = s.takeNotices();
  return n.empty() ? "" : n.back().text;
}
}  // namespace

TEST_CASE("commands: 追加（種別・位置・採番・キー）") {
  auto clk = clockAt(2026, 10, 19);
  auto d = dataFrom(kTree);
  d.settings.key = "DEV";
  Session s(d, &clk);
  const auto top = s.addIssue("", "");
  CHECK(top == "6");
  CHECK(s.issue(top)->type == "Epic");
  CHECK(s.issue(top)->key == "DEV-6");
  CHECK(s.issue(top)->summary == "新しい課題");
  CHECK(s.issue(top)->durW == 1.0);
  const auto t = s.addIssue("1", "3", "X");  // 3 の配下の最後（4）の後ろ
  CHECK(s.issue(t)->type == "Task");
  CHECK(order(s) == std::vector<std::string>{"1", "2", "3", "4", "7", "5", "6"});
  const auto st = s.addIssue("3", "");
  CHECK(s.issue(st)->type == "Sub-task");
  CHECK_FALSE(s.isCollapsed("3"));
}

namespace {
const char* kInherit =
    "Issue Id,Issue Type,Summary,Parent,Assignee,Priority,Labels,Deadline,Duration (weeks)\n"
    "1,Epic,E,,山田,High,a b,2026-12-01,\n"
    "2,Task,T,1,佐藤,Low,x,2026-11-20,3\n"
    "3,Story,S,1,鈴木,Highest,,,2\n";
}

TEST_CASE("commands: 引き継ぎ（既定はオフ。今までどおり）") {
  auto clk = clockAt(2026, 10, 19);
  Session s(dataFrom(kInherit), &clk);
  const auto c = s.addIssue("1", "");
  CHECK(s.issue(c)->assignee.empty());
  CHECK(s.issue(c)->prio == "Medium");
  CHECK(s.issue(c)->labels.empty());
  CHECK_FALSE(s.issue(c)->deadline);
  CHECK(s.issue(c)->durW == 1.0);
  CHECK(s.takeNotices().empty());
}

TEST_CASE("commands: 引き継ぎ（子を追加は親の値。種別は階層で決まる）") {
  auto clk = clockAt(2026, 10, 19);
  Session s(dataFrom(kInherit), &clk);
  InheritSpec sp;
  sp.on = true;
  s.setInherit(sp);
  const auto c = s.addIssue("1", "");
  const Issue* o = s.issue(c);
  CHECK(o->type == "Task");
  CHECK(o->assignee == "山田");
  CHECK(o->prio == "High");
  CHECK_FALSE(o->labels.empty());
  CHECK(o->labels == s.issue("1")->labels);
  CHECK(o->deadline == D("2026-12-01"));
  CHECK(o->durW == 1.0);  // 期間は親からは引き継がない
  const auto n = s.takeNotices();
  REQUIRE(n.size() == 1);
  CHECK(n[0].text.find("親から引き継ぎました") == 0);
  CHECK(n[0].action == NoticeAction::Undo);
  // 孫は Sub-task
  const auto g = s.addIssue("2", "");
  CHECK(s.issue(g)->type == "Sub-task");
  CHECK(s.issue(g)->assignee == "佐藤");
}

TEST_CASE("commands: 引き継ぎ（同じ階層に追加は直前の兄弟のコピー）") {
  auto clk = clockAt(2026, 10, 19);
  Session s(dataFrom(kInherit), &clk);
  InheritSpec sp;
  sp.on = true;
  s.setInherit(sp);
  const auto c = s.addIssue("1", "3");
  const Issue* o = s.issue(c);
  CHECK(o->type == "Story");
  CHECK(o->assignee == "鈴木");
  CHECK(o->prio == "Highest");
  CHECK(o->durW == 2.0);
  CHECK_FALSE(o->deadline);
  const auto n = s.takeNotices();
  REQUIRE(n.size() == 1);
  CHECK(n[0].text.find("直前の兄弟から引き継ぎました") == 0);
  // 最上位の兄弟
  const auto r = s.addIssue("", "1");
  CHECK(s.issue(r)->type == "Epic");
  CHECK(s.issue(r)->assignee == "山田");
  CHECK(s.issue(r)->labels == s.issue("1")->labels);
}

TEST_CASE("commands: 引き継ぎの期限（Deadline 列の無い CSV では Due date を写す）") {
  auto clk = clockAt(2026, 10, 19);
  Session s(dataFrom("Issue Id,Summary,Parent,Due date\n1,P,,\n2,a,1,2026-11-20\n"), &clk);
  InheritSpec sp;
  sp.on = true;
  s.setInherit(sp);
  const auto c = s.addIssue("1", "2");
  CHECK(s.issue(c)->due == D("2026-11-20"));
  CHECK(s.rows()->size() == 3);
}

TEST_CASE("commands: 引き継ぐ項目を選べる") {
  auto clk = clockAt(2026, 10, 19);
  Session s(dataFrom(kInherit), &clk);
  InheritSpec sp;
  sp.on = true;
  sp.assignee = false;
  sp.labels = false;
  sp.deadline = false;
  sp.type = false;
  sp.duration = false;
  s.setInherit(sp);
  const auto c = s.addIssue("1", "2");
  const Issue* o = s.issue(c);
  CHECK(o->assignee.empty());
  CHECK(o->labels.empty());
  CHECK_FALSE(o->deadline);
  CHECK(o->durW == 1.0);
  CHECK(o->prio == "Low");
  // 何も引き継げない（全部外した）ときは通知を出さない
  sp.prio = false;
  s.setInherit(sp);
  s.takeNotices();
  s.addIssue("1", "2");
  CHECK(s.takeNotices().empty());
}

TEST_CASE("commands: 引き継ぎの追加は Undo 1回で戻り、CSV の往復で値が変わらない") {
  auto clk = clockAt(2026, 10, 19);
  Session s(dataFrom(kInherit), &clk);
  InheritSpec sp;
  sp.on = true;
  s.setInherit(sp);
  const auto before = s.issues();
  const auto c1 = s.addIssue("1", "2");
  const auto c2 = s.addIssue("2", "");
  CHECK(s.undo());
  CHECK(s.undo());
  CHECK(s.issues() == before);
  CHECK(s.redo());
  CHECK(s.redo());
  REQUIRE(s.issue(c1));
  REQUIRE(s.issue(c2));
  auto imp = csv::importCsv(s.exportText(ExportKind::App));
  REQUIRE(imp);
  REQUIRE(imp->issues.size() == s.issues().size());
  for (const auto& id : {c1, c2}) {
    const Issue* a = s.issue(id);
    const Issue* b = nullptr;
    for (const auto& o : imp->issues)
      if (o.id == id) b = &o;
    REQUIRE(b);
    CHECK(b->type == a->type);
    CHECK(b->assignee == a->assignee);
    CHECK(b->prio == a->prio);
    CHECK(b->labels == a->labels);
    CHECK(b->deadline == a->deadline);
    CHECK(b->durW == a->durW);
    CHECK(b->parent == a->parent);
  }
}

TEST_CASE("commands: 削除（子孫も・blocks から外す）と通知") {
  auto clk = clockAt(2026, 10, 19);
  auto d = dataFrom("Issue Id,Summary,Parent,Outward issue link (Blocks)\n1,P,,\n2,a,1,\n3,b,,1\n");
  Session s(d, &clk);
  CHECK(s.deleteIssues({"1"}));
  CHECK(order(s) == std::vector<std::string>{"3"});
  CHECK(s.issue("3")->blocks.empty());
  auto n = s.takeNotices();
  REQUIRE(n.size() == 1);
  CHECK(n[0].text == "2件を削除しました");
  CHECK(n[0].action == NoticeAction::Undo);
  CHECK(s.undo());
  CHECK(order(s).size() == 3);
}

TEST_CASE("commands: 上下・インデント・アウトデント・付け替え・移動") {
  auto clk = clockAt(2026, 10, 19);
  Session s(dataFrom(kTree), &clk);
  CHECK(s.moveIssue("3", -1));
  CHECK(order(s) == std::vector<std::string>{"1", "3", "2", "4", "5"});
  CHECK_FALSE(s.moveIssue("3", -1));  // 端
  CHECK(s.moveIssue("3", 1));
  CHECK(s.indent("3"));  // 直前の兄弟（2）の子に
  CHECK(s.issue("3")->parent == "2");
  CHECK_FALSE(s.indent("2"));  // 直前の兄弟がない
  CHECK(s.outdent("3"));
  CHECK(s.issue("3")->parent == "1");
  CHECK(order(s)[2] == "3");  // 元の親（2）の直後
  CHECK_FALSE(s.outdent("1"));
  CHECK(s.reparent("4", "5"));
  CHECK(s.issue("4")->parent == "5");
  CHECK(order(s).back() == "4");
  CHECK_FALSE(s.relocate("1", "2", RelocateWhere::Into));  // 子孫には置けない
  CHECK_FALSE(s.relocate("1", "1", RelocateWhere::After));
  CHECK(s.relocate("5", "1", RelocateWhere::Before));
  CHECK(order(s).front() == "5");
  CHECK(s.relocate("5", "1", RelocateWhere::After));  // 1 の配下の最後の後ろ
  CHECK(order(s) == std::vector<std::string>{"1", "2", "3", "5", "4"});
  CHECK(s.relocate("2", "5", RelocateWhere::Into));
  CHECK(s.issue("2")->parent == "5");
  CHECK(order(s).back() == "2");
}

TEST_CASE("commands: mutate の失敗時の巻き戻し") {
  auto clk = clockAt(2026, 10, 19);
  Session s(dataFrom(kTree), &clk);
  const auto before = s.issues();
  // 親子のループ
  CHECK_FALSE(s.setField("1", [](Issue& o) { o.parent = "4"; }));
  CHECK(s.issues() == before);
  CHECK(lastNotice(s).rfind("その編集はできません: 親子関係がループしています", 0) == 0);
  // 依存の循環
  CHECK(s.addLink("2", "3"));
  CHECK_FALSE(s.addLink("3", "2"));
  CHECK(lastNotice(s).rfind("その編集はできません: 先行・後続の関係がループしています", 0) == 0);
  CHECK(s.error().empty());
  CHECK_FALSE(s.addLink("2", "2"));
  CHECK(lastNotice(s) == "自分自身には依存できません");
  CHECK_FALSE(s.addLink("2", "3"));
  CHECK(lastNotice(s) == "すでに設定されています");
  CHECK(s.removeLink("2", "3"));
  CHECK(s.issue("2")->blocks.empty());
}

TEST_CASE("commands: 選んだ課題を順につなぐ（木の順・表示順・Undo 1回）") {
  auto clk = clockAt(2026, 10, 19);
  Session s(dataFrom(kTree), &clk);
  // 選んだ順（5,3,2）ではなく木の順（2,3,5）で 2→3→5
  const auto r = s.chainIssues({"5", "3", "2"});
  CHECK(r.ok);
  CHECK(r.added == 2);
  CHECK(r.existing == 0);
  CHECK(s.issue("2")->blocks == std::vector<std::string>{"3"});
  CHECK(s.issue("3")->blocks == std::vector<std::string>{"5"});
  CHECK(s.undoSize() == 1);  // 2本のつなぎが Undo 1回
  CHECK(s.undo());
  CHECK(s.issue("2")->blocks.empty());
  CHECK(s.issue("3")->blocks.empty());
  CHECK_FALSE(s.canUndo());
  // 画面の表示順を渡したときはその順（5→3→2）
  s.takeNotices();
  const auto r2 = s.chainIssues({"2", "3", "5"}, {"5", "3", "2"});
  CHECK(r2.ok);
  CHECK(s.issue("5")->blocks == std::vector<std::string>{"3"});
  CHECK(s.issue("3")->blocks == std::vector<std::string>{"2"});
  CHECK(s.undoSize() == 1);
}

TEST_CASE("commands: つなぐ（2件・親と子は上だけ・重複は何もしない・件数が足りない）") {
  auto clk = clockAt(2026, 10, 19);
  Session s(dataFrom(kTree), &clk);
  // 2つ選んで一発
  CHECK(s.chainIssues({"2", "3"}).ok);
  CHECK(s.issue("2")->blocks == std::vector<std::string>{"3"});
  // 親(3)と子(4)を両方選んでも、子は数えない（3→5 だけ）
  s.takeNotices();
  const auto r = s.chainIssues({"3", "4", "5"});
  CHECK(r.ok);
  CHECK(r.added == 1);
  CHECK(s.issue("3")->blocks == std::vector<std::string>{"5"});
  CHECK(s.issue("4")->blocks.empty());
  // 重複は足さない（一部だけ新規）
  const auto before = s.undoSize();
  const auto r2 = s.chainIssues({"2", "3", "5"});
  CHECK(r2.ok);
  CHECK(r2.added == 0);
  CHECK(r2.existing == 2);
  CHECK(s.undoSize() == before);  // 何も変わらなければ Undo に積まない
  CHECK(lastNotice(s) == "すでにつながっています");
  CHECK(s.issue("2")->blocks == std::vector<std::string>{"3"});
  // 1件・存在しない課題・同じ課題の重複指定は、つなぐ相手がない
  CHECK_FALSE(s.chainIssues({"2"}).ok);
  CHECK_FALSE(s.chainIssues({"2", "2", "99"}).ok);
  CHECK(lastNotice(s) == "つなぐ課題を2件以上選んでください");
  CHECK(s.undoSize() == before);
}

TEST_CASE("commands: つなぐと循環になる組み合わせは、何も変えずに理由を通知する") {
  auto clk = clockAt(2026, 10, 19);
  Session s(dataFrom(kTree), &clk);
  CHECK(s.addLink("5", "2"));  // 5 → 2
  s.takeNotices();
  const auto before = s.issues();
  const auto undoBefore = s.undoSize();
  // 2→3 は良いが 3→5 で 5→2 と輪になる。全部やめる
  const auto r = s.chainIssues({"2", "3", "5"});
  CHECK_FALSE(r.ok);
  CHECK(r.added == 0);
  CHECK(s.issues() == before);
  CHECK(s.undoSize() == undoBefore);
  CHECK(lastNotice(s).find("ループ") != std::string::npos);
  CHECK(s.error().empty());
}

TEST_CASE("commands: つなぎを外す（選んだ課題どうしのつなぎだけ・Undo 1回）") {
  auto clk = clockAt(2026, 10, 19);
  Session s(dataFrom(kTree), &clk);
  CHECK(s.chainIssues({"2", "3", "5"}).ok);  // 2→3, 3→5
  CHECK(s.addLink("2", "4"));                // 選ばない課題へのつなぎ
  s.takeNotices();
  const auto undoBefore = s.undoSize();
  // 2,3,5 の間のつなぎ（2→3, 3→5）だけ外す。2→4 は残る
  CHECK(s.unchainIssues({"5", "2", "3"}) == 2);
  CHECK(s.issue("2")->blocks == std::vector<std::string>{"4"});
  CHECK(s.issue("3")->blocks.empty());
  CHECK(s.undoSize() == undoBefore + 1);
  CHECK(s.undo());
  CHECK(s.issue("2")->blocks == std::vector<std::string>{"3", "4"});
  CHECK(s.issue("3")->blocks == std::vector<std::string>{"5"});
  // 間につなぎがなければ何もしない
  CHECK(s.unchainIssues({"2", "5"}) == 0);
  CHECK(lastNotice(s) == "外すつなぎがありません");
  CHECK(s.unchainIssues({"2"}) == 0);
}

TEST_CASE("commands: Undo/Redo（100件の上限・Redo の消去・変更なしは積まない）") {
  auto clk = clockAt(2026, 10, 19);
  Session s(dataFrom(kTree), &clk);
  CHECK(s.setName("3", "B"));  // 変更なし
  CHECK_FALSE(s.canUndo());
  for (int i = 0; i < 120; ++i) CHECK(s.setName("3", "名前" + std::to_string(i)));
  CHECK(s.undoSize() == Session::kUndoMax);
  CHECK(s.undo());
  CHECK(s.issue("3")->summary == "名前118");
  CHECK(s.canRedo());
  CHECK(s.redo());
  CHECK(s.issue("3")->summary == "名前119");
  CHECK(s.undo());
  CHECK(s.setName("3", "新"));
  CHECK_FALSE(s.canRedo());
  int n = 0;
  while (s.undo()) ++n;
  CHECK(n == 100);
  CHECK(s.issue("3")->summary == "名前19");  // 古いものは捨てられている
}

TEST_CASE("commands: bulk は Undo 1回で戻る") {
  auto clk = clockAt(2026, 10, 19);
  Session s(dataFrom(kTree), &clk);
  CHECK(s.bulk({"2", "3", "4"}, [](Issue& o) { o.prio = "High"; }));
  CHECK(s.issue("4")->prio == "High");
  CHECK(s.undo());
  CHECK(s.issue("2")->prio == "");
  CHECK(s.issue("4")->prio == "");
}

TEST_CASE("commands: 状況（完了日・開始日の自動設定）") {
  auto clk = clockAt(2026, 10, 25);  // 今日 > 基準日
  Session s(dataFrom(kTree), &clk);
  CHECK(s.setStatus("2", "Done"));
  CHECK(s.issue("2")->resolved == D("2026-10-19"));  // min(今日, 基準日)
  CHECK(s.setStatus("3", "In Progress"));
  CHECK(s.issue("3")->start == D("2026-10-19"));
  auto clk2 = clockAt(2026, 10, 10);
  Session t(dataFrom(kTree), &clk2);
  CHECK(t.setStatus("2", "完了"));
  CHECK(t.issue("2")->resolved == D("2026-10-10"));
}

TEST_CASE("commands: 番号タグと件名") {
  auto clk = clockAt(2026, 10, 19);
  Session s(dataFrom(kTree), &clk);
  CHECK(s.setTag("3", " [B-2] "));
  CHECK(s.issue("3")->summary == "[B-2] B");
  CHECK_FALSE(s.setTag("3", "b-1"));  // 大文字小文字を無視して重複を拒否
  CHECK(lastNotice(s) == "「b-1」はすでに別の課題（A）で使われています");
  CHECK(s.setTag("3", ""));
  CHECK(s.issue("3")->summary == "B");
  CHECK(s.setName("2", "新しい名前"));
  CHECK(s.issue("2")->summary == "[B-1] 新しい名前");
  CHECK(s.setName("2", ""));
  CHECK(s.issue("2")->summary == "[B-1] （無題）");
}

TEST_CASE("commands: 期間とシナリオ") {
  auto clk = clockAt(2026, 10, 19);
  Session s(dataFrom(kTree), &clk);
  CHECK(s.setDuration("2", 3));
  CHECK(s.issue("2")->durW == 3.0);
  s.addScenario("速い");
  CHECK(lastNotice(s) == "シナリオ「速い」を追加しました。期間を変えると、このシナリオだけに保存されます");
  CHECK(s.settings().scenario == "速い");
  CHECK(s.setDuration("2", 1.5));
  CHECK(*s.issue("2")->alt.find("速い") == 1.5);
  CHECK(s.issue("2")->durW == 3.0);
  CHECK(s.rows()->at(s.find("2")).weeks == 1.5);
  CHECK(s.removeScenarioValue("2"));
  CHECK(s.issue("2")->alt.empty());
  s.setScenario("ない名前");
  CHECK(s.settings().scenario == "標準");
}

TEST_CASE("commands: 着手可能日（動かせないときは戻す）") {
  auto clk = clockAt(2026, 10, 19);
  Session s(dataFrom("Issue Id,Summary,Outward issue link (Blocks),Duration (weeks)\n1,a,2,2\n2,b,,1\n"), &clk);
  CHECK(s.setEarliest("1", D("2026-11-02")));
  CHECK(s.issue("1")->notBefore == D("2026-11-02"));
  CHECK(s.rows()->at(s.find("1")).asapStart == D("2026-11-02"));
  CHECK(s.undo());
  CHECK_FALSE(s.issue("1")->notBefore);
  // 2 は 1 の完了（10/30）の後でないと始められない
  CHECK(s.setEarliest("2", D("2026-10-20")));
  CHECK_FALSE(s.issue("2")->notBefore);
  CHECK(lastNotice(s) == "その日には動かせません（先行や、同じ担当者の作業が終わるまで待つため）");
}

TEST_CASE("commands: 期限（Deadline 列がなければ Due date 由来の期限を移す）") {
  auto clk = clockAt(2026, 10, 19);
  Session s(dataFrom("Issue Id,Summary,Due date,Duration (weeks)\n1,a,2026-12-01,1\n2,b,2026-12-10,1\n"), &clk);
  CHECK_FALSE(s.meta().hasDeadlineCol);
  CHECK(s.setDeadline("1", D("2026-11-20")));
  CHECK(s.meta().hasDeadlineCol);
  CHECK(s.issue("1")->deadline == D("2026-11-20"));
  CHECK(s.issue("2")->deadline == D("2026-12-10"));
  CHECK(s.exportText(ExportKind::App).find("Deadline") != std::string::npos);
}

TEST_CASE("commands: コメント") {
  auto clk = clockAt(2026, 10, 19);
  Session s(dataFrom(kTree), &clk);
  CHECK_FALSE(s.addComment("2", "  ", "me"));
  CHECK(s.addComment("2", " 本文 ", "a;b"));
  REQUIRE(s.issue("2")->comments.size() == 1);
  CHECK(s.issue("2")->comments[0].who == "a；b");
  CHECK(s.issue("2")->comments[0].text == "本文");
  CHECK(s.issue("2")->comments[0].at == "2026-10-19T10:30");
  CHECK(s.delComment("2", 0));
  CHECK(s.issue("2")->comments.empty());
}

TEST_CASE("commands: 参照の解決") {
  auto clk = clockAt(2026, 10, 19);
  Session s(dataFrom(kTree), &clk);
  CHECK(s.resolveRef("B-1") == std::optional<std::string>("2"));
  CHECK(s.resolveRef("b-1") == std::optional<std::string>("2"));
  CHECK(s.resolveRef("3") == std::optional<std::string>("3"));
  CHECK(s.resolveRef(" C ") == std::optional<std::string>("4"));
  CHECK_FALSE(s.resolveRef("ない"));
  CHECK_FALSE(s.resolveRef(""));
}

TEST_CASE("commands: テンプレートの適用（番号タグの採番・先行・Undo 1回）") {
  auto clk = clockAt(2026, 10, 19);
  Session s(dataFrom(kTree), &clk);
  const Template& tpl = builtinTemplates()[2];  // 作成 → レビュー → 承認
  CHECK(s.applyTemplate({"2", "3"}, tpl) == 8);
  CHECK(lastNotice(s) == "8件の子タスクを追加しました");
  // [B-1] の子は [B-1-1]…
  const Tree* T = s.tree();
  const int b1 = T->find("2");
  REQUIRE(T->kids[b1].size() == 4);
  CHECK(s.issues()[T->kids[b1][0]].summary == "[B-1-1] Aの作成");
  CHECK(s.issues()[T->kids[b1][1]].summary == "[B-1-2] レビュー");
  CHECK(s.issues()[T->kids[b1][0]].blocks == std::vector<std::string>{s.issues()[T->kids[b1][1]].id});
  CHECK(s.issues()[T->kids[b1][3]].durW == 0.2);
  CHECK(s.issues()[T->kids[b1][0]].type == "Sub-task");
  // タグのない親
  const int b = T->find("3");
  REQUIRE(T->kids[b].size() == 5);  // 既存の C + 4件
  CHECK(s.issues()[T->kids[b][1]].summary == "Bの作成");
  CHECK(s.undo());
  CHECK(s.issues().size() == 5);
}

TEST_CASE("commands: テンプレートの入れ子とタグの続き番号") {
  auto clk = clockAt(2026, 10, 19);
  Session s(dataFrom("Issue Id,Issue Type,Summary,Parent\n1,Epic,[E] 親,\n2,Task,[E-1] 既存,1\n"), &clk);
  Template t;
  t.id = "u:x";
  t.name = "x";
  t.items = {{"上", 1.0, 0, {}, "", {}, ""}, {"下", 2.0, 1, {}, "", {}, ""}, {"次", std::nullopt, 0, {0}, "", {}, ""}};
  CHECK(s.applyTemplate({"1"}, t) == 3);
  const auto& v = s.issues();
  CHECK(v[2].summary == "[E-2] 上");
  CHECK(v[2].type == "Task");  // 親が Epic
  CHECK(v[3].summary == "[E-2-1] 下");
  CHECK(v[3].parent == v[2].id);
  CHECK(v[3].type == "Sub-task");
  CHECK(v[4].summary == "[E-3] 次");
  CHECK(v[4].durW == 1.0);
  CHECK(v[2].blocks == std::vector<std::string>{v[4].id});
}

TEST_CASE("commands: テンプレートとして保存する項目") {
  auto clk = clockAt(2026, 10, 19);
  Session s(dataFrom("Issue Id,Issue Type,Summary,Parent,Outward issue link (Blocks),Duration (weeks),Labels\n"
                     "1,Task,P,,,,\n2,Task,[P-1] a,1,3,2,x\n3,Milestone,[P-2] b,1,,0,\n"),
            &clk);
  auto items = templateItemsFrom(*s.tree(), s.issues(), s.find("1"), true);
  REQUIRE(items.size() == 2);
  CHECK(items[0].n == "a");
  CHECK(items[0].w == 2.0);
  CHECK(items[0].lab == std::vector<std::string>{"x"});
  CHECK(items[1].after == std::vector<int>{0});
  CHECK(items[1].t == "Milestone");
  auto j = templateToJson(Template{"u:1", false, "名前", "", items});
  auto back = templateFromJson(j);
  REQUIRE(back);
  CHECK(back->items.size() == 2);
}

TEST_CASE("commands: 置き換えの取り込みは Undo で戻せる") {
  auto clk = clockAt(2026, 10, 19);
  Session s(dataFrom(kTree), &clk);
  auto imp = csv::importCsv("Summary\nX\nY\n");
  REQUIRE(imp.ok());
  s.replaceIssues(imp->issues, imp->meta, imp->warns);
  CHECK(s.issues().size() == 2);
  CHECK(lastNotice(s) == "2件を読み込みました（置き換え）");
  CHECK(s.undo());
  CHECK(s.issues().size() == 5);
}

TEST_CASE("commands: 担当者の並び（簡易な日本語照合）") {
  auto clk = clockAt(2026, 10, 19);
  Session s(dataFrom("Summary,Assignee\na,スズキ\nb,あべ\nc,すずき\nd,Bob\ne,あべ\n"), &clk);
  CHECK(s.assignees() == std::vector<std::string>{"Bob", "あべ", "すずき", "スズキ"});
}

TEST_CASE("commands: サンプルを開ける") {
  auto clk = clockAt(2026, 10, 19);
  for (auto sv : {samples::hierarchyCsv(), samples::levelingCsv(), samples::embeddedSwCsv()}) {
    auto rec = newRecord("s", "", std::string(sv), samples::kSampleBase, clk);
    auto h = hydrate(rec);
    REQUIRE(h.ok());
    Session s(h->data, &clk, h->warns);
    CHECK(s.error().empty());
    CHECK(s.summary().total > 0);
  }
}

TEST_CASE("personal: progress records changes and preserves missing versus zero") {
  auto clk = clockAt(2026, 10, 26);
  Session s(dataFrom(kTree), &clk);
  s.setStatus("2", "In Progress");
  CHECK_FALSE(personalPercent(s.personal(), "2"));
  REQUIRE(s.setProgress("2", 0));
  CHECK(personalPercent(s.personal(), "2") == 0);
  const auto count = s.personal()["history"].size();
  s.touchUi();
  CHECK(s.personal()["history"].size() == count);
  REQUIRE(s.setProgress("2", 75));
  CHECK(s.personal()["history"].size() == count + 1);
  CHECK_FALSE(s.setProgress("2", 101));
  s.setReadOnly(true);
  CHECK_FALSE(s.setProgress("2", 25));
  CHECK(personalPercent(s.personal(), "2") == 75);
}
