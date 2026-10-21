// SHA-256（RFC 6234 のテストベクタ）と、添付のメタ情報の sha256（Issue #19）
#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <string>

#include "app_controller.hpp"
#include "roadmap/attachments.hpp"
#include "roadmap/sha256.hpp"

using namespace roadmap;

namespace {
FixedClock clk(LocalTime{2026, 10, 21, 9, 30, 0});
const std::string kProj = "data/案件A";
const std::string kAbc = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
const std::string kPdfData = "1ad9615552126eb88b27e3f5c20c9932a9efafe7a58a790bf8d0d92d0fdc5661";
const std::string kPngData = "2d4566582844690f8634a8b2534ea5221560038c6c0650c99140759bad603ae2";
}  // namespace

TEST_CASE("sha256: RFC 6234 のテストベクタ（abc・空・448 ビット・100万個の a）") {
  CHECK(sha256Hex("abc") == kAbc);
  CHECK(sha256Hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  CHECK(sha256Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
  const std::string million(1'000'000, 'a');
  CHECK(sha256Hex(million) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST_CASE("sha256: パディングの境目（55・56・63・64・65 バイト）と UTF-8 の本文") {
  CHECK(sha256Hex(std::string(55, 'a')) == "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318");
  CHECK(sha256Hex(std::string(56, 'a')) == "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a");
  CHECK(sha256Hex(std::string(63, 'a')) == "7d3e74a05d7db15bce4ad9ec0658ea98e3f06eeecf16b4c6fff2da457ddc2f34");
  CHECK(sha256Hex(std::string(64, 'a')) == "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb");
  CHECK(sha256Hex(std::string(65, 'a')) == "635361c48bb9eab14198e76ea8ab7f1a41685d6ad62aa9146d301d4f17eb0ae0");
  CHECK(sha256Hex(std::string(119, 'a')) == "31eba51c313a5c08226adf18d4a359cfdfd8d2e816b13f4af952f7ea6584dcfb");
  CHECK(sha256Hex(std::string(120, 'a')) == "2f3d335432c70b580af0e8e1b3674a7c020d683aa5f73aaaedfdc55af904c21c");
  CHECK(sha256Hex("あいう") == "486da9b15cffbdea0966687981c51c0281c446681fdc22dad0b8fdca83e99f09");
}

TEST_CASE("sha256: 何回に分けて食わせても、まとめて食わせたときと同じ（reset で使い回せる）") {
  std::string data;
  for (int i = 0; i < 300; ++i) data.push_back(static_cast<char>(i * 7 + 3));
  const std::string whole = sha256Hex(data);
  for (const std::size_t step : {1u, 3u, 63u, 64u, 65u, 200u}) {
    Sha256 h;
    for (std::size_t at = 0; at < data.size(); at += step) h.update(data.data() + at, std::min<std::size_t>(step, data.size() - at));
    CHECK(h.hexDigest() == whole);
  }
  Sha256 h;
  h.update("abc");
  CHECK(h.hexDigest() == kAbc);
  h.reset();
  h.update("abc");
  CHECK(h.hexDigest() == kAbc);
  CHECK(h.digest().size() == 32);
}

TEST_CASE("sha256: isSha256Hex は 16進小文字ちょうど 64 文字だけ通す") {
  CHECK(isSha256Hex(kAbc));
  CHECK_FALSE(isSha256Hex(""));
  CHECK_FALSE(isSha256Hex(kAbc.substr(1)));                         // 63 文字
  CHECK_FALSE(isSha256Hex(kAbc + "0"));                             // 65 文字
  std::string upper = kAbc;
  upper[0] = 'B';
  CHECK_FALSE(isSha256Hex(upper));                                  // 大文字
  std::string bad = kAbc;
  bad[10] = 'g';
  CHECK_FALSE(isSha256Hex(bad));                                    // 16進でない文字
  bad[10] = ' ';
  CHECK_FALSE(isSha256Hex(bad));
}

TEST_CASE("sha256: 添付すると本体の SHA-256 がメタ情報に入る（ファイル・データ・コピー。移動は引き継ぐ）") {
  MemFileStore fs;
  AttachmentMap meta;
  fs.put("ext/仕様書.pdf", "PDFDATA");
  const auto r = attachFiles(fs, clk, kProj, meta, "1", {"ext/仕様書.pdf"}, "山田");
  REQUIRE(r.added.size() == 1);
  CHECK(r.added[0].sha256 == kPdfData);
  CHECK(meta["1"][0].sha256 == kPdfData);
  const auto d = attachData(fs, clk, kProj, meta, "1", "貼り付け.bmp", "PNGDATA", "山田");
  REQUIRE(d.ok());
  CHECK(d->sha256 == kPngData);
  // コピーは、コピーした本体から計算する（元のメタ情報の値は信用しない）
  meta["1"][0].sha256 = "";
  const auto c = copyAttachment(fs, clk, kProj, meta, "1", 0, "2", "山田");
  REQUIRE(c.ok());
  CHECK(c->sha256 == kPdfData);
  // 移動は引き継ぐ（本体は読まない）
  const auto m = moveAttachment(fs, kProj, meta, "1", 1, "3");
  REQUIRE(m.ok());
  CHECK(m->sha256 == kPngData);
  CHECK(meta["3"][0].sha256 == kPngData);
  // 古い（sha256 のない）メタ情報を移動しても空のまま
  meta["1"][0].sha256 = "";
  const auto m2 = moveAttachment(fs, kProj, meta, "1", 0, "4");
  REQUIRE(m2.ok());
  CHECK(m2->sha256.empty());
}

TEST_CASE("sha256: 設定 JSON の往復。sha256 がなければ項目を出さず、既存のメタ情報は空として読む") {
  AttachmentMap m;
  m["1"].push_back({"a.txt", 3, "2026-10-20T09:30", "山田", kAbc});
  m["1"].push_back({"b.txt", 5, "2026-10-20T09:31", "", ""});  // 古いメタ情報（sha256 なし）
  const Json j = attachmentsToJson(m);
  CHECK(j["1"][0]["sha256"] == kAbc);
  CHECK_FALSE(j["1"][1].contains("sha256"));  // 空なら出さない = 既存の設定ファイルは変わらない
  CHECK(attachmentsFromJson(j) == m);
  // sha256 のない JSON（今までの設定ファイル）は壊さず、空として読む
  const Json old = Json::parse(R"({"1":[{"name":"a.txt","size":3,"at":"2026-10-20T09:30","who":"山田"}]})");
  const AttachmentMap om = attachmentsFromJson(old);
  REQUIRE(om.count("1") == 1);
  REQUIRE(om.at("1").size() == 1);
  CHECK(om.at("1")[0].sha256.empty());
  CHECK(attachmentsToJson(om) == old);
}

TEST_CASE("sha256: 壊れた値（長さ・文字・型）は読み飛ばし、ほかの項目は読む") {
  std::string upper = kAbc;
  upper[0] = 'B';
  std::string nonHex = kAbc;
  nonHex[5] = 'z';
  Json arr = Json::array();
  const auto item = [&](const Json& v) {
    arr.push_back(Json{{"name", "f" + std::to_string(arr.size()) + ".txt"}, {"size", std::uint64_t{7}}, {"at", "2026-10-20T09:30"}, {"who", "山田"}, {"sha256", v}});
  };
  item(kAbc.substr(1));  // 短い
  item(kAbc + "0");      // 長い
  item(upper);           // 大文字
  item(nonHex);          // 16進でない
  item(123);             // 数値
  item(nullptr);         // null
  item(kAbc);            // これだけ正しい
  Json j = Json::object();
  j["1"] = arr;
  const AttachmentMap m = attachmentsFromJson(j);
  REQUIRE(m.at("1").size() == 7);  // 項目は消さない
  for (std::size_t k = 0; k < 6; ++k) {
    CHECK(m.at("1")[k].sha256.empty());
    CHECK(m.at("1")[k].size == 7);
    CHECK(m.at("1")[k].who == "山田");
  }
  CHECK(m.at("1")[6].sha256 == kAbc);
}

TEST_CASE("sha256: 50MB のファイルも IFileStore の読み込みで付けられ、値が合う") {
  MemFileStore fs;
  AttachmentMap meta;
  const std::string big(static_cast<std::size_t>(kMaxAttachFileBytes), 'a');
  fs.put("ext/big.bin", big);
  const auto r = attachFiles(fs, clk, kProj, meta, "1", {"ext/big.bin"}, "山田");
  REQUIRE(r.added.size() == 1);
  CHECK(r.added[0].sha256 == sha256Hex(big));
  CHECK(r.added[0].sha256.size() == 64);
}
