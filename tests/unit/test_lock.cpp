#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <random>

#include "roadmap/lock.hpp"
#include "roadmap/project_io.hpp"
#include "roadmap/samples.hpp"
#include "roadmap/store.hpp"

using namespace roadmap;

namespace {
const std::string kPath = "data/P/.roadmap.lock";

struct Env {
  MemFileStore fs;
  MemLockStore store{fs};
  FixedClock clk{LocalTime{2026, 10, 19, 14, 5, 0}};
  CountingRandom rng{100};
  LockWho alice{"Alice", "PC-A"};
  LockWho bob{"Bob", "PC-B"};
  AcquireResult acquire(const LockWho& w) { return acquireLock(store, clk, rng, kPath, w); }
};
}  // namespace

TEST_CASE("lock: ファイル名の判定とパス") {
  CHECK(isLockFileName(".roadmap.lock"));
  CHECK(isLockFileName("data/P/.roadmap.lock"));
  CHECK(isLockFileName("C:\\data\\P\\.roadmap.lock"));
  CHECK_FALSE(isLockFileName("project.csv"));
  CHECK_FALSE(isLockFileName("x.roadmap.lock"));
  CHECK_FALSE(isLockFileName("data/.roadmap.lock/project.csv"));
  CHECK(lockPathIn("data/P") == "data/P/.roadmap.lock");
}

TEST_CASE("lock: JSON の往復・固定長・壊れた中身") {
  LockInfo i;
  i.user = "田中";
  i.machine = "PC-01";
  i.openedAt = 1792400000000;
  i.heartbeatAt = 1792400030000;
  i.token = "abc";
  const std::string t = lockToText(i);
  CHECK(t.size() == kLockFileBytes);  // 短くなっても同じ大きさ（上書きで途中の内容が見えにくい）
  const auto back = lockFromText(t);
  REQUIRE(back);
  CHECK(*back == i);
  // 長い名前は切る
  LockInfo longName = i;
  longName.user = std::string(500, 'x');
  const auto b2 = lockFromText(lockToText(longName));
  REQUIRE(b2);
  CHECK(b2->user.size() <= kLockNameChars);
  // 壊れたもの
  CHECK_FALSE(lockFromText(""));
  CHECK_FALSE(lockFromText("   "));
  CHECK_FALSE(lockFromText("{"));
  CHECK_FALSE(lockFromText("[1,2]"));
  CHECK_FALSE(lockFromText(R"({"user":"a","machine":"b"})"));                                  // トークンがない
  CHECK_FALSE(lockFromText(R"({"user":1,"machine":"b","openedAt":1,"heartbeatAt":1,"token":"t"})"));  // 型が違う
  CHECK_FALSE(lockFromText(R"({"user":"a","machine":"b","openedAt":1,"heartbeatAt":1,"token":""})"));  // 空のトークン
  CHECK(lockFromText(R"({"user":"a","machine":"b","openedAt":1,"heartbeatAt":2,"token":"t"})"));
}

TEST_CASE("lock: 古いロックの判定（2:59 は古くない、3:00 で古い）") {
  LockInfo i;
  i.heartbeatAt = 1'000'000;
  i.token = "t";
  CHECK_FALSE(lockIsStale(i, 1'000'000 + 2 * 60 * 1000 + 59 * 1000));
  CHECK(lockIsStale(i, 1'000'000 + 3 * 60 * 1000));
  CHECK(lockIsStale(i, 1'000'000 + 10 * 60 * 1000));
  CHECK_FALSE(lockIsStale(i, 1'000'000));
  CHECK_FALSE(lockIsStale(i, 1'000'000 - 5 * 60 * 1000));  // 時計が進んだマシンの更新（未来）は古くない
  CHECK(kLockStaleMs == 180000);
  CHECK(kLockHeartbeatMs == 30000);
}

