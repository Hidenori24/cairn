#include "platform/win32_gh.hpp"

#include <windows.h>

#include <algorithm>
#include <string>
#include <vector>

#include "platform/win32_platform.hpp"

namespace roadmap::win {

namespace {

// 閉じ忘れを防ぐ
struct Handle {
  HANDLE h = nullptr;
  Handle() = default;
  explicit Handle(HANDLE x) : h(x) {}
  Handle(const Handle&) = delete;
  Handle& operator=(const Handle&) = delete;
  ~Handle() { reset(); }
  void reset(HANDLE x = nullptr) {
    if (h && h != INVALID_HANDLE_VALUE) CloseHandle(h);
    h = x;
  }
  bool valid() const { return h && h != INVALID_HANDLE_VALUE; }
};

bool isAbsoluteDir(const std::wstring& d) {
  if (d.size() >= 3 && ((d[0] >= L'A' && d[0] <= L'Z') || (d[0] >= L'a' && d[0] <= L'z')) && d[1] == L':' && (d[2] == L'\\' || d[2] == L'/'))
    return true;
  return d.size() >= 3 && d[0] == L'\\' && d[1] == L'\\';
}

// PATH の絶対パスのフォルダだけから gh.exe を探す（SearchPathW。カレントフォルダ・相対パスのフォルダは見ない）
std::wstring findGh() {
  const DWORD n = GetEnvironmentVariableW(L"PATH", nullptr, 0);
  if (n == 0) return {};
  std::wstring path(n, L'\0');
  const DWORD got = GetEnvironmentVariableW(L"PATH", path.data(), n);
  if (got == 0 || got >= n) return {};
  path.resize(got);
  std::wstring buf(32768, L'\0');
  std::size_t pos = 0;
  while (pos <= path.size()) {
    std::size_t e = path.find(L';', pos);
    if (e == std::wstring::npos) e = path.size();
    std::wstring dir = path.substr(pos, e - pos);
    pos = e + 1;
    if (dir.size() >= 2 && dir.front() == L'"' && dir.back() == L'"') dir = dir.substr(1, dir.size() - 2);
    if (dir.empty() || !isAbsoluteDir(dir)) continue;
    const DWORD r = SearchPathW(dir.c_str(), L"gh", L".exe", static_cast<DWORD>(buf.size()), buf.data(), nullptr);
    if (r > 0 && r < buf.size()) return std::wstring(buf.data(), r);
  }
  return {};
}

class GhRemoteStatus final : public IRemoteStatus {
 public:
  // 中断は token で決める（polling。待ち時間は 30ms ごとに見る）。窓口の側に眠っているものはないので、合図は要らない
  Result<RemoteInfo> fetch(const GitHubRef& ref, const CancelToken& token) override {
    const auto args = ghArguments(ref);
    if (!args) return Error{args.error()};
    if (token.cancelled()) return Error{"確認を中断しました"};  // PATH の探索の前（探索の間に中断されていても取りこぼさない）
    const std::wstring exe = findGh();
    if (token.cancelled()) return Error{"確認を中断しました"};
    if (exe.empty())
      return Error{"GitHub CLI（gh）が見つかりません。インストールして、gh auth login でログインしてください"};
    std::wstring cmd = widen(windowsCommandLine(narrow(exe), *args));  // シェルを通さない 1 本のコマンドライン

    // 標準出力のパイプ（読む側は子に継がせない）と、標準入力・標準エラー用の NUL
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    Handle rd, wr, nul;
    HANDLE r = nullptr, w = nullptr;
    if (!CreatePipe(&r, &w, &sa, 0)) return Error{"gh を起動できませんでした（パイプを作れません）"};
    rd.reset(r);
    wr.reset(w);
    SetHandleInformation(rd.h, HANDLE_FLAG_INHERIT, 0);
    nul.reset(CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr));
    if (!nul.valid()) return Error{"gh を起動できませんでした（NUL を開けません）"};

