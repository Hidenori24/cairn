// GitHub の PR・Issue のリンクと、閉じたら完了の提案。Issue #61
#include <doctest/doctest.h>

#include <string>

#include "roadmap/github_ref.hpp"
#include "roadmap/links.hpp"
#include "roadmap/model.hpp"

using namespace roadmap;

namespace {
Result<GitHubRef> P(const std::string& s) { return parseGitHubUrl(s); }
bool rejects(const std::string& s) { return !parseGitHubUrl(s).ok(); }
std::string canon(const std::string& s) {
  auto r = parseGitHubUrl(s);
  return r.ok() ? r->canonicalUrl() : std::string("<NG>");
}
Link mkLink(const std::string& target) {
  Link l;
  l.title = "t";
  l.target = target;
  return l;
}
const char* const kPr1 = "https://github.com/acme/app/pull/1";
const char* const kPr2 = "https://github.com/acme/app/pull/2";
const char* const kIs3 = "https://github.com/acme/app/issues/3";
RemoteInfo ri(RemoteState s, const std::string& at = "") {
  RemoteInfo r;
  r.state = s;
  r.closedAt = at;
  return r;
}
Issue openIssue() {
  Issue o;
  o.id = "7";
  o.status = "In Progress";
  return o;
}
}  // namespace

// ---- URL の検証 ----

TEST_CASE("parseGitHubUrl: PR と Issue の URL を分解して正規化する") {
  auto a = P("https://github.com/acme/app/pull/12");
  REQUIRE(a.ok());
  CHECK(a->owner == "acme");
  CHECK(a->repo == "app");
  CHECK(a->kind == GitHubKind::Pull);
  CHECK(a->number == 12);
  CHECK(a->canonicalUrl() == "https://github.com/acme/app/pull/12");
  auto b = P("https://github.com/acme/app/issues/345");
  REQUIRE(b.ok());
  CHECK(b->kind == GitHubKind::Issue);
  CHECK(b->number == 345);
  CHECK(b->canonicalUrl() == "https://github.com/acme/app/issues/345");
  // owner・repo は英数字と - _ .
  CHECK(canon("https://github.com/Hide-nori_24/task.management_tool/pull/7") == "https://github.com/Hide-nori_24/task.management_tool/pull/7");
  CHECK(canon("https://github.com/o/.github/pull/7") == "https://github.com/o/.github/pull/7");
  // 番号は 9 桁まで
  CHECK(canon("https://github.com/o/r/pull/999999999") == "https://github.com/o/r/pull/999999999");
  CHECK(canon("https://github.com/o/r/pull/1") == "https://github.com/o/r/pull/1");
}

TEST_CASE("parseGitHubUrl: 末尾の /・?…・#…・下位パスは許容して正規化する") {
  const std::string want = "https://github.com/o/r/pull/1";
  CHECK(canon("https://github.com/o/r/pull/1/") == want);
  CHECK(canon("https://github.com/o/r/pull/1#issuecomment-123") == want);
  CHECK(canon("https://github.com/o/r/pull/1?diff=split") == want);
  CHECK(canon("https://github.com/o/r/pull/1/?x=1#y") == want);
  CHECK(canon("https://github.com/o/r/pull/1/files") == want);
  CHECK(canon("https://github.com/o/r/pull/1/files#diff-abc") == want);
  CHECK(canon("https://github.com/o/r/pull/1/commits") == want);
  CHECK(canon("https://github.com/o/r/pull/1/commits/0a1b2c3d") == want);
  CHECK(canon("https://github.com/o/r/pull/1/checks?check_run_id=9") == want);
  CHECK(canon("https://github.com/o/r/issues/5/") == "https://github.com/o/r/issues/5");
  CHECK(canon("https://github.com/o/r/issues/5#issuecomment-1") == "https://github.com/o/r/issues/5");
  // 大文字小文字: スキームとホストは区別しない（正規化で小文字にする）。owner・repo は入力のまま
  CHECK(canon("HTTPS://GitHub.com/Owner/Repo/pull/1") == "https://github.com/Owner/Repo/pull/1");
  CHECK(canon("https://GITHUB.COM/o/r/issues/1") == "https://github.com/o/r/issues/1");
}

TEST_CASE("parseGitHubUrl: 許可リスト以外のホスト・形を断る") {
  CHECK(rejects("http://github.com/o/r/pull/1"));              // http は断る
  CHECK(rejects("https://www.github.com/o/r/pull/1"));         // www も断る
  CHECK(rejects("https://github.com.evil.example/o/r/pull/1"));
  CHECK(rejects("https://evilgithub.com/o/r/pull/1"));
  CHECK(rejects("https://github.example.com/o/r/pull/1"));     // Enterprise のホストは対象外
  CHECK(rejects("https://github.com@evil.example/o/r/pull/1"));
  CHECK(rejects("https://user@github.com/o/r/pull/1"));
  CHECK(rejects("https://user:pw@github.com/o/r/pull/1"));
  CHECK(rejects("https://github.com:444/o/r/pull/1"));
  CHECK(rejects("https://github.com:443/o/r/pull/1"));
  CHECK(rejects("https://github.com./o/r/pull/1"));
  CHECK(rejects("https://github.com"));
  CHECK(rejects("https://github.com/"));
  CHECK(rejects("https://github.com/o"));
  CHECK(rejects("https://github.com/o/r"));
  CHECK(rejects("https://github.com/o/r/pull"));
  CHECK(rejects("https://github.com/o/r/pull/"));
  CHECK(rejects("https://github.com/o/r/pulls"));               // 一覧
  CHECK(rejects("https://github.com/o/r/issues"));
  CHECK(rejects("https://github.com/o/r/discussions/1"));
  CHECK(rejects("https://github.com/o/r/commit/abc"));
  CHECK(rejects("https://github.com/o/r/Pull/1"));              // パスの語は小文字だけ
  CHECK(rejects("https://github.com/o/r/PULL/1"));
  CHECK(rejects("ftp://github.com/o/r/pull/1"));
  CHECK(rejects("javascript:alert(1)"));
  CHECK(rejects("//github.com/o/r/pull/1"));
  CHECK(rejects("github.com/o/r/pull/1"));
  CHECK(rejects(""));
  CHECK(rejects("C:\\a\\b"));
}

