// GitHub の状態の確認と、閉じたら完了の提案（コントローラ）。Issue #61
#include <doctest/doctest.h>

#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "app_controller.hpp"
#include "roadmap/github_ref.hpp"

using namespace roadmap;
using namespace roadmap::app;

namespace {
const char* const kPr = "https://github.com/acme/secret-app/pull/12";
const char* const kPr2 = "https://github.com/acme/secret-app/pull/13";
const char* const kIs = "https://github.com/acme/secret-app/issues/3";

// fetch に入ってすぐは何も登録せず、go() されるまで眠る（gh の PATH 探索のように、中断を見る前に時間がかかる実装の再現）。
// 眠りから覚めたあと、token が中断されていれば戻る。そうでなければ 3 秒待つ（中断を取りこぼすと、その分遅くなる）
class LateStartStatus final : public IRemoteStatus {
 public:
  Result<RemoteInfo> fetch(const GitHubRef&, const CancelToken& token) override {
    {
      std::unique_lock<std::mutex> lk(mu_);
      entered_ = true;
      cv_.notify_all();
      cv_.wait_for(lk, std::chrono::seconds(10), [this] { return go_; });
    }
    for (int i = 0; i < 600; ++i) {
      if (token.cancelled()) return Error{"確認を中断しました"};
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return Error{"取りこぼした"};
  }
  bool waitEntered() {
    std::unique_lock<std::mutex> lk(mu_);
    return cv_.wait_for(lk, std::chrono::seconds(5), [this] { return entered_; });
  }
  void go() {
    std::lock_guard<std::mutex> lk(mu_);
    go_ = true;
    cv_.notify_all();
  }

 private:
  std::mutex mu_;
  std::condition_variable cv_;
  bool entered_ = false, go_ = false;
};

struct Env {
  MemFileStore fs;
  FixedClock clk{LocalTime{2026, 10, 19, 18, 36, 0}};
  AppController c{fs, clk, "data", "roadmap_prefs.json"};
  std::shared_ptr<FakeRemoteStatus> fake = std::make_shared<FakeRemoteStatus>();
  explicit Env(bool async = false) {
    c.start();
    const std::string csv =
        "Issue Id,Issue Type,Summary,Parent,Assignee,Status,Priority,Resolved,Duration (weeks)\n"
        "1,Task,設計書を書く,,田中,In Progress,Medium,,1\n"
        "2,Task,別の課題,,田中,To Do,Medium,,1\n"
        "3,Task,これは三十文字を超える長い長い長い長い件名の課題ですね一二三四五,,田中,To Do,Medium,,1\n";
    REQUIRE(c.importText(csv, "確認", ImportMode::New).ok);
    c.setRemoteStatus(fake, async);
  }
  void step(std::int64_t ms = 16) {
    clk.advanceMs(ms);
    c.tick();
  }
  // 条件が成り立つまで tick する（別のスレッドの確認を待つ）
  bool waitFor(const std::function<bool()>& cond, int maxMs = 5000) {
    for (int i = 0; i < maxMs / 5; ++i) {
      step();
      if (cond()) return true;
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return cond();
  }
  const Issue& issue(const std::string& id) { return *c.session()->issue(id); }
  const Toast* toastWith(NoticeAction a) const {
    for (const Toast& t : c.toasts())
      if (t.action == a) return &t;
    return nullptr;
  }
  bool anyToastContains(const std::string& s) const {
    for (const Toast& t : c.toasts())
      if (t.text.find(s) != std::string::npos) return true;
    return false;
  }
  void closed(const char* url, RemoteState s, const char* at) {
    RemoteInfo r;
    r.state = s;
    r.closedAt = at;
    fake->set(GitHubRef(*parseGitHubUrl(url)).canonicalUrl(), r);
  }
  void open(const char* url, RemoteState s = RemoteState::Open) {
    RemoteInfo r;
    r.state = s;
    fake->set(GitHubRef(*parseGitHubUrl(url)).canonicalUrl(), r);
  }
};
}  // namespace

TEST_CASE("remote: 確認前は未確認。GitHub 以外のリンクは見ない") {
  Env e;
  REQUIRE(e.c.addLink("1", "", kPr));
  REQUIRE(e.c.addLink("1", "", "https://example.com/wiki"));
  CHECK(e.c.githubLinkCount("1") == 1);
  CHECK(e.c.remoteViewOf(kPr).phase == AppController::RemoteView::Phase::None);
  CHECK(e.c.remoteViewOf("https://example.com/wiki").phase == AppController::RemoteView::Phase::None);
  CHECK(e.c.remoteAvailable());
  CHECK_FALSE(e.c.remoteBusy());
}

TEST_CASE("remote: すべて閉じていれば完了の提案を通知し、押すまで課題は変わらない。Undo 1 回で戻る") {
  Env e;
  REQUIRE(e.c.addLink("1", "", kPr));
  REQUIRE(e.c.addLink("1", "", kIs));
  e.closed(kPr, RemoteState::Merged, "2026-10-15T03:00:00Z");
  e.closed(kIs, RemoteState::ClosedCompleted, "2026-10-17T04:00:00Z");
  CHECK(e.c.checkRemote("1") == 2);
  e.step();
  const auto pv = e.c.remoteViewOf(kPr);
  REQUIRE(pv.phase == AppController::RemoteView::Phase::Done);
  CHECK(pv.info.state == RemoteState::Merged);
  CHECK(pv.checkedAt == "2026-10-19T18:36");
  CHECK(e.c.remoteViewOf(kIs).info.state == RemoteState::ClosedCompleted);
  CHECK_FALSE(e.c.remoteBusy());
  const Toast* t = e.toastWith(NoticeAction::Complete);
  REQUIRE(t);
  CHECK(t->arg == "1");
  CHECK(t->project == e.c.currentId());
  CHECK(t->text.find("#1") != std::string::npos);
  CHECK(t->text.find("設計書を書く") != std::string::npos);
  CHECK(t->text.find("すべて閉じています") != std::string::npos);
  CHECK(t->text.find("完了にしますか？") != std::string::npos);
  CHECK(t->text.find("含みます") == std::string::npos);  // マージなし・計画外を含まなければ注意は付けない
  // URL・リポジトリ名は通知に出さない
  CHECK(t->text.find("acme") == std::string::npos);
  CHECK(t->text.find("secret") == std::string::npos);
  CHECK(t->text.find("github") == std::string::npos);
  // 押すまで課題は変わらない
  CHECK(e.issue("1").status == "In Progress");
  CHECK_FALSE(e.issue("1").resolved.has_value());
  const std::size_t undoBefore = e.c.session()->undoSize();
  e.c.runToastAction(t->id);
  CHECK(e.issue("1").status == "Done");
  REQUIRE(e.issue("1").resolved.has_value());
  CHECK(fmtIso(*e.issue("1").resolved) == "2026-10-17");  // 閉じた日のうち最も遅い日
  CHECK(e.c.session()->undoSize() == undoBefore + 1);
  CHECK(e.toastWith(NoticeAction::Complete) == nullptr);  // 提案は閉じる
  const Toast* u = e.toastWith(NoticeAction::Undo);
  REQUIRE(u);
  CHECK(u->text.find("acme") == std::string::npos);
  // Undo 1 回で戻る
  e.c.runToastAction(u->id);
  CHECK(e.issue("1").status == "In Progress");
  CHECK_FALSE(e.issue("1").resolved.has_value());
  // 別の課題は変わらない
  CHECK(e.issue("2").status == "To Do");
}

TEST_CASE("remote: マージなし・計画外を含むときは、提案の文に明記する") {
  Env e;
  REQUIRE(e.c.addLink("1", "", kPr));
  e.closed(kPr, RemoteState::ClosedUnmerged, "2026-10-15T03:00:00Z");
  e.c.checkRemote("1");
  e.step();
  const Toast* t = e.toastWith(NoticeAction::Complete);
  REQUIRE(t);
  CHECK(t->text.find("（マージなしで閉じられたものを含みます）") != std::string::npos);
  CHECK(e.c.remoteViewOf(kPr).info.state == RemoteState::ClosedUnmerged);
  // 了承しなければ課題は変わらない（通知を閉じる）
  e.c.dismissToast(t->id);
  CHECK(e.issue("1").status == "In Progress");
  CHECK_FALSE(e.issue("1").resolved.has_value());

  Env f;
  REQUIRE(f.c.addLink("1", "", kIs));
  f.closed(kIs, RemoteState::ClosedNotPlanned, "2026-10-15T03:00:00Z");
  f.c.checkRemote("1");
  f.step();
  const Toast* t2 = f.toastWith(NoticeAction::Complete);
  REQUIRE(t2);
  CHECK(t2->text.find("（計画外で閉じられたものを含みます）") != std::string::npos);
}

TEST_CASE("remote: 1 件でも open / draft があれば提案しない") {
  Env e;
  REQUIRE(e.c.addLink("1", "", kPr));
  REQUIRE(e.c.addLink("1", "", kIs));
  e.closed(kPr, RemoteState::Merged, "2026-10-15T03:00:00Z");
  e.open(kIs);
  e.c.checkRemote("1");
  e.step();
  CHECK(e.toastWith(NoticeAction::Complete) == nullptr);
  CHECK(e.c.remoteViewOf(kIs).info.state == RemoteState::Open);
  CHECK(e.issue("1").status == "In Progress");
  e.open(kIs, RemoteState::Open);
  e.open(kPr, RemoteState::Draft);
  e.closed(kIs, RemoteState::ClosedCompleted, "2026-10-16T03:00:00Z");
  e.c.checkRemote("1");
  e.step();
  CHECK(e.toastWith(NoticeAction::Complete) == nullptr);
  CHECK(e.c.remoteViewOf(kPr).info.state == RemoteState::Draft);
  CHECK(e.issue("1").status == "In Progress");
}

TEST_CASE("remote: 失敗は理由を通知し、課題は変わらない。理由に URL を入れない") {
  Env e;
  REQUIRE(e.c.addLink("1", "", kPr));
  REQUIRE(e.c.addLink("1", "", kIs));
  e.closed(kPr, RemoteState::Merged, "2026-10-15T03:00:00Z");
  e.fake->fail(GitHubRef(*parseGitHubUrl(kIs)).canonicalUrl(), "gh が見つかりません");
  e.c.checkRemote("1");
  e.step();
  CHECK(e.toastWith(NoticeAction::Complete) == nullptr);  // 一部が失敗したら提案しない
  CHECK(e.anyToastContains("gh が見つかりません"));
  CHECK_FALSE(e.anyToastContains("acme"));
  CHECK_FALSE(e.anyToastContains("github.com"));
  const auto v = e.c.remoteViewOf(kIs);
  CHECK(v.phase == AppController::RemoteView::Phase::Failed);
  CHECK(v.error == "gh が見つかりません");
  CHECK(e.issue("1").status == "In Progress");
  CHECK_FALSE(e.issue("1").resolved.has_value());
  CHECK(e.c.session()->undoSize() == 0);
  // 取得できても中身が読めなかった（Unknown）ときも、理由を通知して提案しない
  Env g;
  REQUIRE(g.c.addLink("1", "", kPr));
  RemoteInfo unk;
  unk.state = RemoteState::Unknown;
  unk.reason = "gh の出力を読めませんでした";
  g.fake->set(GitHubRef(*parseGitHubUrl(kPr)).canonicalUrl(), unk);
  g.c.checkRemote("1");
  g.step();
  CHECK(g.anyToastContains("gh の出力を読めませんでした"));
  CHECK(g.toastWith(NoticeAction::Complete) == nullptr);
  CHECK(g.c.remoteViewOf(kPr).phase == AppController::RemoteView::Phase::Failed);
}

TEST_CASE("remote: 確認の窓口がない・GitHub のリンクがないときは理由を通知する") {
  Env e;
  e.c.setRemoteStatus(nullptr);
  CHECK_FALSE(e.c.remoteAvailable());
  REQUIRE(e.c.addLink("1", "", kPr));
  CHECK(e.c.checkRemote("1") == 0);
  CHECK(e.anyToastContains("確認できません"));
  Env f;
  REQUIRE(f.c.addLink("1", "", "https://example.com/wiki"));
  CHECK(f.c.checkRemote("1") == 0);
  CHECK(f.anyToastContains("GitHub"));
  CHECK(f.fake->calls() == 0);
  CHECK(f.c.checkRemote("nope") == 0);
}

TEST_CASE("remote: 1 つのリンクだけ確認できる。同じ PR は 1 回しか取りに行かない") {
  Env e;
  REQUIRE(e.c.addLink("1", "", kPr));
  REQUIRE(e.c.addLink("1", "PR の別の見方", std::string(kPr) + "/files"));
  REQUIRE(e.c.addLink("1", "", kIs));
  e.open(kPr);
  e.open(kIs);
  CHECK(e.c.checkRemote("1", std::size_t{2}) == 1);
  e.step();
  CHECK(e.fake->calls() == 1);
  CHECK(e.c.remoteViewOf(kIs).phase == AppController::RemoteView::Phase::Done);
  CHECK(e.c.remoteViewOf(kPr).phase == AppController::RemoteView::Phase::None);
  CHECK(e.c.checkRemote("1") == 2);  // 同じ PR を指す 2 つのリンクは 1 回
  e.step();
  CHECK(e.fake->calls() == 3);
  CHECK(e.c.remoteViewOf(std::string(kPr) + "/files").phase == AppController::RemoteView::Phase::Done);
  CHECK(e.c.checkRemote("1", std::size_t{9}) == 0);  // 範囲外
}

TEST_CASE("remote: アプリの設定がオンなら、確認が終わった時点で自動で完了にする。Undo 1 回で戻る") {
  Env e;
  e.c.prefs().autoCompleteGitHub = true;
  REQUIRE(e.c.addLink("1", "", kPr));
  e.closed(kPr, RemoteState::Merged, "2026-10-15T03:00:00Z");
  e.c.checkRemote("1");
  e.step();
  CHECK(e.issue("1").status == "Done");
  REQUIRE(e.issue("1").resolved.has_value());
  CHECK(fmtIso(*e.issue("1").resolved) == "2026-10-15");
  CHECK(e.toastWith(NoticeAction::Complete) == nullptr);
  const Toast* u = e.toastWith(NoticeAction::Undo);
  REQUIRE(u);
  CHECK(u->text.find("自動で完了") != std::string::npos);
  CHECK(u->text.find("acme") == std::string::npos);
  e.c.runToastAction(u->id);
  CHECK(e.issue("1").status == "In Progress");
  CHECK_FALSE(e.issue("1").resolved.has_value());
  // オンでも、閉じていないものがあれば完了にしない
  REQUIRE(e.c.addLink("1", "", kIs));
  e.open(kIs);
  e.c.checkRemote("1");
  e.step();
  CHECK(e.issue("1").status == "In Progress");
  // 失敗しても変えない
  e.fake->fail(GitHubRef(*parseGitHubUrl(kIs)).canonicalUrl(), "つながりません");
  e.c.checkRemote("1");
  e.step();
  CHECK(e.issue("1").status == "In Progress");
}

TEST_CASE("remote: すでに完了の課題には提案しない") {
  Env e;
  REQUIRE(e.c.session()->setStatus("1", "Done"));
  REQUIRE(e.c.addLink("1", "", kPr));
  e.closed(kPr, RemoteState::Merged, "2026-10-15T03:00:00Z");
  e.c.prefs().autoCompleteGitHub = true;
  const std::size_t undo0 = e.c.session()->undoSize();
  e.c.checkRemote("1");
  e.step();
  CHECK(e.toastWith(NoticeAction::Complete) == nullptr);
  CHECK(e.c.session()->undoSize() == undo0);
  CHECK(e.c.remoteViewOf(kPr).phase == AppController::RemoteView::Phase::Done);  // 状態は表示する
}

TEST_CASE("remote: 完了日は利用者の時間帯の暦の日。未来の日付は今日にする") {
  Env e;
  e.c.setUtcOffsetMinutes(540);
  REQUIRE(e.c.addLink("1", "", kPr));
  e.closed(kPr, RemoteState::Merged, "2026-10-16T20:00:00Z");  // 日本時間では 17 日の朝
  e.c.checkRemote("1");
  e.step();
  const Toast* t = e.toastWith(NoticeAction::Complete);
  REQUIRE(t);
  e.c.runToastAction(t->id);
  CHECK(fmtIso(*e.issue("1").resolved) == "2026-10-17");
  e.c.undo();
  // 今日（2026-10-19）より先の日付は今日にする
  e.closed(kPr, RemoteState::Merged, "2026-10-25T01:00:00Z");
  e.c.checkRemote("1");
  e.step();
  const Toast* t2 = e.toastWith(NoticeAction::Complete);
  REQUIRE(t2);
  e.c.runToastAction(t2->id);
  CHECK(fmtIso(*e.issue("1").resolved) == "2026-10-19");
}

TEST_CASE("remote: 通知の件名は先頭 30 文字まで") {
  Env e;
  REQUIRE(e.c.addLink("3", "", kPr));
  e.closed(kPr, RemoteState::Merged, "2026-10-15T03:00:00Z");
  e.c.checkRemote("3");
  e.step();
  const Toast* t = e.toastWith(NoticeAction::Complete);
  REQUIRE(t);
  CHECK(t->text.find("『これは三十文字を超える長い長い長い長い件名の課題ですね一二三四") == std::string::npos);
  CHECK(t->text.find("『これは三十文字を超える長い長い長い長い件名の課題ですね一二三…』") != std::string::npos);
}

TEST_CASE("remote: 提案を押す前に状況が変わっていたら、課題を変えずに理由を通知する") {
  Env e;
  REQUIRE(e.c.addLink("1", "", kPr));
  e.closed(kPr, RemoteState::Merged, "2026-10-15T03:00:00Z");
  e.c.checkRemote("1");
  e.step();
  const Toast* t = e.toastWith(NoticeAction::Complete);
  REQUIRE(t);
  const int id = t->id;
  // リンクを外した
  REQUIRE(e.c.removeLink("1", 0));
  e.c.runToastAction(id);
  CHECK(e.issue("1").status == "In Progress");
  CHECK(e.anyToastContains("完了にしませんでした"));
  // 課題がなくなった
  REQUIRE(e.c.addLink("1", "", kPr));
  e.c.checkRemote("1");
  e.step();
  const Toast* t2 = e.toastWith(NoticeAction::Complete);
  REQUIRE(t2);
  const int id2 = t2->id;
  const std::string proj = e.c.currentId();
  e.c.session()->deleteIssues({"1"});
  e.c.runToastAction(id2);
  CHECK(e.c.session()->issue("1") == nullptr);
  CHECK(e.anyToastContains("完了にしませんでした"));
  CHECK(e.c.currentId() == proj);
}

TEST_CASE("remote: 別のプロジェクトに切り替えたら、提案は何もしない") {
  Env e;
  REQUIRE(e.c.addLink("1", "", kPr));
  e.closed(kPr, RemoteState::Merged, "2026-10-15T03:00:00Z");
  e.c.checkRemote("1");
  e.step();
  const Toast* t = e.toastWith(NoticeAction::Complete);
  REQUIRE(t);
  const int id = t->id;
  const std::string first = e.c.currentId();
  const std::string second = e.c.createProject(SampleKind::Empty, "別のプロジェクト", "");
  REQUIRE(e.c.currentId() == second);
  e.c.runToastAction(id);
  // 先のプロジェクトの課題は変わらない
  REQUIRE(e.c.openProject(first));
  CHECK(e.issue("1").status == "In Progress");
  CHECK_FALSE(e.issue("1").resolved.has_value());
  CHECK(e.anyToastContains("完了にしませんでした"));
}

TEST_CASE("remote(async): 別のスレッドで 1 件ずつ確認し、tick が結果を拾う") {
  Env e(true);
  REQUIRE(e.c.addLink("1", "", kPr));
  REQUIRE(e.c.addLink("1", "", kPr2));
  REQUIRE(e.c.addLink("1", "", kIs));
  e.closed(kPr, RemoteState::Merged, "2026-10-15T03:00:00Z");
  e.closed(kPr2, RemoteState::Merged, "2026-10-16T03:00:00Z");
  e.closed(kIs, RemoteState::ClosedCompleted, "2026-10-17T03:00:00Z");
  e.fake->hold(true);
  CHECK(e.c.checkRemote("1") == 3);
  CHECK(e.c.remoteBusy());
  REQUIRE(e.waitFor([&] { return e.fake->running() == 1; }));
  // 確認中の表示。同時に走るのは 1 件だけ
  CHECK(e.c.remoteViewOf(kPr).phase == AppController::RemoteView::Phase::Checking);
  CHECK(e.c.remoteViewOf(kIs).phase == AppController::RemoteView::Phase::Checking);
  for (int i = 0; i < 10; ++i) {
    e.step();
    CHECK(e.fake->running() <= 1);
    CHECK(e.fake->calls() == 1);
  }
  CHECK(e.toastWith(NoticeAction::Complete) == nullptr);
  e.fake->release();
  REQUIRE(e.waitFor([&] { return !e.c.remoteBusy(); }));
  CHECK(e.fake->calls() == 3);
  CHECK(e.toastWith(NoticeAction::Complete) != nullptr);
  CHECK(e.c.remoteViewOf(kIs).phase == AppController::RemoteView::Phase::Done);
}

TEST_CASE("remote(async): 確認中に別の課題・別のプロジェクトへ切り替えても落ちず、課題は変わらない") {
  Env e(true);
  REQUIRE(e.c.addLink("1", "", kPr));
  e.closed(kPr, RemoteState::Merged, "2026-10-15T03:00:00Z");
  e.fake->hold(true);
  CHECK(e.c.checkRemote("1") == 1);
  REQUIRE(e.waitFor([&] { return e.fake->running() == 1; }));
  // 別の課題を選ぶ
  e.c.select("2");
  e.step();
  // 別のプロジェクトを開く
  const std::string first = e.c.currentId();
  const std::string second = e.c.createProject(SampleKind::Empty, "切り替え先", "");
  REQUIRE(e.c.currentId() == second);
  e.fake->release();
  REQUIRE(e.waitFor([&] { return !e.c.remoteBusy(); }));
  // 結果は残る（表示に使える）が、別のプロジェクトでは提案も変更もしない
  CHECK(e.toastWith(NoticeAction::Complete) == nullptr);
  CHECK(e.c.remoteViewOf(kPr).phase == AppController::RemoteView::Phase::Done);
  REQUIRE(e.c.openProject(first));
  CHECK(e.issue("1").status == "In Progress");
  CHECK_FALSE(e.issue("1").resolved.has_value());
}

TEST_CASE("remote(async): 確認中にコントローラを壊しても止まらない（中断して終わる）") {
  auto fake = std::make_shared<FakeRemoteStatus>();
  {
    Env e(true);
    e.c.setRemoteStatus(fake, true);
    REQUIRE(e.c.addLink("1", "", kPr));
    fake->hold(true);
    e.c.checkRemote("1");
    REQUIRE(e.waitFor([&] { return fake->running() == 1; }));
  }  // ここで中断して join する
  CHECK(fake->running() == 0);
}

TEST_CASE("remote(async): fetch が中断を見る前に破棄しても、待たされない") {
  auto st = std::make_shared<LateStartStatus>();
  Env e(true);
  e.c.setRemoteStatus(st, true);
  REQUIRE(e.c.addLink("1", "", kPr));
  e.c.checkRemote("1");
  e.step();
  REQUIRE(st->waitEntered());  // スレッドは fetch に入っているが、まだ何も登録していない
  std::thread releaser([&] {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    st->go();
  });
  const auto t0 = std::chrono::steady_clock::now();
  e.c.setRemoteStatus(nullptr);  // 破棄（中断は、fetch が眠っている間に起きる）
  const auto took = std::chrono::steady_clock::now() - t0;
  releaser.join();
  CHECK(took < std::chrono::seconds(2));
}

TEST_CASE("remote(async): 確認中のリンクは未確認として扱い、古い「閉じている」で提案も了承もしない") {
  Env e(true);
  REQUIRE(e.c.addLink("1", "", kPr));
  REQUIRE(e.c.addLink("1", "", kPr2));
  e.closed(kPr, RemoteState::Merged, "2026-10-15T03:00:00Z");
  e.closed(kPr2, RemoteState::Merged, "2026-10-16T03:00:00Z");
  e.c.checkRemote("1");
  REQUIRE(e.waitFor([&] { return !e.c.remoteBusy(); }));
  const Toast* t = e.toastWith(NoticeAction::Complete);
  REQUIRE(t);
  const int oldId = t->id;
  const std::string proj = e.c.currentId();
  // 2 つ目をもう一度確認する（まだ返ってこない。返ってくるのは open）
  const std::string k2 = GitHubRef(*parseGitHubUrl(kPr2)).canonicalUrl();
  e.fake->holdUrl(k2, true);
  e.open(kPr2);
  REQUIRE(e.c.checkRemote("1", std::size_t{1}) == 1);
  REQUIRE(e.waitFor([&] { return e.fake->running() == 1; }));
  CHECK(e.c.remoteViewOf(kPr2).phase == AppController::RemoteView::Phase::Checking);
  // 古い提案（通知のボタン）を了承しても、課題は変わらない
  CHECK(e.toastWith(NoticeAction::Complete) == nullptr);  // 確認を始めたら、古い提案は取り下げる
  e.c.runToastAction(oldId);
  CHECK_FALSE(e.c.completeFromRemote(proj, "1", false));  // 通知の了承と同じ道
  CHECK(e.issue("1").status == "In Progress");
  CHECK_FALSE(e.issue("1").resolved.has_value());
  e.fake->holdUrl(k2, false);
  REQUIRE(e.waitFor([&] { return !e.c.remoteBusy(); }));
  CHECK(e.c.remoteViewOf(kPr2).info.state == RemoteState::Open);
  CHECK(e.toastWith(NoticeAction::Complete) == nullptr);  // open が返ったので提案しない
  CHECK(e.issue("1").status == "In Progress");
}

TEST_CASE("remote(async): 別のリンクの確認中の古い結果で、提案も自動の完了も起こさない") {
  for (const bool autoOn : {false, true}) {
    Env e(true);
    e.c.prefs().autoCompleteGitHub = autoOn;
    REQUIRE(e.c.addLink("1", "", kPr));   // 0 番
    REQUIRE(e.c.addLink("1", "", kPr2));  // 1 番
    // 1 回目: 0 番は閉じている・1 番は open（完了にしない。0 番の「閉じている」だけが残る）
    e.closed(kPr, RemoteState::Merged, "2026-10-15T03:00:00Z");
    e.open(kPr2);
    e.c.checkRemote("1");
    REQUIRE(e.waitFor([&] { return !e.c.remoteBusy(); }));
    CHECK(e.issue("1").status == "In Progress");
    CHECK(e.toastWith(NoticeAction::Complete) == nullptr);
    // 2 回目: 1 番を確認し（閉じて返る）、続けて 0 番を確認する（まだ返らない。返るのは open）。
    // 1 番の結果が届いた時点で 0 番は確認中なので、古い「閉じている」を根拠にしない
    const std::string k1 = GitHubRef(*parseGitHubUrl(kPr)).canonicalUrl();
    e.fake->holdUrl(k1, true);
    e.open(kPr);
    e.closed(kPr2, RemoteState::Merged, "2026-10-16T03:00:00Z");
    REQUIRE(e.c.checkRemote("1", std::size_t{1}) == 1);
    REQUIRE(e.c.checkRemote("1", std::size_t{0}) == 1);
    REQUIRE(e.waitFor([&] {
      return e.c.remoteViewOf(kPr2).phase == AppController::RemoteView::Phase::Done && e.fake->running() == 1;
    }));
    for (int i = 0; i < 20; ++i) e.step();
    CHECK(e.c.remoteViewOf(kPr).phase == AppController::RemoteView::Phase::Checking);
    CHECK(e.issue("1").status == "In Progress");  // 自動の完了
    CHECK_FALSE(e.issue("1").resolved.has_value());
    CHECK(e.toastWith(NoticeAction::Complete) == nullptr);  // 提案
    e.fake->holdUrl(k1, false);
    REQUIRE(e.waitFor([&] { return !e.c.remoteBusy(); }));
    CHECK(e.c.remoteViewOf(kPr).info.state == RemoteState::Open);
    CHECK(e.issue("1").status == "In Progress");
    CHECK(e.toastWith(NoticeAction::Complete) == nullptr);
  }
}

TEST_CASE("remote: 完了日は閉じた日時ごとの時間帯の規則で換算する（夏時間）") {
  Env e;
  e.c.setUtcOffsetFn([](std::int64_t epoch) {
    static const std::int64_t on = *epochSecondsOfUtc("2026-03-08T07:00:00Z");
    static const std::int64_t off = *epochSecondsOfUtc("2026-11-01T06:00:00Z");
    return epoch >= on && epoch < off ? -240 : -300;
  });
  REQUIRE(e.c.addLink("1", "", kPr));
  e.closed(kPr, RemoteState::Merged, "2026-01-15T04:30:00Z");  // 現地では 1/14 23:30
  e.c.checkRemote("1");
  e.step();
  const Toast* t = e.toastWith(NoticeAction::Complete);
  REQUIRE(t);
  e.c.runToastAction(t->id);
  REQUIRE(e.issue("1").resolved.has_value());
  CHECK(fmtIso(*e.issue("1").resolved) == "2026-01-14");
}
