// 添付の移動・コピー・並べ替え、クリップボードの画像（DIB → BMP）。Issue #19 の残りの部分
#include <doctest/doctest.h>

#include <cstdint>
#include <string>

#include "app_controller.hpp"
#include "roadmap/attachments.hpp"

using namespace roadmap;

namespace {
FixedClock clk(LocalTime{2026, 10, 21, 9, 30, 0});
const std::string kProj = "data/案件A";

struct Env {
  MemFileStore fs;
  AttachmentMap meta;
  void add(const std::string& issue, const std::string& name, const std::string& data) {
    fs.put("ext/" + name, data);
    auto r = attachFiles(fs, clk, kProj, meta, issue, {"ext/" + name}, "山田");
    REQUIRE(r.added.size() == 1);
  }
  std::string path(const std::string& issue, const std::string& name) const { return kProj + "/添付/" + issue + "/" + name; }
};
}  // namespace

TEST_CASE("moveAttachment: 本体とメタ情報を別の課題へ動かす（日時・作成者は引き継ぐ）") {
  Env e;
  e.add("1", "図.png", "PNGDATA");
  e.meta["1"][0].at = "2026-10-01T08:00";
  e.meta["1"][0].who = "鈴木";
  const std::uint64_t total = attachmentsTotalBytes(e.meta);
  const auto r = moveAttachment(e.fs, kProj, e.meta, "1", 0, "2");
  REQUIRE(r.ok());
  CHECK(r->name == "図.png");
  CHECK(r->at == "2026-10-01T08:00");
  CHECK(r->who == "鈴木");
  CHECK_FALSE(e.fs.exists(e.path("1", "図.png")));
  REQUIRE(e.fs.exists(e.path("2", "図.png")));
  CHECK(e.fs.content(e.path("2", "図.png")) == "PNGDATA");
  CHECK(e.meta.count("1") == 0);  // 空になった課題の項目は残さない
  REQUIRE(e.meta["2"].size() == 1);
  CHECK(e.meta["2"][0] == *r);
  CHECK(attachmentsTotalBytes(e.meta) == total);  // 合計は変わらない
}

TEST_CASE("moveAttachment: 先に同じ名前があれば連番（本体だけでなく、メタ情報だけにある名前も避ける）") {
  Env e;
  e.add("1", "a.txt", "one");
  e.add("2", "a.txt", "two");
  e.meta["2"].push_back({"a-2.txt", 3, "", "", ""});  // 本体は無いがメタ情報にある
  const auto r = moveAttachment(e.fs, kProj, e.meta, "1", 0, "2");
  REQUIRE(r.ok());
  CHECK(r->name == "a-3.txt");
  CHECK(e.fs.content(e.path("2", "a-3.txt")) == "one");
  CHECK(e.fs.content(e.path("2", "a.txt")) == "two");  // 先にあったものは上書きしない
  REQUIRE(e.meta["2"].size() == 3);
  CHECK(e.meta["2"][2].name == "a-3.txt");
}

TEST_CASE("moveAttachment: 途中の添付を動かしても、残りの並びと本体は変わらない") {
  Env e;
  e.add("1", "a.txt", "A");
  e.add("1", "b.txt", "B");
  e.add("1", "c.txt", "C");
  REQUIRE(moveAttachment(e.fs, kProj, e.meta, "1", 1, "2").ok());
  REQUIRE(e.meta["1"].size() == 2);
  CHECK(e.meta["1"][0].name == "a.txt");
  CHECK(e.meta["1"][1].name == "c.txt");
  CHECK(e.fs.exists(e.path("1", "c.txt")));
  CHECK(e.fs.exists(e.path("2", "b.txt")));
}

