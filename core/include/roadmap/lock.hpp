// 共有フォルダで交代して使うための「編集中のロック」（SPEC 8.5。Issue #62）
//   <プロジェクトのフォルダ>/.roadmap.lock （JSON。利用者名・マシン名・開いた時刻・最後の更新時刻・自分を識別するトークン）
// UI・Win32 に依存しない。ファイルの操作は ILockStore、時計は IClock、乱数は IRandom で差し替えられる。
// 失敗は戻り値で返す（例外を出さない）。ロックの中身（利用者名・マシン名）はログに出さない。
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "roadmap/clock.hpp"
#include "roadmap/filestore.hpp"
#include "roadmap/result.hpp"

namespace roadmap {

inline constexpr const char* kLockFile = ".roadmap.lock";
// 最後の更新からこの時間（ミリ秒）以上たったロックは、持ち主がいないもの（アプリが落ちた・ネットワークが切れた）として引き継げる
inline constexpr std::int64_t kLockStaleMs = 3 * 60 * 1000;
// 編集できる状態で開いている間、最後の更新時刻を書き換める間隔（ミリ秒）
inline constexpr std::int64_t kLockHeartbeatMs = 30 * 1000;
// ファイルの大きさ（バイト）。更新は同じ場所への上書きなので、いつも同じ大きさに空白で埋め、読む側が途中の内容を見にくくする
inline constexpr std::size_t kLockFileBytes = 1024;
inline constexpr std::size_t kLockNameChars = 64;  // 利用者名・マシン名の最大の長さ（文字）

// パスの最後の名前が .roadmap.lock か（フォルダ監視・バックアップ・移動などがロックを除くために使う）
bool isLockFileName(std::string_view pathOrName);
std::string lockPathIn(const std::string& projectDir);

struct LockInfo {
  std::string user;
  std::string machine;
  std::int64_t openedAt = 0;     // 開いた時刻（UTC のミリ秒）
  std::int64_t heartbeatAt = 0;  // 最後の更新時刻（UTC のミリ秒）
  std::string token;             // 自分を識別するランダムな文字列
  bool operator==(const LockInfo&) const = default;
};
// ファイルの中身（JSON を空白で kLockFileBytes まで埋めたもの）と、その逆。壊れていれば nullopt
std::string lockToText(const LockInfo& info);
std::optional<LockInfo> lockFromText(std::string_view text);

// 古いか（最後の更新から staleMs 以上。時計が進みすぎている＝未来の時刻は古くない扱い）
bool lockIsStale(const LockInfo& info, std::int64_t nowEpochMs, std::int64_t staleMs = kLockStaleMs);

// ---- 乱数（トークン用。テストでは固定列を使う）----
class IRandom {
 public:
  virtual ~IRandom() = default;
  virtual std::uint64_t next() = 0;
};
class SystemRandom final : public IRandom {
 public:
  SystemRandom();
  std::uint64_t next() override;

 private:
  std::uint64_t s_[2];
};
// 1, 2, 3 … を返す（テスト用）
class CountingRandom final : public IRandom {
 public:
  explicit CountingRandom(std::uint64_t start = 1) : n_(start) {}
  std::uint64_t next() override { return n_++; }

 private:
  std::uint64_t n_;
};
std::string newLockToken(IRandom& rng);  // 32 文字の16進

// ---- ファイルの差し替え口 ----
enum class LockCreate { Created, Exists, Failed };
struct LockCreateResult {
  LockCreate kind = LockCreate::Failed;
  std::string error;  // Failed のとき
};
class ILockStore {
 public:
  virtual ~ILockStore() = default;
  // 排他的に作って data を書く（既にあれば Exists で何も変えない）
  virtual LockCreateResult tryCreate(const std::string& path, const std::string& data) = 0;
  // ファイルがなければ nullopt。読めなければエラー
  virtual Result<std::optional<std::string>> read(const std::string& path) = 0;
  // 既にあるファイルの同じ場所へ書く（一時ファイルへ書いて置き換えない。共有ドライブで他のマシンが開いていても書けるように）
  virtual Status update(const std::string& path, const std::string& data) = 0;
  // 消す（なくても成功）
  virtual Status remove(const std::string& path) = 0;
};

// メモリ上の実装（テスト用）。ファイルは MemFileStore の中に置く（フォルダの移動・一覧にそのまま現れる）。失敗を差し込める
class MemLockStore final : public ILockStore {
 public:
  explicit MemLockStore(MemFileStore& fs) : fs_(fs) {}
  LockCreateResult tryCreate(const std::string& path, const std::string& data) override;
  Result<std::optional<std::string>> read(const std::string& path) override;
  Status update(const std::string& path, const std::string& data) override;
  Status remove(const std::string& path) override;

