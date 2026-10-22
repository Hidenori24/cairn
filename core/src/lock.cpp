#include "roadmap/lock.hpp"

#include <algorithm>
#include <random>
#include <system_error>

#include "roadmap/model.hpp"
#include "roadmap/text.hpp"

#ifndef _WIN32
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace roadmap {

bool isLockFileName(std::string_view pathOrName) {
  const std::size_t k = pathOrName.find_last_of("/\\");
  const std::string_view name = k == std::string_view::npos ? pathOrName : pathOrName.substr(k + 1);
  return name == kLockFile;
}

std::string lockPathIn(const std::string& projectDir) { return joinPath(projectDir, kLockFile); }

// ---------------------------------------------------------------- 中身
namespace {
std::string clip(const std::string& s) {
  const std::u32string u = text::toU32(text::sanitizeUtf8(s));
  if (u.size() <= kLockNameChars) return text::fromU32(u);
  return text::fromU32(std::u32string_view(u).substr(0, kLockNameChars));
}
}  // namespace

std::string lockToText(const LockInfo& info) {
  Json j = Json::object();
  j["v"] = 1;
  j["user"] = clip(info.user);
  j["machine"] = clip(info.machine);
  j["openedAt"] = info.openedAt;
  j["heartbeatAt"] = info.heartbeatAt;
  j["token"] = info.token;
  std::string s = j.dump();
  if (s.size() < kLockFileBytes) s.append(kLockFileBytes - s.size(), ' ');
  return s;
}

std::optional<LockInfo> lockFromText(std::string_view textIn) {
  if (textIn.empty()) return std::nullopt;
  const Json j = Json::parse(textIn.begin(), textIn.end(), nullptr, false);  // 例外を出さない
  if (j.is_discarded() || !j.is_object()) return std::nullopt;
  const auto str = [&](const char* k, std::string& out) {
    if (!j.contains(k) || !j[k].is_string()) return false;
    out = j[k].get<std::string>();
    return true;
  };
  const auto num = [&](const char* k, std::int64_t& out) {
    if (!j.contains(k) || !j[k].is_number_integer()) return false;
    out = j[k].get<std::int64_t>();
    return true;
  };
  LockInfo i;
  if (!str("user", i.user) || !str("machine", i.machine) || !str("token", i.token) || i.token.empty()) return std::nullopt;
  if (!num("openedAt", i.openedAt) || !num("heartbeatAt", i.heartbeatAt)) return std::nullopt;
  return i;
}

bool lockIsStale(const LockInfo& info, std::int64_t nowEpochMs, std::int64_t staleMs) {
  return nowEpochMs - info.heartbeatAt >= staleMs;  // 未来の時刻（差が負）は古くない
}

// ---------------------------------------------------------------- 乱数
SystemRandom::SystemRandom() {
  std::random_device rd;
  for (auto& x : s_) x = (static_cast<std::uint64_t>(rd()) << 32) ^ rd();
  if (s_[0] == 0 && s_[1] == 0) s_[0] = 0x9e3779b97f4a7c15ull;
}

std::uint64_t SystemRandom::next() {  // xorshift128+
  std::uint64_t x = s_[0];
  const std::uint64_t y = s_[1];
  s_[0] = y;
  x ^= x << 23;
  s_[1] = x ^ y ^ (x >> 17) ^ (y >> 26);
  return s_[1] + y;
}

std::string newLockToken(IRandom& rng) {
  static const char kHex[] = "0123456789abcdef";
  std::string t;
  for (int w = 0; w < 2; ++w) {
    const std::uint64_t v = rng.next();
    for (int shift = 60; shift >= 0; shift -= 4) t += kHex[(v >> shift) & 0xf];
  }
  return t;
}

// ---------------------------------------------------------------- メモリ上の実装
LockCreateResult MemLockStore::tryCreate(const std::string& path, const std::string& data) {
  if (failCreate_) return {LockCreate::Failed, "ロックのファイルを作れません（テスト）"};
  if (fs_.exists(path)) return {LockCreate::Exists, {}};
  ++creates_;
  fs_.put(path, data);
  return {LockCreate::Created, {}};
}

Result<std::optional<std::string>> MemLockStore::read(const std::string& path) {
  ++reads_;
  if (failRead_) return Error{"ロックのファイルを読めません（テスト）"};
  if (!fs_.exists(path)) return std::optional<std::string>{};
  return std::optional<std::string>{fs_.content(path)};
}

Status MemLockStore::update(const std::string& path, const std::string& data) {
  if (failUpdate_) return Error{"ロックのファイルに書けません（テスト）"};
  if (!fs_.exists(path)) return Error{"ロックのファイルがありません"};
  ++updates_;
  fs_.put(path, data);
  return okStatus();
}

Status MemLockStore::remove(const std::string& path) {
  ++removes_;
  fs_.erase(path);
  return okStatus();
}

// ---------------------------------------------------------------- POSIX の実装
#ifndef _WIN32
namespace {
std::string errnoMessage(const char* what) { return std::string(what) + ": " + std::generic_category().message(errno); }

bool writeAll(int fd, const std::string& data) {
  std::size_t off = 0;
  while (off < data.size()) {
    const ssize_t n = ::pwrite(fd, data.data() + off, data.size() - off, static_cast<off_t>(off));
    if (n < 0) {
      if (errno == EINTR) continue;
      return false;
    }
    off += static_cast<std::size_t>(n);
  }
  return true;
}
}  // namespace

LockCreateResult PosixLockStore::tryCreate(const std::string& path, const std::string& data) {
  const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
  if (fd < 0) {
    if (errno == EEXIST) return {LockCreate::Exists, {}};
    return {LockCreate::Failed, errnoMessage("ロックのファイルを作れません")};
  }
  const bool ok = writeAll(fd, data);
  const std::string err = ok ? std::string() : errnoMessage("ロックのファイルに書けません");
  ::close(fd);
  if (!ok) {
    ::unlink(path.c_str());
    return {LockCreate::Failed, err};
  }
  return {LockCreate::Created, {}};
}

Result<std::optional<std::string>> PosixLockStore::read(const std::string& path) {
  const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
  if (fd < 0) {
    if (errno == ENOENT) return std::optional<std::string>{};
    return Error{errnoMessage("ロックのファイルを読めません")};
  }
  std::string out;
  char buf[4096];
  while (out.size() < 65536) {
    const ssize_t n = ::read(fd, buf, sizeof buf);
    if (n < 0) {
      if (errno == EINTR) continue;
      const std::string err = errnoMessage("ロックのファイルを読めません");
      ::close(fd);
      return Error{err};
    }
    if (n == 0) break;
    out.append(buf, static_cast<std::size_t>(n));
  }
  ::close(fd);
  return std::optional<std::string>{std::move(out)};
}

Status PosixLockStore::update(const std::string& path, const std::string& data) {
  const int fd = ::open(path.c_str(), O_WRONLY | O_CLOEXEC);  // O_TRUNC なし（途中で空のファイルを見せない）
  if (fd < 0) return Error{errnoMessage("ロックのファイルに書けません")};
  bool ok = writeAll(fd, data);
  struct stat st {};
  if (ok && ::fstat(fd, &st) == 0 && static_cast<std::size_t>(st.st_size) > data.size())
    ok = ::ftruncate(fd, static_cast<off_t>(data.size())) == 0;
  const std::string err = ok ? std::string() : errnoMessage("ロックのファイルに書けません");
  ::close(fd);
  if (!ok) return Error{err};
  return okStatus();
}

Status PosixLockStore::remove(const std::string& path) {
  if (::unlink(path.c_str()) != 0 && errno != ENOENT) return Error{errnoMessage("ロックのファイルを消せません")};
  return okStatus();
}
#endif

// ---------------------------------------------------------------- 操作
namespace {
struct Loaded {
  bool ok = false;       // 読めた（なくても ok）
  bool missing = false;  // ファイルがない
  std::optional<LockInfo> info;  // 読めて、中身が正しい
  std::string error;
};

// 読む。中身が読めないときは、書き込みの途中を見た可能性があるので、すぐに数回読み直す
Loaded loadLock(ILockStore& store, const std::string& path) {
  Loaded l;
  for (int attempt = 0; attempt < 3; ++attempt) {
    auto r = store.read(path);
    if (!r) {
      l.error = r.error();
      return l;
    }
    l.ok = true;
    if (!r->has_value()) {
      l.missing = true;
      return l;
    }
    l.info = lockFromText(**r);
    if (l.info) return l;
  }
  return l;  // ok = true で info なし = 壊れている
}

LockInfo newInfo(const IClock& clock, IRandom& rng, const LockWho& who) {
  LockInfo i;
  i.user = who.user;
  i.machine = who.machine;
  i.openedAt = i.heartbeatAt = clock.epochMs();
  i.token = newLockToken(rng);
  return i;
}

AcquireResult held(const Loaded& cur, const IClock& clock) {
  AcquireResult r;
  r.kind = AcquireKind::Held;
  if (cur.info) {
    r.lock = *cur.info;
    r.stale = lockIsStale(*cur.info, clock.epochMs());
  } else {
    r.broken = true;
    r.stale = true;  // 壊れたロックは古いロックとして扱う
  }
  return r;
}
}  // namespace

AcquireResult acquireLock(ILockStore& store, const IClock& clock, IRandom& rng, const std::string& path, const LockWho& who) {
  const LockInfo mine = newInfo(clock, rng, who);
  const std::string text = lockToText(mine);
  AcquireResult r;
  for (int attempt = 0; attempt < 3; ++attempt) {
    const LockCreateResult c = store.tryCreate(path, text);
    if (c.kind == LockCreate::Created) {
      r.kind = AcquireKind::Acquired;
      r.lock = mine;
      return r;
    }
    if (c.kind == LockCreate::Failed) {
      r.kind = AcquireKind::Failed;
      r.error = c.error;
      return r;
    }
    const Loaded cur = loadLock(store, path);
    if (!cur.ok) {
      r.kind = AcquireKind::Failed;
      r.error = cur.error;
      return r;
    }
    if (cur.missing) continue;  // 作ろうとした直後に消えた。もう一度作る
    return held(cur, clock);
  }
  r.kind = AcquireKind::Failed;
  r.error = "ロックを取れませんでした（ファイルが作られては消えています）";
  return r;
}

AcquireResult takeOver(ILockStore& store, const IClock& clock, IRandom& rng, const std::string& path, const LockWho& who) {
  const LockInfo mine = newInfo(clock, rng, who);
  const std::string text = lockToText(mine);
  AcquireResult r;
  const auto cur = store.read(path);
  if (!cur) {
    r.error = cur.error();
    return r;
  }
  bool written = false;
  if (!cur->has_value()) {
    const LockCreateResult c = store.tryCreate(path, text);
    if (c.kind == LockCreate::Failed) {
      r.error = c.error;
      return r;
    }
    written = c.kind == LockCreate::Created;  // Exists = 読んだ直後に別の人が作った。上書きで引き継ぐ
  }
  if (!written) {
    if (const Status s = store.update(path, text); !s) {
      r.error = s.error();
      return r;
    }
  }
  // 書いたあとで読み直し、自分のものか確かめる（同時に引き継ごうとした別の人に先を越されていないか）
  const Loaded back = loadLock(store, path);
  if (!back.ok) {
    r.error = back.error;
    return r;
  }
  if (back.info && back.info->token == mine.token) {
    r.kind = AcquireKind::Acquired;
    r.lock = mine;
    return r;
  }
  if (back.missing) {
    r.error = "ロックのファイルが消えました";
    return r;
  }
  return held(back, clock);
}

OwnershipResult checkOwnership(ILockStore& store, const std::string& path, const std::string& token) {
  OwnershipResult r;
  const Loaded cur = loadLock(store, path);
  if (!cur.ok) {
    r.kind = Ownership::Unreadable;
    r.error = cur.error;
    return r;
  }
  if (cur.missing) {
    r.kind = Ownership::Missing;
    return r;
  }
  if (!cur.info) {
    r.kind = Ownership::Broken;
    return r;
  }
  r.current = *cur.info;
  r.kind = !token.empty() && cur.info->token == token ? Ownership::Mine : Ownership::Other;
  return r;
}

OwnershipResult heartbeat(ILockStore& store, const IClock& clock, const std::string& path, LockInfo& mine) {
  OwnershipResult r = checkOwnership(store, path, mine.token);
  if (r.kind != Ownership::Mine) return r;
  LockInfo next = mine;
  next.heartbeatAt = clock.epochMs();
  if (const Status s = store.update(path, lockToText(next)); !s) {
    r.kind = Ownership::Unreadable;
    r.error = s.error();
    return r;
  }
  mine = next;
  r.current = next;
  return r;
}

Result<bool> releaseLock(ILockStore& store, const std::string& path, const std::string& token) {
  const OwnershipResult o = checkOwnership(store, path, token);
  if (o.kind == Ownership::Unreadable) return Error{o.error};
  if (o.kind != Ownership::Mine) return false;
  if (const Status s = store.remove(path); !s) return Error{s.error()};
  return true;
}

}  // namespace roadmap
