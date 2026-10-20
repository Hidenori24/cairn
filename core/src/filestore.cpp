#include "roadmap/filestore.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <system_error>

namespace roadmap {

namespace fs = std::filesystem;

std::string joinPath(const std::string& a, const std::string& b) {
  if (a.empty()) return b;
  if (a.back() == '/' || a.back() == '\\') return a + b;
  return a + "/" + b;
}

bool IFileStore::probeWritable(const std::string& dir) {
  if (!makeDirs(dir).ok()) return false;
  const std::string p = joinPath(dir, ".roadmap_write_test");
  if (!writeAtomic(p, "ok").ok()) return false;
  (void)remove(p);
  return true;
}

namespace {
fs::path P(const std::string& s) {
  return fs::path(std::u8string(reinterpret_cast<const char8_t*>(s.data()), s.size()));
}
std::string U(const fs::path& p) {
  const std::u8string u = p.u8string();
  return std::string(reinterpret_cast<const char*>(u.data()), u.size());
}
std::int64_t mtimeOf(const fs::path& p, std::error_code& ec) {
  const auto t = fs::last_write_time(p, ec);
  if (ec) return 0;
  return std::chrono::duration_cast<std::chrono::milliseconds>(t.time_since_epoch()).count();
}
Error errOf(const std::string& what, const std::error_code& ec) { return Error{what + ": " + ec.message()}; }
}  // namespace

Result<std::vector<FileInfo>> StdFileStore::list(const std::string& dir) {
  std::vector<FileInfo> out;
  std::error_code ec;
  fs::directory_iterator it(P(dir), ec);
  if (ec) return errOf("フォルダを読めません", ec);
  for (const auto& e : it) {
    FileInfo f;
    f.name = U(e.path().filename());
    std::error_code e2;
    f.isDir = e.is_directory(e2);
    if (!f.isDir) f.size = e.file_size(e2);
    f.mtime = mtimeOf(e.path(), e2);
    out.push_back(std::move(f));
  }
  return out;
}

std::optional<FileInfo> StdFileStore::stat(const std::string& path) {
  std::error_code ec;
  const fs::path p = P(path);
  if (!fs::exists(p, ec)) return std::nullopt;
  FileInfo f;
  f.name = U(p.filename());
  f.isDir = fs::is_directory(p, ec);
  if (!f.isDir) f.size = fs::file_size(p, ec);
  f.mtime = mtimeOf(p, ec);
  return f;
}

Result<std::string> StdFileStore::read(const std::string& path) {
  std::ifstream f(P(path), std::ios::binary);
  if (!f) return Error{"ファイルを開けません: " + path};
  std::ostringstream ss;
  ss << f.rdbuf();
  if (f.bad()) return Error{"ファイルを読めません: " + path};
  return ss.str();
}

Status StdFileStore::writeAtomic(const std::string& path, const std::string& data) {
  static std::mt19937 rng{std::random_device{}()};
  const fs::path target = P(path);
  fs::path tmp = target;
  tmp += ".tmp" + std::to_string(rng() % 1000000);
  {
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    if (!f) return Error{"書き込めません（一時ファイルを作れません）"};
    f.write(data.data(), static_cast<std::streamsize>(data.size()));
    f.flush();
    if (!f) {
      f.close();
      std::error_code ec;
      fs::remove(tmp, ec);
      return Error{"書き込めません（容量不足など）"};
    }
  }
  std::error_code ec;
  fs::rename(tmp, target, ec);
  if (ec) {
    std::error_code e2;
    fs::remove(tmp, e2);
    return errOf("置き換えられません（他のアプリが開いている可能性があります）", ec);
  }
  return okStatus();
}

Status StdFileStore::remove(const std::string& path) {
  std::error_code ec;
  fs::remove(P(path), ec);
  if (ec) return errOf("削除できません", ec);
  return okStatus();
}

Status StdFileStore::makeDirs(const std::string& dir) {
  std::error_code ec;
  fs::create_directories(P(dir), ec);
  if (ec) return errOf("フォルダを作れません", ec);
  return okStatus();
}

Status StdFileStore::move(const std::string& from, const std::string& to) {
  std::error_code ec;
  const fs::path a = P(from), b = P(to);
  if (!fs::exists(a, ec)) return Error{"移す元がありません: " + from};
  if (fs::exists(b, ec)) return Error{"移す先に同じ名前があります: " + to};
  fs::rename(a, b, ec);
  if (ec) return errOf("移せません（他のアプリが開いている可能性があります）", ec);
  return okStatus();
}

// ---------------------------------------------------------------- メモリ上の実装
namespace {
std::string parentOf(const std::string& p) {
  const auto k = p.rfind('/');
  return k == std::string::npos ? "" : p.substr(0, k);
}
std::string baseOf(const std::string& p) {
  const auto k = p.rfind('/');
  return k == std::string::npos ? p : p.substr(k + 1);
}
}  // namespace

Result<std::vector<FileInfo>> MemFileStore::list(const std::string& dir) {
  std::vector<FileInfo> out;
  std::map<std::string, bool> subdirs;
  const std::string prefix = dir.empty() ? "" : dir + "/";
  for (const auto& [p, f] : files_) {
    if (p.compare(0, prefix.size(), prefix) != 0) continue;
    const std::string rest = p.substr(prefix.size());
    const auto k = rest.find('/');
    if (k == std::string::npos) out.push_back({rest, f.mtime, f.data.size(), false});
    else subdirs[rest.substr(0, k)] = true;
  }
  for (const auto& [d, v] : dirs_)
    if (parentOf(d) == dir) subdirs[baseOf(d)] = true;
  for (const auto& [d, v] : subdirs) out.push_back({d, 0, 0, true});
  return out;
}

std::optional<FileInfo> MemFileStore::stat(const std::string& path) {
  auto it = files_.find(path);
  if (it != files_.end()) return FileInfo{baseOf(path), it->second.mtime, it->second.data.size(), false};
  // フォルダ（makeDirs したもの、または中にファイルやフォルダがあるもの）
  bool dir = dirs_.count(path) > 0;
  const std::string prefix = path + "/";
  for (auto f = files_.lower_bound(prefix); !dir && f != files_.end() && f->first.compare(0, prefix.size(), prefix) == 0; ++f) dir = true;
  for (auto d = dirs_.lower_bound(prefix); !dir && d != dirs_.end() && d->first.compare(0, prefix.size(), prefix) == 0; ++d) dir = true;
  if (!dir) return std::nullopt;
  FileInfo info;
  info.name = baseOf(path);
  info.isDir = true;
  return info;
}

Result<std::string> MemFileStore::read(const std::string& path) {
  auto it = files_.find(path);
  if (it == files_.end()) return Error{"ファイルを開けません: " + path};
  return it->second.data;
}

Status MemFileStore::writeAtomic(const std::string& path, const std::string& data) {
  if (failWrites_) return Error{"書き込めません（テスト）"};
  ++writes_;
  now_ += 10;
  files_[path] = {data, now_};
  return okStatus();
}

Status MemFileStore::remove(const std::string& path) {
  if (failWrites_) return Error{"削除できません（テスト）"};
  files_.erase(path);
  dirs_.erase(path);
  return okStatus();
}

Status MemFileStore::makeDirs(const std::string& dir) {
  if (failWrites_) return Error{"フォルダを作れません（テスト）"};
  dirs_[dir] = true;
  return okStatus();
}

Status MemFileStore::move(const std::string& from, const std::string& to) {
  if (failWrites_) return Error{"移せません（テスト）"};
  if (!stat(from)) return Error{"移す元がありません: " + from};
  if (stat(to)) return Error{"移す先に同じ名前があります: " + to};
  if (files_.count(from)) {
    auto node = files_.extract(from);
    node.key() = to;
    files_.insert(std::move(node));
    return okStatus();
  }
  const std::string prefix = from + "/";
  std::vector<std::string> fk, dk;
  for (const auto& [p, f] : files_)
    if (p.compare(0, prefix.size(), prefix) == 0) fk.push_back(p);
  for (const auto& [p, v] : dirs_)
    if (p == from || p.compare(0, prefix.size(), prefix) == 0) dk.push_back(p);
  for (const auto& p : fk) {
    auto node = files_.extract(p);
    node.key() = to + p.substr(from.size());
    files_.insert(std::move(node));
  }
  for (const auto& p : dk) {
    dirs_.erase(p);
    dirs_[to + p.substr(from.size())] = true;
  }
  dirs_[to] = true;
  return okStatus();
}

void MemFileStore::put(const std::string& path, const std::string& data) {
  now_ += 1000;
  files_[path] = {data, now_};
}

std::vector<std::string> MemFileStore::paths() const {
  std::vector<std::string> v;
  for (const auto& [p, f] : files_) v.push_back(p);
  return v;
}

}  // namespace roadmap
