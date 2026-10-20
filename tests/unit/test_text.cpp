#include <doctest/doctest.h>

#include "roadmap/encoding.hpp"
#include "roadmap/text.hpp"

using namespace roadmap;

TEST_CASE("text: JS と同じ trim（全角空白・BOM文字を含む）") {
  CHECK(text::trim("  a b \t") == "a b");
  CHECK(text::trim("\xE3\x80\x80全角\xE3\x80\x80") == "全角");
  CHECK(text::trim("\xEF\xBB\xBF" "x") == "x");
  CHECK(text::trim("   ") == "");
  CHECK(text::trimEnd("abc \r\n") == "abc");
}

TEST_CASE("text: UTF-16 単位の切り出し") {
  CHECK(text::utf16Length("あいう") == 3);
  CHECK(text::utf16Length("\xF0\x9F\x98\x80") == 2);  // 絵文字はサロゲート2つ
  CHECK(text::utf16Prefix("あいうえお", 2) == "あい");
  CHECK(text::utf16Prefix("a\xF0\x9F\x98\x80" "b", 2) == "a");  // サロゲートの途中では切らない
}

TEST_CASE("text: jsRound と jsNumber") {
  CHECK(text::jsRound(2.5) == 3);
  CHECK(text::jsRound(-2.5) == -2);
  CHECK(text::jsRound(0.49999999999999994) == 0);
  CHECK(text::jsNumber(1) == "1");
  CHECK(text::jsNumber(0.2) == "0.2");
  CHECK(text::jsNumber(2.5) == "2.5");
  CHECK(text::jsNumber(-0.0) == "0");
  CHECK(text::jsNumber(1e21) == "1e+21");
  CHECK(text::jsNumber(123456789012) == "123456789012");
  CHECK(text::jsNumber(0.000001) == "0.000001");
  CHECK(text::jsNumber(1e-7) == "1e-7");
  CHECK(text::jsNumber(0.1 + 0.2) == "0.30000000000000004");
}

TEST_CASE("text: 小文字化") {
  CHECK(text::toLower("Issue ID") == "issue id");
  CHECK(text::toLower("ＡＢＣ") == "ａｂｃ");
  CHECK(text::toLower("課題ID") == "課題id");
}

TEST_CASE("encoding: UTF-8 / BOM / Shift_JIS / 置換") {
  using encoding::Detected;
  auto a = encoding::decodeText("件名");
  CHECK(a.text == "件名");
  CHECK(a.detected == Detected::Utf8);
  auto b = encoding::decodeText("\xEF\xBB\xBF件名");
  CHECK(b.text == "件名");
  CHECK(b.detected == Detected::Utf8Bom);
  // 「件名」を CP932 で
  auto c = encoding::decodeText("\x8C\x8F\x96\xBC,\xB1");
  CHECK(c.detected == Detected::Cp932);
  CHECK(c.text == "件名,ｱ");
  // CP932 でも読めない（先頭バイトだけ）
  auto d = encoding::decodeText("abc\x81");
  CHECK(d.detected == Detected::Utf8Replaced);
  CHECK(d.text == "abc\xEF\xBF\xBD");
}
