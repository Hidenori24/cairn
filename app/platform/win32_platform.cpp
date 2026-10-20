#include "platform/win32_platform.hpp"

#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "roadmap/encoding.hpp"
#include "roadmap/filestore.hpp"
#include "roadmap/store.hpp"

namespace roadmap::win {

std::wstring widen(const std::string& s) {
  if (s.empty()) return {};
  const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
  std::wstring w(static_cast<std::size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
  return w;
}

std::string narrow(const std::wstring& w) {
  if (w.empty()) return {};
  const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
  std::string s(static_cast<std::size_t>(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
  return s;
}

std::string exeDir() {
  std::wstring buf(MAX_PATH, L'\0');
  for (;;) {
    const DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
    if (n < buf.size()) {
      buf.resize(n);
      break;
    }
    buf.resize(buf.size() * 2);
  }
  const auto k = buf.find_last_of(L"\\/");
  return narrow(k == std::wstring::npos ? buf : buf.substr(0, k));
}

std::string localAppDataDir() {
  PWSTR p = nullptr;
  std::string out;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &p))) out = narrow(p) + "\\ProjectRoadmap";
  if (p) CoTaskMemFree(p);
  return out;
}

std::string envVar(const char* name) {
  const std::wstring wn = widen(name);
  const DWORD n = GetEnvironmentVariableW(wn.c_str(), nullptr, 0);
  if (!n) return "";
  std::wstring v(n, L'\0');
  GetEnvironmentVariableW(wn.c_str(), v.data(), n);
  v.resize(n - 1);
  return narrow(v);
}

bool systemDarkMode() {
  DWORD v = 1, sz = sizeof v;
  // 読むだけ（レジストリには書かない）
  if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                   L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &v, &sz) == ERROR_SUCCESS)
    return v == 0;
  return false;
}

void setDarkTitleBar(HWND hwnd, bool dark) {
  const BOOL d = dark ? TRUE : FALSE;
  DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &d, sizeof d);
}

std::optional<std::string> readTextFile(const std::wstring& path, std::string* error) {
  WIN32_FILE_ATTRIBUTE_DATA fa{};
  if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fa)) {
    if (error) *error = "ファイルを開けません";
    return std::nullopt;
  }
  const unsigned long long size = (static_cast<unsigned long long>(fa.nFileSizeHigh) << 32) | fa.nFileSizeLow;
  if (size > kMaxCsvBytes) {
    if (error) *error = "ファイルが大きすぎます（50MB超）";
    return std::nullopt;
  }
  std::ifstream f(std::filesystem::path(path), std::ios::binary);
  if (!f) {
    if (error) *error = "ファイルを開けません（他のアプリが使っている可能性があります）";
    return std::nullopt;
  }
  std::ostringstream ss;
  ss << f.rdbuf();
  return encoding::decodeText(ss.str()).text;
}

std::optional<std::pair<std::string, std::string>> Win32Platform::openTextFile() {
  IFileOpenDialog* dlg = nullptr;
  if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return std::nullopt;
  const COMDLG_FILTERSPEC types[] = {{L"CSV / JSON", L"*.csv;*.tsv;*.txt;*.json"}, {L"すべてのファイル", L"*.*"}};
  dlg->SetFileTypes(2, types);
  std::optional<std::pair<std::string, std::string>> out;
  if (SUCCEEDED(dlg->Show(hwnd_))) {
    IShellItem* item = nullptr;
    if (SUCCEEDED(dlg->GetResult(&item))) {
      PWSTR p = nullptr;
      if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &p))) {
        std::string err;
        if (auto t = readTextFile(p, &err)) {
          std::wstring wp(p);
          const auto k = wp.find_last_of(L"\\/");
          out = std::make_pair(*t, narrow(k == std::wstring::npos ? wp : wp.substr(k + 1)));
        } else {
          MessageBoxW(hwnd_, widen(err).c_str(), L"CSVを取り込む", MB_OK | MB_ICONWARNING);
        }
        CoTaskMemFree(p);
      }
      item->Release();
    }
  }
  dlg->Release();
  return out;
}

// クリップボードのテキスト（CF_UNICODETEXT）を UTF-8 で返す。大きすぎるもの・読めないものは nullopt
std::optional<std::string> Win32Platform::clipboardText() {
  constexpr SIZE_T kMaxBytes = 8u * 1024 * 1024;
  if (!IsClipboardFormatAvailable(CF_UNICODETEXT)) return std::nullopt;
  if (!OpenClipboard(hwnd_)) return std::nullopt;
  std::optional<std::string> out;
  if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
    const SIZE_T bytes = GlobalSize(h);
    if (bytes > 0 && bytes <= kMaxBytes) {
      if (const auto* p = static_cast<const wchar_t*>(GlobalLock(h))) {
        std::size_t n = 0;
        const std::size_t maxN = bytes / sizeof(wchar_t);
        while (n < maxN && p[n] != L'\0') ++n;
        out = narrow(std::wstring(p, n));
        GlobalUnlock(h);
      }
    }
  }
  CloseClipboard();
  return out;
}

std::optional<std::string> Win32Platform::pickFolder(const std::string& start) {
  IFileOpenDialog* dlg = nullptr;
  if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return std::nullopt;
  DWORD opts = 0;
  dlg->GetOptions(&opts);
  dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
  dlg->SetTitle(L"保存先のフォルダを選ぶ");
  if (!start.empty()) {
    IShellItem* folder = nullptr;
    if (SUCCEEDED(SHCreateItemFromParsingName(widen(start).c_str(), nullptr, IID_PPV_ARGS(&folder)))) {
      dlg->SetFolder(folder);
      folder->Release();
    }
  }
  std::optional<std::string> out;
  if (SUCCEEDED(dlg->Show(hwnd_))) {
    IShellItem* item = nullptr;
    if (SUCCEEDED(dlg->GetResult(&item))) {
      PWSTR p = nullptr;
      if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &p))) {
        out = narrow(p);
        CoTaskMemFree(p);
      }
      item->Release();
    }
  }
  dlg->Release();
  return out;
}

bool Win32Platform::saveTextFile(const std::string& suggestedName, const std::string& content) {
  IFileSaveDialog* dlg = nullptr;
  if (FAILED(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return false;
  const bool json = suggestedName.size() > 5 && suggestedName.compare(suggestedName.size() - 5, 5, ".json") == 0;
  const COMDLG_FILTERSPEC types[] = {{json ? L"JSON" : L"CSV", json ? L"*.json" : L"*.csv"}, {L"すべてのファイル", L"*.*"}};
  dlg->SetFileTypes(2, types);
  dlg->SetDefaultExtension(json ? L"json" : L"csv");
  dlg->SetFileName(widen(suggestedName).c_str());
  bool ok = false;
  if (SUCCEEDED(dlg->Show(hwnd_))) {
    IShellItem* item = nullptr;
    if (SUCCEEDED(dlg->GetResult(&item))) {
      PWSTR p = nullptr;
      if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &p))) {
        StdFileStore fs;
        auto s = fs.writeAtomic(narrow(p), content);  // UTF-8（BOMなし）
        ok = s.ok();
        if (!ok) MessageBoxW(hwnd_, widen("保存できません: " + s.error()).c_str(), L"書き出し", MB_OK | MB_ICONWARNING);
        CoTaskMemFree(p);
      }
      item->Release();
    }
  }
  dlg->Release();
  return ok;
}

void Win32Platform::openFolder(const std::string& path) {
  ShellExecuteW(hwnd_, L"open", widen(path).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

}  // namespace roadmap::win
