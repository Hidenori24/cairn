// 報告の書き出し（HTML + インライン SVG）
#include <doctest/doctest.h>

#include <chrono>
#include <set>
#include <string>
#include <vector>

#include "roadmap/baseline.hpp"
#include "roadmap/clock.hpp"
#include "roadmap/csv.hpp"
#include "roadmap/filestore.hpp"
#include "roadmap/gen.hpp"
#include "roadmap/report.hpp"
#include "roadmap/schedule.hpp"
#include "roadmap/text.hpp"

using namespace roadmap;

namespace {

struct Proj {
  std::vector<Issue> issues;
  ProjectMeta meta;
  Computed comp;
  Calendar cal = Calendar::fromText(defaultHolidaysText());
  Day base = *parseIso("2026-10-19");  // 月曜
  FixedClock clk{LocalTime{2026, 10, 19, 18, 36, 0}};

  void compute() {
    ScheduleOptions opt;
    opt.base = base;
    auto c = computeAll(issues, meta, cal, opt);
    REQUIRE(c.ok());
    comp = std::move(c).value();
    comp.issues = issues;
  }
  static Proj fromCsv(const std::string& text) {
    Proj p;
    auto r = csv::importCsv(text);
    REQUIRE(r.ok());
    p.issues = std::move(r.value().issues);
    p.meta = std::move(r.value().meta);
    p.compute();
    return p;
  }
  static Proj fromIssues(std::vector<Issue> is, ProjectMeta m = {}) {
    Proj p;
    p.issues = std::move(is);
    p.meta = std::move(m);
    p.compute();
    return p;
  }
  ExportInput input(const char* name = "テストプロジェクト") const {
    ExportInput in(comp.T, issues, comp.R, cal);
    in.base = base;
    in.meta = &meta;
    in.projectName = name;
    in.clock = &clk;
    return in;
  }
  int idx(const char* id) const { return comp.T.find(id); }
};

const char* const kBasic =
    "Issue Id,Issue Type,Summary,Parent,Assignee,Status,Priority,Resolved,Duration (weeks),Deadline,Outward issue link (Blocks)\n"
    "1,Epic,全体,,,,High,,,,\n"
    "2,Task,A,1,田中,Done,High,2026-10-13,1,,3\n"
    "3,Task,B,1,田中,In Progress,Highest,,2,2026-10-23,4\n"
    "4,Task,C,1,鈴木,To Do,Medium,,2,,5\n"
    "5,Milestone,完了,1,,,High,,0,,\n"
    "6,Task,単独,,鈴木,To Do,Medium,,1,,\n";

bool contains(const std::string& s, const std::string& needle) { return s.find(needle) != std::string::npos; }
std::size_t count(const std::string& s, const std::string& needle) {
  std::size_t n = 0;
  for (std::size_t p = s.find(needle); p != std::string::npos; p = s.find(needle, p + needle.size())) ++n;
  return n;
}

// 出力の中の "<" が、出力側で作ったタグだけであること（データの "<" が混ざっていない）
bool onlyKnownTags(const std::string& h, std::string* bad = nullptr) {
  static const std::set<std::string> ok = {"html", "head", "meta", "title", "style", "body", "div", "header", "main", "section",
                                           "footer", "h1", "h2", "h3", "p", "b", "span", "i", "svg", "defs", "pattern", "path",
                                           "g", "use", "rect", "circle", "text", "tspan", "table", "thead", "tbody", "tr", "th", "td"};
  for (std::size_t p = h.find('<'); p != std::string::npos; p = h.find('<', p + 1)) {
    // <style> の中（CSS）には "<" がない前提。タグの名前を読む
    std::size_t q = p + 1;
    if (q < h.size() && (h[q] == '/' )) ++q;
    if (q < h.size() && h[q] == '!') {
      if (h.compare(p, 9, "<!doctype") != 0) {
        if (bad) *bad = h.substr(p, 20);
        return false;
      }
      continue;
    }
    std::string name;
    while (q < h.size() && ((h[q] >= 'a' && h[q] <= 'z') || (h[q] >= '0' && h[q] <= '9'))) name += h[q++];
    if (!ok.count(name) || q >= h.size() || !(h[q] == ' ' || h[q] == '>' || h[q] == '/')) {
      if (bad) *bad = h.substr(p, 30);
      return false;
    }
  }
  return true;
}

}  // namespace

// ---------------------------------------------------------------- エスケープ
TEST_CASE("report: escapeXml は & < > \" ' を実体参照にする") {
  CHECK(escapeXml("<script>alert(1)</script>") == "&lt;script&gt;alert(1)&lt;/script&gt;");
  CHECK(escapeXml("\"onmouseover=\"x") == "&quot;onmouseover=&quot;x");
  CHECK(escapeXml("&amp;") == "&amp;amp;");  // すでに実体参照に見えるものも、そのまま文字として出す
  CHECK(escapeXml("it's") == "it&#39;s");
  CHECK(escapeXml("a & b < c > d") == "a &amp; b &lt; c &gt; d");
  CHECK(escapeXml("") == "");
  CHECK(escapeXml("日本語 ABC 123 ｱｲｳ ① 🙂") == "日本語 ABC 123 ｱｲｳ ① 🙂");  // 通常の文字はそのまま
}