TEST_CASE("lock: トークンは乱数から作る（32 文字の16進・同じ列なら同じ）") {
  CountingRandom a(1), b(1), c(7);
  const std::string t1 = newLockToken(a), t2 = newLockToken(b), t3 = newLockToken(c);
  CHECK(t1.size() == 32);
  CHECK(t1 == t2);
  CHECK(t1 != t3);
  for (char ch : t1) CHECK(((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f')));
  SystemRandom sr;
  CHECK(newLockToken(sr) != newLockToken(sr));
}

TEST_CASE("lock: 排他的に作る（2 回目は失敗して持ち主が分かる）") {
  Env e;
  const auto a = e.acquire(e.alice);
  REQUIRE(a.kind == AcquireKind::Acquired);
  CHECK(a.lock.user == "Alice");
  CHECK(a.lock.machine == "PC-A");
  CHECK(a.lock.openedAt == e.clk.epochMs());
  CHECK(a.lock.heartbeatAt == e.clk.epochMs());
  CHECK_FALSE(a.lock.token.empty());
  CHECK(e.fs.exists(kPath));

  const std::string before = e.fs.content(kPath);
  e.clk.advanceMs(10'000);
  const auto b = e.acquire(e.bob);
  REQUIRE(b.kind == AcquireKind::Held);
  CHECK(b.lock.user == "Alice");
  CHECK(b.lock.token == a.lock.token);
  CHECK_FALSE(b.stale);
  CHECK_FALSE(b.broken);
  CHECK(e.fs.content(kPath) == before);  // 何も変えない

  // 自分のトークンは別（同じ人でも 2 回目のアプリは入れない）
  const auto again = e.acquire(e.alice);
  CHECK(again.kind == AcquireKind::Held);
}

TEST_CASE("lock: 作れない（権限なし・読み取り専用のフォルダ）は理由つきの Failed") {
  Env e;
  e.store.setFailCreate(true);
  const auto a = e.acquire(e.alice);
  CHECK(a.kind == AcquireKind::Failed);
  CHECK_FALSE(a.error.empty());
  CHECK_FALSE(e.fs.exists(kPath));
}

TEST_CASE("lock: 既にあるロックが読めないと Failed（持ち主を決めつけない）") {
  Env e;
  REQUIRE(e.acquire(e.alice).kind == AcquireKind::Acquired);
  e.store.setFailRead(true);
  const auto b = e.acquire(e.bob);
  CHECK(b.kind == AcquireKind::Failed);
  CHECK_FALSE(b.error.empty());
}

TEST_CASE("lock: ハートビートで最後の更新時刻だけが進む") {
  Env e;
  auto a = e.acquire(e.alice);
  REQUIRE(a.kind == AcquireKind::Acquired);
  LockInfo mine = a.lock;
  e.clk.advanceMs(30'000);
  const auto h = heartbeat(e.store, e.clk, kPath, mine);
  REQUIRE(h.kind == Ownership::Mine);
  CHECK(mine.heartbeatAt == a.lock.heartbeatAt + 30'000);
  CHECK(mine.openedAt == a.lock.openedAt);
  CHECK(mine.token == a.lock.token);
  const auto stored = lockFromText(e.fs.content(kPath));
  REQUIRE(stored);
  CHECK(*stored == mine);
  CHECK(e.store.updates() == 1);

  // 他の人はまだ古くないと見る
  e.clk.advanceMs(2 * 60 * 1000 + 59 * 1000);
  const auto b = e.acquire(e.bob);
  CHECK(b.kind == AcquireKind::Held);
  CHECK_FALSE(b.stale);  // 更新から 2:59
  e.clk.advanceMs(1000);
  const auto c = e.acquire(e.bob);
  CHECK(c.kind == AcquireKind::Held);
  CHECK(c.stale);  // ちょうど 3:00
}

TEST_CASE("lock: ハートビートは自分のものでなければ書かない") {
  Env e;
  auto a = e.acquire(e.alice);
  LockInfo mine = a.lock;
  // 引き継がれた
  const auto t = takeOver(e.store, e.clk, e.rng, kPath, e.bob);
  REQUIRE(t.kind == AcquireKind::Acquired);
  const std::string before = e.fs.content(kPath);
  e.clk.advanceMs(30'000);
  const int updates = e.store.updates();
  const auto h = heartbeat(e.store, e.clk, kPath, mine);
  CHECK(h.kind == Ownership::Other);
  CHECK(h.current.user == "Bob");
  CHECK(e.fs.content(kPath) == before);
  CHECK(e.store.updates() == updates);
  // 消された
  REQUIRE(e.store.remove(kPath));
  CHECK(heartbeat(e.store, e.clk, kPath, mine).kind == Ownership::Missing);
  CHECK_FALSE(e.fs.exists(kPath));  // 作り直さない
}

TEST_CASE("lock: ハートビートの書き込み失敗は Unreadable（持ち主を失ったとは決めない）・時刻は進めない") {
  Env e;
  auto a = e.acquire(e.alice);
  LockInfo mine = a.lock;
  e.clk.advanceMs(30'000);
  e.store.setFailUpdate(true);
  const auto h = heartbeat(e.store, e.clk, kPath, mine);
  CHECK(h.kind == Ownership::Unreadable);
  CHECK_FALSE(h.error.empty());
  CHECK(mine.heartbeatAt == a.lock.heartbeatAt);
  e.store.setFailUpdate(false);
  CHECK(heartbeat(e.store, e.clk, kPath, mine).kind == Ownership::Mine);
  CHECK(mine.heartbeatAt == a.lock.heartbeatAt + 30'000);
}

TEST_CASE("lock: 保存の前の検査（自分・別の人・なし・壊れている・読めない）") {
  Env e;
  const auto a = e.acquire(e.alice);
  REQUIRE(a.kind == AcquireKind::Acquired);
  CHECK(checkOwnership(e.store, kPath, a.lock.token).kind == Ownership::Mine);
  const int updates = e.store.updates();
  CHECK(e.store.updates() == updates);  // 検査は書かない

  // トークンの不一致
  const auto r = checkOwnership(e.store, kPath, "別のトークン");
  CHECK(r.kind == Ownership::Other);
  CHECK(r.current.user == "Alice");
  CHECK(r.current.token == a.lock.token);
  // 空のトークンは常に不一致
  CHECK(checkOwnership(e.store, kPath, "").kind == Ownership::Other);

  e.fs.put(kPath, "壊れた{");
  CHECK(checkOwnership(e.store, kPath, a.lock.token).kind == Ownership::Broken);
  e.fs.put(kPath, "");
  CHECK(checkOwnership(e.store, kPath, a.lock.token).kind == Ownership::Broken);

  REQUIRE(e.store.remove(kPath));
  CHECK(checkOwnership(e.store, kPath, a.lock.token).kind == Ownership::Missing);

  e.store.setFailRead(true);
  const auto u = checkOwnership(e.store, kPath, a.lock.token);
  CHECK(u.kind == Ownership::Unreadable);
  CHECK_FALSE(u.error.empty());
}

TEST_CASE("lock: 壊れたロックファイルは古いロックとして扱う（落ちない）") {
  Env e;
  e.fs.put(kPath, "{ これは JSON ではない");
  const auto b = e.acquire(e.bob);
  CHECK(b.kind == AcquireKind::Held);
  CHECK(b.broken);
  CHECK(b.stale);
  CHECK(b.lock.token.empty());
  // 空のファイルも
  e.fs.put(kPath, "");
  const auto c = e.acquire(e.bob);
  CHECK(c.kind == AcquireKind::Held);
  CHECK(c.broken);
  CHECK(c.stale);
  // 引き継げる
  const auto t = takeOver(e.store, e.clk, e.rng, kPath, e.bob);
  REQUIRE(t.kind == AcquireKind::Acquired);
  CHECK(checkOwnership(e.store, kPath, t.lock.token).kind == Ownership::Mine);
}

TEST_CASE("lock: 引き継ぎ（新しいトークンで上書き。前の持ち主は次の検査で気づく）") {
  Env e;
  const auto a = e.acquire(e.alice);
  REQUIRE(a.kind == AcquireKind::Acquired);
  e.clk.advanceMs(5'000);
  const auto t = takeOver(e.store, e.clk, e.rng, kPath, e.bob);
  REQUIRE(t.kind == AcquireKind::Acquired);
  CHECK(t.lock.user == "Bob");
  CHECK(t.lock.machine == "PC-B");
  CHECK(t.lock.token != a.lock.token);
  CHECK(t.lock.openedAt == e.clk.epochMs());
  CHECK(checkOwnership(e.store, kPath, a.lock.token).kind == Ownership::Other);
  CHECK(checkOwnership(e.store, kPath, t.lock.token).kind == Ownership::Mine);
  // 前の持ち主は解放しても何もしない（自分のものではない）
  const auto rel = releaseLock(e.store, kPath, a.lock.token);
  REQUIRE(rel);
  CHECK_FALSE(*rel);
  CHECK(e.fs.exists(kPath));
}

TEST_CASE("lock: ロックがないところでも引き継ぎは作る・書けなければ Failed") {
  Env e;
  const auto t = takeOver(e.store, e.clk, e.rng, kPath, e.bob);
  REQUIRE(t.kind == AcquireKind::Acquired);
  CHECK(e.fs.exists(kPath));
  e.store.setFailUpdate(true);
  const auto f = takeOver(e.store, e.clk, e.rng, kPath, e.alice);
  CHECK(f.kind == AcquireKind::Failed);
  CHECK_FALSE(f.error.empty());
}

TEST_CASE("lock: 解放は自分のものだけ") {
  Env e;
  const auto a = e.acquire(e.alice);
  REQUIRE(a.kind == AcquireKind::Acquired);
  // 別のトークンでは消えない
  auto r = releaseLock(e.store, kPath, "違う");
  REQUIRE(r);
  CHECK_FALSE(*r);
  CHECK(e.fs.exists(kPath));
  // 壊れたものも消さない（持ち主を確かめられない）
  e.fs.put(kPath, "壊れた");
  r = releaseLock(e.store, kPath, a.lock.token);
  REQUIRE(r);
  CHECK_FALSE(*r);
  CHECK(e.fs.exists(kPath));
  // 自分のものは消える
  REQUIRE(e.store.remove(kPath));
  const auto a2 = e.acquire(e.alice);
  r = releaseLock(e.store, kPath, a2.lock.token);
  REQUIRE(r);
  CHECK(*r);
  CHECK_FALSE(e.fs.exists(kPath));
  // もうない
  r = releaseLock(e.store, kPath, a2.lock.token);
  REQUIRE(r);
  CHECK_FALSE(*r);
  // 解放したあとは別の人が取れる
  CHECK(e.acquire(e.bob).kind == AcquireKind::Acquired);
}

TEST_CASE("lock: 検査と解放は、読めないときに何も消さない") {
  Env e;
  const auto a = e.acquire(e.alice);
  e.store.setFailRead(true);
  const auto r = releaseLock(e.store, kPath, a.lock.token);
  CHECK_FALSE(r.ok());
  e.store.setFailRead(false);
  CHECK(e.fs.exists(kPath));
}

#ifndef _WIN32
TEST_CASE("lock: 実ファイル（POSIX）でも排他的に作れる・同じ場所に上書きできる・消せる") {
  namespace stdfs = std::filesystem;
  const stdfs::path dir = stdfs::temp_directory_path() / ("roadmap_lock_" + std::to_string(std::random_device{}()));
  stdfs::create_directories(dir);
  const std::string path = (dir / ".roadmap.lock").string();
  PosixLockStore store;
  FixedClock clk{LocalTime{2026, 10, 19, 14, 5, 0}};
  CountingRandom rng(5);
  const auto a = acquireLock(store, clk, rng, path, {"Alice", "PC-A"});
  REQUIRE(a.kind == AcquireKind::Acquired);
  CHECK(stdfs::file_size(path) == kLockFileBytes);
  const auto b = acquireLock(store, clk, rng, path, {"Bob", "PC-B"});
  REQUIRE(b.kind == AcquireKind::Held);
  CHECK(b.lock.user == "Alice");
  LockInfo mine = a.lock;
  clk.advanceMs(30'000);
  REQUIRE(heartbeat(store, clk, path, mine).kind == Ownership::Mine);
  CHECK(stdfs::file_size(path) == kLockFileBytes);
  const auto t = takeOver(store, clk, rng, path, {"Bob", "PC-B"});
  REQUIRE(t.kind == AcquireKind::Acquired);
  CHECK(checkOwnership(store, path, a.lock.token).kind == Ownership::Other);
  const auto rel = releaseLock(store, path, t.lock.token);
  REQUIRE(rel);
  CHECK(*rel);
  CHECK_FALSE(stdfs::exists(path));
  // 存在しないフォルダには作れない
  const auto f = acquireLock(store, clk, rng, (dir / "なし" / ".roadmap.lock").string(), {"Alice", "PC-A"});
  CHECK(f.kind == AcquireKind::Failed);
  stdfs::remove_all(dir);
}
#endif

// ---------------------------------------------------------------- Workspace（読み取り専用・ロックの除外）
namespace {
struct WsEnv {
  MemFileStore fs;
  FixedClock clk{LocalTime{2026, 10, 19, 14, 5, 0}};
  Workspace ws{fs, clk, "data"};
  std::string add(const std::string& name) {
    const std::string id = ws.add(newRecord(name, "", std::string(samples::hierarchyCsv()), "2026-10-19", clk), true);
    REQUIRE(ws.flush());
    return id;
  }
};
}  // namespace

TEST_CASE("lock: 読み取り専用のプロジェクトは Workspace が書かない（更新・保存・同期の書き戻し）") {
  WsEnv e;
  const std::string id = e.add("P");
  const int w = e.fs.writeCount();
  e.ws.setFrozen(id, true);
  CHECK(e.ws.isFrozen(id));
  ProjectRecord changed = e.ws.find(id)->rec;
  changed.settings.name = "改名";
  e.ws.update(id, changed);
  ProjectSettings st = changed.settings;
  st.baseDate = "2030-01-01";
  e.ws.updateSettings(id, st);
  CHECK_FALSE(e.ws.hasDirty());
  CHECK(e.ws.find(id)->rec.settings.name == "P");
  // 無理に dirty にしても書かない
  e.ws.find(id)->dirty = true;
  CHECK(e.ws.flush());
  CHECK(e.fs.writeCount() == w);
  CHECK_FALSE(e.ws.remove(id).ok());  // 削除もしない
  e.ws.find(id)->dirty = false;
  // フォルダから消えても、書き戻さない（読み取り専用の側が、相手の削除を元に戻さない）
  e.fs.erase("data/P/project.csv");
  e.fs.erase("data/P/project.settings.json");
  REQUIRE(e.ws.sync());
  CHECK_FALSE(e.fs.exists("data/P/project.csv"));
  CHECK(e.fs.writeCount() == w);
  // 解けば、今までどおり
  e.ws.setFrozen(id, false);
  REQUIRE(e.ws.sync());
  CHECK(e.fs.exists("data/P/project.csv"));
}

TEST_CASE("lock: 未保存の編集を捨てると、次の同期でフォルダの版を読み直す") {
  WsEnv e;
  const std::string id = e.add("P");
  ProjectRecord changed = e.ws.find(id)->rec;
  changed.settings.name = "手元の改名";
  e.ws.update(id, changed);
  REQUIRE(e.ws.hasDirty());
  e.ws.discardUnsaved(id);
  CHECK_FALSE(e.ws.hasDirty());
  auto r = e.ws.sync();
  REQUIRE(r);
  CHECK(r->loaded == 1);
  CHECK(r->reloadedIds == std::vector<std::string>{id});
  CHECK(e.ws.find(id)->rec.settings.name == "P");
}

TEST_CASE("lock: 未保存の編集があり、フォルダのファイルが自分の記録より新しいときだけ検出する") {
  WsEnv e;
  const std::string id = e.add("P");
  CHECK(e.ws.externallyChangedDirty().empty());
  ProjectRecord changed = e.ws.find(id)->rec;
  changed.settings.name = "手元の改名";
  e.ws.update(id, changed);
  CHECK(e.ws.externallyChangedDirty().empty());  // 自分が最後に書いたまま
  e.fs.put("data/P/project.csv", e.fs.content("data/P/project.csv") + "\n");  // 別のアプリが書いた
  CHECK(e.ws.externallyChangedDirty() == std::vector<std::string>{id});
  // 未保存の編集がなければ、検出しない（読み直す側の話）
  e.ws.discardUnsaved(id);
  CHECK(e.ws.externallyChangedDirty().empty());
}

TEST_CASE("lock: ロックのファイルはプロジェクトの更新時刻に入らず、一覧にも現れない") {
  WsEnv e;
  e.add("P");
  auto before = scanProjects(e.fs, "data");
  REQUIRE(before);
  REQUIRE(before->size() == 1);
  e.fs.put("data/P/.roadmap.lock", "x");
  e.fs.put("data/P/.roadmap.lock", "y");  // ハートビート
  auto after = scanProjects(e.fs, "data");
  REQUIRE(after);
  REQUIRE(after->size() == 1);
  CHECK(after->front().mtime == before->front().mtime);
  CHECK(after->front().csvPath == before->front().csvPath);
  auto r = e.ws.sync();
  REQUIRE(r);
  CHECK(r->loaded == 0);
  CHECK(r->added == 0);
  CHECK(r->message(false).empty());
}

TEST_CASE("lock: プロジェクトを _削除済み へ移すとき、ロックは持ち込まない") {
  WsEnv e;
  const std::string id = e.add("P");
  e.fs.put("data/P/.roadmap.lock", "x");
  REQUIRE(e.ws.remove(id));
  CHECK_FALSE(e.fs.exists("data/P/.roadmap.lock"));
  for (const auto& p : e.fs.paths()) CHECK_FALSE(isLockFileName(p));
  const auto names = e.ws.deletedNames();
  REQUIRE(names.size() == 1);
  CHECK(e.fs.exists("data/_削除済み/" + names[0] + "/project.csv"));
}
