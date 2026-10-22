// 編集中のロック（Issue #62）: AppController が、共有フォルダ（MemFileStore）を使う 2 台ぶんのアプリとして振る舞う
#include <doctest/doctest.h>

#include <algorithm>
#include <memory>

#include "app_controller.hpp"
#include "roadmap/attachments.hpp"
#include "roadmap/csv.hpp"
#include "roadmap/samples.hpp"

using namespace roadmap;
using namespace roadmap::app;

namespace {
constexpr const char* kCsv = "data/P/project.csv";
constexpr const char* kSet = "data/P/project.settings.json";
constexpr const char* kLock = "data/P/.roadmap.lock";

// 1 台ぶんのアプリ。同じ共有フォルダ（fs）と同じ時計を使う
struct Pc {
  MemLockStore store;
  CountingRandom rng;
  AppController c;
  Pc(MemFileStore& fs, FixedClock& clk, const std::string& prefs, const std::string& machine, std::uint64_t seed)
      : store(fs), rng(seed), c(fs, clk, "data", prefs) {
    c.setLockStore(&store, &rng, machine);
    c.setUtcOffsetMinutes(540);  // 日本時間
  }
};

struct Env {
  MemFileStore fs;
  FixedClock clk{LocalTime{2026, 10, 19, 18, 36, 0}};
  std::unique_ptr<Pc> a, b;

  std::unique_ptr<Pc> make(const std::string& user, const std::string& machine, std::uint64_t seed, const std::string& extra = "") {
    const std::string prefs = user + "_prefs.json";
    fs.put(prefs, "{\"me\":\"" + user + "\"" + extra + "}");
    return std::make_unique<Pc>(fs, clk, prefs, machine, seed);
  }
  void startA(const std::string& extra = "") {
    a = make("Alice", "PC-A", 1000, extra);
    a->c.start();
  }
  void startB(const std::string& extra = "") {
    b = make("Bob", "PC-B", 2000, extra);
    b->c.start();
  }
  // Alice がプロジェクト P を作って、保存して、ロックを取るまで
  void aliceCreates() {
    startA();
    a->c.createProject(SampleKind::Hierarchy, "P", "");
    step(500);
    REQUIRE(fs.exists(kCsv));
  }
  void step(std::int64_t ms) {
    clk.advanceMs(ms);
    if (a) a->c.tick();
    if (b) b->c.tick();
  }
  std::vector<std::string> lockFiles() const {
    std::vector<std::string> v;
    for (const auto& p : fs.paths())
      if (isLockFileName(p)) v.push_back(p);
    return v;
  }
};

bool hasToast(const AppController& c, const std::string& part) {
  return std::any_of(c.toasts().begin(), c.toasts().end(), [&](const Toast& t) { return t.text.find(part) != std::string::npos; });
}
bool contains(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }

LockInfo foreign(const FixedClock& clk, std::int64_t idleMs) {
  LockInfo i;
  i.user = "Carol";
  i.machine = "PC-C";
  i.openedAt = clk.epochMs() - idleMs - 600'000;
  i.heartbeatAt = clk.epochMs() - idleMs;
  i.token = "carol-token";
  return i;
}
}  // namespace

