// 「先行」列の入力（Issue #9）: 書き方の解析・検証・差分の反映・並べ替え/絞り込みのための値
#include <doctest/doctest.h>

#include <algorithm>

#include "roadmap/commands.hpp"
#include "roadmap/pred_edit.hpp"
#include "roadmap/project_io.hpp"

using namespace roadmap;

namespace {
FixedClock clk(LocalTime{2026, 10, 19, 9, 0, 0});

Session fromCsv(const char* csvText) {
  ProjectData d;
  auto imp = csv::importCsv(csvText);
  if (!imp) FAIL(imp.error());
  d.issues = imp->issues;
  d.meta = imp->meta;
  d.settings.baseDate = "2026-10-19";
  d.settings.ui = defaultUi();
  return Session(d, &clk);
}

// 表示ID = キー。1(DEV-1, Epic) ─ 2(DEV-2) / 3(DEV-3) / 4(DEV-4) ／ 5(DEV-5) / 6(DEV-6)
Session make() {
  return fromCsv(
      "Issue Id,Issue key,Issue Type,Summary,Parent,Duration (weeks)\n"
      "1,DEV-1,Epic,設計,,\n"
      "2,DEV-2,Task,画面,1,1\n"
      "3,DEV-3,Task,DB,1,2\n"
      "4,DEV-4,Task,API,1,1\n"
      "5,DEV-5,Task,結合試験,,1\n"
      "6,DEV-6,Task,総合試験,,1\n");
}

int idx(const Session& s, const char* id) { return s.tree()->find(id); }

PredPlan plan(const Session& s, const char* self, const char* text) {
  return planPredEdit(*s.tree(), s.viewIssues(), idx(s, self), text);
}

std::vector<std::string> blocksOf(const Session& s, const char* id) { return s.issue(id)->blocks; }
}  // namespace

TEST_CASE("pred: 書き方の解析（区切り・空白・引用符）") {
  const auto refs = [](const char* t) {
    std::vector<std::string> v;
    for (const auto& k : parsePredText(t)) v.push_back(k.ref);
    return v;
  };
  using V = std::vector<std::string>;
  CHECK(refs("DEV-12, DEV-15") == V{"DEV-12", "DEV-15"});
  CHECK(refs("  DEV-12 ,DEV-15  ") == V{"DEV-12", "DEV-15"});
  CHECK(refs("A，B、C;D；E\nF\r\nG") == V{"A", "B", "C", "D", "E", "F", "G"});
  CHECK(refs("").empty());
  CHECK(refs(" ,, ,").empty());
  CHECK(refs("Phase 1, DEV-2") == V{"Phase 1", "DEV-2"});  // 表示ID の中の空白は区切りにしない
  CHECK(refs("\"DEV-1, DEV-2\"") == V{"DEV-1", "DEV-2"});  // Excel のセルの引用符
  CHECK(parsePredText("DEV-1")[0].raw == "DEV-1");
}

TEST_CASE("pred: いまの先行の逆引きと表示") {
  Session s = make();
  CHECK(s.addLink("2", "4"));
  CHECK(s.addLink("3", "4"));
  CHECK(s.addLink("5", "4"));
  CHECK(s.addLink("4", "4") == false);
  const Tree& T = *s.tree();
  const auto dp = declaredPreds(T, s.viewIssues());
  REQUIRE(dp.size() == 6);
  CHECK(formatPreds(T, dp[static_cast<std::size_t>(idx(s, "4"))]) == "DEV-2, DEV-3, DEV-5");  // 木の順
  CHECK(dp[static_cast<std::size_t>(idx(s, "2"))].empty());
  CHECK(formatPredsForEdit(T, s.viewIssues(), dp[static_cast<std::size_t>(idx(s, "4"))]) == "DEV-2, DEV-3, DEV-5");
}

TEST_CASE("pred: 差分だけを計画する（追加・削除・変更なし）") {
  Session s = make();
  s.addLink("2", "4");
  s.addLink("3", "4");
  // 追加だけ
  auto p = plan(s, "5", "DEV-2, DEV-3");
  CHECK(p.ok);
  CHECK(p.add == std::vector<std::string>{"2", "3"});
  CHECK(p.remove.empty());
  // 追加と削除
  p = plan(s, "4", "DEV-3, DEV-5");
  CHECK(p.ok);
  CHECK(p.add == std::vector<std::string>{"5"});
  CHECK(p.remove == std::vector<std::string>{"2"});
  // 変わらない（順番・大文字小文字・空白は無関係）
  p = plan(s, "4", "dev-3 ,  DEV-2");
  CHECK(p.ok);
  CHECK_FALSE(p.changed());
  // 空にすると全部外す
  p = plan(s, "4", "");
  CHECK(p.ok);
  CHECK(p.add.empty());
  CHECK(p.remove == std::vector<std::string>{"2", "3"});
}

