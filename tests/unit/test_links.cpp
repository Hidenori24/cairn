// 課題の関連リンク（URL・社内ファイル・フォルダ）。Issue #60
#include <doctest/doctest.h>

#include <chrono>
#include <string>

#include "app_controller.hpp"
#include "roadmap/attachments.hpp"
#include "roadmap/links.hpp"
#include "roadmap/model.hpp"
#include "roadmap/project_io.hpp"
#include "roadmap/text.hpp"

using namespace roadmap;

namespace {
FixedClock clk(LocalTime{2026, 10, 22, 9, 30, 0}, 1792400000000);

LinkClass K(const std::string& s) { return classifyLink(s); }
Link mk(const std::string& title, const std::string& target, std::int64_t at = 1) {
  Link l;
  l.title = title;
  l.target = target;
  l.addedAt = at;
  return l;
}
}  // namespace

// ---- 種類の判定 ----

TEST_CASE("classifyLink: Web は http と https だけ（大文字のスキームも。スキームは小文字にそろえる）") {
  CHECK(K("https://example.com/a/b").kind == LinkKind::Web);
  CHECK(K("http://example.com").kind == LinkKind::Web);
  CHECK(K("HTTP://Example.com/Path").kind == LinkKind::Web);
  CHECK(K("HTTP://Example.com/Path").target == "http://Example.com/Path");  // ホストとパスは変えない
  CHECK(K("HtTpS://example.com/").target == "https://example.com/");
  CHECK(K("https://example.com:8443/x?y=1#z").kind == LinkKind::Web);
  CHECK(K("https://日本語.example/パス").kind == LinkKind::Web);
  CHECK(K("https://[::1]:8080/").kind == LinkKind::Web);
  CHECK(K("https://example.com/a b").target == "https://example.com/a%20b");  // URL の空白は %20
}

TEST_CASE("classifyLink: ドライブ付き・UNC・file:// は File、末尾が \\ か / ならフォルダ") {
  CHECK(K("C:\\Users\\a\\spec.xlsx").kind == LinkKind::File);
  CHECK(K("c:/work/spec.xlsx").kind == LinkKind::File);
  CHECK(K("S:\\案件\\設計書.docx").kind == LinkKind::File);
  CHECK(K("C:\\Users\\a\\").kind == LinkKind::Folder);
  CHECK(K("C:\\").kind == LinkKind::Folder);
  CHECK(K("D:/data/").kind == LinkKind::Folder);
  CHECK(K("S:\\案件\\共有フォルダ\\").kind == LinkKind::Folder);
  CHECK(K("\\\\server\\share\\docs\\a.pdf").kind == LinkKind::File);
  CHECK(K("\\\\server\\share\\docs\\").kind == LinkKind::Folder);
  CHECK(K("\\\\server\\share").kind == LinkKind::Folder);  // 共有のルートはフォルダ
  CHECK(K("\\\\server\\share\\").kind == LinkKind::Folder);
  CHECK(K("\\\\ファイルサーバー\\共有\\設計\\仕様書.pdf").kind == LinkKind::File);
  CHECK(K("file:///C:/work/a.txt").kind == LinkKind::File);
  CHECK(K("file:///C:/work/").kind == LinkKind::Folder);
  CHECK(K("FILE:///C:/work/a.txt").target == "file:///C:/work/a.txt");
  CHECK(K("file://server/share/docs/a.pdf").kind == LinkKind::File);
  CHECK(K("file://server/share/docs/").kind == LinkKind::Folder);
  CHECK(K("file://localhost/C:/work/a.txt").kind == LinkKind::File);
  CHECK(K("file:////server/share/a.pdf").kind == LinkKind::File);
  CHECK(K("file:///C:/%E6%97%A5%E6%9C%AC%E8%AA%9E.txt").kind == LinkKind::File);
}

TEST_CASE("classifyLink: 許可リスト以外のスキームは Rejected（理由つき。入力の中身は理由に入れない）") {
  for (const char* s : {"javascript:alert(1)", "JavaScript:alert(1)", "ms-msdt:/id PCWDiagnostic", "data:text/html,<script>1</script>",
                        "vbscript:msgbox(1)", "ftp://example.com/a", "mailto:a@example.com", "ms-excel:ofe|u|https://x/a.xlsx",
                        "search-ms:query=x", "tel:0312345678", "ssh://host", "microsoft-edge:https://x", "about:blank", "blob:https://x/y"}) {
    const LinkClass c = K(s);
    CAPTURE(s);
    CHECK(c.kind == LinkKind::Rejected);
    CHECK_FALSE(c.ok());
    CHECK_FALSE(c.reason.empty());
    CHECK(c.reason.find("alert") == std::string::npos);
    CHECK(c.reason.find("example.com") == std::string::npos);
  }
}

TEST_CASE("classifyLink: 難読化したスキームも許可リストで落ちる（制御文字・空白を入れても Web 扱いにならない）") {
  CHECK(K("java\tscript:alert(1)").kind == LinkKind::Rejected);   // タブを取るとスキームになる
  CHECK(K("java\nscript:alert(1)").kind == LinkKind::Rejected);
  CHECK(K("  \"javascript:alert(1)\"  ").kind == LinkKind::Rejected);
  CHECK(K("java\xE2\x80\x8Bscript:alert(1)").kind == LinkKind::Rejected);  // ゼロ幅スペース
  CHECK(K("%6Aavascript:alert(1)").kind == LinkKind::Unknown);            // 許可リスト以外は開かない
  CHECK(K("jav&#x61;script:alert(1)").kind == LinkKind::Unknown);
  CHECK_FALSE(K("javascript:alert(1)").ok());
  CHECK_FALSE(K("%6Aavascript:alert(1)").ok());
}

TEST_CASE("classifyLink: 判定できないものは Unknown（相対パス・ふつうの文章・空）") {
  for (const char* s : {"", "   ", "\"\"", "docs/readme.md", "..\\a.txt", "www.example.com", "メモ", "\\a.txt", "C:a.txt", "C:", "http:/example.com",
                        "https:example.com", "https://", "http:///x", "file:///etc/passwd", "file:C:\\a", "\\\\server", "\\\\server\\"}) {
    const LinkClass c = K(s);
    CAPTURE(s);
    CHECK(c.kind == LinkKind::Unknown);
    CHECK_FALSE(c.ok());
    CHECK_FALSE(c.reason.empty());
  }
}

TEST_CASE("classifyLink: 使えない文字・デバイスのパス・ID入りの URL は Rejected") {
  CHECK(K("\\\\?\\C:\\a.txt").kind == LinkKind::Rejected);
  CHECK(K("\\\\.\\PhysicalDrive0").kind == LinkKind::Rejected);
  CHECK(K("C:\\a|b.txt").kind == LinkKind::Rejected);
  CHECK(K("C:\\a*.txt").kind == LinkKind::Rejected);
  CHECK(K("C:\\a?.txt").kind == LinkKind::Rejected);
  CHECK(K("C:\\a.txt:stream").kind == LinkKind::Rejected);  // 代替データストリーム
  CHECK(K("\\\\server\\sh<are\\a").kind == LinkKind::Rejected);
  CHECK(K("https://user:pass@example.com/").kind == LinkKind::Rejected);
  CHECK(K("https://good.example.com@evil.example.com/").kind == LinkKind::Rejected);
  CHECK(K("https://evil.example.com\\@good.example.com/").kind == LinkKind::Rejected);
  CHECK(K("https://exa mple.com/").kind == LinkKind::Rejected);
  CHECK(K("https://example.com/\"onmouseover=\"x").kind == LinkKind::Rejected);
  CHECK(K("https://example.com/<script>").kind == LinkKind::Rejected);
  CHECK(K("\xE2\x80\xAE" "https://example.com/").kind == LinkKind::Rejected);  // 右から左への上書き
  CHECK(K(std::string("https://example.com/\xFF\xFE")).kind == LinkKind::Rejected);  // 不正な UTF-8
}

