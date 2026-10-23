// ファイルシステムの差し替え口（SPEC 3.1 の IFileStore）。パスは UTF-8、区切りは '/'。
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "roadmap/result.hpp"

namespace roadmap {

struct FileInfo {
  std::string name;        // フォルダ内の名前
  std::int64_t mtime = 0;  // 更新時刻（ミリ秒。比較にだけ使う）
  std::uint64_t size = 0;
  bool isDir = false;
};

class IFileStore {
 public:
  virtual ~IFileStore() = default;
  virtual Result<std::vector<FileInfo>> list(const std::string& dir) = 0;
  virtual std::optional<FileInfo> stat(const std::string& path) = 0;
  virtual Result<std::string> read(const std::string& path) = 0;
  // 原子的に書く（同じフォルダの一時ファイルに書いて閉じ、置き換える）
  virtual Status writeAtomic(const std::string& path, const std::string& data) = 0;
  virtual Status remove(const std::string& path) = 0;
  virtual Status makeDirs(const std::string& dir) = 0;
  // ファイルまたはフォルダ（中身ごと）を移す・名前を変える。行き先に同じ名前があれば失敗する（上書きしない）
  virtual Status move(const std::string& from, const std::string& to) = 0;
  // 書き込めるか（フォルダを作って試し書きする）
  virtual bool probeWritable(const std::string& dir);
};

std::string joinPath(const std::string& a, const std::string& b);

// std::filesystem による実装（Windows では MSVC の rename が MoveFileExW(REPLACE_EXISTING) を使う）
class StdFileStore final : public IFileStore {
 public:
  Result<std::vector<FileInfo>> list(const std::string& dir) override;
  std::optional<FileInfo> stat(const std::string& path) override;
  Result<std::string> read(const std::string& path) override;
  Status writeAtomic(const std::string& path, const std::string& data) override;
  Status remove(const std::string& path) override;
  Status makeDirs(const std::string& dir) override;
  Status move(const std::string& from, const std::string& to) override;
};

// メモリ上の実装（テスト用）。失敗を差し込める
class MemFileStore final : public IFileStore {
 public:
  Result<std::vector<FileInfo>> list(const std::string& dir) override;
  std::optional<FileInfo> stat(const std::string& path) override;
  Result<std::string> read(const std::string& path) override;
  Status writeAtomic(const std::string& path, const std::string& data) override;
  Status remove(const std::string& path) override;
  Status makeDirs(const std::string& dir) override;
  Status move(const std::string& from, const std::string& to) override;

  // テスト用の操作
  void put(const std::string& path, const std::string& data);  // 外部からの書き込み（時刻を進める）
  void erase(const std::string& path) { files_.erase(path); }  // 外部からの削除（失敗の差し込みを通さない）
  bool exists(const std::string& path) const { return files_.count(path) > 0; }
  const std::string& content(const std::string& path) const { return files_.at(path).data; }
  std::vector<std::string> paths() const;
  void setFailWrites(bool f) { failWrites_ = f; }
  void setFailMoves(bool f) { failMoves_ = f; }               // move だけ失敗させる（フォルダ作成・書き込みは通す）
  void setFailAtomicWrites(bool f) { failAtomic_ = f; }       // writeAtomic だけ失敗させる（フォルダ作成は通す）
  int writeCount() const { return writes_; }
  void tick(std::int64_t ms = 1000) { now_ += ms; }

 private:
  struct F {
    std::string data;
    std::int64_t mtime;
  };
  std::map<std::string, F> files_;
  std::map<std::string, bool> dirs_;
  std::int64_t now_ = 1'000'000;
  bool failWrites_ = false, failMoves_ = false, failAtomic_ = false;
  int writes_ = 0;
};

}  // namespace roadmap
