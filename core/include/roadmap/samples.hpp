// 実行ファイルに埋め込むサンプル（golden/samples/ の CSV。SPEC 8.7）
#pragma once

#include <string_view>

namespace roadmap::samples {

std::string_view regulationCsv();  // サンプル（法規対応）
std::string_view hierarchyCsv();   // 階層デモ（5階層の例）
std::string_view levelingCsv();    // 資源制約デモ

inline constexpr const char* kSampleBase = "2026-10-19";

}  // namespace roadmap::samples
