// 貼り付けたテキストの解析（parseOutline）と、子課題のまとめ追加（Session::addOutline）
#include <doctest/doctest.h>

#include <algorithm>

#include "../test_util.hpp"
#include "roadmap/commands.hpp"
#include "roadmap/outline.hpp"
#include "roadmap/project_io.hpp"
#include "roadmap/samples.hpp"

using namespace roadmap;

namespace {
using V = std::vector<std::pair<std::string, int>>;
V flat(const Outline& o) {
  V v;
  for (const auto& it : o.items) v.emplace_back(it.name, it.lv);
  return v;
}

ProjectData dataFrom(const std::string& csvText) {
  ProjectData d;
  auto imp = csv::importCsv(csvText);
  if (!imp) FAIL(imp.error());
  d.issues = imp->issues;
  d.meta = imp->meta;
  d.settings.id = "p1";
  d.settings.name = "テスト";
  d.settings.baseDate = "2026-10-19";
  d.settings.holidays = defaultHolidaysText();
  d.settings.ui = defaultUi();
  return d;
}
}  // namespace

TEST_CASE("outline: 平らな行・空行・前後の空白・全角スペース") {
  const auto o = parseOutline("設計  \n\n　　\n実装\r\n\t\r\nテスト\r");
  CHECK(flat(o) == V{{"設計", 0}, {"実装", 0}, {"テスト", 0}});
  CHECK(o.warns.empty());
  CHECK(o.items[1].line == 4);
  CHECK(parseOutline("").items.empty());
  CHECK(parseOutline("\n \n　\n").items.empty());
  // BOM と不正な UTF-8 でも落ちない
  CHECK(parseOutline("\xEF\xBB\xBF" "a").items[0].name == "a");
  CHECK(parseOutline("a\n\xFF" "b").items.size() == 2);
}

TEST_CASE("outline: タブのインデント") {
  const auto o = parseOutline("親\n\t子1\n\t\t孫\n\t子2\n次");
  CHECK(flat(o) == V{{"親", 0}, {"子1", 1}, {"孫", 2}, {"子2", 1}, {"次", 0}});
  CHECK(o.warns.empty());
  CHECK(o.maxLevel == 2);
}

TEST_CASE("outline: 空白 2 個・4 個のインデント") {
  CHECK(flat(parseOutline("a\n  b\n    c\n  d")) == V{{"a", 0}, {"b", 1}, {"c", 2}, {"d", 1}});
  CHECK(flat(parseOutline("a\n    b\n        c\n    d\ne")) == V{{"a", 0}, {"b", 1}, {"c", 2}, {"d", 1}, {"e", 0}});
}

TEST_CASE("outline: 全角スペースのインデントは空白 2 個分") {
  CHECK(flat(parseOutline("a\n　b\n　　c")) == V{{"a", 0}, {"b", 1}, {"c", 2}});
  CHECK(flat(parseOutline("a\n  b\n　　c")) == V{{"a", 0}, {"b", 1}, {"c", 2}});
}

TEST_CASE("outline: タブと空白の混在") {
  // 空白 4 個の文書では、タブも空白 4 個分
  CHECK(flat(parseOutline("a\n    b\n\t\tc\n\td")) == V{{"a", 0}, {"b", 1}, {"c", 2}, {"d", 1}});
  // 空白 2 個の文書では、タブは 1 段
  CHECK(flat(parseOutline("a\n  b\n\t\tc")) == V{{"a", 0}, {"b", 1}, {"c", 2}});
}

TEST_CASE("outline: 全体が同じだけ字下げされていれば、いちばん浅い行を先頭の階層にする") {
  const auto o = parseOutline("    a\n      b\n    c");
  CHECK(flat(o) == V{{"a", 0}, {"b", 1}, {"c", 0}});
  CHECK(o.warns.empty());
}

