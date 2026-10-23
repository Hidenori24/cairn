#include "platform/win32_lock.hpp"

#include <windows.h>

#include <algorithm>
#include <string>
#include <vector>

#include "platform/win32_platform.hpp"

namespace roadmap::win {

namespace {
constexpr DWORD kShareAll = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
constexpr std::size_t kMaxReadBytes = 64 * 1024;

// 理由（日本語）。パスや中身は入れない
std::string reasonOf(const char* what, DWORD err) {
  std::string why;
  switch (err) {
    case ERROR_ACCESS_DENIED: why = "アクセスが拒否されました（権限がない、または読み取り専用です）"; break;
    case ERROR_WRITE_PROTECT: why = "書き込みが禁止されています"; break;
    case ERROR_PATH_NOT_FOUND:
    case ERROR_FILE_NOT_FOUND: why = "フォルダが見つかりません"; break;
    case ERROR_SHARING_VIOLATION:
    case ERROR_LOCK_VIOLATION: why = "ほかのアプリが開いています"; break;
    case ERROR_BAD_NETPATH:
    case ERROR_NETNAME_DELETED:
    case ERROR_UNEXP_NET_ERR:
    case ERROR_NETWORK_UNREACHABLE: why = "共有フォルダに接続できません"; break;
    case ERROR_DISK_FULL:
    case ERROR_HANDLE_DISK_FULL: why = "空き容量がありません"; break;
    default: why = "エラー " + std::to_string(static_cast<unsigned long>(err)); break;
  }
  return std::string(what) + "（" + why + "）";
}

bool notFound(DWORD err) { return err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND; }

// data を先頭から書く。書けたら true
bool writeAll(HANDLE h, const std::string& data, DWORD* err) {
  std::size_t off = 0;
  while (off < data.size()) {
    const std::size_t left = data.size() - off;
    const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(left, 1u << 20));
    DWORD written = 0;
    if (!WriteFile(h, data.data() + off, chunk, &written, nullptr) || written == 0) {
      *err = GetLastError();
      return false;
    }
    off += written;
  }
  return true;
}
}  // namespace

LockCreateResult Win32LockStore::tryCreate(const std::string& path, const std::string& data) {
  const std::wstring wp = widen(path);
  HANDLE h = CreateFileW(wp.c_str(), GENERIC_WRITE, kShareAll, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) {
    const DWORD e = GetLastError();
    if (e == ERROR_FILE_EXISTS || e == ERROR_ALREADY_EXISTS) return {LockCreate::Exists, {}};
    // 他のマシンが開いたまま消した（削除待ち）ファイルは、アクセス拒否・共有違反になる。見えているなら「ある」とみなす
    if ((e == ERROR_ACCESS_DENIED || e == ERROR_SHARING_VIOLATION) && GetFileAttributesW(wp.c_str()) != INVALID_FILE_ATTRIBUTES)
      return {LockCreate::Exists, {}};
    return {LockCreate::Failed, reasonOf("ロックのファイルを作れません", e)};
  }
  DWORD err = 0;
  const bool ok = writeAll(h, data, &err) && FlushFileBuffers(h);
  if (!ok && err == 0) err = GetLastError();
  CloseHandle(h);
  if (!ok) {
    DeleteFileW(wp.c_str());  // 中身のないロックを残さない
    return {LockCreate::Failed, reasonOf("ロックのファイルに書けません", err)};
  }
  return {LockCreate::Created, {}};
}

Result<std::optional<std::string>> Win32LockStore::read(const std::string& path) {
  const std::wstring wp = widen(path);
  HANDLE h = CreateFileW(wp.c_str(), GENERIC_READ, kShareAll, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) {
    const DWORD e = GetLastError();
    if (notFound(e)) return std::optional<std::string>{};
    return Error{reasonOf("ロックのファイルを読めません", e)};
  }
  std::string out;
  std::vector<char> buf(4096);
  for (;;) {
    DWORD n = 0;
    if (!ReadFile(h, buf.data(), static_cast<DWORD>(buf.size()), &n, nullptr)) {
      const DWORD e = GetLastError();
      CloseHandle(h);
      return Error{reasonOf("ロックのファイルを読めません", e)};
    }
    if (n == 0) break;
    out.append(buf.data(), n);
    if (out.size() >= kMaxReadBytes) break;
  }
  CloseHandle(h);
  return std::optional<std::string>{std::move(out)};
}

Status Win32LockStore::update(const std::string& path, const std::string& data) {
  const std::wstring wp = widen(path);
  // OPEN_EXISTING・切り詰めなし: 先頭から同じ大きさを上書きする
  HANDLE h = CreateFileW(wp.c_str(), GENERIC_WRITE, kShareAll, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return Error{reasonOf("ロックのファイルに書けません", GetLastError())};
  DWORD err = 0;
  bool ok = writeAll(h, data, &err);
  if (ok) {
    LARGE_INTEGER size{};
    if (GetFileSizeEx(h, &size) && size.QuadPart > static_cast<LONGLONG>(data.size())) {
      LARGE_INTEGER to{};
      to.QuadPart = static_cast<LONGLONG>(data.size());
      ok = SetFilePointerEx(h, to, nullptr, FILE_BEGIN) && SetEndOfFile(h);  // 前より短くなったときだけ、余りを切る
      if (!ok) err = GetLastError();
    }
  }
  if (ok) ok = FlushFileBuffers(h) != 0;
  if (!ok && err == 0) err = GetLastError();
  CloseHandle(h);
  if (!ok) return Error{reasonOf("ロックのファイルに書けません", err)};
  return okStatus();
}

Status Win32LockStore::remove(const std::string& path) {
  if (!DeleteFileW(widen(path).c_str())) {
    const DWORD e = GetLastError();
    if (!notFound(e)) return Error{reasonOf("ロックのファイルを消せません", e)};
  }
  return okStatus();
}

std::string machineName() {
  wchar_t buf[256];
  DWORD n = static_cast<DWORD>(sizeof(buf) / sizeof(buf[0]));
  if (GetComputerNameExW(ComputerNameDnsHostname, buf, &n) && n > 0) return narrow(std::wstring(buf, n));
  n = static_cast<DWORD>(sizeof(buf) / sizeof(buf[0]));
  if (GetComputerNameW(buf, &n) && n > 0) return narrow(std::wstring(buf, n));
  return "";
}

}  // namespace roadmap::win