TEST_CASE("parseGitHubUrl: 番号の検査（0・先頭 0・桁あふれ・数字以外）") {
  CHECK(rejects("https://github.com/o/r/pull/0"));
  CHECK(rejects("https://github.com/o/r/pull/00"));
  CHECK(rejects("https://github.com/o/r/pull/01"));
  CHECK(rejects("https://github.com/o/r/pull/1000000000"));      // 10 桁
  CHECK(rejects("https://github.com/o/r/pull/12345678901"));
  CHECK(rejects("https://github.com/o/r/pull/99999999999999999999999"));
  CHECK(rejects("https://github.com/o/r/pull/-1"));
  CHECK(rejects("https://github.com/o/r/pull/+1"));
  CHECK(rejects("https://github.com/o/r/pull/1a"));
  CHECK(rejects("https://github.com/o/r/pull/a1"));
  CHECK(rejects("https://github.com/o/r/pull/1.5"));
  CHECK(rejects("https://github.com/o/r/pull/0x10"));
  CHECK(rejects("https://github.com/o/r/pull/%31"));
  CHECK(rejects("https://github.com/o/r/pull/１"));              // 全角
  CHECK(rejects("https://github.com/o/r/issues/1;calc"));
  CHECK(rejects("https://github.com/o/r/issues/01"));
}

TEST_CASE("parseGitHubUrl: 下位パス・パスの区切りの不正を断る") {
  CHECK(rejects("https://github.com/o/r/pull/1//"));
  CHECK(rejects("https://github.com/o/r/pull/1//files"));
  CHECK(rejects("https://github.com//r/pull/1"));
  CHECK(rejects("https://github.com/o//pull/1"));
  CHECK(rejects("https://github.com/o/r//pull/1"));
  CHECK(rejects("https://github.com/o/r/pull/1/.."));
  CHECK(rejects("https://github.com/o/r/pull/1/../2"));
  CHECK(rejects("https://github.com/o/r/pull/1/./files"));
  CHECK(rejects("https://github.com/../r/pull/1"));
  CHECK(rejects("https://github.com/o/../pull/1"));
  CHECK(rejects("https://github.com/./r/pull/1"));
  CHECK(rejects("https://github.com/o/r/../r/pull/1"));
  CHECK(rejects("https://github.com/o/r/pull/1/files/a..b"));
  CHECK(rejects("https://github.com/o/r/pull/1/%2e%2e/2"));       // エンコードされた ..
  CHECK(rejects("https://github.com/o/r/pull/1%2Ffiles"));        // エンコードされた区切り
  CHECK(rejects("https://github.com/o%2Fx/r/pull/1"));
  CHECK(rejects("https://github.com/o/r%2f/pull/1"));
  CHECK(rejects("https://github.com/o%5Cx/r/pull/1"));
  CHECK(rejects("https://github.com/o/r/pull/1/files%20x"));
  CHECK(rejects("https://github.com/o\\x/r/pull/1"));             // バックスラッシュ
  CHECK(rejects("https://github.com/o/r/pull/1/fi\\les"));
  CHECK(rejects("https://github.com\\@evil.example/o/r/pull/1"));
}