TEST_CASE("moveAttachment: 自分自身・存在しない添付・本体がない・先が空は断り、何も変えない") {
  Env e;
  e.add("1", "a.txt", "A");
  const AttachmentMap before = e.meta;
  CHECK_FALSE(moveAttachment(e.fs, kProj, e.meta, "1", 0, "1").ok());      // 自分自身へ
  CHECK_FALSE(moveAttachment(e.fs, kProj, e.meta, "1", 5, "2").ok());      // 範囲外
  CHECK_FALSE(moveAttachment(e.fs, kProj, e.meta, "none", 0, "2").ok());   // 元の課題に添付がない
  CHECK_FALSE(moveAttachment(e.fs, kProj, e.meta, "1", 0, "").ok());       // 先がない
  CHECK_FALSE(moveAttachment(e.fs, "", e.meta, "1", 0, "2").ok());         // 置き場がない
  CHECK(e.meta == before);
  CHECK(e.fs.exists(e.path("1", "a.txt")));
  REQUIRE(e.fs.remove(e.path("1", "a.txt")));  // 本体が先に無くなっていた
  const auto r = moveAttachment(e.fs, kProj, e.meta, "1", 0, "2");
  CHECK_FALSE(r.ok());
  CHECK(e.meta == before);  // メタ情報だけを先へ動かして食い違わせない
  CHECK(e.meta.count("2") == 0);
}

TEST_CASE("moveAttachment: 移せなかったら元のまま（ファイルもメタ情報も）") {
  Env e;
  e.add("1", "a.txt", "A");
  const AttachmentMap before = e.meta;
  e.fs.setFailMoves(true);
  const auto r = moveAttachment(e.fs, kProj, e.meta, "1", 0, "2");
  CHECK_FALSE(r.ok());
  CHECK(e.meta == before);
  CHECK(e.fs.exists(e.path("1", "a.txt")));
  CHECK_FALSE(e.fs.exists(e.path("2", "a.txt")));
  e.fs.setFailMoves(false);
  e.fs.setFailWrites(true);  // フォルダも作れない
  CHECK_FALSE(moveAttachment(e.fs, kProj, e.meta, "1", 0, "2").ok());
  CHECK(e.meta == before);
  CHECK(e.fs.exists(e.path("1", "a.txt")));
}

TEST_CASE("moveAttachment・copyAttachment: 設定 JSON の往復で通らない課題 ID へは、ファイルを動かす前に断る") {
  Env e;
  e.add("1", "a.txt", "A");
  const AttachmentMap before = e.meta;
  const auto filesBefore = e.fs.paths();
  for (const char* bad : {"../../evil", "a/b", "a\\b", "..", ".", "x ", " x", "x.", "a:b", "a*b", "a\"b"}) {
    CHECK_FALSE(moveAttachment(e.fs, kProj, e.meta, "1", 0, bad).ok());
    CHECK_FALSE(copyAttachment(e.fs, clk, kProj, e.meta, "1", 0, bad, "山田").ok());
    CHECK(e.meta == before);  // メタ情報も本体も動かさない（移動で孤児にしない）
    CHECK(e.fs.paths() == filesBefore);
  }
  CHECK(e.fs.exists(e.path("1", "a.txt")));
  const auto r = moveAttachment(e.fs, kProj, e.meta, "1", 0, "../../evil");
  REQUIRE_FALSE(r.ok());
  CHECK(r.error().find("課題") != std::string::npos);
}

TEST_CASE("moveAttachment・copyAttachment: 動かしたあとのメタ情報は、保存して読み直しても残る") {
  Env e;
  e.add("1", "a.txt", "A");
  e.add("1", "b.txt", "B");
  REQUIRE(moveAttachment(e.fs, kProj, e.meta, "1", 0, "PRJ-2").ok());
  REQUIRE(copyAttachment(e.fs, clk, kProj, e.meta, "1", 0, "3 件目", "山田").ok());
  const AttachmentMap reloaded = attachmentsFromJson(attachmentsToJson(e.meta));
  CHECK(reloaded == e.meta);
  CHECK(reloaded.count("PRJ-2") == 1);
  CHECK(reloaded.count("3 件目") == 1);
  for (const auto& [id, list] : reloaded)
    for (const auto& a : list) CHECK(e.fs.exists(attachmentPath(kProj, id, a)));
}

