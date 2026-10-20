#include <doctest/doctest.h>

#include "../test_util.hpp"
#include "app_controller.hpp"
#include "roadmap/attachments.hpp"
#include "roadmap/csv.hpp"
#include "roadmap/project_io.hpp"

using namespace roadmap;

namespace {
FixedClock clk(LocalTime{2026, 10, 20, 9, 30, 0});
const std::string kProj = "data/案件A";

struct Env {
  MemFileStore fs;
  AttachmentMap meta;
  AttachOutcome add(const std::string& issue, const std::vector<std::string>& files, const AttachLimits& lim = {}) {
    return attachFiles(fs, clk, kProj, meta, issue, files, "山田", lim);
  }
};
}  // namespace

TEST_CASE("attachments: 置き場は 添付/<Issue Id>/<ファイル名>、メタ情報を持つ") {
  Env e;
  e.fs.put("ext/仕様書.pdf", "PDFDATA");
  const auto r = e.add("PRJ-12", {"ext/仕様書.pdf"});
  REQUIRE(r.rejected.empty());
  REQUIRE(r.added.size() == 1);
  CHECK(e.fs.exists("data/案件A/添付/PRJ-12/仕様書.pdf"));
  CHECK(e.fs.content("data/案件A/添付/PRJ-12/仕様書.pdf") == "PDFDATA");
  REQUIRE(e.meta["PRJ-12"].size() == 1);
  const Attachment& a = e.meta["PRJ-12"][0];
  CHECK(a.name == "仕様書.pdf");
  CHECK(a.size == 7);
  CHECK(a.at == "2026-10-20T09:30");
  CHECK(a.who == "山田");
  CHECK(attachmentPath(kProj, "PRJ-12", a) == "data/案件A/添付/PRJ-12/仕様書.pdf");
  CHECK(attachmentCount(e.meta, "PRJ-12") == 1);
  CHECK(attachmentCount(e.meta, "none") == 0);
}

TEST_CASE("attachments: 同じ名前は連番、Windows の \\ 区切りの元パスも名前だけを取る") {
  Env e;
  e.fs.put("ext/a.png", "1");
  e.fs.put("ext/b/a.png", "22");
  REQUIRE(e.add("1", {"ext/a.png", "ext/b/a.png", "ext/a.png"}).added.size() == 3);
  CHECK(e.meta["1"][0].name == "a.png");
  CHECK(e.meta["1"][1].name == "a-2.png");
  CHECK(e.meta["1"][2].name == "a-3.png");
  CHECK(e.fs.content("data/案件A/添付/1/a-2.png") == "22");
  e.fs.put("C:\\dir\\x.txt", "x");  // MemFileStore は文字列そのものをキーにする
  const auto r = e.add("2", {"C:\\dir\\x.txt"});
  REQUIRE(r.added.size() == 1);
  CHECK(r.added[0].name == "x.txt");
}

TEST_CASE("attachments: フォルダ名に使えない文字・長すぎる名前を直す") {
  Env e;
  e.fs.put("ext/a.txt", "x");
  const auto r = e.add("A/B:1", {"ext/a.txt"});
  REQUIRE(r.added.size() == 1);
  CHECK(e.fs.exists("data/案件A/添付/A_B_1/a.txt"));
  const std::string longName = std::string(300, 'x') + ".txt";
  e.fs.put("ext/" + longName, "y");
  const auto r2 = e.add("2", {"ext/" + longName});
  REQUIRE(r2.added.size() == 1);
  CHECK(r2.added[0].name.size() < 300);
  CHECK(r2.added[0].name.substr(r2.added[0].name.size() - 4) == ".txt");
  CHECK(paths::utf16Units(attachmentPath(kProj, "2", r2.added[0])) <= paths::kMaxPathUnits);
}

