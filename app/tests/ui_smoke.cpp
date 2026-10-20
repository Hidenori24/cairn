// UI の煙テスト: ImGui をヘッドレスで動かし、各ビュー・ダイアログ・パレットを描いて落ちないこと・assert しないことを確かめる。
// Win32・DX11 は使わない（フォントのテクスチャを作るだけ）。
#include <cmath>
#include <cstdio>
#include <fstream>
#include <set>
#include <sstream>
#include <cstdlib>
#include <string>

#include <imgui.h>
#include <imgui_internal.h>

#include "app_controller.hpp"
#include "roadmap/gen.hpp"
#include "roadmap/schedule.hpp"
#include "roadmap/text.hpp"
#include "ui/app_ui.hpp"
#include "ui/internal.hpp"

using namespace roadmap;

namespace {
struct NullPlatform : ui::IPlatform {
  std::optional<std::pair<std::string, std::string>> openTextFile() override { return std::nullopt; }
  bool saveTextFile(const std::string&, const std::string&) override { return true; }
  void openFolder(const std::string&) override {}
  std::optional<std::string> pickFolder(const std::string&) override { return picked; }
  std::optional<std::string> picked;
  bool systemDark() override { return false; }
  void requestQuit() override {}
  std::optional<std::string> clip;  // クリップボードの代わり（テストが差し替える）
  std::optional<std::string> clipboardText() override { return clip; }
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
  // その場編集: 入力して Enter で確定する（IME の変換中の Enter は無視）
  {
    h.c.setView(app::View::List);
    h.frame(2);
    ui::Frame fr{h.c, h.u, h.p};
    ui::beginEdit(fr, "6", ui::EditField::Name);
    h.frame(2);
    ImGuiIO& io = ImGui::GetIO();
    io.AddKeyEvent(ImGuiMod_Ctrl, true);
    io.AddKeyEvent(ImGuiKey_A, true);
    h.frame();
    io.AddKeyEvent(ImGuiKey_A, false);
    io.AddKeyEvent(ImGuiMod_Ctrl, false);
    io.AddInputCharactersUTF8("新しい名前");
    h.frame();
    // 変換中（プラットフォームが Enter を ImGui に渡さない）・確定直後のフレームは編集を続ける
    h.u.imeComposing = true;
    h.frame(2);
    h.u.imeComposing = false;
    h.u.imeCooldown = 2;
    h.frame(1);
    check(h.u.editField == ui::EditField::Name, "変換中は編集が続く");
    h.frame(2);
    h.key(ImGuiKey_Enter);
    check(h.u.editField == ui::EditField::None, "Enter で編集が終わる");
    const Issue* o = h.c.session()->issue("6");
    check(o && o->summary == "[T01] 新しい名前", "Enter で件名が確定する");
    // キーボードから編集を始める（F2）→ Esc で取り消し
    h.c.select("7");
    h.key(ImGuiKey_F2);
    check(h.u.editField == ui::EditField::Name && h.u.editId == "7", "F2 で編集を始める");
    io.AddInputCharactersUTF8("取り消す");
    h.frame();
    h.key(ImGuiKey_Escape);
    check(h.u.editField == ui::EditField::None, "Esc で編集をやめる");
    check(h.c.session()->issue("7")->summary.find("取り消す") == std::string::npos, "Esc では変更しない");
    // N で追加して編集 → Enter（追加直後の行は次の行を追加して続ける）
    h.c.select("7");
    // N を押した直後（入力欄がまだアクティブになる前）に打った文字も落とさない（B13）。
    // N 自身の文字は入れない
    io.AddKeyEvent(ImGuiKey_N, true);
    io.AddInputCharacter('n');
    h.frame();
    io.AddKeyEvent(ImGuiKey_N, false);
    check(h.u.editField == ui::EditField::Name && h.u.editFresh, "N で追加して編集");
    const std::string first = h.u.editId;
    io.AddInputCharactersUTF8("追");
    h.frame();
    io.AddInputCharactersUTF8("加1");
    h.frame();
    h.key(ImGuiKey_Enter);
    check(h.u.editField == ui::EditField::Name && h.u.editId != first, "追加直後の Enter で次の行");
    h.key(ImGuiKey_Escape);
    check(h.c.session()->issue(first) && h.c.session()->issue(first)->summary == "追加1", "追加した行の件名");
  }
  // ガントでも同じ（編集の確定で計算し直しても、同じフレームの描画が古い参照で壊れないこと）
  {
    h.c.setView(app::View::Gantt);
    h.frame(2);
    ui::Frame fr{h.c, h.u, h.p};
    ui::beginEdit(fr, "8", ui::EditField::Name);
    h.frame(3);
    ImGui::GetIO().AddInputCharactersUTF8("ガントで編集");
    h.frame();
    h.key(ImGuiKey_Enter);
    check(h.c.session()->issue("8")->summary == "[T03] ガントで編集", "ガントで件名が確定する");
    h.key(ImGuiKey_Tab);  // 選択全体の階層を下げる → 元に戻す
    h.key(ImGuiKey_Z, true);
    h.key(ImGuiKey_Delete);  // 削除 → 元に戻す
    h.key(ImGuiKey_Z, true);
    check(h.c.session()->issue("8") != nullptr, "削除を元に戻せる");
  }
  // 複数選択（まとめ編集バー）
  h.c.setSelection({"6", "7", "8"}, "8");
  h.frame(3);
  // すべてのダイアログ
  for (int d = 1; d <= static_cast<int>(ui::Dlg::ConfirmIssueDelete); ++d) {
    h.u.dTargets = {"1"};
    h.u.dlg = static_cast<ui::Dlg>(d);
    h.u.dlgOpening = true;
    h.frame(3);
    h.u.dlg = ui::Dlg::None;
    h.frame(1);
  }
  // 貼り付け: Ctrl+V → プレビュー → 追加 → 元に戻す（クリップボードはスタブ）
  {
    h.c.setView(app::View::List);
    h.u.editField = ui::EditField::None;
    h.frame(2);
    const std::size_t n0 = h.c.session()->issues().size();
    // 親が選ばれていない・クリップボードが空のときはダイアログを開かない
    h.c.clearSelection();
    h.p.clip = "a\nb";
    h.key(ImGuiKey_V, true);
    check(h.u.dlg == ui::Dlg::None, "親が選ばれていなければ貼り付けのダイアログを開かない");
    h.c.select("6");
    h.p.clip = std::nullopt;
    h.key(ImGuiKey_V, true);
    check(h.u.dlg == ui::Dlg::None, "クリップボードが空ならダイアログを開かない");
    h.p.clip = " \n\u3000\n";
    h.key(ImGuiKey_V, true);
    check(h.u.dlg == ui::Dlg::None, "空行だけなら開かない");
    // 解析 → プレビュー（階層が飛ぶ行は丸めて警告）
    h.p.clip = "子A\n\t孫A1\n\t\t\t飛んだ孫\n- 子B\r\n\n";
    h.key(ImGuiKey_V, true);
    check(h.u.dlg == ui::Dlg::PasteChildren, "Ctrl+V で貼り付けのプレビューを開く");
    check(h.u.paste.items.size() == 4 && !h.u.paste.warns.empty(), "プレビューの件数と警告");
    h.frame(3);
    // Esc で取り消し（何も作らず、貼り付けた内容も捨てる）
    h.key(ImGuiKey_Escape);
    check(h.u.dlg == ui::Dlg::None && h.u.paste.items.empty(), "Esc で取り消せる");
    check(h.c.session()->issues().size() == n0, "取り消したら作らない");
    // 「追加」ボタン（Esc は選択も外すので、選び直す）
    h.c.select("6");
    h.key(ImGuiKey_V, true);
    check(h.u.dlg == ui::Dlg::PasteChildren, "もう一度開く");
    h.frame(3);
    ImGuiWindow* w = ImGui::FindWindowByName("###dlg");
    check(w != nullptr, "ダイアログの窓");
    if (w) {
      // 右下の主ボタン。位置は字の幅に左右されるので、窓の中を縦に探す
      for (float dy = 8; dy < 50 && h.u.dlg == ui::Dlg::PasteChildren; dy += 4) h.click(w->Pos.x + w->Size.x - 45, w->Pos.y + w->Size.y - dy);
      h.frame(2);
    }
    check(h.u.dlg == ui::Dlg::None, "追加でダイアログが閉じる");
    check(h.c.session()->issues().size() == n0 + 4, "貼り付けた行が子課題になる");
    const Issue* a = nullptr;
    for (const auto& o : h.c.session()->issues())
      if (o.summary.find("子A") != std::string::npos) a = &o;
    check(a && a->parent == "6", "子Aは選んだ課題の子");
    h.c.undo();
    h.frame(2);
    check(h.c.session()->issues().size() == n0, "Undo 1回でまとめて戻る");
    h.p.clip = std::nullopt;
  }
  // 絞り込み・並べ替え
  h.c.filter().q = "CRA";
  h.c.setView(app::View::List);
  h.u.sortKey = "float";
  h.u.sortDir = 1;
  h.frame(3);
  h.u.sortDir = 0;
  h.c.filter() = {};
  // 上の絞り込みは薄くするだけ（行は減らない）・列の絞り込みは隠す
  {
    const std::size_t all = h.c.visibleRows().size();
    h.c.filter().st = StFilter::Done;
    h.c.filter().flt = FltFilter::Late;
    for (auto v : {app::View::Gantt, app::View::Board, app::View::List}) {
      h.c.setView(v);
      h.frame(2);
    }
    check(h.c.visibleRows().size() == all, "上の絞り込みでは行が減らない");
    check(!h.c.dimmedSet().empty(), "上の絞り込みで薄くする課題がある");
    h.c.filter() = {};
    h.c.setView(app::View::List);
    for (const char* col : {"name", "type", "status", "asg", "prio", "float"}) {
      h.u.openColFilter = col;
      h.frame(2);
      h.key(ImGuiKey_Escape);
    }
    h.c.setView(app::View::Gantt);
    for (const char* col : {"name", "asg", "status", "float"}) {
      h.u.openColFilter = col;
      h.frame(2);
      h.key(ImGuiKey_Escape);
    }
    h.c.columnFilter().toggle("status", "To Do");
    h.frame(2);
    check(h.c.visibleRows().size() < all, "列の絞り込みで行が隠れる");
    h.c.setView(app::View::List);
    h.u.sortKey = "status";
    h.u.sortDir = 1;
    h.frame(2);
    h.u.sortDir = 0;
    h.u.sortKey.clear();
    h.c.clearFilter();
    h.frame(2);
  }
  // アプリの設定（保存先の切り替えの確認・文字の大きさ・薄さ）と、削除の前の確認
  {
    h.key(ImGuiKey_Comma, true);
    check(h.u.dlg == ui::Dlg::AppSettings, "Ctrl+, でアプリの設定");
    h.frame(2);
    h.u.pendingDir = "data2";
    h.u.pendingCount = 0;
    h.frame(2);
    h.u.pendingDir.clear();
    h.c.prefs().dimPercent = 60;
    h.c.prefs().uiScale = 125;
    h.frame(2);
    h.u.dlg = ui::Dlg::None;
    h.frame(2);
    h.c.prefs().confirmDelete = true;
    const std::size_t n0 = h.c.session()->issues().size();
    h.c.select(h.c.session()->issues().back().id);
    h.key(ImGuiKey_Delete);
    check(h.u.dlg == ui::Dlg::ConfirmIssueDelete, "確かめる設定なら確認ダイアログ");
    check(h.c.session()->issues().size() == n0, "確認の前には消さない");
    h.frame(2);
    h.u.dlg = ui::Dlg::None;
    h.u.pendingDelete.clear();
    h.frame(2);
    h.c.prefs().confirmDelete = false;
    h.c.prefs().dimPercent = 30;
    h.c.prefs().uiScale = 100;
  }
  // 拡大縮小: ガントの時間軸の上の Ctrl+ホイールは時間軸、それ以外の Ctrl+ホイール・Ctrl+「+」「-」・Ctrl+0 は画面全体
  {
    h.c.setView(app::View::Gantt);
    h.c.setUi("zoom", "week");
    h.u.customPx = 0;
    h.frame(3);
    ImGuiIO& io = ImGui::GetIO();
    const auto wheel = [&](float x, float y, float dy, bool ctrl) {
      io.AddMousePosEvent(x, y);
      io.AddKeyEvent(ImGuiMod_Ctrl, ctrl);
      h.frame(1);
      io.AddMouseWheelEvent(0, dy);
      h.frame(1);
      io.AddKeyEvent(ImGuiMod_Ctrl, false);
      h.frame(2);
    };
    const int scale0 = h.c.prefs().uiScale;
    wheel(900, 300, 1, true);  // 時間軸の上 → 時間軸だけ変わる
    check(h.u.customPx > 6.f, "Ctrl+ホイールで時間軸が広がる");
    check(h.c.prefs().uiScale == scale0, "時間軸の上では画面全体は変わらない");
    {
      // マウスの下の日付が、拡大縮小の前後で動かない
      const float mx = 900;
      const auto dayAt = [&] { return (mx - (h.u.ganttLeft - h.u.ganttSx)) / h.u.ganttPx; };
      h.frame(2);
      const float before = dayAt();
      wheel(mx, 300, 2, true);
      const float after = dayAt();
      check(std::abs(after - before) < 0.6f, "拡大してもマウスの下の日付が動かない");
      wheel(mx, 300, -2, true);
      check(std::abs(dayAt() - before) < 0.6f, "縮小してもマウスの下の日付が動かない");
    }
    const float wide = h.u.customPx;
    wheel(900, 300, -3, true);
    check(h.u.customPx < wide, "Ctrl+ホイールで時間軸が狭まる");
    for (int i = 0; i < 40; ++i) wheel(900, 300, -1, true);
    check(h.u.customPx >= 0.9f - 1e-3f, "時間軸の下限");
    for (int i = 0; i < 60; ++i) wheel(900, 300, 1, true);
    check(h.u.customPx <= 40.f + 1e-3f, "時間軸の上限");
    wheel(900, 300, 1, false);  // Ctrl なしは今までどおりスクロール
    check(h.u.customPx <= 40.f + 1e-3f, "Ctrl なしでは時間軸は変わらない");
    h.c.setUi("zoom", "week");  // 段階のボタンに戻す
    h.u.customPx = 0;
    h.frame(2);
    wheel(100, 300, 1, true);  // 左の表の上 → 画面全体
    check(h.c.prefs().uiScale == 110, "表の上の Ctrl+ホイールで画面全体が大きくなる");
    h.clk.advanceMs(300);
    wheel(100, 300, -1, true);
    check(h.c.prefs().uiScale == 100, "Ctrl+ホイールで画面全体が小さくなる");
    h.clk.advanceMs(300);
    h.key(ImGuiKey_Equal, true);
    check(h.c.prefs().uiScale == 110, "Ctrl+「+」で大きくなる");
    h.key(ImGuiKey_Minus, true);
    h.key(ImGuiKey_Minus, true);
    check(h.c.prefs().uiScale == 90, "Ctrl+「-」で小さくなる");
    h.key(ImGuiKey_0, true);
    check(h.c.prefs().uiScale == 100, "Ctrl+0 で100%に戻る");
    check(!h.u.zoomHint.empty(), "倍率を表示する");
    h.frame(2);
    // 日・週・月・全体のボタンに戻すと、ホイールで変えた幅は捨てる
    h.u.customPx = 12.f;
    h.u.customProject = h.c.currentId();
    h.frame(2);
    h.c.setView(app::View::Board);
    h.frame(1);
    h.c.setView(app::View::Gantt);
    h.frame(2);
  }
  // 行にマウスを載せると出るごみ箱で削除し、元に戻す
  {
    h.c.setView(app::View::Gantt);
    h.frame(2);
    const std::size_t n0 = h.c.session()->issues().size();
    for (float y = 120; y < 260 && h.c.session()->issues().size() == n0; y += 4) h.click(253, y);
    check(h.c.session()->issues().size() < n0, "ごみ箱のボタンで削除");
    h.c.undo();
    h.frame(2);
    check(h.c.session()->issues().size() == n0, "ごみ箱で削除したものを元に戻す");
    h.c.setView(app::View::Board);
    for (float y = 120; y < 400; y += 40) {
      ImGui::GetIO().AddMousePosEvent(200, y);
      h.frame(2);
    }
  }
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
                           "core/src/commands.cpp", "core/src/outline.cpp", "core/src/csv.cpp", "core/src/engine.cpp", "core/src/store.cpp"};
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