  void setFailCreate(bool f) { failCreate_ = f; }  // 書き込み禁止のフォルダ・権限なし
  void setFailRead(bool f) { failRead_ = f; }
  void setFailUpdate(bool f) { failUpdate_ = f; }
  int creates() const { return creates_; }
  int reads() const { return reads_; }
  int updates() const { return updates_; }
  int removes() const { return removes_; }

 private:
  MemFileStore& fs_;
  bool failCreate_ = false, failRead_ = false, failUpdate_ = false;
  int creates_ = 0, reads_ = 0, updates_ = 0, removes_ = 0;
};

#ifndef _WIN32
// POSIX の実装（open(O_CREAT|O_EXCL)）。Linux でのテスト・確認用。Windows のアプリは app/platform の実装を使う
class PosixLockStore final : public ILockStore {
 public:
  LockCreateResult tryCreate(const std::string& path, const std::string& data) override;
  Result<std::optional<std::string>> read(const std::string& path) override;
  Status update(const std::string& path, const std::string& data) override;
  Status remove(const std::string& path) override;
};
#endif

// ---- ロックの操作 ----
struct LockWho {
  std::string user;
  std::string machine;
};

enum class AcquireKind { Acquired, Held, Failed };
struct AcquireResult {
  AcquireKind kind = AcquireKind::Failed;
  LockInfo lock;        // Acquired: 自分のロック / Held: 持ち主のロック（壊れていれば空）
  bool stale = false;   // Held: 古い（壊れたロックを含む）
  bool broken = false;  // Held: 中身を読めない
  std::string error;    // Failed: 理由
};
// 排他的に作る。既にあれば作らず、持ち主を返す（Held）。作れない（権限なし・読み取り専用のフォルダ）ときは Failed
AcquireResult acquireLock(ILockStore& store, const IClock& clock, IRandom& rng, const std::string& path, const LockWho& who);
// 他の人（古いものも）のロックを引き継ぐ。新しいトークンで上書きするので、前の持ち主は次の保存の前の検査で気づく。
// 書いたあとに読み直し、別の人に先を越されていたら Held を返す
AcquireResult takeOver(ILockStore& store, const IClock& clock, IRandom& rng, const std::string& path, const LockWho& who);

enum class Ownership {
  Mine,        // 自分のロック
  Other,       // 別の人のロック（current に持ち主）
  Missing,     // ロックのファイルがない
  Broken,      // 中身を読めない（壊れている）
  Unreadable   // ファイルの読み込みに失敗した（ネットワークの不調など。持ち主は分からない）
};
struct OwnershipResult {
  Ownership kind = Ownership::Unreadable;
  LockInfo current;   // Other: 持ち主 / Mine: 今のロック
  std::string error;  // Unreadable のとき
};
// 保存の前の検査。読むだけ（書かない）
OwnershipResult checkOwnership(ILockStore& store, const std::string& path, const std::string& token);
// 自分のロックなら最後の更新時刻を書き換える（mine も更新する）。自分のものでなければ書かない。書けなければ Unreadable
OwnershipResult heartbeat(ILockStore& store, const IClock& clock, const std::string& path, LockInfo& mine);
// 自分のロックのときだけ消す。消したら true（自分のものでない・なかったときは false で、何もしない）
Result<bool> releaseLock(ILockStore& store, const std::string& path, const std::string& token);

}  // namespace roadmap
