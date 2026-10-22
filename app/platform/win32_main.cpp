// Windows の入口: ウィンドウ・DirectX 11・フォント・IME・高DPI・ファイルのドロップ・終了時の確認（SPEC 3.4・8.1・10章）
#include <windows.h>

#include <d3d11.h>
#include <imm.h>
#include <shellapi.h>

#include <cstdio>
#include <exception>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <imgui_internal.h>

#include "app_controller.hpp"
#include "platform/win32_gh.hpp"
#include "platform/win32_platform.hpp"
#include "roadmap/clock.hpp"
#include "roadmap/filestore.hpp"
#include "roadmap/store.hpp"
#include "ui/app_ui.hpp"
#include "ui/theme.hpp"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

using namespace roadmap;

namespace {

ID3D11Device* gDevice = nullptr;
ID3D11DeviceContext* gContext = nullptr;
IDXGISwapChain* gSwap = nullptr;
ID3D11RenderTargetView* gRtv = nullptr;
UINT gResizeW = 0, gResizeH = 0;

app::AppController* gCtl = nullptr;
ui::UiState* gUi = nullptr;
win::Win32Platform* gPlat = nullptr;
bool gFontsDirty = true;
float gDpiScale = 1.0f;
std::set<ImWchar> gExtraGlyphs;
bool gCloseRequested = false;
bool gVerbose = false;

void vlog(const char* s) {
  if (gVerbose) std::fprintf(stderr, "[roadmap] %s\n", s);
}

// ---------------------------------------------------------------- データフォルダの監視（8.5 の任意項目）
// 変更の通知を受けたら少し待ってから同期する（Excel の保存などで続けて届く通知をまとめる）。
// 自分の保存でも通知は来るが、同期は更新時刻が覚えているものと同じなら何もしない
class FolderWatch {
 public:
  ~FolderWatch() { close(); }
  void watch(const std::string& dir) {
    if (dir == dir_ && h_ != INVALID_HANDLE_VALUE) return;
    close();
    dir_ = dir;
    // プロジェクトごとのフォルダの中（<data>/<プロジェクト>/project.csv）も見るため、下のフォルダまで監視する
    h_ = FindFirstChangeNotificationW(win::widen(dir).c_str(), TRUE,
                                      FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE |
                                          FILE_NOTIFY_CHANGE_SIZE);
    vlog(h_ == INVALID_HANDLE_VALUE ? "フォルダを監視できません" : "フォルダの監視を始めました");
  }
  HANDLE handle() const { return h_; }
  bool fired() {
    if (h_ == INVALID_HANDLE_VALUE || WaitForSingleObject(h_, 0) != WAIT_OBJECT_0) return false;
    FindNextChangeNotification(h_);
    return true;
  }

 private:
  void close() {
    if (h_ != INVALID_HANDLE_VALUE) FindCloseChangeNotification(h_);
    h_ = INVALID_HANDLE_VALUE;
  }
  std::string dir_;
  HANDLE h_ = INVALID_HANDLE_VALUE;
};

// ---------------------------------------------------------------- DirectX 11
void createRenderTarget() {
  ID3D11Texture2D* back = nullptr;
  gSwap->GetBuffer(0, IID_PPV_ARGS(&back));
  if (back) {
    gDevice->CreateRenderTargetView(back, nullptr, &gRtv);
    back->Release();
  }
}
void cleanupRenderTarget() {
  if (gRtv) {
    gRtv->Release();
    gRtv = nullptr;
  }
}
bool createDevice(HWND hwnd) {
  DXGI_SWAP_CHAIN_DESC sd{};
  sd.BufferCount = 2;
  sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  sd.BufferDesc.RefreshRate.Numerator = 60;
  sd.BufferDesc.RefreshRate.Denominator = 1;
  sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
  sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  sd.OutputWindow = hwnd;
  sd.SampleDesc.Count = 1;
  sd.Windowed = TRUE;
  sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
  const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
  D3D_FEATURE_LEVEL got;
  HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2, D3D11_SDK_VERSION,
                                             &sd, &gSwap, &gDevice, &got, &gContext);
  if (hr == DXGI_ERROR_UNSUPPORTED)  // GPU がなければソフトウェア描画
    hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 2, D3D11_SDK_VERSION, &sd,
                                       &gSwap, &gDevice, &got, &gContext);
  if (hr != S_OK) return false;
  createRenderTarget();
  return true;
}
void cleanupDevice() {
  cleanupRenderTarget();
  if (gSwap) gSwap->Release();
  if (gContext) gContext->Release();
  if (gDevice) gDevice->Release();
  gSwap = nullptr;
  gContext = nullptr;
  gDevice = nullptr;
}

