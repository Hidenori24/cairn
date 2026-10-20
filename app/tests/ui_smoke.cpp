// UI の煙テスト: ImGui をヘッドレスで動かし、各ビュー・ダイアログ・パレットを描いて落ちないこと・assert しないことを確かめる。
// Win32・DX11 は使わない（フォントのテクスチャを作るだけ）。
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <cstdlib>
#include <string>

#include <imgui.h>
#include <imgui_internal.h>

#include "app_controller.hpp"
#include "roadmap/gen.hpp"
#include "roadmap/link_candidates.hpp"
#include "roadmap/schedule.hpp"
#include "roadmap/text.hpp"
#include "ui/app_ui.hpp"
#include "ui/image_decode.hpp"
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
  int created = 0, released = 0;  // テクスチャ（本物は作らず、重ならない目印の値を返す）
  ImTextureID createTexture(const unsigned char* rgba, int w, int h) override {
    if (!rgba || w <= 0 || h <= 0) return nullptr;
    return reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(++created));
  }
  void releaseTexture(ImTextureID t) override {
    if (t) ++released;
  }
  std::vector<std::string> opened;  // openFile で開いたパス
  void openFile(const std::string& path) override { opened.push_back(path); }
  std::map<std::string, std::string> texts;  // ドロップされた CSV の中身（パス → 本文）
  std::optional<std::string> readTextFile(const std::string& path, std::string* error) override {
    const auto it = texts.find(path);
    if (it == texts.end()) {
      if (error) *error = "読めません";
      return std::nullopt;
    }
    return it->second;
  }
};

int failures = 0;
void check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "NG: %s\n", what);
    ++failures;
  }
}