TEST_CASE("classifyLink: 空白・引用符・制御文字・改行を取り除いて正規化する") {
  CHECK(K("  https://example.com/a  ").target == "https://example.com/a");
  CHECK(K("\"C:\\Users\\a\\spec.xlsx\"").target == "C:\\Users\\a\\spec.xlsx");  // エクスプローラーの「パスのコピー」
  CHECK(K("\"C:\\Users\\a\\spec.xlsx\"").kind == LinkKind::File);
  CHECK(K(" \" \\\\server\\share\\a.pdf \" ").target == "\\\\server\\share\\a.pdf");
  CHECK(K("\xE2\x80\x9Chttps://example.com/\xE2\x80\x9D").target == "https://example.com/");  // “…”
  CHECK(K("<https://example.com/mail>").target == "https://example.com/mail");
  CHECK(K("https://example.com/a\r\n").target == "https://example.com/a");
  CHECK(K("https://exam\nple.com/\tx\x01\x7F").target == "https://example.com/x");
  CHECK(K("\xE3\x80\x80https://example.com/\xE3\x80\x80").target == "https://example.com/");  // 全角の空白
  CHECK(K("https://example.com/\xC2\x85\xC2\x9F").target == "https://example.com/");           // C1 制御文字
}

TEST_CASE("classifyLink: 長さの上限（Web は 2,048 バイト、パスは 1,024 バイト）") {
  const std::string url = "https://example.com/";
  CHECK(K(url + std::string(kMaxLinkUrlBytes - url.size(), 'a')).kind == LinkKind::Web);
  const LinkClass over = K(url + std::string(kMaxLinkUrlBytes - url.size() + 1, 'a'));
  CHECK(over.kind == LinkKind::Rejected);
  CHECK_FALSE(over.reason.empty());
  const std::string drive = "C:\\";
  CHECK(K(drive + std::string(kMaxLinkPathBytes - drive.size(), 'a')).kind == LinkKind::File);
  CHECK(K(drive + std::string(kMaxLinkPathBytes - drive.size() + 1, 'a')).kind == LinkKind::Rejected);
  const std::string unc = "\\\\server\\share\\";
  CHECK(K(unc + std::string(kMaxLinkPathBytes - unc.size(), 'a')).kind == LinkKind::File);
  CHECK(K(unc + std::string(kMaxLinkPathBytes - unc.size() + 1, 'a')).kind == LinkKind::Rejected);
  // 空白を %20 にして伸びた分も、上限の中で数える
  CHECK(K(url + std::string(kMaxLinkUrlBytes - url.size() - 1, ' ') + "a").kind == LinkKind::Rejected);
  // 日本語の長いパス（1 文字 3 バイト）。バイト数で数える
  std::string jp = "S:\\";
  while (jp.size() + 3 <= kMaxLinkPathBytes) jp += "案";
  CHECK(K(jp).kind == LinkKind::File);
  CHECK(K(jp + "案").kind == LinkKind::Rejected);
}

// ---- 表示名 ----

TEST_CASE("defaultTitle: Web はホスト + 末尾の区画、ファイルはファイル名、フォルダは最後のフォルダ名") {
  CHECK(defaultTitle("https://example.com/") == "example.com");
  CHECK(defaultTitle("https://example.com") == "example.com");
  CHECK(defaultTitle("https://example.com/wiki") == "example.com/wiki");
  CHECK(defaultTitle("https://github.com/Hidenori24/task_management_tool/issues/60") == "github.com/issues/60");
  CHECK(defaultTitle("https://example.com/docs/design/spec.html?x=1#top") == "example.com/design/spec.html");
  CHECK(defaultTitle("https://example.com/a/b/") == "example.com/a/b");
  CHECK(defaultTitle("https://example.com/%E8%A8%AD%E8%A8%88/%E4%BB%95%E6%A7%98") == "example.com/設計/仕様");
  CHECK(defaultTitle("https://example.com/%FF%FE") == "example.com/%FF%FE");  // UTF-8 でなければ戻さない
  CHECK(defaultTitle("C:\\Users\\a\\spec.xlsx") == "spec.xlsx");
  CHECK(defaultTitle("\\\\server\\share\\設計\\仕様書.pdf") == "仕様書.pdf");
  CHECK(defaultTitle("file:///C:/work/%E4%BB%95%E6%A7%98.txt") == "仕様.txt");
  CHECK(defaultTitle("C:\\Users\\a\\docs\\") == "docs");
  CHECK(defaultTitle("S:\\案件\\共有フォルダ\\") == "共有フォルダ");
  CHECK(defaultTitle("\\\\server\\share") == "share");
  CHECK(defaultTitle("\\\\server\\share\\x\\") == "x");
  CHECK(defaultTitle("file:///C:/work/") == "work");
  CHECK(defaultTitle("C:\\") == "C:");
}

TEST_CASE("defaultTitle: 長すぎる名前は切り詰める（文字の途中では切らない）") {
  std::string path = "C:\\";
  for (int i = 0; i < 100; ++i) path += "名";  // 300 バイト
  const std::string t = defaultTitle(path);
  CHECK(t.size() <= kMaxLinkTitleBytes);
  CHECK(!t.empty());
  CHECK(text::isValidUtf8(t));
}

TEST_CASE("sanitizeLinkTitle: 制御文字を除き、前後の空白を取り、上限で切る") {
  CHECK(sanitizeLinkTitle("  設計書\n（最新）\t ") == "設計書（最新）");
  CHECK(sanitizeLinkTitle("") == "");
  CHECK(sanitizeLinkTitle(std::string("a\xFF" "b")) == "a\xEF\xBF\xBD" "b");
  const std::string t = sanitizeLinkTitle(std::string(500, 'a'));
  CHECK(t.size() == kMaxLinkTitleBytes);
  std::string jp;
  for (int i = 0; i < 100; ++i) jp += "あ";
  const std::string t2 = sanitizeLinkTitle(jp);
  CHECK(t2.size() <= kMaxLinkTitleBytes);
  CHECK(text::isValidUtf8(t2));
}

// ---- 実行ファイル・親フォルダ・重複の判定 ----

