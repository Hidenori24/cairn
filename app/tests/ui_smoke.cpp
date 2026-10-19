// UI の煙テスト: ImGui をヘッドレスで動かし、各ビュー・ダイアログ・パレットを描いて落ちないこと・assert しないことを確かめる。
// Win32・DX11 は使わない（フォントのテクスチャを作るだけ）。
#include <cstdio>
#include <fstream>
#include <set>
#include <sstream>
#include <cstdlib>
#include <string>

#include <imgui.h>

#include "app_controller.hpp"
#include "roadmap/gen.hpp"
#include "roadmap/schedule.hpp"
#include "roadmap/text.hpp"
#include "ui/app_ui.hpp"

using namespace roadmap;

namespace {
struct NullPlatform : ui::IPlatform {
  std::optional<std::pair<std::string, std::string>> openTextFile() override { return std::nullopt; }
  bool saveTextFile(const std::string&, const std::string&) override { return true; }
  void openFolder(const std::string&) override {}
  bool systemDark() override { return false; }
  void requestQuit() override {}
};

int failures = 0;
void check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "NG: %s\n", what);
    ++failures;
  }
}

struct Harness {
  MemFileStore fs;
  FixedClock clk{LocalTime{2026, 10, 19, 9, 0, 0}};
  app::AppController c{fs, clk, "data", "prefs.json"};
  ui::UiState u;
  NullPlatform p;
  Harness() {
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1440, 900);
    io.DeltaTime = 1.f / 60.f;
    io.IniFilename = nullptr;
    u.fontBody = io.Fonts->AddFontDefault();
    u.fontMono = u.fontBold = u.fontLarge = u.fontBody;
    unsigned char* px;
    int w, h;
    io.Fonts->GetTexDataAsRGBA32(&px, &w, &h);
    ui::applyStyle(*u.t, u.m);
    c.start();
  }
  ~Harness() { ImGui::DestroyContext(); }
  void frame(int n = 1) {
    for (int i = 0; i < n; ++i) {
      clk.advanceMs(16);
      ImGui::NewFrame();
      ui::drawApp(c, u, p);
      ImGui::Render();
    }
  }
  void key(ImGuiKey k, bool ctrl = false, bool shift = false) {
    ImGuiIO& io = ImGui::GetIO();
    io.AddKeyEvent(ImGuiMod_Ctrl, ctrl);
    io.AddKeyEvent(ImGuiMod_Shift, shift);
    io.AddKeyEvent(k, true);
    frame();
    io.AddKeyEvent(k, false);
    io.AddKeyEvent(ImGuiMod_Ctrl, false);
    io.AddKeyEvent(ImGuiMod_Shift, false);
    frame();
  }
  void click(float x, float y, int count = 1) {
    ImGuiIO& io = ImGui::GetIO();
    for (int k = 0; k < count; ++k) {
      io.AddMousePosEvent(x, y);
      io.AddMouseButtonEvent(0, true);
      frame();
      io.AddMouseButtonEvent(0, false);
      frame();
    }
  }
};
}  // namespace