TEST_CASE("parseGitHubUrl: 空白・制御文字・ASCII 以外・長すぎる名前を断る") {
  CHECK(rejects(" https://github.com/o/r/pull/1"));
  CHECK(rejects("https://github.com/o/r/pull/1 "));
  CHECK(rejects("https://github.com/o /r/pull/1"));
  CHECK(rejects("https://github.com/o/r/pull/1 /files"));
  CHECK(rejects("https://github.com/o/r/pull/1\n"));
  CHECK(rejects("https://github.com/o/r/pull/1\r\n"));
  CHECK(rejects("https://github.com/o/r/pull/1\t"));
  CHECK(rejects(std::string("https://github.com/o/r/pull/1\0x", 31)));
  CHECK(rejects("https://github.com/o/r/pull/1\x7f"));
  CHECK(rejects("https://github.com/o/r/pull/1#a b"));
  CHECK(rejects("https://github.com/o/r/pull/1?x=\n"));
  CHECK(rejects("https://github.com/日本語/r/pull/1"));
  CHECK(rejects("https://github.com/o/リポ/pull/1"));
  CHECK(rejects("https://github.com/o/r/pull/1#日本語"));
  CHECK(rejects("https://github.com/o/r/pull/1\xE2\x80\xAE"));    // 方向を上書きする制御文字
  // 英数字・- _ . 以外
  for (const char* bad : {"o$", "o&", "o;", "o|", "o`", "o'", "o\"", "o(", "o)", "o<", "o>", "o^", "o!", "o~", "o+", "o=", "o,", "o@", "o:", "o*", "o?"}) {
    CHECK_MESSAGE(rejects(std::string("https://github.com/") + bad + "/r/pull/1"), bad);
    CHECK_MESSAGE(rejects(std::string("https://github.com/o/") + bad + "/pull/1"), bad);
  }
  // 長さの上限は 100
  const std::string n100(100, 'a'), n101(101, 'a');
  CHECK(!rejects("https://github.com/" + n100 + "/" + n100 + "/pull/1"));
  CHECK(rejects("https://github.com/" + n101 + "/r/pull/1"));
  CHECK(rejects("https://github.com/o/" + n101 + "/pull/1"));
  // URL 全体の長さ
  CHECK(rejects("https://github.com/o/r/pull/1#" + std::string(3000, 'a')));
}

TEST_CASE("parseGitHubUrl: シェルのメタ文字を含む URL を断る") {
  for (const char* meta : {";calc", "&calc", "|calc", "`calc`", "$(calc)", "%26calc", "&&calc", ">x", "<x", "^x", "\"x"}) {
    CHECK_MESSAGE(rejects(std::string("https://github.com/o/r/pull/1") + meta), meta);
    CHECK_MESSAGE(rejects(std::string("https://github.com/o") + meta + "/r/pull/1"), meta);
    CHECK_MESSAGE(rejects(std::string("https://github.com/o/r") + meta + "/pull/1"), meta);
  }
}

TEST_CASE("parseGitHubUrl: 断る理由に入力の中身を入れない") {
  auto r = P("https://github.com/secret-owner/secret-repo/pull/0");
  REQUIRE(!r.ok());
  CHECK(r.error().find("secret") == std::string::npos);
  auto r2 = P("https://evil.example/secret-owner/secret-repo/pull/1");
  REQUIRE(!r2.ok());
  CHECK(r2.error().find("secret") == std::string::npos);
  CHECK(r2.error().find("evil") == std::string::npos);
}

TEST_CASE("validateGitHubRef / ghArguments: URL を 1 つの引数として渡す。メタ文字は断る") {
  auto pr = P("https://github.com/acme/app/pull/12");
  REQUIRE(pr.ok());
  auto a = ghArguments(*pr);
  REQUIRE(a.ok());
  const std::vector<std::string> wantPr = {"pr", "view", "https://github.com/acme/app/pull/12", "--json", "state,isDraft,mergedAt,closedAt"};
  CHECK(*a == wantPr);
  auto is = P("https://github.com/acme/app/issues/3/?x=1#y");
  REQUIRE(is.ok());
  auto b = ghArguments(*is);
  REQUIRE(b.ok());
  const std::vector<std::string> wantIs = {"issue", "view", "https://github.com/acme/app/issues/3", "--json", "state,stateReason,closedAt"};
  CHECK(*b == wantIs);
  // 元の URL の末尾（?・#・下位パス）は渡さない
  auto c = P("https://github.com/acme/app/pull/12/files?x=%20y#diff");
  REQUIRE(c.ok());
  CHECK((*ghArguments(*c))[2] == "https://github.com/acme/app/pull/12");
  // 手で作った不正な参照は引数にしない
  GitHubRef bad = *pr;
  for (const char* o : {"a b", "a&b", "a;b", "a|b", "a\"b", "a%b", "a^b", "a`b", "a$b", "a\nb", "", "a/b", "a\\b", "..", "a..b"}) {
    bad.owner = o;
    CHECK_MESSAGE(!ghArguments(bad).ok(), o);
    CHECK(!validateGitHubRef(bad).ok());
  }
  bad = *pr;
  bad.repo = "r&calc";
  CHECK(!ghArguments(bad).ok());
  bad = *pr;
  bad.number = 0;
  CHECK(!ghArguments(bad).ok());
  bad.number = 1000000000;
  CHECK(!ghArguments(bad).ok());
  bad = *pr;
  bad.owner = std::string(101, 'a');
  CHECK(!ghArguments(bad).ok());
  CHECK(validateGitHubRef(*pr).ok());
}

TEST_CASE("windowsCommandLine: 引数を 1 つずつ引用符にくるみ、シェルを通さない") {
  const std::string url = "https://github.com/acme/app/pull/12";
  const std::string line = windowsCommandLine("C:\\Program Files\\GitHub CLI\\gh.exe", {"pr", "view", url, "--json", "state,isDraft,mergedAt,closedAt"});
  CHECK(line == "\"C:\\Program Files\\GitHub CLI\\gh.exe\" pr view " + url + " --json state,isDraft,mergedAt,closedAt");
  CHECK(line.find("cmd") == std::string::npos);
  CHECK(windowsCommandLine("gh.exe", {"a b"}) == "gh.exe \"a b\"");
  CHECK(windowsCommandLine("gh.exe", {""}) == "gh.exe \"\"");
  CHECK(windowsCommandLine("gh.exe", {"a\"b"}) == "gh.exe \"a\\\"b\"");
  CHECK(windowsCommandLine("gh.exe", {"a b\\"}) == "gh.exe \"a b\\\\\"");
  CHECK(windowsCommandLine("gh.exe", {"a\\\"b"}) == "gh.exe \"a\\\\\\\"b\"");
  CHECK(windowsCommandLine("gh.exe", {}) == "gh.exe");
}

