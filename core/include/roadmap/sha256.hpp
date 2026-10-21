// SHA-256（RFC 6234 / FIPS 180-4）。依存を足さずに自前で実装する。添付の本体の指紋（SPEC 8.9）に使う
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace roadmap {

// 少しずつ食わせられる SHA-256（update を何度に分けて呼んでも、まとめて 1 回で食わせたときと同じ値になる）
class Sha256 {
 public:
  Sha256();
  void update(const void* data, std::size_t n);
  void update(std::string_view s) { update(s.data(), s.size()); }
  // 値を確定する。呼んだあとは reset するまで使えない
  std::array<std::uint8_t, 32> digest();
  // 16進小文字 64 文字
  std::string hexDigest();
  void reset();

 private:
  void block(const std::uint8_t* p);
  std::array<std::uint32_t, 8> h_{};
  std::array<std::uint8_t, 64> buf_{};
  std::size_t bufLen_ = 0;
  std::uint64_t total_ = 0;  // 食わせたバイト数
};

// まとめて計算して、16進小文字 64 文字で返す
std::string sha256Hex(std::string_view data);

// 16進小文字ちょうど 64 文字か（settings.json から読んだ値の検査）
bool isSha256Hex(std::string_view s);

}  // namespace roadmap