// 24bit の BMP（下の行から）。画像のデコードの確認用
std::string makeBmp(int w, int h) {
  const int rowBytes = (w * 3 + 3) / 4 * 4;
  const std::uint32_t size = 54 + static_cast<std::uint32_t>(rowBytes * h);
  std::string s;
  const auto put32 = [&](std::uint32_t v) { for (int i = 0; i < 4; ++i) s.push_back(static_cast<char>((v >> (8 * i)) & 0xff)); };
  const auto put16 = [&](std::uint16_t v) { s.push_back(static_cast<char>(v & 0xff)); s.push_back(static_cast<char>(v >> 8)); };
  s += "BM";
  put32(size); put32(0); put32(54);
  put32(40); put32(static_cast<std::uint32_t>(w)); put32(static_cast<std::uint32_t>(h)); put16(1); put16(24);
  put32(0); put32(static_cast<std::uint32_t>(rowBytes * h)); put32(2835); put32(2835); put32(0); put32(0);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) { s.push_back(static_cast<char>(40)); s.push_back(static_cast<char>(x * 255 / std::max(1, w - 1))); s.push_back(static_cast<char>(200)); }
    for (int k = w * 3; k < rowBytes; ++k) s.push_back('\0');
  }
  return s;
}
const unsigned char kPng3x2[] = {0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x03,
                                 0x00, 0x00, 0x00, 0x02, 0x08, 0x02, 0x00, 0x00, 0x00, 0x12, 0x16, 0xf1, 0x4d, 0x00, 0x00, 0x00, 0x15, 0x49, 0x44, 0x41,
                                 0x54, 0x78, 0x9c, 0x63, 0xf8, 0xcf, 0xc0, 0xc0, 0x00, 0xc1, 0xff, 0x81, 0xd4, 0x7f, 0x20, 0xf1, 0x1f, 0x00, 0x4a, 0xc9,
                                 0x08, 0xf8, 0xee, 0x68, 0x40, 0xb6, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};

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
  {
    // 機能の確認用の汎用データ（golden/samples。画面には出さないテスト専用のサンプル）
    std::ifstream in(std::string(ROADMAP_SOURCE_DIR) + "/golden/samples/scenario_demo.csv", std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    check(h.c.importText(ss.str(), "サンプル", app::ImportMode::New).ok, "サンプルの取り込み");
  }
  for (auto v : {app::View::Gantt, app::View::Board, app::View::List, app::View::Goals, app::View::Stats}) {
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
  h.c.select("9");
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
    check(o && o->summary == "[A3] 新しい名前", "Enter で件名が確定する");
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
    // 新しい課題の名前（アプリの設定）: 既定の名前が入力欄に入り、そのまま Enter で確定して次の行も同じ流儀で付く
    h.c.prefs().newNameFormat = "parentSeq";
    h.c.select("7");
    io.AddKeyEvent(ImGuiKey_N, true);
    h.frame();
    io.AddKeyEvent(ImGuiKey_N, false);
    h.frame();
    const std::string n1 = h.u.editId;
    check(h.u.editField == ui::EditField::Name && h.u.editFresh && n1 != "7", "設定ありでも N で追加して編集");
    const std::string name1 = h.c.session()->issue(n1) ? h.c.session()->issue(n1)->summary : "";
    check(!name1.empty() && name1 != "新しい課題", "追加した行に設定どおりの名前が付く");
    check(h.u.editBuf == h.c.session()->tree()->name[h.c.session()->find(n1)], "入力欄に既定の名前が入る");
    h.key(ImGuiKey_Enter);  // 何も打たずに確定 → 次の行を追加
    const std::string n2 = h.u.editId;
    check(h.u.editField == ui::EditField::Name && n2 != n1, "追加直後の Enter で次の行（設定あり）");
    const std::string name2 = h.c.session()->issue(n2) ? h.c.session()->issue(n2)->summary : "";
    check(h.c.session()->issue(n1) && h.c.session()->issue(n1)->summary == name1, "そのまま確定した行の名前が残る");
    check(!name2.empty() && name2 != "新しい課題" && name2 != name1, "次の行にも名前が付き、兄弟と重ならない");
    h.key(ImGuiKey_Escape);
    // 後のガントの拡大の検査（頂点数が16ビットに収まる行数）に影響しないよう、増やした行は消す
    h.c.session()->deleteIssues({n1, n2});
    h.c.afterEdit();
    // 自由入力でアプリの設定を描く
    h.c.prefs().newNameFormat = "custom";
    h.c.prefs().newNameCustom = "{親}の子 {n}（{日付}）";
    h.c.select("7");
    h.u.dlg = ui::Dlg::AppSettings;
    h.u.dlgOpening = true;
    h.frame(3);
    h.u.dlg = ui::Dlg::None;
    h.frame(1);
    h.c.prefs().newNameFormat = "keep";
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
    check(h.c.session()->issue("8")->summary == "[B2] ガントで編集", "ガントで件名が確定する");
    h.key(ImGuiKey_Tab);  // 選択全体の階層を下げる → 元に戻す
    h.key(ImGuiKey_Z, true);
    h.key(ImGuiKey_Delete);  // 削除 → 元に戻す
    h.key(ImGuiKey_Z, true);
    check(h.c.session()->issue("8") != nullptr, "削除を元に戻せる");
  }
  // 複数選択（まとめ編集バー）
  h.c.setSelection({"6", "7", "8"}, "8");
  h.frame(3);
  // 選んだ課題を順につなぐ・外す（一覧・ガント・ボードのどこから選んでも。右クリックのメニューと選択バーを描く）
  {
    Session* ss = h.c.session();
    const auto na = ss->addIssue("", "", "鎖A");
    const auto nb = ss->addIssue("", na, "鎖B");
    const auto nc = ss->addIssue("", nb, "鎖C");
    h.c.afterEdit();
    for (auto v : {app::View::List, app::View::Gantt, app::View::Board}) {
      h.c.setView(v);
      h.c.setSelection({nc, na, nb}, nb);
      h.frame(2);
      ui::Frame fr{h.c, h.u, h.p};
      ui::openContext(fr, nb);
      h.frame(3);  // メニューとバーを描く
      h.click(1430, 880);  // メニューの外を押して閉じる
      check(!ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId), "メニューが閉じる");
      h.c.setSelection({nc, na, nb}, nb);
      const auto before = ss->undoSize();
      const auto r = h.c.chainSelection(h.u.order);
      check(r.ok && r.added == 2, "順につなぐ（画面の表示順）");
      check(ss->issue(na)->blocks == std::vector<std::string>{nb} && ss->issue(nb)->blocks == std::vector<std::string>{nc},
            "A→B→C のつなぎができる");
      h.frame(2);  // つないだ後の描画
      check(!h.c.chainSelection(h.u.order).added, "もう一度は重複しない");
      check(ss->undoSize() == before + 1, "Undo は1回分");
      h.key(ImGuiKey_Z, true);
      check(ss->issue(na)->blocks.empty() && ss->issue(nb)->blocks.empty(), "Undo 1回でつなぎが戻る");
      h.c.chainSelection(h.u.order);
      check(h.c.unchainSelection() == 2, "つなぎを外す");
      h.frame(2);
      h.c.undo();
      h.c.undo();
    }
    h.c.clearSelection();
    h.frame(1);
  }
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
  // 先行の列（Issue #9）: 設定でオン → 表に出る・セルを編集して先行を足す・誤りは確定しない・Undo 1回
  {
    check(!h.c.uiBool("cols", "pred", false), "先行の列は既定でオフ");
    h.c.setUiSub("cols", "pred", true);
    for (auto v : {app::View::Gantt, app::View::List}) {
      h.c.setView(v);
      h.frame(2);
    }
    h.u.openColFilter = "pred";
    h.frame(2);
    h.key(ImGuiKey_Escape);
    h.u.sortKey = "pred";
    h.u.sortDir = 1;
    h.frame(2);
    h.u.sortDir = -1;
    h.frame(2);
    h.u.sortDir = 0;
    h.u.sortKey.clear();
    h.c.setView(app::View::List);
    h.frame(2);
    Session* s = h.c.session();
    const Tree& T = *s->tree();
    // 先行にできる葉の組を探す
    std::string self, pred;
    for (int id : T.leafOrder) {
      const auto cand = linkCandidates(T, s->viewIssues(), id, LinkDir::AddPred, "", {}, 50);
      for (const auto& cd : cand)
        if (cd.block == LinkBlock::None && T.isLeaf(cd.idx)) {
          self = T.ids[id];
          pred = T.ids[cd.idx];
          break;
        }
      if (!self.empty()) break;
    }
    check(!self.empty(), "先行にできる組がある");
    const std::string predDid = T.did[T.find(pred)];
    const auto before = s->issues();
    ui::Frame fr{h.c, h.u, h.p};
    ImGuiIO& io = ImGui::GetIO();
    // 誤り（存在しない ID）は Enter でも確定せず、編集が続く。Esc で取り消すと何も変わらない
    ui::beginEdit(fr, self, ui::EditField::Pred);
    h.frame(2);
    io.AddInputCharactersUTF8("NOPE-999");
    h.frame(2);
    h.key(ImGuiKey_Enter);
    h.frame(2);
    check(h.u.editField == ui::EditField::Pred, "誤った先行の入力は Enter で確定しない");
    check(h.c.session()->issues() == before, "誤った先行の入力は何も変えない");
    h.key(ImGuiKey_Escape);
    check(h.u.editField == ui::EditField::None, "Esc で先行の編集をやめる");
    // 正しい ID は Enter で確定し、Undo 1回で戻る
    ui::beginEdit(fr, self, ui::EditField::Pred);
    h.frame(2);
    io.AddInputCharactersUTF8(predDid.c_str());
    h.frame(2);
    h.key(ImGuiKey_Enter);
    h.frame(2);
    check(h.u.editField == ui::EditField::None, "正しい先行の入力は Enter で確定する");
    const Issue* po = h.c.session()->issue(pred);
    check(po && std::find(po->blocks.begin(), po->blocks.end(), self) != po->blocks.end(), "先行が追加される");
    h.c.undo();
    h.frame(2);
    check(h.c.session()->issues() == before, "Undo 1回で先行の追加が戻る");
    // 候補（Tab）
    ui::beginEdit(fr, self, ui::EditField::Pred);
    h.frame(2);
    io.AddInputCharactersUTF8(predDid.substr(0, 1).c_str());
    h.frame(2);
    h.key(ImGuiKey_Tab);
    h.frame(2);
    check(h.u.editField == ui::EditField::Pred && !h.u.editBuf.empty(), "Tab で候補を入れても編集が続く");
    h.key(ImGuiKey_Escape);
    // ガントの表でも編集できる
    h.c.setView(app::View::Gantt);
    h.frame(2);
    ui::beginEdit(fr, self, ui::EditField::Pred);
    h.frame(3);
    h.key(ImGuiKey_Escape);
    h.c.setUiSub("cols", "pred", false);
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
    // 以前の平置きのプロジェクトがあれば、移すボタンを出す（自動では動かさない）→ 移すとボタンが消える
    h.fs.put("data/平置き.csv", "Summary\nA\n");
    h.c.syncFolder(false);
    check(h.c.legacyCount() == 1, "平置きのプロジェクトを読む");
    h.frame(2);
    h.u.dlg = ui::Dlg::Folder;
    h.u.dlgOpening = true;
    h.frame(2);
    h.u.dlg = ui::Dlg::AppSettings;
    h.u.dlgOpening = true;
    h.frame(2);
    check(h.c.migrateLegacy().ok(), "平置きをフォルダ構成へ移す");
    check(h.c.legacyCount() == 0 && h.fs.exists("data/平置き/project.csv"), "移したあとは平置きがない");
    h.frame(2);
    h.u.dlg = ui::Dlg::AppSettings;
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
  // 依存の相手の選択（Issue #6）: 画面でクリックして選ぶ・候補から選ぶ・循環は選べない・Undo 1回
  {
    h.c.setView(app::View::Gantt);
    h.frame(2);
    ui::Frame fr{h.c, h.u, h.p};
    Session* s = h.c.session();
    const Tree& T = *s->tree();
    // つなげる組（A→B）と、つなぐと循環する組を探す
    std::string idA, idB, idCycA, idCycB;
    for (int a : T.leafOrder) {
      for (int b : T.leafOrder) {
        if (idA.empty() && a != b && checkLink(T, s->viewIssues(), a, b) == LinkBlock::None) idA = T.ids[a], idB = T.ids[b];
      }
      for (int b : T.succs[a])
        if (idCycA.empty()) idCycA = T.ids[a], idCycB = T.ids[b];
    }
    check(!idA.empty() && !idCycA.empty(), "依存のテスト用の課題がある");
    const auto linked = [&](const std::string& from, const std::string& to) {
      const Issue* o = h.c.session()->issue(from);
      return o && std::find(o->blocks.begin(), o->blocks.end(), to) != o->blocks.end();
    };
    // 循環になる相手は選べない（理由を通知。課題は変わらない）
    const std::size_t undo0 = s->undoSize();
    const std::size_t toasts0 = h.c.toasts().size();
    check(!ui::linkIssues(fr, idCycB, true, idCycA, true), "循環になる相手はつなげない");
    check(h.c.toasts().size() == toasts0 + 1, "つなげない理由を通知する");
    check(s->undoSize() == undo0 && !linked(idCycB, idCycA), "つなげないときは何も変わらない");
    // 選択モード: 自分は選べず続く → Esc で中止
    ui::beginLinkPick(fr, idA, true);
    h.frame(2);  // バナー
    ui::rowClick(fr, idA);
    h.frame(2);
    check(h.u.linkPick.active, "自分を選んでも選択モードは続く");
    h.key(ImGuiKey_Escape);
    check(!h.u.linkPick.active, "Esc で選択モードを中止");
    // 選択モード: 相手の行をクリックして確定 → Undo 1回で戻る
    ui::beginLinkPick(fr, idA, true);
    h.frame(2);
    ui::rowClick(fr, idB);
    h.frame(2);
    check(!h.u.linkPick.active && linked(idA, idB), "行をクリックしてつながる");
    check(h.c.session()->undoSize() == undo0 + 1, "つなぐのは Undo 1回分");
    check(!h.u.recentLinks.empty() && h.u.recentLinks.front() == idB, "最近つないだ相手を覚える");
    h.c.undo();
    h.frame(2);
    check(!linked(idA, idB), "Undo 1回で戻る");
    // 実際のマウスクリック（ガントの行）でも確定または理由が出る
    ui::beginLinkPick(fr, h.c.session()->issues().back().id, false);
    h.frame(2);
    const std::size_t undo1 = h.c.session()->undoSize();
    const int toast1 = h.c.toasts().empty() ? 0 : h.c.toasts().back().id;
    // 行の位置はバナーの分だけずれるので、上から順にクリックして、確定するか理由が出るまで探す
    for (float y = 130; y < 330 && h.u.linkPick.active && (h.c.toasts().empty() ? 0 : h.c.toasts().back().id) == toast1; y += 6)
      h.click(200, y);
    check(h.c.session()->undoSize() == undo1 + 1 || (h.c.toasts().size() && h.c.toasts().back().id != toast1),
          "ガントの行のクリックで確定するか、理由を出して続ける");
    ui::cancelLinkPick(fr);
    // 右クリックメニュー
    ui::openContext(fr, idA);
    h.frame(3);
    h.key(ImGuiKey_Escape);
    // 詳細パネルの入力欄: 入力すると候補が出て、↑↓ と Enter で選べる
    h.c.select(idB);
    h.u.detailOpen = true;
    h.u.detailTab = 1;
    h.frame(3);
    ImGuiIO& io = ImGui::GetIO();
    bool focused = false;
    for (float y = 120; y < 700 && !focused; y += 6) {
      h.click(1150, y);
      focused = io.WantTextInput;
    }
    check(focused, "依存関係タブの入力欄に入れる");
    if (focused) {
      h.frame(3);  // 候補の一覧を描く（入力が空でも、同じ階層・最近の課題が出る）
      h.key(ImGuiKey_DownArrow);
      const std::size_t u2 = h.c.session()->undoSize();
      h.key(ImGuiKey_Enter);
      check(h.c.session()->undoSize() == u2 + 1, "↑↓ と Enter で候補から選ぶと Undo 1回分");
      h.c.undo();
    }
    h.u.detailOpen = false;
    h.frame(2);
  }
  // 依存のドラッグ（Issue #8）: ガントのバーの端のハンドルから別のバーへ
  {
    h.c.setView(app::View::Gantt);
    h.c.setUi("zoom", "week");
    h.u.detailOpen = false;
    h.c.clearSelection();
    h.u.customPx = 0;
    h.u.scrollToBase = true;  // 基準日の付近へ（前のテストで動かしたスクロールを戻す）
    h.frame(3);
    ImGuiIO& io = ImGui::GetIO();
    Session* s = h.c.session();
    const auto geom = [&](const std::string& id) -> const ui::UiState::BarGeom* {
      for (const auto& g : h.u.barGeom)
        if (g.id == id) return &g;
      return nullptr;
    };
    const auto linked = [&](const std::string& from, const std::string& to) {
      const Issue* o = h.c.session()->issue(from);
      return o && std::find(o->blocks.begin(), o->blocks.end(), to) != o->blocks.end();
    };
    const auto onScreen = [&](const ui::UiState::BarGeom& g) {
      return g.xl > h.u.ganttLeft + 30 && g.xr < 1380 && g.cy > 160 && g.cy < 600 && g.xr - g.xl > 12;
    };
    // 画面に見えていて、つなげる葉の組 A, B（B の方が下の行）を探す
    const Tree& T = *s->tree();
    std::string idA, idB;
    for (const auto& ga : h.u.barGeom)
      for (const auto& gb : h.u.barGeom) {
        const int a = T.find(ga.id), b = T.find(gb.id);
        if (idA.empty() && a >= 0 && b >= 0 && gb.cy > ga.cy && onScreen(ga) && onScreen(gb) && T.isLeaf(a) && T.isLeaf(b) &&
            T.kind[a] == Kind::Task && T.kind[b] == Kind::Task && checkLink(T, s->viewIssues(), a, b) == LinkBlock::None &&
            checkLink(T, s->viewIssues(), b, a) == LinkBlock::None)
          idA = ga.id, idB = gb.id;
      }
    check(!idA.empty() && geom(idA) && geom(idB), "ドラッグのテスト用のバーが見えている");
    if (!idA.empty() && geom(idA) && geom(idB)) {
      const float hd = h.u.m.s(10.f);  // バーの端からハンドルの中心まで
      const auto move = [&](float x, float y, int n = 2) {
        io.AddMousePosEvent(x, y);
        h.frame(n);
      };
      const auto down = [&] {
        io.AddMouseButtonEvent(0, true);
        h.frame(2);
      };
      const auto up = [&] {
        io.AddMouseButtonEvent(0, false);
        h.frame(3);  // 確定は描画が終わってから
      };
      const std::size_t undo0 = s->undoSize();
      const auto center = [&](const std::string& id, float& x, float& y) {
        const auto* g = geom(id);
        x = (g->xl + g->xr) / 2, y = g->cy;
      };
      float bx, by;
      center(idB, bx, by);
      // 右のハンドル → 別のバー: A が先行・B が後続になる。Undo 1回で戻る。行は選択されない
      move(geom(idA)->xr + hd, geom(idA)->cy);
      down();
      check(h.u.linkDrag.active && h.u.linkDrag.fromRight && h.u.linkDrag.id == idA, "右のハンドルを押すとドラッグが始まる");
      check(h.c.selection().empty(), "ハンドルを押しても行は選択されない");
      center(idB, bx, by);
      move(bx, by);
      check(h.u.linkDrag.active && !h.u.linkDrag.previewKey.empty() && h.u.linkDrag.preview.ok, "相手に載せると日程の影響を計算する");
      check(!linked(idA, idB), "離すまでは変わらない");
      up();
      check(!h.u.linkDrag.active && linked(idA, idB), "右のハンドルから別のバーへ離すと A → B がつながる");
      check(s->undoSize() == undo0 + 1, "つなぐのは Undo 1回分");
      h.c.undo();
      h.frame(2);
      check(!linked(idA, idB), "Undo 1回で戻る");
      // 左のハンドル → 別のバー: つかんだバー(B)が後続、相手(A)が先行になる
      center(idA, bx, by);
      move(geom(idB)->xl - hd, geom(idB)->cy);
      down();
      check(h.u.linkDrag.active && !h.u.linkDrag.fromRight && h.u.linkDrag.id == idB, "左のハンドルを押すとドラッグが始まる");
      move(bx, by);
      up();
      check(linked(idA, idB), "左のハンドルから別のバーへ離すと、つかんだ方が後続になる");
      // つなげない相手（循環・重複・自分自身）には作れない
      const std::size_t undo1 = s->undoSize();
      const auto tryDrag = [&](const std::string& from, bool right, const std::string& to) {
        const auto* g = geom(from);
        move(right ? g->xr + hd : g->xl - hd, g->cy);
        down();
        float tx, ty;
        center(to, tx, ty);
        move(tx, ty);
        const bool bad = h.u.linkDrag.active && !h.u.linkDrag.preview.ok;
        up();
        return bad;
      };
      check(s->undoSize() == undo1, "確認");
      tryDrag(idB, true, idA);  // B → A は循環
      check(!linked(idB, idA) && s->undoSize() == undo1, "循環になる相手にはつなげない");
      tryDrag(idA, true, idB);  // すでにつながっている
      check(s->undoSize() == undo1, "重複はつなげない");
      // 元の場所で離すと取り消し
      {
        const auto* g = geom(idA);
        move(g->xr + hd, g->cy);
        down();
        move(g->xr + hd + 60, g->cy + 2);
        move(g->xr + hd, g->cy);
        up();
        check(!h.u.linkDrag.active && s->undoSize() == undo1, "元の場所で離すと取り消し");
      }
      // Esc で取り消し（途中でマウスを離しても確定しない）
      h.c.undo();
      h.frame(2);
      check(!linked(idA, idB), "Undo で A → B を外す");
      {
        const std::size_t undo2 = s->undoSize();
        move(geom(idA)->xr + hd, geom(idA)->cy);
        down();
        center(idB, bx, by);
        move(bx, by);
        check(h.u.linkDrag.active, "Esc のテスト: ドラッグ中");
        h.key(ImGuiKey_Escape);
        check(!h.u.linkDrag.active, "Esc でドラッグを取り消す");
        up();
        check(!linked(idA, idB) && s->undoSize() == undo2, "Esc の後に離しても確定しない");
      }
      // バーの本体のドラッグは日程の移動（ハンドルと混ざらない）
      {
        const auto* g = geom(idA);
        const float mx = (g->xl + g->xr) / 2, my = g->cy;
        move(mx, my);
        down();
        move(mx + 40, my, 3);
        check(!h.u.linkDrag.active, "バーの本体を押してもハンドルのドラッグにならない");
        const bool moved = h.u.barDrag.active;
        up();
        if (moved) {
          h.c.undo();
          h.frame(2);
        }
      }
      // 選択モード（Issue #6）の間はハンドルを出さない・押してもドラッグにならない
      {
        ui::Frame fr{h.c, h.u, h.p};
        ui::beginLinkPick(fr, idB, true);
        h.frame(2);
        const auto* g = geom(idA);
        if (g) {
          move(g->xr + hd, g->cy);
          down();
          check(!h.u.linkDrag.active, "選択モードの間はハンドルのドラッグを始めない");
          up();
        }
        ui::cancelLinkPick(fr);
        h.frame(2);
      }
      // ドラッグ中にビューを切り替えても残らない
      move(geom(idA)->xr + hd, geom(idA)->cy);
      down();
      check(h.u.linkDrag.active, "ビュー切り替えのテスト: ドラッグ中");
      h.c.setView(app::View::List);
      h.frame(2);
      check(!h.u.linkDrag.active, "ガントを離れたらドラッグは終わる");
      up();
      h.c.setView(app::View::Gantt);
      h.frame(3);
    }
    io.AddMousePosEvent(900, 800);
    h.frame(2);
  }
  // 依存の矢印（Issue #10）: ホバー・つかんで付け替え・削除・たどる・クリティカル
  {
    h.c.setView(app::View::Gantt);
    h.c.setUi("zoom", "week");
    h.u.detailOpen = false;
    h.c.clearSelection();
    h.u.customPx = 0;
    h.u.scrollToBase = true;
    h.frame(3);
    ImGuiIO& io = ImGui::GetIO();
    Session* s = h.c.session();
    using Geom = ui::UiState::BarGeom;
    using AGeom = ui::UiState::ArrowGeom;
    const auto linked = [&](const std::string& from, const std::string& to) {
      const Issue* o = h.c.session()->issue(from);
      return o && std::find(o->blocks.begin(), o->blocks.end(), to) != o->blocks.end();
    };
    const auto onScreen = [&](ImVec2 p) { return p.x > h.u.ganttLeft + 24 && p.x < 1380 && p.y > 170 && p.y < 600; };
    const auto barOk = [&](const Geom& g) {
      const Tree& T = *s->tree();
      const int i = T.find(g.id);
      return i >= 0 && T.isLeaf(i) && g.xl > h.u.ganttLeft + 30 && g.xr < 1380 && g.cy > 175 && g.cy < 595 && g.xr - g.xl > 12;
    };
    const auto at = [&](const AGeom& a, float frac) {  // 折れ線の道のりの割合 frac の点
      float total = 0;
      for (std::size_t i = 0; i + 1 < a.pts.size(); ++i) total += std::hypot(a.pts[i + 1].x - a.pts[i].x, a.pts[i + 1].y - a.pts[i].y);
      float run = 0;
      for (std::size_t i = 0; i + 1 < a.pts.size(); ++i) {
        const float len = std::hypot(a.pts[i + 1].x - a.pts[i].x, a.pts[i + 1].y - a.pts[i].y);
        if (len > 0 && run + len >= total * frac) {
          const float t = (total * frac - run) / len;
          return ImVec2(a.pts[i].x + (a.pts[i + 1].x - a.pts[i].x) * t, a.pts[i].y + (a.pts[i + 1].y - a.pts[i].y) * t);
        }
        run += len;
      }
      return a.pts.back();
    };
    const auto move = [&](float x, float y, int n = 2) {
      io.AddMousePosEvent(x, y);
      h.frame(n);
    };
    // 矢印 a の道のりの割合 fracs のどれかでマウスを載せて、その矢印が「マウスの下」になる点を探す
    const auto hoverOn = [&](const AGeom& a, std::initializer_list<float> fracs) -> bool {
      for (float fr : fracs) {
        const ImVec2 p = at(a, fr);
        if (!onScreen(p)) continue;
        move(p.x, p.y);
        if (h.u.arrowHoverFrom == a.from && h.u.arrowHoverTo == a.to) return true;
      }
      return false;
    };
    const std::initializer_list<float> headFr = {0.8f, 0.75f, 0.7f, 0.85f, 0.65f, 0.9f, 0.6f};
    const std::initializer_list<float> tailFr = {0.2f, 0.25f, 0.3f, 0.15f, 0.35f, 0.1f, 0.4f};
    // 付け替えできる矢印（先行・後続とも画面に見える葉で、課題に書かれたつなぎ）
    std::vector<AGeom> cands;
    for (const auto& a : h.u.arrowGeom) {
      bool all = a.pts.size() >= 4 && linked(a.from, a.to);
      for (const auto& p : a.pts) all = all && onScreen(p);
      const Tree& T = *s->tree();
      all = all && T.find(a.from) >= 0 && T.find(a.to) >= 0 && T.isLeaf(T.find(a.from)) && T.isLeaf(T.find(a.to));
      if (all) cands.push_back(a);
    }
    check(!h.u.arrowGeom.empty(), "ガントに依存の矢印がある");
    check(!cands.empty(), "つかめる矢印（画面内の葉どうし）がある");
    // 矢印の上にマウスを載せたとき: 「マウスの下の矢印」になる
    AGeom pick;
    bool havePick = false;
    for (const auto& a : cands) {
      if (!hoverOn(a, headFr)) continue;
      pick = a;
      havePick = true;
      break;
    }
    check(havePick, "矢印にマウスを載せると、その矢印がマウスの下になる");
    if (havePick) {
      const auto pickTarget = [&](bool head) -> std::optional<Geom> {
        for (const auto& g : h.u.barGeom) {
          if (!barOk(g) || g.id == pick.from || g.id == pick.to) continue;
          const std::string np = head ? pick.from : g.id, ns = head ? g.id : pick.to;
          if (s->previewLink(np, ns, pick.from, pick.to).block == LinkBlock::None) return g;
        }
        return std::nullopt;
      };
      const auto grab = [&](bool head) -> bool {  // 矢印の近い方の端をつかむ（押す）
        if (!hoverOn(pick, head ? headFr : tailFr)) return false;
        io.AddMouseButtonEvent(0, true);
        h.frame(2);
        return h.u.arrowDrag.pending || h.u.arrowDrag.active;
      };
      const auto release = [&] {
        io.AddMouseButtonEvent(0, false);
        h.frame(3);  // 確定は描画が終わってから
      };
      const std::size_t undo0 = s->undoSize();
      // 動かさずに離す（クリック）: 何も変わらず、行も選ばれない
      check(grab(true), "矢印の上で押せる");
      check(h.u.arrowDrag.editable, "画面の葉どうしの矢印はつかんで付け替えできる");
      check(h.c.selection().empty(), "矢印を押しても行は選択されない");
      release();
      check(!h.u.arrowDrag.pending && !h.u.arrowDrag.active && s->undoSize() == undo0 && linked(pick.from, pick.to),
            "動かさずに離すと何も変えない");
      // 後続の端をつかんで別のバーへ: 付け替え（Undo 1回）
      if (auto t = pickTarget(true); t) {
        check(grab(true), "後続側の端をつかめる");
        move((t->xl + t->xr) / 2, t->cy);
        check(h.u.arrowDrag.active && h.u.arrowDrag.grabHead, "動かすとドラッグになる（後続側の端）");
        check(h.u.arrowDrag.preview.ok, "付け替え先の日程の影響を計算する");
        check(linked(pick.from, pick.to), "離すまでは変わらない");
        release();
        check(!linked(pick.from, pick.to) && linked(pick.from, t->id), "後続の端を別のバーへ離すと付け替わる");
        check(s->undoSize() == undo0 + 1, "付け替えは Undo 1回分");
        h.c.undo();
        h.frame(2);
        check(linked(pick.from, pick.to) && !linked(pick.from, t->id), "Undo 1回で戻る");
      } else {
        check(false, "後続の付け替え先のバーがある");
      }
      // 先行の根元をつかんで別のバーへ
      if (auto t = pickTarget(false); t) {
        h.frame(2);
        if (grab(false)) {
          move((t->xl + t->xr) / 2, t->cy);
          check(h.u.arrowDrag.active && !h.u.arrowDrag.grabHead, "根元に近い所は先行側の端をつかむ");
          release();
          check(!linked(pick.from, pick.to) && linked(t->id, pick.to), "先行の端を別のバーへ離すと付け替わる");
          h.c.undo();
          h.frame(2);
          check(linked(pick.from, pick.to), "先行の付け替えも Undo 1回で戻る");
        } else {
          check(false, "先行側の端をつかめる");
        }
      }
      // バーのない場所へ離すと削除（Undo 1回）
      {
        float fx = h.u.ganttLeft + 4;
        const float fy = h.u.barGeom.empty() ? 300.f : h.u.barGeom.front().cy;
        const auto freeAt = [&](float x, float y) {
          for (const auto& g : h.u.barGeom)
            if (std::abs(g.cy - y) < 14 && x > g.xl - 24 && x < g.xr + 24) return false;
          return true;
        };
        for (float x = h.u.ganttLeft + 4; x < 1300; x += 9)
          if (freeAt(x, fy)) {
            fx = x;
            break;
          }
        check(grab(true), "削除のテスト: 矢印をつかめる");
        move(fx, fy);
        check(h.u.arrowDrag.active, "削除のテスト: ドラッグ中");
        check(linked(pick.from, pick.to), "離すまでは外れない");
        release();
        check(!linked(pick.from, pick.to), "バーのない場所へ離すと依存が外れる");
        check(s->undoSize() == undo0 + 1, "削除は Undo 1回分");
        h.c.undo();
        h.frame(2);
        check(linked(pick.from, pick.to) && s->undoSize() == undo0, "Undo 1回で戻る");
      }
      // 取り消し: タイムラインの外で離す・Esc・元の相手の上で離す
      {
        check(grab(true), "取り消しのテスト: 矢印をつかめる");
        move(60, 300);  // 左の表の上（タイムラインの外）
        release();
        check(linked(pick.from, pick.to) && s->undoSize() == undo0, "タイムラインの外で離すと取り消し（外さない）");
        check(grab(true), "Esc のテスト: 矢印をつかめる");
        move(h.u.ganttLeft + 6, 300);
        check(h.u.arrowDrag.active, "Esc のテスト: ドラッグ中");
        h.key(ImGuiKey_Escape);
        check(!h.u.arrowDrag.active && !h.u.arrowDrag.pending, "Esc で取り消す");
        release();
        check(linked(pick.from, pick.to) && s->undoSize() == undo0, "Esc の後に離しても外れない");
        const Geom* orig = nullptr;
        for (const auto& g : h.u.barGeom)
          if (g.id == pick.to) orig = &g;
        if (orig && barOk(*orig)) {
          check(grab(true), "元の相手のテスト: 矢印をつかめる");
          move((orig->xl + orig->xr) / 2, orig->cy);
          release();
          check(linked(pick.from, pick.to) && s->undoSize() == undo0, "元の相手の上で離すと変えない");
        }
      }
      // つなげない相手（先行元自身）には付け替えない
      {
        const Geom* self = nullptr;
        for (const auto& g : h.u.barGeom)
          if (g.id == pick.from) self = &g;
        if (self && barOk(*self)) {
          check(grab(true), "つなげない相手のテスト: 矢印をつかめる");
          move((self->xl + self->xr) / 2, self->cy);
          check(h.u.arrowDrag.active && !h.u.arrowDrag.preview.ok, "自分自身へはつなげない（プレビューなし）");
          release();
          check(linked(pick.from, pick.to) && s->undoSize() == undo0, "つなげない相手に離しても変わらない");
        }
      }
    }
    // 依存をたどる・クリティカルの強調: 描いて落ちない・設定が保存される
    {
      const Tree& T = *s->tree();
      std::string withLinks;
      for (int i = 0; i < T.n; ++i)
        if (T.isLeaf(i) && (!T.preds[i].empty() || !T.succs[i].empty())) {
          withLinks = T.ids[i];
          break;
        }
      check(!withLinks.empty(), "依存のある課題がある");
      if (!withLinks.empty()) h.c.select(withLinks);
      h.c.setUiSub("dep", "trace", true);
      h.frame(3);
      check(h.c.uiBool("dep", "trace", false), "たどる表示をオンにできる");
      h.c.setUiSub("dep", "crit", true);
      h.frame(3);
      check(h.c.uiBool("dep", "crit", false), "クリティカルの強調をオンにできる");
      for (const char* z : {"day", "month", "all", "week"}) {
        h.c.setUi("zoom", z);
        h.frame(2);
      }
      h.c.clearSelection();
      h.frame(2);  // 選んだ課題がなければたどらない
      h.c.setUiSub("dep", "trace", false);
      h.c.setUiSub("dep", "crit", false);
      h.frame(2);
      check(!h.c.uiBool("dep", "trace", true) && !h.c.uiBool("dep", "crit", true), "オフに戻せる");
      h.c.setUi("zoom", "week");
      h.frame(2);
    }
    io.AddMousePosEvent(900, 800);
    h.frame(2);
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
  for (auto v : {app::View::Gantt, app::View::Board, app::View::List, app::View::Goals, app::View::Stats}) {
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
  // 添付: ドロップ先が課題（行・詳細パネル）なら添付、何もないところなら CSV の取り込み
  {
    h.c.createProject(app::SampleKind::Hierarchy, "添付の確認", "ATT");
    h.c.setView(app::View::List);
    h.u.detailOpen = false;
    h.c.clearSelection();
    h.frame(3);
    check(!h.u.dropTargets.empty(), "行がドロップ先として登録される");
    h.fs.put("ext/メモ.txt", "hello");
    h.fs.put("ext/run.exe", "MZ");
    h.fs.put("ext/dir/a.png", "x");
    h.fs.put("ext/dir/b.png", "y");
    const ui::UiState::DropTarget t = h.u.dropTargets.front();
    const ImVec2 mid((t.a.x + t.b.x) / 2, (t.a.y + t.b.y) / 2);
    h.u.pendingDrop.active = true;
    h.u.pendingDrop.paths = {"ext/メモ.txt", "ext/run.exe", "ext/dir"};
    h.u.pendingDrop.pos = mid;
    h.frame();
    check(!h.u.pendingDrop.active, "ドロップは1回で処理される");
    check(h.c.attachmentCount(t.id) == 4, "複数ファイルとフォルダが行へのドロップで付く");
    check(h.u.dlg == ui::Dlg::None, "課題へのドロップでは取り込みを開かない");
    h.frame(2);
    // 詳細パネルの添付タブ（一覧・開く・削除）
    h.c.select(t.id);
    h.u.detailOpen = true;
    h.u.detailTab = 3;
    h.frame(3);
    check(h.u.dropTargets.size() > 1, "詳細パネルもドロップ先");
    {
      ui::Frame f{h.c, h.u, h.p};
      ui::openAttachment(f, t.id, 0);  // メモ.txt
      check(h.p.opened.size() == 1, "ふつうのファイルはそのまま開く");
      ui::openAttachment(f, t.id, 1);  // run.exe
      check(h.p.opened.size() == 1 && h.u.dlg == ui::Dlg::AttachOpenWarn, "実行ファイルは開く前に警告する");
    }
    h.frame(3);
    h.key(ImGuiKey_Escape);
    check(h.u.dlg == ui::Dlg::None && h.p.opened.size() == 1, "警告をやめると開かない");
    // 詳細パネルの上へのドロップ → 開いている課題へ（Esc で選択が外れたので選び直す）
    h.c.select(t.id);
    h.frame(2);
    h.fs.put("ext/図.png", "z");
    h.u.pendingDrop.active = true;
    h.u.pendingDrop.paths = {"ext/図.png"};
    h.u.pendingDrop.pos = ImVec2(1400, 600);
    h.frame();
    check(h.c.attachmentCount(t.id) == 5, "詳細パネルへのドロップで開いている課題に付く");
    h.frame(2);
    // 何もないところ（上のバー）へのドロップ → 今まで通り CSV の取り込み
    h.p.texts["ext/in.csv"] = "Issue Id,Summary\n1,a\n";
    h.u.pendingDrop.active = true;
    h.u.pendingDrop.paths = {"ext/in.csv"};
    h.u.pendingDrop.pos = ImVec2(3, 3);
    const std::size_t before = h.c.attachmentCount(t.id);
    h.frame();
    check(h.u.dlg == ui::Dlg::Import && h.u.importName == "in", "何もないところへのドロップは CSV の取り込み");
    check(h.c.attachmentCount(t.id) == before, "取り込みのときは添付しない");
    h.frame(2);
    h.key(ImGuiKey_Escape);
    h.frame(2);
    // 削除（_削除済み へ）
    h.c.removeAttachment(t.id, 0);
    h.frame(2);
    check(h.c.attachmentCount(t.id) == before - 1, "削除で1つ減る");
    // ボード・ガントでもバッジとドロップ先を描く
    for (auto v : {app::View::Board, app::View::Gantt}) {
      h.c.setView(v);
      h.frame(12);  // 5000 件のガントで下までスクロールした位置が戻るのを待つ
      check(!h.u.dropTargets.empty(), "ボード・ガントにもドロップ先がある");
    }
    // 平置きのプロジェクトでは添付できない旨を出し、ドロップしても付けずに落ちない
    h.fs.put("data/平置き.csv", "Issue Id,Summary\n1,a\n");
    h.c.syncFolder(true);
    for (const auto& pi : h.c.projects())
      if (pi.name == "平置き") h.c.openProject(pi.id);
    h.c.setView(app::View::List);
    h.c.select("1");
    h.u.detailOpen = true;
    h.u.detailTab = 3;
    h.frame(3);
    check(!h.c.canAttach(), "平置きは添付できない");
    h.u.pendingDrop.active = true;
    h.u.pendingDrop.paths = {"ext/図.png"};
    h.u.pendingDrop.pos = ImVec2(1400, 600);
    h.frame(2);
    check(h.c.attachmentCount("1") == 0, "平置きへのドロップでは付かない");
    h.u.detailOpen = false;
    h.frame(2);
  }
  // 画像の添付: デコード（壊れたファイルでも落ちない）・サムネイル・プレビュー
  {
    const std::string png(reinterpret_cast<const char*>(kPng3x2), sizeof kPng3x2);
    const auto d = ui::decodeImage(png, 160);
    check(d && d->w == 3 && d->h == 2 && d->rgba.size() == 24, "PNG をデコードできる");
    check(d && d->rgba[0] == 255 && d->rgba[1] == 0 && d->rgba[2] == 0 && d->rgba[3] == 255, "PNG の先頭の画素");
    const auto big = ui::decodeImage(makeBmp(300, 100), 100);
    check(big && big->w == 100 && big->h == 33 && big->rgba.size() == 100u * 33u * 4u, "長辺が上限を超える画像は縮小する");
    const auto same = ui::decodeImage(makeBmp(30, 10), 100);
    check(same && same->w == 30 && same->h == 10, "上限以内の画像は縮小しない");
    check(!ui::decodeImage("", 160), "空のデータ");
    check(!ui::decodeImage("これは画像ではありません", 160), "画像でないデータ");
    check(!ui::decodeImage(png.substr(0, 20), 160), "途中で切れた PNG");
    std::string broken = png;
    broken[40] = static_cast<char>(broken[40] ^ 0x55);
    (void)ui::decodeImage(broken, 160);  // 壊れた中身でも落ちない（結果は問わない）
    std::string noise(4096, '\0');
    for (std::size_t k = 0; k < noise.size(); ++k) noise[k] = static_cast<char>((k * 131 + 7) & 0xff);
    noise.replace(0, 8, png.substr(0, 8));  // PNG の署名だけ正しい
    check(!ui::decodeImage(noise, 160), "署名だけ正しいでたらめなデータ");

    h.c.createProject(app::SampleKind::Hierarchy, "画像の確認", "IMG");
    h.c.setView(app::View::List);
    h.c.select("11");
    h.fs.put("ext/a.png", png);
    h.fs.put("ext/b.bmp", makeBmp(64, 48));
    h.fs.put("ext/bad.png", "壊れたファイル");
    h.fs.put("ext/note.txt", "memo");
    h.c.attachFiles("11", {"ext/a.png", "ext/b.bmp", "ext/bad.png", "ext/note.txt"});
    h.u.detailOpen = true;
    h.u.detailTab = 3;
    h.frame(8);
    check(h.p.created >= 2, "画像のサムネイルをテクスチャにする（壊れたファイルは作らない）");
    const int made = h.p.created;
    h.frame(5);
    check(h.p.created == made, "サムネイルは作り直さない（キャッシュ）");
    {
      ui::Frame f{h.c, h.u, h.p};
      ui::openImagePreview(f, "11", 0);  // a.png
      check(h.u.dlg == ui::Dlg::AttachPreview, "画像をクリックするとプレビュー");
      h.frame(4);
      ui::openImagePreview(f, "11", 2);  // 壊れた bad.png: 理由を出して落ちない
      h.frame(4);
      ui::openImagePreview(f, "11", 3);  // 画像でないものはプレビューしない
      check(h.u.dlg != ui::Dlg::AttachPreview || h.u.previewIndex != 3, "画像でないものはプレビューしない");
    }
    h.u.dlg = ui::Dlg::None;
    h.frame(2);
    // 添付を外す・プロジェクトを切り替えたとき、使わないテクスチャを解放する
    h.c.removeAttachment("11", 0);
    h.frame(3);
    h.c.createProject(app::SampleKind::Empty, "別", "");
    h.frame(3);
    check(h.p.released > 0, "使わなくなったテクスチャを解放する");
    h.c.deleteCurrentProject();
    h.frame(2);
    h.u.detailOpen = false;
    h.frame(2);
  }
  // JIRA の Attachment 列: 添付タブにメタ情報として出し、書き出しでも戻す
  {
    const std::string csvIn = "Issue Id,Summary,Attachment\n1,課題,20/Oct/26 9:30 AM;山田;図.png;https://example.invalid/a.png\n";
    check(h.c.importText(csvIn, "JIRA添付", app::ImportMode::New).ok, "Attachment 列つき CSV の取り込み");
    h.c.setView(app::View::List);
    h.c.select("1");
    h.u.detailOpen = true;
    h.u.detailTab = 3;
    h.frame(3);
    check(h.c.session()->exportText(ExportKind::Jira).find("図.png") != std::string::npos, "Attachment 列は書き出しにも残る");
    h.u.detailOpen = false;
    h.frame(2);
  }
  // 空のプロジェクト
  h.c.createProject(app::SampleKind::Empty, "空", "");
  h.frame(3);
  h.c.deleteCurrentProject();
  h.frame(2);
  // 画面の文言の文字が ui_glyphs.inc にすべて入っていること（文言を変えたら tools/gen_ui_glyphs.py を実行）
  {
    std::set<ImWchar> have(ui::uiGlyphs().begin(), ui::uiGlyphs().end());
    const char* files[] = {"app/ui/app_ui.cpp", "app/ui/view_list.cpp", "app/ui/view_gantt.cpp", "app/ui/view_board.cpp",
                           "app/ui/view_goals.cpp", "app/ui/panel_detail.cpp", "app/ui/panel_attach.cpp", "app/ui/palette.cpp", "app/ui/dialogs.cpp",
                           "app/ui/widgets.cpp", "app/ui/strings.hpp", "app/controller/app_controller.cpp",
                           "core/src/commands.cpp", "core/src/outline.cpp", "core/src/csv.cpp", "core/src/engine.cpp", "core/src/store.cpp", "core/src/attachments.cpp"};
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
