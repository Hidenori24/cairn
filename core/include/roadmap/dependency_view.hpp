// 依存の見せ方（たどる・日付の衝突・クリティカル）の判定。UI から独立した純粋関数
#pragma once

#include <vector>

#include "roadmap/engine.hpp"

namespace roadmap {

// self の先行・後続をすべてたどった課題の集合（Tree の添字ごとに 0/1）。
//  - self の配下の葉、そこから先行の向き・後続の向きにたどれる葉（兄弟の枝は含めない）
//  - それらの葉の祖先（親の行を薄くしないため）
// self が範囲外なら全部 0
std::vector<char> traceDependencies(const Tree& T, int self);

// 依存 pred → succ の日付が合わない（後続が先行より前に始まる）か。
// 後続が完了していれば数えない。期間 0 のマイルストーン（succZeroWidth）は先行の完了日と同じ日に置けるので、それより前だけ
bool isDateConflict(const ScheduleRow& pred, const ScheduleRow& succ, bool succZeroWidth);

// クリティカルな連鎖の線か（先行・後続とも未完了で、余裕が 0 以下）
bool isCriticalLink(const ScheduleRow& pred, const ScheduleRow& succ);

}  // namespace roadmap
