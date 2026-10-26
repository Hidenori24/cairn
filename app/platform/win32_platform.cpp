#include "platform/win32_platform.hpp"

#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shlwapi.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "roadmap/attachments.hpp"
#include "roadmap/encoding.hpp"
#include "roadmap/filestore.hpp"
#include "roadmap/links.hpp"
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
        if (auto t = win::readTextFile(p, &err)) {
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

// クリップボードに CF_UNICODETEXT で書く。大きすぎる（8MB 超）・確保できない・開けないときは false
bool Win32Platform::setClipboardText(const std::string& text) {
  constexpr std::size_t kMaxChars = 4u * 1024 * 1024;
  std::wstring w = widen(text);
  if (w.size() > kMaxChars) return false;
  // 改行は CRLF にそろえる（メモ帳など古いアプリでも改行される）
  std::wstring crlf;
  crlf.reserve(w.size() + w.size() / 16);
  for (std::size_t i = 0; i < w.size(); ++i) {
    if (w[i] == L'\n' && (i == 0 || w[i - 1] != L'\r')) crlf += L'\r';
    crlf += w[i];
  }
  if (crlf.size() > kMaxChars) return false;
  w = std::move(crlf);
  const SIZE_T bytes = (w.size() + 1) * sizeof(wchar_t);
  HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
  if (!mem) return false;
  void* dst = GlobalLock(mem);
  if (!dst) {
    GlobalFree(mem);
    return false;
  }
  std::memcpy(dst, w.c_str(), bytes);
  GlobalUnlock(mem);
  if (!OpenClipboard(hwnd_)) {
    GlobalFree(mem);
    return false;
  }
  bool ok = false;
  if (EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, mem)) ok = true;  // 成功したら所有権はクリップボードへ移る
  if (!ok) GlobalFree(mem);
  CloseClipboard();
  return ok;
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

// 添付するファイルを選ぶ（複数選択）。フォルダは選べない（フォルダは従来どおりドロップで付ける）。やめたら空
std::vector<std::string> Win32Platform::pickOpenFiles(const std::string& title) {
  std::vector<std::string> out;
  IFileOpenDialog* dlg = nullptr;
  if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return out;
  DWORD opts = 0;
  dlg->GetOptions(&opts);
  dlg->SetOptions(opts | FOS_ALLOWMULTISELECT | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST);
  dlg->SetTitle(widen(title).c_str());
  const COMDLG_FILTERSPEC types[] = {{L"すべてのファイル", L"*.*"}};
  dlg->SetFileTypes(1, types);
  if (SUCCEEDED(dlg->Show(hwnd_))) {
    IShellItemArray* items = nullptr;
    if (SUCCEEDED(dlg->GetResults(&items)) && items) {
      DWORD n = 0;
      if (SUCCEEDED(items->GetCount(&n))) {
        constexpr DWORD kMaxPicked = static_cast<DWORD>(kMaxDropFiles);  // 1回の上限（ドロップと同じ）
        for (DWORD i = 0; i < n && i < kMaxPicked; ++i) {
          IShellItem* item = nullptr;
          if (FAILED(items->GetItemAt(i, &item)) || !item) continue;
          PWSTR p = nullptr;
          if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &p)) && p) out.push_back(narrow(p));
          if (p) CoTaskMemFree(p);
          item->Release();
        }
      }
      items->Release();
    }
  }
  dlg->Release();
  return out;
}