TEST_CASE("copyAttachment: 本体を複製して先に付ける（元は残る。日時と作成者はいまのもの）") {
  Env e;
  e.add("1", "図.png", "PNGDATA");
  e.meta["1"][0].at = "2026-10-01T08:00";
  e.meta["1"][0].who = "鈴木";
  const auto r = copyAttachment(e.fs, clk, kProj, e.meta, "1", 0, "2", "山田");
  REQUIRE(r.ok());
  CHECK(r->name == "図.png");
  CHECK(r->size == 7);
  CHECK(r->at == "2026-10-21T09:30");
  CHECK(r->who == "山田");
  CHECK(e.fs.content(e.path("1", "図.png")) == "PNGDATA");
  CHECK(e.fs.content(e.path("2", "図.png")) == "PNGDATA");
  CHECK(e.meta["1"].size() == 1);
  CHECK(e.meta["1"][0].who == "鈴木");
  CHECK(e.meta["2"].size() == 1);
  CHECK(attachmentsTotalBytes(e.meta) == 14);  // コピーは合計に数える
  // もう一度コピーすると連番
  const auto r2 = copyAttachment(e.fs, clk, kProj, e.meta, "1", 0, "2", "山田");
  REQUIRE(r2.ok());
  CHECK(r2->name == "図-2.png");
}

TEST_CASE("copyAttachment: 合計・1ファイルの上限を超えるコピーは断り、何も作らない") {
  Env e;
  e.add("1", "a.bin", "12345678");
  AttachLimits lim;
  lim.perProject = 10;
  lim.perFile = 10;
  const AttachmentMap before = e.meta;
  const auto r = copyAttachment(e.fs, clk, kProj, e.meta, "1", 0, "2", "山田", lim);  // 8 + 8 > 10
  REQUIRE_FALSE(r.ok());
  CHECK(r.error().find("合計") != std::string::npos);
  CHECK(e.meta == before);
  CHECK_FALSE(e.fs.exists(e.path("2", "a.bin")));
  lim.perProject = 100;
  lim.perFile = 5;  // 1ファイルの上限が下がっていた
  const auto r2 = copyAttachment(e.fs, clk, kProj, e.meta, "1", 0, "2", "山田", lim);
  REQUIRE_FALSE(r2.ok());
  CHECK(r2.error().find("上限") != std::string::npos);
  // 移動は合計が増えないので、合計の上限いっぱいでも動かせる
  lim.perProject = 8;
  lim.perFile = 50;
  CHECK(moveAttachment(e.fs, kProj, e.meta, "1", 0, "2").ok());
}

TEST_CASE("copyAttachment: 自分自身・存在しない添付・本体がないは断る。書き込みに失敗してもメタ情報を増やさない") {
  Env e;
  e.add("1", "a.txt", "A");
  const AttachmentMap before = e.meta;
  CHECK_FALSE(copyAttachment(e.fs, clk, kProj, e.meta, "1", 0, "1", "山田").ok());
  CHECK_FALSE(copyAttachment(e.fs, clk, kProj, e.meta, "1", 3, "2", "山田").ok());
  CHECK_FALSE(copyAttachment(e.fs, clk, kProj, e.meta, "none", 0, "2", "山田").ok());
  CHECK_FALSE(copyAttachment(e.fs, clk, kProj, e.meta, "1", 0, "", "山田").ok());
  e.fs.setFailAtomicWrites(true);
  CHECK_FALSE(copyAttachment(e.fs, clk, kProj, e.meta, "1", 0, "2", "山田").ok());
  e.fs.setFailAtomicWrites(false);
  CHECK(e.meta == before);
  CHECK_FALSE(e.fs.exists(e.path("2", "a.txt")));
  REQUIRE(e.fs.remove(e.path("1", "a.txt")));
  CHECK_FALSE(copyAttachment(e.fs, clk, kProj, e.meta, "1", 0, "2", "山田").ok());
  CHECK(e.meta == before);
}