TEST_CASE("isRiskyTarget: 添付と同じ判定（isRiskyFile）をファイルの場所に使う") {
  for (const char* s : {"C:\\tools\\a.exe", "C:\\tools\\A.EXE", "C:\\a.bat", "C:\\a.cmd", "C:\\a.com", "C:\\a.ps1", "C:\\a.vbs", "C:\\a.js", "C:\\a.jse",
                        "C:\\a.wsf", "C:\\a.msi", "C:\\a.scr", "C:\\a.lnk", "C:\\a.hta", "C:\\a.jar", "C:\\a.reg", "C:\\a.dll", "C:\\a.url",
                        "\\\\server\\share\\setup.exe", "file:///C:/a.exe", "S:/tools/run.Bat"}) {
    CAPTURE(s);
    CHECK(isRiskyTarget(s));
  }
  for (const char* s : {"C:\\a.pdf", "C:\\メモ.txt", "C:\\a.xlsx", "C:\\dir.exe\\", "\\\\server\\share\\", "C:\\exe", "C:\\a.exe.txt",
                        "https://example.com/setup.exe", "https://example.com/", "file:///C:/dir.exe/", "", "javascript:alert(1)"}) {
    CAPTURE(s);
    CHECK_FALSE(isRiskyTarget(s));
  }
  // 添付の判定と食い違わない（同じ拡張子の表）
  for (const char* n : {"a.exe", "a.bat", "a.ps1", "a.lnk", "a.pdf", "a.txt", "a.url", "a.dll"})
    CHECK(isRiskyTarget(std::string("C:\\x\\") + n) == isRiskyFile(n));
}

TEST_CASE("isRiskyTarget: すり抜けを許さない（%XX・末尾の空白と「.」・# 以降）") {
  CHECK(isRiskyTarget("file:///C:/evil%2Eexe"));
  CHECK(isRiskyTarget("file:///C:/evil.ex%65"));
  CHECK(isRiskyTarget("file:///C:/dir%5Cevil.exe"));
  CHECK(isRiskyTarget("C:\\evil.exe."));     // Windows は末尾の「.」を無視して evil.exe を指す
  CHECK(isRiskyTarget("C:\\evil.exe ."));
  CHECK(isRiskyTarget("file:///C:/evil.exe%20"));
  CHECK(isRiskyTarget("file:///C:/evil.exe#.txt"));
  CHECK_FALSE(isRiskyTarget("file:///C:/readme.txt#.exe"));  // # 以降は断片（ファイル名ではない）
  CHECK_FALSE(isRiskyTarget("file:///C:/readme.txt#top"));
}

TEST_CASE("linkFolderOf: ファイルの場所の親フォルダ") {
  CHECK(linkFolderOf("C:\\Users\\a\\spec.xlsx") == "C:\\Users\\a\\");
  CHECK(linkFolderOf("C:\\spec.xlsx") == "C:\\");
  CHECK(linkFolderOf("D:/data/a.txt") == "D:/data/");
  CHECK(linkFolderOf("\\\\server\\share\\docs\\a.pdf") == "\\\\server\\share\\docs\\");
  CHECK(linkFolderOf("\\\\server\\share\\a.pdf") == "\\\\server\\share\\");
  CHECK(linkFolderOf("file:///C:/work/a.txt") == "C:\\work\\");  // file:// は開ける形（ふつうのパス）に直して返す
  CHECK(linkFolderOf("file://server/share/a.txt") == "\\\\server\\share\\");
  CHECK(linkFolderOf("C:\\Users\\a\\") == "");  // フォルダに親は出さない（フォルダ自体を開く）
  CHECK(linkFolderOf("https://example.com/a/b") == "");
  CHECK(linkFolderOf("javascript:alert(1)") == "");
  for (const char* s : {"C:\\Users\\a\\spec.xlsx", "\\\\server\\share\\docs\\a.pdf", "file:///C:/work/a.txt"})
    CHECK(classifyLink(linkFolderOf(s)).kind == LinkKind::Folder);
}

TEST_CASE("file:// の解析は、判定・警告・親フォルダ・表示名で同じ（# の断片と %XX の区切りで迷わせない）") {
  // 断片（#…）の中のスラッシュはディレクトリの区切りではない。実行ファイルをフォルダと見せかけられない
  CHECK(K("file:///C:/tools/run.exe#docs/").kind == LinkKind::File);
  CHECK(K("file:///C:/tools/run.exe#docs/page").kind == LinkKind::File);
  CHECK(isRiskyTarget("file:///C:/tools/run.exe#docs/"));
  CHECK(isRiskyTarget("file:///C:/tools/run.exe#docs/page"));
  CHECK(isRiskyTarget("file://server/share/run.exe#a/b"));
  CHECK(linkFolderOf("file:///C:/tools/run.exe#docs/page") == "C:\\tools\\");  // 実行ファイルを指す URL を返さない
  CHECK(linkFolderOf("file://server/share/dir/run.exe#a/b/") == "\\\\server\\share\\dir\\");
  CHECK(defaultTitle("file:///C:/tools/run.exe#docs/page") == "run.exe");
  CHECK(K("file:///C:/tools/#frag/x").kind == LinkKind::Folder);
  CHECK(K("file:///C:/x?y=1").kind == LinkKind::Rejected);  // クエリは受けない（%3F ならファイル名に使えない文字として断る）
  // %2F・%5C は区切りとして数える（デコードしてから成分に分ける）
  CHECK(K("file://server/share%2Frun.exe").kind == LinkKind::File);
  CHECK(isRiskyTarget("file://server/share%2Frun.exe"));
  CHECK(isRiskyTarget("file:///C:/tools%2Frun.exe"));
  CHECK(isRiskyTarget("file:///C:/tools%5Crun.exe"));
  CHECK(linkFolderOf("file://server/share%2Frun.exe") == "\\\\server\\share\\");
  CHECK(defaultTitle("file://server/share%2Frun.exe") == "run.exe");
  CHECK(K("file://server/share%2F").kind == LinkKind::Folder);
  CHECK(K("file://server%2Fshare/a.txt").kind == LinkKind::File);
  CHECK(K("file:///C:/report%3F.txt").kind == LinkKind::Rejected);
  // 「場所を開く」が返す場所は、どれも Folder と判定でき、警告の対象ではない
  for (const char* s : {"file:///C:/tools/run.exe#docs/page", "file://server/share%2Frun.exe", "file:///C:/a%2Fb/c.txt"}) {
    CAPTURE(s);
    const std::string dir = linkFolderOf(s);
    CHECK(K(dir).kind == LinkKind::Folder);
    CHECK_FALSE(isRiskyTarget(dir));
  }
}

TEST_CASE("生の Windows パス（ドライブ・UNC）の % はそのままのファイル名。デコードしない") {
  CHECK(K("C:\\archive\\report%3F.txt").kind == LinkKind::File);
  CHECK(K("C:\\archive\\report%3F.txt").target == "C:\\archive\\report%3F.txt");
  CHECK(K("\\\\server\\share\\report%3F.txt").kind == LinkKind::File);
  CHECK(K("\\\\server\\share\\100%25\\").kind == LinkKind::Folder);
  CHECK(K("C:\\a\\x%2Fy.txt").kind == LinkKind::File);
  CHECK(K("C:\\a\\x%3Ay.txt").kind == LinkKind::File);   // %3A もそのまま
  CHECK(K("C:\\a\\x:y.txt").kind == LinkKind::Rejected);   // 本物の「:」は断る
  CHECK(K("C:\\a\\x?y.txt").kind == LinkKind::Rejected);
  CHECK(defaultTitle("C:\\archive\\report%3F.txt") == "report%3F.txt");
  CHECK(defaultTitle("\\\\server\\share\\a%20b.txt") == "a%20b.txt");
  // 名前の %2E は「.」ではない（拡張子にならない）
  CHECK_FALSE(isRiskyTarget("C:\\a\\evil%2Eexe"));
  CHECK(isRiskyTarget("C:\\a\\evil.exe"));
  CHECK(linkFolderOf("C:\\archive\\report%3F.txt") == "C:\\archive\\");
}