// クリップボードの画像（CF_DIBV5 を優先、なければ CF_DIB）を BMP ファイルのバイト列にして返す。
// 1ファイルの上限を超えるもの・ヘッダが壊れているもの・読めないものは nullopt（bmpFromDib が検査する）
std::optional<std::string> Win32Platform::clipboardImage() {
  UINT fmt = 0;
  if (IsClipboardFormatAvailable(CF_DIBV5)) fmt = CF_DIBV5;
  else if (IsClipboardFormatAvailable(CF_DIB)) fmt = CF_DIB;
  else return std::nullopt;
  if (!OpenClipboard(hwnd_)) return std::nullopt;
  std::optional<std::string> out;
  if (HANDLE h = GetClipboardData(fmt)) {
    const SIZE_T bytes = GlobalSize(h);
    if (bytes >= 40 && bytes <= static_cast<SIZE_T>(kMaxAttachFileBytes)) {
      if (const auto* p = static_cast<const char*>(GlobalLock(h))) {
        if (auto bmp = bmpFromDib(std::string(p, bytes))) out = std::move(*bmp);
        GlobalUnlock(h);
      }
    }
  }
  CloseClipboard();
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

// 名前を付けて保存。上書きの確認はダイアログに任せ（FOS_OVERWRITEPROMPT）、選んだパスだけを返す（書くのは呼び出し側）
std::optional<std::string> Win32Platform::pickSaveFile(const std::string& suggestedName, const std::string& ext,
                                                       const std::string& typeLabel) {
  IFileSaveDialog* dlg = nullptr;
  if (FAILED(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return std::nullopt;
  DWORD opts = 0;
  dlg->GetOptions(&opts);
  dlg->SetOptions(opts | FOS_FORCEFILESYSTEM | FOS_OVERWRITEPROMPT | FOS_PATHMUSTEXIST | FOS_NOREADONLYRETURN);
  const std::wstring wext = widen(ext);
  const std::wstring label = widen(typeLabel) + L" (*." + wext + L")";
  const std::wstring pattern = L"*." + wext;
  const COMDLG_FILTERSPEC types[] = {{label.c_str(), pattern.c_str()}, {L"すべてのファイル", L"*.*"}};
  dlg->SetFileTypes(2, types);
  dlg->SetFileTypeIndex(1);
  dlg->SetDefaultExtension(wext.c_str());
  dlg->SetFileName(widen(suggestedName).c_str());
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

// RGBA 8bit の画像を DX11 のテクスチャ（シェーダリソースビュー）にする。ImGui の DX11 バックエンドは
// ImTextureID を ID3D11ShaderResourceView* として描く
ImTextureID Win32Platform::createTexture(const unsigned char* rgba, int w, int h) {
  if (!device_ || !rgba || w <= 0 || h <= 0) return nullptr;
  D3D11_TEXTURE2D_DESC desc{};
  desc.Width = static_cast<UINT>(w);
  desc.Height = static_cast<UINT>(h);
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  D3D11_SUBRESOURCE_DATA sub{};
  sub.pSysMem = rgba;
  sub.SysMemPitch = static_cast<UINT>(w) * 4;
  ID3D11Texture2D* tex = nullptr;
  if (FAILED(device_->CreateTexture2D(&desc, &sub, &tex)) || !tex) return nullptr;
  D3D11_SHADER_RESOURCE_VIEW_DESC sv{};
  sv.Format = desc.Format;
  sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
  sv.Texture2D.MipLevels = 1;
  ID3D11ShaderResourceView* srv = nullptr;
  const HRESULT hr = device_->CreateShaderResourceView(tex, &sv, &srv);
  tex->Release();  // ビューが持っているので、ここでは手放す
  if (FAILED(hr)) return nullptr;
  return reinterpret_cast<ImTextureID>(srv);
}

void Win32Platform::releaseTexture(ImTextureID tex) {
  if (tex) static_cast<ID3D11ShaderResourceView*>(tex)->Release();
}

// 関連付けられたアプリで開く。実行ファイルかどうかの確認は UI が先に済ませている
void Win32Platform::openFile(const std::string& path) {
  ShellExecuteW(hwnd_, L"open", widen(path).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

namespace {
bool fileExists(const std::filesystem::path& path) {
  std::error_code ec;
  return std::filesystem::is_regular_file(path, ec) && !ec;
}

std::wstring edgePath() {
  for (HKEY root : {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE}) {
    for (const DWORD view : {static_cast<DWORD>(RRF_SUBKEY_WOW6464KEY), static_cast<DWORD>(RRF_SUBKEY_WOW6432KEY)}) {
      wchar_t value[32768]{};
      DWORD bytes = sizeof(value);
      if (RegGetValueW(root, L"Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\msedge.exe", nullptr,
                       RRF_RT_REG_SZ | view, nullptr, value, &bytes) == ERROR_SUCCESS &&
          fileExists(value)) return value;
    }
  }
  for (const wchar_t* base : {L"ProgramFiles", L"ProgramFiles(x86)", L"LOCALAPPDATA"}) {
    const DWORD n = GetEnvironmentVariableW(base, nullptr, 0);
    if (!n) continue;
    std::wstring dir(n, L'\0');
    GetEnvironmentVariableW(base, dir.data(), n);
    dir.resize(n - 1);
    const std::filesystem::path p = std::filesystem::path(dir) / L"Microsoft" / L"Edge" / L"Application" / L"msedge.exe";
    if (fileExists(p)) return p.wstring();
  }
  return {};
}

std::wstring quoteArg(const std::wstring& arg) {
  std::wstring out = L"\"";
  std::size_t slashes = 0;
  for (wchar_t ch : arg) {
    if (ch == L'\\') { ++slashes; continue; }
    if (ch == L'"') {
      out.append(slashes * 2 + 1, L'\\');
      out += ch;
    } else {
      out.append(slashes, L'\\');
      out += ch;
    }
    slashes = 0;
  }
  out.append(slashes * 2, L'\\');
  out += L'"';
  return out;
}

std::wstring fileUri(const std::wstring& path) {
  // Let Windows produce the file URL so drive paths, extended-length paths, and Unicode
  // characters are escaped using the form expected by Edge/Chromium.
  std::wstring url(32768, L'\0');
  DWORD chars = static_cast<DWORD>(url.size());
  if (FAILED(UrlCreateFromPathW(path.c_str(), url.data(), &chars, 0))) return {};
  url.resize(chars);
  if (!url.empty() && url.back() == L'\0') url.pop_back();
  return url;
}
}  // namespace

bool Win32Platform::printHtmlToPdf(const std::string& html, const std::string& path, std::string* error) {
  const std::wstring edge = edgePath();
  if (edge.empty()) {
    if (error) *error = "Microsoft Edge が見つかりません。HTML を保存してブラウザーから PDF にしてください";
    return false;
  }
  wchar_t tempDir[MAX_PATH + 1]{};
  const DWORD dirLen = GetTempPathW(MAX_PATH, tempDir);
  if (!dirLen || dirLen >= MAX_PATH) {
    if (error) *error = "一時ファイルを作成できません";
    return false;
  }
  wchar_t tempName[MAX_PATH + 1]{};
  if (!GetTempFileNameW(tempDir, L"cairn", 0, tempName)) {
    if (error) *error = "一時ファイルを作成できません";
    return false;
  }
  std::filesystem::path htmlPath(tempName);
  htmlPath.replace_extension(L".html");
  std::filesystem::path profilePath = htmlPath;
  profilePath.replace_extension();
  DeleteFileW(tempName);
  if (!CreateDirectoryW(profilePath.c_str(), nullptr)) {
    if (error) *error = "PDF 出力用の一時領域を作成できません";
    return false;
  }
  {
    std::ofstream file(htmlPath, std::ios::binary | std::ios::trunc);
    file.write(html.data(), static_cast<std::streamsize>(html.size()));
    if (!file) {
      DeleteFileW(htmlPath.c_str());
      std::error_code ec;
      std::filesystem::remove_all(profilePath, ec);
      if (error) *error = "一時 HTML を書き込めません";
      return false;
    }
  }
  if (!fileExists(htmlPath)) {
    std::error_code ec;
    std::filesystem::remove_all(profilePath, ec);
    if (error) *error = "一時 HTML が見つかりません";
    return false;
  }
  const std::wstring htmlUrl = fileUri(htmlPath.wstring());
  if (htmlUrl.empty()) {
    DeleteFileW(htmlPath.c_str());
    std::error_code ec;
    std::filesystem::remove_all(profilePath, ec);
    if (error) *error = "一時 HTML のファイル URL を作成できません";
    return false;
  }
  const std::wstring outPath = widen(path);
  std::wstring command = quoteArg(edge) + L" --headless --disable-gpu --no-first-run --disable-background-networking --disable-extensions " +
                         L"--user-data-dir=" + quoteArg(profilePath.wstring()) + L" --no-pdf-header-footer --print-to-pdf=" +
                         quoteArg(outPath) + L" " + quoteArg(htmlUrl);
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  const BOOL started = CreateProcessW(edge.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
                                      &startup, &process);
  bool ok = false;
  if (started) {
    const DWORD wait = WaitForSingleObject(process.hProcess, 120000);
    DWORD code = 1;
    if (wait == WAIT_OBJECT_0) GetExitCodeProcess(process.hProcess, &code);
    else if (wait == WAIT_TIMEOUT) {
      TerminateProcess(process.hProcess, ERROR_TIMEOUT);
      WaitForSingleObject(process.hProcess, 5000);
    }
    ok = wait == WAIT_OBJECT_0 && code == 0;
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
  }
  DeleteFileW(htmlPath.c_str());
  std::error_code profileError;
  std::filesystem::remove_all(profilePath, profileError);
  if (!ok) {
    if (error) *error = started ? "PDF を作成できませんでした（Edge の処理が失敗またはタイムアウトしました）" : "Microsoft Edge を起動できません";
    return false;
  }
  WIN32_FILE_ATTRIBUTE_DATA pdfInfo{};
  if (!GetFileAttributesExW(outPath.c_str(), GetFileExInfoStandard, &pdfInfo) ||
      (pdfInfo.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 ||
      (pdfInfo.nFileSizeHigh == 0 && pdfInfo.nFileSizeLow == 0)) {
    if (error) *error = "Edge は正常終了しましたが PDF ファイルが作られていません";
    return false;
  }
  return true;
}

// 関連リンクを開く（Web は既定のブラウザ、ファイルは関連付けられたアプリ、フォルダはエクスプローラー）。
// UI が先に検査・警告しているが、ここでも許可リスト（classifyLink）に通ったものだけを渡す。場所はログに出さない。
// 存在確認はしない（ネットワークドライブで固まらないため）。開けなければ理由を返す
bool Win32Platform::openLink(const std::string& target, std::string* error) {
  const LinkClass c = classifyLink(target);
  if (!c.ok()) {
    if (error) *error = c.reason;
    return false;
  }
  const HINSTANCE r = ShellExecuteW(hwnd_, L"open", widen(c.target).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
  const INT_PTR code = reinterpret_cast<INT_PTR>(r);
  if (code > 32) return true;
  if (error) {
    switch (code) {
      case ERROR_FILE_NOT_FOUND:
      case ERROR_PATH_NOT_FOUND:
        *error = "見つかりません（移動・削除されたか、共有につながっていない可能性があります）";
        break;
      case SE_ERR_ACCESSDENIED: *error = "アクセスが許可されていません"; break;
      case SE_ERR_NOASSOC:
      case SE_ERR_ASSOCINCOMPLETE: *error = "開くアプリが関連付けられていません"; break;
      case SE_ERR_SHARE: *error = "ほかのアプリが使用中のため開けません"; break;
      default: *error = "開けませんでした（コード " + std::to_string(code) + "）"; break;
    }
  }
  return false;
}

std::optional<std::string> Win32Platform::readTextFile(const std::string& path, std::string* error) {
  return win::readTextFile(widen(path), error);
}

void Win32Platform::openFolder(const std::string& path) {
  ShellExecuteW(hwnd_, L"open", widen(path).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

}  // namespace roadmap::win