TEST_CASE("reorderAttachment: 同じ課題の中で並べ替える（本体は動かさない）") {
  Env e;
  e.add("1", "a.txt", "A");
  e.add("1", "b.txt", "B");
  e.add("1", "c.txt", "C");
  REQUIRE(reorderAttachment(e.meta, "1", 0, 2).ok());  // a を最後へ
  CHECK(e.meta["1"][0].name == "b.txt");
  CHECK(e.meta["1"][1].name == "c.txt");
  CHECK(e.meta["1"][2].name == "a.txt");
  REQUIRE(reorderAttachment(e.meta, "1", 2, 0).ok());  // a を先頭へ戻す
  CHECK(e.meta["1"][0].name == "a.txt");
  CHECK(reorderAttachment(e.meta, "1", 1, 1).ok());     // 同じ位置は何もしない
  CHECK(e.meta["1"][1].name == "b.txt");
  CHECK_FALSE(reorderAttachment(e.meta, "1", 3, 0).ok());
  CHECK_FALSE(reorderAttachment(e.meta, "1", 0, 3).ok());
  CHECK_FALSE(reorderAttachment(e.meta, "none", 0, 0).ok());
  CHECK(e.fs.exists(e.path("1", "a.txt")));
}

// ---- クリップボードの画像（DIB → BMP） ----
namespace {
void put16(std::string& s, std::uint32_t v) {
  s.push_back(static_cast<char>(v & 0xff));
  s.push_back(static_cast<char>((v >> 8) & 0xff));
}
void put32(std::string& s, std::uint32_t v) {
  put16(s, v & 0xffff);
  put16(s, v >> 16);
}
// BITMAPINFOHEADER（hdr = 40 / 56 / 108 / 124）のあとに、extra（マスク・色表）と画素を置いた DIB
std::string makeDib(std::uint32_t hdr, int w, int h, int bpp, std::uint32_t comp, std::uint32_t clrUsed, const std::string& extra,
                    std::size_t pixelBytes) {
  std::string s;
  put32(s, hdr);
  put32(s, static_cast<std::uint32_t>(w));
  put32(s, static_cast<std::uint32_t>(h));
  put16(s, 1);
  put16(s, static_cast<std::uint32_t>(bpp));
  put32(s, comp);
  put32(s, static_cast<std::uint32_t>(pixelBytes));
  put32(s, 2835);
  put32(s, 2835);
  put32(s, clrUsed);
  put32(s, 0);
  s.resize(hdr, '\0');  // 40 より大きいヘッダの残り（マスクなど）は 0
  s += extra;
  s.append(pixelBytes, 'p');
  return s;
}
std::uint32_t rd32(const std::string& s, std::size_t at) {
  std::uint32_t v = 0;
  for (int i = 3; i >= 0; --i) v = (v << 8) | static_cast<unsigned char>(s[at + static_cast<std::size_t>(i)]);
  return v;
}
}  // namespace

TEST_CASE("bmpFromDib: 24bit の DIB に 14 バイトのファイルヘッダを足す") {
  const std::string dib = makeDib(40, 4, 3, 24, 0, 0, "", 12 * 3);  // 4 画素 = 12 バイト（4 の倍数）× 3 行
  const auto r = bmpFromDib(dib);
  REQUIRE(r.ok());
  CHECK(r->size() == dib.size() + 14);
  CHECK(r->substr(0, 2) == "BM");
  CHECK(rd32(*r, 2) == r->size());
  CHECK(rd32(*r, 10) == 54);  // 14 + 40
  CHECK(r->substr(14) == dib);
}

