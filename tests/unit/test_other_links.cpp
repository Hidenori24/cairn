// Blocks 以外の JIRA リンク（Issue #12）: 解釈・往復・編集・日程に影響しないこと
#include <doctest/doctest.h>

#include "roadmap/commands.hpp"
#include "roadmap/csv.hpp"
#include "roadmap/other_links.hpp"
#include "roadmap/project_io.hpp"

using namespace roadmap;

namespace {
csv::ImportResult imp(const std::string& text) {
  auto r = csv::importCsv(text);
  if (!r.ok()) FAIL(r.error());
  return std::move(r).value();
}
FixedClock clk(LocalTime{2026, 10, 19, 9, 0, 0});
Session makeSession(const csv::ImportResult& r) {
  ProjectData d;
  d.issues = r.issues;
  d.meta = r.meta;
  d.settings.baseDate = "2026-10-19";
  d.settings.ui = defaultUi();
  return Session(d, &clk);
}
const char* kCsv =
    "Issue Id,Issue key,Summary,Parent,Duration (weeks),Outward issue link (Blocks),Outward issue link (Relates),"
    "Inward issue link (Relates),Outward issue link (Duplicate),Sprint\r\n"
    "1,PRJ-1,設計,,1,2,PRJ-3,,,S1\r\n"
    "2,PRJ-2,実装,,2,,,PRJ-1,PRJ-99,S1\r\n"
    "3,PRJ-3,試験,,1,,3,,,S2\r\n";
}  // namespace

TEST_CASE("other_links: 見出しの解釈") {
  auto k = linkColumnOf("Outward issue link (Relates)");
  REQUIRE(k);
  CHECK(k->kind == "Relates");
  CHECK(k->outward);
  k = linkColumnOf("Inward issue link (Duplicate)");
  REQUIRE(k);
  CHECK(k->kind == "Duplicate");
  CHECK_FALSE(k->outward);
  k = linkColumnOf("  outward ISSUE link (Cloners)  ");
  REQUIRE(k);
  CHECK(k->kind == "Cloners");
  k = linkColumnOf("外向き (Relates)");
  REQUIRE(k);
  CHECK(k->kind == "Relates");
  CHECK(k->outward);
  k = linkColumnOf("内向き（関連）");
  REQUIRE(k);
  CHECK(k->kind == "関連");
  CHECK_FALSE(k->outward);
  // 括弧がなければ接頭辞より後ろ
  k = linkColumnOf("Outward issue link");
  REQUIRE(k);
  CHECK(k->kind.empty());
  // 日程に使う Blocks と、リンクではない列は対象外
  CHECK_FALSE(linkColumnOf("Outward issue link (Blocks)"));
  CHECK_FALSE(linkColumnOf("Inward issue link (Blocks)"));
  CHECK_FALSE(linkColumnOf("外向き (block)"));
  CHECK_FALSE(linkColumnOf("Sprint"));
  CHECK_FALSE(linkColumnOf("Issue link (Relates)"));
  CHECK_FALSE(linkColumnOf(""));
}

TEST_CASE("other_links: 取り込んだリンクを種類ごとに取り出す（相手がなければ文字のまま）") {
  auto r = imp(kCsv);
  REQUIRE(r.meta.extraHeaders.size() == 4);  // Relates 2 列・Duplicate・Sprint
  const auto l1 = otherLinksOf(r.meta, r.issues, 0);
  REQUIRE(l1.size() == 1);
  CHECK(l1[0].kind == "Relates");
  CHECK(l1[0].outward);
  CHECK(l1[0].ref == "PRJ-3");
  CHECK(l1[0].target == 2);
  CHECK(l1[0].col == 0);
  const auto l2 = otherLinksOf(r.meta, r.issues, 1);
  REQUIRE(l2.size() == 2);
  CHECK(l2[0].kind == "Relates");
  CHECK_FALSE(l2[0].outward);
  CHECK(l2[0].target == 0);
  CHECK(l2[1].kind == "Duplicate");
  CHECK(l2[1].ref == "PRJ-99");
  CHECK(l2[1].target == -1);  // 存在しない相手
  // Issue Id でも引ける
  const auto l3 = otherLinksOf(r.meta, r.issues, 2);
  REQUIRE(l3.size() == 1);
  CHECK(l3[0].target == 2);
  CHECK(findLinkTarget(r.issues, "") == -1);
  CHECK(findLinkTarget(r.issues, "PRJ-2") == 1);
  CHECK(findLinkTarget(r.issues, "pRj-2") == -1);  // 完全一致
  // 種類の一覧（出現順・重複なし）
  CHECK(otherLinkKinds(r.meta) == std::vector<std::string>{"Relates", "Duplicate"});
  // extra が短い課題でも落ちない
  r.issues[0].extra.clear();
  CHECK(otherLinksOf(r.meta, r.issues, 0).empty());
}

