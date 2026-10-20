#include <doctest/doctest.h>

#include "roadmap/issue_name.hpp"

using namespace roadmap;

namespace {
NewNameInput under(NewNameMode m, std::vector<std::string> sib = {}, std::string custom = "") {
  NewNameInput in;
  in.mode = m;
  in.custom = std::move(custom);
  in.hasParent = true;
  in.parentName = "要件定義";
  in.parentDid = "T01";
  in.siblings = std::move(sib);
  in.today = "2026-10-19";
  return in;
}
NewNameInput top(NewNameMode m, std::vector<std::string> sib = {}, std::string custom = "") {
  NewNameInput in = under(m, std::move(sib), std::move(custom));
  in.hasParent = false;
  in.parentName.clear();
  in.parentDid.clear();
  return in;
}
}  // namespace

TEST_CASE("issue_name: 形式のキー") {
  CHECK(std::string(newNameModeKey(NewNameMode::Keep)) == "keep");
  for (auto m : {NewNameMode::Keep, NewNameMode::ParentSeq, NewNameMode::ParentDefault, NewNameMode::Custom})
    CHECK(newNameModeFromKey(newNameModeKey(m)) == m);
  CHECK(newNameModeFromKey("") == NewNameMode::Keep);
  CHECK(newNameModeFromKey("知らない") == NewNameMode::Keep);
}

TEST_CASE("issue_name: 今のまま（既定名は呼び出し側）") {
  CHECK(makeNewIssueName(under(NewNameMode::Keep, {"a"})).empty());
  CHECK(makeNewIssueName(top(NewNameMode::Keep)).empty());
}

TEST_CASE("issue_name: 親の名前＋連番。兄弟の名前と重ならない") {
  CHECK(makeNewIssueName(under(NewNameMode::ParentSeq)) == "要件定義の子 1");
  CHECK(makeNewIssueName(under(NewNameMode::ParentSeq, {"A", "B"})) == "要件定義の子 3");
  // 兄弟が2件でも「子 3」が既にあれば次の番号
  CHECK(makeNewIssueName(under(NewNameMode::ParentSeq, {"要件定義の子 3", "B"})) == "要件定義の子 4");
  CHECK(makeNewIssueName(under(NewNameMode::ParentSeq, {"要件定義の子 1", "要件定義の子 2"})) == "要件定義の子 3");
  // 兄弟の名前の前後の空白は無視
  CHECK(makeNewIssueName(under(NewNameMode::ParentSeq, {" 要件定義の子 2 "})) == "要件定義の子 3");
  // 最上位は親がないので「新しい課題 n」
  CHECK(makeNewIssueName(top(NewNameMode::ParentSeq, {"x", "y", "z"})) == "新しい課題 4");
  CHECK(makeNewIssueName(top(NewNameMode::ParentSeq)) == "新しい課題 1");
}

TEST_CASE("issue_name: 親の名前／既定名") {
  CHECK(makeNewIssueName(under(NewNameMode::ParentDefault, {"a", "b"})) == "要件定義／新しい課題");
  CHECK(makeNewIssueName(top(NewNameMode::ParentDefault)).empty());
}

TEST_CASE("issue_name: 自由入力 {親} {親ID} {n} {日付}") {
  CHECK(makeNewIssueName(under(NewNameMode::Custom, {"a"}, "{親}-{親ID}-{n}（{日付}）")) == "要件定義-T01-2（2026-10-19）");
  // {n} がなければ連番もチェックもしない
  CHECK(makeNewIssueName(under(NewNameMode::Custom, {"要件定義メモ"}, "{親}メモ")) == "要件定義メモ");
  // {n} は重ならない
  CHECK(makeNewIssueName(under(NewNameMode::Custom, {"課題#2", "x"}, "課題#{n}")) == "課題#3");
  // 知らない {…} はそのまま
  CHECK(makeNewIssueName(under(NewNameMode::Custom, {}, "{他}{n}")) == "{他}1");
  // 最上位: {親}・{親ID} を含む形は「新しい課題 n」にする。含まなければそのまま使う
  CHECK(makeNewIssueName(top(NewNameMode::Custom, {"a", "b"}, "{親}の子 {n}")) == "新しい課題 3");
  CHECK(makeNewIssueName(top(NewNameMode::Custom, {"a"}, "メモ {日付} #{n}")) == "メモ 2026-10-19 #2");
  // 空・空白だけは既定名（空を返す）、改行は空白に直す
  CHECK(makeNewIssueName(under(NewNameMode::Custom, {}, "")).empty());
  CHECK(makeNewIssueName(under(NewNameMode::Custom, {}, "   ")).empty());
  CHECK(makeNewIssueName(under(NewNameMode::Custom, {}, "a\nb")) == "a b");
}