TEST_CASE("sameLinkTarget: ファイルとフォルダは大文字小文字と区切りを無視、Web は完全一致") {
  CHECK(sameLinkTarget("C:\\Work\\A.txt", "c:/work/a.TXT"));
  CHECK(sameLinkTarget("\\\\Server\\Share\\", "\\\\server\\share\\"));
  CHECK_FALSE(sameLinkTarget("C:\\work\\a.txt", "C:\\work\\b.txt"));
  CHECK(sameLinkTarget("https://example.com/A", "https://example.com/A"));
  CHECK_FALSE(sameLinkTarget("https://example.com/A", "https://example.com/a"));  // URL のパスは大文字小文字を区別する
  // アクセント付きの文字・全角英字の大文字小文字も無視する（ファイル・フォルダ）
  CHECK(sameLinkTarget("C:\\\xC3\x89t\xC3\xA9.txt", "c:/\xC3\xA9t\xC3\xA9.TXT"));  // C:\Été.txt と c:/été.txt
  CHECK(sameLinkTarget("\\\\server\\share\\\xC3\x80\\", "\\\\SERVER\\share\\\xC3\xA0\\"));
  CHECK_FALSE(sameLinkTarget("https://example.com/\xC3\x89", "https://example.com/\xC3\xA9"));  // Web は今のまま
  CHECK_FALSE(sameLinkTarget("https://example.com/a", "C:\\a"));
}

// ---- 追加・編集・削除・並べ替え・移動・コピー ----

TEST_CASE("linkAdd: 表示名・時刻・正規化。表示名を省略すると defaultTitle") {
  LinkMap m;
  auto r = linkAdd(m, clk, "10", "設計書", "  \"https://example.com/wiki/spec\"  ");
  REQUIRE(r.ok());
  CHECK(r->title == "設計書");
  CHECK(r->target == "https://example.com/wiki/spec");
  CHECK(r->addedAt == clk.epochMs());
  auto r2 = linkAdd(m, clk, "10", "", "C:\\Users\\a\\spec.xlsx");
  REQUIRE(r2.ok());
  CHECK(r2->title == "spec.xlsx");
  auto r3 = linkAdd(m, clk, "10", "  \n ", "\\\\server\\share\\docs\\");
  REQUIRE(r3.ok());
  CHECK(r3->title == "docs");
  REQUIRE(m["10"].size() == 3);
  CHECK(m["10"][0] == *r);  // 追加した順
  CHECK(linkCount(m, "10") == 3);
  CHECK(linkCount(m, "11") == 0);
  CHECK(linkTotal(m) == 3);
}

TEST_CASE("linkAdd: 開けない種類は理由つきで断り、何も変えない") {
  LinkMap m;
  for (const char* s : {"javascript:alert(1)", "ms-msdt:x", "data:text/html,x", "ftp://x/y", "", "メモ", "docs/a.md"}) {
    const auto r = linkAdd(m, clk, "10", "x", s);
    CAPTURE(s);
    CHECK_FALSE(r.ok());
    CHECK_FALSE(r.error().empty());
  }
  CHECK(m.empty());
}

TEST_CASE("linkAdd: 同じ場所の重複は追加しない（課題ごと。ファイルは大文字小文字を無視）") {
  LinkMap m;
  REQUIRE(linkAdd(m, clk, "10", "", "C:\\Work\\spec.xlsx").ok());
  const auto dup = linkAdd(m, clk, "10", "別名", "c:/work/SPEC.xlsx");
  CHECK_FALSE(dup.ok());
  CHECK(dup.error().find("すでに") != std::string::npos);
  CHECK(linkCount(m, "10") == 1);
  CHECK(linkAdd(m, clk, "11", "", "C:\\Work\\spec.xlsx").ok());  // 別の課題には付けてよい
}

TEST_CASE("linkAdd: 1 課題 50 件・全体 5,000 件まで") {
  LinkMap m;
  for (std::size_t i = 0; i < kMaxLinksPerIssue; ++i) REQUIRE(linkAdd(m, clk, "1", "", "https://example.com/" + std::to_string(i)).ok());
  const auto over = linkAdd(m, clk, "1", "", "https://example.com/over");
  CHECK_FALSE(over.ok());
  CHECK(over.error().find("50") != std::string::npos);
  CHECK(linkCount(m, "1") == kMaxLinksPerIssue);

  LinkMap big;
  for (std::size_t i = 0; i < kMaxLinksTotal; ++i)
    big["i" + std::to_string(i / kMaxLinksPerIssue)].push_back(mk("t", "https://example.com/" + std::to_string(i)));
  REQUIRE(linkTotal(big) == kMaxLinksTotal);
  const auto over2 = linkAdd(big, clk, "new", "", "https://example.com/new");
  CHECK_FALSE(over2.ok());
  CHECK(over2.error().find("5,000") != std::string::npos);
  CHECK(linkTotal(big) == kMaxLinksTotal);
}

TEST_CASE("linkAdd: 設定ファイルに保存して読み直せない Issue Id には付けない") {
  LinkMap m;
  const std::string tooLong(65, '1');
  for (const std::string& id : {std::string(), std::string("."), std::string(".."), std::string("a/b"), std::string("a\\b"), std::string("a:b"),
                                std::string(" 1"), std::string("1 "), tooLong}) {
    CAPTURE(id);
    CHECK_FALSE(linkAdd(m, clk, id, "", "https://example.com/").ok());
  }
  CHECK(m.empty());
  CHECK(linkAdd(m, clk, std::string(64, '1'), "", "https://example.com/").ok());
}

TEST_CASE("重複の判定: アクセント付きの文字の大文字小文字違いも重複（追加・編集・移動・コピー・読み込み）") {
  const std::string upper = "C:\\Dir\\\xC3\x89t\xC3\xA9.txt", lower = "c:/dir/\xC3\xA9t\xC3\xA9.TXT";
  LinkMap m;
  REQUIRE(linkAdd(m, clk, "1", "", upper).ok());
  CHECK_FALSE(linkAdd(m, clk, "1", "", lower).ok());
  REQUIRE(linkAdd(m, clk, "1", "", "C:\\other.txt").ok());
  CHECK_FALSE(linkEdit(m, "1", 1, "", lower).ok());
  REQUIRE(linkAdd(m, clk, "2", "", lower).ok());
  CHECK_FALSE(linkMove(m, "1", 0, "2").ok());
  CHECK_FALSE(linkCopy(m, clk, "1", 0, "2").ok());
  using J = nlohmann::ordered_json;
  J j = J::object();
  j["1"] = J::array();
  j["1"].push_back(J{{"title", "a"}, {"target", upper}, {"at", 1}});
  j["1"].push_back(J{{"title", "b"}, {"target", lower}, {"at", 2}});
  CHECK(linksFromJson(j).at("1").size() == 1);
}

