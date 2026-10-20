#include "roadmap/encoding.hpp"

#include <cstdint>

#include "roadmap/text.hpp"

namespace roadmap::encoding {
namespace {
const std::uint16_t kCp932[] = {
#include "cp932_table.inc"
};

int leadIndex(unsigned b) {
  if (b >= 0x81 && b <= 0x9F) return static_cast<int>(b - 0x81);
  if (b >= 0xE0 && b <= 0xFC) return static_cast<int>(b - 0xE0 + 31);
  return -1;
}
int trailIndex(unsigned b) {
  if (b >= 0x40 && b <= 0x7E) return static_cast<int>(b - 0x40);
  if (b >= 0x80 && b <= 0xFC) return static_cast<int>(b - 0x80 + 63);
  return -1;
}
}  // namespace

std::optional<std::string> decodeCp932(std::string_view bytes) {
  std::string out;
  out.reserve(bytes.size() * 3 / 2);
  for (std::size_t i = 0; i < bytes.size(); ++i) {
    const auto b = static_cast<unsigned char>(bytes[i]);
    if (b <= 0x80) {
      text::appendUtf8(out, b);
    } else if (b >= 0xA1 && b <= 0xDF) {
      text::appendUtf8(out, 0xFF61 + (b - 0xA1));  // 半角カナ
    } else {
      const int li = leadIndex(b);
      if (li < 0 || i + 1 >= bytes.size()) return std::nullopt;
      const int ti = trailIndex(static_cast<unsigned char>(bytes[i + 1]));
      if (ti < 0) return std::nullopt;
      const std::uint16_t u = kCp932[li * 188 + ti];
      if (!u) return std::nullopt;
      text::appendUtf8(out, u);
      ++i;
    }
  }
  return out;
}

Decoded decodeText(std::string_view bytes) {
  bool bom = false;
  if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xEF &&
      static_cast<unsigned char>(bytes[1]) == 0xBB && static_cast<unsigned char>(bytes[2]) == 0xBF) {
    bytes.remove_prefix(3);
    bom = true;
  }
  if (text::isValidUtf8(bytes)) return {std::string(bytes), bom ? Detected::Utf8Bom : Detected::Utf8};
  if (!bom) {
    if (auto s = decodeCp932(bytes)) return {std::move(*s), Detected::Cp932};
  }
  return {text::sanitizeUtf8(bytes), Detected::Utf8Replaced};
}

}  // namespace roadmap::encoding
