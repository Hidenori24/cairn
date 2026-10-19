#include <doctest/doctest.h>

#include "../test_util.hpp"
#include "roadmap/csv.hpp"
#include "roadmap/schedule.hpp"

using namespace roadmap;
using testutil::D;

namespace {
csv::ImportResult imp(const std::string& text) {
  auto r = csv::importCsv(text);
  if (!r.ok()) FAIL(r.error());
  return std::move(r).value();
}
}  // namespace

TEST_CASE("csv: 字句解析（引用符・改行・BOM・混在した改行）") {
  auto t = csv::parse("\xEF\xBB\xBF" "a,b,c\r\n1,\"x,y\",\"he said \"\"hi\"\"\"\n2,\"line1\nline2\",z\r3,,\n\n , ,\n4");
  REQUIRE(t.header.size() == 3);
  CHECK(t.header[0] == "a");
  REQUIRE(t.rows.size() == 4);
  CHECK(t.rows[0][1] == "x,y");
  CHECK(t.rows[0][2] == "he said \"hi\"");
  CHECK(t.rows[1][1] == "line1\nline2");
  CHECK(t.rows[2][0] == "3");
  CHECK(t.rows[3].size() == 1);  // 末尾に改行なし・列数が不揃い
  CHECK(t.rows[3][0] == "4");
}

TEST_CASE("csv: 区切りの自動判定（タブ・セミコロン）") {
  auto t = csv::parse("a\tb\tc\n1\t2\t3\n");
  REQUIRE(t.header.size() == 3);
  CHECK(t.rows[0][2] == "3");
  auto s = csv::parse("a;b;c,d\n1;2;3\n");
  REQUIRE(s.header.size() == 3);
  CHECK(s.header[2] == "c,d");
}

TEST_CASE("csv: 巨大なセル") {
  std::string big(200000, 'x');
  auto t = csv::parse("a\n\"" + big + "\"\n");
  REQUIRE(t.rows.size() == 1);
  CHECK(t.rows[0][0].size() == big.size());
}

TEST_CASE("csv: stringify") {
  CHECK(csv::stringify({{"a", "b,c", "d\"e", "f\ng"}}) == "a,\"b,c\",\"d\"\"e\",\"f\ng\"\r\n");
  CHECK(csv::stringify({}) == "\r\n");
}

TEST_CASE("csv: 見出しの正規化") {
  CHECK(csv::norm("  Issue_ID ") == "issue id");
  CHECK(csv::norm("Epic\xE3\x80\x80 Link") == "epic link");
}

TEST_CASE("csv: parseDate の各書式") {
  CHECK(csv::parseDate("2026-10-19") == D("2026-10-19"));
  CHECK(csv::parseDate(" 2026/1/5 10:30") == D("2026-01-05"));
  CHECK(csv::parseDate("2026.10.19") == D("2026-10-19"));
  CHECK(csv::parseDate("2026年10月19日") == D("2026-10-19"));
  CHECK(csv::parseDate("19/Oct/26 10:30 AM") == D("2026-10-19"));
  CHECK(csv::parseDate("19-October-2026") == D("2026-10-19"));
  CHECK(csv::parseDate("5 Jan, 2027") == D("2027-01-05"));
  CHECK(csv::parseDate("Oct 19, 2026") == D("2026-10-19"));
  CHECK(csv::parseDate("Oct. 9 2026") == D("2026-10-09"));
  CHECK_FALSE(csv::parseDate("2026-02-30"));
  CHECK_FALSE(csv::parseDate("19/Foo/26"));
  CHECK_FALSE(csv::parseDate("abc"));
  CHECK_FALSE(csv::parseDate(""));
  CHECK_FALSE(csv::parseDate("2026-123-4"));
}

