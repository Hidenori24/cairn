// 貼り付けたテキスト（インデントで階層を表した複数行）の解析。UI・クリップボードには依存しない純粋関数。
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace roadmap {

constexpr std::size_t kOutlineMax = 500;  // 1回に作る課題の上限
constexpr int kOutlineMaxDepth = 10;      // 貼り付けた中での階層の深さの上限（0 = 直下の子）

struct OutlineItem {
  std::string name;  // 件名（印・前後の空白を除いたもの）
  int lv = 0;        // 深さ（0 = 親の直下の子）。直前の項目より 1 段までしか深くならない
  int line = 0;      // 元の行番号（1 から）
};

struct Outline {
  std::vector<OutlineItem> items;
  std::vector<std::string> warns;  // 丸めた・切り捨てたことの説明。貼り付けた内容は入れない
  int maxLevel = 0;                // items の lv の最大
  bool truncated = false;          // 上限を超えた
  int ignored = 0;                 // 上限を超えて無視した行の数
};

// 行ごとに、字下げ（タブ・空白・全角スペース）・箇条書きの印（- * + ・ •、1. 、[ ] ）を読んで階層にする。
// タブ区切り（Excel のコピー）は 1 列目を件名にする。空行は捨てる。
Outline parseOutline(std::string_view text, std::size_t maxItems = kOutlineMax);

}  // namespace roadmap