    // 継がせるハンドルを 2 つに限る
    SIZE_T attrSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attrSize);
    std::vector<char> attrBuf(attrSize);
    auto* attrs = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrBuf.data());
    if (attrSize == 0 || !InitializeProcThreadAttributeList(attrs, 1, 0, &attrSize)) return Error{"gh を起動できませんでした"};
    HANDLE inherit[2] = {wr.h, nul.h};
    const BOOL attrOk = UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherit, sizeof(inherit), nullptr, nullptr);
    if (!attrOk) {
      DeleteProcThreadAttributeList(attrs);
      return Error{"gh を起動できませんでした"};
    }

    STARTUPINFOEXW si{};
    si.StartupInfo.cb = sizeof(si);
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    si.StartupInfo.hStdInput = nul.h;
    si.StartupInfo.hStdOutput = wr.h;
    si.StartupInfo.hStdError = nul.h;
    si.lpAttributeList = attrs;
    wchar_t sysDir[MAX_PATH] = {};
    const UINT sysLen = GetSystemDirectoryW(sysDir, MAX_PATH);
    PROCESS_INFORMATION pi{};
    const BOOL created = CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, TRUE,
                                        EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr,
                                        sysLen > 0 && sysLen < MAX_PATH ? sysDir : nullptr, &si.StartupInfo, &pi);
    const DWORD createErr = GetLastError();
    DeleteProcThreadAttributeList(attrs);
    if (!created) return Error{"gh を起動できませんでした（コード " + std::to_string(createErr) + "）"};
    Handle proc(pi.hProcess), thread(pi.hThread);
    wr.reset();  // 親の書き込み側を閉じる（子が終われば読み終わる）
    nul.reset();

    // 子ごと終わらせられるように、ジョブに入れてから動かす（ジョブに入れられなくても続ける）
    Handle job(CreateJobObjectW(nullptr, nullptr));
    bool inJob = false;
    if (job.valid()) {
      JOBOBJECT_EXTENDED_LIMIT_INFORMATION li{};
      li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
      if (SetInformationJobObject(job.h, JobObjectExtendedLimitInformation, &li, sizeof(li)))
        inJob = AssignProcessToJobObject(job.h, proc.h) != 0;
    }
    ResumeThread(thread.h);

    std::string out;
    const ULONGLONG deadline = GetTickCount64() + static_cast<ULONGLONG>(kGhTimeoutSeconds) * 1000ULL;
    bool overflow = false, timedOut = false, cancelled = false;
    const auto readAvailable = [&]() -> bool {  // 読めるだけ読む。上限を超えたら false
      for (;;) {
        DWORD avail = 0;
        if (!PeekNamedPipe(rd.h, nullptr, 0, nullptr, &avail, nullptr) || avail == 0) return true;
        char buf[4096];
        DWORD got = 0;
        const DWORD want = std::min<DWORD>(avail, static_cast<DWORD>(sizeof(buf)));
        if (!ReadFile(rd.h, buf, want, &got, nullptr) || got == 0) return true;
        if (out.size() + got > kMaxGhOutputBytes) return false;
        out.append(buf, got);
      }
    };
    for (;;) {
      if (!readAvailable()) {
        overflow = true;
        break;
      }
      if (WaitForSingleObject(proc.h, 30) == WAIT_OBJECT_0) {
        if (!readAvailable()) overflow = true;
        break;
      }
      if (token.cancelled()) {
        cancelled = true;
        break;
      }
      if (GetTickCount64() >= deadline) {
        timedOut = true;
        break;
      }
    }
    DWORD exitCode = 0;
    if (overflow || timedOut || cancelled) {
      if (inJob) TerminateJobObject(job.h, 1);
      else TerminateProcess(proc.h, 1);
      WaitForSingleObject(proc.h, 2000);
    } else {
      GetExitCodeProcess(proc.h, &exitCode);
    }

    if (cancelled) return Error{"確認を中断しました"};
    if (timedOut) return Error{std::to_string(kGhTimeoutSeconds) + " 秒以内に応答がありませんでした（ネットワークと gh の状態を確認してください）"};
    if (overflow) return Error{"gh の出力が大きすぎます"};
    if (exitCode != 0)
      return Error{"gh が失敗しました（未ログイン・アクセス権がない・見つからないのいずれかです。gh auth status で確認してください）"};
    return ref.kind == GitHubKind::Pull ? parseGhPrJson(out) : parseGhIssueJson(out);
  }
};

}  // namespace

std::shared_ptr<IRemoteStatus> makeGhRemoteStatus() { return std::make_shared<GhRemoteStatus>(); }

int utcOffsetMinutesAt(std::int64_t epochSeconds) {
  // 1601-01-01 からの 100ns 単位（FILETIME）にして、OS の時間帯の規則でその時刻の現地時刻を求める（夏時間も、その日時の規則）。
  // 範囲外・変換できないときは 0（UTC）
  constexpr std::int64_t kEpochDiffSec = 11644473600LL;  // 1601-01-01 → 1970-01-01
  if (epochSeconds < -kEpochDiffSec + 86400 || epochSeconds > 253402300799LL) return 0;
  ULARGE_INTEGER u{};
  u.QuadPart = static_cast<ULONGLONG>(epochSeconds + kEpochDiffSec) * 10000000ULL;
  FILETIME ft{};
  ft.dwLowDateTime = u.LowPart;
  ft.dwHighDateTime = u.HighPart;
  SYSTEMTIME utc{}, local{};
  if (!FileTimeToSystemTime(&ft, &utc)) return 0;
  if (!SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local)) return 0;  // nullptr = いまの時間帯の設定
  FILETIME lft{};
  if (!SystemTimeToFileTime(&local, &lft)) return 0;
  ULARGE_INTEGER lu{};
  lu.LowPart = lft.dwLowDateTime;
  lu.HighPart = lft.dwHighDateTime;
  const std::int64_t diff100ns = static_cast<std::int64_t>(lu.QuadPart) - static_cast<std::int64_t>(u.QuadPart);
  return static_cast<int>(diff100ns / 600000000LL);  // 分
}

}  // namespace roadmap::win
