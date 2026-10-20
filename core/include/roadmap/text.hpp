// UTF-8 文字列の小道具。JS版（ブラウザ）の String のふるまいに合わせるためのもの。
//  - trim / \s は JS の空白の定義（全角空白 U+3000 や U+FEFF を含む）
//  - 長さ・切り出しは UTF-16 の単位（JS の length / slice と同じ）
//  - 小文字化は ASCII と全角英字・Latin-1（JS の toLowerCase の主要部分）
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace roadmap::text {

// UTF-8 を1文字ずつ読む。不正なバイトは U+FFFD として1バイト進む。
char32_t decodeAt(std::string_view s, std::size_t& i);
void appendUtf8(std::string& out, char32_t cp);
std::u32string toU32(std::string_view s);
std::string fromU32(std::u32string_view s);

bool isValidUtf8(std::string_view s);
// 不正なバイト列を U+FFFD に置き換える
std::string sanitizeUtf8(std::string_view s);

// JS の空白（\s・trim の対象）
bool isJsSpace(char32_t c);
// JS の行終端（正規表現の . に一致しない文字）
bool isLineTerminator(char32_t c);

std::string trim(std::string_view s);
std::string trimEnd(std::string_view s);
std::string trimStart(std::string_view s);

std::string toLower(std::string_view s);   // JS の toLowerCase 相当（主要な範囲）
std::string asciiLower(std::string_view s);
std::string toUpper(std::string_view s);   // ASCII のみ大文字化（キーの大文字化用）

// ASCII の大文字小文字を無視して部分一致・前方一致・完全一致
bool icontains(std::string_view hay, std::string_view needle);
bool istartsWith(std::string_view s, std::string_view prefix);
bool iequals(std::string_view a, std::string_view b);

// UTF-16 単位の長さと、先頭 n 単位の切り出し（サロゲートの途中では切らない）
std::size_t utf16Length(std::string_view s);
std::string utf16Prefix(std::string_view s, std::size_t n);
std::size_t codepointCount(std::string_view s);

bool isAsciiDigits(std::string_view s);   // /^\d+$/

std::string replaceAll(std::string s, std::string_view from, std::string_view to);
std::string join(const std::vector<std::string>& v, std::string_view sep);
// 区切り文字の集合のどれかで分割（空のトークンは捨てる）。sepCps は区切りに使うコードポイント
std::vector<std::string> splitAny(std::string_view s, std::u32string_view sepCps, bool alsoJsSpace);

// 祖先の名前の並び（根 → 直近の親）を、幅 maxW に収まるよう真ん中を省略して並べる。
// 残す名前の添字を返し、省いた区間は -1（「…」）で表す。収まらなくても、直近の親は必ず残す。
// measure は結合した文字列の幅を返す（UI は画面の幅、テストは文字数など）。画面に依存しない純粋な関数
std::vector<int> ancestorPath(const std::vector<std::string>& names,
                              const std::function<float(const std::string&)>& measure, float maxW,
                              std::string_view sep = " › ");
// ancestorPath の結果を文字列にする（-1 は「…」）
std::string ancestorPathText(const std::vector<std::string>& names, const std::vector<int>& kept,
                             std::string_view sep = " › ");

// JS の Math.round（0.5 は +∞ 方向）
double jsRound(double x);
// JS の Number → 文字列（String(n)）
std::string jsNumber(double x);

}  // namespace roadmap::text