int main() {
  Harness h;
  h.frame(3);  // はじめに
  check(h.c.firstRun(), "初回はプロジェクトなし");
  h.c.createProject(app::SampleKind::Regulation, "サンプル（法規対応）", "");
  for (auto v : {app::View::Gantt, app::View::Board, app::View::List, app::View::Goals}) {
    h.c.setView(v);
    h.frame(3);
  }
  // ズーム・色分け
  for (const char* z : {"day", "week", "month", "all"}) {
    h.c.setView(app::View::Gantt);
    h.c.setUi("zoom", z);
    h.frame(2);
  }
  for (const char* cb : {"root", "float", "asg"}) {
    h.c.setUi("colorBy", cb);
    h.frame(2);
  }
  // 選択・詳細パネル・矢印の強調
  h.c.select("23");
  h.u.detailOpen = true;
  for (int tab = 0; tab < 3; ++tab) {
    h.u.detailTab = tab;
    h.frame(2);
  }
  // キー操作
  h.key(ImGuiKey_DownArrow);
  h.key(ImGuiKey_N);  // 追加して編集
  h.frame(2);
  h.key(ImGuiKey_Escape);
  h.key(ImGuiKey_Z, true);
  h.key(ImGuiKey_K, true);  // パレット
  h.frame(2);
  h.key(ImGuiKey_Escape);
  // 複数選択（まとめ編集バー）
  h.c.setSelection({"6", "7", "8"}, "8");
  h.frame(3);
  // すべてのダイアログ
  for (int d = 1; d <= static_cast<int>(ui::Dlg::ConfirmQuit); ++d) {
    h.u.dTargets = {"1"};
    h.u.dlg = static_cast<ui::Dlg>(d);
    h.u.dlgOpening = true;
    h.frame(3);
    h.u.dlg = ui::Dlg::None;
    h.frame(1);
  }
  // 絞り込み・並べ替え
  h.c.filter().q = "CRA";
  h.c.setView(app::View::List);
  h.u.sortKey = "float";
  h.u.sortDir = 1;
  h.frame(3);
  h.u.sortDir = 0;
  h.c.filter() = {};
  // スイムレーン
  h.c.setView(app::View::Board);
  for (const char* l : {"none", "asg", "root"}) {
    h.c.setUi("lane", l);
    h.frame(2);
  }
  // 階層デモ・資源制限・ダーク・コンパクト・高DPI
  h.c.createProject(app::SampleKind::Hierarchy, "階層デモ", "DEV");
  h.c.session()->setResource({true, 1});
  h.u.t = &ui::darkTokens();
  h.u.m.compact = true;
  h.u.m.scale = 2.0f;
  ui::applyStyle(*h.u.t, h.u.m);
  for (auto v : {app::View::Gantt, app::View::Board, app::View::List, app::View::Goals}) {
    h.c.setView(v);
    h.frame(3);
  }
  // 5,000件のガント（仮想化）
  {
    auto g = generateProject({5000, 7});
    auto r = h.c.importText(exportAppCsv(g.issues, g.meta), "大きい", app::ImportMode::New);
    check(r.ok, "5000件の取り込み");
    h.u.m.scale = 1.0f;
    h.u.m.compact = false;
    ui::applyStyle(*h.u.t, h.u.m);
    h.c.setView(app::View::Gantt);
    h.frame(5);
    ImGui::GetIO().AddMouseWheelEvent(0, -50);
    h.frame(5);
    h.c.setView(app::View::List);
    h.frame(3);
  }
  // マウス: ガントの行をクリック・ダブルクリック
  h.c.setView(app::View::Gantt);
  h.click(200, 160);
  h.click(200, 160, 2);
  h.frame(3);
  // 空のプロジェクト
  h.c.createProject(app::SampleKind::Empty, "空", "");
  h.frame(3);
  h.c.deleteCurrentProject();
  h.frame(2);
  // 画面の文言の文字が ui_glyphs.inc にすべて入っていること（文言を変えたら tools/gen_ui_glyphs.py を実行）
  {
    std::set<ImWchar> have(ui::uiGlyphs().begin(), ui::uiGlyphs().end());
    const char* files[] = {"app/ui/app_ui.cpp", "app/ui/view_list.cpp", "app/ui/view_gantt.cpp", "app/ui/view_board.cpp",
                           "app/ui/view_goals.cpp", "app/ui/panel_detail.cpp", "app/ui/palette.cpp", "app/ui/dialogs.cpp",
                           "app/ui/widgets.cpp", "app/ui/strings.hpp", "app/controller/app_controller.cpp",
                           "core/src/commands.cpp", "core/src/csv.cpp", "core/src/engine.cpp", "core/src/store.cpp"};
    for (const char* rel : files) {
      std::ifstream in(std::string(ROADMAP_SOURCE_DIR) + "/" + rel, std::ios::binary);
      std::stringstream ss;
      ss << in.rdbuf();
      const std::string src = ss.str();
      check(!src.empty(), rel);
      for (std::size_t i = 0; i < src.size();) {
        const char32_t cp = text::decodeAt(src, i);
        if (cp >= 0x80 && cp <= 0xFFFF && !have.count(static_cast<ImWchar>(cp))) {
          std::fprintf(stderr, "ui_glyphs.inc にない文字 U+%04X（%s）。tools/gen_ui_glyphs.py を実行してください\n",
                       static_cast<unsigned>(cp), rel);
          ++failures;
          break;
        }
      }
    }
  }
  std::set<ImWchar> glyphs;
  ui::collectGlyphs(h.c, glyphs);
  check(!glyphs.empty(), "文字の収集");
  if (failures) return 1;
  std::printf("ui_smoke: OK\n");
  return 0;
}