TEST_CASE("csv: parseDur") {
  CHECK(*csv::parseDur("2w 3d", false) == doctest::Approx(2.6));
  CHECK(*csv::parseDur("1.5", true) == 1.5);
  CHECK(*csv::parseDur("1.5", false) == 1.5);
  CHECK(*csv::parseDur("40h", false) == doctest::Approx(1));
  CHECK(*csv::parseDur("3d", false) == doctest::Approx(0.6));
  CHECK(*csv::parseDur("144000", false) == doctest::Approx(1));  // 秒（JIRA）
  CHECK(*csv::parseDur("2週間", false) == 2);
  CHECK(*csv::parseDur("3日", false) == doctest::Approx(0.6));
  CHECK(*csv::parseDur("4時間", false) == doctest::Approx(0.1));
  CHECK(*csv::parseDur("1W", false) == 1);
  CHECK_FALSE(csv::parseDur("abc", false));
  CHECK_FALSE(csv::parseDur("", true));
}

TEST_CASE("csv: 日時・コメント") {
  CHECK(csv::parseDT("19/Oct/26 10:30 AM") == "2026-10-19T10:30");
  CHECK(csv::parseDT("19/Oct/26 12:05 AM") == "2026-10-19T00:05");
  CHECK(csv::parseDT("2026-10-19 1:30 pm") == "2026-10-19T13:30");
  CHECK(csv::parseDT("2026-10-19") == "2026-10-19T00:00");
  CHECK(csv::parseDT("x") == "");
  CHECK(csv::fmtDT("2026-10-19T10:30") == "19/Oct/26 10:30 AM");
  CHECK(csv::fmtDT("2026-10-19T00:05") == "19/Oct/26 12:05 AM");
  CHECK(csv::fmtDT("2026-10-19T13:30") == "19/Oct/26 01:30 PM");
  auto c = csv::parseComment("19/Oct/26 10:30 AM;田中 ;本文;続き");
  CHECK(c.at == "2026-10-19T10:30");
  CHECK(c.who == "田中");
  CHECK(c.text == "本文;続き");
  auto d = csv::parseComment("ただのメモ;です;よ");
  CHECK(d.at == "");
  CHECK(d.text == "ただのメモ;です;よ");
  CHECK(csv::fmtComment(c) == "19/Oct/26 10:30 AM;田中;本文;続き");
}

TEST_CASE("csv: 数値の書式") {
  CHECK(csv::fmtNum(1) == "1");
  CHECK(csv::fmtNum(0.2) == "0.2");
  CHECK(csv::fmtNum(2.5) == "2.5");
  CHECK(csv::fmtNum(1.005) == "1");  // 1.005*100 = 100.49999…
  CHECK(csv::fmtNum(0.125) == "0.13");
  CHECK(csv::fmtEst(std::nullopt) == "");
  CHECK(csv::fmtEst(0) == "");
  CHECK(csv::fmtEst(2) == "2w");
  CHECK(csv::fmtEst(0.2) == "1d");
  CHECK(csv::fmtEst(0.6) == "3d");
  CHECK(csv::fmtEst(0.01) == "1h");
  CHECK(csv::fmtEst(0.15) == "6h");
}