// ---- classifyLink の GitHub 判定 ----

TEST_CASE("classifyLink: GitHub の PR・Issue は専用の種類。Web として開けることは変わらない") {
  const LinkClass pr = classifyLink("https://github.com/acme/app/pull/12");
  CHECK(pr.kind == LinkKind::GitHubPull);
  CHECK(pr.ok());
  CHECK(pr.target == "https://github.com/acme/app/pull/12");
  const LinkClass is = classifyLink("  \"https://github.com/acme/app/issues/3/\"  ");
  CHECK(is.kind == LinkKind::GitHubIssue);
  CHECK(is.ok());
  CHECK(is.target == "https://github.com/acme/app/issues/3/");  // 保存するのは入力のまま（正規化は取得のときだけ）
  CHECK(classifyLink("https://github.com/acme/app/pull/12/files#diff").kind == LinkKind::GitHubPull);
  CHECK(linkKindOpenable(LinkKind::GitHubPull));
  CHECK(linkKindOpenable(LinkKind::GitHubIssue));
  CHECK(std::string(linkKindLabel(LinkKind::GitHubPull)) == "GitHub PR");
  CHECK(std::string(linkKindLabel(LinkKind::GitHubIssue)) == "GitHub Issue");
  // GitHub でも PR・Issue の形でないものは今までどおり Web
  CHECK(classifyLink("https://github.com/acme/app").kind == LinkKind::Web);
  CHECK(classifyLink("https://github.com/acme/app/pulls").kind == LinkKind::Web);
  CHECK(classifyLink("https://github.com/acme/app/pull/0").kind == LinkKind::Web);
  CHECK(classifyLink("http://github.com/acme/app/pull/1").kind == LinkKind::Web);
  CHECK(classifyLink("https://www.github.com/acme/app/pull/1").kind == LinkKind::Web);
  CHECK(classifyLink("https://github.com.evil.example/acme/app/pull/1").kind == LinkKind::Web);
  CHECK(classifyLink("https://example.com/acme/app/pull/1").kind == LinkKind::Web);
  // 既存の判定は変わらない
  CHECK(classifyLink("https://example.com/a/b").kind == LinkKind::Web);
  CHECK(classifyLink("\\\\server\\share\\a.xlsx").kind == LinkKind::File);
  CHECK(classifyLink("javascript:alert(1)").kind == LinkKind::Rejected);
  // 表示名は Web と同じ
  CHECK(defaultTitle("https://github.com/Hidenori24/task_management_tool/issues/60") == "github.com/issues/60");
  CHECK(defaultTitle("https://github.com/acme/app/pull/12") == "github.com/pull/12");
  // 重複の判定・警告の判定も Web と同じ
  CHECK(sameLinkTarget("https://github.com/acme/app/pull/12", "https://github.com/acme/app/pull/12"));
  CHECK(!isRiskyTarget("https://github.com/acme/app/pull/12"));
}

TEST_CASE("リンクの保存: GitHub のリンクも往復し、未知のキーを壊さない") {
  LinkMap m;
  m["7"] = {mkLink("https://github.com/acme/app/pull/12"), mkLink("https://github.com/acme/app/issues/3")};
  m["7"][0].extra = nlohmann::ordered_json::object();
  m["7"][0].extra["futureKey"] = 5;
  const auto j = linksToJson(m);
  const LinkMap back = linksFromJson(j);
  REQUIRE(back.count("7") == 1);
  REQUIRE(back.at("7").size() == 2);
  CHECK(back.at("7")[0].target == "https://github.com/acme/app/pull/12");
  CHECK(back.at("7")[0].extra.value("futureKey", 0) == 5);
  CHECK(classifyLink(back.at("7")[1].target).kind == LinkKind::GitHubIssue);
  // 状態の確認結果は保存しない
  CHECK(j.dump().find("state") == std::string::npos);
  CHECK(j.dump().find("closedAt") == std::string::npos);
}

// ---- gh の JSON ----