TEST_CASE("pred: 参照は表示ID・キー・id のどれでもよい") {
  Session s = fromCsv(
      "Issue Id,Issue Type,Summary,Parent,Duration (weeks)\n"
      "10,Task,[認証] ログイン,,1\n"
      "11,Task,[認証] 登録,,1\n"
      "12,Task,単独,,1\n");
  // 表示ID はタグ（認証）。2 つが同じ表示ID なので、タグでは決まらない
  auto p = plan(s, "12", "認証");
  CHECK_FALSE(p.ok);
  REQUIRE(p.problems.size() == 1);
  CHECK(p.problems[0].reason.find("複数") != std::string::npos);
  // id なら決まる
  p = plan(s, "12", "10, 11");
  CHECK(p.ok);
  CHECK(p.add == std::vector<std::string>{"10", "11"});
}

TEST_CASE("pred: 検証エラー（存在しない・重複・自分・親子・循環）は理由つきで、何も反映しない") {
  Session s = make();
  s.addLink("2", "3");
  const auto reason = [&](const char* self, const char* text) {
    const auto p = plan(s, self, text);
    CHECK_FALSE(p.ok);
    CHECK(p.add.empty());
    CHECK(p.remove.empty());
    return p.problems.empty() ? std::string() : p.problems[0].reason;
  };
  CHECK(reason("4", "DEV-99").find("見つかりません") != std::string::npos);
  CHECK(reason("4", "DEV-99 DEV-2").find("カンマ") != std::string::npos);  // 区切り忘れのヒント
  CHECK(reason("4", "DEV-2, dev-2").find("重複") != std::string::npos);
  CHECK(reason("4", "DEV-4").find("自分自身") != std::string::npos);
  CHECK(reason("2", "DEV-1").find("親子") != std::string::npos);  // 親（Epic）
  CHECK(reason("1", "DEV-3").find("親子") != std::string::npos);  // 子
  CHECK(reason("2", "DEV-3").find("循環") != std::string::npos);  // DEV-2 → DEV-3 がある
  // 有効な項目が混じっていても、1 つでも問題があれば全体を確定しない
  const auto p = plan(s, "4", "DEV-2, DEV-99, DEV-5");
  CHECK_FALSE(p.ok);
  CHECK(p.problems.size() == 1);
  CHECK(p.problems[0].token == "DEV-99");
  // 問題はすべて列挙する
  CHECK(plan(s, "4", "X1, X2").problems.size() == 2);
}

TEST_CASE("pred: Session に差分だけを反映し、Undo 1回で戻る") {
  Session s = make();
  s.addLink("2", "4");
  s.addLink("3", "4");
  const auto before = s.issues();
  const std::size_t u0 = s.undoSize();
  const auto r = s.setPredecessors("4", "DEV-3, DEV-5, DEV-6");
  CHECK(r.ok);
  CHECK(blocksOf(s, "2").empty());                              // 外した
  CHECK(blocksOf(s, "3") == std::vector<std::string>{"4"});     // そのまま
  CHECK(blocksOf(s, "5") == std::vector<std::string>{"4"});     // 足した
  CHECK(blocksOf(s, "6") == std::vector<std::string>{"4"});
  CHECK(s.undoSize() == u0 + 1);
  const auto notes = s.takeNotices();
  REQUIRE_FALSE(notes.empty());
  CHECK(notes.back().action == NoticeAction::Undo);
  CHECK(s.undo());
  CHECK(s.issues() == before);
  // 変更なしなら履歴を増やさない
  const std::size_t u1 = s.undoSize();
  CHECK(s.setPredecessors("4", "DEV-2, DEV-3").ok);
  CHECK(s.undoSize() == u1);
}

TEST_CASE("pred: 問題があれば Session は何も変えない") {
  Session s = make();
  s.addLink("2", "3");
  const auto before = s.issues();
  const std::size_t u0 = s.undoSize();
  const auto r = s.setPredecessors("2", "DEV-3");  // 循環
  CHECK_FALSE(r.ok);
  CHECK(s.issues() == before);
  CHECK(s.undoSize() == u0);
  CHECK_FALSE(s.setPredecessors("nope", "DEV-3").ok);
}