TEST_CASE("outline: 箇条書きの印を外す") {
  CHECK(flat(parseOutline("- a\n  - b\n    * c\n+ d\n・e\n• f\n●g")) ==
        V{{"a", 0}, {"b", 1}, {"c", 2}, {"d", 0}, {"e", 0}, {"f", 0}, {"g", 0}});
  // Markdown の番号付き・チェックボックス
  CHECK(flat(parseOutline("1. 設計\n   - [ ] 画面\n   - [x] DB\n2) 実装")) ==
        V{{"設計", 0}, {"画面", 1}, {"DB", 1}, {"実装", 0}});
  // 印ではないもの（空白がない・数字が長い・章番号）は件名のまま。印だけの行は捨てる
  CHECK(flat(parseOutline("-5度\n*注意\n2026. 目標\n1.1 設計\n[x] 印なし\n- \n-")) ==
        V{{"-5度", 0}, {"*注意", 0}, {"2026. 目標", 0}, {"1.1 設計", 0}, {"[x] 印なし", 0}});
}

TEST_CASE("outline: タブ区切り（Excel）は 1 列目を件名にする") {
  const auto o = parseOutline("設計\t山田\t2\n実装\t佐藤\t3\n\"引用,\"\"符\"\"\"\tx\n\t\t\n");
  CHECK(flat(o) == V{{"設計", 0}, {"実装", 0}, {"引用,\"符\"", 0}});
  // 引用符で囲まれていないものはそのまま
  CHECK(parseOutline("a\"b\tc").items[0].name == "a\"b");
  // 先頭のタブは字下げ、1 列目は件名
  CHECK(parseOutline("\tfoo\t\n").items.size() == 1);
  CHECK(parseOutline("\tfoo\t\n").items[0].name == "foo");
}

TEST_CASE("outline: 階層が飛ぶ入力は 1 段に丸めて警告する") {
  const auto o = parseOutline("a\n  b\n      c\n  d");
  CHECK(flat(o) == V{{"a", 0}, {"b", 1}, {"c", 2}, {"d", 1}});
  REQUIRE(o.warns.size() == 1);
  CHECK(o.warns[0].find("3行目") != std::string::npos);
  // 先頭の行が深い
  const auto f = parseOutline("\t\ta\nb\n\tc");
  CHECK(flat(f) == V{{"a", 0}, {"b", 0}, {"c", 1}});
  CHECK(f.warns.size() == 1);
  // 飛ばない入力に警告は出ない
  CHECK(parseOutline("a\n\tb\n\t\tc").warns.empty());
}

TEST_CASE("outline: 警告に貼り付けた内容を入れない") {
  const auto o = parseOutline("秘密A\n\tx\n\t\t\t秘密B");
  REQUIRE(!o.warns.empty());
  for (const auto& w : o.warns) CHECK(w.find("秘密") == std::string::npos);
}

TEST_CASE("outline: 上限") {
  std::string t;
  for (int i = 0; i < 520; ++i) t += "行" + std::to_string(i) + "\n";
  const auto o = parseOutline(t);
  CHECK(o.items.size() == kOutlineMax);
  CHECK(o.truncated);
  CHECK(o.ignored == 20);
  CHECK(!o.warns.empty());
  const auto p = parseOutline(t, 10);
  CHECK(p.items.size() == 10);
  // ちょうど上限は切り捨てない
  std::string u;
  for (int i = 0; i < 500; ++i) u += "x\n";
  const auto q = parseOutline(u);
  CHECK(q.items.size() == 500);
  CHECK(!q.truncated);
  CHECK(q.warns.empty());
}

TEST_CASE("outline: 深すぎる階層は丸めて警告する") {
  std::string t;
  for (int i = 0; i < 30; ++i) t += std::string(static_cast<std::size_t>(i), '\t') + "x\n";
  const auto o = parseOutline(t);
  CHECK(o.items.size() == 30);
  int mx = 0;
  for (const auto& it : o.items) mx = std::max(mx, it.lv);
  CHECK(mx == kOutlineMaxDepth);
  CHECK(o.maxLevel == mx);
  CHECK(!o.warns.empty());
}

