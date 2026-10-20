// 実行ファイルに埋め込むサンプルと雛形（golden/samples/ と core/samples/ の CSV。SPEC 8.7）
#pragma once

#include <string_view>

namespace roadmap::samples {

std::string_view hierarchyCsv();   // 階層デモ（5階層の例）
std::string_view levelingCsv();    // 資源制約デモ
std::string_view embeddedSwCsv();  // 組込みソフトウェア開発の雛形（要求〜量産移管の作業分解。日付は未設定）

inline constexpr const char* kSampleBase = "2026-10-19";

}  // namespace roadmap::samples
