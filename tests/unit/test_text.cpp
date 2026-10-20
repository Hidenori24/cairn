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

namespace {
// 幅の代わりに文字数（コードポイント）で測る
float cpWidth(const std::string& s) { return static_cast<float>(text::codepointCount(s)); }
std::vector<std::string> anc4() { return {"要件定義", "認証", "ログイン", "画面"}; }
}  // namespace

TEST_CASE("ancestorPath: 収まるときは全部・親がいなければ空") {
  CHECK(text::ancestorPath({}, cpWidth, 100).empty());
  const auto k = text::ancestorPath(anc4(), cpWidth, 100);
  CHECK(k == std::vector<int>{0, 1, 2, 3});
  CHECK(text::ancestorPathText(anc4(), k) == "要件定義 › 認証 › ログイン › 画面");
}

TEST_CASE("ancestorPath: 長いときは真ん中を省略（先頭と直近の親を残す）") {
  const auto names = anc4();
  // 全部 = 4+2+4+2 + 区切り3×3 = 21
  auto k = text::ancestorPath(names, cpWidth, 20);
  CHECK(text::ancestorPathText(names, k) == "要件定義 › … › ログイン › 画面");
  k = text::ancestorPath(names, cpWidth, 15);
  CHECK(text::ancestorPathText(names, k) == "要件定義 › … › 画面");
  k = text::ancestorPath(names, cpWidth, 8);
  CHECK(text::ancestorPathText(names, k) == "… › 画面");
  // どうしても収まらなくても、直近の親は必ず残す
  k = text::ancestorPath(names, cpWidth, 1);
  CHECK(text::ancestorPathText(names, k) == "… › 画面");
  CHECK(k == std::vector<int>{-1, 3});
  // 親が1人なら省略しない
  const std::vector<std::string> one = {"長い長い長い名前"};
  k = text::ancestorPath(one, cpWidth, 1);
  CHECK(text::ancestorPathText(one, k) == "長い長い長い名前");
}

TEST_CASE("ancestorPath: 区切りを変えられる") {
  const auto names = anc4();
  const auto k = text::ancestorPath(names, cpWidth, 100, " / ");
  CHECK(text::ancestorPathText(names, k, " / ") == "要件定義 / 認証 / ログイン / 画面");
}