TEST_CASE("other_links: 往復（取り込み → 書き出し）で列名・並び・値が変わらない") {
  auto r = imp(kCsv);
  const std::string out = csv::exportCsv(r.issues, r.meta);
  CHECK(out.find("Outward issue link (Relates),Inward issue link (Relates),Outward issue link (Duplicate),Sprint\r\n") !=
        std::string::npos);
  CHECK(out.find(",PRJ-3,,,S1\r\n") != std::string::npos);
  CHECK(out.find(",,PRJ-1,PRJ-99,S1\r\n") != std::string::npos);
  auto r2 = imp(out);
  CHECK(csv::exportCsv(r2.issues, r2.meta) == out);
  CHECK(r2.meta.extraHeaders == r.meta.extraHeaders);
  for (std::size_t i = 0; i < r.issues.size(); ++i) CHECK(r2.issues[i].extra == r.issues[i].extra);
  // JIRA 用の書き出しでも列は残る
  csv::ExportOptions jira;
  jira.planned = true;
  const std::string j = csv::exportCsv(r.issues, r.meta, jira);
  CHECK(j.find("Outward issue link (Relates),Inward issue link (Relates),Outward issue link (Duplicate),Sprint") !=
        std::string::npos);
  CHECK(j.find("PRJ-99") != std::string::npos);
}

TEST_CASE("other_links: 日程の計算には影響しない") {
  const std::string plain =
      "Issue Id,Issue key,Summary,Parent,Duration (weeks),Outward issue link (Blocks)\r\n"
      "1,PRJ-1,設計,,1,2\r\n2,PRJ-2,実装,,2,\r\n3,PRJ-3,試験,,1,\r\n";
  Session a = makeSession(imp(plain));
  Session b = makeSession(imp(kCsv));
  REQUIRE(a.rows());
  REQUIRE(b.rows());
  REQUIRE(a.rows()->size() == b.rows()->size());
  const auto same = [&] {
    for (std::size_t i = 0; i < a.rows()->size(); ++i) {
      CHECK((*a.rows())[i].asapStart == (*b.rows())[i].asapStart);
      CHECK((*a.rows())[i].asapFinish == (*b.rows())[i].asapFinish);
      CHECK((*a.rows())[i].alapFinish == (*b.rows())[i].alapFinish);
    }
  };
  same();
  // リンクを足しても変わらない（先行・後続にも入らない）
  REQUIRE(b.addOtherLink("3", "Relates", true, "PRJ-1"));
  same();
  CHECK(b.tree()->preds[2].empty());
  CHECK(b.issue("3")->blocks.empty());
  CHECK(b.allWarns() == a.allWarns());
}

TEST_CASE("other_links: 種類の名前の検査") {
  CHECK_FALSE(checkLinkKind("Relates"));
  CHECK_FALSE(checkLinkKind("関連"));
  CHECK_FALSE(checkLinkKind("Is cloned by"));
  CHECK(checkLinkKind(""));
  CHECK(checkLinkKind("   "));
  CHECK(checkLinkKind("a(b)"));
  CHECK(checkLinkKind("a）"));
  CHECK(checkLinkKind("a\nb"));
  CHECK(checkLinkKind("Blocks"));  // 日程に使う列になってしまう
  CHECK(checkLinkKind("blocked by"));
  CHECK(checkLinkKind("Start date"));
}