TEST_CASE("parseGhPrJson: open / draft / merged / closed の全組合せ") {
  RemoteInfo r = parseGhPrJson(R"({"state":"OPEN","isDraft":false,"mergedAt":null,"closedAt":null})");
  CHECK(r.state == RemoteState::Open);
  CHECK(r.closedAt.empty());
  r = parseGhPrJson(R"({"state":"OPEN","isDraft":true,"mergedAt":null,"closedAt":null})");
  CHECK(r.state == RemoteState::Draft);
  // gh は未設定の日時をゼロ値で出すことがある
  r = parseGhPrJson(R"({"state":"OPEN","isDraft":false,"mergedAt":"0001-01-01T00:00:00Z","closedAt":"0001-01-01T00:00:00Z"})");
  CHECK(r.state == RemoteState::Open);
  CHECK(r.closedAt.empty());
  r = parseGhPrJson(R"({"state":"MERGED","isDraft":false,"mergedAt":"2026-10-20T03:04:05Z","closedAt":"2026-10-20T03:04:05Z"})");
  CHECK(r.state == RemoteState::Merged);
  CHECK(r.closedAt == "2026-10-20T03:04:05Z");
  // mergedAt が closedAt と違えば mergedAt を採る
  r = parseGhPrJson(R"({"state":"MERGED","isDraft":false,"mergedAt":"2026-10-20T03:04:05Z","closedAt":"2026-10-21T00:00:00Z"})");
  CHECK(r.state == RemoteState::Merged);
  CHECK(r.closedAt == "2026-10-20T03:04:05Z");
  // MERGED で mergedAt がなければ closedAt
  r = parseGhPrJson(R"({"state":"MERGED","isDraft":false,"mergedAt":null,"closedAt":"2026-10-21T00:00:00Z"})");
  CHECK(r.state == RemoteState::Merged);
  CHECK(r.closedAt == "2026-10-21T00:00:00Z");
  // マージなしで閉じた
  r = parseGhPrJson(R"({"state":"CLOSED","isDraft":false,"mergedAt":null,"closedAt":"2026-10-22T09:00:00Z"})");
  CHECK(r.state == RemoteState::ClosedUnmerged);
  CHECK(r.closedAt == "2026-10-22T09:00:00Z");
  // CLOSED でも mergedAt があればマージ済み
  r = parseGhPrJson(R"({"state":"CLOSED","isDraft":false,"mergedAt":"2026-10-20T03:04:05Z","closedAt":"2026-10-22T09:00:00Z"})");
  CHECK(r.state == RemoteState::Merged);
  CHECK(r.closedAt == "2026-10-20T03:04:05Z");
  // 閉じた draft（isDraft が true でも、閉じていれば閉じたもの）
  r = parseGhPrJson(R"({"state":"CLOSED","isDraft":true,"mergedAt":null,"closedAt":"2026-10-22T09:00:00Z"})");
  CHECK(r.state == RemoteState::ClosedUnmerged);
  // 知らないキーは無視する
  r = parseGhPrJson(R"({"state":"OPEN","isDraft":false,"title":"秘密","extra":[1,2,{"a":null}]})");
  CHECK(r.state == RemoteState::Open);
  CHECK(r.reason.empty());
}

TEST_CASE("parseGhIssueJson: open / closed（完了）/ closed（計画外）の全組合せ") {
  RemoteInfo r = parseGhIssueJson(R"({"state":"OPEN","stateReason":"","closedAt":null})");
  CHECK(r.state == RemoteState::Open);
  r = parseGhIssueJson(R"({"state":"OPEN","stateReason":null,"closedAt":null})");
  CHECK(r.state == RemoteState::Open);
  r = parseGhIssueJson(R"({"state":"OPEN","stateReason":"REOPENED","closedAt":"0001-01-01T00:00:00Z"})");
  CHECK(r.state == RemoteState::Open);
  CHECK(r.closedAt.empty());
  r = parseGhIssueJson(R"({"state":"OPEN","closedAt":null})");  // stateReason がなくても open
  CHECK(r.state == RemoteState::Open);
  r = parseGhIssueJson(R"({"state":"CLOSED","stateReason":"COMPLETED","closedAt":"2026-10-21T10:00:00Z"})");
  CHECK(r.state == RemoteState::ClosedCompleted);
  CHECK(r.closedAt == "2026-10-21T10:00:00Z");
  r = parseGhIssueJson(R"({"state":"CLOSED","stateReason":"NOT_PLANNED","closedAt":"2026-10-21T10:00:00Z"})");
  CHECK(r.state == RemoteState::ClosedNotPlanned);
  CHECK(r.closedAt == "2026-10-21T10:00:00Z");
}