TEST_CASE("linkEdit: 表示名と場所を直す（追加した時刻は変えない）。重複・開けない場所は断る") {
  LinkMap m;
  m["1"] = {mk("A", "https://example.com/a", 111), mk("B", "https://example.com/b", 222)};
  REQUIRE(linkEdit(m, "1", 0, "A改", "  https://example.com/a2 ").ok());
  CHECK(m["1"][0].title == "A改");
  CHECK(m["1"][0].target == "https://example.com/a2");
  CHECK(m["1"][0].addedAt == 111);
  REQUIRE(linkEdit(m, "1", 0, "", "C:\\x\\y.txt").ok());  // 表示名を空にすると defaultTitle
  CHECK(m["1"][0].title == "y.txt");
  CHECK_FALSE(linkEdit(m, "1", 0, "A", "https://example.com/b").ok());      // 自分以外と重複
  CHECK_FALSE(linkEdit(m, "1", 0, "A", "javascript:alert(1)").ok());
  CHECK_FALSE(linkEdit(m, "1", 5, "A", "https://example.com/z").ok());      // 範囲外
  CHECK_FALSE(linkEdit(m, "9", 0, "A", "https://example.com/z").ok());
  CHECK(m["1"][0].target == "C:\\x\\y.txt");                                  // 断ったら変わらない
  CHECK(linkEdit(m, "1", 0, "同じ場所のまま", "C:\\x\\y.txt").ok());          // 自分自身と同じ場所は重複ではない
}

TEST_CASE("linkEdit: 読み込んだ未知のキー（extra）は残る") {
  LinkMap m;
  Link l = mk("A", "https://example.com/a");
  l.extra = nlohmann::ordered_json{{"gh", {{"state", "open"}}}};
  m["1"] = {l};
  REQUIRE(linkEdit(m, "1", 0, "A2", "https://example.com/a").ok());
  CHECK(m["1"][0].extra.contains("gh"));
}

TEST_CASE("linkRemove: 取り除く。空になった課題の項目は残さない") {
  LinkMap m;
  m["1"] = {mk("A", "https://example.com/a"), mk("B", "https://example.com/b")};
  REQUIRE(linkRemove(m, "1", 0).ok());
  REQUIRE(m["1"].size() == 1);
  CHECK(m["1"][0].title == "B");
  REQUIRE(linkRemove(m, "1", 0).ok());
  CHECK(m.count("1") == 0);
  CHECK_FALSE(linkRemove(m, "1", 0).ok());
  CHECK_FALSE(linkRemove(m, "2", 0).ok());
}

TEST_CASE("linkReorder: 取り除いたあとの添字 to へ入れる（添付と同じ）") {
  LinkMap m;
  m["1"] = {mk("A", "https://e/a"), mk("B", "https://e/b"), mk("C", "https://e/c"), mk("D", "https://e/d")};
  const auto order = [&] {
    std::string s;
    for (const auto& l : m["1"]) s += l.title;
    return s;
  };
  REQUIRE(linkReorder(m, "1", 0, 2).ok());
  CHECK(order() == "BCAD");
  REQUIRE(linkReorder(m, "1", 3, 0).ok());
  CHECK(order() == "DBCA");
  REQUIRE(linkReorder(m, "1", 1, 1).ok());
  CHECK(order() == "DBCA");
  REQUIRE(linkReorder(m, "1", 0, 3).ok());
  CHECK(order() == "BCAD");
  CHECK_FALSE(linkReorder(m, "1", 4, 0).ok());
  CHECK_FALSE(linkReorder(m, "1", 0, 4).ok());
  CHECK_FALSE(linkReorder(m, "2", 0, 0).ok());
  CHECK(order() == "BCAD");
}

TEST_CASE("linkMove: 別の課題の末尾へ（時刻・未知のキーは引き継ぐ）。空になった課題の項目は残さない") {
  LinkMap m;
  m["1"] = {mk("A", "https://e/a", 111)};
  m["1"][0].extra = nlohmann::ordered_json{{"k", 1}};
  m["2"] = {mk("X", "https://e/x", 5)};
  const auto r = linkMove(m, "1", 0, "2");
  REQUIRE(r.ok());
  CHECK(r->title == "A");
  CHECK(m.count("1") == 0);
  REQUIRE(m["2"].size() == 2);
  CHECK(m["2"][1] == *r);
  CHECK(m["2"][1].addedAt == 111);
  CHECK(m["2"][1].extra.contains("k"));
  CHECK(linkTotal(m) == 2);
}

TEST_CASE("linkMove: 断る場合は何も変えない（同じ課題・先に同じ場所・先がいっぱい・範囲外・Issue Id）") {
  LinkMap m;
  m["1"] = {mk("A", "https://e/a"), mk("B", "https://e/b")};
  m["2"] = {mk("A2", "https://e/a")};
  const LinkMap before = m;
  CHECK_FALSE(linkMove(m, "1", 0, "1").ok());
  CHECK_FALSE(linkMove(m, "1", 0, "2").ok());  // 先に同じ場所がある
  CHECK_FALSE(linkMove(m, "1", 5, "3").ok());
  CHECK_FALSE(linkMove(m, "9", 0, "3").ok());
  CHECK_FALSE(linkMove(m, "1", 1, "a/b").ok());
  CHECK_FALSE(linkMove(m, "1", 1, "").ok());
  CHECK(m == before);
  for (std::size_t i = 0; i < kMaxLinksPerIssue; ++i) m["full"].push_back(mk("t", "https://e/f" + std::to_string(i)));
  const LinkMap before2 = m;
  const auto r = linkMove(m, "1", 1, "full");
  CHECK_FALSE(r.ok());
  CHECK(r.error().find("50") != std::string::npos);
  CHECK(m == before2);
}

TEST_CASE("linkCopy: 元は残し、時刻は「いま」にする") {
  LinkMap m;
  m["1"] = {mk("A", "https://e/a", 111)};
  const auto r = linkCopy(m, clk, "1", 0, "2");
  REQUIRE(r.ok());
  REQUIRE(m["1"].size() == 1);
  REQUIRE(m["2"].size() == 1);
  CHECK(m["2"][0].title == "A");
  CHECK(m["2"][0].target == "https://e/a");
  CHECK(m["2"][0].addedAt == clk.epochMs());
  CHECK(m["1"][0].addedAt == 111);
  CHECK_FALSE(linkCopy(m, clk, "1", 0, "2").ok());  // 先に同じ場所
  CHECK_FALSE(linkCopy(m, clk, "1", 0, "1").ok());
  CHECK_FALSE(linkCopy(m, clk, "1", 3, "3").ok());
  CHECK(linkTotal(m) == 2);
  // 全体の上限
  LinkMap big;
  for (std::size_t i = 0; i < kMaxLinksTotal; ++i)
    big["i" + std::to_string(i / kMaxLinksPerIssue)].push_back(mk("t", "https://example.com/" + std::to_string(i)));
  CHECK_FALSE(linkCopy(big, clk, "i0", 0, "new").ok());
  CHECK(linkTotal(big) == kMaxLinksTotal);
}

// ---- 貼り付けて追加（1 行 1 リンク） ----

TEST_CASE("linkAddLines: 各行を 1 リンクとして追加する（空行は読み飛ばし、改行は \\n \\r\\n \\r）") {
  LinkMap m;
  const auto o = linkAddLines(m, clk, "10", "https://example.com/a\r\n\r\n  C:\\Users\\a\\spec.xlsx  \n\"\\\\server\\share\\docs\\\"\rhttps://example.com/b");
  CHECK(o.added.size() == 4);
  CHECK(o.rejected.empty());
  CHECK(o.skipped == 0);
  REQUIRE(m["10"].size() == 4);
  CHECK(m["10"][1].title == "spec.xlsx");
  CHECK(m["10"][2].target == "\\\\server\\share\\docs\\");
}