TEST_CASE("csv: 見出しの同義語がすべて対応づく") {
  struct Case {
    const char* header;
    const char* value;
    std::function<bool(const Issue&)> ok;
  };
  const Case cases[] = {
      {"Key", "K-1", [](const Issue& o) { return o.key == "K-1"; }},
      {"Issue key", "K-1", [](const Issue& o) { return o.key == "K-1"; }},
      {"課題キー", "K-1", [](const Issue& o) { return o.key == "K-1"; }},
      {"キー", "K-1", [](const Issue& o) { return o.key == "K-1"; }},
      {"Issue Type", "Bug", [](const Issue& o) { return o.type == "Bug"; }},
      {"IssueType", "Bug", [](const Issue& o) { return o.type == "Bug"; }},
      {"type", "Bug", [](const Issue& o) { return o.type == "Bug"; }},
      {"課題タイプ", "Bug", [](const Issue& o) { return o.type == "Bug"; }},
      {"Priority", "High", [](const Issue& o) { return o.prio == "High"; }},
      {"優先度", "High", [](const Issue& o) { return o.prio == "High"; }},
      {"Status", "Done", [](const Issue& o) { return o.status == "Done"; }},
      {"ステータス", "Done", [](const Issue& o) { return o.status == "Done"; }},
      {"Assignee", "鈴木", [](const Issue& o) { return o.assignee == "鈴木"; }},
      {"担当者", "鈴木", [](const Issue& o) { return o.assignee == "鈴木"; }},
      {"担当", "鈴木", [](const Issue& o) { return o.assignee == "鈴木"; }},
      {"Earliest start", "2026-11-02", [](const Issue& o) { return o.notBefore == D("2026-11-02"); }},
      {"着手可能日", "2026-11-02", [](const Issue& o) { return o.notBefore == D("2026-11-02"); }},
      {"Start no earlier than", "2026-11-02", [](const Issue& o) { return o.notBefore == D("2026-11-02"); }},
      {"Comment", "メモ", [](const Issue& o) { return o.comments.size() == 1 && o.comments[0].text == "メモ"; }},
      {"コメント", "メモ", [](const Issue& o) { return o.comments.size() == 1; }},
      {"Start date", "2026-11-02", [](const Issue& o) { return o.start == D("2026-11-02"); }},
      {"Custom field (Start date)", "2026-11-02", [](const Issue& o) { return o.start == D("2026-11-02"); }},
      {"開始日", "2026-11-02", [](const Issue& o) { return o.start == D("2026-11-02"); }},
      {"Due date", "2026-11-02", [](const Issue& o) { return o.due == D("2026-11-02"); }},
      {"Due", "2026-11-02", [](const Issue& o) { return o.due == D("2026-11-02"); }},
      {"期限日", "2026-11-02", [](const Issue& o) { return o.due == D("2026-11-02"); }},
      {"期限", "2026-11-02", [](const Issue& o) { return o.due == D("2026-11-02"); }},
      {"Deadline", "2026-11-02", [](const Issue& o) { return o.deadline == D("2026-11-02"); }},
      {"目標期限", "2026-11-02", [](const Issue& o) { return o.deadline == D("2026-11-02"); }},
      {"逆算期限", "2026-11-02", [](const Issue& o) { return o.deadline == D("2026-11-02"); }},
      {"Resolved", "2026-11-02", [](const Issue& o) { return o.resolved == D("2026-11-02"); }},
      {"解決日", "2026-11-02", [](const Issue& o) { return o.resolved == D("2026-11-02"); }},
      {"Resolution date", "2026-11-02", [](const Issue& o) { return o.resolved == D("2026-11-02"); }},
      {"Original estimate", "2w", [](const Issue& o) { return o.estW == 2.0; }},
      {"Original time estimate", "2w", [](const Issue& o) { return o.estW == 2.0; }},
      {"元の見積り", "2w", [](const Issue& o) { return o.estW == 2.0; }},
      {"初期見積り", "2w", [](const Issue& o) { return o.estW == 2.0; }},
      {"Remaining (weeks)", "0.5", [](const Issue& o) { return o.remW == 0.5; }},
      {"残り(週)", "0.5", [](const Issue& o) { return o.remW == 0.5; }},
      {"残り（週）", "0.5", [](const Issue& o) { return o.remW == 0.5; }},
      {"Remaining estimate", "1d", [](const Issue& o) { return o.remW == 0.2; }},
      {"残りの見積り", "1d", [](const Issue& o) { return o.remW == 0.2; }},
      {"Labels", "x", [](const Issue& o) { return o.labels == std::vector<std::string>{"x"}; }},
      {"ラベル", "x", [](const Issue& o) { return o.labels.size() == 1; }},
      {"Description", "説明文  ", [](const Issue& o) { return o.desc == "説明文"; }},
      {"説明", "説明文", [](const Issue& o) { return o.desc == "説明文"; }},
      {"Epic Name", "E", [](const Issue& o) { return o.epicName == "E"; }},
      {"Custom field (Epic Name)", "E", [](const Issue& o) { return o.epicName == "E"; }},
      {"エピック名", "E", [](const Issue& o) { return o.epicName == "E"; }},
      {"Duration (weeks)", "3", [](const Issue& o) { return o.durW == 3.0; }},
      {"期間(週)", "3", [](const Issue& o) { return o.durW == 3.0; }},
      {"期間（週）", "3", [](const Issue& o) { return o.durW == 3.0; }},
      {"duration (WEEKS)", "3", [](const Issue& o) { return o.durW == 3.0; }},
      {"Duration (weeks) @案A", "3", [](const Issue& o) { return o.alt.find("案A") && *o.alt.find("案A") == 3.0; }},
  };
  for (const auto& c : cases) {
    CAPTURE(c.header);
    auto r = imp(std::string("Summary,") + c.header + "\r\n件名," + c.value + "\r\n");
    REQUIRE(r.issues.size() == 1);
    CHECK(c.ok(r.issues[0]));
    CHECK(r.meta.extraHeaders.empty());
  }
  // id・件名・親の同義語
  for (const char* h : {"Issue id", "id", "課題ID"}) {
    auto r = imp(std::string(h) + ",件名\n7,a\n");
    CHECK(r.issues[0].id == "7");
  }
  for (const char* h : {"Summary", "件名", "要約"}) {
    auto r = imp(std::string("id,") + h + "\n1,a\n");
    CHECK(r.issues[0].summary == "a");
  }
  for (const char* h : {"Parent", "Parent id", "親", "親課題", "Epic Link", "Custom field (Epic Link)", "エピックリンク"}) {
    CAPTURE(h);
    auto r = imp(std::string("id,Summary,") + h + "\n1,p,\n2,c,1\n");
    CHECK(r.issues[1].parent == "1");
  }
  for (const char* h : {"Outward issue link (Blocks)", "外向き (block)"}) {
    auto r = imp(std::string("id,Summary,") + h + "\n1,a,2\n2,b,\n");
    CHECK(r.issues[0].blocks == std::vector<std::string>{"2"});
  }
  for (const char* h : {"Inward issue link (Blocks)", "内向き block"}) {
    auto r = imp(std::string("id,Summary,") + h + "\n1,a,2\n2,b,\n");
    CHECK(r.issues[1].blocks == std::vector<std::string>{"1"});
  }
}