TEST_CASE("parseGh*Json: 壊れた JSON・欠け・未知の値は Unknown と理由") {
  const auto bad = [](const RemoteInfo& r) { return r.state == RemoteState::Unknown && !r.reason.empty() && r.closedAt.empty(); };
  // 壊れた JSON
  for (const char* s : {"", "   ", "{", "}", "[]", "null", "42", "\"OPEN\"", "{\"state\":", "{\"state\":\"OPEN\"", "not json", "{'state':'OPEN'}",
                        "{\"state\":\"OPEN\"}garbage", "\xff\xfe"}) {
    CHECK_MESSAGE(bad(parseGhPrJson(s)), s);
    CHECK_MESSAGE(bad(parseGhIssueJson(s)), s);
  }
  // 欠け
  CHECK(bad(parseGhPrJson("{}")));
  CHECK(bad(parseGhPrJson(R"({"isDraft":false})")));
  CHECK(bad(parseGhPrJson(R"({"state":"OPEN"})")));  // isDraft がない
  CHECK(bad(parseGhPrJson(R"({"state":"OPEN","isDraft":"no"})")));
  CHECK(bad(parseGhPrJson(R"({"state":"CLOSED","isDraft":false,"mergedAt":null})")));  // 閉じた日がない
  CHECK(bad(parseGhPrJson(R"({"state":"CLOSED","isDraft":false,"mergedAt":null,"closedAt":null})")));
  CHECK(bad(parseGhPrJson(R"({"state":"CLOSED","isDraft":false,"mergedAt":null,"closedAt":"0001-01-01T00:00:00Z"})")));
  CHECK(bad(parseGhPrJson(R"({"state":"MERGED","isDraft":false,"mergedAt":null,"closedAt":null})")));
  CHECK(bad(parseGhIssueJson("{}")));
  CHECK(bad(parseGhIssueJson(R"({"closedAt":null})")));
  CHECK(bad(parseGhIssueJson(R"({"state":"CLOSED","stateReason":"COMPLETED"})")));          // 閉じた日がない
  CHECK(bad(parseGhIssueJson(R"({"state":"CLOSED","closedAt":"2026-10-21T10:00:00Z"})")));  // 理由がない
  CHECK(bad(parseGhIssueJson(R"({"state":"CLOSED","stateReason":"","closedAt":"2026-10-21T10:00:00Z"})")));
  CHECK(bad(parseGhIssueJson(R"({"state":"CLOSED","stateReason":null,"closedAt":"2026-10-21T10:00:00Z"})")));
  // 未知の値
  CHECK(bad(parseGhPrJson(R"({"state":"WEIRD","isDraft":false})")));
  CHECK(bad(parseGhPrJson(R"({"state":"open","isDraft":false})")));  // 小文字
  CHECK(bad(parseGhPrJson(R"({"state":"","isDraft":false})")));
  CHECK(bad(parseGhPrJson(R"({"state":1,"isDraft":false})")));
  CHECK(bad(parseGhPrJson(R"({"state":null,"isDraft":false})")));
  CHECK(bad(parseGhPrJson(R"({"state":["OPEN"],"isDraft":false})")));
  CHECK(bad(parseGhIssueJson(R"({"state":"MERGED","closedAt":"2026-10-21T10:00:00Z"})")));  // Issue に MERGED はない
  CHECK(bad(parseGhIssueJson(R"({"state":"WEIRD"})")));
  CHECK(bad(parseGhIssueJson(R"({"state":"CLOSED","stateReason":"DUPLICATE","closedAt":"2026-10-21T10:00:00Z"})")));
  CHECK(bad(parseGhIssueJson(R"({"state":"CLOSED","stateReason":"WHATEVER","closedAt":"2026-10-21T10:00:00Z"})")));
  CHECK(bad(parseGhIssueJson(R"({"state":"CLOSED","stateReason":5,"closedAt":"2026-10-21T10:00:00Z"})")));
  CHECK(bad(parseGhPrJson(R"({"state":"OPEN","isDraft":false,"mergedAt":5})")));
  // 日時の形式が違う
  for (const char* at : {"yesterday", "2026-10-21", "2026-10-21T10:00:00", "2026-10-21 10:00:00Z", "2026-13-21T10:00:00Z", "2026-02-30T10:00:00Z",
                         "2026-10-21T25:00:00Z", "2026-10-21T10:61:00Z", "2026-10-21T10:00:61Z", "2026-10-21T10:00:00.123Z", "2026-10-21T10:00:00+09:00",
                         "２０２６-10-21T10:00:00Z", "9999999999-10-21T10:00:00Z"}) {
    CHECK_MESSAGE(bad(parseGhIssueJson(std::string(R"({"state":"CLOSED","stateReason":"COMPLETED","closedAt":")") + at + "\"}")), at);
    CHECK_MESSAGE(bad(parseGhPrJson(std::string(R"({"state":"CLOSED","isDraft":false,"mergedAt":null,"closedAt":")") + at + "\"}")), at);
  }
  // 理由に入力の中身を入れない
  const RemoteInfo r = parseGhPrJson(R"({"state":"SECRET-STATE-VALUE","isDraft":false})");
  CHECK(r.state == RemoteState::Unknown);
  CHECK(r.reason.find("SECRET") == std::string::npos);
}

TEST_CASE("parseGh*Json: 巨大な値・深い入れ子") {
  const std::string huge(kMaxGhOutputBytes, 'a');
  RemoteInfo r = parseGhPrJson(R"({"state":"OPEN","isDraft":false,"title":")" + huge + "\"}");
  CHECK(r.state == RemoteState::Unknown);
  CHECK(!r.reason.empty());
  r = parseGhIssueJson(R"({"state":"OPEN","title":")" + huge + "\"}");
  CHECK(r.state == RemoteState::Unknown);
  // ちょうど上限以内なら読む
  std::string pad = R"({"state":"OPEN","isDraft":false,"x":")";
  pad += std::string(kMaxGhOutputBytes - pad.size() - 2, 'a') + "\"}";
  REQUIRE(pad.size() == kMaxGhOutputBytes);
  CHECK(parseGhPrJson(pad).state == RemoteState::Open);
  // state に巨大な文字列
  CHECK(parseGhPrJson(R"({"state":")" + std::string(50000, 'x') + R"(","isDraft":false})").state == RemoteState::Unknown);
  // 深い入れ子でも落ちない
  std::string deep = R"({"state":"OPEN","isDraft":false,"x":)";
  deep += std::string(20000, '[') + std::string(20000, ']') + "}";
  const RemoteInfo d = parseGhPrJson(deep);
  CHECK((d.state == RemoteState::Open || d.state == RemoteState::Unknown));
  CHECK(parseGhPrJson(std::string(30000, '[')).state == RemoteState::Unknown);
  // 数値の桁あふれ
  CHECK(parseGhPrJson(R"({"state":"OPEN","isDraft":false,"n":1e999999})").state != RemoteState::Merged);
}