TEST_CASE("report: escapeXml は制御文字・向きを変える文字を取り除き、壊れた UTF-8 は U+FFFD にする") {
  CHECK(escapeXml(std::string("a\0b", 3)) == "ab");
  CHECK(escapeXml("a\x01\x02\x1f" "b") == "ab");
  CHECK(escapeXml("a\x7f" "b") == "ab");
  CHECK(escapeXml("a\tb\nc\rd") == "a b c d");  // 改行・タブは空白に
  CHECK(escapeXml("a\xC2\x85" "b") == "ab");    // C1 制御文字（U+0085）
  CHECK(escapeXml("a\xE2\x80\xAE" "b") == "ab");  // U+202E（右から左への上書き）
  CHECK(escapeXml("a\xE2\x81\xA6" "b") == "ab");  // U+2066
  CHECK(escapeXml("a\xEF\xBB\xBF" "b") == "ab");  // BOM（U+FEFF）
  CHECK(escapeXml("a\xE2\x80\xA8" "b") == "a b");  // U+2028（行区切り）
  CHECK(escapeXml("a\xFF" "b") == "a\xEF\xBF\xBD" "b");  // 不正なバイト
  CHECK(escapeXml("a\xE3\x81" "b") == "a\xEF\xBF\xBD" "\xEF\xBF\xBD" "b");  // 途中で切れた文字
  CHECK(text::isValidUtf8(escapeXml("a\xFF\xFE\xC0\x80 b\xED\xA0\x80")));
  CHECK(escapeXml("\xEF\xBF\xBD") == "\xEF\xBF\xBD");  // 本物の U+FFFD はそのまま
  // どんなバイト列でも、出力に生の < > " ' & は残らない
  std::string all;
  for (int i = 0; i < 256; ++i) all += static_cast<char>(i);
  const std::string e = escapeXml(all);
  CHECK(text::isValidUtf8(e));
  CHECK(e.find('<') == std::string::npos);
  CHECK(e.find('>') == std::string::npos);
  CHECK(e.find('"') == std::string::npos);
  CHECK(e.find('\'') == std::string::npos);
  CHECK(e.find('\0') == std::string::npos);
}

TEST_CASE("report: 件名・担当者・状況・プロジェクト名に危険な文字があっても HTML が壊れず、スクリプトが現れない") {
  std::vector<Issue> is;
  Issue a;
  a.id = "1";
  a.summary = "<script>alert(1)</script>";
  a.assignee = "\"onmouseover=\"alert(2)";
  a.status = "&amp;<b>";
  a.prio = "High";
  a.durW = 2.0;
  a.labels = {"<img src=x onerror=alert(3)>"};
  is.push_back(a);
  Issue b;
  b.id = "2";
  b.summary = "子</text><script>x</script>";
  b.assignee = "'><svg onload=alert(4)>";
  b.type = "Epic";
  is.push_back(b);
  Issue c;
  c.id = "3";
  c.summary = "孫\"; fill=\"red\" onclick=\"alert(5)";
  c.parent = "2";
  c.assignee = std::string("N\0UL\x01", 5);
  c.durW = 1.0;
  is.push_back(c);
  Proj p = Proj::fromIssues(is);

  for (const bool dark : {false, true})
    for (const ReportScope sc : {ReportScope::All, ReportScope::Current}) {
      ExportInput in = p.input("</title><script>alert(6)</script>");
      in.filterLabel = "<i onclick=alert(7)>";
      for (int id : p.comp.T.dfs) in.currentRows.push_back({id, p.comp.T.depth[static_cast<std::size_t>(id)], !p.comp.T.kids[static_cast<std::size_t>(id)].empty(), true});
      ExportOptions opt;
      opt.dark = dark;
      opt.scope = sc;
      for (const char* by : {"root", "float", "asg"}) {
        opt.colorBy = by;
        const std::string h = exportHtml(in, opt);
        std::string bad;
        CHECK_MESSAGE(onlyKnownTags(h, &bad), bad);
        CHECK_FALSE(contains(h, "<script"));
        CHECK_FALSE(contains(h, "</script"));
        CHECK_FALSE(contains(h, "<img"));
        CHECK_FALSE(contains(h, "<svg onload"));
        CHECK_FALSE(contains(h, "\"onmouseover="));   // 属性の外に出る形（引用符で始まる）が残らない
        CHECK_FALSE(contains(h, "onclick=\"alert"));
        CHECK_FALSE(contains(h, "\" fill=\"red\" onclick"));
        CHECK(contains(h, "&lt;script&gt;alert(1)&lt;/script&gt;"));  // 件名は文字として出る
        CHECK(contains(h, "&quot;onmouseover=&quot;alert(2)"));
        CHECK(contains(h, "&amp;amp;&lt;b&gt;"));
        CHECK_FALSE(contains(h, std::string("\0", 1)));
        CHECK(text::isValidUtf8(h));
        // <title> が閉じられて抜けることがない
        // <head> の <title> は 1 つで、プロジェクト名の "</title>" で閉じられて抜けることがない
        const std::string head = h.substr(0, h.find("</head>"));
        CHECK(count(head, "<title>") == 1);
        CHECK(count(head, "</title>") == 1);
        CHECK(contains(head, "&lt;/title&gt;&lt;script&gt;alert(6)&lt;/script&gt; - 報告</title>"));
      }
    }
}

// ---------------------------------------------------------------- ファイル名・保存先
TEST_CASE("report: 既定のファイル名は <プロジェクト名>_<日付>.html。使えない文字は _") {
  CHECK(reportFileName("階層デモ", "2026-10-19") == "階層デモ_2026-10-19.html");
  CHECK(reportFileName("a/b\\c:d*e?f\"g<h>i|j", "2026-10-19") == "a_b_c_d_e_f_g_h_i_j_2026-10-19.html");
  CHECK(reportFileName("tab\there\nnl", "2026-10-19") == "tab_here_nl_2026-10-19.html");
  CHECK(reportFileName("", "2026-10-19") == "報告_2026-10-19.html");
  CHECK(reportFileName("   ", "2026-10-19") == "報告_2026-10-19.html");
  CHECK(reportFileName("名前...", "2026-10-19") == "名前_2026-10-19.html");  // 末尾の . と空白は付けない
  CHECK(reportFileName("名前", "") == "名前.html");
  // 日付に区切りや記号が来ても、数字と - だけ
  CHECK(reportFileName("名前", "../../x") == "名前.html");
  CHECK(reportFileName("名前", "2026/10/19; rm") == "名前_20261019.html");
  // Windows の予約名（拡張子が付いても予約）
  CHECK(reportFileName("CON", "2026-10-19") == "_CON_2026-10-19.html");
  CHECK(reportFileName("nul", "2026-10-19") == "_nul_2026-10-19.html");
  CHECK(reportFileName("com1", "2026-10-19") == "_com1_2026-10-19.html");
  CHECK(reportFileName("CON.x", "2026-10-19") == "_CON.x_2026-10-19.html");
  CHECK(reportFileName("CONSOLE", "2026-10-19") == "CONSOLE_2026-10-19.html");
  // 長い名前は 60 文字まで
  const std::string longName(200, 'a');
  CHECK(reportFileName(longName, "2026-10-19") == std::string(60, 'a') + "_2026-10-19.html");
  std::string jpName;
  for (int i = 0; i < 100; ++i) jpName += "あ";
  const std::string jp = reportFileName(jpName, "2026-10-19");
  CHECK(text::codepointCount(jp) == 60 + std::string("_2026-10-19.html").size());
  CHECK(text::isValidUtf8(jp));
  // 壊れた UTF-8 でも有効な名前になる
  CHECK(text::isValidUtf8(reportFileName("a\xFF" "b", "2026-10-19")));
}