TEST_CASE("bmpFromDib: 色表・ビットマスク・V5 ヘッダの分だけ画素の位置を後ろへ") {
  // 8bit: 色表 256 × 4 バイト
  auto r = bmpFromDib(makeDib(40, 4, 4, 8, 0, 0, std::string(256 * 4, 'c'), 16));
  REQUIRE(r.ok());
  CHECK(rd32(*r, 10) == 14 + 40 + 1024);
  // 色表の数を clrUsed で指定
  r = bmpFromDib(makeDib(40, 4, 4, 8, 0, 16, std::string(16 * 4, 'c'), 16));
  REQUIRE(r.ok());
  CHECK(rd32(*r, 10) == 14 + 40 + 64);
  // 32bit の BI_BITFIELDS（BITMAPINFOHEADER の後ろにマスク 3 つ）
  r = bmpFromDib(makeDib(40, 2, 2, 32, 3, 0, std::string(12, 'm'), 16));
  REQUIRE(r.ok());
  CHECK(rd32(*r, 10) == 14 + 40 + 12);
  // V5（124 バイト。マスクはヘッダの中）
  r = bmpFromDib(makeDib(124, 2, 2, 32, 3, 0, "", 16));
  REQUIRE(r.ok());
  CHECK(rd32(*r, 10) == 14 + 124);
  // 高さが負（上の行から）
  r = bmpFromDib(makeDib(40, 2, -2, 32, 0, 0, "", 16));
  REQUIRE(r.ok());
}

TEST_CASE("bmpFromDib: 壊れたヘッダ・極端な大きさ・画素が足りないものは断る") {
  CHECK_FALSE(bmpFromDib("").ok());
  CHECK_FALSE(bmpFromDib(std::string(20, '\0')).ok());                                  // ヘッダより短い
  CHECK_FALSE(bmpFromDib(makeDib(41, 4, 4, 24, 0, 0, "", 48)).ok());                    // 知らないヘッダの大きさ
  CHECK_FALSE(bmpFromDib(makeDib(40, 0, 4, 24, 0, 0, "", 48)).ok());                    // 幅 0
  CHECK_FALSE(bmpFromDib(makeDib(40, 4, 0, 24, 0, 0, "", 48)).ok());                    // 高さ 0
  CHECK_FALSE(bmpFromDib(makeDib(40, -4, 4, 24, 0, 0, "", 48)).ok());                   // 幅が負
  CHECK_FALSE(bmpFromDib(makeDib(40, 1000000, 1000000, 24, 0, 0, "", 48)).ok());        // 極端に大きい
  CHECK_FALSE(bmpFromDib(makeDib(40, 4, 4, 7, 0, 0, "", 48)).ok());                     // 知らない bit 数
  CHECK_FALSE(bmpFromDib(makeDib(40, 4, 4, 24, 9, 0, "", 48)).ok());                    // 知らない圧縮
  // stb_image が読めない形式は、貼り付けが成功したのにサムネイルが出ないことになるので、先に断る（理由つき）
  for (std::uint32_t comp : {1u, 2u, 4u, 5u, 6u}) {
    const auto r = bmpFromDib(makeDib(40, 4, 4, comp == 2 ? 4 : 8, comp, 0, std::string(comp == 2 ? 64 : 1024, 'c'), 16));
    CHECK_FALSE(r.ok());
    if (!r.ok()) CHECK(r.error().find("対応していない") != std::string::npos);
  }
  CHECK_FALSE(bmpFromDib(makeDib(40, 2, 2, 32, 6, 0, std::string(16, 'm'), 16)).ok());  // BI_ALPHABITFIELDS
  CHECK_FALSE(bmpFromDib(makeDib(108, 2, 2, 32, 6, 0, "", 16)).ok());
  CHECK_FALSE(bmpFromDib(makeDib(40, 4, 4, 24, 0, 0, "", 20)).ok());                    // 画素が足りない（BI_RGB は計算で分かる）
  CHECK_FALSE(bmpFromDib(makeDib(40, 4, 4, 8, 0, 0, std::string(100, 'c'), 16)).ok());  // 色表が途中で切れている
  CHECK_FALSE(bmpFromDib(makeDib(40, 4, 4, 8, 0, 999, "", 16)).ok());                   // 色の数が多すぎる
  std::string planes = makeDib(40, 4, 4, 24, 0, 0, "", 48);
  planes[12] = 2;  // biPlanes は 1 でなければならない
  CHECK_FALSE(bmpFromDib(planes).ok());
}