TEST_CASE("日時: isGhTimestamp / localDayOfUtc") {
  CHECK(isGhTimestamp("2026-10-20T03:04:05Z"));
  CHECK(!isGhTimestamp("0001-01-01T00:00:00Z"));
  CHECK(!isGhTimestamp("2026-10-20T03:04:05"));
  CHECK(!isGhTimestamp(""));
  CHECK(fmtIso(localDayOfUtc("2026-10-20T03:04:05Z", 0)) == "2026-10-20");
  // 日本時間（+9 時間）: UTC の 15:00 以降は翌日
  CHECK(fmtIso(localDayOfUtc("2026-10-20T14:59:59Z", 540)) == "2026-10-20");
  CHECK(fmtIso(localDayOfUtc("2026-10-20T15:00:00Z", 540)) == "2026-10-21");
  CHECK(fmtIso(localDayOfUtc("2026-10-31T23:00:00Z", 540)) == "2026-11-01");
  CHECK(fmtIso(localDayOfUtc("2026-12-31T20:00:00Z", 540)) == "2027-01-01");
  // 西側（-5 時間）: UTC の 04:59 までは前日
  CHECK(fmtIso(localDayOfUtc("2026-10-20T04:59:00Z", -300)) == "2026-10-19");
  CHECK(fmtIso(localDayOfUtc("2026-10-20T05:00:00Z", -300)) == "2026-10-20");
  CHECK(fmtIso(localDayOfUtc("2026-03-01T00:00:00Z", -60)) == "2026-02-28");
  CHECK(!localDayOfUtc("bad", 0).has_value());
}

// ---- 完了の提案 ----

TEST_CASE("proposeCompletion: GitHub のリンクがすべて閉じているときだけ提案する") {
  LinkMap links;
  links["7"] = {mkLink(kPr1), mkLink(kIs3)};
  RemoteStates st;
  st[kPr1] = ri(RemoteState::Merged, "2026-10-20T03:00:00Z");
  st[kIs3] = ri(RemoteState::ClosedCompleted, "2026-10-21T04:00:00Z");
  auto p = proposeCompletion(openIssue(), links, st);
  REQUIRE(p.has_value());
  CHECK(p->resolvedIso == "2026-10-21");  // 最も遅い日
  CHECK(fmtIso(p->resolved) == "2026-10-21");
  CHECK(p->linkCount == 2);
  CHECK(!p->hasUnmerged);
  CHECK(!p->hasNotPlanned);
  CHECK(p->caution.empty());
  // 順序が違っても最も遅い日
  st[kPr1] = ri(RemoteState::Merged, "2026-10-25T01:00:00Z");
  p = proposeCompletion(openIssue(), links, st);
  REQUIRE(p.has_value());
  CHECK(p->resolvedIso == "2026-10-25");
}

TEST_CASE("proposeCompletion: 1 件でも open / draft / Unknown / 未確認があれば提案しない") {
  LinkMap links;
  links["7"] = {mkLink(kPr1), mkLink(kIs3)};
  RemoteStates st;
  st[kPr1] = ri(RemoteState::Merged, "2026-10-20T03:00:00Z");
  // 未確認
  CHECK(!proposeCompletion(openIssue(), links, st).has_value());
  for (RemoteState s : {RemoteState::Open, RemoteState::Draft, RemoteState::Unknown}) {
    st[kIs3] = ri(s);
    CHECK_MESSAGE(!proposeCompletion(openIssue(), links, st).has_value(), remoteStateLabel(s));
  }
  st[kIs3] = ri(RemoteState::ClosedNotPlanned, "2026-10-21T04:00:00Z");
  CHECK(proposeCompletion(openIssue(), links, st).has_value());
  // 閉じた状態でも日時が空なら提案しない（日付を決められない）
  st[kIs3] = ri(RemoteState::ClosedCompleted, "");
  CHECK(!proposeCompletion(openIssue(), links, st).has_value());
  st[kIs3] = ri(RemoteState::ClosedCompleted, "bad");
  CHECK(!proposeCompletion(openIssue(), links, st).has_value());
}