TEST_CASE("attachments: 1ファイルの上限を超えたら理由つきで断る（既定は 50MB）") {
  CHECK(AttachLimits{}.perFile == 50ull * 1024 * 1024);
  CHECK(AttachLimits{}.perProject == 1024ull * 1024 * 1024);
  Env e;
  e.fs.put("ext/big.bin", std::string(11, 'x'));
  e.fs.put("ext/ok.bin", std::string(10, 'x'));
  AttachLimits lim;
  lim.perFile = 10;
  const auto r = e.add("1", {"ext/big.bin", "ext/ok.bin"}, lim);
  REQUIRE(r.added.size() == 1);
  CHECK(r.added[0].name == "ok.bin");
  REQUIRE(r.rejected.size() == 1);
  CHECK(r.rejected[0].name == "big.bin");
  CHECK(r.rejected[0].reason.find("上限") != std::string::npos);
  CHECK_FALSE(e.fs.exists("data/案件A/添付/1/big.bin"));
}

TEST_CASE("attachments: プロジェクト合計の上限（他の課題の分も数える）") {
  Env e;
  e.fs.put("ext/a", std::string(6, 'x'));
  e.fs.put("ext/b", std::string(6, 'x'));
  AttachLimits lim;
  lim.perProject = 10;
  CHECK(e.add("1", {"ext/a"}, lim).added.size() == 1);
  const auto r = e.add("2", {"ext/b"}, lim);
  CHECK(r.added.empty());
  REQUIRE(r.rejected.size() == 1);
  CHECK(r.rejected[0].reason.find("合計") != std::string::npos);
  CHECK(attachmentsTotalBytes(e.meta) == 6);
}

TEST_CASE("attachments: 見つからないファイルは断り、ほかは付ける") {
  Env e;
  e.fs.put("ext/a.txt", "x");
  const auto r = e.add("1", {"ext/none.txt", "ext/a.txt"});
  CHECK(r.added.size() == 1);
  REQUIRE(r.rejected.size() == 1);
  CHECK(r.rejected[0].name == "none.txt");
}

TEST_CASE("attachments: 書き込みに失敗したらメタ情報を増やさない") {
  Env e;
  e.fs.put("ext/a.txt", "x");
  e.fs.setFailWrites(true);
  const auto r = e.add("1", {"ext/a.txt"});
  CHECK(r.added.empty());
  CHECK(r.rejected.size() == 1);
  CHECK(e.meta.empty());
}

TEST_CASE("attachments: フォルダのドロップは中のファイルを全部（サブフォルダも）") {
  Env e;
  e.fs.put("ext/dir/a.txt", "a");
  e.fs.put("ext/dir/sub/b.txt", "b");
  e.fs.put("ext/dir/sub/deep/c.txt", "c");
  e.fs.put("ext/single.txt", "s");
  const auto files = expandDropped(e.fs, {"ext/dir", "ext/single.txt"});
  CHECK(files.files == std::vector<std::string>{"ext/dir/a.txt", "ext/dir/sub/b.txt", "ext/dir/sub/deep/c.txt", "ext/single.txt"});
  CHECK_FALSE(files.truncated);
  const auto r = e.add("1", {"ext/dir", "ext/single.txt"});
  CHECK(r.added.size() == 4);
  CHECK(r.rejected.empty());
}

TEST_CASE("attachments: ファイルが多すぎるフォルダは途中まで付け、その旨を知らせる") {
  Env e;
  for (int i = 0; i < 5; ++i) e.fs.put("ext/d/f" + std::to_string(i), "x");
  AttachLimits lim;
  lim.maxFiles = 3;
  const auto r = e.add("1", {"ext/d"}, lim);
  CHECK(r.added.size() == 3);
  REQUIRE(r.rejected.size() == 1);
  CHECK(r.rejected[0].reason.find("多すぎ") != std::string::npos);
}

TEST_CASE("attachments: データ（貼り付けた画像など）を直接付ける") {
  Env e;
  const auto r = attachData(e.fs, clk, kProj, e.meta, "1", "画像.png", "\x89PNG", "山田");
  REQUIRE(r.ok());
  CHECK(r->size == 4);
  CHECK(e.fs.content("data/案件A/添付/1/画像.png") == "\x89PNG");
  CHECK(e.meta["1"].size() == 1);
}