TEST_CASE("ロック: 編集できる状態で開くとロックができ、ハートビートで更新され、閉じると消える") {
  Env e;
  e.aliceCreates();
  CHECK(e.a->c.lockStatus().kind == AppController::LockKind::Mine);
  CHECK_FALSE(e.a->c.readOnly());
  REQUIRE(e.fs.exists(kLock));
  const auto first = lockFromText(e.fs.content(kLock));
  REQUIRE(first);
  CHECK(first->user == "Alice");
  CHECK(first->machine == "PC-A");
  CHECK(e.a->store.updates() == 0);
  e.step(29'000);
  CHECK(e.a->store.updates() == 0);
  e.step(1'000);  // 30 秒
  CHECK(e.a->store.updates() == 1);
  const auto second = lockFromText(e.fs.content(kLock));
  REQUIRE(second);
  CHECK(second->heartbeatAt == e.clk.epochMs());
  CHECK(second->openedAt == first->openedAt);
  CHECK(second->token == first->token);
  e.step(30'000);
  CHECK(e.a->store.updates() == 2);
  // 閉じる（終了）と、自分のロックは消える
  e.a.reset();
  CHECK_FALSE(e.fs.exists(kLock));
  CHECK(e.lockFiles().empty());
}

TEST_CASE("ロック: 他の人のロックがあれば読み取り専用で開く（帯の文言・持ち主・時刻）") {
  Env e;
  e.aliceCreates();
  e.startB();
  AppController& b = e.b->c;
  REQUIRE(b.session());
  CHECK(b.readOnly());
  CHECK(b.session()->readOnly());
  const auto& st = b.lockStatus();
  CHECK(st.kind == AppController::LockKind::Other);
  CHECK(st.user == "Alice");
  CHECK(st.machine == "PC-A");
  CHECK_FALSE(st.stale);
  CHECK_FALSE(st.free);
  CHECK(b.lockSinceLabel() == "17:53");  // 固定の時計（UTC 08:53）を日本時間にした今日の時刻
  const std::string band = b.readOnlyBand();
  CHECK(contains(band, "Aliceさん（PC-A）が 17:53 から編集中です。読み取り専用で開いています"));
  // 持ち主のロックには触らない
  const auto held = lockFromText(e.fs.content(kLock));
  REQUIRE(held);
  CHECK(held->user == "Alice");
  CHECK(e.b->store.creates() == 0);
  CHECK(e.b->store.updates() == 0);
  // 日付をまたいだ時刻は日付つき
  e.clk.set(LocalTime{2026, 10, 20, 9, 0, 0});  // 翌日になると、日付つき
  CHECK(b.lockSinceLabel() == "10/19 17:53");
}

TEST_CASE("ロック: 読み取り専用では編集系の入口がすべて断られ、CSV・設定を 1 回も書かない") {
  Env e;
  e.aliceCreates();
  e.startB();
  AppController& b = e.b->c;
  Session* s = b.session();
  REQUIRE(s);
  REQUIRE(b.readOnly());
  const std::string csv0 = e.fs.content(kCsv), set0 = e.fs.content(kSet);
  const std::size_t n0 = s->issues().size();
  const std::string id0 = s->issues()[0].id;
  const std::uint64_t ver0 = s->version();
  const int writes0 = e.fs.writeCount();

  // コントローラの入口
  CHECK(b.createIssue("", "").empty());
  CHECK(b.addSibling().empty());
  CHECK(b.addChild().empty());
  b.select(id0);
  b.indentSelection();
  b.outdentSelection();
  b.moveSelection(1);
  b.deleteSelection();
  b.setStatusFor({id0}, "Done");
  b.setPrioFor({id0}, "High");
  b.setAssigneeFor({id0}, "誰か");
  CHECK_FALSE(b.chainSelection({}).ok);
  CHECK(b.unchainSelection() == 0);
  b.undo();
  b.redo();
  b.editProject("別名", "ZZ");
  b.setWeeklyNotes("所感");
  b.recordWeekly(WeeklyOptions{});
  b.recordHistory(AppController::HistoryTrigger::Manual);
  b.recordHistory(AppController::HistoryTrigger::Weekly);
  CHECK_FALSE(b.saveBaseline("b").ok());
  CHECK_FALSE(b.renameBaseline("x", "y"));
  CHECK_FALSE(b.deleteBaseline("x"));
  // 添付・リンク
  CHECK(b.attachFiles(id0, {"/no/such/file.txt"}).added.empty());
  CHECK_FALSE(b.attachData(id0, "a.txt", "abc"));
  CHECK_FALSE(b.attachClipboardImage(id0, std::string(40, 'x')));
  CHECK_FALSE(b.moveAttachment(id0, 0, id0));
  CHECK_FALSE(b.copyAttachment(id0, 0, id0));
  CHECK_FALSE(b.reorderAttachment(id0, 0, 1));
  CHECK_FALSE(b.removeAttachment(id0, 0));
  std::string why;
  CHECK_FALSE(b.addLink(id0, "t", "https://example.com/a", &why));
  CHECK_FALSE(why.empty());
  CHECK(b.addLinksFromText(id0, "https://example.com/b").added.empty());
  CHECK_FALSE(b.editLink(id0, 0, "t", "https://example.com/c", &why));
  CHECK_FALSE(b.removeLink(id0, 0));
  CHECK_FALSE(b.reorderLink(id0, 0, 1));
  CHECK_FALSE(b.moveLink(id0, 0, id0));
  CHECK_FALSE(b.copyLink(id0, 0, id0));
  // プロジェクトの削除・置き換えの取り込み・完了
  CHECK_FALSE(b.deleteCurrentProject());
  CHECK_FALSE(b.importText(csv::emptyCsv(), "x", ImportMode::Replace).ok);
  CHECK_FALSE(b.completeFromRemote(b.currentId(), id0, false));
  // Session を直接呼ぶ入口（UI のコマンドパレット・ドラッグ・その場編集・貼り付け・ドロップが使う）
  CHECK(s->addIssue("", "", "X").empty());
  CHECK_FALSE(s->setStatus(id0, "Done"));
  CHECK_FALSE(s->setName(id0, "改名"));
  CHECK_FALSE(s->setField(id0, [](Issue& o) { o.summary = "直接"; }));
  CHECK_FALSE(s->mutate([](ProjectData& d) { d.issues.clear(); }));
  CHECK_FALSE(s->deleteIssues({id0}));
  CHECK_FALSE(s->moveIssue(id0, 1));
  CHECK_FALSE(s->relocate(id0, s->issues()[1].id, RelocateWhere::After));
  CHECK_FALSE(s->addComment(id0, "c", "me"));
  CHECK(s->addOutline("", parseOutline("a\n b").items) == 0);
  s->setBaseDate("2030-01-01");
  s->setHolidays("2030-01-01");
  s->setScenario("案");
  s->addScenario("新案");
  s->setNameKey("N", "K");
  s->setWeeklyNotes("x");
  CHECK_FALSE(s->recordHistory(HistoryMode::Replace).ok());
  CHECK_FALSE(s->recordWeeklyAuto(WeeklyAutoMode::Replace).ok());
  CHECK_FALSE(s->recordWeeklyAuto(WeeklyAutoMode::IfMissing).ok());
  CHECK_FALSE(s->addBaseline("b").ok());
  CHECK_FALSE(s->undo());
  s->replaceIssues({}, ProjectMeta{}, {});
  CHECK(s->version() == ver0);  // 何も変わらない
  CHECK(s->issues().size() == n0);
  CHECK(s->settings().baseDate != "2030-01-01");

  // 画面の状態（表示・絞り込み・選択）は変えられる。ただしファイルへは書かない
  b.setView(View::Board);
  CHECK(b.view() == View::Board);
  b.setUi("density", "compact");
  b.filter().q = "テスト";
  b.tick();
  b.afterEdit();
  // 断った理由が通知に出る（同じ通知は重ねない）
  CHECK(hasToast(b, "読み取り専用"));
  const auto n = std::count_if(b.toasts().begin(), b.toasts().end(), [](const Toast& t) { return t.text.find("読み取り専用") != std::string::npos; });
  CHECK(n <= 3);
  e.step(2'000);
  e.step(2'000);

  CHECK(e.fs.content(kCsv) == csv0);
  CHECK(e.fs.content(kSet) == set0);
  CHECK(e.fs.writeCount() == writes0);  // 1 回も書いていない
  CHECK_FALSE(b.hasUnsaved());
  CHECK(b.saveState() == SaveState::Saved);
  CHECK(b.session()->issues().size() == n0);
}

TEST_CASE("ロック: 読み取り専用のあいだに、持ち主が保存すれば読み直す（ロックの更新では読み直さない）") {
  Env e;
  e.aliceCreates();
  e.startB();
  AppController& b = e.b->c;
  const std::size_t n0 = b.session()->issues().size();
  const std::int64_t m0 = b.workspace().entries()[0].mtime;
  // ハートビートはロックのファイルだけを書き換える
  e.step(30'000);
  e.step(30'000);
  REQUIRE(e.a->store.updates() >= 2);
  const auto r = b.workspace().sync();
  REQUIRE(r);
  CHECK(r->loaded == 0);
  CHECK(r->reloadedIds.empty());
  CHECK(r->message(false).empty());
  CHECK(b.workspace().entries()[0].mtime == m0);
  b.syncFolder(false);
  CHECK(b.toasts().empty());
  CHECK(b.session()->issues().size() == n0);
  // 持ち主が保存した
  e.a->c.session()->addIssue("", "", "Alice が足した課題");
  e.a->c.afterEdit();
  e.step(500);
  e.step(4'000);
  b.onActivated();
  CHECK(b.session()->issues().size() == n0 + 1);
  CHECK(b.readOnly());  // 読み直しても、読み取り専用のまま
  CHECK(b.session()->readOnly());
  CHECK(b.workspace().isFrozen(b.currentId()));
}

TEST_CASE("ロック: 相手が閉じたあと「再確認」で編集できる状態になる（最新の内容を読む）") {
  Env e;
  e.aliceCreates();
  e.startB();
  AppController& b = e.b->c;
  b.recheckLock();  // まだ編集中
  CHECK(b.readOnly());
  CHECK(hasToast(b, "まだ編集中"));
  e.a->c.session()->addIssue("", "", "最後の保存");
  e.a->c.afterEdit();
  e.step(500);
  const std::size_t latest = e.a->c.session()->issues().size();
  e.a.reset();  // 閉じる → ロックが消える
  e.step(1'000);
  CHECK(e.b->c.lockStatus().kind == AppController::LockKind::Other);
  b.recheckLock();
  CHECK_FALSE(b.readOnly());
  CHECK(b.lockStatus().kind == AppController::LockKind::Mine);
  CHECK_FALSE(b.session()->readOnly());
  CHECK_FALSE(b.workspace().isFrozen(b.currentId()));
  CHECK(b.session()->issues().size() == latest);
  CHECK(lockFromText(e.fs.content(kLock))->user == "Bob");
  // 編集して保存できる
  const int w = e.fs.writeCount();
  CHECK_FALSE(b.createIssue("", "").empty());
  e.step(500);
  CHECK(e.fs.writeCount() > w);
  CHECK(b.saveState() == SaveState::Saved);
}

TEST_CASE("ロック: 相手が閉じる前でも、読み取り専用の側は帯でロックの解除に気づく") {
  Env e;
  e.aliceCreates();
  e.startB();
  e.a.reset();
  e.step(30'000);  // 持ち主の様子を見直す間隔
  CHECK(e.b->c.lockStatus().free);
  CHECK(contains(e.b->c.readOnlyBand(), "ロックが解けています"));
  CHECK(e.b->c.readOnly());  // 自動では切り替えない（「再確認」を押す）
}

TEST_CASE("ロック: 古いロックの判定（更新から 2:59 は古くない、3:00 で古い）と引き継ぎ") {
  Env e;
  e.aliceCreates();
  e.a.reset();  // いったん閉じて、落ちたアプリのロックを作る
  e.fs.put(kLock, lockToText(foreign(e.clk, 179'000)));
  e.startB();
  AppController& b = e.b->c;
  CHECK(b.readOnly());
  CHECK(b.lockStatus().user == "Carol");
  CHECK_FALSE(b.lockStatus().stale);
  e.step(1'000);  // ちょうど 3:00
  CHECK(b.lockStatus().stale);
  CHECK(contains(b.readOnlyBand(), "Carolさん（PC-C）の編集中の記録が残っています"));
  CHECK(contains(b.readOnlyBand(), "3分前から更新がありません"));
  CHECK(contains(b.readOnlyBand(), "持ち主がいないようです"));
  // 引き継ぐ
  REQUIRE(b.takeOverLock());
  CHECK_FALSE(b.readOnly());
  CHECK(b.lockStatus().kind == AppController::LockKind::Mine);
  CHECK(lockFromText(e.fs.content(kLock))->user == "Bob");
  CHECK_FALSE(b.createIssue("", "").empty());
  e.step(500);
  CHECK(b.saveState() == SaveState::Saved);
}

TEST_CASE("ロック: 壊れたロックファイルは古いロックとして扱い、落ちずに引き継げる") {
  Env e;
  e.aliceCreates();
  e.a.reset();
  e.fs.put(kLock, "{ これはロックではない");
  e.startB();
  AppController& b = e.b->c;
  REQUIRE(b.session());
  CHECK(b.readOnly());
  CHECK(b.lockStatus().broken);
  CHECK(b.lockStatus().stale);
  CHECK(contains(b.readOnlyBand(), "壊れています"));
  CHECK(b.takeOverLock());
  CHECK_FALSE(b.readOnly());
  CHECK(lockFromText(e.fs.content(kLock)));
}

TEST_CASE("ロック: 新しいロックの引き継ぎ。前の持ち主は次の保存で気づき、保存せず、未保存の編集を別プロジェクトに残す") {
  Env e;
  e.aliceCreates();
  e.startB();
  AppController& a = e.a->c;
  AppController& b = e.b->c;
  const std::string csv0 = e.fs.content(kCsv);
  REQUIRE(b.takeOverLock());
  CHECK_FALSE(b.readOnly());
  CHECK(hasToast(b, "引き継ぎました"));
  // Alice は気づかずに編集する
  a.session()->addIssue("", "", "Alice の未保存の編集");
  a.afterEdit();
  CHECK(a.hasUnsaved());
  e.step(500);
  // 保存されていない（CSV は引き継いだ時点のまま）
  CHECK(e.fs.content(kCsv) == csv0);
  CHECK(a.readOnly());
  CHECK(a.lockStatus().kind == AppController::LockKind::Lost);
  CHECK(a.lockStatus().user == "Bob");
  CHECK(contains(a.readOnlyBand(), "Bobさん（PC-B）が編集を引き継いだため、読み取り専用に切り替えました"));
  CHECK(hasToast(a, "保存せずに読み取り専用に切り替えました"));
  CHECK(hasToast(a, "P（未保存の編集）"));
  CHECK_FALSE(a.hasUnsaved());
  CHECK(a.saveState() == SaveState::Saved);
  // 手元の編集は別のプロジェクトとして残り、書かれている
  const auto items = a.projects();
  const auto kept = std::find_if(items.begin(), items.end(), [](const AppController::ProjectItem& p) { return p.name == "P（未保存の編集）"; });
  REQUIRE(kept != items.end());
  const std::string keptCsv = e.fs.content("data/P（未保存の編集）/project.csv");
  CHECK(contains(keptCsv, "Alice の未保存の編集"));
  // こちらの画面はフォルダの版（Bob のもの）に読み直してある
  CHECK_FALSE(contains(e.fs.content(kCsv), "Alice の未保存の編集"));
  const auto& cur = a.session()->issues();
  CHECK(std::none_of(cur.begin(), cur.end(), [](const Issue& o) { return o.summary == "Alice の未保存の編集"; }));
  // 読み取り専用になったあとは、編集できない
  CHECK(a.createIssue("", "").empty());
  e.step(2'000);
  CHECK(e.fs.content(kCsv) == csv0);
  // Bob のロックは Bob のまま
  CHECK(lockFromText(e.fs.content(kLock))->user == "Bob");
  // 新しいプロジェクトのほうは、普通に編集できる（自分のロックを持つ）
  REQUIRE(a.openProject(kept->id));
  CHECK_FALSE(a.readOnly());
}

TEST_CASE("ロック: ハートビートで引き継ぎに気づいた場合も読み取り専用に切り替わる（保存はしていない）") {
  Env e;
  e.aliceCreates();
  e.startB();
  REQUIRE(e.b->c.takeOverLock());
  const std::string csv0 = e.fs.content(kCsv);
  e.step(30'000);
  CHECK(e.a->c.readOnly());
  CHECK(e.a->c.lockStatus().kind == AppController::LockKind::Lost);
  CHECK(e.fs.content(kCsv) == csv0);
  CHECK(lockFromText(e.fs.content(kLock))->user == "Bob");
  // 未保存の編集がなかったので、別プロジェクトは作らない
  CHECK(e.a->c.projects().size() == 1);
}

TEST_CASE("ロック: ロックのファイルが消されたら、保存せず読み取り専用に切り替える。再確認で取り直せる") {
  Env e;
  e.aliceCreates();
  AppController& a = e.a->c;
  const std::string csv0 = e.fs.content(kCsv);
  e.fs.erase(kLock);
  a.session()->addIssue("", "", "消されたあとの編集");
  a.afterEdit();
  e.step(500);
  CHECK(e.fs.content(kCsv) == csv0);
  CHECK(a.readOnly());
  CHECK(a.lockStatus().kind == AppController::LockKind::Lost);
  CHECK(a.lockStatus().free);
  CHECK(contains(a.readOnlyBand(), "ロックのファイルがなくなったため"));
  CHECK(a.projects().size() == 2);
  a.recheckLock();
  CHECK_FALSE(a.readOnly());
  CHECK(e.fs.exists(kLock));
}

TEST_CASE("ロック: ロックを確かめられない（読み込みの失敗）あいだは、失ったと決めず保存を見合わせて再試行する") {
  Env e;
  e.aliceCreates();
  AppController& a = e.a->c;
  a.session()->addIssue("", "", "ネットワークが切れていた間の編集");
  a.afterEdit();
  e.a->store.setFailRead(true);
  e.step(500);
  CHECK(a.saveState() == SaveState::Error);
  CHECK(a.hasUnsaved());
  CHECK_FALSE(a.readOnly());
  CHECK(a.projects().size() == 1);
  e.a->store.setFailRead(false);
  e.step(2'000);
  CHECK(a.saveState() == SaveState::Saved);
  CHECK_FALSE(a.hasUnsaved());
  CHECK(contains(e.fs.content(kCsv), "ネットワークが切れていた間の編集"));
}

TEST_CASE("ロック: ハートビートが書けないときは一度だけ知らせ、読み取り専用にはしない") {
  Env e;
  e.aliceCreates();
  e.a->store.setFailUpdate(true);
  e.step(30'000);
  CHECK_FALSE(e.a->c.readOnly());
  CHECK(hasToast(e.a->c, "ロックを更新できません"));
  e.step(30'000);  // 最初の通知は消えている。繰り返し知らせない
  CHECK_FALSE(e.a->c.readOnly());
  CHECK_FALSE(hasToast(e.a->c, "ロックを更新できません"));
  e.a->store.setFailUpdate(false);
  e.step(30'000);
  CHECK(lockFromText(e.fs.content(kLock))->heartbeatAt == e.clk.epochMs());
}

TEST_CASE("ロック: ロックを作れない（権限なし・読み取り専用のフォルダ）ときは理由つきの読み取り専用") {
  Env e;
  e.aliceCreates();
  e.a.reset();
  e.b = e.make("Bob", "PC-B", 2000);
  e.b->store.setFailCreate(true);
  e.b->c.start();
  AppController& b = e.b->c;
  REQUIRE(b.session());
  CHECK(b.readOnly());
  CHECK(b.lockStatus().kind == AppController::LockKind::Failed);
  CHECK_FALSE(b.lockStatus().reason.empty());
  CHECK(contains(b.readOnlyBand(), "ロックを作れないため、読み取り専用で開いています"));
  const int w = e.fs.writeCount();
  CHECK(b.createIssue("", "").empty());
  e.step(2'000);
  CHECK(e.fs.writeCount() == w);
  // 作れるようになれば「再確認」で編集できる
  e.b->store.setFailCreate(false);
  b.recheckLock();
  CHECK_FALSE(b.readOnly());
}

TEST_CASE("ロック: 「ロックを使う」をオフにすると、ロックを作らず、他の人のロックも見ない") {
  Env e;
  e.startA(",\"useLock\":false");
  e.a->c.createProject(SampleKind::Hierarchy, "P", "");
  e.step(500);
  REQUIRE(e.fs.exists(kCsv));
  CHECK(e.lockFiles().empty());
  CHECK(e.a->c.lockStatus().kind == AppController::LockKind::None);
  // 他の人のロックがあっても、見ずに編集できる
  e.fs.put(kLock, lockToText(foreign(e.clk, 1'000)));
  e.startB(",\"useLock\":false");
  CHECK_FALSE(e.b->c.readOnly());
  CHECK(e.b->store.reads() == 0);
  CHECK(e.b->store.creates() == 0);
  CHECK(lockFromText(e.fs.content(kLock))->user == "Carol");  // 触らない
  // 設定でオンにすると、いまのプロジェクトのロックを確かめる
  e.b->c.setUseLock(true);
  CHECK(e.b->c.readOnly());
  CHECK(e.b->c.prefs().useLock);
  // オフに戻すと読み取り専用が解ける
  e.b->c.setUseLock(false);
  CHECK_FALSE(e.b->c.readOnly());
  CHECK_FALSE(e.b->c.session()->readOnly());
  CHECK_FALSE(e.b->c.workspace().isFrozen(e.b->c.currentId()));
}

TEST_CASE("ロック: 設定のオン・オフで自分のロックを作る・外す。設定は prefs に残る") {
  Env e;
  e.aliceCreates();
  CHECK(e.fs.exists(kLock));
  e.a->c.setUseLock(false);
  CHECK_FALSE(e.fs.exists(kLock));
  CHECK_FALSE(e.a->c.readOnly());
  e.a->c.setUseLock(true);
  CHECK(e.fs.exists(kLock));
  CHECK(e.a->c.lockStatus().kind == AppController::LockKind::Mine);
  e.a->c.setUseLock(false);
  CHECK(contains(e.fs.content("Alice_prefs.json"), "\"useLock\": false"));
}

TEST_CASE("ロック: プロジェクトを切り替えると前のロックを外し、新しいロックを取る") {
  Env e;
  e.startA();
  e.a->c.createProject(SampleKind::Hierarchy, "P1", "");
  e.step(500);
  const std::string id1 = e.a->c.currentId();
  CHECK(e.fs.exists("data/P1/.roadmap.lock"));
  e.a->c.createProject(SampleKind::Empty, "P2", "");
  CHECK_FALSE(e.fs.exists("data/P1/.roadmap.lock"));  // 切り替えた時点で外れる
  e.step(500);
  CHECK(e.fs.exists("data/P2/.roadmap.lock"));  // 最初の保存でフォルダができたら取る
  REQUIRE(e.a->c.openProject(id1));
  CHECK(e.fs.exists("data/P1/.roadmap.lock"));
  CHECK_FALSE(e.fs.exists("data/P2/.roadmap.lock"));
  CHECK(e.lockFiles().size() == 1);
  // 切り替える前の未保存の編集は、ロックを持っているうちに書く
  e.a->c.session()->addIssue("", "", "切り替え直前の編集");
  e.a->c.afterEdit();
  e.a->c.createProject(SampleKind::Empty, "P3", "");
  CHECK(contains(e.fs.content("data/P1/project.csv"), "切り替え直前の編集"));
}

TEST_CASE("ロック: 平置きのプロジェクトにはロックを作らない（フォルダがない）") {
  Env e;
  e.fs.put("data/Flat.csv", std::string(samples::hierarchyCsv()));
  e.startA();
  REQUIRE(e.a->c.session());
  CHECK_FALSE(e.a->c.readOnly());
  CHECK(e.a->c.lockStatus().kind == AppController::LockKind::None);
  CHECK(e.lockFiles().empty());
  // フォルダ構成へ移すと、開いているプロジェクトのロックを新しいフォルダに取る。移行前の控えにロックは入らない
  auto m = e.a->c.migrateLegacy();
  REQUIRE(m);
  CHECK(m->moved == 1);
  CHECK(e.fs.exists("data/Flat/project.csv"));
  CHECK(e.fs.exists("data/Flat/.roadmap.lock"));
  CHECK(e.a->c.lockStatus().kind == AppController::LockKind::Mine);
  for (const auto& p : e.fs.paths())
    if (p.find("移行前") != std::string::npos) CHECK_FALSE(isLockFileName(p));
  CHECK(e.lockFiles().size() == 1);
}

TEST_CASE("ロック: バックアップ・削除済みへの移動・添付の合計・書き出しにロックが入らない") {
  Env e;
  e.aliceCreates();
  // 起動し直して編集すると、元の CSV のバックアップができる
  e.a.reset();
  e.startA();
  e.a->c.session()->addIssue("", "", "バックアップを作らせる編集");
  e.a->c.afterEdit();
  e.step(500);
  int backups = 0;
  for (const auto& p : e.fs.paths()) {
    if (p.find("/バックアップ/") == std::string::npos) continue;
    ++backups;
    CHECK_FALSE(isLockFileName(p));
    CHECK(p.size() > 4);
    CHECK(p.substr(p.size() - 4) == ".csv");
  }
  CHECK(backups == 1);
  // 添付の合計はメタ情報の合計（ロックのファイルは数えない）
  const std::string id0 = e.a->c.session()->issues()[0].id;
  REQUIRE(e.a->c.attachData(id0, "a.txt", "12345"));
  CHECK(attachmentsTotalBytes(e.a->c.session()->attachments()) == 5);
  // 書き出し（バックアップ JSON・共有 JSON）に、ロックの内容は入らない
  CHECK_FALSE(contains(e.a->c.exportText("backup"), "Alice"));
  CHECK_FALSE(contains(e.a->c.exportText("backup"), ".roadmap.lock"));
  CHECK_FALSE(contains(e.a->c.exportText("share"), "PC-A"));
  // 削除すると、フォルダごと _削除済み へ。ロックは持ち込まない
  REQUIRE(e.a->c.deleteCurrentProject());
  CHECK(e.lockFiles().empty());
  const auto names = e.a->c.workspace().deletedNames();
  REQUIRE(names.size() == 1);
  CHECK(e.fs.exists("data/_削除済み/" + names[0] + "/project.csv"));
  CHECK_FALSE(e.fs.exists("data/_削除済み/" + names[0] + "/.roadmap.lock"));
  // 戻すと、ロックのないフォルダとして戻る
  auto back = e.a->c.workspace().restoreDeleted(names[0]);
  REQUIRE(back);
  CHECK_FALSE(e.fs.exists("data/" + *back + "/.roadmap.lock"));
}

TEST_CASE("ロック: 削除に失敗したら、外したロックを取り直す") {
  Env e;
  e.aliceCreates();
  e.fs.setFailMoves(true);
  CHECK_FALSE(e.a->c.deleteCurrentProject());
  e.fs.setFailMoves(false);
  CHECK(e.a->c.lockStatus().kind == AppController::LockKind::Mine);
  CHECK(e.fs.exists(kLock));
  CHECK(e.a->c.session());
}

TEST_CASE("ロック: 他の人が編集中のプロジェクトは、JSON の取り込みで上書きしない") {
  Env e;
  e.aliceCreates();
  e.startB();
  AppController& b = e.b->c;
  const std::string json = b.exportText("share");
  const std::string csv0 = e.fs.content(kCsv);
  const auto out = b.importText(json, "", ImportMode::New);
  REQUIRE(out.ok);
  REQUIRE(out.conflictNames.size() == 1);
  b.confirmJsonImport(true);  // 「上書き」を選んでも
  e.step(500);
  CHECK(e.fs.content(kCsv) == csv0);
  const auto items = b.projects();
  CHECK(std::any_of(items.begin(), items.end(), [](const AppController::ProjectItem& p) { return p.name == "P（読み込み）"; }));
}

TEST_CASE("ロック: 古い版が書いた（ロックを無視した）変更を上書きしない。両方変わっていれば別プロジェクトに残す") {
  Env e;
  e.aliceCreates();
  // ロックを知らない別のアプリが CSV を書き換えた
  std::string csv = e.fs.content(kCsv);
  csv += "\n";
  e.fs.put(kCsv, csv);
  e.a->c.session()->addIssue("", "", "手元の編集");
  e.a->c.afterEdit();
  e.step(500);
  const auto items = e.a->c.projects();
  CHECK(std::any_of(items.begin(), items.end(), [](const AppController::ProjectItem& p) { return p.name == "P（フォルダの版）"; }));
  CHECK(contains(e.fs.content(kCsv), "手元の編集"));
  CHECK(e.a->c.saveState() == SaveState::Saved);
}

TEST_CASE("ロック: 窓口を渡さなければ今までどおり（ロックなし）") {
  MemFileStore fs;
  FixedClock clk{LocalTime{2026, 10, 19, 18, 36, 0}};
  AppController c(fs, clk, "data", "prefs.json");
  c.start();
  c.createProject(SampleKind::Hierarchy, "P", "");
  clk.advanceMs(500);
  c.tick();
  CHECK(fs.exists(kCsv));
  CHECK_FALSE(fs.exists(kLock));
  CHECK_FALSE(c.readOnly());
  CHECK(c.readOnlyBand().empty());
}

TEST_CASE("ロック: 自動の基準日・履歴・週の頭のスナップショットは、読み取り専用で開いたときには何もしない") {
  Env e;
  e.aliceCreates();
  const std::size_t points = e.a->c.session()->history().points.size();
  const std::size_t snaps = e.a->c.session()->weeklyAuto().snaps.size();
  const std::string base0 = e.a->c.session()->settings().baseDate;
  // 翌週に開く（編集できるなら、その週の点とスナップショットを足し、基準日を今日に合わせる）
  e.clk.set(LocalTime{2026, 11, 2, 9, 0, 0});
  e.startB(",\"autoBaseToday\":true");
  AppController& b = e.b->c;
  REQUIRE(b.readOnly());
  const int w = e.fs.writeCount();
  b.openProject(b.currentId());
  e.step(2'000);
  CHECK(e.fs.writeCount() == w);
  CHECK_FALSE(hasToast(b, "基準日を今日"));
  CHECK(b.session()->settings().baseDate == base0);
  CHECK(b.session()->history().points.size() == points);
  CHECK(b.session()->weeklyAuto().snaps.size() == snaps);
  // 同じ状況で、ロックを取れるなら足す（上の「何もしない」が、ロックのおかげだと確かめる）
  e.a.reset();
  b.recheckLock();
  REQUIRE_FALSE(b.readOnly());
  b.openProject(b.currentId());
  e.step(2'000);
  CHECK(b.session()->history().points.size() == points + 1);
  CHECK(b.session()->weeklyAuto().snaps.size() == snaps + 1);
}

// ---------------------------------------------------------------- Copilot の指摘（#88）の回帰テスト
namespace {
// 読み込み（read）の n 回目のあとで、持ち主を替える（検査と書き込みの間に引き継がれる状況を作る）
class TakeoverDuringStore final : public ILockStore {
 public:
  TakeoverDuringStore(MemLockStore& inner, FixedClock& clk) : inner_(inner), clk_(clk) {}
  int armAfterReads = -1;  // この回数だけ読んだ直後に、Bob が引き継ぐ（-1 = しない）
  LockCreateResult tryCreate(const std::string& p, const std::string& d) override { return inner_.tryCreate(p, d); }
  Result<std::optional<std::string>> read(const std::string& p) override {
    auto r = inner_.read(p);
    if (armAfterReads >= 0 && ++reads_ == armAfterReads) {
      CountingRandom rng(9000);
      (void)takeOver(inner_, clk_, rng, p, {"Bob", "PC-B"});
      armAfterReads = -1;
    }
    return r;
  }
  Status update(const std::string& p, const std::string& d) override { return inner_.update(p, d); }
  Status remove(const std::string& p) override { return inner_.remove(p); }
  void resetReads() { reads_ = 0; }

 private:
  MemLockStore& inner_;
  FixedClock& clk_;
  int reads_ = 0;
};
}  // namespace

TEST_CASE("ロック（指摘）: 引き継がれた直後の添付・削除は、本体を書く前・動かす前に止める") {
  Env e;
  e.aliceCreates();
  e.startB();
  AppController& a = e.a->c;
  const std::string id0 = a.session()->issues()[0].id;
  REQUIRE(e.b->c.takeOverLock());  // Alice はまだ気づいていない（保存もしていない）
  CHECK_FALSE(a.attachData(id0, "a.txt", "12345"));
  for (const auto& p : e.fs.paths()) CHECK(p.find("/添付/") == std::string::npos);  // 本体が書かれていない
  CHECK(a.readOnly());
  CHECK(a.attachmentCount(id0) == 0);
  // 外す
  Env f;
  f.aliceCreates();
  f.startB();
  const std::string g0 = f.a->c.session()->issues()[0].id;
  REQUIRE(f.a->c.attachData(g0, "b.txt", "abc"));
  REQUIRE(f.b->c.takeOverLock());
  CHECK_FALSE(f.a->c.removeAttachment(g0, 0));
  CHECK(f.fs.exists("data/P/添付/" + g0 + "/b.txt"));  // 動かされていない
  // 削除
  Env h;
  h.aliceCreates();
  h.startB();
  REQUIRE(h.b->c.takeOverLock());
  CHECK_FALSE(h.a->c.deleteCurrentProject());  // 引き継いだ人のフォルダを _削除済み へ動かさない
  CHECK(h.fs.exists(kCsv));
  CHECK(h.fs.exists(kLock));
  CHECK(lockFromText(h.fs.content(kLock))->user == "Bob");
}

TEST_CASE("ロック（指摘）: 引き継がれた直後にプロジェクトを切り替えても、前のプロジェクトを上書きしない") {
  for (const bool unreadable : {false, true}) {
    Env e;
    e.aliceCreates();
    e.startB();
    AppController& a = e.a->c;
    REQUIRE(e.b->c.takeOverLock());
    e.step(1'000);
    const std::string csv0 = e.fs.content(kCsv), set0 = e.fs.content(kSet);
    a.session()->addIssue("", "", "切り替え直前の編集");
    a.afterEdit();
    if (unreadable) e.a->store.setFailRead(true);  // 確かめられない場合も、前のプロジェクトへは書かない
    a.createProject(SampleKind::Empty, "Q", "");   // 検出の前に切り替える
    e.a->store.setFailRead(false);
    e.step(500);
    e.step(500);
    CHECK(e.fs.content(kCsv) == csv0);  // 引き継いだ人の版のまま
    CHECK(e.fs.content(kSet) == set0);
    CHECK(lockFromText(e.fs.content(kLock))->user == "Bob");
    // 未保存の編集は別プロジェクトとして残る
    const auto items = a.projects();
    CHECK(std::any_of(items.begin(), items.end(), [](const AppController::ProjectItem& p) { return p.name == "P（未保存の編集）"; }));
    CHECK(contains(e.fs.content("data/P（未保存の編集）/project.csv"), "切り替え直前の編集"));
    CHECK(e.fs.exists("data/Q/project.csv"));
  }
}

TEST_CASE("ロック（指摘）: 検査のあと書き込みの前に引き継がれたら、あとのファイルは書かない") {
  Env e;
  e.fs.put("Alice_prefs.json", "{\"me\":\"Alice\"}");
  MemLockStore inner(e.fs);
  TakeoverDuringStore store(inner, e.clk);
  CountingRandom rng(1000);
  AppController a(e.fs, e.clk, "data", "Alice_prefs.json");
  a.setLockStore(&store, &rng, "PC-A");
  a.start();
  a.createProject(SampleKind::Hierarchy, "P", "");
  e.clk.advanceMs(500);
  a.tick();
  REQUIRE(a.lockStatus().kind == AppController::LockKind::Mine);
  const std::string set0 = e.fs.content(kSet);
  a.session()->addIssue("", "", "途中の編集");
  a.afterEdit();
  // 保存の前の検査（1 回目）を通ったあと、CSV の書き込みの直前の検査（2 回目）の直後に引き継がれる
  store.resetReads();
  store.armAfterReads = 2;
  e.clk.advanceMs(500);
  a.tick();
  CHECK(a.readOnly());
  CHECK(e.fs.content(kSet) == set0);  // 設定は書いていない（CSV の書き込みと設定の書き込みの間にも検査する）
  CHECK(lockFromText(e.fs.content(kLock))->user == "Bob");
  const auto items = a.projects();
  CHECK(std::any_of(items.begin(), items.end(), [](const AppController::ProjectItem& p) { return p.name == "P（未保存の編集）"; }));
}

TEST_CASE("ロック（指摘）: 書いたあとに引き継ぎが分かったら、失った扱いにして知らせる") {
  Env e;
  e.fs.put("Alice_prefs.json", "{\"me\":\"Alice\"}");
  MemLockStore inner(e.fs);
  TakeoverDuringStore store(inner, e.clk);
  CountingRandom rng(1000);
  AppController a(e.fs, e.clk, "data", "Alice_prefs.json");
  a.setLockStore(&store, &rng, "PC-A");
  a.start();
  a.createProject(SampleKind::Hierarchy, "P", "");
  e.clk.advanceMs(500);
  a.tick();
  a.session()->addIssue("", "", "書いている最中の編集");
  a.afterEdit();
  // 1・2・3 回目（保存の前・CSV の前・設定の前）の検査を通り、書き終えたあとの検査（4 回目）の前に引き継がれる
  store.resetReads();
  store.armAfterReads = 3;
  e.clk.advanceMs(500);
  a.tick();
  CHECK(a.readOnly());
  CHECK(a.lockStatus().kind == AppController::LockKind::Lost);
  CHECK(hasToast(a, "保存の途中"));
}

TEST_CASE("ロック（指摘）: JSON の取り込み: ロックを確かめられないときと、他の人が編集中のときは上書きしない") {
  for (const int mode : {0, 1, 2}) {  // 0 = 読めない  1 = 他の人が編集中  2 = ロックなし（上書きできる）
    Env e;
    e.startA();
    e.a->c.createProject(SampleKind::Empty, "P1", "");
    e.step(500);
    e.a->c.createProject(SampleKind::Empty, "P2", "");  // P1 は現在のプロジェクトではなくなる
    e.step(500);
    std::string json = e.a->c.exportText("backup");
    for (std::size_t k = json.find("P1"); k != std::string::npos; k = json.find("P1", k + 3)) json.replace(k, 2, "PX1");
    const std::string set1 = e.fs.content("data/P1/project.settings.json");
    if (mode != 2) e.fs.put("data/P1/.roadmap.lock", lockToText(foreign(e.clk, mode == 1 ? 1'000 : 4 * 60 * 1000)));  // 0 は古いロックだが、読めない
    REQUIRE(e.a->c.importText(json, "", ImportMode::New).ok);
    if (mode == 0) e.a->store.setFailRead(true);
    e.a->c.confirmJsonImport(true);
    e.a->store.setFailRead(false);
    e.step(500);
    const auto items = e.a->c.projects();
    const auto has = [&](const std::string& n) {
      return std::any_of(items.begin(), items.end(), [&](const AppController::ProjectItem& p) { return p.name == n; });
    };
    if (mode == 2) {
      CHECK(e.fs.content("data/P1/project.settings.json") != set1);  // 上書きされた
      CHECK(has("PX1"));
      CHECK(e.lockFiles().size() == 1);  // 書いている間だけ取って外す。残るのは、取り込んだあとに開いたプロジェクトのロックだけ
    } else {
      CHECK(e.fs.content("data/P1/project.settings.json") == set1);  // 上書きされていない
      CHECK(has("P1"));
      CHECK(has("PX1（読み込み）"));
    }
  }
}