TEST_CASE("report: 保存先の検査（.html / .htm だけ・制御文字や空は不可）") {
  CHECK(checkReportPath("C:\\Users\\x\\報告_2026-10-19.html").ok());
  CHECK(checkReportPath("/tmp/out/報告.html").ok());
  CHECK(checkReportPath("out.HTM").ok());
  CHECK(checkReportPath("a.b.html").ok());
  CHECK_FALSE(checkReportPath("").ok());
  CHECK_FALSE(checkReportPath("out").ok());
  CHECK_FALSE(checkReportPath("out.exe").ok());
  CHECK_FALSE(checkReportPath("out.html.exe").ok());
  CHECK_FALSE(checkReportPath("out.txt").ok());
  CHECK_FALSE(checkReportPath(".html").ok());               // ファイル名がない
  CHECK_FALSE(checkReportPath("/tmp/dir/").ok());           // フォルダ
  CHECK_FALSE(checkReportPath("C:\\tmp\\").ok());
  CHECK_FALSE(checkReportPath(std::string("a\0.html", 7)).ok());
  CHECK_FALSE(checkReportPath("a\n.html").ok());
  CHECK_FALSE(checkReportPath("a\xFF.html").ok());          // 壊れた UTF-8
  CHECK_FALSE(checkReportPath(std::string(40000, 'a') + ".html").ok());
}

TEST_CASE("report: writeReport は検査して IFileStore に書く（失敗は戻り値）") {
  MemFileStore fs;
  CHECK(fs.makeDirs("out").ok());
  CHECK(writeReport(fs, "out/r.html", "<!doctype html>x").ok());
  REQUIRE(fs.exists("out/r.html"));
  CHECK(fs.content("out/r.html") == "<!doctype html>x");
  // 上書き（保存ダイアログで確認済みの前提）
  CHECK(writeReport(fs, "out/r.html", "y").ok());
  CHECK(fs.content("out/r.html") == "y");
  // 拡張子が違えば書かない
  const int w = fs.writeCount();
  auto bad = writeReport(fs, "out/r.exe", "z");
  CHECK_FALSE(bad.ok());
  CHECK_FALSE(bad.error().empty());
  CHECK(fs.writeCount() == w);
  CHECK_FALSE(fs.exists("out/r.exe"));
  // 同じ名前のフォルダがあれば書かない
  CHECK(fs.makeDirs("dir.html").ok());
  CHECK_FALSE(writeReport(fs, "dir.html", "z").ok());
  // 書き込みの失敗は戻り値で返る（例外にしない）
  fs.setFailWrites(true);
  auto fail = writeReport(fs, "out/r2.html", "z");
  CHECK_FALSE(fail.ok());
  CHECK_FALSE(fs.exists("out/r2.html"));
}

// ---------------------------------------------------------------- 書き出す行
TEST_CASE("report: reportRows は範囲に従う（全体・今の表示・選んだ課題の配下）") {
  Proj p = Proj::fromCsv(kBasic);
  ExportInput in = p.input();
  const Tree& T = p.comp.T;
  // 全体: 深さ優先の順で全部
  auto all = reportRows(in, ReportScope::All);
  REQUIRE(all.size() == static_cast<std::size_t>(T.n));
  for (std::size_t i = 0; i < all.size(); ++i) CHECK(all[i].id == T.dfs[i]);
  CHECK(all[0].isPar);
  CHECK(all[0].depth == 0);
  // 今の表示: 渡した行のまま（範囲外の添字は捨てる）
  in.currentRows = {{p.idx("1"), 0, true, false}, {999, 0, false, false}, {-1, 0, false, false}, {p.idx("6"), 0, false, true}};
  auto cur = reportRows(in, ReportScope::Current);
  REQUIRE(cur.size() == 2);
  CHECK(cur[0].id == p.idx("1"));
  CHECK(cur[1].id == p.idx("6"));
  // 配下: 選んだ課題が深さ 0
  in.subtreeRoot = p.idx("1");
  auto sub = reportRows(in, ReportScope::Subtree);
  REQUIRE(sub.size() == 5);
  CHECK(sub[0].id == p.idx("1"));
  CHECK(sub[0].depth == 0);
  CHECK(sub[1].depth == 1);
  in.subtreeRoot = p.idx("3");  // 葉
  CHECK(reportRows(in, ReportScope::Subtree).size() == 1);
  in.subtreeRoot = -1;
  CHECK(reportRows(in, ReportScope::Subtree).empty());
  in.subtreeRoot = 999;
  CHECK(reportRows(in, ReportScope::Subtree).empty());
  // 空のプロジェクト
  Proj e = Proj::fromIssues({});
  CHECK(reportRows(e.input(), ReportScope::All).empty());
}

