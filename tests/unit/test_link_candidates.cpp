// 依存の相手の候補（Issue #6）: 部分一致・並び・除外・循環の判定
#include <doctest/doctest.h>

#include <algorithm>

#include "roadmap/commands.hpp"
#include "roadmap/link_candidates.hpp"
#include "roadmap/project_io.hpp"

using namespace roadmap;

namespace {
FixedClock clk(LocalTime{2026, 10, 19, 9, 0, 0});
// 1 設計(Epic) ─ 2 画面 / 3 DB / 4 API ／ 5 試験(Epic) ─ 6 結合試験 / 7 画面の試験
Session make() {
  ProjectData d;
  auto imp = csv::importCsv(
      "Issue Id,Issue Type,Summary,Parent,Assignee,Status,Priority,Labels,Deadline,Duration (weeks)\n"
      "1,Epic,設計,,,,,,,\n"
      "2,Task,画面,1,田中,To Do,High,UI,,1\n"
      "3,Task,DB,1,,To Do,,,,2\n"
      "4,Task,API,1,,To Do,,,,1\n"
      "5,Epic,試験,,,,,,,\n"
      "6,Task,結合試験,5,,To Do,,,,1\n"
      "7,Task,画面の試験,5,,To Do,,,,1\n");
  if (!imp) FAIL(imp.error());
  d.issues = imp->issues;
  d.meta = imp->meta;
  d.settings.baseDate = "2026-10-19";
  d.settings.ui = defaultUi();
  return Session(d, &clk);
}

std::vector<std::string> idsOf(const Session& s, const std::vector<LinkCandidate>& v) {
  std::vector<std::string> r;
  for (const auto& c : v) r.push_back(s.viewIssues()[c.idx].id);
  return r;
}
const LinkCandidate* find(const Session& s, const std::vector<LinkCandidate>& v, const std::string& id) {
  for (const auto& c : v)
    if (s.viewIssues()[c.idx].id == id) return &c;
  return nullptr;
}
}  // namespace

TEST_CASE("link_candidates: 自分と追加済みの相手は出さない") {
  Session s = make();
  REQUIRE(s.addLink("2", "3"));
  const Tree& T = *s.tree();
  const auto pred = linkCandidates(T, s.viewIssues(), T.find("3"), LinkDir::AddPred, "", {});
  CHECK(find(s, pred, "3") == nullptr);  // 自分
  CHECK(find(s, pred, "2") == nullptr);  // すでに先行
  CHECK(find(s, pred, "4") != nullptr);
  const auto succ = linkCandidates(T, s.viewIssues(), T.find("2"), LinkDir::AddSucc, "", {});
  CHECK(find(s, succ, "3") == nullptr);  // すでに後続
  CHECK(find(s, succ, "2") == nullptr);
}

TEST_CASE("link_candidates: 循環になる相手は理由つきで無効、後ろに並ぶ") {
  Session s = make();
  REQUIRE(s.addLink("2", "3"));
  REQUIRE(s.addLink("3", "6"));
  const Tree& T = *s.tree();
  // 3 の先行に 6 を足すと 3→6→3 の循環
  const auto pred = linkCandidates(T, s.viewIssues(), T.find("3"), LinkDir::AddPred, "", {});
  REQUIRE(find(s, pred, "6"));
  CHECK(find(s, pred, "6")->block == LinkBlock::Cycle);
  // 親（Epic 5）は配下の 6 を通って循環する
  CHECK(find(s, pred, "5")->block == LinkBlock::Cycle);
  CHECK(find(s, pred, "7")->block == LinkBlock::None);
  // 使えるものが先、使えないものは後ろ
  bool seenBlocked = false;
  for (const auto& c : pred) {
    if (c.block != LinkBlock::None) seenBlocked = true;
    else CHECK_FALSE(seenBlocked);
  }
  // 3 の後続に 2 を足すと 2→3→2
  const auto succ = linkCandidates(T, s.viewIssues(), T.find("3"), LinkDir::AddSucc, "", {});
  CHECK(find(s, succ, "2")->block == LinkBlock::Cycle);
  CHECK(find(s, succ, "4")->block == LinkBlock::None);
  CHECK(std::string(linkBlockReason(LinkBlock::Cycle)).find("循環") != std::string::npos);
}

TEST_CASE("link_candidates: 親子の間は無効（理由つき）") {
  Session s = make();
  const Tree& T = *s.tree();
  const auto v = linkCandidates(T, s.viewIssues(), T.find("2"), LinkDir::AddPred, "", {});
  CHECK(find(s, v, "1")->block == LinkBlock::Family);
  CHECK(find(s, v, "5")->block == LinkBlock::None);
  const auto w = linkCandidates(T, s.viewIssues(), T.find("1"), LinkDir::AddSucc, "", {});
  CHECK(find(s, w, "3")->block == LinkBlock::Family);
}

TEST_CASE("link_candidates: 部分一致（表示ID・件名・大文字小文字・空白）") {
  Session s = make();
  const Tree& T = *s.tree();
  const int self = T.find("4");
  auto q = [&](const char* text) {
    auto ids = idsOf(s, linkCandidates(T, s.viewIssues(), self, LinkDir::AddPred, text, {}));
    std::sort(ids.begin(), ids.end());
    return ids;
  };
  CHECK(q("画面") == std::vector<std::string>{"2", "7"});
  CHECK(q("db") == std::vector<std::string>{"3"});
  CHECK(q("DB") == std::vector<std::string>{"3"});
  CHECK(q("  画面  ") == std::vector<std::string>{"2", "7"});  // 前後の空白は無視
  CHECK(q("画面 試験") == std::vector<std::string>{"7"});       // 空白区切りは AND
  CHECK(q("存在しない").empty());
  CHECK(q("6") == std::vector<std::string>{"6"});  // 表示ID（番号）
}