// ---- コントローラ ----
namespace {
struct CEnv {
  MemFileStore fs;
  FixedClock clock{LocalTime{2026, 10, 21, 9, 30, 0}};
  app::AppController c{fs, clock, "data", "roadmap_prefs.json"};
  void step(std::int64_t ms) {
    clock.advanceMs(ms);
    c.tick();
  }
  std::string lastToast() const { return c.toasts().empty() ? "" : c.toasts().back().text; }
};
}  // namespace

TEST_CASE("controller: 添付を別の課題へ移す・コピーする。保存され、再起動後も残る") {
  CEnv e;
  e.c.start();
  e.c.createProject(app::SampleKind::Empty, "案件", "");
  const std::string a = e.c.session()->addIssue("", "", "A");
  const std::string b = e.c.session()->addIssue("", "", "B");
  e.c.afterEdit();
  e.fs.put("ext/メモ.txt", "hello");
  e.fs.put("ext/図.png", "PNG");
  REQUIRE(e.c.attachFiles(a, {"ext/メモ.txt", "ext/図.png"}).added.size() == 2);
  REQUIRE(e.c.moveAttachment(a, 0, b));
  CHECK(e.c.attachmentCount(a) == 1);
  CHECK(e.c.attachmentCount(b) == 1);
  CHECK(e.fs.exists("data/案件/添付/" + b + "/メモ.txt"));
  CHECK_FALSE(e.fs.exists("data/案件/添付/" + a + "/メモ.txt"));
  REQUIRE(e.c.copyAttachment(a, 0, b));  // 図.png
  CHECK(e.c.attachmentCount(a) == 1);
  CHECK(e.c.attachmentCount(b) == 2);
  CHECK(e.fs.content("data/案件/添付/" + b + "/図.png") == "PNG");
  e.step(600);
  CEnv e2;
  e2.fs = e.fs;
  e2.c.start();
  REQUIRE(e2.c.session());
  CHECK(e2.c.attachmentCount(a) == 1);
  REQUIRE(e2.c.attachmentCount(b) == 2);
  CHECK(e2.c.attachmentsOf(b)[0].name == "メモ.txt");
  CHECK(e2.c.attachmentExists(b, 0));
}

TEST_CASE("controller: 存在しない課題・自分自身・平置きへは移せず、理由を通知して何も変えない") {
  CEnv e;
  e.c.start();
  e.c.createProject(app::SampleKind::Empty, "案件", "");
  const std::string a = e.c.session()->addIssue("", "", "A");
  e.c.afterEdit();
  e.fs.put("ext/メモ.txt", "hello");
  REQUIRE(e.c.attachFiles(a, {"ext/メモ.txt"}).added.size() == 1);
  CHECK_FALSE(e.c.moveAttachment(a, 0, "999"));
  CHECK_FALSE(e.lastToast().empty());
  CHECK_FALSE(e.c.copyAttachment(a, 0, "999"));
  CHECK_FALSE(e.fs.exists("data/案件/添付/999/メモ.txt"));
  CHECK_FALSE(e.c.moveAttachment(a, 0, a));
  CHECK_FALSE(e.c.copyAttachment(a, 0, a));
  CHECK_FALSE(e.c.moveAttachment("999", 0, a));
  CHECK_FALSE(e.c.moveAttachment(a, 7, a));
  CHECK(e.c.attachmentCount(a) == 1);
  CHECK(e.fs.exists("data/案件/添付/" + a + "/メモ.txt"));
  CHECK_FALSE(e.c.reorderAttachment(a, 0, 5));
  e.fs.put("data/古い.csv", "Issue Id,Summary\n1,a\n2,b\n");
  e.c.syncFolder(true);
  e.c.openProject(e.c.projects().front().id);
  REQUIRE(e.c.session());
  CHECK_FALSE(e.c.moveAttachment("1", 0, "2"));
  CHECK_FALSE(e.c.copyAttachment("1", 0, "2"));
}