TEST_CASE("linkAddLines: 断った行は行番号と理由（中身は入れない）。ほかの行は続ける") {
  LinkMap m;
  REQUIRE(linkAdd(m, clk, "10", "", "https://example.com/dup").ok());
  const auto o = linkAddLines(m, clk, "10", "javascript:alert(1)\nhttps://example.com/ok\n\nhttps://example.com/dup\nメモ\n");
  CHECK(o.added.size() == 1);
  REQUIRE(o.rejected.size() == 3);
  CHECK(o.rejected[0].line == 1);
  CHECK(o.rejected[1].line == 4);  // 空行も数える
  CHECK(o.rejected[2].line == 5);
  for (const auto& r : o.rejected) {
    CHECK_FALSE(r.reason.empty());
    CHECK(r.reason.find("alert") == std::string::npos);
    CHECK(r.reason.find("example.com") == std::string::npos);
  }
  CHECK(linkCount(m, "10") == 2);
}

TEST_CASE("linkAddLines: 読むのは先頭の 20 行まで（超えた分は件数だけ）") {
  std::string text;
  for (int i = 0; i < 25; ++i) text += "https://example.com/" + std::to_string(i) + "\n";
  LinkMap m;
  const auto o = linkAddLines(m, clk, "10", text);
  CHECK(o.added.size() == kMaxPasteLinkLines);
  CHECK(o.skipped == 5);
  CHECK(linkCount(m, "10") == kMaxPasteLinkLines);
  // 空行は数えない
  std::string sparse;
  for (int i = 0; i < 20; ++i) sparse += "\n\nhttps://example.com/s" + std::to_string(i);
  LinkMap m2;
  const auto o2 = linkAddLines(m2, clk, "10", sparse);
  CHECK(o2.added.size() == 20);
  CHECK(o2.skipped == 0);
}

TEST_CASE("linkAddLines: 1 課題の上限（50 件）に達したら、残りの行は理由つきで断る") {
  LinkMap m;
  for (std::size_t i = 0; i < kMaxLinksPerIssue - 2; ++i) REQUIRE(linkAdd(m, clk, "10", "", "https://example.com/p" + std::to_string(i)).ok());
  const auto o = linkAddLines(m, clk, "10", "https://e/1\nhttps://e/2\nhttps://e/3");
  CHECK(o.added.size() == 2);
  REQUIRE(o.rejected.size() == 1);
  CHECK(o.rejected[0].line == 3);
  CHECK(linkCount(m, "10") == kMaxLinksPerIssue);
}

// ---- 保存（project.settings.json の "links"） ----

TEST_CASE("linksToJson / linksFromJson: 往復で同じ値に戻る（日本語・長いパス・UNC）") {
  LinkMap m;
  m["10"] = {mk("設計書（最新）", "https://example.com/wiki/%E8%A8%AD%E8%A8%88", 1792400000123),
             mk("共有フォルダ", "\\\\ファイルサーバー\\共有\\案件\\", 2),
             mk("仕様書", "S:\\案件\\設計\\仕様書_v3.xlsx", 3)};
  m["2"] = {mk("x", "file:///C:/work/a.txt", 0)};
  const auto j = linksToJson(m);
  REQUIRE(j.is_object());
  CHECK(j["10"][0]["title"] == "設計書（最新）");
  CHECK(j["10"][0]["at"] == 1792400000123);
  CHECK(j["10"][1]["target"] == "\\\\ファイルサーバー\\共有\\案件\\");
  const LinkMap back = linksFromJson(nlohmann::ordered_json::parse(j.dump()));  // 文字列を経由する
  CHECK(back == m);
}

TEST_CASE("linksToJson: リンクのない課題は書かない。キーは title, target, at の順") {
  LinkMap m;
  m["1"] = {};
  m["2"] = {mk("A", "https://e/a", 5)};
  const auto j = linksToJson(m);
  CHECK_FALSE(j.contains("1"));
  REQUIRE(j.contains("2"));
  CHECK(j["2"][0].dump() == "{\"title\":\"A\",\"target\":\"https://e/a\",\"at\":5}");
  CHECK(linksToJson(LinkMap{}).is_object());
}

TEST_CASE("linksFromJson: 壊れた値は読み飛ばす（型・Issue Id・開けない場所・重複・長さ）") {
  using J = nlohmann::ordered_json;
  const J j = J::parse(R"JS({
    "ok": [{"title":"A","target":"https://example.com/a","at":7}],
    "../evil": [{"title":"x","target":"https://example.com/x","at":1}],
    "a/b": [{"title":"x","target":"https://example.com/x","at":1}],
    "notarray": {"title":"x"},
    "mixed": [
      1, "s", null, [],
      {"title":"no target"},
      {"title":"num target","target":5},
      {"title":"js","target":"javascript:alert(1)","at":1},
      {"title":"ms","target":"ms-msdt:x","at":1},
      {"title":"rel","target":"docs/a.md","at":1},
      {"title":"good","target":"C:\\w\\a.txt","at":9},
      {"title":"dup","target":"c:/W/A.TXT","at":9},
      {"title":"good2","target":"https://example.com/g2","at":-5},
      {"title":"good3","target":"https://example.com/g3","at":1.5},
      {"title":"good4","target":"https://example.com/g4","at":"x"},
      {"title":123,"target":"https://example.com/g5","at":2},
      {"title":"","target":"C:\\w\\no_title.txt","at":2}
    ]
  })JS");
  const LinkMap m = linksFromJson(j);
  CHECK(m.count("../evil") == 0);
  CHECK(m.count("a/b") == 0);
  CHECK(m.count("notarray") == 0);
  REQUIRE(m.count("ok") == 1);
  CHECK(m.at("ok")[0] == mk("A", "https://example.com/a", 7));
  REQUIRE(m.count("mixed") == 1);
  const auto& v = m.at("mixed");
  REQUIRE(v.size() == 6);
  CHECK(v[0].title == "good");
  CHECK(v[0].addedAt == 9);
  CHECK(v[1].title == "good2");
  CHECK(v[1].addedAt == 0);  // 負の時刻は 0（項目は残す）
  CHECK(v[2].title == "good3");
  CHECK(v[2].addedAt == 0);
  CHECK(v[3].title == "good4");
  CHECK(v[3].addedAt == 0);
  CHECK(v[4].title == "example.com/g5");  // 表示名が壊れていれば defaultTitle
  CHECK(v[4].target == "https://example.com/g5");
  CHECK(v[5].title == "no_title.txt");
}

TEST_CASE("linksFromJson: 長すぎる場所は捨て、長すぎる表示名は切り詰める。場所は正規化し直す") {
  using J = nlohmann::ordered_json;
  J j = J::object();
  j["1"] = J::array();
  j["1"].push_back(J{{"title", "t"}, {"target", "https://example.com/" + std::string(3000, 'a')}, {"at", 1}});
  j["1"].push_back(J{{"title", std::string(500, 'b')}, {"target", "https://example.com/ok"}, {"at", 1}});
  j["1"].push_back(J{{"title", "q"}, {"target", "  \"C:\\w\\q.txt\"  "}, {"at", 1}});
  j["1"].push_back(J{{"title", "bad utf8"}, {"target", std::string("https://example.com/\xFF")}, {"at", 1}});
  const LinkMap m = linksFromJson(j);
  REQUIRE(m.count("1") == 1);
  REQUIRE(m.at("1").size() == 2);
  CHECK(m.at("1")[0].title.size() == kMaxLinkTitleBytes);
  CHECK(m.at("1")[1].target == "C:\\w\\q.txt");
}