TEST_CASE("link_candidates: 同じ親の兄弟と最近つないだ課題を先頭に") {
  Session s = make();
  const Tree& T = *s.tree();
  const int self = T.find("6");  // 親 5。兄弟は 7
  auto v = linkCandidates(T, s.viewIssues(), self, LinkDir::AddPred, "", {});
  REQUIRE(!v.empty());
  CHECK(s.viewIssues()[v[0].idx].id == "7");
  CHECK(v[0].sibling);
  // 最近つないだもの（4）が兄弟より前
  v = linkCandidates(T, s.viewIssues(), self, LinkDir::AddPred, "", {"4"});
  CHECK(s.viewIssues()[v[0].idx].id == "4");
  CHECK(v[0].recent);
  CHECK(s.viewIssues()[v[1].idx].id == "7");
  // 新しい順（先頭が最近）
  v = linkCandidates(T, s.viewIssues(), self, LinkDir::AddPred, "", {"3", "4"});
  CHECK(s.viewIssues()[v[0].idx].id == "3");
  CHECK(s.viewIssues()[v[1].idx].id == "4");
  // 表示ID の完全一致は最優先
  v = linkCandidates(T, s.viewIssues(), self, LinkDir::AddPred, "2", {"4"});
  CHECK(s.viewIssues()[v[0].idx].id == "2");
}

TEST_CASE("link_candidates: 件数の上限") {
  Session s = make();
  const Tree& T = *s.tree();
  CHECK(linkCandidates(T, s.viewIssues(), T.find("2"), LinkDir::AddPred, "", {}, 3).size() == 3);
}

TEST_CASE("checkLink: 1組の判定と addLink の結果が合う") {
  Session s = make();
  REQUIRE(s.addLink("2", "3"));
  const Tree& T = *s.tree();
  const auto& I = s.viewIssues();
  CHECK(checkLink(T, I, T.find("3"), T.find("3")) == LinkBlock::Self);
  CHECK(checkLink(T, I, T.find("2"), T.find("3")) == LinkBlock::Already);
  CHECK(checkLink(T, I, T.find("3"), T.find("2")) == LinkBlock::Cycle);
  CHECK(checkLink(T, I, T.find("1"), T.find("2")) == LinkBlock::Family);
  CHECK(checkLink(T, I, T.find("3"), T.find("4")) == LinkBlock::None);
  CHECK(s.addLink("3", "4"));
  CHECK_FALSE(s.addLink("4", "2"));  // 4→2→3→4 の循環。計算で拒否されて元に戻る
}

// ---- つなぐ前の日程の影響（Issue #8: ガントのドラッグのプレビュー）----

TEST_CASE("previewLink: 後続が先行の終わりまで遅れる（営業日）。セッションは変わらない") {
  Session s = make();
  const std::size_t undo0 = s.undoSize();
  const auto before = s.issues();
  const LinkPreview p = s.previewLink("2", "3");  // 画面(1週) → DB
  CHECK(p.ok);
  CHECK(p.block == LinkBlock::None);
  CHECK(p.delayDays == 5);
  REQUIRE(p.shifts.size() == 1);
  const Tree& T = *s.tree();
  const auto& R = *s.rows();
  CHECK(p.shifts[0].idx == T.find("3"));
  CHECK(p.shifts[0].oldStart == R[T.find("3")].asapStart);
  CHECK(p.shifts[0].newStart == R[T.find("3")].asapStart + 7);  // 1週間後の同じ曜日
  CHECK(p.shifts[0].newFinish - p.shifts[0].newStart == p.shifts[0].oldFinish - p.shifts[0].oldStart);
  CHECK(s.undoSize() == undo0);
  CHECK(s.issues() == before);
}

TEST_CASE("previewLink: すでに十分遅い後続は動かない") {
  Session s = make();
  REQUIRE(s.addLink("2", "3"));  // DB は画面の1週間後から
  const LinkPreview p = s.previewLink("4", "3");  // API(1週) → DB。DB はすでに API の後
  CHECK(p.ok);
  CHECK(p.delayDays == 0);
  CHECK(p.shifts.empty());
}

TEST_CASE("previewLink: 親への依存は配下の葉に広がる／つなげない組は理由つきで ok=false") {
  Session s = make();
  const LinkPreview p = s.previewLink("3", "5");  // DB(2週) → 試験(Epic)。6・7 が遅れる
  CHECK(p.ok);
  CHECK(p.delayDays == 10);
  CHECK(p.shifts.size() == 2);
  REQUIRE(s.addLink("2", "3"));
  const LinkPreview c = s.previewLink("3", "2");
  CHECK_FALSE(c.ok);
  CHECK(c.block == LinkBlock::Cycle);
  CHECK(c.shifts.empty());
  CHECK(s.previewLink("2", "3").block == LinkBlock::Already);
  CHECK(s.previewLink("2", "2").block == LinkBlock::Self);
  CHECK(s.previewLink("1", "2").block == LinkBlock::Family);
  CHECK_FALSE(s.previewLink("2", "nothing").ok);
}

TEST_CASE("previewLink: 下流にも広がる（後続の後続も遅れる）") {
  Session s = make();
  REQUIRE(s.addLink("3", "4"));  // DB(2週) → API
  const LinkPreview p = s.previewLink("2", "3");  // 画面(1週) → DB: DB が1週遅れ、API も1週遅れる
  CHECK(p.ok);
  CHECK(p.delayDays == 5);
  CHECK(p.shifts.size() == 2);
}
