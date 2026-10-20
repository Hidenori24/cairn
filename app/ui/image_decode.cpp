#include "ui/image_decode.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

// 使う形式だけを有効にする（コードを小さく、攻撃面を狭くする）。ファイルは自前で読むので stdio は使わない
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_GIF
#define STBI_ONLY_BMP
#include "stb_image.h"

namespace roadmap::ui {

namespace {
constexpr long long kMaxPixels = 100'000'000;

// 面積平均で縮小する（元の画素のうち、1つの出力画素にかかる範囲を平均する）
std::vector<unsigned char> shrink(const unsigned char* src, int sw, int sh, int dw, int dh) {
  std::vector<unsigned char> out(static_cast<std::size_t>(dw) * static_cast<std::size_t>(dh) * 4);
  const double kx = static_cast<double>(sw) / dw, ky = static_cast<double>(sh) / dh;
  for (int y = 0; y < dh; ++y) {
    const int y0 = static_cast<int>(std::floor(y * ky));
    const int y1 = std::min(sh, std::max(y0 + 1, static_cast<int>(std::ceil((y + 1) * ky))));
    for (int x = 0; x < dw; ++x) {
      const int x0 = static_cast<int>(std::floor(x * kx));
      const int x1 = std::min(sw, std::max(x0 + 1, static_cast<int>(std::ceil((x + 1) * kx))));
      unsigned long long acc[4] = {0, 0, 0, 0};
      for (int yy = y0; yy < y1; ++yy) {
        const unsigned char* p = src + (static_cast<std::size_t>(yy) * static_cast<std::size_t>(sw) + static_cast<std::size_t>(x0)) * 4;
        for (int xx = x0; xx < x1; ++xx, p += 4)
          for (int c = 0; c < 4; ++c) acc[c] += p[c];
      }
      const unsigned long long n = static_cast<unsigned long long>(y1 - y0) * static_cast<unsigned long long>(x1 - x0);
      unsigned char* d = &out[(static_cast<std::size_t>(y) * static_cast<std::size_t>(dw) + static_cast<std::size_t>(x)) * 4];
      for (int c = 0; c < 4; ++c) d[c] = static_cast<unsigned char>((acc[c] + n / 2) / n);
    }
  }
  return out;
}
}  // namespace

std::optional<Pixels> decodeImage(const std::string& bytes, int maxDim) {
  if (bytes.empty() || bytes.size() > static_cast<std::size_t>(INT32_MAX)) return std::nullopt;
  const auto* data = reinterpret_cast<const unsigned char*>(bytes.data());
  const int len = static_cast<int>(bytes.size());
  int w = 0, h = 0, comp = 0;
  if (!stbi_info_from_memory(data, len, &w, &h, &comp)) return std::nullopt;
  if (w <= 0 || h <= 0 || static_cast<long long>(w) * h > kMaxPixels) return std::nullopt;
  unsigned char* px = stbi_load_from_memory(data, len, &w, &h, &comp, 4);
  if (!px) return std::nullopt;
  Pixels out;
  out.origW = w;
  out.origH = h;
  const int longest = std::max(w, h);
  if (maxDim > 0 && longest > maxDim) {
    const double k = static_cast<double>(maxDim) / longest;
    out.w = std::max(1, static_cast<int>(std::lround(w * k)));
    out.h = std::max(1, static_cast<int>(std::lround(h * k)));
    out.rgba = shrink(px, w, h, out.w, out.h);
  } else {
    out.w = w;
    out.h = h;
    out.rgba.assign(px, px + static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4);
  }
  stbi_image_free(px);
  return out;
}

}  // namespace roadmap::ui