// ---------------------------------------------------------------- 全体
TEST_CASE("report: 文字コード・形（doctype・meta charset・lang・BOM なし・NUL なし）") {
  Proj p = Proj::fromCsv(kBasic);
  const std::string h = exportHtml(p.input(), {});
  CHECK(h.compare(0, 15, "<!doctype html>") == 0);
  CHECK(contains(h, "<html lang=\"ja\">"));
  CHECK(h.find("<meta charset=\"utf-8\">") < 1024);  // 先頭 1024 バイトの中
  CHECK(static_cast<unsigned char>(h[0]) != 0xEF);   // BOM なし
  CHECK(text::isValidUtf8(h));
  CHECK(h.find('\0') == std::string::npos);
  CHECK(contains(h, "</html>"));
  CHECK(contains(h, "<title>テストプロジェクト - 報告</title>"));
  // 印刷: A4 横・@media print・ページの区切り
  CHECK(contains(h, "@page{size:A4 landscape"));
  CHECK(contains(h, "@media print"));
  CHECK(contains(h, "break-before:page"));
  CHECK(contains(h, "break-inside:avoid"));
  // ヘッダ・フッタ
  CHECK(contains(h, "<h1>テストプロジェクト</h1>"));
  CHECK(contains(h, "基準日 <b>2026/10/19(月)</b>"));
  CHECK(contains(h, "出力 <b>2026/10/19 18:36</b>"));
  CHECK(contains(h, "このアプリで出力"));
  std::string bad;
  CHECK_MESSAGE(onlyKnownTags(h, &bad), bad);
}

TEST_CASE("report: 外部のファイル・スクリプトを読まない（1 ファイルで完結）") {
  Proj p = Proj::fromCsv(kBasic);
  ExportOptions opt;
  for (const bool dark : {false, true}) {
    opt.dark = dark;
    const std::string h = exportHtml(p.input(), opt);
    CHECK_FALSE(contains(h, "<script"));
    CHECK_FALSE(contains(h, "<link"));
    CHECK_FALSE(contains(h, "<img"));
    CHECK_FALSE(contains(h, "<iframe"));
    CHECK_FALSE(contains(h, "@import"));
    CHECK_FALSE(contains(h, "src="));
    CHECK_FALSE(contains(h, "javascript:"));
    CHECK_FALSE(contains(h, " onclick="));
    CHECK_FALSE(contains(h, " onload="));
    CHECK_FALSE(contains(h, "url(http"));
    CHECK_FALSE(contains(h, "https://"));
    // "http://" は SVG の名前空間の宣言だけ
    CHECK(count(h, "http://") == count(h, "xmlns=\"http://www.w3.org/2000/svg\""));
    // href は文書内の参照（#）だけ
    for (std::size_t q = h.find("href="); q != std::string::npos; q = h.find("href=", q + 1)) CHECK(h.compare(q, 7, "href=\"#") == 0);
    // url() も文書内の参照だけ
    for (std::size_t q = h.find("url("); q != std::string::npos; q = h.find("url(", q + 1)) CHECK(h.compare(q, 5, "url(#") == 0);
  }
}

TEST_CASE("report: 内容のオン・オフ（ガント・統計・リスト）") {
  Proj p = Proj::fromCsv(kBasic);
  const auto has = [](const std::string& h, const char* what) { return h.find(what) != std::string::npos; };
  const auto run = [&](bool g, bool s, bool l) {
    ExportOptions o;
    o.gantt = g;
    o.stats = s;
    o.list = l;
    return exportHtml(p.input(), o);
  };
  const std::string all = run(true, true, true);
  CHECK(has(all, "<h2>ガント</h2>"));
  CHECK(has(all, "<h2>統計</h2>"));
  CHECK(has(all, "<h2>リスト</h2>"));
  const std::string g = run(true, false, false);
  CHECK(has(g, "<h2>ガント</h2>"));
  CHECK_FALSE(has(g, "<h2>統計</h2>"));
  CHECK_FALSE(has(g, "<h2>リスト</h2>"));
  CHECK_FALSE(has(g, "<table"));
  const std::string s = run(false, true, false);
  CHECK_FALSE(has(s, "<h2>ガント</h2>"));
  CHECK(has(s, "<h2>統計</h2>"));
  CHECK(has(s, "担当者別の負荷"));
  CHECK_FALSE(has(s, "id=\"gh\""));
  const std::string l = run(false, false, true);
  CHECK(has(l, "<table"));
  CHECK_FALSE(has(l, "<h2>統計</h2>"));
  CHECK_FALSE(has(l, "id=\"gh\""));
  const std::string none = run(false, false, false);
  CHECK(has(none, "書き出す内容が選ばれていません"));
  CHECK_FALSE(has(none, "<h2>"));
  // 最初の節だけ新しいページにしない
  CHECK(count(all, "<section class=\"part first\">") == 1);
  CHECK(count(all, "<section class=\"part\">") == 2);
  CHECK(count(g, "<section class=\"part first\">") == 1);
}

TEST_CASE("report: ライトが既定（背景は白）、ダークも選べる") {
  Proj p = Proj::fromCsv(kBasic);
  ExportOptions light;
  CHECK_FALSE(light.dark);
  const std::string l = exportHtml(p.input(), light);
  CHECK(contains(l, "color-scheme:light"));
  CHECK(contains(l, "--bg:#ffffff"));
  CHECK(contains(l, "--ink:#1a2230"));
  ExportOptions dark;
  dark.dark = true;
  const std::string d = exportHtml(p.input(), dark);
  CHECK(contains(d, "color-scheme:dark"));
  CHECK(contains(d, "--bg:#0e1116"));
  CHECK(contains(d, "--ink:#e6e9ee"));
  CHECK_FALSE(contains(d, "--bg:#ffffff"));
  // 印刷でも背景を落とさない
  CHECK(contains(d, "print-color-adjust:exact"));
  // SVG の色もテーマで変わる（最初の Epic の色: ライト 0xb5667f / ダーク 0xd0809b）
  CHECK(contains(l, "#b5667f"));
  CHECK(contains(d, "#d0809b"));
  CHECK_FALSE(contains(l, "#d0809b"));
  CHECK_FALSE(contains(d, "#b5667f"));
}

TEST_CASE("report: 出力日時は IClock から（固定できる）。時計がなければ書かない") {
  Proj p = Proj::fromCsv(kBasic);
  p.clk.set(LocalTime{2027, 3, 4, 5, 6, 7});
  CHECK(contains(exportHtml(p.input(), {}), "出力 <b>2027/03/04 05:06</b>"));
  ExportInput in = p.input();
  in.clock = nullptr;
  const std::string h = exportHtml(in, {});
  CHECK_FALSE(contains(h, "出力 <b>"));
  CHECK(contains(h, "基準日"));
  // 同じ入力なら同じバイト列
  CHECK(exportHtml(p.input(), {}) == exportHtml(p.input(), {}));
}

