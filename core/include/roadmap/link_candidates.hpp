// 依存の相手の候補（部分一致・並び・除外・循環の判定）。UI から独立した純粋関数
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "roadmap/engine.hpp"
#include "roadmap/model.hpp"

namespace roadmap {

// 追加するのは、自分の先行（相手 → 自分）か後続（自分 → 相手）か
enum class LinkDir { AddPred, AddSucc };

// 相手にできない理由
enum class LinkBlock {
  None,     // つなげる
  Self,     // 自分自身
  Already,  // すでにつながっている
  Cycle,    // 先行・後続がループする
  Family    // 親子（同じ配下）の間。依存関係は無視される
};
const char* linkBlockReason(LinkBlock b);  // 画面に出す理由（日本語）

struct LinkCandidate {
  int idx = -1;  // Tree / issues の添字
  LinkBlock block = LinkBlock::None;
  bool recent = false;   // 最近つないだ課題
  bool sibling = false;  // 自分と同じ親
};

// pred → succ をつなげられるか（Tree と同じ時点の issues を渡す）
LinkBlock checkLink(const Tree& T, const std::vector<Issue>& issues, int pred, int succ);

// 候補の一覧。
//  - query: 表示ID・キー・id・件名の部分一致（大文字小文字を区別しない。空白区切りは AND。空なら全部）
//  - recentIds: 最近つないだ課題の id（先頭が新しい）
//  - 自分とすでにつながっている相手は出さない。循環・親子の相手は block つきで、つなげられるものの後ろに並べる
//  - 並び: 表示ID・キーの完全一致 → 最近つないだ → 同じ親の兄弟 → ツリーの順
//  - limit: 返す件数の上限
std::vector<LinkCandidate> linkCandidates(const Tree& T, const std::vector<Issue>& issues, int self, LinkDir dir,
                                          std::string_view query, const std::vector<std::string>& recentIds,
                                          std::size_t limit = 50);

}  // namespace roadmap