TEST_CASE("pred: 親の課題にも先行を入れられ、子の日程に効く") {
  Session s = make();
  const auto before = (*s.rows())[static_cast<std::size_t>(idx(s, "2"))].asapStart;
  CHECK(s.setPredecessors("1", "DEV-5").ok);  // 設計(Epic) は 結合試験 のあと
  CHECK(blocksOf(s, "5") == std::vector<std::string>{"1"});
  CHECK((*s.rows())[static_cast<std::size_t>(idx(s, "2"))].asapStart > before);
}

TEST_CASE("pred: 入力中の候補（最後の項目で絞る・入力済み・つなげない相手は出さない）") {
  Session s = make();
  s.addLink("2", "3");
  const Tree& T = *s.tree();
  const auto ids = [&](const char* self, const char* text) {
    std::vector<std::string> v;
    for (int i : predSuggestions(T, s.viewIssues(), idx(s, self), text, 10)) v.push_back(T.ids[static_cast<std::size_t>(i)]);
    return v;
  };
  using V = std::vector<std::string>;
  CHECK(ids("4", "dev-5") == V{"5"});
  CHECK(ids("4", "試験") == V{"5", "6"});             // 件名でも探せる
  CHECK(ids("4", "DEV-5, DEV-") == V{"2", "3", "6"});  // 入力済み（DEV-5）・自分・親（親子）は出さない
  CHECK(ids("2", "DEV-3").empty());                   // DEV-2 → DEV-3 があるので、DEV-3 を先行にすると循環
  CHECK(predSuggestions(T, s.viewIssues(), idx(s, "4"), "", 2).size() == 2);  // 空なら先頭から limit 件
}

TEST_CASE("pred: 絞り込み（先行あり / なし）と値の一覧") {
  Session s = make();
  s.addLink("2", "3");
  s.addLink("3", "4");
  const Filter none;
  ColumnFilter cf;
  const auto leaves = [&] {
    std::vector<std::string> v;
    for (int i : s.visibleLeaves(none, &cf)) v.push_back(s.issues()[static_cast<std::size_t>(i)].id);
    return v;
  };
  // 値は葉の出現順（DEV-2 は先行なし、DEV-3 は先行あり）
  CHECK(columnValues("pred", *s.tree(), s.viewIssues(), *s.rows()) ==
        std::vector<std::string>{kNoneValue, kHasPredValue});
  CHECK(leaves() == std::vector<std::string>{"2", "3", "4", "5", "6"});
  cf.toggle("pred", kNoneValue);  // 先行なしを隠す
  CHECK(cf.active());
  CHECK(leaves() == std::vector<std::string>{"3", "4"});
  cf.clear("pred");
  cf.toggle("pred", kHasPredValue);  // 先行ありを隠す
  CHECK(leaves() == std::vector<std::string>{"2", "5", "6"});
}

TEST_CASE("pred: Tab 補完は入力中の項目を先頭の候補に置き換える") {
  Session s = make();
  const Tree& T = *s.tree();
  const auto comp = [&](const char* self, const char* text) {
    return completePredText(T, s.viewIssues(), idx(s, self), text);
  };
  CHECK(comp("4", "dev-5") == "DEV-5");
  CHECK(comp("4", "DEV-2, 総合") == "DEV-2, DEV-6");
  CHECK(comp("4", "DEV-2,総合") == "DEV-2, DEV-6");
  CHECK(comp("4", "DEV-2, ") == "DEV-2, DEV-3");  // 入力済み（DEV-2）は候補にしない
  CHECK(comp("4", "zzz") == "zzz");               // 候補なしはそのまま
}

TEST_CASE("pred: 表示ID が区切りを含む（件名のタグ）ときは、キーか id に切り替えて、1項目として読み戻せる") {
  Session s = fromCsv(
      "Issue Id,Issue key,Issue Type,Summary,Parent,Duration (weeks)\n"
      "1,DEV-1,Task,[設計、実装] 画面,,1\n"
      "2,DEV-2,Task,結合,,1\n");
  const Tree& T = *s.tree();
  const int a = idx(s, "1"), b = idx(s, "2");
  REQUIRE(T.did[static_cast<std::size_t>(a)].find("、") != std::string::npos);  // 前提: 表示ID に「、」が入る
  const std::string ref = formatPredsForEdit(T, s.viewIssues(), {a});
  CHECK(ref == "DEV-1");
  const auto tk = parsePredText(ref);
  REQUIRE(tk.size() == 1);
  CHECK(tk[0].ref == ref);
  // 先行に追加しても入力エラーにならない（Tab 補完・初期値と同じ書き方）
  const PredPlan p = planPredEdit(T, s.viewIssues(), b, ref);
  CHECK(p.ok);
  CHECK(completePredText(T, s.viewIssues(), b, "画面") == "DEV-1");
}