TEST_CASE("attachments: 削除は _削除済み へ移す（同名は連番、ファイルが無くてもメタ情報は消せる）") {
  Env e;
  e.fs.put("ext/a.txt", "one");
  REQUIRE(e.add("1", {"ext/a.txt"}).added.size() == 1);
  REQUIRE(removeAttachment(e.fs, kProj, e.meta, "1", 0).ok());
  CHECK_FALSE(e.fs.exists("data/案件A/添付/1/a.txt"));
  CHECK(e.fs.exists("data/案件A/_削除済み/1/a.txt"));
  CHECK(e.fs.content("data/案件A/_削除済み/1/a.txt") == "one");
  CHECK(e.meta.empty());  // 空になった課題の項目は残さない
  // 付け直して、もう一度消すと連番
  e.fs.put("ext/a.txt", "two");
  REQUIRE(e.add("1", {"ext/a.txt"}).added.size() == 1);
  REQUIRE(removeAttachment(e.fs, kProj, e.meta, "1", 0).ok());
  CHECK(e.fs.content("data/案件A/_削除済み/1/a-2.txt") == "two");
  CHECK(e.fs.content("data/案件A/_削除済み/1/a.txt") == "one");
  // 本体が先に消えていた
  e.meta["1"].push_back({"gone.txt", 3, "2026-10-20T09:30", "x"});
  CHECK(removeAttachment(e.fs, kProj, e.meta, "1", 0).ok());
  CHECK(e.meta.empty());
  // 範囲外
  CHECK_FALSE(removeAttachment(e.fs, kProj, e.meta, "1", 5).ok());
}

TEST_CASE("attachments: 移せなかったときはメタ情報を残す") {
  Env e;
  e.fs.put("ext/a.txt", "one");
  REQUIRE(e.add("1", {"ext/a.txt"}).added.size() == 1);
  e.fs.setFailWrites(true);
  CHECK_FALSE(removeAttachment(e.fs, kProj, e.meta, "1", 0).ok());
  CHECK(e.meta["1"].size() == 1);
  CHECK(e.fs.exists("data/案件A/添付/1/a.txt"));
}

TEST_CASE("attachments: 実行ファイルの判定（開く前に警告する対象）") {
  for (const char* n : {"a.exe", "A.EXE", "run.bat", "x.cmd", "s.ps1", "m.msi", "s.vbs", "j.js", "a.scr", "l.lnk", "a.jar",
                        "a.hta", "a.com", "a.reg", "a.wsf"})
    CHECK(isRiskyFile(n));
  for (const char* n : {"a.png", "メモ.txt", "exe", "a.exe.txt", "仕様書.pdf", "a.xlsx", ""}) CHECK_FALSE(isRiskyFile(n));
  CHECK(isImageFile("a.PNG"));
  CHECK(isImageFile("b.jpeg"));
  CHECK_FALSE(isImageFile("c.gif.exe"));
}

TEST_CASE("attachments: サイズの表記") {
  CHECK(formatBytes(0) == "0 B");
  CHECK(formatBytes(999) == "999 B");
  CHECK(formatBytes(1024) == "1.0 KB");
  CHECK(formatBytes(1536) == "1.5 KB");
  CHECK(formatBytes(5ull * 1024 * 1024) == "5.0 MB");
  CHECK(formatBytes(3ull * 1024 * 1024 * 1024) == "3.00 GB");
}

TEST_CASE("attachments: 設定 JSON の往復（空なら項目を出さない = 既存の設定ファイルは変わらない）") {
  ProjectRecord rec = newRecord("P", "", "Summary\nA\n", "2026-10-19", clk);
  const std::string before = dumpJs(settingsToJson(rec));
  CHECK(before.find("attachments") == std::string::npos);
  rec.settings.attachments["1"].push_back({"仕様書.pdf", 1234, "2026-10-20T09:30", "山田"});
  rec.settings.attachments["1"].push_back({"b.png", 5, "2026-10-20T09:31", ""});
  const std::string after = dumpJs(settingsToJson(rec));
  CHECK(after.find("\"attachments\"") != std::string::npos);
  const Json j = Json::parse(after);
  const ProjectRecord back = recordFromSettingsJson(j, rec.csv, "P", clk);
  REQUIRE(back.settings.attachments.count("1") == 1);
  CHECK(back.settings.attachments.at("1") == rec.settings.attachments.at("1"));
  CHECK_FALSE(back.settings.unknown.contains("attachments"));
  // 壊れた値は読み飛ばす（落ちない）
  const Json bad = Json::parse(R"({"id":"p1","attachments":{"1":[{"name":5},{"name":"ok.txt","size":"x"},"zzz"],"2":3}})");
  const ProjectRecord r2 = recordFromSettingsJson(bad, "Summary\nA\n", "P", clk);
  REQUIRE(r2.settings.attachments.count("1") == 1);
  CHECK(r2.settings.attachments.at("1").size() == 1);
  CHECK(r2.settings.attachments.at("1")[0].name == "ok.txt");
  CHECK(r2.settings.attachments.count("2") == 0);
}