TEST_CASE("report: 空のプロジェクト（課題なし）でも案内つきの HTML を返し、落ちない") {
  Proj e = Proj::fromIssues({});
  for (const bool g : {true, false})
    for (const bool s : {true, false})
      for (const bool l : {true, false}) {
        ExportOptions o;
        o.gantt = g;
        o.stats = s;
        o.list = l;
        const std::string h = exportHtml(e.input("空"), o);
        CHECK(contains(h, "課題がありません"));
        CHECK(contains(h, "<h1>空</h1>"));
        CHECK(contains(h, "</html>"));
        CHECK(text::isValidUtf8(h));
      }
  // 大きさの食い違い（呼び出しの誤り）でも落ちない
  Proj p = Proj::fromCsv(kBasic);
  std::vector<Issue> shorter(p.issues.begin(), p.issues.begin() + 2);
  ExportInput bad(p.comp.T, shorter, p.comp.R, p.cal);
  CHECK(contains(exportHtml(bad, {}), "課題がありません"));
  // 作業が 1 つもない（Epic だけ）統計
  Issue only;
  only.id = "1";
  only.summary = "親だけ";
  only.type = "Epic";
  Proj q = Proj::fromIssues({only});
  ExportOptions so;
  so.gantt = false;
  so.list = false;
  const std::string sh = exportHtml(q.input(), so);
  CHECK((contains(sh, "集計できる作業がありません") || contains(sh, "進み")));
}

TEST_CASE("report: ガントは見出し・バー・基準日・期限・依存の矢印・余裕を SVG で出す") {
  Proj p = Proj::fromCsv(kBasic);
  ExportOptions o;
  o.stats = false;
  o.list = false;
  const std::string h = exportHtml(p.input(), o);
  CHECK(contains(h, "<svg "));
  CHECK(contains(h, "id=\"gh\""));
  CHECK(contains(h, "id=\"gb\""));
  CHECK(contains(h, ">基準日</text>"));
  CHECK(contains(h, "<use href=\"#gb\"/><use href=\"#gh\"/>"));
  CHECK(contains(h, "stroke-width=\"1.3\""));           // 依存の矢印
  CHECK(contains(h, "fill=\"url(#gp)\""));             // 進行中の斜線
  CHECK(contains(h, "l6 6l-6 6l-6-6z"));               // 期限の菱形
  CHECK(contains(h, "2026/10/19"));                    // 基準日（注記）
  // 行ごとの件名・担当・状況
  CHECK(contains(h, ">鈴木</text>"));
  CHECK(contains(h, "In Progress"));
  CHECK(contains(h, ">2w</text>"));
  // 依存の矢印を消せる
  o.showDeps = false;
  const std::string nd = exportHtml(p.input(), o);
  CHECK_FALSE(contains(nd, "stroke-width=\"1.3\""));
  CHECK(nd.size() < h.size());
}

TEST_CASE("report: 余裕の影（斜線）は未完了で最遅が最早より後の作業だけ") {
  // 長い作業 X(4週) と短い作業 Y(1週) がどちらも M に先行する: Y は 3 週ぶんの余裕
  Proj p = Proj::fromCsv(
      "Issue Id,Issue Type,Summary,Parent,Assignee,Status,Priority,Duration (weeks),Outward issue link (Blocks)\n"
      "1,Task,X,,田中,To Do,High,4,3\n"
      "2,Task,Y,,鈴木,To Do,High,1,3\n"
      "3,Milestone,M,,,,High,0,\n");
  REQUIRE(p.comp.R[static_cast<std::size_t>(p.idx("2"))].alapFinish > p.comp.R[static_cast<std::size_t>(p.idx("2"))].asapFinish);
  ExportOptions o;
  o.stats = false;
  o.list = false;
  const std::string h = exportHtml(p.input(), o);
  CHECK(count(h, "fill=\"url(#hs-") == 1);  // Y だけ
}

TEST_CASE("report: 拡大率（日・週・月・全体）で 1 日の幅と見出しが変わる") {
  Proj p = Proj::fromCsv(kBasic);
  std::vector<std::size_t> widths;
  for (const char* z : {"day", "week", "month", "all"}) {
    ExportOptions o;
    o.stats = false;
    o.list = false;
    o.zoom = z;
    const std::string h = exportHtml(p.input(), o);
    const std::size_t sv = h.find("<svg xmlns=");
    REQUIRE(sv != std::string::npos);
    const std::size_t w = h.find("width=\"", sv);
    REQUIRE(w != std::string::npos);
    widths.push_back(static_cast<std::size_t>(std::stod(h.substr(w + 7))));
    if (std::string(z) == "day") CHECK(contains(h, "text-anchor=\"middle\" fill="));  // 日付の数字の行
    CHECK(contains(h, "<h2>ガント</h2>"));
  }
  CHECK(widths[0] > widths[1]);  // 日 > 週
  CHECK(widths[1] > widths[2]);  // 週 > 月
  CHECK(widths[3] <= 590 + 900 + 2);  // 全体は幅に収める
  // 不明な名前は週
  ExportOptions o;
  o.stats = false;
  o.list = false;
  o.zoom = "???";
  const std::string h = exportHtml(p.input(), o);
  o.zoom = "week";
  CHECK(h == exportHtml(p.input(), o));
}

TEST_CASE("report: 色分け（Epic・余裕・担当者）") {
  Proj p = Proj::fromCsv(kBasic);
  ExportOptions o;
  o.stats = false;
  o.list = false;
  o.colorBy = "root";
  const std::string r = exportHtml(p.input(), o);
  o.colorBy = "float";
  const std::string f = exportHtml(p.input(), o);
  o.colorBy = "asg";
  const std::string a = exportHtml(p.input(), o);
  CHECK(r != f);
  CHECK(f != a);
  CHECK(contains(f, "余裕あり"));
  CHECK(contains(a, "田中"));
  CHECK(contains(r, "全体"));  // Epic の名前が凡例に
}

