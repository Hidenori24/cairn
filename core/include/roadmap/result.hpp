// core の公開APIの失敗を返す結果型（C++23 の std::expected の代わり）。UI へ例外を漏らさない。
#pragma once

#include <string>
#include <utility>
#include <variant>

namespace roadmap {

struct Error {
  std::string message;
};

template <class T>
class [[nodiscard]] Result {
 public:
  Result(T value) : v_(std::move(value)) {}  // NOLINT(google-explicit-constructor)
  Result(Error e) : v_(std::move(e)) {}      // NOLINT(google-explicit-constructor)

  bool ok() const { return v_.index() == 0; }
  explicit operator bool() const { return ok(); }
  T& value() & { return std::get<0>(v_); }
  const T& value() const& { return std::get<0>(v_); }
  T&& value() && { return std::get<0>(std::move(v_)); }
  T* operator->() { return &value(); }
  const T* operator->() const { return &value(); }
  T& operator*() & { return value(); }
  const T& operator*() const& { return value(); }
  const std::string& error() const { return std::get<1>(v_).message; }

 private:
  std::variant<T, Error> v_;
};

struct Unit {};
using Status = Result<Unit>;
inline Status okStatus() { return Unit{}; }

}  // namespace roadmap