TEST_CASE("linksFromJson: 件数の超過は捨てる（1 課題 50 件・全体 5,000 件）") {
  using J = nlohmann::ordered_json;
  J j = J::object();
  j["1"] = J::array();
  for (int i = 0; i < 80; ++i) j["1"].push_back(J{{"title", "t"}, {"target", "https://example.com/" + std::to_string(i)}, {"at", i}});
  LinkMap m = linksFromJson(j);
  CHECK(m.at("1").size() == kMaxLinksPerIssue);
  CHECK(m.at("1")[0].addedAt == 0);  // 先頭から残す
  CHECK(m.at("1")[49].addedAt == 49);

  J big = J::object();
  for (int c = 0; c < 120; ++c) {
    J arr = J::array();
    for (int i = 0; i < 50; ++i) arr.push_back(J{{"title", "t"}, {"target", "https://example.com/" + std::to_string(c) + "/" + std::to_string(i)}, {"at", 1}});
    big["c" + std::to_string(c)] = std::move(arr);
  }
  m = linksFromJson(big);
  CHECK(linkTotal(m) == kMaxLinksTotal);
}

TEST_CASE("linksFromJson: オブジェクトでない・空の値は空") {
  using J = nlohmann::ordered_json;
  for (const J& j : {J(), J(nullptr), J(5), J("s"), J::array(), J::object()}) CHECK(linksFromJson(j).empty());
}