TEST_CASE("report: 遅れは色だけに頼らない（赤の枠 + 「!」）") {
  // 期限が基準日の前: 余裕がマイナスになる
  Proj p = Proj::fromCsv(
      "Issue Id,Issue Type,Summary,Parent,Assignee,Status,Priority,Duration (weeks),Deadline\n"
      "1,Task,遅れる作業,,田中,To Do,High,4,2026-10-23\n");
  const ScheduleRow& r = p.comp.R[0];
  REQUIRE(r.floatDays < 0);
  const std::string h = exportHtml(p.input(), {});
  CHECK(contains(h, "! −"));                 // 余裕の列
  CHECK(contains(h, "rk-late"));
  CHECK(contains(h, "stroke=\"#d92d20\" stroke-width=\"2\""));  // バーの赤い枠
  CHECK(contains(h, "! 遅れ 1"));              // ヘッダ・統計
}

TEST_CASE("report: 範囲「今の表示のまま」は行だけを出し、薄くする行は opacity、絞り込みの名前を escape して書く") {
  Proj p = Proj::fromCsv(kBasic);
  ExportInput in = p.input();
  in.currentRows = {{p.idx("6"), 0, false, true}, {p.idx("4"), 1, false, true}};
  in.dimmed.assign(static_cast<std::size_t>(p.comp.T.n), 0);
  in.dimmed[static_cast<std::size_t>(p.idx("4"))] = 1;
  in.dimAlpha = 0.35f;
  in.filterLabel = "担当: <鈴木>";
  ExportOptions o;
  o.scope = ReportScope::Current;
  o.stats = false;
  const std::string h = exportHtml(in, o);
  CHECK(contains(h, "表示範囲: 画面の表示のまま（絞り込み: 担当: &lt;鈴木&gt;）"));
  CHECK(contains(h, "opacity=\"0.35\""));
  CHECK(contains(h, "</tspan> 単独</text>"));
  CHECK(contains(h, "</tspan> C</text>"));
  CHECK_FALSE(contains(h, "</tspan> B</text>"));  // 出ていない行は書かない
  CHECK_FALSE(contains(h, "B</td>"));
  // 範囲「全体」では絞り込みは関係しない
  o.scope = ReportScope::All;
  const std::string all = exportHtml(in, o);
  CHECK(contains(all, "B</td>"));
  CHECK_FALSE(contains(all, "opacity=\"0.35\""));
  CHECK_FALSE(contains(all, "表示範囲"));
}

TEST_CASE("report: 範囲「選んだ課題の配下」") {
  Proj p = Proj::fromCsv(kBasic);
  ExportInput in = p.input();
  in.subtreeRoot = p.idx("1");
  ExportOptions o;
  o.scope = ReportScope::Subtree;
  o.stats = false;
  const std::string h = exportHtml(in, o);
  CHECK(contains(h, "表示範囲: 「全体」とその配下"));
  CHECK_FALSE(contains(h, "単独</td>"));  // 配下ではない
  CHECK(contains(h, "B</td>"));
}

TEST_CASE("report: 統計は画面の統計と同じ内容（タイル・状況・余裕・週・担当別・負荷・完了・ルート別・優先度別）") {
  Proj p = Proj::fromCsv(kBasic);
  ExportOptions o;
  o.gantt = false;
  o.list = false;
  const std::string h = exportHtml(p.input(), o);
  for (const char* s : {"進み（期間ベース）", "余裕小（10日以内）", "完了の見込み", "担当なし", "余裕の分布", "週ごとの完了", "担当別",
                        "担当者別の負荷（週ごと）", "担当者別の完了（過去 8 週）", "ルート課題別", "優先度別", "21日以上", "Highest",
                        "状況の内訳"})
    CHECK_MESSAGE(contains(h, s), s);
  // 数字: 作業 5 件のうち完了 1（A）
  CHECK(contains(h, "<div class=\"v\">1 / 4</div>"));  // マイルストーンを除く作業 4 件
  CHECK(contains(h, "田中"));
  CHECK(contains(h, "鈴木"));
  // 担当者の負荷は 100% を超えないこの例では「!」なし。ヒートマップのセルが出る
  CHECK(contains(h, "最大 "));
  // 資源制限
  ExportInput in = p.input();
  in.resource = {true, 2};
  CHECK(contains(exportHtml(in, o), "同時に 2 件が動いている状態"));
}

TEST_CASE("report: 統計に「推移」の節（バーンダウン・ペース予測・リードタイム・滞留日数）を、同じ内容で出す") {
  Proj p = Proj::fromCsv(
      "Issue Id,Issue Type,Summary,Parent,Assignee,Status,Priority,Created,Updated,Resolved,Duration (weeks)\n"
      "1,Task,<img src=x onerror=alert(1)>,,田中,To Do,High,2026-09-01,2026-09-10,,1\n"
      "2,Task,B,,田中,Done,High,2026-09-01,,2026-10-06,1\n"
      "3,Task,C,,鈴木,Done,High,2026-09-02,,2026-10-07,1\n"
      "4,Task,D,,鈴木,Done,High,2026-09-02,,2026-10-13,1\n"
      "5,Task,E,,鈴木,Done,High,2026-09-03,,2026-10-14,1\n"
      "6,Task,F,,,To Do,Low,2026-10-12,,,1\n"
      "7,Task,G,,,To Do,Low,,,,1\n"
      "8,Task,H,,,Done,Low,2026-10-12,,2026-10-09,1\n");
  ExportOptions o;
  o.gantt = false;
  o.list = false;
  const std::string h = exportHtml(p.input(), o);
  for (const char* s : {"<h2>推移</h2>", "バーンダウン／バーンアップ", "ペース予測（点線）", "stroke-dasharray", "完了の見込み", "リードタイム", "90 パーセンタイル",
                        "滞留日数", "! 20+ 営業日", "動いていない日数", "長く動いていない順", "絞り込みに関係なく", "作成日（Created）のない 1 件は数えていません",
                        "1 件は完了日が作成日より前"})
    CHECK_MESSAGE(contains(h, s), s);
  CHECK(contains(h, "&lt;img src=x onerror=alert(1)&gt;"));  // 件名はエスケープされて表に入る
  CHECK_FALSE(contains(h, "<img"));
  CHECK_FALSE(contains(h, "<script"));
  std::string bad;
  CHECK_MESSAGE(onlyKnownTags(h, &bad), bad);
  CHECK(text::isValidUtf8(h));
  // 統計を出さないときは「推移」も出さない
  ExportOptions off;
  off.gantt = true;
  off.stats = false;
  off.list = false;
  CHECK_FALSE(contains(exportHtml(p.input(), off), "推移"));
  // ダークでも出る
  ExportOptions dk = o;
  dk.dark = true;
  CHECK(contains(exportHtml(p.input(), dk), "バーンダウン／バーンアップ"));
}