TEST_CASE("attachments: 共有・バックアップ JSON には添付のメタ情報を入れない（本体を含まないため）") {
  ProjectRecord rec = newRecord("P", "", "Summary\nA\n", "2026-10-19", clk);
  rec.settings.attachments["1"].push_back({"a.txt", 1, "2026-10-20T09:30", "x"});
  CHECK(exportShareJson({rec}, {}, clk).find("attachments") == std::string::npos);
}

TEST_CASE("attachments: JIRA の Attachment 列（同じ見出しが複数）は取り込み・書き出しで壊れない") {
  const std::string att1 = "20/Oct/26 9:30 AM;山田;図,1.png;https://example.invalid/secure/attachment/10001/a.png";
  const std::string att2 = "21/Oct/26 10:00 AM;佐藤;spec.pdf;https://example.invalid/secure/attachment/10002/spec.pdf";
  const std::string in = "Issue Id,Summary,Attachment,Attachment,Sprint\r\n"
                         "1,a,\"" + att1 + "\"," + att2 + ",S1\r\n"
                         "2,b,,,\r\n";
  auto r = csv::importCsv(in);
  REQUIRE(r.ok());
  CHECK(r->meta.extraHeaders == std::vector<std::string>{"Attachment", "Attachment", "Sprint"});
  CHECK(r->issues[0].extra[0] == att1);
  CHECK(r->issues[0].extra[1] == att2);
  const std::string out = csv::exportCsv(r->issues, r->meta, {});
  CHECK(out.find("Attachment,Attachment,Sprint\r\n") != std::string::npos);
  CHECK(out.find("\"" + att1 + "\"," + att2 + ",S1\r\n") != std::string::npos);
  auto r2 = csv::importCsv(out);
  REQUIRE(r2.ok());
  CHECK(csv::exportCsv(r2->issues, r2->meta, {}) == out);
  // JIRA 用の書き出しでも列と値を落とさない
  csv::ExportOptions jira;
  jira.planned = true;
  jira.plan = [](const std::string&) -> std::optional<csv::PlanInfo> { return std::nullopt; };
  const std::string jout = csv::exportCsv(r->issues, r->meta, jira);
  CHECK(jout.find("Attachment,Attachment,Sprint") != std::string::npos);
  CHECK(jout.find(att2) != std::string::npos);
}

TEST_CASE("attachments: JIRA の Attachment 列を、メタ情報として読む（本体はない）") {
  auto r = csv::importCsv("Issue Id,Summary,Attachment,Attachment,attachment \n"
                          "1,a,\"20/Oct/26 9:30 AM;山田;図,1.png;https://example.invalid/a.png\",x;u;a;b.png;https://e/b,memo.txt\n"
                          "2,b,,,\n");
  REQUIRE(r.ok());
  const auto a = jiraAttachmentsOf(r->issues[0], r->meta);
  REQUIRE(a.size() == 3);
  CHECK(a[0].at == "20/Oct/26 9:30 AM");
  CHECK(a[0].who == "山田");
  CHECK(a[0].name == "図,1.png");
  CHECK(a[0].url == "https://example.invalid/a.png");
  CHECK(a[1].name == "a;b.png");  // ファイル名の中の ; は名前に含める（最初の2つと最後で区切る）
  CHECK(a[1].url == "https://e/b");
  CHECK(a[2].name == "memo.txt");  // 区切りがなければ全体を名前にする
  CHECK(a[2].at.empty());
  CHECK(a[2].url.empty());
  CHECK(jiraAttachmentsOf(r->issues[1], r->meta).empty());
  // Attachment 列がなければ空
  auto r2 = csv::importCsv("Issue Id,Summary,Sprint\n1,a,S1\n");
  REQUIRE(r2.ok());
  CHECK(jiraAttachmentsOf(r2->issues[0], r2->meta).empty());
}