// ---------------------------------------------------------------- フォント（SPEC 3.4: Yu Gothic UI → Meiryo UI → MS UI Gothic）
struct FontFile {
  const wchar_t* file;
  int index;  // TTC の中の番号
};

std::string fontsDir() {
  wchar_t buf[MAX_PATH];
  const UINT n = GetWindowsDirectoryW(buf, MAX_PATH);
  return win::narrow(std::wstring(buf, n)) + "\\Fonts\\";
}

bool fileExists(const std::string& p) {
  const DWORD a = GetFileAttributesW(win::widen(p).c_str());
  return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

// 画面の倍率 = DPI × アプリの設定の「文字の大きさ」
float uiScale() {
  const int pct = gCtl ? gCtl->prefs().uiScale : 100;
  return gDpiScale * static_cast<float>(pct) / 100.0f;
}

void buildFonts(ui::UiState& u) {
  ImGuiIO& io = ImGui::GetIO();
  io.Fonts->Clear();
  const std::string dir = fontsDir();
  const float px = 13.0f * uiScale();
  // 文字の範囲: 日本語（常用漢字を含む約3,000字）+ 記号 + データにある文字
  ImFontGlyphRangesBuilder rb;
  rb.AddRanges(io.Fonts->GetGlyphRangesJapanese());
  static const ImWchar extra[] = {0x2000, 0x206F, 0x2190, 0x21FF, 0x2200, 0x22FF, 0x2460, 0x24FF, 0x2500, 0x257F,
                                  0x25A0, 0x25FF, 0x2600, 0x26FF, 0x2700, 0x27BF, 0x3000, 0x30FF, 0xFF00, 0xFFEF, 0};
  rb.AddRanges(extra);
  for (ImWchar c : ui::uiGlyphs()) rb.AddChar(c);
  for (ImWchar c : gExtraGlyphs) rb.AddChar(c);
  static ImVector<ImWchar> ranges;
  ranges.clear();
  rb.BuildRanges(&ranges);

  const FontFile regular[] = {{L"YuGothR.ttc", 1}, {L"YuGothM.ttc", 1}, {L"meiryo.ttc", 2}, {L"msgothic.ttc", 1}};
  const FontFile bold[] = {{L"YuGothB.ttc", 1}, {L"meiryob.ttc", 2}};
  const auto load = [&](const FontFile* list, int n, float size, bool merge, const ImWchar* rg) -> ImFont* {
    for (int k = 0; k < n; ++k) {
      const std::string path = dir + win::narrow(list[k].file);
      if (!fileExists(path)) continue;
      ImFontConfig cfg;
      cfg.FontNo = list[k].index;
      cfg.MergeMode = merge;
      cfg.OversampleH = 1;
      cfg.PixelSnapH = true;
      if (ImFont* f = io.Fonts->AddFontFromFileTTF(path.c_str(), size, &cfg, rg)) return f;
    }
    return nullptr;
  };
  const auto symbols = [&](float size) {
    static const ImWchar sym[] = {0x2190, 0x21FF, 0x2600, 0x27BF, 0};
    const std::string p = dir + "seguisym.ttf";
    if (!fileExists(p)) return;
    ImFontConfig cfg;
    cfg.MergeMode = true;
    cfg.OversampleH = 1;
    io.Fonts->AddFontFromFileTTF(p.c_str(), size, &cfg, sym);
  };
  u.fontBody = load(regular, 4, px, false, ranges.Data);
  if (!u.fontBody) u.fontBody = io.Fonts->AddFontDefault();
  symbols(px);
  // 等幅（数字・日付）: Consolas + 日本語を合成
  {
    const std::string p = dir + "consola.ttf";
    ImFontConfig cfg;
    cfg.OversampleH = 1;
    u.fontMono = fileExists(p) ? io.Fonts->AddFontFromFileTTF(p.c_str(), px, &cfg, io.Fonts->GetGlyphRangesDefault()) : nullptr;
    if (u.fontMono) load(regular, 4, px, true, ranges.Data);
    else u.fontMono = u.fontBody;
  }
  u.fontBold = load(bold, 2, px, false, ranges.Data);
  if (u.fontBold) symbols(px);
  else u.fontBold = u.fontBody;
  u.fontLarge = load(regular, 4, 17.0f * gDpiScale, false, ranges.Data);
  if (!u.fontLarge) u.fontLarge = u.fontBody;
  io.Fonts->Build();
  ImGui_ImplDX11_InvalidateDeviceObjects();
  ImGui_ImplDX11_CreateDeviceObjects();
}

// データにある文字のうち、フォントに入っていないものを足して作り直す
void checkGlyphs() {
  std::set<ImWchar> want;
  ui::collectGlyphs(*gCtl, want);
  ImFont* f = gUi->fontBody;
  bool added = false;
  for (ImWchar c : want)
    if (!gExtraGlyphs.count(c) && f && !f->FindGlyphNoFallback(c)) {
      gExtraGlyphs.insert(c);
      added = true;
    }
  if (added) gFontsDirty = true;
}

// ---------------------------------------------------------------- テーマ・スタイル
void applyTheme() {
  const std::string t = gCtl->prefs().theme;
  const bool dark = t == "dark" || (t == "auto" && win::systemDarkMode());
  gUi->t = dark ? &ui::darkTokens() : &ui::lightTokens();
  gUi->m.scale = uiScale();
  gUi->m.compact = gCtl->prefs().density == "compact";
  ui::applyStyle(*gUi->t, gUi->m);
}

// ---------------------------------------------------------------- ウィンドウ
LRESULT WINAPI wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
    case WM_IME_STARTCOMPOSITION:
      vlog("WM_IME_STARTCOMPOSITION");
      if (gUi) gUi->imeComposing = true;
      break;
    case WM_IME_ENDCOMPOSITION:
      vlog("WM_IME_ENDCOMPOSITION");
      if (gUi) {
        gUi->imeComposing = false;
        gUi->imeCooldown = 2;  // 確定の Enter で別の操作が走らないように
      }
      break;
    default: break;
  }
  // IME の変換中・確定直後の Enter は ImGui に渡さない（確定の Enter で編集が終わったり行が増えたりしないように。付録D-1）
  const bool imeEnter = (msg == WM_KEYDOWN || msg == WM_KEYUP) && wParam == VK_RETURN && gUi &&
                        (gUi->imeComposing || gUi->imeCooldown > 0);
  if (imeEnter) vlog("IME の変換中の Enter を無視");  // キーの中身（何を打ったか）はログに出さない
  if (!imeEnter && ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam)) return true;
  switch (msg) {
    case WM_SIZE:
      if (wParam == SIZE_MINIMIZED) return 0;
      gResizeW = LOWORD(lParam);
      gResizeH = HIWORD(lParam);
      return 0;
    case WM_GETMINMAXINFO: {
      auto* mm = reinterpret_cast<MINMAXINFO*>(lParam);
      mm->ptMinTrackSize.x = static_cast<LONG>(960 * gDpiScale);
      mm->ptMinTrackSize.y = static_cast<LONG>(600 * gDpiScale);
      return 0;
    }
    case WM_DPICHANGED: {
      gDpiScale = static_cast<float>(HIWORD(wParam)) / 96.0f;
      const RECT* r = reinterpret_cast<RECT*>(lParam);
      SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
      gFontsDirty = true;
      return 0;
    }
    case WM_ACTIVATE:
      if (LOWORD(wParam) != WA_INACTIVE && gCtl) gCtl->onActivated();
      break;
    case WM_SETTINGCHANGE:
      if (gCtl && gCtl->prefs().theme == "auto") applyTheme();
      break;
    case WM_DROPFILES: {
      // 複数のファイル・フォルダ。ドロップ先が課題なら添付、何もないところなら CSV の取り込み（UI が判断する）
      HDROP drop = reinterpret_cast<HDROP>(wParam);
      const UINT n = DragQueryFileW(drop, 0xFFFFFFFFu, nullptr, 0);
      POINT pt{};
      DragQueryPoint(drop, &pt);
      std::vector<std::string> paths;
      for (UINT k = 0; k < n; ++k) {
        const UINT len = DragQueryFileW(drop, k, nullptr, 0);
        std::wstring w(static_cast<std::size_t>(len) + 1, L'\0');
        DragQueryFileW(drop, k, w.data(), len + 1);
        w.resize(len);
        paths.push_back(win::narrow(w));
      }
      DragFinish(drop);
      if (gUi && !paths.empty()) {
        gUi->pendingDrop.active = true;
        gUi->pendingDrop.paths = std::move(paths);
        gUi->pendingDrop.pos = ImVec2(static_cast<float>(pt.x), static_cast<float>(pt.y));
      }
      return 0;
    }
    case WM_CLOSE:
      gCloseRequested = true;
      return 0;
    case WM_SYSCOMMAND:
      if ((wParam & 0xfff0) == SC_KEYMENU) return 0;  // Alt でメニューに入らない
      break;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
    default: break;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

struct Args {
  std::string dataDir;
  bool verbose = false;
};

Args parseArgs() {
  Args a;
  int argc = 0;
  LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  for (int i = 1; i < argc; ++i) {
    const std::wstring s = argv[i];
    if (s == L"--data" && i + 1 < argc) a.dataDir = win::narrow(argv[++i]);
    else if (s.rfind(L"--data=", 0) == 0) a.dataDir = win::narrow(s.substr(7));
    else if (s == L"--verbose") a.verbose = true;
  }
  LocalFree(argv);
  return a;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int) {
  const Args args = parseArgs();
  gVerbose = args.verbose;
  if (gVerbose && AttachConsole(ATTACH_PARENT_PROCESS)) {
    FILE* f = nullptr;
    freopen_s(&f, "CONOUT$", "w", stderr);
  }
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
  ImGui_ImplWin32_EnableDpiAwareness();

  // データフォルダと設定ファイルの場所（SPEC 8.1）
  StdFileStore fs;
  SystemClock clock;
  const std::string exe = win::exeDir();
  const std::string local = win::localAppDataDir();
  // 設定ファイルを先に決め、アプリの設定で選んだ保存先を読む（--data > ROADMAP_DATA > 設定 > 既定）
  std::string prefsPath = joinPath(exe, "roadmap_prefs.json");
  if (!fs.probeWritable(exe)) {
    (void)fs.makeDirs(local);
    prefsPath = joinPath(local, "roadmap_prefs.json");
  }
  const Prefs bootPrefs = loadPrefs(fs, prefsPath);
  const DataDirChoice dd =
      chooseDataDir(fs, args.dataDir, win::envVar("ROADMAP_DATA"), bootPrefs.dataDir, exe, local + "\\projects");
  vlog(("data: " + dd.dir).c_str());

  app::AppController ctl(fs, clock, dd.dir, prefsPath);
  ctl.setDataDirInfo(dd, exe);
  ui::UiState uiState;
  gCtl = &ctl;
  gUi = &uiState;
  ctl.start();
  // GitHub の PR・Issue の状態は gh で取る（押したときだけ。別のスレッドで 1 件ずつ。Issue #61）
  ctl.setUtcOffsetFn(win::utcOffsetMinutesAt);
  ctl.setRemoteStatus(win::makeGhRemoteStatus());

  // ウィンドウ（前回の位置・サイズ）
  WNDCLASSEXW wc{sizeof(wc), CS_CLASSDC, wndProc, 0, 0, hInst, LoadIconW(hInst, L"IDI_APP"), LoadCursorW(nullptr, IDC_ARROW),
                 nullptr, nullptr, L"ProjectRoadmap", nullptr};
  RegisterClassExW(&wc);
  const WindowPlacement& wp = ctl.prefs().window;
  HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"プロジェクト管理", WS_OVERLAPPEDWINDOW, wp.x, wp.y, wp.w, wp.h, nullptr,
                              nullptr, hInst, nullptr);
  if (!hwnd || !createDevice(hwnd)) {
    MessageBoxW(nullptr, L"DirectX 11 を初期化できませんでした。", L"プロジェクト管理", MB_OK | MB_ICONERROR);
    cleanupDevice();
    return 1;
  }
  gDpiScale = ImGui_ImplWin32_GetDpiScaleForHwnd(hwnd);
  {
    // 画面外に出ていたら既定の位置へ
    RECT r{wp.x, wp.y, wp.x + wp.w, wp.y + wp.h};
    if (!MonitorFromRect(&r, MONITOR_DEFAULTTONULL)) SetWindowPos(hwnd, nullptr, 100, 100, 1440, 900, SWP_NOZORDER);
  }
  ShowWindow(hwnd, wp.maximized ? SW_SHOWMAXIMIZED : SW_SHOWDEFAULT);
  UpdateWindow(hwnd);
  DragAcceptFiles(hwnd, TRUE);
  win::Win32Platform platform(hwnd);
  platform.setDevice(gDevice);
  gPlat = &platform;

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr;  // 画面の配置は保存しない（設定は roadmap_prefs.json）
  io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
  ImGui_ImplWin32_Init(hwnd);
  ImGui_ImplDX11_Init(gDevice, gContext);
  applyTheme();
  std::string lastTheme = ctl.prefs().theme, lastDensity = ctl.prefs().density;
  int lastScale = ctl.prefs().uiScale;
  std::string lastProject;
  std::uint64_t lastVersion = 0;

  bool done = false;
  int idleFrames = 0;
  FolderWatch watch;
  ULONGLONG syncDueAt = 0;  // 0 = 予定なし
  while (!done) {
    watch.watch(ctl.dataDir());  // 保存先を切り替えたら監視し直す
    // 入力がなければ少し待つ（保存のタイマーは 50ms ごとに回る。フォルダの変更でも起きる）
    if (idleFrames > 4) {
      HANDLE h = watch.handle();
      MsgWaitForMultipleObjects(h != INVALID_HANDLE_VALUE ? 1 : 0, &h, FALSE, 50, QS_ALLINPUT);
    }
    if (watch.fired()) syncDueAt = GetTickCount64() + 500;
    // 編集中・ダイアログ中は待つ（入力の途中でプロジェクトを開き直さない）
    if (syncDueAt != 0 && GetTickCount64() >= syncDueAt && uiState.editField == ui::EditField::None &&
        uiState.dlg == ui::Dlg::None && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
      syncDueAt = 0;
      vlog("フォルダの変更を読み込みます");
      ctl.syncFolder(false);
    }
    MSG msg;
    bool any = false;
    while (PeekMessageW(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
      if (msg.message == WM_QUIT) done = true;
      any = true;
    }
    if (done) break;
    idleFrames = any ? 0 : idleFrames + 1;
    if (gResizeW != 0 && gResizeH != 0) {
      cleanupRenderTarget();
      gSwap->ResizeBuffers(0, gResizeW, gResizeH, DXGI_FORMAT_UNKNOWN, 0);
      gResizeW = gResizeH = 0;
      createRenderTarget();
    }
    // テーマ・密度・DPI・文字の変化
    if (ctl.prefs().theme != lastTheme || ctl.prefs().density != lastDensity) {
      lastTheme = ctl.prefs().theme;
      lastDensity = ctl.prefs().density;
      applyTheme();
      win::setDarkTitleBar(hwnd, uiState.t->dark);
    }
    if (ctl.prefs().uiScale != lastScale) {  // 文字の大きさ → フォントを作り直す
      lastScale = ctl.prefs().uiScale;
      gFontsDirty = true;
    }
    if (gFontsDirty) {
      buildFonts(uiState);
      applyTheme();
      win::setDarkTitleBar(hwnd, uiState.t->dark);
      gFontsDirty = false;
      lastProject = "\x01";  // 作り直したフォントで、データの文字をもう一度確かめる
    }
    // データにフォントにない文字があれば、次のフレームでフォントを作り直す
    if (ctl.currentId() != lastProject || (ctl.session() && ctl.session()->version() != lastVersion)) {
      lastProject = ctl.currentId();
      lastVersion = ctl.session() ? ctl.session()->version() : 0;
      checkGlyphs();
    }
    // 終了の確認（未保存があればまず保存。保存できなければ確認する）
    if (gCloseRequested) {
      gCloseRequested = false;
      if (!ctl.hasUnsaved() || ctl.flushNow()) platform.requestQuit();
      else {
        uiState.dlg = ui::Dlg::ConfirmQuit;
        uiState.dlgOpening = true;
      }
    }
    if (platform.quitRequested()) {
      WINDOWPLACEMENT pl{};
      pl.length = sizeof(pl);
      if (GetWindowPlacement(hwnd, &pl)) {
        WindowPlacement& w = ctl.prefs().window;
        w.x = pl.rcNormalPosition.left;
        w.y = pl.rcNormalPosition.top;
        w.w = pl.rcNormalPosition.right - pl.rcNormalPosition.left;
        w.h = pl.rcNormalPosition.bottom - pl.rcNormalPosition.top;
        w.maximized = pl.showCmd == SW_SHOWMAXIMIZED;
        ctl.savePrefs();
      }
      DestroyWindow(hwnd);
      continue;
    }

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    try {
      ui::drawApp(ctl, uiState, platform);
    } catch (const std::exception& e) {
      // 例外は最上位で捕まえて、通知を出して続ける（SPEC 11）
      ImGui::ErrorCheckEndFrameRecover(nullptr);
      ctl.toast(std::string("内部エラー: ") + e.what());
    }
    ImGui::Render();
    const ImVec4 bg = ui::v4(uiState.t->bg);
    const float clear[4] = {bg.x, bg.y, bg.z, 1.0f};
    gContext->OMSetRenderTargets(1, &gRtv, nullptr);
    gContext->ClearRenderTargetView(gRtv, clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    gSwap->Present(1, 0);
  }
  ctl.flushNow();
  ImGui_ImplDX11_Shutdown();
  ImGui_ImplWin32_Shutdown();
  ImGui::DestroyContext();
  cleanupDevice();
  UnregisterClassW(wc.lpszClassName, wc.hInstance);
  CoUninitialize();
  return 0;
}