TEST_CASE("proposeCompletion: GitHub 以外のリンクは無視する。GitHub のリンクがなければ提案しない") {
  RemoteStates st;
  st[kPr1] = ri(RemoteState::Merged, "2026-10-20T03:00:00Z");
  LinkMap links;
  links["7"] = {mkLink("https://example.com/wiki"), mkLink("\\\\server\\share\\a.xlsx"), mkLink(kPr1), mkLink("https://github.com/acme/app")};
  auto p = proposeCompletion(openIssue(), links, st);
  REQUIRE(p.has_value());
  CHECK(p->linkCount == 1);
  CHECK(p->resolvedIso == "2026-10-20");
  // GitHub 以外だけ
  LinkMap only;
  only["7"] = {mkLink("https://example.com/wiki"), mkLink("C:\\a\\b.txt")};
  CHECK(!proposeCompletion(openIssue(), only, st).has_value());
  // リンクなし・別の課題のリンクだけ
  CHECK(!proposeCompletion(openIssue(), LinkMap{}, st).has_value());
  LinkMap other;
  other["8"] = {mkLink(kPr1)};
  CHECK(!proposeCompletion(openIssue(), other, st).has_value());
  // 同じ PR を 2 回付けても 1 件の状態でよい
  LinkMap dup;
  dup["7"] = {mkLink(kPr1), mkLink("https://github.com/acme/app/pull/1/files")};
  CHECK(proposeCompletion(openIssue(), dup, st).has_value());
  // 鍵は正規化した URL（付け方が違っても同じ PR）
  LinkMap pretty;
  pretty["7"] = {mkLink("HTTPS://GITHUB.COM/acme/app/pull/1/#issuecomment-1")};
  CHECK(proposeCompletion(openIssue(), pretty, st).has_value());
}

TEST_CASE("proposeCompletion: すでに完了の課題には提案しない") {
  LinkMap links;
  links["7"] = {mkLink(kPr1)};
  RemoteStates st;
  st[kPr1] = ri(RemoteState::Merged, "2026-10-20T03:00:00Z");
  Issue o = openIssue();
  CHECK(proposeCompletion(o, links, st).has_value());
  Issue withResolved = o;
  withResolved.resolved = *parseIso("2026-10-01");
  CHECK(!proposeCompletion(withResolved, links, st).has_value());
  for (const char* done : {"Done", "done", "完了", "Closed", "Resolved", "解決", "クローズ"}) {
    Issue d = o;
    d.status = done;
    CHECK_MESSAGE(!proposeCompletion(d, links, st).has_value(), done);
  }
  Issue todo = o;
  todo.status = "To Do";
  CHECK(proposeCompletion(todo, links, st).has_value());
}

TEST_CASE("proposeCompletion: マージなし・計画外を含むときは注意の文言を持つ") {
  LinkMap links;
  links["7"] = {mkLink(kPr1), mkLink(kPr2), mkLink(kIs3)};
  RemoteStates st;
  st[kPr1] = ri(RemoteState::Merged, "2026-10-20T03:00:00Z");
  st[kPr2] = ri(RemoteState::ClosedUnmerged, "2026-10-22T03:00:00Z");
  st[kIs3] = ri(RemoteState::ClosedCompleted, "2026-10-21T03:00:00Z");
  auto p = proposeCompletion(openIssue(), links, st);
  REQUIRE(p.has_value());
  CHECK(p->hasUnmerged);
  CHECK(!p->hasNotPlanned);
  CHECK(p->caution == "マージなしで閉じられたものを含みます");
  CHECK(p->resolvedIso == "2026-10-22");  // マージなしで閉じた日も数える
  st[kPr2] = ri(RemoteState::Merged, "2026-10-22T03:00:00Z");
  st[kIs3] = ri(RemoteState::ClosedNotPlanned, "2026-10-21T03:00:00Z");
  p = proposeCompletion(openIssue(), links, st);
  REQUIRE(p.has_value());
  CHECK(!p->hasUnmerged);
  CHECK(p->hasNotPlanned);
  CHECK(p->caution == "計画外で閉じられたものを含みます");
  st[kPr2] = ri(RemoteState::ClosedUnmerged, "2026-10-22T03:00:00Z");
  p = proposeCompletion(openIssue(), links, st);
  REQUIRE(p.has_value());
  CHECK(p->hasUnmerged);
  CHECK(p->hasNotPlanned);
  CHECK(p->caution == "マージなしで閉じられたもの・計画外で閉じられたものを含みます");
  // 注意の文言に URL を入れない
  CHECK(p->caution.find("github") == std::string::npos);
}

TEST_CASE("proposeCompletion: 完了日は利用者の時間帯の暦の日") {
  LinkMap links;
  links["7"] = {mkLink(kPr1)};
  RemoteStates st;
  st[kPr1] = ri(RemoteState::Merged, "2026-10-20T20:00:00Z");
  CHECK(proposeCompletion(openIssue(), links, st, 0)->resolvedIso == "2026-10-20");
  CHECK(proposeCompletion(openIssue(), links, st, 540)->resolvedIso == "2026-10-21");  // 日本時間では翌朝
}

// ---- 取得の窓口（固定の応答） ----

TEST_CASE("FakeRemoteStatus: 固定の応答を返す") {
  FakeRemoteStatus f;
  auto ref = *parseGitHubUrl(kPr1);
  auto miss = f.fetch(ref);
  CHECK(!miss.ok());
  f.set(ref.canonicalUrl(), ri(RemoteState::Open));
  auto hit = f.fetch(ref);
  REQUIRE(hit.ok());
  CHECK(hit->state == RemoteState::Open);
  f.fail(ref.canonicalUrl(), "つながりません");
  auto ng = f.fetch(ref);
  REQUIRE(!ng.ok());
  CHECK(ng.error() == "つながりません");
  CHECK(f.calls() == 3);
  CHECK(f.calledUrls().size() == 3);
  UnavailableRemoteStatus u;
  CHECK(!u.fetch(ref).ok());
}
