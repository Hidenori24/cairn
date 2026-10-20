#include "roadmap/outline.hpp"

#include <algorithm>

#include "roadmap/text.hpp"

namespace roadmap {

namespace {

constexpr int kMaxJumpWarns = 10;

struct Raw {
  std::string name;
  int tabs = 0;  // 字下げのタブの数
  int sp = 0;    // 字下げの空白の幅（半角 1・全角スペース 2）
  int line = 0;
};

// i の位置の 1 文字（なければ 0）。i は進めない
char32_t peek(std::string_view s, std::size_t i) {
  if (i >= s.size()) return 0;
  return text::decodeAt(s, i);
}

// i から JS の空白を飛ばした位置
std::size_t skipSpaces(std::string_view s, std::size_t i) {
  while (i < s.size()) {
    std::size_t j = i;
    if (!text::isJsSpace(text::decodeAt(s, j))) break;
    i = j;
  }
  return i;
}

bool isCjkBullet(char32_t c) {
  return c == 0x30FB /* ・ */ || c == 0x2022 /* • */ || c == 0x25CF /* ● */ || c == 0x25CB /* ○ */ ||
         c == 0x2023 /* ‣ */ || c == 0x25E6 /* ◦ */ || c == 0x25AA /* ▪ */;
}

// 先頭の箇条書きの印（- * + ・ •、1. 、1) ）と、そのあとの [ ] [x] を外した残り。印がなければ s のまま
std::string_view stripMarker(std::string_view s) {
  if (s.empty()) return s;
  std::size_t i = 0;
  const char32_t c = text::decodeAt(s, i);
  std::size_t after = 0;
  if (c == '-' || c == '*' || c == '+') {
    // 印のあとに空白（または行末）が要る（"-5度" "*注意" は件名）
    if (i < s.size() && !text::isJsSpace(peek(s, i))) return s;
    after = skipSpaces(s, i);
  } else if (isCjkBullet(c)) {
    after = skipSpaces(s, i);
  } else if (c >= '0' && c <= '9') {
    // 1〜3 桁の数字 + . または ) + 空白（"2026. 目標" "1.1 設計" は件名）
    std::size_t j = 0;
    int digits = 0;
    while (j < s.size() && s[j] >= '0' && s[j] <= '9') {
      ++j;
      ++digits;
    }
    if (digits > 3 || j >= s.size()) return s;
    std::size_t k = j;
    const char32_t d = text::decodeAt(s, k);
    if (d != '.' && d != ')' && d != 0xFF09 /* ） */) return s;
    if (k >= s.size() || !text::isJsSpace(peek(s, k))) return s;
    after = skipSpaces(s, k);
  } else {
    return s;
  }
  std::string_view rest = s.substr(after);
  // Markdown のチェックボックス "[ ] " "[x] "
  if (rest.size() >= 4 && rest[0] == '[' && (rest[1] == ' ' || rest[1] == 'x' || rest[1] == 'X') && rest[2] == ']' &&
      text::isJsSpace(peek(rest, 3)))
    rest = rest.substr(skipSpaces(rest, 3));
  return rest;
}

}  // namespace

Outline parseOutline(std::string_view textIn, std::size_t maxItems) {
  Outline out;
  const std::string clean = text::sanitizeUtf8(textIn);
  std::string_view all = clean;
  if (all.size() >= 3 && all.substr(0, 3) == "\xEF\xBB\xBF") all.remove_prefix(3);

  // ---- 行ごとに字下げと件名を読む ----
  std::vector<Raw> raws;
  int lineNo = 0;
  std::size_t pos = 0;
  while (pos <= all.size()) {
    std::size_t e = pos;
    while (e < all.size() && all[e] != '\n' && all[e] != '\r') ++e;
    const std::string_view line = all.substr(pos, e - pos);
    ++lineNo;
    if (e + 1 < all.size() && all[e] == '\r' && all[e + 1] == '\n') ++e;
    pos = e + 1;

    Raw r;
    r.line = lineNo;
    std::size_t i = 0;
    while (i < line.size()) {
      std::size_t j = i;
      const char32_t c = text::decodeAt(line, j);
      if (c == '\t') ++r.tabs;
      else if (c == 0x3000) r.sp += 2;
      else if (text::isJsSpace(c)) r.sp += 1;
      else break;
      i = j;
    }
    std::string_view rest = line.substr(i);
    if (rest.empty()) continue;
    rest = stripMarker(rest);
    // タブ区切り（Excel のコピー）は 1 列目だけ
    const std::size_t tab = rest.find('\t');
    const bool cells = tab != std::string_view::npos;
    std::string name = text::trim(cells ? rest.substr(0, tab) : rest);
    if (cells && name.size() >= 2 && name.front() == '"' && name.back() == '"')
      name = text::replaceAll(name.substr(1, name.size() - 2), "\"\"", "\"");
    if (name.empty()) continue;
    if (raws.size() >= maxItems) {
      out.truncated = true;
      ++out.ignored;
      continue;
    }
    r.name = std::move(name);
    raws.push_back(std::move(r));
  }
  if (out.truncated)
    out.warns.push_back("上限の" + std::to_string(maxItems) + "件までを対象にしました（ほか" + std::to_string(out.ignored) +
                        "行は無視）");

  // ---- 字下げの幅から段を決める ----
  // 空白だけで字下げした行のいちばん小さい幅を 1 段とする。タブはその幅（空白の字下げがなければ 1 段）
  int unitSp = 0;
  for (const auto& r : raws)
    if (r.tabs == 0 && r.sp > 0 && (unitSp == 0 || r.sp < unitSp)) unitSp = r.sp;
  const int tabW = unitSp > 0 ? unitSp : 1;
  std::vector<int> w;
  w.reserve(raws.size());
  for (const auto& r : raws) w.push_back(r.tabs * tabW + r.sp);
  // 全体が同じだけ字下げされていれば、その分は無視する
  const int base = w.empty() ? 0 : *std::min_element(w.begin(), w.end());
  int unit = 0;
  for (int& x : w) {
    x -= base;
    if (x > 0 && (unit == 0 || x < unit)) unit = x;
  }

  int prev = -1;
  int jumps = 0, deep = 0;
  for (std::size_t k = 0; k < raws.size(); ++k) {
    int lv = unit > 0 ? w[k] / unit : 0;
    if (lv > prev + 1) {
      lv = prev + 1;
      if (++jumps <= kMaxJumpWarns)
        out.warns.push_back(std::to_string(raws[k].line) + "行目: 字下げが2段以上深いので、1段だけ下げました");
    }
    if (lv > kOutlineMaxDepth) {
      lv = kOutlineMaxDepth;
      ++deep;
    }
    prev = lv;
    out.maxLevel = std::max(out.maxLevel, lv);
    out.items.push_back({std::move(raws[k].name), lv, raws[k].line});
  }
  if (jumps > kMaxJumpWarns)
    out.warns.push_back("ほか" + std::to_string(jumps - kMaxJumpWarns) + "件、字下げが飛んでいる行を丸めました");
  if (deep > 0)
    out.warns.push_back("階層が深すぎる" + std::to_string(deep) + "行は、深さ" + std::to_string(kOutlineMaxDepth) + "に丸めました");
  return out;
}

}  // namespace roadmap
