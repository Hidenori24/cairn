#include "roadmap/text.hpp"

#include <charconv>
#include <cmath>
#include <cstdlib>

namespace roadmap::text {

char32_t decodeAt(std::string_view s, std::size_t& i) {
  const auto b0 = static_cast<unsigned char>(s[i]);
  if (b0 < 0x80) {
    ++i;
    return b0;
  }
  int len = 0;
  char32_t cp = 0;
  if ((b0 & 0xE0) == 0xC0) {
    len = 2;
    cp = b0 & 0x1F;
  } else if ((b0 & 0xF0) == 0xE0) {
    len = 3;
    cp = b0 & 0x0F;
  } else if ((b0 & 0xF8) == 0xF0) {
    len = 4;
    cp = b0 & 0x07;
  } else {
    ++i;
    return 0xFFFD;
  }
  if (i + len > s.size()) {
    ++i;
    return 0xFFFD;
  }
  for (int k = 1; k < len; ++k) {
    const auto b = static_cast<unsigned char>(s[i + k]);
    if ((b & 0xC0) != 0x80) {
      ++i;
      return 0xFFFD;
    }
    cp = (cp << 6) | (b & 0x3F);
  }
  // 冗長な表現・サロゲート・範囲外は不正
  if ((len == 2 && cp < 0x80) || (len == 3 && cp < 0x800) || (len == 4 && cp < 0x10000) ||
      cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
    ++i;
    return 0xFFFD;
  }
  i += len;
  return cp;
}

void appendUtf8(std::string& out, char32_t cp) {
  if (cp < 0x80) {
    out += static_cast<char>(cp);
  } else if (cp < 0x800) {
    out += static_cast<char>(0xC0 | (cp >> 6));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    out += static_cast<char>(0xE0 | (cp >> 12));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    out += static_cast<char>(0xF0 | (cp >> 18));
    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  }
}

std::u32string toU32(std::string_view s) {
  std::u32string out;
  out.reserve(s.size());
  for (std::size_t i = 0; i < s.size();) out += decodeAt(s, i);
  return out;
}

std::string fromU32(std::u32string_view s) {
  std::string out;
  out.reserve(s.size());
  for (char32_t c : s) appendUtf8(out, c);
  return out;
}

bool isValidUtf8(std::string_view s) {
  for (std::size_t i = 0; i < s.size();) {
    const std::size_t before = i;
    const char32_t c = decodeAt(s, i);
    if (c == 0xFFFD) {
      // 本物の U+FFFD（EF BF BD）なら正しい
      if (!(i - before == 3 && static_cast<unsigned char>(s[before]) == 0xEF)) return false;
    }
  }
  return true;
}

std::string sanitizeUtf8(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (std::size_t i = 0; i < s.size();) appendUtf8(out, decodeAt(s, i));
  return out;
}

bool isJsSpace(char32_t c) {
  switch (c) {
    case 0x09: case 0x0A: case 0x0B: case 0x0C: case 0x0D: case 0x20: case 0xA0: case 0x1680:
    case 0x2028: case 0x2029: case 0x202F: case 0x205F: case 0x3000: case 0xFEFF:
      return true;
    default:
      return c >= 0x2000 && c <= 0x200A;
  }
}

bool isLineTerminator(char32_t c) { return c == 0x0A || c == 0x0D || c == 0x2028 || c == 0x2029; }

namespace {
// s の先頭から空白が続くバイト数
std::size_t leadingSpaceBytes(std::string_view s) {
  std::size_t i = 0;
  while (i < s.size()) {
    std::size_t j = i;
    if (!isJsSpace(decodeAt(s, j))) break;
    i = j;
  }
  return i;
}
// s の末尾の空白を除いた長さ（バイト）
std::size_t endWithoutSpace(std::string_view s) {
  std::size_t end = 0;
  for (std::size_t i = 0; i < s.size();) {
    const char32_t c = decodeAt(s, i);
    if (!isJsSpace(c)) end = i;
  }
  return end;
}
}  // namespace

std::string trimStart(std::string_view s) { return std::string(s.substr(leadingSpaceBytes(s))); }
std::string trimEnd(std::string_view s) { return std::string(s.substr(0, endWithoutSpace(s))); }
std::string trim(std::string_view s) {
  const std::size_t a = leadingSpaceBytes(s);
  if (a == s.size()) return {};
  return std::string(s.substr(a, endWithoutSpace(s) - a));
}

namespace {
char32_t lowerCp(char32_t c) {
  if (c >= 'A' && c <= 'Z') return c + 32;
  if (c < 0x80) return c;
  if ((c >= 0xC0 && c <= 0xDE && c != 0xD7)) return c + 32;
  if (c >= 0x391 && c <= 0x3A9 && c != 0x3A2) return c + 32;
  if (c >= 0x410 && c <= 0x42F) return c + 32;
  if (c >= 0x400 && c <= 0x40F) return c + 80;
  if (c >= 0xFF21 && c <= 0xFF3A) return c + 32;
  return c;
}
char asciiLowerCh(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c; }
}  // namespace

std::string toLower(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (std::size_t i = 0; i < s.size();) {
    if (static_cast<unsigned char>(s[i]) < 0x80) {
      out += asciiLowerCh(s[i]);
      ++i;
      continue;
    }
    const std::size_t st = i;
    const char32_t c = decodeAt(s, i);
    const char32_t l = lowerCp(c);
    if (l == c) out.append(s.substr(st, i - st));
    else appendUtf8(out, l);
  }
  return out;
}

std::string asciiLower(std::string_view s) {
  std::string out(s);
  for (char& c : out) c = asciiLowerCh(c);
  return out;
}

std::string toUpper(std::string_view s) {
  std::string out(s);
  for (char& c : out)
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 32);
  return out;
}