TEST_CASE("report: 推移 — 日付の列がない CSV（meta なし）でも落ちず、案内を出す") {
  Proj p = Proj::fromCsv(kBasic);
  ExportOptions o;
  o.gantt = false;
  o.list = false;
  ExportInput in = p.input();
  in.meta = nullptr;
  const std::string h = exportHtml(in, o);
  CHECK(contains(h, "<h2>推移</h2>"));
  CHECK(contains(h, "Created（作成日）の列がないので、バーンダウンとリードタイムは出せません"));
  CHECK(contains(h, "作成日（Created）のある作業がないので、バーンダウンは出せません"));
  std::string bad;
  CHECK_MESSAGE(onlyKnownTags(h, &bad), bad);
}

TEST_CASE("report: 負荷が 100% を超えたセルには「!」が付く（色だけに頼らない）") {
  // 田中に 3 週間の作業を 2 件並行で入れる（資源制限なし）→ 週の負荷 200%
  Proj p = Proj::fromCsv(
      "Issue Id,Issue Type,Summary,Parent,Assignee,Status,Priority,Duration (weeks),Resolved\n"
      "1,Task,P,,田中,To Do,High,3,\n"
      "2,Task,Q,,田中,To Do,High,3,\n");
  ExportOptions o;
  o.gantt = false;
  o.list = false;
  const std::string h = exportHtml(p.input(), o);
  CHECK(contains(h, ">!200<"));
  CHECK(contains(h, "最大 200% !"));
}

TEST_CASE("report: リストは件名・状況・担当・期間・余裕・期限の表") {
  Proj p = Proj::fromCsv(kBasic);
  ExportOptions o;
  o.gantt = false;
  o.stats = false;
  const std::string h = exportHtml(p.input(), o);
  CHECK(contains(h, "<th>件名</th><th>状況</th><th>担当</th><th>期間</th><th class=\"n\">余裕</th><th>期限</th>"));
  CHECK(count(h, "<tr") == 1 + 6);          // 見出し + 6 行
  CHECK(contains(h, "<tr class=\"par\">"));   // 親の行
  CHECK(contains(h, "class=\"st-done\">Done<"));
  CHECK(contains(h, "10/23(金)"));            // B の期限
  CHECK(contains(h, "完了</td>"));            // A の余裕の列
  CHECK(contains(h, "padding-left:22px"));    // 子の行はインデント（8 + 14）
  CHECK(contains(h, "◆ "));                  // マイルストーン
}

TEST_CASE("report: ガントの行数の上限を超えたら注記を出し、先頭の行だけにする（リストは全部）") {
  Proj p = Proj::fromCsv(kBasic);
  ExportOptions o;
  o.stats = false;
  o.maxGanttRows = 3;
  const std::string h = exportHtml(p.input(), o);
  CHECK(contains(h, "課題が 6 行あるため、ガントには先頭の 3 行だけを出しています"));
  CHECK(count(h, "<tr") == 1 + 6);  // リストは全部
  o.maxGanttRows = 0;               // 0 は 1 行として扱う（落ちない）
  CHECK(contains(exportHtml(p.input(), o), "先頭の 1 行だけ"));
  o.maxGanttRows = 6;
  CHECK_FALSE(contains(exportHtml(p.input(), o), "ガントには先頭"));
}

TEST_CASE("report: 行が少ないときだけバーに説明（title）を付ける") {
  Proj p = Proj::fromCsv(kBasic);
  ExportOptions o;
  o.stats = false;
  o.list = false;
  const std::string h = exportHtml(p.input(), o);
  CHECK(contains(h, "<title>3  B\n計画 "));
  CHECK(contains(h, "余裕 "));
}

TEST_CASE("report: 日付が極端でも出力が膨らまない（時間軸は上限の日数まで）") {
  Proj p = Proj::fromCsv(
      "Issue Id,Issue Type,Summary,Parent,Assignee,Status,Priority,Duration (weeks),Deadline\n"
      "1,Task,遠い期限,,田中,To Do,High,1,9999-12-31\n"
      "2,Task,ふつう,,田中,To Do,High,1,\n");
  ExportOptions o;
  o.stats = false;
  o.list = false;
  o.zoom = "day";
  const auto t0 = std::chrono::steady_clock::now();
  const std::string h = exportHtml(p.input(), o);
  const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  CHECK(contains(h, "時間軸は 4000 日までで切っています"));
  CHECK(h.size() < 3u * 1024 * 1024);
  CHECK(ms < 3000);
}