TEST_CASE("outline: 警告が多いときは先頭だけ出して件数を添える") {
  std::string t;
  for (int i = 0; i < 30; ++i) t += "a\n\tb\n\t\t\tc\n";  // 毎回 c が 1 段飛ぶ
  const auto o = parseOutline(t);
  CHECK(o.items.size() == 90);
  CHECK(o.warns.size() == 11);
  CHECK(o.warns.back().find("20件") != std::string::npos);
}

TEST_CASE("addOutline: 親の下にまとめて作り、Undo 1回で戻る") {
  FixedClock clk(LocalTime{2026, 10, 19, 10, 30, 0});
  Session s(dataFrom("Issue Id,Issue Type,Summary,Parent\n1,Epic,親,\n2,Task,既存,1\n3,Epic,別,\n"), &clk);
  const auto o = parseOutline("a\n  a1\n  a2\nb");
  CHECK(s.addOutline("1", o.items) == 4);
  const auto n = s.takeNotices();
  REQUIRE(!n.empty());
  CHECK(n.back().text == "4件の子課題を追加しました");
  CHECK(n.back().action == NoticeAction::Undo);
  const auto& v = s.issues();
  REQUIRE(v.size() == 7);
  // 既存の子のあと・次の Epic の前に入る
  CHECK(v[1].id == "2");
  CHECK(v[2].summary == "a");
  CHECK(v[2].parent == "1");
  CHECK(v[2].type == "Task");  // 親が Epic
  CHECK(v[3].summary == "a1");
  CHECK(v[3].parent == v[2].id);
  CHECK(v[3].type == "Sub-task");
  CHECK(v[4].parent == v[2].id);
  CHECK(v[5].summary == "b");
  CHECK(v[5].parent == "1");
  CHECK(v[6].id == "3");
  CHECK(v[2].durW == 1.0);
  CHECK(v[2].status == "To Do");
  CHECK(s.undo());
  CHECK(s.issues().size() == 3);
  CHECK(!s.canUndo());
}

TEST_CASE("addOutline: 親に番号タグがあれば続き番号を振る（貼り付けたタグは付け替える）") {
  FixedClock clk(LocalTime{2026, 10, 19, 10, 30, 0});
  Session s(dataFrom("Issue Id,Issue Type,Summary,Parent\n1,Epic,[E] 親,\n2,Task,[E-1] 既存,1\n"), &clk);
  const auto o = parseOutline("[T01] 上\n  下\n[X] 次\n[Y]");
  CHECK(s.addOutline("1", o.items) == 4);
  const auto& v = s.issues();
  CHECK(v[2].summary == "[E-2] 上");
  CHECK(v[3].summary == "[E-2-1] 下");
  CHECK(v[4].summary == "[E-3] 次");
  CHECK(v[5].summary == "[E-4] [Y]");  // 件名が空になるものは元の文字を残す
}

TEST_CASE("addOutline: タグのない親では件名をそのまま使う") {
  FixedClock clk(LocalTime{2026, 10, 19, 10, 30, 0});
  Session s(dataFrom("Issue Id,Issue Type,Summary,Parent\n1,Task,親,\n"), &clk);
  CHECK(s.addOutline("1", parseOutline("[T01] 上\n下").items) == 2);
  CHECK(s.issues()[1].summary == "[T01] 上");
  CHECK(s.issues()[1].type == "Sub-task");
  CHECK(s.issues()[2].summary == "下");
}

TEST_CASE("addOutline: 親がない・項目がないときは何もしない／折りたたみを開く") {
  FixedClock clk(LocalTime{2026, 10, 19, 10, 30, 0});
  Session s(dataFrom("Issue Id,Issue Type,Summary,Parent\n1,Epic,親,\n"), &clk);
  CHECK(s.addOutline("99", parseOutline("a").items) == 0);
  CHECK(s.addOutline("1", {}) == 0);
  CHECK(!s.canUndo());
  s.setCollapsed("1", true);
  CHECK(s.addOutline("1", parseOutline("a").items) == 1);
  CHECK(!s.isCollapsed("1"));
}