// ---- コントローラ ----
namespace {
struct CEnv {
  MemFileStore fs;
  FixedClock clock{LocalTime{2026, 10, 20, 9, 30, 0}};
  app::AppController c{fs, clock, "data", "roadmap_prefs.json"};
  void step(std::int64_t ms) {
    clock.advanceMs(ms);
    c.tick();
  }
};
}  // namespace

TEST_CASE("controller: ドロップしたファイルを課題に添付し、再起動後も残る") {
  CEnv e;
  e.c.start();
  e.c.createProject(app::SampleKind::Empty, "案件", "");
  const std::string id = e.c.session()->addIssue("", "", "課題");
  e.c.afterEdit();
  e.fs.put("ext/メモ.txt", "hello");
  CHECK(e.c.canAttach());
  const auto r = e.c.attachFiles(id, {"ext/メモ.txt"});
  CHECK(r.added.size() == 1);
  CHECK(e.fs.exists("data/案件/添付/" + id + "/メモ.txt"));
  CHECK(e.c.attachmentsOf(id).size() == 1);
  CHECK(e.c.attachmentCount(id) == 1);
  e.step(600);
  REQUIRE(e.fs.exists("data/案件/project.settings.json"));
  CHECK(e.fs.content("data/案件/project.settings.json").find("メモ.txt") != std::string::npos);

  // 同じフォルダの中身で起動し直す
  CEnv e2;
  e2.fs = e.fs;
  e2.c.start();
  REQUIRE(e2.c.session());
  REQUIRE(e2.c.attachmentsOf(id).size() == 1);
  CHECK(e2.c.attachmentsOf(id)[0].name == "メモ.txt");
  // 削除 → _削除済み、メタ情報も保存される
  REQUIRE(e2.c.removeAttachment(id, 0));
  CHECK(e2.fs.exists("data/案件/_削除済み/" + id + "/メモ.txt"));
  CHECK(e2.c.attachmentCount(id) == 0);
  e2.step(600);
  CHECK(e2.fs.content("data/案件/project.settings.json").find("メモ.txt") == std::string::npos);
}

TEST_CASE("controller: 添付は元に戻す（Undo）の対象外で、課題を削除・Undo してもファイルは残る") {
  CEnv e;
  e.c.start();
  e.c.createProject(app::SampleKind::Empty, "案件", "");
  const std::string id = e.c.session()->addIssue("", "", "課題");
  e.fs.put("ext/a.txt", "x");
  e.c.attachFiles(id, {"ext/a.txt"});
  const std::size_t undo = e.c.session()->undoSize();
  CHECK(e.c.session()->deleteIssues({id}));
  CHECK(e.c.session()->undo());
  CHECK(e.c.session()->undoSize() == undo);
  CHECK(e.c.attachmentCount(id) == 1);
  CHECK(e.fs.exists("data/案件/添付/" + id + "/a.txt"));
}

TEST_CASE("controller: 平置きのプロジェクト・プロジェクトを開いていないときは添付できない") {
  CEnv e;
  e.c.start();
  CHECK_FALSE(e.c.canAttach());
  CHECK_FALSE(e.c.attachDisabledReason().empty());
  e.fs.put("data/古い.csv", "Issue Id,Summary\n1,a\n");
  e.c.syncFolder(true);
  e.c.openProject(e.c.projects().front().id);
  REQUIRE(e.c.session());
  CHECK_FALSE(e.c.canAttach());  // 平置きは添付の置き場がない
  e.fs.put("ext/a.txt", "x");
  const auto r = e.c.attachFiles("1", {"ext/a.txt"});
  CHECK(r.added.empty());
  CHECK_FALSE(e.fs.exists("data/添付/1/a.txt"));
}

TEST_CASE("controller: プロジェクトの複製では添付のメタ情報を持ち越さない") {
  CEnv e;
  e.c.start();
  e.c.createProject(app::SampleKind::Empty, "案件", "");
  const std::string id = e.c.session()->addIssue("", "", "課題");
  e.fs.put("ext/a.txt", "x");
  e.c.attachFiles(id, {"ext/a.txt"});
  e.c.duplicateProject();
  CHECK(e.c.attachmentCount(id) == 0);
}