bool iequals(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i)
    if (asciiLowerCh(a[i]) != asciiLowerCh(b[i])) return false;
  return true;
}

bool istartsWith(std::string_view s, std::string_view prefix) {
  return s.size() >= prefix.size() && iequals(s.substr(0, prefix.size()), prefix);
}

bool icontains(std::string_view hay, std::string_view needle) {
  if (needle.empty()) return true;
  if (needle.size() > hay.size()) return false;
  for (std::size_t i = 0; i + needle.size() <= hay.size(); ++i)
    if (iequals(hay.substr(i, needle.size()), needle)) return true;
  return false;
}

std::size_t utf16Length(std::string_view s) {
  std::size_t n = 0;
  for (std::size_t i = 0; i < s.size();) n += decodeAt(s, i) >= 0x10000 ? 2 : 1;
  return n;
}

std::string utf16Prefix(std::string_view s, std::size_t n) {
  std::size_t units = 0, i = 0;
  while (i < s.size()) {
    std::size_t j = i;
    const std::size_t w = decodeAt(s, j) >= 0x10000 ? 2 : 1;
    if (units + w > n) break;
    units += w;
    i = j;
  }
  return std::string(s.substr(0, i));
}

std::size_t codepointCount(std::string_view s) {
  std::size_t n = 0;
  for (std::size_t i = 0; i < s.size(); ++n) decodeAt(s, i);
  return n;
}

bool isAsciiDigits(std::string_view s) {
  if (s.empty()) return false;
  for (char c : s)
    if (c < '0' || c > '9') return false;
  return true;
}

std::string replaceAll(std::string s, std::string_view from, std::string_view to) {
  if (from.empty()) return s;
  std::size_t pos = 0;
  while ((pos = s.find(from, pos)) != std::string::npos) {
    s.replace(pos, from.size(), to);
    pos += to.size();
  }
  return s;
}

std::string join(const std::vector<std::string>& v, std::string_view sep) {
  std::string out;
  for (std::size_t i = 0; i < v.size(); ++i) {
    if (i) out += sep;
    out += v[i];
  }
  return out;
}

std::vector<std::string> splitAny(std::string_view s, std::u32string_view sepCps, bool alsoJsSpace) {
  std::vector<std::string> out;
  std::string cur;
  for (std::size_t i = 0; i < s.size();) {
    const std::size_t st = i;
    const char32_t c = decodeAt(s, i);
    const bool sep = (alsoJsSpace && isJsSpace(c)) || sepCps.find(c) != std::u32string_view::npos;
    if (sep) {
      if (!cur.empty()) out.push_back(std::move(cur));
      cur.clear();
    } else {
      cur.append(s.substr(st, i - st));
    }
  }
  if (!cur.empty()) out.push_back(std::move(cur));
  return out;
}

double jsRound(double x) {
  if (!std::isfinite(x)) return x;
  const double r = std::floor(x);
  return (x - r >= 0.5) ? r + 1 : r;
}

std::string jsNumber(double x) {
  if (std::isnan(x)) return "NaN";
  if (x == 0) return "0";
  if (std::isinf(x)) return x < 0 ? "-Infinity" : "Infinity";
  std::string sign;
  if (x < 0) {
    sign = "-";
    x = -x;
  }
  char buf[64];
  auto res = std::to_chars(buf, buf + sizeof buf, x, std::chars_format::scientific);
  std::string sci(buf, res.ptr);
  const auto ePos = sci.find('e');
  std::string mant = sci.substr(0, ePos);
  const int e = std::atoi(sci.c_str() + ePos + 1);
  std::string digits;
  for (char c : mant)
    if (c != '.') digits += c;
  const int k = static_cast<int>(digits.size());
  const int n = e + 1;
  std::string out;
  if (k <= n && n <= 21) {
    out = digits + std::string(static_cast<std::size_t>(n - k), '0');
  } else if (0 < n && n <= 21) {
    out = digits.substr(0, static_cast<std::size_t>(n)) + "." + digits.substr(static_cast<std::size_t>(n));
  } else if (-6 < n && n <= 0) {
    out = "0." + std::string(static_cast<std::size_t>(-n), '0') + digits;
  } else {
    const int ex = n - 1;
    out = digits.substr(0, 1);
    if (k > 1) out += "." + digits.substr(1);
    out += "e";
    out += ex < 0 ? "-" : "+";
    out += std::to_string(ex < 0 ? -ex : ex);
  }
  return sign + out;
}

}  // namespace roadmap::text
