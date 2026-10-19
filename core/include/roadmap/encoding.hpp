// 文字コード: CSV の読み込みは UTF-8（BOM可）→ 不正なら CP932（Shift_JIS）→ それも不正なら UTF-8 の置換（SPEC 6.1）
#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace roadmap::encoding {

enum class Detected { Utf8, Utf8Bom, Cp932, Utf8Replaced };

struct Decoded {
  std::string text;  // UTF-8（BOMは取り除く）
  Detected detected = Detected::Utf8;
};

// CP932 として厳密に読む。読めないバイト列があれば nullopt
std::optional<std::string> decodeCp932(std::string_view bytes);
Decoded decodeText(std::string_view bytes);

}  // namespace roadmap::encoding