TEST_CASE("controller: コピーが合計の上限を超えるときは断る") {
  CEnv e;
  e.c.start();
  e.c.createProject(app::SampleKind::Empty, "案件", "");
  const std::string a = e.c.session()->addIssue("", "", "A");
  const std::string b = e.c.session()->addIssue("", "", "B");
  e.c.afterEdit();
  e.fs.put("ext/大.bin", std::string(1024, 'x'));
  REQUIRE(e.c.attachFiles(a, {"ext/大.bin"}).added.size() == 1);
  // 合計の上限に近い状態にする（メタ情報だけの別の添付で、合計をかさ上げする）
  e.c.session()->attachments()["other"].push_back({"pad.bin", kMaxAttachProjectBytes - 1024, "", "", ""});
  CHECK_FALSE(e.c.copyAttachment(a, 0, b));
  CHECK(e.c.attachmentCount(b) == 0);
  CHECK(e.lastToast().find("合計") != std::string::npos);
  CHECK(e.c.moveAttachment(a, 0, b));  // 移動は合計が増えないので通る
}

TEST_CASE("controller: 同じ課題の中の並べ替え") {
  CEnv e;
  e.c.start();
  e.c.createProject(app::SampleKind::Empty, "案件", "");
  const std::string a = e.c.session()->addIssue("", "", "A");
  e.c.afterEdit();
  e.fs.put("ext/1.txt", "1");
  e.fs.put("ext/2.txt", "2");
  REQUIRE(e.c.attachFiles(a, {"ext/1.txt", "ext/2.txt"}).added.size() == 2);
  REQUIRE(e.c.reorderAttachment(a, 0, 1));
  CHECK(e.c.attachmentsOf(a)[0].name == "2.txt");
  e.step(600);
  CEnv e2;
  e2.fs = e.fs;
  e2.c.start();
  REQUIRE(e2.c.attachmentCount(a) == 2);
  CHECK(e2.c.attachmentsOf(a)[0].name == "2.txt");  // 並びも保存される
}

TEST_CASE("controller: クリップボードの画像は 貼り付け-<日時>.bmp で付く。大きすぎる・壊れた画像は断る") {
  CEnv e;
  e.c.start();
  e.c.createProject(app::SampleKind::Empty, "案件", "");
  const std::string a = e.c.session()->addIssue("", "", "A");
  e.c.afterEdit();
  const std::string bmp = *bmpFromDib(makeDib(40, 4, 3, 24, 0, 0, "", 36));
  REQUIRE(e.c.attachClipboardImage(a, bmp));
  REQUIRE(e.c.attachmentCount(a) == 1);
  CHECK(e.c.attachmentsOf(a)[0].name == "貼り付け-20261021-093000.bmp");
  CHECK(e.fs.content("data/案件/添付/" + a + "/貼り付け-20261021-093000.bmp") == bmp);
  REQUIRE(e.c.attachClipboardImage(a, bmp));  // 同じ秒でも連番
  CHECK(e.c.attachmentsOf(a)[1].name == "貼り付け-20261021-093000-2.bmp");
  CHECK_FALSE(e.c.attachClipboardImage(a, ""));
  CHECK(e.c.attachmentCount(a) == 2);
  CHECK_FALSE(e.c.attachClipboardImage("999", bmp));  // 存在しない課題
}