TEST_CASE("linksFromJson: 項目の未知のキーは extra に残り、書き戻しても消えない") {
  using J = nlohmann::ordered_json;
  const J j = J::parse(R"JS({"1":[{"title":"A","target":"https://example.com/a","at":3,"gh":{"state":"open","n":12},"memo":"x"}]})JS");
  const LinkMap m = linksFromJson(j);
  REQUIRE(m.at("1").size() == 1);
  CHECK(m.at("1")[0].extra.size() == 2);
  const J back = linksToJson(m);
  CHECK(back["1"][0]["gh"]["n"] == 12);
  CHECK(back["1"][0]["memo"] == "x");
  CHECK(back == J::parse(back.dump()));
  // 既知のキーが extra に紛れても、書き戻しで title・target を上書きしない
  LinkMap m2;
  Link l = mk("A", "https://e/a", 3);
  l.extra = J{{"title", "evil"}, {"target", "javascript:1"}, {"at", 99}, {"k", 1}};
  m2["1"] = {l};
  const J out = linksToJson(m2);
  CHECK(out["1"][0]["title"] == "A");
  CHECK(out["1"][0]["target"] == "https://e/a");
  CHECK(out["1"][0]["at"] == 3);
}

TEST_CASE("性能: 5,000 件の分類・保存・読み込み（リリースビルドで 200ms 以内）") {
  LinkMap m;
  for (std::size_t i = 0; i < kMaxLinksTotal; ++i) {
    const std::string t = i % 3 == 0 ? "https://example.com/wiki/page" + std::to_string(i)
                          : i % 3 == 1 ? "\\\\server\\share\\dir\\file" + std::to_string(i) + ".xlsx"
                                       : "S:\\案件\\設計\\仕様書_" + std::to_string(i) + ".docx";
    m["i" + std::to_string(i / kMaxLinksPerIssue)].push_back(mk("リンク" + std::to_string(i), t, static_cast<std::int64_t>(i)));
  }
  const auto t0 = std::chrono::steady_clock::now();
  const std::string dumped = linksToJson(m).dump();
  const LinkMap back = linksFromJson(nlohmann::ordered_json::parse(dumped));
  std::size_t risky = 0;
  for (const auto& [id, list] : back)
    for (const auto& l : list) {
      risky += isRiskyTarget(l.target) ? 1 : 0;
      (void)defaultTitle(l.target);
    }
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
  CHECK(back == m);
  CHECK(risky == 0);
  MESSAGE("5000 件の保存・読み込み・判定: " << ms << "ms");
  CHECK(ms < 20000);  // 二乗に遅くなっていないこと（サニタイザ入りのデバッグビルドでも余裕を持たせる）
#if defined(NDEBUG) && !defined(ROADMAP_SANITIZED)
  CHECK(ms < 200);
#endif
}

// ---- 設定ファイル・共有 JSON との関係 ----

TEST_CASE("project.settings.json: links は往復し、未知のキーは壊さず、共有・バックアップの JSON には入れない") {
  ProjectRecord rec = newRecord("案件", "PRJ", "", "2026-10-19", clk);
  rec.settings.links["10"] = {mk("設計書", "https://example.com/wiki/spec", 5), mk("共有", "\\\\server\\share\\", 6)};
  rec.settings.unknown["future"] = nlohmann::ordered_json{{"a", 1}};
  const auto j = settingsToJson(rec);
  REQUIRE(j.contains("links"));
  CHECK(j["links"]["10"].size() == 2);
  CHECK(j.contains("future"));
  const ProjectRecord back = recordFromSettingsJson(nlohmann::ordered_json::parse(dumpJs(j)), "", "stem", clk);
  CHECK(back.settings.links == rec.settings.links);
  CHECK(back.settings.unknown.contains("future"));
  CHECK_FALSE(back.settings.unknown.contains("links"));  // 既知のキーとして読む（未知のキーに残さない）
  // リンクがなければ書かない（既存の設定ファイルは変わらない）
  ProjectRecord empty = newRecord("空", "E", "", "2026-10-19", clk);
  CHECK_FALSE(settingsToJson(empty).contains("links"));
  // 共有・バックアップには入れない（パスは環境依存）
  const std::string share = exportShareJson({rec}, {}, clk);
  CHECK(share.find("links") == std::string::npos);
  CHECK(share.find("server") == std::string::npos);
  CHECK(share.find("example.com/wiki") == std::string::npos);
}

TEST_CASE("project.settings.json: 壊れた links でも読める（ほかの設定は無傷）") {
  using J = nlohmann::ordered_json;
  for (const char* bad : {"5", "\"x\"", "[]", "null", "{\"1\":5}", "{\"1\":[5,\"x\"]}", "{\"../x\":[{\"target\":\"https://e/\"}]}"}) {
    J s = J::object();
    s["id"] = "p1";
    s["name"] = "案件";
    s["base"] = "2026-10-19";
    s["links"] = J::parse(bad);
    const ProjectRecord r = recordFromSettingsJson(s, "", "stem", clk);
    CAPTURE(bad);
    CHECK(r.settings.links.empty());
    CHECK(r.settings.name == "案件");
  }
}

// ---- 課題の id の使い回し・コントローラ ----

TEST_CASE("nextIssueId: リンクが残っている課題の id は、削除しても新しい課題に使い回さない") {
  std::vector<Issue> issues(2);
  issues[0].id = "1";
  issues[1].id = "2";
  LinkMap links;
  CHECK(nextIssueId(issues, {}, links) == "3");
  links["7"].push_back(mk("A", "https://e/a"));
  CHECK(nextIssueId(issues, {}, links) == "8");  // 7 のリンクが残っている
  links["9"].clear();                             // 空の項目は数えない
  CHECK(nextIssueId(issues, {}, links) == "8");
  CHECK(nextIssueId(issues) == "3");  // 渡さなければ今まで通り
}

namespace {
struct LEnv {
  MemFileStore fs;
  FixedClock clock{LocalTime{2026, 10, 22, 9, 30, 0}};
  app::AppController c{fs, clock, "data", "roadmap_prefs.json"};
  void step(std::int64_t ms) {
    clock.advanceMs(ms);
    c.tick();
  }
};
}  // namespace

TEST_CASE("controller: リンクを付け、再起動後も残る（CSV は変わらない）") {
  LEnv e;
  e.c.start();
  e.c.createProject(app::SampleKind::Empty, "案件", "");
  const std::string id = e.c.session()->addIssue("", "", "課題");
  e.c.afterEdit();
  e.step(600);
  const std::string csvBefore = e.fs.content("data/案件/project.csv");
  std::string why;
  REQUIRE(e.c.addLink(id, "設計書", "https://example.com/wiki/spec", &why));
  REQUIRE(e.c.addLink(id, "", "\\\\server\\share\\docs\\", &why));
  CHECK(e.c.linkCountOf(id) == 2);
  CHECK(e.c.linksOf(id)[1].title == "docs");
  CHECK_FALSE(e.c.addLink(id, "", "javascript:alert(1)", &why));
  CHECK_FALSE(why.empty());
  CHECK_FALSE(e.c.addLink("999", "", "https://example.com/", &why));  // 課題がない
  CHECK(e.c.linkCountOf(id) == 2);
  e.step(600);
  REQUIRE(e.fs.exists("data/案件/project.settings.json"));
  CHECK(e.fs.content("data/案件/project.settings.json").find("https://example.com/wiki/spec") != std::string::npos);
  CHECK(e.fs.content("data/案件/project.csv") == csvBefore);  // CSV は変わらない

  LEnv e2;
  e2.fs = e.fs;
  e2.c.start();
  REQUIRE(e2.c.session());
  REQUIRE(e2.c.linksOf(id).size() == 2);
  CHECK(e2.c.linksOf(id)[0].title == "設計書");
  CHECK(e2.c.linksOf(id)[1].target == "\\\\server\\share\\docs\\");
  // 編集・並べ替え・削除も保存される
  REQUIRE(e2.c.editLink(id, 0, "設計書 v2", "https://example.com/wiki/spec2", &why));
  REQUIRE(e2.c.reorderLink(id, 0, 1));
  REQUIRE(e2.c.removeLink(id, 0));
  e2.step(600);
  LEnv e3;
  e3.fs = e2.fs;
  e3.c.start();
  REQUIRE(e3.c.linksOf(id).size() == 1);
  CHECK(e3.c.linksOf(id)[0].title == "設計書 v2");
}

TEST_CASE("controller: 貼り付けた複数行は 1 行 1 リンク。通知に中身を出さない") {
  LEnv e;
  e.c.start();
  e.c.createProject(app::SampleKind::Empty, "案件", "");
  const std::string id = e.c.session()->addIssue("", "", "課題");
  const auto o = e.c.addLinksFromText(id, "https://secret.example/a\nC:\\secret\\b.txt\nftp://secret.example/c\n");
  CHECK(o.added.size() == 2);
  CHECK(o.rejected.size() == 1);
  CHECK(e.c.linkCountOf(id) == 2);
  REQUIRE_FALSE(e.c.toasts().empty());
  const std::string t = e.c.toasts().back().text;
  CHECK(t.find("2 件") != std::string::npos);
  CHECK(t.find("secret") == std::string::npos);
  e.c.addLinksFromText(id, "   \n\n");
  CHECK(e.c.toasts().back().text.find("追加できる行がありません") != std::string::npos);
  e.c.addLinksFromText("999", "https://example.com/");
  CHECK(e.c.toasts().back().text.find("課題がありません") != std::string::npos);
}

TEST_CASE("controller: リンクを別の課題へ移す・コピーする（先の課題が実在すること）") {
  LEnv e;
  e.c.start();
  e.c.createProject(app::SampleKind::Empty, "案件", "");
  const std::string a = e.c.session()->addIssue("", "", "A");
  const std::string b = e.c.session()->addIssue("", "", "B");
  REQUIRE(e.c.addLink(a, "", "https://example.com/1"));
  REQUIRE(e.c.addLink(a, "", "https://example.com/2"));
  CHECK(e.c.copyLink(a, 0, b));
  CHECK(e.c.moveLink(a, 1, b));
  CHECK(e.c.linkCountOf(a) == 1);
  CHECK(e.c.linkCountOf(b) == 2);
  CHECK_FALSE(e.c.moveLink(a, 0, "999"));  // 先がない
  CHECK(e.c.toasts().back().text.find("課題がありません") != std::string::npos);
  CHECK_FALSE(e.c.copyLink(a, 0, b));  // 先に同じ場所
  CHECK(e.c.toasts().back().text.find("すでに") != std::string::npos);
  CHECK(e.c.toasts().back().text.find("example.com") == std::string::npos);
  CHECK(e.c.linkCountOf(a) == 1);
}

TEST_CASE("controller: リンクは Undo の対象外。課題を削除しても残り、削除した課題の id は使い回さない") {
  LEnv e;
  e.c.start();
  e.c.createProject(app::SampleKind::Empty, "案件", "");
  const std::string id = e.c.session()->addIssue("", "", "課題");
  REQUIRE(e.c.addLink(id, "", "https://example.com/1"));
  CHECK(e.c.session()->deleteIssues({id}));
  // 削除した課題のリンクは残る。次に作る課題は、その id を使わない（古いリンクを引き継がない）
  const std::string next = e.c.session()->addIssue("", "", "新しい課題");
  CHECK(next != id);
  CHECK(e.c.linkCountOf(next) == 0);
  CHECK(e.c.linkCountOf(id) == 1);
}

TEST_CASE("controller: 平置きのプロジェクトにもリンクは付けられる。複製ではリンクを持ち越す（場所を指すだけ）") {
  LEnv e;
  e.c.start();
  e.fs.put("data/古い.csv", "Issue Id,Summary\n1,a\n");
  e.c.syncFolder(true);
  e.c.openProject(e.c.projects().front().id);
  REQUIRE(e.c.session());
  CHECK_FALSE(e.c.canAttach());
  CHECK(e.c.addLink("1", "", "https://example.com/flat"));
  CHECK(e.c.linkCountOf("1") == 1);
  e.c.duplicateProject();
  CHECK(e.c.linkCountOf("1") == 1);
}

TEST_CASE("controller: プロジェクトを開いていないときは安全に断る") {
  LEnv e;
  e.c.start();
  std::string why;
  CHECK_FALSE(e.c.addLink("1", "", "https://example.com/", &why));
  CHECK_FALSE(why.empty());
  CHECK(e.c.linksOf("1").empty());
  CHECK(e.c.linkCountOf("1") == 0);
  CHECK_FALSE(e.c.removeLink("1", 0));
  CHECK_FALSE(e.c.reorderLink("1", 0, 1));
  CHECK(e.c.addLinksFromText("1", "https://example.com/").added.empty());
}