TEST_CASE("csv: 同じ見出しの複数列・未知の列の保持と書き戻し") {
  auto r = imp("Issue Id,Summary,Labels,Labels,Comment,Comment,Outward issue link (Blocks),Outward issue link (Blocks),Sprint,Team\n"
               "1,a,x,y,2026-10-01;me;c1,2026-10-02;you;c2,2,3,S1,T\n2,b,,,,,,,,\n3,c,,,,,,,S3,\n");
  CHECK(r.issues[0].labels == std::vector<std::string>{"x", "y"});
  CHECK(r.issues[0].comments.size() == 2);
  CHECK(r.issues[0].blocks == std::vector<std::string>{"2", "3"});
  CHECK(r.meta.extraHeaders == std::vector<std::string>{"Sprint", "Team"});
  CHECK(r.issues[2].extra == std::vector<std::string>{"S3", ""});
  const std::string out = exportAppCsv(r.issues, r.meta);
  CHECK(out.find(",Sprint,Team\r\n") != std::string::npos);
  CHECK(out.find(",S1,T\r\n") != std::string::npos);
  // 往復
  auto r2 = imp(out);
  CHECK(exportAppCsv(r2.issues, r2.meta) == out);
}

TEST_CASE("csv: 左から最初に値のある列を使う") {
  auto r = imp("Summary,Status,Status\na,,Done\n");
  CHECK(r.issues[0].status == "Done");
}