TEST_CASE("report: 5,000 件のプロジェクトを数秒以内に出し、サイズが極端に大きくならない") {
  auto g = generateProject({5000, 7});
  Proj p = Proj::fromIssues(std::move(g.issues), g.meta);
  REQUIRE(p.comp.T.n == 5000);
  ExportOptions o;  // 3 つとも・週・Epic 色・依存あり
  const auto t0 = std::chrono::steady_clock::now();
  const std::string h = exportHtml(p.input("大きなプロジェクト"), o);
  const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  MESSAGE("5000件: " << ms << " ms, " << h.size() / 1024 << " KB, svg " << count(h, "<svg ") << " 個, 要素 " << count(h, "<") << " 個");
  CHECK(ms < 8000);                       // サニタイザ付きでも数秒
  CHECK(h.size() < 16u * 1024 * 1024);    // 数 MB
  CHECK_FALSE(contains(h, "ガントには先頭"));  // ちょうど上限なので注記なし
  CHECK(text::isValidUtf8(h));
  CHECK(count(h, "<tr") >= 5001);  // リストは全部（見出し + 5000 行）
  // 上限を 1 つ超えると注記が出る
  auto g2 = generateProject({5001, 7});
  Proj p2 = Proj::fromIssues(std::move(g2.issues), g2.meta);
  ExportOptions go;
  go.stats = false;
  go.list = false;
  CHECK(contains(exportHtml(p2.input(), go), "ガントには先頭の 5000 行だけを出しています"));
  // 日の拡大率でも大きくならない（見出しは使い回す）
  go.zoom = "day";
  const std::string hd = exportHtml(p.input(), go);
  MESSAGE("5000件・日: " << hd.size() / 1024 << " KB");
  CHECK(hd.size() < 16u * 1024 * 1024);
  CHECK(count(hd, "id=\"gh\"") == 1);
}

TEST_CASE("report: 長いガントは行ごとに区切った SVG になり、見出しと背景は最初の 1 つだけが持つ") {
  auto g = generateProject({400, 3});
  Proj p = Proj::fromIssues(std::move(g.issues), g.meta);
  ExportOptions o;
  o.stats = false;
  o.list = false;
  const std::string h = exportHtml(p.input(), o);
  const std::size_t chunks = count(h, "<div class=\"gseg\">");
  CHECK(chunks >= 3);
  CHECK(count(h, "id=\"gh\"") == 1);
  CHECK(count(h, "id=\"gb\"") == 1);
  CHECK(count(h, "<use href=\"#gh\"/>") == chunks);
  // どのチャンクの SVG も同じ幅（印刷で同じ倍率になる）
  std::set<std::string> widths;
  for (std::size_t q = h.find("<svg xmlns"); q != std::string::npos; q = h.find("<svg xmlns", q + 1)) {
    const std::size_t w = h.find("width=\"", q);
    widths.insert(h.substr(w, h.find('"', w + 7) - w));
  }
  CHECK(widths.size() == 1);
}

// ---------------------------------------------------------------- ベースライン（Issue #53）
TEST_CASE("report: ベースラインを選ぶと、ガントに計画のバーと差、統計に「計画との差」の節が出る") {
  Proj base = Proj::fromCsv(kBasic);
  auto bl = captureBaseline(base.issues, base.comp, "キックオフ", "2026-10-19T09:30");
  REQUIRE(bl.ok());
  // C（4）を 2 週 → 4 週に延ばす（遅れ）。単独（6）は 1 週 → 0.4 週（早まり）
  Proj p = base;
  for (Issue& o : p.issues) {
    if (o.id == "4") o.durW = 4.0;
    if (o.id == "6") o.durW = 0.4;
  }
  p.compute();
  auto cmp = compareBaseline(bl.value(), p.issues, p.comp, p.cal);
  REQUIRE(cmp.ok());
  REQUIRE(cmp.value().sum.late >= 1);
  REQUIRE(cmp.value().sum.early >= 1);

  ExportOptions o;
  o.list = false;
  ExportInput in = p.input();
  in.baseline = &cmp.value();
  const std::string h = exportHtml(in, o);
  // ガント: 計画のバー（全行ぶん）と、ずれた分（遅れは「!」と「+」）
  CHECK(count(h, "class=\"plan\"") == p.issues.size());
  const BaselineEntry& c4 = cmp.value().entries[static_cast<std::size_t>(p.idx("4"))];
  REQUIRE(c4.state == BaselineState::Late);
  CHECK(contains(h, ">! +" + std::to_string(c4.finishDiff) + "日<"));
  const BaselineEntry& c6 = cmp.value().entries[static_cast<std::size_t>(p.idx("6"))];
  REQUIRE(c6.state == BaselineState::Early);
  CHECK(contains(h, ">-" + std::to_string(-c6.finishDiff) + "日<"));
  CHECK(contains(h, "キックオフ"));       // 凡例・注記にベースライン名
  // 統計: 節と遅れの上位
  CHECK(contains(h, "計画との差"));
  CHECK(contains(h, "遅れの大きい順"));
  CHECK(onlyKnownTags(h));
  CHECK(text::isValidUtf8(h));

  // ベースラインがなければ何も出ない
  const std::string plain = exportHtml(p.input(), o);
  CHECK(count(plain, "class=\"plan\"") == 0);
  CHECK_FALSE(contains(plain, "計画との差"));

  // ダークでも出る。件名の < は実体参照
  o.dark = true;
  CHECK(count(exportHtml(in, o), "class=\"plan\"") == p.issues.size());
}

TEST_CASE("report: ベースラインの名前・件名の HTML はエスケープされ、課題の数が合わない比較は無視する") {
  Proj base = Proj::fromCsv(kBasic);
  auto bl = captureBaseline(base.issues, base.comp, "<script>x</script>", "2026-10-19T09:30");
  REQUIRE(bl.ok());
  Proj p = base;
  p.issues.erase(p.issues.begin() + 5);  // 単独の課題を消す
  p.compute();
  auto cmp = compareBaseline(bl.value(), p.issues, p.comp, p.cal);
  REQUIRE(cmp.ok());
  ExportInput in = p.input();
  in.baseline = &cmp.value();
  ExportOptions o;
  o.list = false;
  const std::string h = exportHtml(in, o);
  CHECK_FALSE(contains(h, "<script>"));
  CHECK(contains(h, "&lt;script&gt;"));
  CHECK(contains(h, "消えた"));
  CHECK(onlyKnownTags(h));
  // 課題の数が合わない比較（古い比較を渡してしまった）は使わない
  Proj q = base;
  q.issues.erase(q.issues.begin() + 3, q.issues.end());
  q.compute();
  ExportInput stale = q.input();
  stale.baseline = &cmp.value();  // 5 件の課題に対する比較
  const std::string hs = exportHtml(stale, o);
  CHECK(count(hs, "class=\"plan\"") == 0);
  CHECK_FALSE(contains(hs, "計画との差"));
}