TEST_CASE("other_links: 追加は同じ種類・向きの空のセルへ、なければ列を末尾に足す") {
  auto r = imp(kCsv);
  // 課題 3 の Inward(Relates) は空のセル → 列は増えない
  CHECK(addOtherLink(r.meta, r.issues, 2, "relates", false, " PRJ-2 ").empty());
  CHECK(r.meta.extraHeaders.size() == 4);
  auto l = otherLinksOf(r.meta, r.issues, 2);
  REQUIRE(l.size() == 2);
  CHECK(l[0].col == 0);
  CHECK(l[1].col == 1);
  CHECK(l[1].ref == "PRJ-2");  // 前後の空白は除く
  // 課題 3 の Outward(Relates) はすでに埋まっている → 同じ種類でも新しい列
  CHECK(addOtherLink(r.meta, r.issues, 2, "Relates", true, "PRJ-2").empty());
  REQUIRE(r.meta.extraHeaders.size() == 5);
  CHECK(r.meta.extraHeaders[4] == "Outward issue link (Relates)");
  for (const auto& o : r.issues) CHECK(o.extra.size() == 5);
  // 新しい種類
  CHECK(addOtherLink(r.meta, r.issues, 0, "Cloners", false, "PRJ-2").empty());
  CHECK(r.meta.extraHeaders.back() == "Inward issue link (Cloners)");
  // 往復しても変わらない
  const std::string out = csv::exportCsv(r.issues, r.meta);
  auto r2 = imp(out);
  CHECK(csv::exportCsv(r2.issues, r2.meta) == out);
  CHECK(otherLinksOf(r2.meta, r2.issues, 0).size() == 2);
  // 失敗は何も変えない
  const auto before = r.meta;
  const auto issuesBefore = r.issues;
  CHECK_FALSE(addOtherLink(r.meta, r.issues, 0, "Relates", true, "").empty());       // 相手が空
  CHECK_FALSE(addOtherLink(r.meta, r.issues, 0, "Blocks", true, "PRJ-2").empty());   // 種類が不可
  CHECK_FALSE(addOtherLink(r.meta, r.issues, 0, "Relates", true, "PRJ-1").empty());  // 自分自身（キー）
  CHECK_FALSE(addOtherLink(r.meta, r.issues, 0, "Relates", true, "1").empty());      // 自分自身（Issue Id）
  CHECK_FALSE(addOtherLink(r.meta, r.issues, 0, "relates", true, "PRJ-3").empty());  // 同じリンクがすでにある
  CHECK_FALSE(addOtherLink(r.meta, r.issues, 9, "Relates", true, "PRJ-2").empty());  // 課題がない
  CHECK(r.meta == before);
  CHECK(r.issues == issuesBefore);
  // 相手が存在しない文字でも足せる（JIRA 側の課題など）
  CHECK(addOtherLink(r.meta, r.issues, 0, "Relates", true, "OTHER-7").empty());
}

TEST_CASE("other_links: 削除はセルを空にするだけ（列は残る）") {
  auto r = imp(kCsv);
  const auto headers = r.meta.extraHeaders;
  REQUIRE(otherLinksOf(r.meta, r.issues, 1).size() == 2);
  CHECK(removeOtherLink(r.meta, r.issues, 1, 2));  // Duplicate の列
  CHECK(otherLinksOf(r.meta, r.issues, 1).size() == 1);
  CHECK(r.meta.extraHeaders == headers);
  CHECK(r.issues[1].extra[2].empty());
  CHECK(r.issues[1].extra[3] == "S1");  // 隣の列は無関係
  CHECK_FALSE(removeOtherLink(r.meta, r.issues, 1, 2));  // すでに空
  CHECK_FALSE(removeOtherLink(r.meta, r.issues, 1, 3));  // リンク列ではない（Sprint）
  CHECK_FALSE(removeOtherLink(r.meta, r.issues, 1, 99));
  CHECK_FALSE(removeOtherLink(r.meta, r.issues, 9, 0));
}

TEST_CASE("other_links: Session の追加・削除は Undo できる") {
  Session s = makeSession(imp(kCsv));
  const auto metaBefore = s.meta();
  CHECK(s.addOtherLink("2", "Cloners", true, "PRJ-3"));
  CHECK(s.meta().extraHeaders.size() == metaBefore.extraHeaders.size() + 2);  // リンクの列と、編集の記録（Updated。Issue #66）
  CHECK(s.canUndo());
  CHECK(s.undo());
  CHECK(s.meta() == metaBefore);
  // 失敗は通知して何も変えない
  s.takeNotices();
  const auto v = s.undoSize();
  CHECK_FALSE(s.addOtherLink("2", "Blocks", true, "PRJ-3"));
  CHECK_FALSE(s.addOtherLink("nope", "Relates", true, "PRJ-3"));
  CHECK(s.undoSize() == v);
  CHECK_FALSE(s.takeNotices().empty());
  // 削除
  const std::size_t col = otherLinksOf(s.meta(), s.issues(), 1)[0].col;
  CHECK(s.removeOtherLink("2", col));
  CHECK(otherLinksOf(s.meta(), s.issues(), 1).size() == 1);
  CHECK(s.undo());
  CHECK(otherLinksOf(s.meta(), s.issues(), 1).size() == 2);
  // 新規課題も列の数に合う
  const std::string nid = s.addIssue("", "");
  REQUIRE_FALSE(nid.empty());
  CHECK(s.issue(nid)->extra.size() == s.meta().extraHeaders.size());
  CHECK(s.addOtherLink(nid, "Relates", true, "PRJ-1"));
}