TEST_CASE("csv: 取り込みの警告とエラー") {
  CHECK_FALSE(csv::importCsv("a,b\n1,2\n").ok());
  CHECK(csv::importCsv("a,b\n1,2\n").error() == "「Summary」列が見つかりません。1行目に列名があるCSVを読み込んでください。");
  CHECK(csv::importCsv("Summary\n").error() == "課題の行が見つかりません。");
  auto r = imp("Issue Id,Summary,Parent,Start date,Outward issue link (Blocks)\n"
               "1,a,,bad,9\n1,b,1,,\n,c,99,,\n5,,,,\n");
  REQUIRE(r.issues.size() == 3);
  CHECK(r.issues[1].id == "2");  // 重複は採番
  CHECK(r.issues[2].id == "3");  // 空も採番
  CHECK(r.issues[1].parent == "1");
  REQUIRE(r.warns.size() == 4);
  CHECK(r.warns[0] == "Issue Id が重複している行が1件あり、別のIDを振りました");
  CHECK(r.warns[1] == "参照先が見つからず無視: c → 親 99, a → 9");
  CHECK(r.warns[2] == "Summary が空の行を1件、読み飛ばしました");
  CHECK(r.warns[3] == "日付として読めない値が1件（Start date列）");
}

TEST_CASE("csv: 親の循環を切る・Epic は件名と Epic Name でも引ける") {
  auto r = imp("Issue Id,Summary,Parent,Issue Type,Epic Name,Epic Link\n1,a,2,Task,,\n2,b,1,Task,,\n3,E,,Epic,EN,\n4,t,,Task,,EN\n5,u,,Task,,E\n");
  CHECK(r.issues[0].parent == "");
  CHECK(r.issues[1].parent == "1");
  REQUIRE_FALSE(r.warns.empty());
  CHECK(r.warns[0] == "親子関係がループしていたため「a」の親を外しました");
  CHECK(r.issues[3].parent == "3");
  CHECK(r.issues[4].parent == "3");
}

TEST_CASE("csv: Shift_JIS のCSVを読み、UTF-8（BOMなし）で書き出す") {
  // "Summary\r\n件名\r\n" を CP932 で
  const std::string sjis = "Summary\r\n\x8C\x8F\x96\xBC\r\n";
  auto dec = encoding::decodeText(sjis);
  CHECK(dec.detected == encoding::Detected::Cp932);
  auto r = imp(dec.text);
  CHECK(r.issues[0].summary == "件名");
  const std::string out = exportAppCsv(r.issues, r.meta);
  CHECK(out.rfind("Issue Id,", 0) == 0);  // BOMなし
  CHECK(out.find("件名") != std::string::npos);
  CHECK(out.substr(out.size() - 2) == "\r\n");
}

TEST_CASE("csv: JIRA用の書き出しで Milestone にラベルを足す・深い階層をサブタスクに") {
  auto r = imp("Issue Id,Issue Type,Summary,Parent,Labels,Duration (weeks)\n"
               "1,Epic,E,,,\n2,Story,S,1,,\n3,Task,T,2,,\n4,Task,U,3,,1\n5,Milestone,M,1,,0\n");
  ScheduleOptions opt;
  opt.base = D("2026-10-19");
  auto c = computeAll(r.issues, r.meta, Calendar(), opt);
  REQUIRE(c.ok());
  const std::string j = exportJiraCsv(r.issues, r.meta, &c.value());
  CHECK(j.find("4,Sub-task,U,2,") != std::string::npos);
  CHECK(j.find("[元の階層] T") != std::string::npos);
  CHECK(j.find("5,Task,M,1,") != std::string::npos);
  CHECK(j.find(",Milestone,") != std::string::npos);
  // 取り込み直すと Milestone（ラベル）として扱われる
  auto back = imp(j);
  auto c2 = computeAll(back.issues, back.meta, Calendar(), opt);
  REQUIRE(c2.ok());
  CHECK(c2->T.kind[c2->T.find("5")] == Kind::Ms);
}
