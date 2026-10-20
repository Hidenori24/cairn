// 「先行」列の入力（Issue #9）: 書き方の解析・検証・差分の計画。UI から独立した純粋関数
//  例: `DEV-12, DEV-15` → 先行を DEV-12 と DEV-15 の 2 件にする（今の先行との差分だけを足す・外す）
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "roadmap/engine.hpp"
#include "roadmap/model.hpp"

namespace roadmap {

// 入力の 1 項目。いまは課題の参照だけ。
// 将来「種類・ラグ」（例 `DEV-12FS+2d`）を足すときは、parsePredText がここへ kind / lagDays を入れる
struct PredToken {
  std::string raw;  // 入力のまま（エラーの表示用）
  std::string ref;  // 課題の参照（表示ID・キー・id）
};

// 区切り（, ， 、 ; ； 改行）で分けて前後の空白を除く。空の項目は捨てる。
// セル全体が "…" で囲まれていれば（Excel からの貼り付け）外す。表示ID の中の空白は区切りにしない
std::vector<PredToken> parsePredText(std::string_view text);

// 課題ごとの先行（その課題を blocks に持つ課題の添字）。T の dfs の順。存在しない id・自分自身は数えない
std::vector<std::vector<int>> declaredPreds(const Tree& T, const std::vector<Issue>& issues);

// 表に出す文字（表示ID をカンマ区切り）
std::string formatPreds(const Tree& T, const std::vector<int>& preds);
// 入力欄の初期値。表示ID が他の課題と重なる先行は、重ならない参照（キー → id）で書く
std::string formatPredsForEdit(const Tree& T, const std::vector<Issue>& issues, const std::vector<int>& preds);

// 入力に見つかった問題
struct PredProblem {
  std::string token;   // 問題のある項目（入力のまま）
  std::string reason;  // 画面に出す理由（日本語）
};

// 入力から求めた変更の計画。問題が 1 つでもあれば ok = false で、何も足さない・外さない
struct PredPlan {
  bool ok = true;
  std::vector<PredProblem> problems;
  std::vector<std::string> add;     // 新しく先行にする課題の id
  std::vector<std::string> remove;  // 先行から外す課題の id
  bool changed() const { return !add.empty() || !remove.empty(); }
};

// self（添字）の先行を text のとおりにするための計画。
// 存在しない ID・あいまいな ID・重複・自分自身・親子・循環は problems に理由を入れる。T と issues は同じ時点のもの
PredPlan planPredEdit(const Tree& T, const std::vector<Issue>& issues, int self, std::string_view text);

// 入力中の項目（最後の区切り以降）に合う課題の候補（表示ID・キー・id・件名の部分一致。大文字小文字を区別しない）。
// すでに入力した項目・自分・つなげない相手（循環・親子）は出さない。tree の順で limit 件まで。添字を返す
std::vector<int> predSuggestions(const Tree& T, const std::vector<Issue>& issues, int self, std::string_view text,
                                 std::size_t limit = 5);

// 入力中の項目を先頭の候補（predSuggestions の先頭）の参照に置き換えた入力。候補がなければ text のまま。
// 置き換えた項目は、重ならない参照（formatPredsForEdit と同じ書き方）で書く
std::string completePredText(const Tree& T, const std::vector<Issue>& issues, int self, std::string_view text);

}  // namespace roadmap
