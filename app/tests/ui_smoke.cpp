// UI の煙テスト: ImGui をヘッドレスで動かし、各ビュー・ダイアログ・パレットを描いて落ちないこと・assert しないことを確かめる。
// Win32・DX11 は使わない（フォントのテクスチャを作るだけ）。
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <cstdlib>
#include <string>
#include <thread>

#include <imgui.h>
#include <imgui_internal.h>

#include "app_controller.hpp"
#include "roadmap/attachments.hpp"
#include "roadmap/gen.hpp"
#include "roadmap/github_ref.hpp"
#include "roadmap/link_candidates.hpp"
#include "roadmap/schedule.hpp"
#include "roadmap/today.hpp"
#include "roadmap/text.hpp"
#include "ui/app_ui.hpp"
#include "ui/icons.hpp"
#include "ui/image_decode.hpp"
#include "ui/internal.hpp"

using namespace roadmap;

namespace {
struct NullPlatform : ui::IPlatform {
  std::optional<std::pair<std::string, std::string>> openTextFile() override { return std::nullopt; }
  bool saveTextFile(const std::string&, const std::string&) override { return true; }
  void openFolder(const std::string& p) override { openedFolders.push_back(p); }
  std::vector<std::string> openedFolders;  // openFolder で開いたフォルダ
  // 保存ダイアログの代わり（テストが保存先を差し込む。pickedSave が空ならやめたことになる）
  std::optional<std::string> pickedSave;
  std::vector<std::string> saveAsked;  // 保存ダイアログに渡した既定の名前
  std::optional<std::string> pickSaveFile(const std::string& name, const std::string& ext, const std::string&) override {
    saveAsked.push_back(name + "|" + ext);
    return pickedSave;
  }
  std::optional<std::string> pickFolder(const std::string&) override { return picked; }
  std::optional<std::string> picked;
  bool systemDark() override { return false; }
  void requestQuit() override {}
  std::optional<std::string> clip;  // クリップボードの代わり（テストが差し替える）
  std::optional<std::string> clipboardText() override { return clip; }
  // クリップボードへの書き込みの代わり（週次レポートの「Markdown をコピー」。clipFail で失敗を差し込める）
  std::vector<std::string> clipSet;
  bool clipFail = false;
  bool setClipboardText(const std::string& text) override {
    if (clipFail) return false;
    clipSet.push_back(text);
    clip = text;
    return true;
  }
  int created = 0, released = 0;  // テクスチャ（本物は作らず、重ならない目印の値を返す）
  ImTextureID createTexture(const unsigned char* rgba, int w, int h) override {
    if (!rgba || w <= 0 || h <= 0) return nullptr;
    return reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(++created));
  }
  void releaseTexture(ImTextureID t) override {
    if (t) ++released;
  }
  // 「ファイルを選んで追加」のダイアログの代わり（pickedFiles を返す。聞かれた回数は pickAsked）
  std::vector<std::string> pickedFiles;
  int pickAsked = 0;
  std::vector<std::string> pickOpenFiles(const std::string&) override {
    ++pickAsked;
    return pickedFiles;
  }
  std::optional<std::string> clipImage;  // クリップボードの画像（BMP ファイルのバイト列）の代わり
  std::optional<std::string> clipboardImage() override { return clipImage; }
  std::vector<std::string> opened;  // openFile で開いたパス
  void openFile(const std::string& path) override { opened.push_back(path); }
  // 関連リンクを開く代わり（渡された場所を記録する。linkOpenWhy が空でなければ、その理由で失敗する）
  std::vector<std::string> openedLinks;
  std::string linkOpenWhy;
  bool openLink(const std::string& target, std::string* error) override {
    if (!linkOpenWhy.empty()) {
      if (error) *error = linkOpenWhy;
      return false;
    }
    openedLinks.push_back(target);
    return true;
  }
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
  for (auto v : {app::View::Gantt, app::View::Board, app::View::List, app::View::Goals, app::View::Stats, app::View::Today}) {
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
  // 依存のドラッグ（Issue #8）: 一覧の行・ボードのカードの左右のハンドルから別の行・カードへ（ガントと同じ操作）
  for (const app::View view : {app::View::List, app::View::Board}) {
    const bool isList = view == app::View::List;
    const std::string nm = isList ? "一覧: " : "ボード: ";
    const auto ck = [&](bool ok, const char* what) { check(ok, (nm + what).c_str()); };
    h.c.setView(view);
    h.u.detailOpen = false;
    h.c.clearSelection();
    h.u.sortKey.clear();
    h.u.sortDir = 0;
    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(900, 880);
    h.frame(3);
    Session* s = h.c.session();
    const auto tree = [&]() -> const Tree& { return *h.c.session()->tree(); };
    using Slot = ui::UiState::LinkSlot;
    const auto slotOf = [&](const std::string& id) -> const Slot* {
      const int i = tree().find(id);
      for (const auto& sl : h.u.linkSlots)
        if (sl.idx == i) return &sl;
      return nullptr;
    };
    const auto linked = [&](const std::string& from, const std::string& to) {
      const Issue* o = h.c.session()->issue(from);
      return o && std::find(o->blocks.begin(), o->blocks.end(), to) != o->blocks.end();
    };
    const auto visible = [&](const Slot& sl) { return sl.a.y > 150 && sl.b.y < 700 && sl.a.x > 10 && sl.b.x < 1420; };
    // 画面に見えていて、どちら向きにもつなげる葉の組 A, B（B の方が下）を探す
    std::string idA, idB;
    for (const auto& sa : h.u.linkSlots)
      for (const auto& sb : h.u.linkSlots) {
        const Tree& T = tree();
        const int a = sa.idx, b = sb.idx;
        if (idA.empty() && a != b && a >= 0 && b >= 0 && sb.a.y > sa.a.y && visible(sa) && visible(sb) && T.isLeaf(a) && T.isLeaf(b) &&
            T.kind[a] == Kind::Task && T.kind[b] == Kind::Task && checkLink(T, s->viewIssues(), a, b) == LinkBlock::None &&
            checkLink(T, s->viewIssues(), b, a) == LinkBlock::None)
          idA = T.ids[a], idB = T.ids[b];
      }
    ck(!idA.empty() && slotOf(idA) && slotOf(idB), "ドラッグのテスト用の行・カードが見えている");
    if (idA.empty() || !slotOf(idA) || !slotOf(idB)) continue;
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
    const auto center = [&](const std::string& id, float& x, float& y) {
      const Slot* sl = slotOf(id);
      x = (sl->a.x + sl->b.x) / 2, y = (sl->a.y + sl->b.y) / 2;
    };
    const std::size_t undo0 = s->undoSize();
    float bx, by;
    ck(h.u.linkSlots.size() >= 2, "行・カードの位置を登録している");
    // 右のハンドル → 別の行・カード: A が先行・B が後続。Undo 1回で戻る。行・カードは選択されない
    move(slotOf(idA)->hr.x, slotOf(idA)->hr.y);
    down();
    ck(h.u.linkDrag.active && h.u.linkDrag.fromRight && h.u.linkDrag.id == idA && h.u.linkDrag.view == view,
       "右のハンドルを押すとドラッグが始まる");
    ck(h.c.selection().empty(), "ハンドルを押しても選択されない");
    center(idB, bx, by);
    move(bx, by);
    ck(h.u.linkDrag.active && !h.u.linkDrag.previewKey.empty() && h.u.linkDrag.preview.ok, "相手に載せると日程の影響を計算する");
    ck(!linked(idA, idB), "離すまでは変わらない");
    up();
    ck(!h.u.linkDrag.active && linked(idA, idB), "右のハンドルから別の行・カードへ離すと A → B がつながる");
    ck(s->undoSize() == undo0 + 1, "つなぐのは Undo 1回分");
    ck(h.c.selection().empty(), "つないでも選択は変わらない");
    h.c.undo();
    h.frame(2);
    ck(!linked(idA, idB), "Undo 1回で戻る");
    // 左のハンドル → 別の行・カード: つかんだ B が後続、相手 A が先行になる
    move(slotOf(idB)->hl.x, slotOf(idB)->hl.y);
    down();
    ck(h.u.linkDrag.active && !h.u.linkDrag.fromRight && h.u.linkDrag.id == idB, "左のハンドルを押すとドラッグが始まる");
    center(idA, bx, by);
    move(bx, by);
    up();
    ck(linked(idA, idB), "左のハンドルから別の行・カードへ離すと、つかんだ方が後続になる");
    // つなげない相手（循環・重複）にはつなげない。理由を通知する
    const std::size_t undo1 = s->undoSize();
    const auto tryDrag = [&](const std::string& from, bool right, const std::string& to) {
      const Slot* g = slotOf(from);
      move(right ? g->hr.x : g->hl.x, right ? g->hr.y : g->hl.y);
      down();
      float tx, ty;
      center(to, tx, ty);
      move(tx, ty);
      const bool active = h.u.linkDrag.active;
      up();
      return active;
    };
    const auto lastToast = [&] { return h.c.toasts().empty() ? 0 : h.c.toasts().back().id; };
    const int toast1 = lastToast();
    ck(tryDrag(idB, true, idA), "循環になる相手にもドラッグはできる（赤で示す）");
    ck(!linked(idB, idA) && s->undoSize() == undo1, "循環になる相手にはつなげない");
    ck(lastToast() != toast1, "つなげない理由を通知する");
    tryDrag(idA, true, idB);  // すでにつながっている
    ck(s->undoSize() == undo1, "重複はつなげない");
    // 元の場所で離すと取り消し
    {
      const Slot* g = slotOf(idA);
      move(g->hr.x, g->hr.y);
      down();
      move(g->hr.x + 40, g->hr.y + 2);
      move(g->hr.x, g->hr.y);
      up();
      ck(!h.u.linkDrag.active && s->undoSize() == undo1, "元の場所で離すと取り消し");
      ck(h.c.selection().empty(), "取り消しても選択されない");
    }
    // 自分自身の行・カードの上で離しても取り消し
    {
      const Slot* g = slotOf(idA);
      move(g->hr.x, g->hr.y);
      down();
      center(idA, bx, by);
      move(bx, by);
      up();
      ck(s->undoSize() == undo1, "自分自身の上で離すと取り消し");
    }
    // 何もないところで離すと取り消し
    {
      const Slot* g = slotOf(idA);
      move(g->hr.x, g->hr.y);
      down();
      move(700, 880);
      up();
      ck(s->undoSize() == undo1 && !h.u.linkDrag.active, "何もないところで離しても課題は変わらない（新しい課題のメニューが出るだけ）");
      if (h.u.linkMenu.active) h.key(ImGuiKey_Escape);  // 閉じる（メニューの操作は次のテスト）
      ck(!h.u.linkMenu.active, "メニューは Esc で閉じる");
    }
    // Esc で取り消し（途中でマウスを離しても確定しない）
    h.c.undo();
    h.frame(2);
    ck(!linked(idA, idB), "Undo で A → B を外す");
    {
      const std::size_t undo2 = s->undoSize();
      move(slotOf(idA)->hr.x, slotOf(idA)->hr.y);
      down();
      center(idB, bx, by);
      move(bx, by);
      ck(h.u.linkDrag.active, "Esc のテスト: ドラッグ中");
      h.key(ImGuiKey_Escape);
      ck(!h.u.linkDrag.active, "Esc でドラッグを取り消す");
      up();
      ck(!linked(idA, idB) && s->undoSize() == undo2, "Esc の後に離しても確定しない");
    }
    // 行・カードの本体のクリックは選択（ハンドルと混ざらない）。本体のドラッグはハンドルのドラッグにならない
    {
      center(idA, bx, by);
      if (isList) bx = slotOf(idA)->a.x + 150;  // 件名の列（状況の列などは別の操作になる）
      move(bx, by);
      down();
      up();
      ck(!h.u.linkDrag.active && h.c.isSelected(idA), "本体をクリックすると選択される");
      move(bx, by);
      down();
      move(bx + 30, by + 2, 3);
      ck(!h.u.linkDrag.active, "本体を押してもハンドルのドラッグにならない");
      up();
      h.c.clearSelection();
      h.frame(2);
    }
    if (!isList) {
      // ボードのカードの移動（状況の列の変更）はハンドルから始まらない: 別の列の空いたところへ離しても状況は変わらない
      const std::string st0 = h.c.session()->issue(idA)->status;
      const std::size_t undo3 = s->undoSize();
      move(slotOf(idA)->hr.x, slotOf(idA)->hr.y);
      down();
      move(slotOf(idA)->hr.x + 300, 690, 3);
      up();
      ck(h.c.session()->issue(idA)->status == st0 && s->undoSize() == undo3, "ハンドルのドラッグで状況の列は変わらない");
    }
    // 選択モード（Issue #6）の間はハンドルを出さない・押してもドラッグにならない
    {
      ui::Frame fr{h.c, h.u, h.p};
      ui::beginLinkPick(fr, idB, true);
      h.frame(2);
      if (slotOf(idA)) {
        move(slotOf(idA)->hr.x, slotOf(idA)->hr.y);
        down();
        ck(!h.u.linkDrag.active, "選択モードの間はハンドルのドラッグを始めない");
        up();
      }
      ui::cancelLinkPick(fr);
      h.frame(2);
    }
    // ドラッグ中にビューを切り替えても残らない
    const std::size_t undo4 = s->undoSize();
    move(slotOf(idA)->hr.x, slotOf(idA)->hr.y);
    down();
    ck(h.u.linkDrag.active, "ビュー切り替えのテスト: ドラッグ中");
    h.c.setView(isList ? app::View::Board : app::View::List);
    h.frame(2);
    ck(!h.u.linkDrag.active, "別のビューに切り替えたらドラッグは終わる");
    up();
    ck(s->undoSize() == undo4, "切り替えのあとに離しても確定しない");
    h.c.setView(view);
    h.frame(3);
    // ダイアログが開いている間は出さない
    {
      ui::Frame fr{h.c, h.u, h.p};
      ui::openDialog(fr, ui::Dlg::Export);
      h.frame(2);
      move(slotOf(idA)->hr.x, slotOf(idA)->hr.y);
      down();
      ck(!h.u.linkDrag.active, "ダイアログの間はハンドルのドラッグを始めない");
      up();
      h.key(ImGuiKey_Escape);
      h.frame(2);
    }
    io.AddMousePosEvent(900, 880);
    h.frame(2);
  }
  // 依存のドラッグ（Issue #8）: 空いた場所で離すと「新しい後続（先行）の課題を作ってつなぐ」メニューが出る（ガント・一覧・ボード）
  for (const app::View view : {app::View::Gantt, app::View::List, app::View::Board}) {
    const std::string nm = view == app::View::Gantt ? "ガント: " : view == app::View::List ? "一覧: " : "ボード: ";
    const auto ck = [&](bool ok, const char* what) { check(ok, (nm + "空き領域のメニュー: " + what).c_str()); };
    h.c.setView(view);
    h.c.setUi("zoom", "week");
    h.u.detailOpen = false;
    h.c.clearSelection();
    h.u.sortKey.clear();
    h.u.sortDir = 0;
    h.u.customPx = 0;
    h.u.scrollToBase = true;
    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(900, 880);
    h.frame(3);
    Session* s = h.c.session();
    const auto tree = [&]() -> const Tree& { return *h.c.session()->tree(); };
    const auto linked = [&](const std::string& from, const std::string& to) {
      const Issue* o = h.c.session()->issue(from);
      return o && std::find(o->blocks.begin(), o->blocks.end(), to) != o->blocks.end();
    };
    const float hd = h.u.m.s(10.f);
    // 課題 id のハンドルの中心（見えていなければ false）
    const auto handle = [&](const std::string& id, bool right, ImVec2& out) {
      if (view == app::View::Gantt) {
        for (const auto& g : h.u.barGeom)
          if (g.id == id && g.xl > h.u.ganttLeft + 30 && g.xr < 1380 && g.cy > 160 && g.cy < 600 && g.xr - g.xl > 12) {
            out = ImVec2(right ? g.xr + hd : g.xl - hd, g.cy);
            return true;
          }
        return false;
      }
      const int i = tree().find(id);
      for (const auto& sl : h.u.linkSlots)
        if (sl.idx == i && sl.a.y > 110 && sl.b.y < 700 && sl.a.x > 10 && sl.b.x < 1420) {
          out = right ? sl.hr : sl.hl;
          return true;
        }
      return false;
    };
    std::string idA;
    for (int i = 0; i < tree().n && idA.empty(); ++i) {
      ImVec2 pt;
      if (tree().isLeaf(i) && tree().kind[static_cast<std::size_t>(i)] == Kind::Task && handle(tree().ids[static_cast<std::size_t>(i)], true, pt))
        idA = tree().ids[static_cast<std::size_t>(i)];
    }
    ck(!idA.empty(), "テスト用の課題が見えている");
    if (idA.empty()) continue;
    // ボードでは、起点が To Do 以外の列にあるときに、作る課題が同じ列に入ることを確かめる
    const std::string statusOrig = s->issue(idA)->status;
    if (view == app::View::Board) {
      s->setStatus(idA, "In Progress");
      h.frame(3);
    }
    const ImVec2 empty(800.f, 760.f);  // 行・カードのない場所
    const auto move = [&](float x, float y, int n = 2) {
      io.AddMousePosEvent(x, y);
      h.frame(n);
    };
    // ハンドルを押して空き領域まで動かし、離す
    const auto dragToEmpty = [&](bool right) {
      ImVec2 pt;
      if (!handle(idA, right, pt)) return false;
      move(pt.x, pt.y);
      io.AddMouseButtonEvent(0, true);
      h.frame(2);
      move(empty.x, empty.y);
      io.AddMouseButtonEvent(0, false);
      h.frame(3);
      return true;
    };
    const auto center = [](const std::pair<ImVec2, ImVec2>& r) { return ImVec2((r.first.x + r.second.x) / 2, (r.first.y + r.second.y) / 2); };
    const auto count = [&] { return h.c.session()->issues().size(); };
    const std::size_t undo0 = s->undoSize(), n0 = count();
    const std::string parentA = s->issue(idA)->parent, statusA = s->issue(idA)->status;
    const std::string expectName = h.c.newIssueName(parentA);
    // ボードの列（状況。大文字小文字を無視し、空は先頭の To Do）
    const auto colOf = [](std::string st) {
      if (st.empty()) st = "To Do";
      for (char& ch : st) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
      return st;
    };

    // 右のハンドルから空き領域: メニューが出る。何も変わらない。主になる項目は「後続」（先に並ぶ）
    ck(dragToEmpty(true), "右のハンドルを空き領域まで動かせる");
    ck(h.u.linkMenu.active && h.u.linkMenu.id == idA && h.u.linkMenu.fromRight && !h.u.linkDrag.active, "離すとメニューが出る");
    ck(count() == n0 && s->undoSize() == undo0, "メニューの間は何も変えない");
    ck(h.u.linkMenu.succItem.first.y < h.u.linkMenu.predItem.first.y && h.u.linkMenu.predItem.first.y < h.u.linkMenu.cancelItem.first.y,
       "右のハンドルでは「後続」が先に並ぶ");
    ck(ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId), "メニューはポップアップ");
    // Esc: 何も変えない（選択も残る）
    h.c.select(idA);
    h.frame(2);
    h.key(ImGuiKey_Escape);
    ck(!h.u.linkMenu.active && count() == n0 && s->undoSize() == undo0, "Esc でメニューが閉じ、何も変わらない");
    ck(h.c.isSelected(idA), "Esc でメニューを閉じても選択は外れない");
    h.c.clearSelection();
    h.frame(2);
    // メニューの外をクリック: 閉じるだけ
    ck(dragToEmpty(true) && h.u.linkMenu.active, "もう一度メニューを出す");
    h.click(300, 780);
    h.frame(2);
    ck(!h.u.linkMenu.active && count() == n0 && s->undoSize() == undo0, "メニューの外をクリックすると閉じ、何も変わらない");
    ck(h.c.selection().empty(), "外のクリックは選択にならない");
    // 「取り消し」
    ck(dragToEmpty(true) && h.u.linkMenu.active, "メニューを出す（取り消し用）");
    {
      const ImVec2 c = center(h.u.linkMenu.cancelItem);
      h.click(c.x, c.y);
      h.frame(2);
    }
    ck(!h.u.linkMenu.active && count() == n0 && s->undoSize() == undo0, "「取り消し」で何も変わらない");

    // 新しい後続を作ってつなぐ
    ck(dragToEmpty(true) && h.u.linkMenu.active, "メニューを出す（後続）");
    {
      const ImVec2 c = center(h.u.linkMenu.succItem);
      h.click(c.x, c.y);
      h.frame(3);
    }
    ck(!h.u.linkMenu.active && count() == n0 + 1, "「後続」を選ぶと課題が1つ増える");
    std::string nid;
    {
      // 作った課題は起点（葉）の直後にある
      const auto& iss = h.c.session()->issues();
      std::size_t ia = 0;
      for (std::size_t i = 0; i < iss.size(); ++i)
        if (iss[i].id == idA) ia = i;
      nid = ia + 1 < iss.size() ? iss[ia + 1].id : std::string();
    }
    ck(!nid.empty() && h.c.session()->issue(nid) && h.c.session()->issue(nid)->parent == parentA, "作った課題は起点の兄弟（親が同じ）");
    ck(!nid.empty() && linked(idA, nid), "起点 → 新しい課題 の順につながる");
    ck(!nid.empty() && (expectName.empty() ? !h.c.session()->issue(nid)->summary.empty() : h.c.session()->issue(nid)->summary == expectName),
       "名前は新規課題の名前の規則どおり");
    if (view == app::View::Board)
      ck(!nid.empty() && colOf(h.c.session()->issue(nid)->status) == colOf(statusA) && colOf(statusA) == "in progress", "ボードでは作る課題の状況（列）は起点と同じ");
    ck(s->undoSize() == undo0 + 1, "作成とつなぎは Undo 1回分");
    ck(!nid.empty() && h.u.editField == ui::EditField::Name && h.u.editId == nid, "作ったあと件名のその場編集を始める");
    ck(!nid.empty() && h.c.isSelected(nid), "作った課題が選択される");
    h.key(ImGuiKey_Escape);  // その場編集を終える
    ck(h.u.editField == ui::EditField::None, "Esc でその場編集を終える");
    h.c.undo();
    h.frame(3);
    ck(count() == n0 && !h.c.session()->issue(nid) && !linked(idA, nid) && s->undoSize() == undo0, "Undo 1回で課題ごと戻る");

    // 左のハンドル: 主になるのは「先行」。新しい先行を作ってつなぐ（新しい課題 → 起点）
    ck(dragToEmpty(false) && h.u.linkMenu.active && !h.u.linkMenu.fromRight, "左のハンドルから空き領域でメニューが出る");
    ck(h.u.linkMenu.predItem.first.y < h.u.linkMenu.succItem.first.y, "左のハンドルでは「先行」が先に並ぶ");
    {
      const ImVec2 c = center(h.u.linkMenu.predItem);
      h.click(c.x, c.y);
      h.frame(3);
    }
    ck(count() == n0 + 1 && s->undoSize() == undo0 + 1, "「先行」を選ぶと課題が1つ増え、Undo 1回分");
    {
      const auto& iss = h.c.session()->issues();
      std::size_t ia = 0;
      for (std::size_t i = 0; i < iss.size(); ++i)
        if (iss[i].id == idA) ia = i;
      nid = ia + 1 < iss.size() ? iss[ia + 1].id : std::string();
    }
    ck(!nid.empty() && nid != idA && linked(nid, idA) && !linked(idA, nid), "新しい課題 → 起点 の順につながる");
    ck(!nid.empty() && h.c.session()->issue(nid)->parent == parentA, "新しい先行も起点の兄弟");
    if (view == app::View::Board)
      ck(!nid.empty() && colOf(h.c.session()->issue(nid)->status) == colOf(statusA), "ボードでは新しい先行の状況（列）も起点と同じ");
    ck(!nid.empty() && h.u.editField == ui::EditField::Name && h.u.editId == nid, "新しい先行でも件名のその場編集を始める");
    h.key(ImGuiKey_Escape);
    h.c.undo();
    h.frame(3);
    ck(count() == n0 && !linked(nid, idA) && s->undoSize() == undo0, "新しい先行も Undo 1回で課題ごと戻る");

    // 相手のいる行・カードの上で離すときは、今までどおりつなぐ（メニューは出ない）
    // 元の場所で離す・メニューの出ない場所（ビューの外）で離すと取り消し
    {
      ImVec2 pt;
      ck(handle(idA, true, pt), "ハンドルが見えている");
      move(pt.x, pt.y);
      io.AddMouseButtonEvent(0, true);
      h.frame(2);
      move(pt.x + 30, pt.y + 2);
      move(pt.x, pt.y);
      io.AddMouseButtonEvent(0, false);
      h.frame(3);
      ck(!h.u.linkMenu.active && !h.u.linkDrag.active && s->undoSize() == undo0, "元の場所で離すとメニューは出ず、取り消し");
      move(pt.x, pt.y);
      io.AddMouseButtonEvent(0, true);
      h.frame(2);
      move(700, 20);  // 上のバー（ビューの外）
      io.AddMouseButtonEvent(0, false);
      h.frame(3);
      ck(!h.u.linkMenu.active && !h.u.linkDrag.active && s->undoSize() == undo0, "ビューの外で離してもメニューは出ない");
    }
    // ダイアログが開いている間は出ない
    {
      ui::Frame fr{h.c, h.u, h.p};
      ui::openDialog(fr, ui::Dlg::Export);
      h.frame(2);
      ck(!dragToEmpty(true) || (!h.u.linkMenu.active && !h.u.linkDrag.active), "ダイアログの間はドラッグもメニューも出ない");
      h.key(ImGuiKey_Escape);
      h.frame(2);
    }
    if (view == app::View::Board) {
      s->setStatus(idA, statusOrig.empty() ? "To Do" : statusOrig);
      h.frame(2);
    }
    io.AddMousePosEvent(900, 880);
    h.frame(2);
  }
  // 件名のその場編集で Enter（確定して次の兄弟を足す）・Ctrl+Enter（続けて足す）: 課題の追加は描画が終わってから行い、
  // そのフレームの描画が1つの計算結果に揃っている（カードの重複・脱落がない）こと（ガント・一覧・ボード）
  for (const app::View view : {app::View::Gantt, app::View::List, app::View::Board}) {
    const std::string nm = view == app::View::Gantt ? "ガント: " : view == app::View::List ? "一覧: " : "ボード: ";
    const auto ck = [&](bool ok, const char* what) { check(ok, (nm + "Enter で続けて追加: " + what).c_str()); };
    h.c.setView(view);
    h.c.setUi("zoom", "week");
    h.u.detailOpen = false;
    h.c.clearSelection();
    h.u.sortKey.clear();
    h.u.sortDir = 0;
    h.u.customPx = 0;
    h.u.scrollToBase = true;
    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(900, 880);
    h.frame(3);
    Session* s = h.c.session();
    // このフレームに描かれた課題 id（描いた時点の計算結果での id）
    const auto drawn = [&] {
      std::vector<std::string> v;
      if (view == app::View::Gantt) {
        for (const auto& g : h.u.barGeom) v.push_back(g.id);
      } else {
        for (const auto& sl : h.u.linkSlots) v.push_back(sl.id);
      }
      std::sort(v.begin(), v.end());
      return v;
    };
    const auto issueIds = [&] {
      std::vector<std::string> v;
      for (const Issue& o : h.c.session()->issues()) v.push_back(o.id);
      std::sort(v.begin(), v.end());
      return v;
    };
    // 1フレームごとに、描かれた課題 id の集合を覚える。追加の前の集合 S か、追加した課題を足した集合 S+新しい課題 のどちらかで、
    // 重複も脱落もないこと（追加が描画の途中で起きると、前半は古い計算結果・後半は新しい計算結果で描かれて崩れる）
    std::vector<std::vector<std::string>> frames;
    bool dup = false;
    const auto watch = [&](int n) {
      for (int i = 0; i < n; ++i) {
        h.frame(1);
        const auto d = drawn();
        if (std::adjacent_find(d.begin(), d.end()) != d.end()) dup = true;
        frames.push_back(d);
      }
    };
    const auto exact = [&](const std::vector<std::string>& base, const std::string& added) {
      std::vector<std::string> plus = base;
      plus.push_back(added);
      std::sort(plus.begin(), plus.end());
      for (const auto& d : frames)
        if (d != base && d != plus) return false;
      return true;
    };
    // 追加の元（葉）: 画面に見えている最初の葉
    std::string idA;
    for (int i = 0; i < h.c.session()->tree()->n && idA.empty(); ++i)
      if (h.c.session()->tree()->isLeaf(i) && h.c.session()->tree()->kind[static_cast<std::size_t>(i)] == Kind::Task) {
        const std::string id = h.c.session()->tree()->ids[static_cast<std::size_t>(i)];
        const auto d = drawn();
        if (std::binary_search(d.begin(), d.end(), id)) idA = id;
      }
    ck(!idA.empty(), "テスト用の課題が見えている");
    if (idA.empty()) continue;
    const std::size_t n0 = h.c.session()->issues().size(), undo0 = s->undoSize();
    ui::Frame fr{h.c, h.u, h.p};
    ui::beginEdit(fr, idA, ui::EditField::Name, true);  // 追加した直後の編集
    h.frame(3);
    ck(h.u.editField == ui::EditField::Name && h.u.editId == idA, "件名の編集が始まる");
    const std::vector<std::string> s0 = drawn();
    // Enter: 確定して、兄弟を足してその編集を始める
    io.AddKeyEvent(ImGuiKey_Enter, true);
    watch(1);
    io.AddKeyEvent(ImGuiKey_Enter, false);
    watch(3);
    ck(h.c.session()->issues().size() == n0 + 1, "Enter で兄弟が1つ増える");
    ck(h.u.editField == ui::EditField::Name && h.u.editId != idA && !h.u.editId.empty(), "増えた課題の件名の編集が始まる");
    const std::string id2 = h.u.editId;
    const auto after1 = issueIds();
    ck(!dup && exact(s0, id2), "Enter のフレームの描画に重複・脱落がない");
    frames.clear();
    const std::vector<std::string> s1 = drawn();
    // Ctrl+Enter: 続けて足す
    io.AddKeyEvent(ImGuiMod_Ctrl, true);
    io.AddKeyEvent(ImGuiKey_Enter, true);
    watch(1);
    io.AddKeyEvent(ImGuiKey_Enter, false);
    io.AddKeyEvent(ImGuiMod_Ctrl, false);
    watch(3);
    ck(h.c.session()->issues().size() == n0 + 2, "Ctrl+Enter でさらに1つ増える");
    ck(h.u.editField == ui::EditField::Name && h.u.editId != id2 && h.u.editId != idA, "続けて足した課題の編集が始まる");
    ck(!dup && exact(s1, h.u.editId), "Ctrl+Enter のフレームの描画に重複・脱落がない");
    ck(h.c.session()->issues().size() == after1.size() + 1, "件数の確認");
    h.key(ImGuiKey_Escape);
    ck(h.u.editField == ui::EditField::None, "Esc で編集を終える");
    // 戻す（足した 2 件）
    h.c.undo();
    h.c.undo();
    h.frame(3);
    ck(h.c.session()->issues().size() == n0 && s->undoSize() == undo0, "Undo で足した課題が戻る");
    io.AddMousePosEvent(900, 880);
    h.frame(2);
  }
  // 依存のドラッグ中、見えている範囲の端へマウスを近づけると自動でスクロールする（課題が多いとき。一覧・ボード）
  {
    const std::string prevProject = h.c.currentId();
    auto g = generateProject({150, 11});
    check(h.c.importText(exportAppCsv(g.issues, g.meta), "スクロール確認", app::ImportMode::New).ok, "ドラッグのスクロール確認用の取り込み");
    ImGuiIO& io = ImGui::GetIO();
    for (const app::View view : {app::View::List, app::View::Board}) {
      h.c.setView(view);
      h.c.clearSelection();
      h.u.sortKey.clear();
      h.u.sortDir = 0;
      io.AddMousePosEvent(900, 880);
      h.frame(3);
      const std::size_t undo0 = h.c.session()->undoSize();
      const ui::UiState::LinkSlot* first = nullptr;
      for (const auto& sl : h.u.linkSlots)
        if (sl.a.y > 200 && sl.b.y < 600 && (!first || sl.a.y < first->a.y)) first = &sl;
      const bool okFirst = first != nullptr;
      check(okFirst, "スクロール確認: 行・カードが見えている");
      if (!okFirst) continue;
      const int firstIdx = first->idx;
      const float y0 = first->a.y;
      const float hx = first->hr.x;
      io.AddMousePosEvent(hx, first->hr.y);
      h.frame(2);
      io.AddMouseButtonEvent(0, true);
      h.frame(2);
      check(h.u.linkDrag.active, "スクロール確認: ドラッグ中");
      io.AddMousePosEvent(hx, 895);  // 下の端
      h.frame(30);
      bool found = false;
      float y1 = y0;
      for (const auto& sl : h.u.linkSlots)
        if (sl.idx == firstIdx) found = true, y1 = sl.a.y;
      check(!h.u.linkSlots.empty() && (!found || y1 < y0 - 20.f), "下の端へ近づくと自動でスクロールする（元の行が上へ動くか、見えなくなる）");
      h.key(ImGuiKey_Escape);
      io.AddMouseButtonEvent(0, false);
      h.frame(3);
      check(!h.u.linkDrag.active && h.c.session()->undoSize() == undo0, "スクロールしたあと Esc で取り消せる");
    }
    h.c.openProject(prevProject);
    h.c.setView(app::View::Gantt);
    h.frame(3);
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
  for (auto v : {app::View::Gantt, app::View::Board, app::View::List, app::View::Goals, app::View::Stats, app::View::Today}) {
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
    // 統計ビュー（集計は毎フレーム。5,000 件でも 1 フレームが重くならない）
    h.c.setView(app::View::Stats);
    h.frame(3);
    check(h.u.statsLoadRows > 0, "5000件でも負荷のヒートマップを集計する");
    {
      // 5,000 件の報告（HTML）を数秒以内に作れる。大きさも極端でない
      ExportOptions all;
      const auto t0 = std::chrono::steady_clock::now();
      const auto rep = h.c.buildReport(all);
      const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
      std::fprintf(stderr, "ui_smoke: 5000件の報告 %.0f ms, %zu KB\n", ms, rep.ok() ? rep.value().size() / 1024 : 0);
      check(rep.ok() && rep.value().size() < 16u * 1024 * 1024, "5000件の報告（HTML）の大きさ");
#ifdef ROADMAP_SANITIZED
      check(ms < 8000, "5000件の報告が数秒以内（サニタイザ付き）");
#else
      check(ms < 3000, "5000件の報告が数秒以内");
#endif
    }
    {
      const auto t0 = std::chrono::steady_clock::now();
      h.frame(10);
      const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / 10;
      std::fprintf(stderr, "ui_smoke: 5000件の統計ビュー 1フレーム %.1f ms\n", ms);
#ifdef ROADMAP_SANITIZED
      check(ms < 400, "5000件の統計ビューが重くない（サニタイザ付き）");
#else
      check(ms < 100, "5000件の統計ビューが重くない");
#endif
    }
    // 今日ビュー（Issue #72）: 5,000 件でも見える行だけを描き、1 フレームが重くならない
    h.c.setView(app::View::Today);
    h.c.setUi("todayWho", "all");
    h.frame(3);
    check(h.u.today.drawn && h.u.today.total > 0, "5000件でも今日ビューを描く");
    check(!h.u.today.rows.empty() && h.u.today.rows.size() < 200, "今日ビューは見える行だけを描く（仮想化）");
    {
      const auto t0 = std::chrono::steady_clock::now();
      h.frame(10);
      const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / 10;
      std::fprintf(stderr, "ui_smoke: 5000件の今日ビュー 1フレーム %.1f ms\n", ms);
#ifdef ROADMAP_SANITIZED
      check(ms < 400, "5000件の今日ビューが重くない（サニタイザ付き）");
#else
      check(ms < 100, "5000件の今日ビューが重くない");
#endif
    }
    h.c.setUi("todayTab", "week");
    h.frame(4);
    check(h.u.today.drawn && h.u.today.week, "5000件でも今週を描く");
    h.c.setUi("todayTab", "today");
    h.c.setView(app::View::List);
    h.frame(2);
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
  // 添付バッジのツールチップ: マウスを載せたときだけ、最初の 4 件ぶんの画像をテクスチャにして並べる（載せていないときは読まない）
  {
    const std::string png(reinterpret_cast<const char*>(kPng3x2), sizeof kPng3x2);
    h.c.createProject(app::SampleKind::Hierarchy, "ホバーの確認", "HOV");
    h.c.setView(app::View::List);
    h.u.detailOpen = false;
    h.c.clearSelection();
    h.frame(3);
    // 1 件目はテキスト、2〜6 件目は画像（4 件までしか読まない）。画像ごとにバイト列を変えて、キャッシュが共有されないようにする
    h.fs.put("ext/memo.txt", "memo");
    std::vector<std::string> files = {"ext/memo.txt"};
    for (int k = 0; k < 5; ++k) {
      const std::string name = "ext/p" + std::to_string(k) + ".bmp";
      h.fs.put(name, makeBmp(20 + k, 10));
      files.push_back(name);
    }
    h.c.attachFiles("11", files);
    check(h.c.attachmentCount("11") == 6, "ツールチップの確認用に 6 件を付ける");
    h.frame(30);
    const int before = h.p.created;
    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(700, 880);  // 何もないところ
    h.frame(30);
    check(h.p.created == before, "マウスを載せていないときは添付の画像を読まない");
    const auto badge = [&](const std::string& id) {
      for (const auto& b : h.u.attachBadges)
        if (b.id == id) return b;
      return ui::UiState::DropTarget{ImVec2(-1, -1), ImVec2(-1, -1), ""};
    };
    const auto tooltipOn = [&] {
      ImGuiWindow* tip = ImGui::FindWindowByName("##Tooltip_00");
      return tip && tip->Active;
    };
    const auto b = badge("11");
    check(!b.id.empty(), "バッジの位置が分かる");
    const ImVec2 mid((b.a.x + b.b.x) / 2, (b.a.y + b.b.y) / 2);
    io.AddMousePosEvent(mid.x, mid.y);
    h.frame(30);
    check(tooltipOn(), "バッジにマウスを載せるとツールチップが出る");
    check(h.p.created - before == 3, "最初の 4 件のうち画像の 3 枚だけをテクスチャにする（5・6 件目は読まない）");
    const int shown = h.p.created;
    h.frame(10);
    check(h.p.created == shown, "同じツールチップを出し続けても作り直さない");
    io.AddMousePosEvent(700, 880);
    h.frame(5);
    check(!tooltipOn(), "マウスを外すとツールチップが消える");
    // 添付タブの項目: マウスを載せると SHA-256 の短縮表示（先頭 12 文字）が出る。sha256 のない古いメタ情報には出ない
    {
      check(h.c.attachmentsOf("11")[0].sha256.size() == 64, "付けた添付は SHA-256 を持つ");
      h.c.select("11");
      h.u.detailOpen = true;
      h.u.detailTab = 3;
      io.AddMousePosEvent(700, 880);
      h.frame(8);
      check(h.u.attachRows.size() == 6, "添付タブの行が 6 つ");
      if (h.u.attachRows.size() == 6) {
        const auto& r = h.u.attachRows[0];  // memo.txt（画像でない）
        io.AddMousePosEvent(r.first.x + 150, (r.first.y + r.second.y) / 2);
        h.frame(30);
        check(tooltipOn(), "項目にマウスを載せると詳細（SHA-256）のツールチップが出る");
        io.AddMousePosEvent(700, 880);
        h.frame(5);
        h.c.session()->attachments()["11"][0].sha256.clear();  // 古いメタ情報
        io.AddMousePosEvent(r.first.x + 150, (r.first.y + r.second.y) / 2);
        h.frame(30);
        check(!tooltipOn(), "sha256 がない古い項目には詳細を出さない");
      }
      // 読めない画像（壊れた PNG）のアイコンの上: エラーの説明は消えず、詳細（SHA-256）と 1 つのツールチップにまとまる
      h.c.session()->attachments()["11"][0].sha256 = h.c.attachmentsOf("11")[1].sha256;  // 1 行目も詳細を持つ状態に戻す
      h.fs.put("ext/bad.png", "壊れたファイル");
      h.c.attachFiles("11", {"ext/bad.png"});
      io.AddMousePosEvent(700, 880);
      h.frame(12);  // デコードは 1 フレームに 1 枚。失敗の結果が出るまで待つ
      check(h.u.attachRows.size() == 7, "壊れた画像を足した添付タブの行が 7 つ");
      if (h.u.attachRows.size() == 7) {
        const auto tipHeight = [&](ImVec2 pos) {
          io.AddMousePosEvent(700, 880);
          h.frame(5);
          io.AddMousePosEvent(pos.x, pos.y);
          h.frame(1);
          ImGuiWindow* tip = ImGui::FindWindowByName("##Tooltip_00");
          const bool early = tip && tip->Active;  // 待つ前（エラーの説明だけがすぐ出る）
          h.frame(30);
          tip = ImGui::FindWindowByName("##Tooltip_00");
          return std::pair<bool, float>(early, tip && tip->Active ? tip->Size.y : 0.f);
        };
        const auto& bad = h.u.attachRows[6];
        const auto failed = tipHeight(ImVec2(bad.first.x + 20, (bad.first.y + bad.second.y) / 2));
        const auto& plain = h.u.attachRows[0];  // 画像でない行（詳細だけの 1 行）
        const auto hashOnly = tipHeight(ImVec2(plain.first.x + 150, (plain.first.y + plain.second.y) / 2));
        check(failed.first, "読めない画像のアイコンでは、待たずにエラーの説明が出る");
        check(!hashOnly.first, "詳細は少し待ってから出る");
        check(hashOnly.second > 0.f && failed.second > hashOnly.second, "待ったあとも、エラーの説明が詳細に置き換わらず両方が出る");
      }
      io.AddMousePosEvent(700, 880);
      h.u.detailOpen = false;
      h.frame(5);
    }
    // ボード: 末端の課題のカードのバッジ（画像 1 枚だけ付けた課題で、載せたときに 1 枚だけ読む。画面に見えているカードを使う）
    for (const char* id : {"4", "6", "7", "8"}) h.c.attachFiles(id, {"ext/p0.bmp"});
    h.c.setView(app::View::Board);
    io.AddMousePosEvent(700, 500);
    for (int k = 0; k < 8; ++k) {  // 前の画面のスクロールが残っていることがあるので、いちばん上まで戻す
      io.AddMouseWheelEvent(0, 10);
      h.frame(2);
    }
    h.frame(30);
    const int boardBefore = h.p.created;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImVec2 at(-1, -1);
    for (const auto& bb : h.u.attachBadges)
      if (bb.a.y > 150 && bb.b.y < vp->Size.y - 50 && bb.a.x > 0 && bb.b.x < vp->Size.x) at = ImVec2((bb.a.x + bb.b.x) / 2, (bb.a.y + bb.b.y) / 2);
    check(at.x >= 0, "ボードのカードのバッジの位置が分かる");
    io.AddMousePosEvent(at.x, at.y);
    h.frame(30);
    check(tooltipOn(), "ボードのバッジでもツールチップが出る");
    check(h.p.created == boardBefore + 1, "ボードでも載せたときだけ画像を読む");
    io.AddMousePosEvent(700, 880);
    h.frame(5);
    h.c.deleteCurrentProject();
    h.frame(2);
  }
  // 添付のサムネイルのドラッグ: 別の課題へ移動・Ctrl でコピー・同じ課題の中で並べ替え・何もないところでは何もしない
  {
    const std::string png(reinterpret_cast<const char*>(kPng3x2), sizeof kPng3x2);
    h.c.createProject(app::SampleKind::Hierarchy, "ドラッグの確認", "DRG");
    h.c.setView(app::View::List);
    h.u.detailOpen = false;
    h.c.clearSelection();
    h.frame(3);
    check(h.u.dropTargets.size() >= 2, "行がドロップ先になる");
    const std::string src = h.u.dropTargets[0].id, dst = h.u.dropTargets[1].id;
    check(src != dst, "ドラッグの確認用に2つの課題がある");
    h.fs.put("ext/一枚目.png", png);
    h.fs.put("ext/二枚目.png", png);
    h.fs.put("ext/三枚目.txt", "memo");
    h.c.attachFiles(src, {"ext/一枚目.png", "ext/二枚目.png", "ext/三枚目.txt"});
    h.c.select(src);
    h.u.detailOpen = true;
    h.u.detailTab = 3;
    h.frame(8);
    check(h.u.attachRows.size() == 3, "添付タブの行の範囲が分かる");
    ImGuiIO& io = ImGui::GetIO();
    const auto rowMid = [&](std::size_t k) {
      const auto& r = h.u.attachRows[k];
      return ImVec2(r.first.x + 100, (r.first.y + r.second.y) / 2);
    };
    const auto targetMid = [&](const std::string& id) {
      for (const auto& t : h.u.dropTargets)
        if (t.id == id) return ImVec2((t.a.x + t.b.x) / 2, (t.a.y + t.b.y) / 2);
      return ImVec2(-1, -1);
    };
    // 押す → 動かす → 離す。途中の状態を返す
    const auto dragTo = [&](ImVec2 from, ImVec2 to, bool ctrl, bool release = true) {
      io.AddMousePosEvent(from.x, from.y);
      h.frame();
      io.AddMouseButtonEvent(0, true);
      h.frame();
      io.AddMousePosEvent(from.x + 12, from.y + 2);
      h.frame();
      io.AddMousePosEvent(to.x, to.y);
      h.frame(2);
      if (ctrl) io.AddKeyEvent(ImGuiMod_Ctrl, true);
      h.frame();
      if (release) {
        io.AddMouseButtonEvent(0, false);
        h.frame(2);
        io.AddKeyEvent(ImGuiMod_Ctrl, false);
        h.frame();
      }
    };
    // 移動: 1つ目 → dst の行
    dragTo(rowMid(0), targetMid(dst), false);
    check(!h.u.attachDrag.active, "離したらドラッグは終わる");
    check(h.c.attachmentCount(src) == 2 && h.c.attachmentCount(dst) == 1, "サムネイルを別の課題の行へ離すと移動する");
    check(h.c.attachmentsOf(dst).size() == 1 && h.c.attachmentsOf(dst)[0].name == "一枚目.png", "移した添付の名前");
    check(h.u.dlg == ui::Dlg::None, "つかんで動かしたときはプレビューを開かない");
    check(h.fs.exists("data/ドラッグの確認/添付/" + dst + "/一枚目.png"), "本体も移る");
    // コピー: Ctrl を押しながら
    h.frame(3);
    dragTo(rowMid(0), targetMid(dst), true);
    check(h.c.attachmentCount(src) == 2 && h.c.attachmentCount(dst) == 2, "Ctrl を押して離すとコピーする（元は残る）");
    // 何もないところ（上のバー）へ離しても何も起きない。CSV の取り込みも開かない
    h.frame(3);
    dragTo(rowMid(0), ImVec2(3, 3), false);
    check(h.c.attachmentCount(src) == 2 && h.c.attachmentCount(dst) == 2, "ドロップ先がない場所では何もしない");
    check(h.u.dlg == ui::Dlg::None, "ドロップ先がない場所で取り込みを開かない");
    // 同じ課題の詳細パネルの、一覧の外では何もしない
    dragTo(rowMid(0), ImVec2(h.u.attachRows[0].first.x + 100, h.u.attachRows[0].first.y - 150), false);
    check(h.c.attachmentsOf(src)[0].name == "二枚目.png", "一覧の外では並べ替えない");
    // 並べ替え: 先頭を最後の行の下へ
    h.frame(3);
    const std::string first = h.c.attachmentsOf(src)[0].name;
    dragTo(rowMid(0), ImVec2(h.u.attachRows[1].first.x + 100, h.u.attachRows[1].second.y - 2), false);
    check(h.c.attachmentsOf(src).size() == 2 && h.c.attachmentsOf(src)[1].name == first, "同じ課題の中でドラッグすると並べ替える");
    // Esc で取り消し
    h.frame(3);
    dragTo(rowMid(0), targetMid(dst), false, false);
    check(h.u.attachDrag.active, "つかんで動かすとドラッグが始まる");
    h.key(ImGuiKey_Escape);
    check(!h.u.attachDrag.active, "Esc で取り消す");
    io.AddMouseButtonEvent(0, false);
    h.frame(2);
    check(h.c.attachmentCount(src) == 2 && h.c.attachmentCount(dst) == 2, "取り消したら動かさない");
    // コマンドパレット（Ctrl+K）が開いたら取り消す。モーダルの裏で離しても動かさない
    dragTo(rowMid(0), targetMid(dst), false, false);
    check(h.u.attachDrag.active, "パレットのテスト: つかんでいる");
    h.key(ImGuiKey_K, true);
    check(h.u.paletteOpen && !h.u.attachDrag.active, "パレットを開くとドラッグを取り消す");
    io.AddMouseButtonEvent(0, false);
    h.frame(3);
    check(h.c.attachmentCount(src) == 2 && h.c.attachmentCount(dst) == 2, "パレットの裏で離しても動かさない");
    h.key(ImGuiKey_Escape);
    check(!h.u.paletteOpen, "パレットを閉じる");
    h.frame(3);
    // Esc とボタンを離すのが同じフレームでも、動かさずに取り消す（Esc で選択も外さない）
    h.c.select(src);
    h.u.detailOpen = true;
    h.frame(4);
    dragTo(rowMid(0), targetMid(dst), false, false);
    check(h.u.attachDrag.active, "Esc と同時のテスト: つかんでいる");
    io.AddKeyEvent(ImGuiKey_Escape, true);
    io.AddMouseButtonEvent(0, false);
    h.frame();
    io.AddKeyEvent(ImGuiKey_Escape, false);
    h.frame(3);
    check(h.c.attachmentCount(src) == 2 && h.c.attachmentCount(dst) == 2, "Esc と離すのが同じフレームなら動かさない");
    check(!h.c.selection().empty() && h.u.detailOpen, "ドラッグを取り消した Esc は、選択や詳細パネルには効かない");
    // 存在しない課題・自分自身へ（コントローラ側の確認。画面の途中で課題が消えても落ちない）
    dragTo(rowMid(0), targetMid(dst), false, false);
    h.c.session()->deleteIssues({dst});
    h.c.afterEdit();
    io.AddMouseButtonEvent(0, false);
    h.frame(3);
    check(!h.u.attachDrag.active, "ドラッグ中に先の課題が消えても落ちない");
    // クリックだけ（動かさない）ならプレビューを開く
    h.frame(3);
    {
      const auto& r = h.u.attachRows[1];  // 並べ替えたあとの2番目 = 二枚目.png
      h.click(r.first.x + 2 + 18, (r.first.y + r.second.y) / 2);
    }
    check(h.u.dlg == ui::Dlg::AttachPreview, "動かさずにクリックするとプレビューを開く");
    h.u.dlg = ui::Dlg::None;
    h.frame(2);
    // サムネイルの大きさのスライダー: 右端のほうを押すと大きくなり、プロジェクトの設定に覚える
    {
      const int before = h.c.uiInt("attachThumb", 36);
      const auto& sl = h.u.attachSlider;
      check(sl.second.x > sl.first.x, "スライダーがある");
      const float rowH0 = h.u.attachRows[0].second.y - h.u.attachRows[0].first.y;
      h.click(sl.first.x + 105, (sl.first.y + sl.second.y) / 2);
      h.frame(3);
      const int after = h.c.uiInt("attachThumb", 36);
      check(after > before && after <= 160, "スライダーでサムネイルを大きくできる");
      check(h.u.attachRows[0].second.y - h.u.attachRows[0].first.y > rowH0, "サムネイルの大きさに合わせて行が高くなる");
      check(h.c.session()->settings().ui.contains("attachThumb"), "大きさはプロジェクトの ui 設定に覚える");
      h.c.setUi("attachThumb", 99999);  // 範囲外の値が設定に入っていても落ちない（範囲に収める）
      h.frame(3);
      h.c.setUi("attachThumb", Json("壊れた値"));
      h.frame(3);
      check(h.c.uiInt("attachThumb", 36) == 36, "数値でない値は既定に戻る");
    }
    // 「ファイルを選んで追加」
    {
      h.fs.put("ext/選んだ.txt", "picked");
      h.p.pickedFiles = {"ext/選んだ.txt"};
      const int asked = h.p.pickAsked;
      const std::size_t n0 = h.c.attachmentCount(src);
      const auto& b = h.u.attachPickBtn;
      check(b.second.x > b.first.x, "「ファイルを選んで追加」のボタンがある");
      h.click((b.first.x + b.second.x) / 2, (b.first.y + b.second.y) / 2);
      h.frame(2);
      check(h.p.pickAsked == asked + 1, "ボタンでファイルを選ぶダイアログを開く");
      check(h.c.attachmentCount(src) == n0 + 1, "選んだファイルが付く");
      h.p.pickedFiles.clear();  // やめた
      const std::size_t n1 = h.c.attachmentCount(src);
      h.click((b.first.x + b.second.x) / 2, (b.first.y + b.second.y) / 2);
      h.frame(2);
      check(h.c.attachmentCount(src) == n1, "ダイアログをやめたら何も付けない");
    }
    h.u.detailOpen = false;
    h.frame(2);
  }
  // 添付の並べ替え: スクロールで隠れた行（子ウィンドウのクリップの外）の上では、並べ替えない
  {
    h.c.createProject(app::SampleKind::Hierarchy, "スクロールの確認", "SCR");
    h.c.setView(app::View::List);
    h.u.editField = ui::EditField::None;
    h.u.detailOpen = false;
    h.c.clearSelection();
    h.frame(3);
    const std::string id = h.u.dropTargets.at(0).id;
    std::vector<std::string> files;
    for (int k = 0; k < 30; ++k) {
      char nm[32];
      std::snprintf(nm, sizeof nm, "ext/メモ%02d.txt", k);
      h.fs.put(nm, "x");
      files.push_back(nm);
    }
    h.c.attachFiles(id, files);
    h.c.select(id);
    h.u.detailOpen = true;
    h.u.detailTab = 3;
    h.frame(6);
    ImGuiIO& io = ImGui::GetIO();
    check(h.u.attachRows.size() == 30, "30 件の行の範囲が分かる");
    const auto clip = [&] { return h.u.attachRowsClip; };
    io.AddMousePosEvent(clip().first.x + 100, clip().first.y + 200);
    for (int k = 0; k < 6; ++k) {  // 一覧を下へスクロール（上の行は子ウィンドウの外へ隠れる）
      io.AddMouseWheelEvent(0, -3);
      h.frame(2);
    }
    check(h.u.attachRows.size() == 30 && h.u.attachRows.front().first.y < clip().first.y, "スクロールすると、上の行が見える範囲の外へ出る");
    // 見えている行を探す
    std::size_t vis = 0;
    for (std::size_t k = 0; k < h.u.attachRows.size(); ++k) {
      const auto& r = h.u.attachRows[k];
      if (r.first.y >= clip().first.y + 4 && r.second.y <= clip().second.y - 4) {
        vis = k;
        break;
      }
    }
    check(vis > 0, "見えている行がある");
    const std::string moving = h.c.attachmentsOf(id)[vis].name;
    const auto rowMidAt = [&](std::size_t k) {
      const auto& r = h.u.attachRows[k];
      return ImVec2(r.first.x + 100, (r.first.y + r.second.y) / 2);
    };
    const auto drag = [&](ImVec2 from, ImVec2 to) {
      io.AddMousePosEvent(from.x, from.y);
      h.frame();
      io.AddMouseButtonEvent(0, true);
      h.frame();
      io.AddMousePosEvent(from.x + 12, from.y + 2);
      h.frame();
      io.AddMousePosEvent(to.x, to.y);
      h.frame(2);
      io.AddMouseButtonEvent(0, false);
      h.frame(3);
    };
    // 見える範囲のすぐ上（詳細パネルの見出し・タブ）。ここの下に、隠れた行の範囲が重なっている
    drag(rowMidAt(vis), ImVec2(clip().first.x + 100, clip().first.y - 6));
    check(h.c.attachmentsOf(id)[vis].name == moving && h.c.attachmentsOf(id)[0].name == "メモ00.txt", "隠れた行の上で離しても並べ替えない");
    for (std::size_t k = 0; k < 30; ++k)
      if (h.c.attachmentsOf(id)[k].name != [&] { char nm[32]; std::snprintf(nm, sizeof nm, "メモ%02d.txt", static_cast<int>(k)); return std::string(nm); }()) {
        check(false, "並びが変わっていない");
        break;
      }
    // 見える範囲の中なら今までどおり並べ替える（次の行の下半分へ）
    const std::size_t next = vis + 1;
    check(next < h.u.attachRows.size() && h.u.attachRows[next].second.y <= clip().second.y, "次の行も見えている");
    drag(rowMidAt(vis), ImVec2(h.u.attachRows[next].first.x + 100, h.u.attachRows[next].second.y - 2));
    check(h.c.attachmentsOf(id)[next].name == moving, "見える行の上では並べ替える");
    h.u.detailOpen = false;
    h.frame(2);
  }
  // クリップボードの画像の貼り付け（Ctrl+V）: 画像だけなら添付、テキストがあれば従来どおり子課題の貼り付け
  {
    const std::string bmp = makeBmp(40, 30);
    h.c.createProject(app::SampleKind::Hierarchy, "貼り付けの確認", "PST");
    h.c.setView(app::View::List);
    h.u.editField = ui::EditField::None;
    h.c.clearSelection();
    h.u.detailOpen = false;
    h.frame(3);
    const std::string id = h.u.dropTargets.at(0).id;
    h.p.clip = std::nullopt;
    h.p.clipImage = bmp;
    h.c.select(id);
    h.frame(2);
    h.key(ImGuiKey_V, true);
    h.frame(2);
    check(h.c.attachmentCount(id) == 1, "テキストがなく画像があるとき Ctrl+V で添付する");
    check(h.c.attachmentsOf(id).at(0).name.rfind("貼り付け-", 0) == 0 && h.c.attachmentsOf(id)[0].name.find(".bmp") != std::string::npos,
          "名前は 貼り付け-<日時>.bmp");
    check(h.u.dlg == ui::Dlg::None, "画像の貼り付けでは子課題のダイアログを開かない");
    h.frame(3);
    h.u.detailOpen = true;
    h.u.detailTab = 3;
    const int createdBefore = h.p.created;
    h.frame(8);  // 添付した画像のサムネイルも描ける（BMP としてデコードできる）
    check(h.p.created > createdBefore, "貼り付けた画像のサムネイルを作る");
    h.u.detailOpen = false;
    // テキストもあれば、従来どおり子課題の貼り付けが先
    h.p.clip = "子A\n子B";
    h.key(ImGuiKey_V, true);
    h.frame(2);
    check(h.u.dlg == ui::Dlg::PasteChildren && h.c.attachmentCount(id) == 1, "テキストがあれば画像より先に子課題の貼り付け");
    h.key(ImGuiKey_Escape);
    h.u.dlg = ui::Dlg::None;
    h.p.clip = std::nullopt;
    h.frame(2);
    // 課題を2つ選んでいる・選んでいないときは付けない
    h.c.clearSelection();
    h.frame(2);
    h.key(ImGuiKey_V, true);
    h.frame(2);
    check(h.c.attachmentCount(id) == 1, "課題が選ばれていなければ画像は付けない");
    h.u.dlg = ui::Dlg::None;
    // 画像もテキストもなければ通知だけ
    h.p.clipImage = std::nullopt;
    h.c.select(id);
    h.frame(2);
    h.key(ImGuiKey_V, true);
    h.frame(2);
    check(h.c.attachmentCount(id) == 1, "クリップボードが空なら何も付けない");
    // Windows の DIB（BMP ファイルのヘッダなし）から作った BMP も、サムネイルにできる
    const auto fromDib = bmpFromDib(bmp.substr(14));
    check(fromDib.ok() && *fromDib == bmp, "DIB に 14 バイトのヘッダを足すと元の BMP になる");
    check(fromDib.ok() && ui::decodeImage(*fromDib, 160).has_value(), "DIB から作った BMP をデコードできる");
    check(!bmpFromDib(bmp.substr(14, 20)).ok(), "途中で切れた DIB は断る");
    h.u.dlg = ui::Dlg::None;
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
  // 関連リンク（Issue #60）: 添付タブの「リンク」の節で追加（Web・ファイル・フォルダ・断る）・貼り付けて追加・クリックで開く・
  // 右クリックのメニュー・編集・削除・並べ替え・ドラッグ・バッジとツールチップ（一覧・ガントの表・ボード）
  {
    h.c.createProject(app::SampleKind::Hierarchy, "リンクの確認", "LNK");
    h.c.setView(app::View::List);
    h.u.detailOpen = false;
    h.c.clearSelection();
    h.frame(3);
    check(h.u.dropTargets.size() >= 2, "リンクの確認: 行がある");
    const std::string A = h.u.dropTargets[0].id, B = h.u.dropTargets[1].id;
    ImGuiIO& io = ImGui::GetIO();
    const auto center = [](const std::pair<ImVec2, ImVec2>& r) { return ImVec2((r.first.x + r.second.x) / 2, (r.first.y + r.second.y) / 2); };
    // クリック。押した・離したが別のフレームに分かれて処理されるので、離すところまで進める
    const auto lclick = [&](ImVec2 p) {
      io.AddMousePosEvent(p.x, p.y);
      h.frame();
      io.AddMouseButtonEvent(0, true);
      h.frame();
      io.AddMouseButtonEvent(0, false);
      h.frame(2);
    };
    const auto lastToast = [&]() { return h.c.toasts().empty() ? std::string() : h.c.toasts().back().text; };
    h.c.select(A);
    h.u.detailOpen = true;
    h.u.detailTab = 3;
    h.frame(4);
    check(h.u.linkTargetBox.second.x > h.u.linkTargetBox.first.x && h.u.linkAddBtn.second.x > h.u.linkAddBtn.first.x,
          "添付タブにリンクの節（入力欄と追加ボタン）が出る");
    check(h.u.linkRowsIssue == A && h.u.linkRows.empty(), "リンクがなければ行は 0");
    // 場所の入力欄に打って「リンクを追加」を押す（表示名の入力欄も使う）
    const auto addUi = [&](const std::string& target, const std::string& title) {
      lclick(center(h.u.linkTargetBox));
      check(io.WantTextInput, "場所の入力欄に入れる");
      io.AddInputCharactersUTF8(target.c_str());
      h.frame(2);
      if (!title.empty()) {
        lclick(center(h.u.linkTitleBox));
        io.AddInputCharactersUTF8(title.c_str());
        h.frame(2);
      }
      lclick(center(h.u.linkAddBtn));
      h.frame(3);
    };
    addUi("https://example.com/wiki/spec", "");
    check(h.c.linkCountOf(A) == 1 && h.c.linksOf(A)[0].target == "https://example.com/wiki/spec", "Web のリンクを追加できる");
    check(h.c.linksOf(A)[0].title == "example.com/wiki/spec", "表示名を省略すると URL の末尾から付く");
    check(h.u.linkTargetBuf.empty() && h.u.linkTitleBuf.empty() && h.u.linkError.empty(), "追加したら入力欄は空になる");
    check(h.u.linkRows.size() == 1, "追加した行が一覧に出る");
    addUi("\\\\server\\share\\docs\\spec.xlsx", "設計書");
    check(h.c.linkCountOf(A) == 2 && h.c.linksOf(A)[1].title == "設計書", "UNC のファイルを表示名つきで追加できる");
    addUi("\"S:\\案件\\共有\\\"", "");
    check(h.c.linkCountOf(A) == 3 && h.c.linksOf(A)[2].target == "S:\\案件\\共有\\" && h.c.linksOf(A)[2].title == "共有",
          "引用符つきのフォルダ（エクスプローラーの「パスのコピー」）を追加できる");
    // 断る: 許可リストにないスキーム（理由を出す。入力は残す。理由に入力の中身を入れない）
    addUi("javascript:alert(1)", "");
    check(h.c.linkCountOf(A) == 3, "javascript: は追加しない");
    check(!h.u.linkError.empty() && h.u.linkError.find("alert") == std::string::npos, "理由が入力欄の下に出る（中身は入れない）");
    check(h.u.linkTargetBuf == "javascript:alert(1)", "断ったときは入力を残す（直せる）");
    lclick(center(h.u.linkTargetBox));
    io.AddInputCharactersUTF8("x");
    h.frame(2);
    check(h.u.linkError.empty(), "入力し直すと理由は消える");
    lclick(center(h.u.linkTitleBox));  // 場所の入力欄を抜ける（入力中は外から値を入れても上書きされる）
    h.u.linkTargetBuf = "https://example.com/wiki/spec";  // 重複
    h.frame(2);
    lclick(center(h.u.linkAddBtn));
    h.frame(3);
    check(h.c.linkCountOf(A) == 3 && h.u.linkError.find("すでに") != std::string::npos, "同じ場所は重複して追加しない（理由つき）");
    h.u.linkTargetBuf.clear();
    h.u.linkError.clear();
    // Enter でも追加できる
    lclick(center(h.u.linkTargetBox));
    io.AddInputCharactersUTF8("http://intra.example/enter");
    h.frame(2);
    h.key(ImGuiKey_Enter);
    h.frame(2);
    check(h.c.linkCountOf(A) == 4, "入力欄で Enter を押しても追加できる");
    // 入力欄の中での Ctrl+V は、子課題の貼り付けを始めない（入力欄に貼り付くだけ）
    h.p.clip = "子A\n子B\n";
    lclick(center(h.u.linkTargetBox));
    h.key(ImGuiKey_V, true);
    check(h.u.dlg != ui::Dlg::PasteChildren, "リンクの入力欄で Ctrl+V しても、子課題の貼り付けのダイアログは開かない");
    h.u.linkTargetBuf.clear();
    h.p.clip = std::nullopt;
    // 4 件から 3 件にして（行を画面内に収める）、クリックで開く
    check(h.c.removeLink(A, 3) && h.c.linkCountOf(A) == 3, "リンクを外せる");
    h.frame(3);
    check(h.u.linkRows.size() == 3, "行が 3 つ");
    const auto rowAt = [&](std::size_t k) {
      const auto& r = h.u.linkRows[k];
      return ImVec2(r.first.x + 150, (r.first.y + r.second.y) / 2);
    };
    const std::size_t filesOpened0 = h.p.opened.size();
    lclick(rowAt(0));
    check(h.p.openedLinks.size() == 1 && h.p.openedLinks[0] == "https://example.com/wiki/spec", "行をクリックすると、場所をプラットフォームへ渡して開く");
    lclick(rowAt(1));
    check(h.p.openedLinks.size() == 2 && h.p.openedLinks[1] == "\\\\server\\share\\docs\\spec.xlsx", "UNC のファイルも開く（存在確認はしない）");
    check(h.p.opened.size() == filesOpened0, "リンクを開いても、添付の openFile は使わない");
    // 開けなかったら理由を通知する（場所は通知に入れない）
    h.p.linkOpenWhy = "見つかりません";
    lclick(rowAt(2));
    check(h.p.openedLinks.size() == 2 && lastToast().find("開けませんでした（見つかりません") != std::string::npos, "開けないときは理由を通知する");
    check(lastToast().find("共有") == std::string::npos, "通知に場所を入れない");
    h.p.linkOpenWhy.clear();
    // 許可リストにないリンクが（設定の書き換えなどで）あっても開かない
    {
      Link bad;
      bad.title = "壊れた";
      bad.target = "javascript:alert(1)";
      h.c.session()->links()[A].push_back(bad);
      h.c.session()->touchLinks();
      h.frame(3);
      check(h.u.linkRows.size() == 4, "壊れたリンクの行");
      lclick(rowAt(3));
      check(h.p.openedLinks.size() == 2 && lastToast().find("開けません") != std::string::npos, "許可リストにない場所は開かず、理由を通知する");
      check(lastToast().find("alert") == std::string::npos, "理由に中身を入れない");
      h.c.session()->links()[A].pop_back();
      h.c.session()->touchLinks();
      h.frame(3);
    }
    // 実行ファイルは添付と同じ警告（確認したら開く）
    {
      std::string why;
      check(h.c.addLink(A, "", "C:\\tools\\run.exe", &why), "実行ファイルのリンクを追加できる");
      h.frame(3);
      check(h.u.linkRows.size() == 4, "行が 4 つ");
      lclick(rowAt(3));
      check(h.u.dlg == ui::Dlg::LinkOpenWarn && h.p.openedLinks.size() == 2, "実行ファイルは開く前に警告する");
      h.frame(3);
      h.key(ImGuiKey_Escape);
      check(h.u.dlg == ui::Dlg::None && h.p.openedLinks.size() == 2, "警告をやめると開かない");
      h.c.select(A);
      h.frame(2);
      lclick(rowAt(3));
      h.frame(3);
      ImGuiWindow* w = ImGui::FindWindowByName("###dlg");
      check(h.u.dlg == ui::Dlg::LinkOpenWarn && w, "もう一度、警告が出る");
      if (w)
        for (float dy = 8; dy < 50 && h.u.dlg == ui::Dlg::LinkOpenWarn; dy += 4) lclick(ImVec2(w->Pos.x + w->Size.x - 45, w->Pos.y + w->Size.y - dy));
      h.frame(2);
      check(h.u.dlg == ui::Dlg::None && h.p.openedLinks.size() == 3 && h.p.openedLinks[2] == "C:\\tools\\run.exe", "「それでも開く」で開く");
      check(h.c.removeLink(A, 3), "実行ファイルのリンクを外す");
      h.frame(3);
    }
    // 右クリックのメニュー
    const auto rightClick = [&](ImVec2 p) {
      io.AddMousePosEvent(p.x, p.y);
      h.frame();
      io.AddMouseButtonEvent(1, true);
      h.frame();
      io.AddMouseButtonEvent(1, false);
      h.frame(2);
    };
    const auto menuItem = [&](const std::string& label) {
      for (const auto& it : h.u.linkCtxItems)
        if (it.label == label) return it;
      return ui::UiState::MenuItemGeom{label, ImVec2(-1, -1), ImVec2(-1, -1), false};
    };
    const auto pressItem = [&](const std::string& label) {
      const auto it = menuItem(label);
      check(it.a.x >= 0, ("メニューの項目: " + label).c_str());
      lclick(ImVec2((it.a.x + it.b.x) / 2, (it.a.y + it.b.y) / 2));
      h.frame(2);
    };
    rightClick(rowAt(1));
    check(h.u.linkCtxItems.size() == 7, "右クリックでメニューが開く（7 項目）");
    check(menuItem("場所（フォルダ）を開く").enabled, "ファイルのリンクでは「場所（フォルダ）を開く」が使える");
    check(menuItem("上へ").enabled && menuItem("下へ").enabled, "真ん中の行は上へも下へも動かせる");
    pressItem("場所（フォルダ）を開く");
    check(h.p.openedLinks.size() == 4 && h.p.openedLinks[3] == "\\\\server\\share\\docs\\", "親のフォルダを開く");
    rightClick(rowAt(0));
    check(!menuItem("場所（フォルダ）を開く").enabled && !menuItem("上へ").enabled, "Web のリンクでは親のフォルダはなく、先頭は上へ動かせない");
    pressItem("コピー");
    check(!h.p.clipSet.empty() && h.p.clipSet.back() == "https://example.com/wiki/spec", "「コピー」で場所をクリップボードへ");
    check(lastToast().find("example.com") == std::string::npos, "コピーの通知に場所を入れない");
    rightClick(rowAt(0));
    pressItem("開く");
    check(h.p.openedLinks.size() == 5 && h.p.openedLinks[4] == "https://example.com/wiki/spec", "メニューの「開く」");
    // 並べ替え（メニューの上へ・下へ）
    rightClick(rowAt(0));
    pressItem("下へ");
    check(h.c.linksOf(A)[0].title == "設計書" && h.c.linksOf(A)[1].target == "https://example.com/wiki/spec", "「下へ」で 1 つ下げる");
    rightClick(rowAt(1));
    pressItem("上へ");
    check(h.c.linksOf(A)[0].target == "https://example.com/wiki/spec", "「上へ」で元に戻る");
    // 編集: 入力欄に入れて、保存・やめる
    rightClick(rowAt(1));
    pressItem("編集");
    h.frame(2);
    check(h.u.linkEditing && h.u.linkTargetBuf == "\\\\server\\share\\docs\\spec.xlsx" && h.u.linkTitleBuf == "設計書", "編集で入力欄に入る");
    h.u.linkTitleBuf = "設計書 v2";
    h.frame(2);
    lclick(center(h.u.linkAddBtn));  // 「保存」
    h.frame(3);
    check(!h.u.linkEditing && h.c.linksOf(A)[1].title == "設計書 v2" && h.c.linkCountOf(A) == 3, "保存で表示名が直る（件数は変わらない）");
    check(h.c.linksOf(A)[1].target == "\\\\server\\share\\docs\\spec.xlsx", "場所は変わらない");
    rightClick(rowAt(1));
    pressItem("編集");
    h.frame(2);
    lclick(center(h.u.linkTitleBox));  // 編集を始めると場所の欄にフォーカスが入っている。抜けてから値を入れる
    h.u.linkTargetBuf = "javascript:1";
    h.frame(2);
    lclick(center(h.u.linkAddBtn));
    h.frame(3);
    check(h.u.linkEditing && !h.u.linkError.empty() && h.c.linksOf(A)[1].target == "\\\\server\\share\\docs\\spec.xlsx", "開けない場所には直せない（理由が出て、もとのまま）");
    lclick(center(h.u.linkCancelBtn));
    h.frame(3);
    check(!h.u.linkEditing && h.u.linkTargetBuf.empty() && h.u.linkError.empty(), "「やめる」で編集をやめる");
    // 削除
    rightClick(rowAt(2));
    pressItem("削除");
    check(h.c.linkCountOf(A) == 2 && h.c.linksOf(A)[1].title == "設計書 v2", "メニューの「削除」");
    h.frame(2);
    // 行のごみ箱
    h.c.addLink(A, "", "https://example.com/trash");
    h.frame(3);
    check(h.u.linkRows.size() == 3, "ごみ箱のテスト用の行");
    if (h.u.linkRows.size() == 3) {
      const auto& r = h.u.linkRows[2];
      lclick(ImVec2(r.second.x - 18, (r.first.y + r.second.y) / 2));
      h.frame(2);
      check(h.c.linkCountOf(A) == 2, "行のごみ箱でリンクを外せる");
    }
    // 貼り付けて追加（複数行。断った行は件数と理由だけを通知する）
    h.p.clip = "https://p.example/one\n\nC:\\paste\\two.txt\r\njavascript:alert(1)\n";
    h.frame(3);
    lclick(center(h.u.linkPasteBtn));
    h.frame(3);
    check(h.c.linkCountOf(A) == 4, "貼り付けて追加: 読める 2 行が追加される");
    check(lastToast().find("2 件のリンクを追加") != std::string::npos && lastToast().find("1 行は追加できません") != std::string::npos,
          "追加した件数と断った行数を通知する");
    check(lastToast().find("p.example") == std::string::npos && lastToast().find("alert") == std::string::npos, "通知に行の中身を入れない");
    h.p.clip = std::nullopt;
    lclick(center(h.u.linkPasteBtn));
    h.frame(3);
    check(h.c.linkCountOf(A) == 4 && lastToast().find("テキストがありません") != std::string::npos, "クリップボードが空なら何も追加せず知らせる");
    // 設定ファイルに書かれる（CSV は変わらない）
    h.clk.advanceMs(2000);
    h.frame(3);
    {
      std::string settings, csv;
      for (const auto& pi : h.c.projects())
        if (pi.name == "リンクの確認") {
          settings = h.fs.content("data/リンクの確認/project.settings.json");
          csv = h.fs.content("data/リンクの確認/project.csv");
        }
      check(settings.find("\"links\"") != std::string::npos && settings.find("https://example.com/wiki/spec") != std::string::npos,
            "project.settings.json の links に保存される");
      check(csv.find("example.com") == std::string::npos && csv.find("links") == std::string::npos, "CSV には入らない");
    }
    // 他の課題へ: ドラッグで移動（Ctrl でコピー）・同じ一覧の中で並べ替え
    h.c.removeLink(A, 3);
    h.c.removeLink(A, 2);
    h.frame(4);
    check(h.u.linkRows.size() == 2, "ドラッグのテスト用に 2 行");
    const auto targetMid = [&](const std::string& id) {
      for (const auto& t : h.u.dropTargets)
        if (t.id == id) return ImVec2((t.a.x + t.b.x) / 2, (t.a.y + t.b.y) / 2);
      return ImVec2(-1, -1);
    };
    const auto dragTo = [&](ImVec2 from, ImVec2 to, bool ctrl, bool release = true) {
      io.AddMousePosEvent(from.x, from.y);
      h.frame();
      io.AddMouseButtonEvent(0, true);
      h.frame();
      io.AddMousePosEvent(from.x + 12, from.y + 2);
      h.frame();
      io.AddMousePosEvent(to.x, to.y);
      h.frame(2);
      if (ctrl) io.AddKeyEvent(ImGuiMod_Ctrl, true);
      h.frame();
      if (release) {
        io.AddMouseButtonEvent(0, false);
        h.frame(2);
        io.AddKeyEvent(ImGuiMod_Ctrl, false);
        h.frame();
      }
    };
    const std::size_t opened0 = h.p.openedLinks.size();
    dragTo(rowAt(0), ImVec2(h.u.linkRows[1].first.x + 100, h.u.linkRows[1].second.y - 2), false);
    check(h.c.linksOf(A)[1].target == "https://example.com/wiki/spec" && h.c.linkCountOf(A) == 2, "同じ一覧の中でつかんで動かすと並べ替える");
    check(h.p.openedLinks.size() == opened0, "つかんで動かしたときは開かない");
    h.frame(3);
    dragTo(rowAt(0), targetMid(B), true);
    check(h.c.linkCountOf(A) == 2 && h.c.linkCountOf(B) == 1, "Ctrl を押して別の課題の行へ離すとコピー（元は残る）");
    h.frame(3);
    dragTo(rowAt(1), targetMid(B), false);
    check(h.c.linkCountOf(A) == 1 && h.c.linkCountOf(B) == 2, "別の課題の行へ離すと移動");
    h.frame(3);
    dragTo(rowAt(0), ImVec2(3, 3), false);
    check(h.c.linkCountOf(A) == 1 && h.u.dlg == ui::Dlg::None, "ドロップ先がない場所では何もしない");
    h.frame(3);
    dragTo(rowAt(0), targetMid(B), false, false);
    check(h.u.attachDrag.active && h.u.attachDrag.link, "つかんで動かすとドラッグが始まる（リンク）");
    h.key(ImGuiKey_Escape);
    check(!h.u.attachDrag.active, "Esc で取り消す");
    io.AddMouseButtonEvent(0, false);  // つかんだままのボタンを離す
    h.frame(3);
    // 添付とリンクが同じ添字にあっても、リンクの行の押下・ドラッグ・右クリックが添付に影響しない（行・ボタンの ID が別）
    {
      h.fs.put("ext/同じ添字.txt", "x");
      h.c.attachFiles(A, {"ext/同じ添字.txt"});
      h.c.select(A);
      h.u.detailTab = 3;
      h.frame(6);
      check(h.u.attachRows.size() == 1 && h.u.linkRows.size() == 1, "添付 1 件とリンク 1 件（同じ添字 0）");
      const std::size_t attBefore = h.c.attachmentCount(A);
      const std::size_t openedBefore = h.p.openedLinks.size(), filesBefore = h.p.opened.size();
      // リンクの行を押して動かす → リンクのドラッグ（添付のドラッグにならない）
      dragTo(rowAt(0), targetMid(B), false, false);
      check(h.u.attachDrag.active && h.u.attachDrag.link, "リンクの行をつかむと、リンクのドラッグになる");
      h.key(ImGuiKey_Escape);
      io.AddMouseButtonEvent(0, false);
      h.frame(3);
      check(!h.u.attachDrag.active && h.c.attachmentCount(A) == attBefore && h.c.linkCountOf(A) == 1, "取り消したら何も動かない");
      // 押して離す → リンクが開く（添付は開かない）
      lclick(rowAt(0));
      check(h.p.openedLinks.size() == openedBefore + 1 && h.p.opened.size() == filesBefore && h.u.dlg == ui::Dlg::None, "リンクの行のクリックは、リンクだけを開く");
      // 右クリック → リンクのメニュー（添付のドラッグは始まらない）
      rightClick(rowAt(0));
      check(h.u.linkCtxItems.size() == 7 && !h.u.attachDrag.active, "リンクの行の右クリックはリンクのメニューを開く");
      lclick(ImVec2(700, 880));  // メニューの外をクリックして閉じる
      h.frame(3);
      // 添付の行をつかむと、添付のドラッグ
      {
        const auto& r = h.u.attachRows[0];
        dragTo(ImVec2(r.first.x + 100, (r.first.y + r.second.y) / 2), targetMid(B), false, false);
        check(h.u.attachDrag.active && !h.u.attachDrag.link, "添付の行をつかむと、添付のドラッグになる");
        h.key(ImGuiKey_Escape);
        io.AddMouseButtonEvent(0, false);
        h.frame(3);
      }
      check(h.c.removeAttachment(A, 0), "確認用の添付を外す");
      h.frame(3);
    }
    // 別の課題を選ぶと、書きかけの入力は捨てる（書きかけの場所を別の課題に付けてしまわない）
    h.u.linkTargetBuf = "https://draft.example/";
    h.u.linkError = "x";
    h.c.select(B);
    h.u.detailTab = 3;  // 課題を切り替えると、詳細パネルは先頭のタブに戻る
    h.frame(3);
    check(h.u.linkTargetBuf.empty() && h.u.linkError.empty() && h.u.linkRowsIssue == B, "別の課題を選ぶと、書きかけの入力を捨てる");
    // バッジとツールチップ（一覧・ガントの左の表・ボード）。ボードは末端の課題だけがカードになるので、末端の課題 L を使う
    h.u.detailOpen = false;
    const std::string L = "7";
    for (int k = 0; k < 10; ++k) h.c.addLink(L, "リンク" + std::to_string(k), "https://tip.example/p" + std::to_string(k));
    check(h.c.linkCountOf(L) == 10, "ツールチップの確認用に 10 件にする");
    const auto badgeOf = [&](const std::string& id) {
      for (const auto& b : h.u.linkBadges)
        if (b.id == id) return b;
      return ui::UiState::DropTarget{ImVec2(-1, -1), ImVec2(-1, -1), ""};
    };
    const auto tooltipOn = [&] {
      ImGuiWindow* tip = ImGui::FindWindowByName("##Tooltip_00");
      return tip && tip->Active;
    };
    for (auto v : {app::View::List, app::View::Gantt, app::View::Board}) {
      h.c.setView(v);
      io.AddMousePosEvent(700, 880);
      for (int k = 0; k < 8; ++k) {
        io.AddMouseWheelEvent(0, 10);
        h.frame(2);
      }
      h.frame(20);
      const auto bb = badgeOf(L);
      const std::string vn = v == app::View::List ? "一覧" : (v == app::View::Gantt ? "ガント" : "ボード");
      check(bb.a.x >= 0, (vn + ": リンクのバッジが出る").c_str());
      if (bb.a.x < 0) continue;
      h.u.linkTipLines.clear();
      io.AddMousePosEvent((bb.a.x + bb.b.x) / 2, (bb.a.y + bb.b.y) / 2);
      h.frame(30);
      check(tooltipOn(), (vn + ": バッジに載せるとツールチップが出る").c_str());
      check(h.u.linkTipLines.size() == 8 && h.u.linkTipLines.front() == "リンク 10 件（クリックで一覧）" && h.u.linkTipLines[1] == "リンク0" &&
                h.u.linkTipLines[6] == "リンク5" && h.u.linkTipLines.back() == "ほか 4 件",
            (vn + ": 先頭の 6 件と「ほか 4 件」").c_str());
      io.AddMousePosEvent(700, 880);
      h.frame(5);
      check(!tooltipOn(), (vn + ": 外すと消える").c_str());
    }
    // バッジのクリックで、詳細パネルの添付タブが開く
    h.c.setView(app::View::List);
    h.u.detailOpen = false;
    h.c.clearSelection();
    io.AddMousePosEvent(700, 880);
    h.frame(20);
    {
      const auto bb = badgeOf(L);
      check(bb.a.x >= 0, "一覧に戻ってバッジが出る");
      lclick(ImVec2((bb.a.x + bb.b.x) / 2, (bb.a.y + bb.b.y) / 2));
      h.frame(3);
      check(h.u.detailOpen && h.u.linkRowsIssue == L, "バッジをクリックすると、その課題の添付タブ（リンクの節）が開く");
    }
    h.frame(3);
    // ダーク・コンパクト・高 DPI でも描ける。たくさんある課題・長い場所・日本語のパス
    {
      for (int k = 0; k < 45; ++k) h.c.addLink(L, "", "S:\\案件\\" + std::string(300, 'a') + std::to_string(k) + "\\仕様書.xlsx");
      check(h.c.linkCountOf(L) == 50, "1 課題 50 件まで");
      std::string why;
      check(!h.c.addLink(L, "", "https://over.example/", &why) && why.find("50") != std::string::npos, "51 件目は理由つきで断る");
      h.u.t = &ui::darkTokens();
      ui::applyStyle(*h.u.t, h.u.m);
      h.frame(5);
      h.u.m.compact = true;
      h.u.m.scale = 1.5f;
      ui::applyStyle(*h.u.t, h.u.m);
      h.frame(5);
      h.u.t = &ui::lightTokens();
      h.u.m.compact = false;
      h.u.m.scale = 1.0f;
      ui::applyStyle(*h.u.t, h.u.m);
      h.frame(5);
    }
    h.u.detailOpen = false;
    h.frame(2);
    h.c.deleteCurrentProject();
    h.frame(2);
    // 平置きのプロジェクト（添付は置けない）でも、リンクは付けられる（場所を指すだけでコピーしない）
    h.fs.put("data/平置きリンク.csv", "Issue Id,Summary\n1,a\n");
    h.c.syncFolder(true);
    for (const auto& pi : h.c.projects())
      if (pi.name == "平置きリンク") h.c.openProject(pi.id);
    h.c.setView(app::View::List);
    h.c.select("1");
    h.u.detailOpen = true;
    h.u.detailTab = 3;
    h.frame(4);
    check(!h.c.canAttach(), "平置きは添付できない");
    check(h.c.addLink("1", "", "https://flat.example/a"), "平置きでもリンクは付けられる");
    h.frame(3);
    check(h.u.linkRows.size() == 1, "平置きでもリンクの行が出る");
    h.u.detailOpen = false;
    h.frame(2);
    h.c.deleteCurrentProject();
    h.frame(2);
  }
  // GitHub の PR・Issue のリンク（Issue #61）: 専用アイコン・「GitHub の状態を確認」・状態の表示・完了の提案の通知と「完了にする」・
  // Undo 1 回で戻る・open が 1 件あれば提案なし・失敗は理由を通知して課題が変わらない・自動で完了にする設定・確認中の切り替え
  {
    const char* const kPr = "https://github.com/acme/secret-app/pull/12";
    const char* const kIs = "https://github.com/acme/secret-app/issues/3";
    h.c.createProject(app::SampleKind::Hierarchy, "GitHub の確認", "GHC");
    h.c.setView(app::View::List);
    h.u.detailOpen = false;
    h.c.clearSelection();
    h.frame(3);
    check(h.u.dropTargets.size() >= 3, "GitHub の確認: 行がある");
    const std::string A = h.u.dropTargets[0].id, B = h.u.dropTargets[1].id;
    ImGuiIO& io = ImGui::GetIO();
    const auto center = [](const std::pair<ImVec2, ImVec2>& r) { return ImVec2((r.first.x + r.second.x) / 2, (r.first.y + r.second.y) / 2); };
    const auto lclick = [&](ImVec2 p) {
      io.AddMousePosEvent(p.x, p.y);
      h.frame();
      io.AddMouseButtonEvent(0, true);
      h.frame();
      io.AddMouseButtonEvent(0, false);
      h.frame(2);
    };
    const auto rightClick = [&](ImVec2 p) {
      io.AddMousePosEvent(p.x, p.y);
      h.frame();
      io.AddMouseButtonEvent(1, true);
      h.frame();
      io.AddMouseButtonEvent(1, false);
      h.frame(2);
    };
    const auto toastWith = [&](NoticeAction a) -> const app::Toast* {
      for (const auto& t : h.c.toasts())
        if (t.action == a) return &t;
      return nullptr;
    };
    const auto anyToast = [&](const std::string& s) {
      for (const auto& t : h.c.toasts())
        if (t.text.find(s) != std::string::npos) return true;
      return false;
    };
    const auto clearToasts = [&]() {
      h.clk.advanceMs(60000);
      h.frame(2);
    };
    const auto rowAt = [&](std::size_t k) {
      const auto& r = h.u.linkRows[k];
      return ImVec2(r.first.x + 150, (r.first.y + r.second.y) / 2);
    };
    const auto issueOf = [&](const std::string& id) -> const Issue& { return *h.c.session()->issue(id); };
    const auto closedInfo = [](RemoteState s, const char* at) {
      RemoteInfo r;
      r.state = s;
      r.closedAt = at;
      return r;
    };
    const auto key = [](const char* url) { return parseGitHubUrl(url)->canonicalUrl(); };

    check(std::string(ui::linkIconName(LinkKind::GitHubPull, false)) == "gitpr" && std::string(ui::linkIconName(LinkKind::GitHubIssue, false)) == "gitissue",
          "GitHub の PR・Issue は専用のアイコン");
    check(ui::hasIcon("gitpr") && ui::hasIcon("gitissue"), "アイコンが定義されている");

    auto fake = std::make_shared<FakeRemoteStatus>();
    h.c.setRemoteStatus(fake, false);
    h.c.setUtcOffsetMinutes(0);
    h.c.select(A);
    h.u.detailOpen = true;
    h.u.detailTab = 3;
    h.frame(3);
    check(h.u.linkCheckBtn.second.x <= h.u.linkCheckBtn.first.x, "GitHub のリンクがなければ「GitHub の状態を確認」は出ない");
    check(h.c.addLink(A, "", kPr) && h.c.addLink(A, "設計の Issue", kIs) && h.c.addLink(A, "", "https://example.com/wiki"), "リンクを付ける");
    h.frame(3);
    check(h.u.linkRows.size() == 3, "行が 3 つ");
    check(h.u.linkCheckBtn.second.x > h.u.linkCheckBtn.first.x, "GitHub のリンクがあれば「GitHub の状態を確認」が出る");
    check(h.u.linkStatusShown.size() == 3 && h.u.linkStatusShown[0].empty() && h.u.linkStatusShown[1].empty() && h.u.linkStatusShown[2].empty(),
          "確認前は状態を出さない（起動時の自動確認はしない）");
    check(fake->calls() == 0, "押すまで取りに行かない");

    // 「GitHub の状態を確認」→ 状態の表示 → 通知 → 「完了にする」
    fake->set(key(kPr), closedInfo(RemoteState::Merged, "2026-10-15T03:00:00Z"));
    fake->set(key(kIs), closedInfo(RemoteState::ClosedCompleted, "2026-10-17T04:00:00Z"));
    lclick(center(h.u.linkCheckBtn));
    h.frame(3);
    check(fake->calls() == 2, "押すと GitHub の 2 件を取りに行く（GitHub 以外は取りに行かない）");
    check(h.u.linkStatusShown.size() == 3 && h.u.linkStatusShown[0] == "merged" && h.u.linkStatusShown[1] == "closed（完了）" && h.u.linkStatusShown[2].empty(),
          "状態のラベルが出る（色だけに頼らない）");
    const app::Toast* prop = toastWith(NoticeAction::Complete);
    check(prop != nullptr, "完了の提案の通知が出る");
    if (prop) {
      check(prop->text.find("のリンクがすべて閉じています。完了にしますか？") != std::string::npos, "提案の文");
      check(prop->text.find("acme") == std::string::npos && prop->text.find("github") == std::string::npos, "通知に URL・リポジトリ名を入れない");
    }
    check(issueOf(A).status != "Done" && !issueOf(A).resolved.has_value(), "押すまで課題は変わらない");
    check(h.u.toastActionBtn.second.x > h.u.toastActionBtn.first.x, "通知に「完了にする」ボタンが出る");
    const std::size_t undo0 = h.c.session()->undoSize();
    lclick(center(h.u.toastActionBtn));
    h.frame(2);
    check(issueOf(A).status == "Done" && issueOf(A).resolved.has_value() && fmtIso(*issueOf(A).resolved) == "2026-10-17",
          "「完了にする」で状況が完了になり、Resolved に閉じた日（最も遅い日）が入る");
    check(h.c.session()->undoSize() == undo0 + 1, "Undo の 1 手");
    check(toastWith(NoticeAction::Complete) == nullptr, "提案の通知は閉じる");
    const app::Toast* undoT = toastWith(NoticeAction::Undo);
    check(undoT != nullptr, "完了にした通知に「元に戻す」");
    if (undoT) h.c.runToastAction(undoT->id);
    h.frame(2);
    check(issueOf(A).status != "Done" && !issueOf(A).resolved.has_value(), "Undo 1 回で戻る");
    clearToasts();

    // マージなしで閉じた PR を含む → 文に明記。了承しなければ課題は変わらない。右クリックの「状態を確認」は、そのリンクだけ
    fake->set(key(kPr), closedInfo(RemoteState::ClosedUnmerged, "2026-10-15T03:00:00Z"));
    rightClick(rowAt(0));
    bool hasCheck = false;
    ui::UiState::MenuItemGeom checkItem;
    for (const auto& it : h.u.linkCtxItems)
      if (it.label == "状態を確認") {
        hasCheck = true;
        checkItem = it;
      }
    check(hasCheck && h.u.linkCtxItems.size() == 8, "GitHub のリンクの右クリックに「状態を確認」が出る");
    if (hasCheck) lclick(center({checkItem.a, checkItem.b}));
    h.frame(3);
    check(fake->calls() == 3, "メニューの「状態を確認」はそのリンクだけを取りに行く");
    check(h.u.linkStatusShown[0] == "closed（マージなし）", "マージなしの表示");
    clearToasts();
    lclick(center(h.u.linkCheckBtn));
    h.frame(3);
    prop = toastWith(NoticeAction::Complete);
    check(prop != nullptr && prop->text.find("（マージなしで閉じられたものを含みます）") != std::string::npos, "提案の文にマージなしを明記する");
    if (prop) h.c.dismissToast(prop->id);
    h.frame(2);
    check(issueOf(A).status != "Done", "了承しなければ課題は変わらない");
    rightClick(rowAt(2));
    bool webHasCheck = false;
    for (const auto& it : h.u.linkCtxItems)
      if (it.label == "状態を確認") webHasCheck = true;
    check(!webHasCheck && h.u.linkCtxItems.size() == 7, "GitHub 以外のリンクに「状態を確認」は出ない");
    for (const auto& it : h.u.linkCtxItems)
      if (it.label == "コピー") {  // メニューを閉じる（コピーするだけで、何も変えない）
        lclick(center({it.a, it.b}));
        break;
      }
    h.frame(3);
    clearToasts();

    // open が 1 件あれば提案しない
    fake->set(key(kPr), closedInfo(RemoteState::Merged, "2026-10-15T03:00:00Z"));
    RemoteInfo open;
    open.state = RemoteState::Open;
    fake->set(key(kIs), open);
    lclick(center(h.u.linkCheckBtn));
    h.frame(3);
    check(h.u.linkStatusShown[1] == "open" && h.u.linkStatusShown[0] == "merged", ("open と merged が混ざる: " + h.u.linkStatusShown[0] + "/" + h.u.linkStatusShown[1] + " calls=" + std::to_string(fake->calls())).c_str());
    check(toastWith(NoticeAction::Complete) == nullptr && issueOf(A).status != "Done", "open が 1 件あれば提案しない");
    RemoteInfo draft;
    draft.state = RemoteState::Draft;
    fake->set(key(kIs), closedInfo(RemoteState::ClosedNotPlanned, "2026-10-16T04:00:00Z"));
    fake->set(key(kPr), draft);
    lclick(center(h.u.linkCheckBtn));
    h.frame(3);
    check(h.u.linkStatusShown[0] == "draft" && h.u.linkStatusShown[1] == "closed（計画外）", "draft と closed（計画外）の表示");
    check(toastWith(NoticeAction::Complete) == nullptr, "draft があれば提案しない");
    clearToasts();

    // 失敗: 理由を通知して課題は変わらない。URL を理由に入れない
    fake->fail(key(kPr), "gh が見つかりません");
    lclick(center(h.u.linkCheckBtn));
    h.frame(3);
    check(anyToast("gh が見つかりません"), "失敗の理由を通知する");
    check(!anyToast("acme") && !anyToast("secret-app"), "理由に URL・リポジトリ名を入れない");
    check(h.u.linkStatusShown[0] == "確認できません", "失敗の表示");
    check(toastWith(NoticeAction::Complete) == nullptr && issueOf(A).status != "Done" && !issueOf(A).resolved.has_value(), "失敗しても課題は変わらない");
    clearToasts();

    // 自動で完了にする設定（既定はオフ）
    check(!h.c.prefs().autoCompleteGitHub, "自動で完了にする設定の既定はオフ");
    h.u.dlg = ui::Dlg::AppSettings;
    h.frame(3);
    h.u.dlg = ui::Dlg::None;
    h.frame(2);
    h.c.prefs().autoCompleteGitHub = true;
    fake->set(key(kPr), closedInfo(RemoteState::Merged, "2026-10-14T03:00:00Z"));
    fake->set(key(kIs), closedInfo(RemoteState::ClosedCompleted, "2026-10-16T04:00:00Z"));
    lclick(center(h.u.linkCheckBtn));
    h.frame(3);
    check(issueOf(A).status == "Done" && issueOf(A).resolved.has_value() && fmtIso(*issueOf(A).resolved) == "2026-10-16",
          "設定がオンなら、確認が終わった時点で自動で完了にする");
    undoT = toastWith(NoticeAction::Undo);
    check(undoT != nullptr && toastWith(NoticeAction::Complete) == nullptr, "通知に「元に戻す」");
    if (undoT) h.c.runToastAction(undoT->id);
    h.frame(2);
    check(issueOf(A).status != "Done" && !issueOf(A).resolved.has_value(), "自動の完了も Undo 1 回で戻る");
    h.c.prefs().autoCompleteGitHub = false;
    clearToasts();

    // 状態は保存しない（メモリ上のみ）
    h.c.flushNow();
    bool leaked = false;
    const std::string settings = h.fs.content("data/GitHub の確認/project.settings.json");
    if (settings.find("closedAt") != std::string::npos || settings.find("merged") != std::string::npos) leaked = true;
    check(!settings.empty() && !leaked, "確認結果を設定ファイルに保存しない");

    // ダークでも崩れない
    h.u.t = &ui::darkTokens();
    ui::applyStyle(*h.u.t, h.u.m);
    h.frame(3);
    check(h.u.linkStatusShown.size() == 3 && h.u.linkStatusShown[0] == "merged", "ダークでも状態を出す");
    h.u.t = &ui::lightTokens();
    ui::applyStyle(*h.u.t, h.u.m);
    h.frame(2);

    // 別スレッドで確認中に、別の課題・別のプロジェクトへ切り替えても落ちない
    auto slow = std::make_shared<FakeRemoteStatus>();
    slow->set(key(kPr), closedInfo(RemoteState::Merged, "2026-10-15T03:00:00Z"));
    slow->set(key(kIs), closedInfo(RemoteState::ClosedCompleted, "2026-10-17T04:00:00Z"));
    h.c.setRemoteStatus(slow, true);
    slow->hold(true);
    lclick(center(h.u.linkCheckBtn));
    for (int i = 0; i < 400 && slow->running() == 0; ++i) {
      h.frame();
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    check(slow->running() == 1 && h.c.remoteBusy(), "確認中（別のスレッドで 1 件）");
    h.frame(2);
    check(h.u.linkStatusShown.size() == 3 && h.u.linkStatusShown[0] == "確認中…" && h.u.linkStatusShown[1] == "確認中…", "確認中の表示");
    lclick(center(h.u.linkCheckBtn));
    check(slow->calls() == 1, "確認中は重ねて取りに行かない");
    h.c.select(B);  // 別の課題に切り替える
    h.frame(3);
    check(h.u.linkRows.empty(), "別の課題には GitHub のリンクがない");
    const std::string ghProject = h.c.currentId();
    h.c.createProject(app::SampleKind::Empty, "切り替え先", "SW");  // 別のプロジェクト
    h.frame(3);
    slow->release();
    for (int i = 0; i < 1000 && h.c.remoteBusy(); ++i) {
      h.frame();
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    check(!h.c.remoteBusy(), "確認が終わる");
    h.frame(3);
    check(toastWith(NoticeAction::Complete) == nullptr, "別のプロジェクトでは提案を出さない");
    check(h.c.openProject(ghProject), "元のプロジェクトに戻れる");
    h.c.select(A);
    h.u.detailOpen = true;
    h.u.detailTab = 3;
    h.frame(4);
    check(issueOf(A).status != "Done" && !issueOf(A).resolved.has_value(), "切り替え中の確認で、課題は変わらない");
    check(h.u.linkStatusShown.size() == 3 && h.u.linkStatusShown[0] == "merged", "戻ると結果が見える");
    // 窓口を外すと未確認に戻り、ボタンは押せない
    h.c.setRemoteStatus(nullptr);
    h.frame(3);
    check(h.u.linkCheckBtn.second.x > h.u.linkCheckBtn.first.x, "窓口がなくてもボタンの場所はある（押せない）");
    lclick(center(h.u.linkCheckBtn));
    check(!h.c.remoteAvailable() && h.c.remoteViewOf(kPr).phase == app::AppController::RemoteView::Phase::None, "窓口を外すと未確認に戻る");
    h.u.detailOpen = false;
    h.frame(2);
  }
  // 通知の折り返し: 高さを測るときの幅と、描くときの幅が同じこと（折り返しの境目で 1 行増えて、末尾が切れない）。
  // 文字の左の余白（12）のぶん、描画のほうが狭いので、測るときも同じだけ引く。単語の長さを少しずつ変えて、境目を何度も通る
  {
    const auto dismissAll = [&]() {
      for (const auto& t : std::vector<app::Toast>(h.c.toasts())) h.c.dismissToast(t.id);
    };
    int checked = 0, bad = 0, tallest = 0;
    for (const NoticeAction act : {NoticeAction::None, NoticeAction::OpenFolder}) {
      for (int n = 6; n <= 70; ++n) {
        dismissAll();
        std::string text;
        for (int i = 0; i < n; ++i) text += std::string(static_cast<std::size_t>(2 + (i * 7) % 9), static_cast<char>('a' + i % 26)) + " ";
        h.c.toast(text, act, act == NoticeAction::OpenFolder ? "out" : "");
        h.frame(2);
        if (h.c.toasts().empty()) continue;
        const std::string name = "##toast" + std::to_string(h.c.toasts().back().id);
        ImGuiWindow* w = ImGui::FindWindowByName(name.c_str());
        if (!w) continue;
        ++checked;
        // 内容の下端（上の余白を含む）と下の余白が、窓に収まっていること
        // 1 行の通知は、閉じるボタンの分だけもともと 2px ほど超える。行が 1 つ増えた（13px 以上）ときは超えすぎ
        if (w->ContentSize.y + 2 * w->WindowPadding.y > w->Size.y + 3.0f) ++bad;
        tallest = std::max(tallest, static_cast<int>(w->Size.y));
      }
    }
    dismissAll();
    check(checked > 100 && tallest > 60, "通知の折り返しの確認: 長い文を何通りも出した");
    check(bad == 0, "通知の文字が、折り返しの境目でも窓の高さに収まる（末尾が切れない）");
  }
  // 統計ビュー: 担当者別の負荷（週×担当のヒートマップ）と完了の推移
  {
    const std::string csvText =
        "Issue Id,Issue Type,Summary,Parent,Assignee,Status,Priority,Resolved,Duration (weeks),Outward issue link (Blocks)\n"
        "1,Task,設計,,田中,To Do,High,,2,\n"
        "2,Task,実装,,田中,To Do,Medium,,1,\n"
        "3,Task,評価,,鈴木,In Progress,Medium,,3,\n"
        "4,Task,調整,,,To Do,Low,,1,\n"
        "5,Task,調査,,鈴木,Done,Medium,2026-10-13,1,\n"
        "6,Task,準備,,田中,Done,Medium,2026-10-06,2,\n"
        "7,Task,下見,,,Done,Low,2026-09-22,1,\n";
    check(h.c.importText(csvText, "統計の確認", app::ImportMode::New).ok, "統計の確認用の取り込み");
    h.u.t = &ui::lightTokens();
    h.u.m.compact = false;
    h.u.m.scale = 1.0f;
    ui::applyStyle(*h.u.t, h.u.m);
    h.c.filter() = Filter{};
    h.c.setView(app::View::Stats);
    h.clk.advanceMs(15000);  // 取り込みの通知（左下）が消えるまで待つ。マウスの当たりを邪魔する
    h.frame(3);
    check(h.u.statsLoadRows == 3, "負荷の行（田中・鈴木・担当なし）");
    check(h.u.statsDoneRows == 3, "完了の行（田中・鈴木・担当なし）");
    ImGuiIO& io = ImGui::GetIO();
    const auto findCell = [&](const std::string& who, int week) -> const ui::UiState::StatsLoadCell* {
      for (const auto& c : h.u.statsLoadCells)
        if (c.who == who && c.week == week && c.a.y > 120 && c.b.y < 860) return &c;  // ウィンドウの中に完全に見えるもの
      return nullptr;
    };
    io.AddMousePosEvent(700, 450);
    for (int k = 0; k < 60 && !(findCell("田中", 0) && findCell("", 0)); ++k) {  // ヒートマップが見える位置までスクロール
      io.AddMouseWheelEvent(0, -3);
      h.frame(2);
    }
    const auto* tanaka = findCell("田中", 0);
    check(tanaka != nullptr, "ヒートマップのセルが見える");
    if (tanaka) {
      check(tanaka->percent == 200, "同じ週に 2 件が重なる田中の初週は 200%（設計 5 日 + 実装 5 日 ÷ 営業日 5）");
      const ImVec2 mid((tanaka->a.x + tanaka->b.x) / 2, (tanaka->a.y + tanaka->b.y) / 2);
      io.AddMousePosEvent(mid.x, mid.y);
      h.frame(3);
      ImGuiWindow* tip = ImGui::FindWindowByName("##Tooltip_00");
      check(tip && tip->Active, "セルにマウスを載せると内訳のツールチップが出る");
      io.AddMousePosEvent(5, 5);
      h.frame(2);
      tip = ImGui::FindWindowByName("##Tooltip_00");
      check(!(tip && tip->Active), "マウスを外すとツールチップが消える");
      h.click(mid.x, mid.y);
      h.frame(2);
      check(h.c.view() == app::View::List, "セルを押すとリストが開く");
      check(h.c.filter().asg == AsgFilter::Name && h.c.filter().asgName == "田中", "セルを押すとその担当者で絞る");
    }
    // 担当なしの行 → 「担当なし」で絞る
    h.c.filter() = Filter{};
    h.c.setView(app::View::Stats);
    h.frame(3);
    io.AddMousePosEvent(700, 450);
    for (int k = 0; k < 60 && !findCell("", 0); ++k) {
      io.AddMouseWheelEvent(0, -3);
      h.frame(2);
    }
    if (const auto* none = findCell("", 0)) {
      const ImVec2 mid((none->a.x + none->b.x) / 2, (none->a.y + none->b.y) / 2);
      io.AddMousePosEvent(mid.x, mid.y);
      h.frame(2);
      h.click(mid.x, mid.y);
      h.frame(2);
      check(h.c.view() == app::View::List && h.c.filter().asg == AsgFilter::Unassigned, "担当なしの行を押すと未割当で絞る");
    } else {
      check(false, "担当なしの行が見える");
    }
    // ダーク・コンパクト・高DPI でも描ける
    h.u.t = &ui::darkTokens();
    h.u.m.compact = true;
    h.u.m.scale = 1.5f;
    ui::applyStyle(*h.u.t, h.u.m);
    h.c.setView(app::View::Stats);
    h.frame(4);
    h.u.t = &ui::lightTokens();
    h.u.m.compact = false;
    h.u.m.scale = 1.0f;
    ui::applyStyle(*h.u.t, h.u.m);
    // 作業がすべて完了: 負荷は空（説明の一文）、完了だけ出る
    const std::string doneOnly =
        "Issue Id,Issue Type,Summary,Parent,Assignee,Status,Priority,Resolved,Duration (weeks),Outward issue link (Blocks)\n"
        "1,Task,完了だけ,,田中,Done,Medium,2026-10-13,1,\n";
    check(h.c.importText(doneOnly, "完了だけ", app::ImportMode::New).ok, "完了だけの取り込み");
    h.c.setView(app::View::Stats);
    h.frame(3);
    check(h.u.statsLoadRows == 0 && h.u.statsLoadCells.empty(), "未完了がなければ負荷の行は出ない");
    check(h.u.statsDoneRows == 1, "完了だけでも完了の行は出る");
    // 完了日がない・範囲外の完了だけ: 完了の行は出ない
    const std::string noResolved =
        "Issue Id,Issue Type,Summary,Parent,Assignee,Status,Priority,Resolved,Duration (weeks),Outward issue link (Blocks)\n"
        "1,Task,日付なし,,田中,To Do,Medium,,1,\n"
        "2,Task,古い完了,,田中,Done,Medium,2025-01-06,1,\n";
    check(h.c.importText(noResolved, "完了なし", app::ImportMode::New).ok, "完了なしの取り込み");
    h.c.setView(app::View::Stats);
    h.frame(3);
    check(h.u.statsLoadRows == 1 && h.u.statsDoneRows == 0, "過去 8 週に完了がなければ完了の行は出ない");
    h.c.deleteCurrentProject();
    h.c.deleteCurrentProject();
    h.c.deleteCurrentProject();
    h.u.t = &ui::darkTokens();
    ui::applyStyle(*h.u.t, h.u.m);
    h.frame(2);
  }
  // 統計ビューの「推移」（Issue #54 の A）: バーンダウン・リードタイム・滞留日数、ホバー、滞留の行 → 詳細、担当者・ルート課題の絞り込み
  {
    const std::string csvText =
        "Issue Id,Issue Type,Summary,Parent,Assignee,Status,Priority,Created,Updated,Resolved,Duration (weeks)\n"
        "1,Epic,基盤,,,,High,2026-09-01,,,\n"
        "2,Task,設計,1,田中,Done,High,2026-09-01,2026-09-30,2026-09-30,1\n"
        "3,Task,実装,1,田中,In Progress,Medium,2026-09-08,2026-10-14,,2\n"
        "4,Task,評価,1,鈴木,To Do,Medium,2026-09-15,2026-09-16,,1\n"
        "5,Task,調整,,鈴木,To Do,Low,2026-10-12,,,1\n"
        "6,Task,資料,,,Done,Low,2026-10-01,2026-10-13,2026-10-13,1\n"
        "7,Task,検証,,田中,Done,Medium,2026-10-05,2026-10-12,2026-10-12,1\n"
        "8,Task,下見,,田中,To Do,Low,,,,1\n";
    check(h.c.importText(csvText, "推移の確認", app::ImportMode::New).ok, "推移の確認用の取り込み");
    h.u.t = &ui::lightTokens();
    h.u.m.compact = false;
    h.u.m.scale = 1.0f;
    ui::applyStyle(*h.u.t, h.u.m);
    h.c.filter() = Filter{};
    h.c.clearSelection();
    h.u.detailOpen = false;
    h.c.setView(app::View::Stats);
    h.clk.advanceMs(15000);
    h.frame(3);
    auto& st = h.u.statsTrend;
    ImGuiIO& io = ImGui::GetIO();
    check(st.drawn, "統計ビューに推移が描かれる");
    check(st.items == 7, "推移の対象は作業 7 件（親とマイルストーンを除く）");
    check(st.weeks == 8, "週は 8/31 から基準日の週まで 8 つ");
    check(st.forecastOk, "ペース予測が出る（直近 4 週に完了のある週が 2 つ）");
    check(st.leadN == 3, "リードタイムは完了 3 件");
    check(st.agingN == 3, "滞留は未完了で日付のある 3 件（日付のない 1 件は数えない）");
    check(st.agingRows.size() == 3 && st.agingRows[0].id == "4" && st.agingRows[1].id == "5" && st.agingRows[2].id == "3",
          "滞留は長い順（評価 → 調整 → 実装）");
    // ホバー: 週ごとの値
    const auto colsVisible = [&] {
      return !st.weekCols.empty() && st.weekCols.front().a.y > 130 && st.weekCols.front().b.y < 800;
    };
    io.AddMousePosEvent(700, 450);
    for (int k = 0; k < 120 && !colsVisible(); ++k) {
      io.AddMouseWheelEvent(0, -1);
      h.frame(2);
    }
    check(colsVisible(), "週ごとのグラフが見える位置までスクロールできる");
    if (colsVisible()) {
      const auto& col = st.weekCols[0];
      io.AddMousePosEvent((col.a.x + col.b.x) / 2, (col.a.y + col.b.y) / 2);
      h.frame(3);
      ImGuiWindow* tip = ImGui::FindWindowByName("##Tooltip_00");
      check(tip && tip->Active, "週の上にマウスを載せると値のツールチップが出る");
      io.AddMousePosEvent(5, 5);
      h.frame(2);
      tip = ImGui::FindWindowByName("##Tooltip_00");
      check(!(tip && tip->Active), "マウスを外すとツールチップが消える");
    }
    // 担当者・ルート課題の選択（この節の中）。選択欄を押すと一覧が開き、項目を選ぶと絞り込まれる
    const auto whoVisible = [&] { return st.whoA.y > 130 && st.whoB.y < 800; };
    io.AddMousePosEvent(700, 450);
    for (int k = 0; k < 60 && !whoVisible(); ++k) {
      io.AddMouseWheelEvent(0, 1);
      h.frame(2);
    }
    check(whoVisible(), "担当者の選択欄が見える");
    if (whoVisible()) {
      h.click((st.whoA.x + st.whoB.x) / 2, (st.whoA.y + st.whoB.y) / 2);
      h.frame(2);
      ImGuiWindow* pop = ImGui::FindWindowByName("##Combo_00");
      check(pop && pop->Active, "担当者の選択欄を押すと一覧が開く");
      if (pop && pop->Active) {
        // 一覧は 全員・担当なし・田中・鈴木 の順。鈴木（4 番目）を押す
        const float lh = ImGui::GetTextLineHeight(), sp = ImGui::GetStyle().ItemSpacing.y;
        const float y = pop->Pos.y + ImGui::GetStyle().WindowPadding.y + 3 * (lh + sp) + lh / 2;
        h.click(pop->Pos.x + 30, y);
        h.frame(3);
        check(st.who == TrendFilter::Who::Name && st.name == "鈴木", "一覧から担当者を選べる");
        check(st.items == 2, "鈴木の作業は 2 件（評価・調整）");
      }
    }
    h.c.filter() = Filter{};  // 上の絞り込みには連動しない（推移の選択だけ）
    h.frame(2);
    check(st.items == 2, "上のバーの絞り込みを変えても推移の選択は変わらない");
    st.who = TrendFilter::Who::Name;
    st.name = "田中";
    h.frame(3);
    check(st.items == 4 && st.leadN == 2 && st.agingN == 1, "田中: 作業 4 件・完了 2 件・滞留 1 件");
    st.rootId = "1";
    h.frame(3);
    check(st.items == 2, "田中 × ルート課題「基盤」は 2 件");
    st.rootId.clear();
    st.standalone = true;
    st.who = TrendFilter::Who::Unassigned;
    h.frame(3);
    check(st.items == 1 && st.leadN == 1, "担当なし × 親のない単独の課題は 1 件（資料）");
    st.who = TrendFilter::Who::Name;
    st.name = "いない人";
    h.frame(3);
    check(st.drawn && st.items == 0 && st.weekCols.empty() && st.agingRows.empty(), "該当がなければ案内を出す（落ちない）");
    st.who = TrendFilter::Who::All;
    st.standalone = false;
    h.frame(3);
    check(st.items == 7, "絞り込みを戻すと 7 件に戻る");

    // 滞留の行を押すと、その課題を選んで詳細を開く
    const auto agingVisible = [&] {
      if (st.agingRows.size() != 3) return false;
      for (const auto& r : st.agingRows)
        if (r.a.y < 130 || r.b.y > 860) return false;
      return true;
    };
    io.AddMousePosEvent(700, 450);
    for (int k = 0; k < 120 && !agingVisible(); ++k) {
      io.AddMouseWheelEvent(0, -2);
      h.frame(2);
    }
    check(agingVisible(), "滞留の一覧が見える位置までスクロールできる");
    if (agingVisible()) {
      const auto row = st.agingRows[1];  // 調整（5）
      const ImVec2 mid((row.a.x + row.b.x) / 2, (row.a.y + row.b.y) / 2);
      io.AddMousePosEvent(mid.x, mid.y);
      h.frame(3);
      ImGuiWindow* tip = ImGui::FindWindowByName("##Tooltip_00");
      check(tip && tip->Active, "滞留の行にマウスを載せると説明が出る");
      h.click(mid.x, mid.y);
      h.frame(3);
      check(h.c.selection().size() == 1 && h.c.selection()[0] == row.id, "滞留の行を押すとその課題が選ばれる");
      check(h.u.detailOpen, "滞留の行を押すと詳細パネルが開く");
      check(h.c.view() == app::View::Stats, "統計ビューのまま（詳細は横に開く）");
    }
    h.u.detailOpen = false;
    h.c.clearSelection();
    // ダーク・コンパクト・高DPI でも描ける
    h.u.t = &ui::darkTokens();
    h.u.m.compact = true;
    h.u.m.scale = 1.5f;
    ui::applyStyle(*h.u.t, h.u.m);
    h.frame(4);
    check(st.drawn && st.items == 7, "ダーク・コンパクト・高DPI でも推移が描かれる");
    h.u.t = &ui::lightTokens();
    h.u.m.compact = false;
    h.u.m.scale = 1.0f;
    ui::applyStyle(*h.u.t, h.u.m);
    // 日付の列がない CSV: 落ちずに案内と注記を出す
    const std::string noCols =
        "Issue Id,Issue Type,Summary,Parent,Assignee,Status,Priority,Resolved,Duration (weeks)\n"
        "1,Task,日付なし,,田中,To Do,Medium,,1\n"
        "2,Task,完了,,田中,Done,Medium,2026-10-13,1\n";
    check(h.c.importText(noCols, "列なし", app::ImportMode::New).ok, "列なしの取り込み");
    h.c.setView(app::View::Stats);
    h.frame(3);
    check(st.drawn && st.items == 2 && st.weeks == 0 && st.leadN == 0 && st.agingN == 0 && !st.forecastOk, "Created 列がなければ図は出さず、注記で案内する");
    check(st.who == TrendFilter::Who::All && st.rootId.empty(), "プロジェクトを切り替えると推移の選択は初期化される");
    // 空のプロジェクト: 統計ビューは案内だけで、推移は描かない
    h.c.createProject(app::SampleKind::Empty, "空", "EMP");
    h.c.setView(app::View::Stats);
    h.frame(3);
    check(!st.drawn && st.weekCols.empty() && st.agingRows.empty(), "空のプロジェクトでは推移を描かない（落ちない）");
    // 幅が狭い（ウィンドウが小さく、詳細パネルも開いている）とき: 履歴 27 週 + 予測 26 週の列が幅に収まり、最後の列もホバーできる
    {
      std::string wide = "Issue Id,Issue Type,Summary,Parent,Assignee,Status,Priority,Created,Updated,Resolved,Duration (weeks)\n";
      wide += "d1,Task,完了1,,田中,Done,Medium,2026-04-20,,2026-10-06,1\n";
      wide += "d2,Task,完了2,,田中,Done,Medium,2026-04-20,,2026-10-13,1\n";
      wide += "d3,Task,完了3,,田中,Done,Medium,2026-04-20,,2026-10-14,1\n";
      for (int i = 0; i < 30; ++i)
        wide += "o" + std::to_string(i) + ",Task,未完了" + std::to_string(i) + ",,鈴木,To Do,Medium," + fmtIso(*parseIso("2026-04-21") + i * 5) + ",,,1\n";
      check(h.c.importText(wide, "狭い幅", app::ImportMode::New).ok, "狭い幅の確認用の取り込み");
      h.c.setView(app::View::Stats);
      h.clk.advanceMs(15000);
      ImGuiIO& io2 = ImGui::GetIO();
      io2.DisplaySize = ImVec2(900, 900);
      h.u.detailOpen = true;
      h.frame(4);
      check(h.u.statsTrend.drawn && h.u.statsTrend.weeks == 27 && h.u.statsTrend.forecastOk, "27 週の履歴と予測が出る");
      const auto lastVisible = [&] {
        const auto& c = h.u.statsTrend.lastCol;
        return c.a.y > 130 && c.b.y < 800;
      };
      io2.AddMousePosEvent(300, 450);
      for (int k = 0; k < 20; ++k) {  // 前のプロジェクトのスクロール位置が残っているので、先頭へ戻してから下る
        io2.AddMouseWheelEvent(0, 20);
        h.frame(1);
      }
      for (int k = 0; k < 120 && !lastVisible(); ++k) {
        io2.AddMouseWheelEvent(0, -1);
        h.frame(2);
      }
      check(lastVisible(), "狭い幅でもグラフが見える位置までスクロールできる");
      const auto& lc = h.u.statsTrend.lastCol;
      check(lc.b.x <= h.u.statsTrend.chartX1 + 0.5f, "最後の列（予測の終点）が図の幅に収まる");
      check(h.u.statsTrend.chartX1 < 700, "図は詳細パネルの左に収まる");
      check(h.u.statsTrend.weekCols.size() == 27, "過去の週は 27 列");
      io2.AddMousePosEvent((lc.a.x + lc.b.x) / 2, (lc.a.y + lc.b.y) / 2);
      h.frame(3);
      ImGuiWindow* tip2 = ImGui::FindWindowByName("##Tooltip_00");
      check(tip2 && tip2->Active, "狭い幅でも最後の列（予測の週）にマウスを載せると値が出る");
      io2.AddMousePosEvent(5, 5);
      h.frame(2);
      io2.DisplaySize = ImVec2(1440, 900);
      h.u.detailOpen = false;
      h.frame(3);
    }
    h.c.deleteCurrentProject();
    h.c.deleteCurrentProject();
    h.c.deleteCurrentProject();
    h.c.deleteCurrentProject();
    h.u.t = &ui::darkTokens();
    ui::applyStyle(*h.u.t, h.u.m);
    h.frame(2);
  }
  // アプリで新規作成 → 課題を作る → 編集 → 完了 までで、統計の推移（バーンダウン・リードタイム・滞留日数）が出る（Issue #66）
  {
    const auto goTo = [&](int y, int m, int d) { h.clk.set(LocalTime{y, m, d, 9, 0, 0}); };
    goTo(2026, 10, 12);
    h.u.t = &ui::lightTokens();
    h.u.m.compact = false;
    h.u.m.scale = 1.0f;
    ui::applyStyle(*h.u.t, h.u.m);
    h.c.filter() = Filter{};
    h.c.clearSelection();
    h.u.detailOpen = false;
    h.c.createProject(app::SampleKind::Empty, "作成日の記録", "STP");
    Session* ss = h.c.session();
    check(ss != nullptr && ss->issues().empty(), "作成日の確認用の空のプロジェクト");
    const std::string epic = h.c.createIssue("", "");
    const std::string ta = h.c.createIssue(epic, "");
    const std::string tb = h.c.createIssue(epic, ta);
    const std::string tc = h.c.createIssue(epic, tb);
    check(!epic.empty() && !ta.empty() && !tb.empty() && !tc.empty(), "課題を作れる");
    const auto cellOf = [&](const std::string& id, const char* header) -> std::string {
      const auto& hd = ss->meta().extraHeaders;
      const Issue* o = ss->issue(id);
      for (std::size_t i = 0; o && i < hd.size(); ++i)
        if (hd[i] == header) return i < o->extra.size() ? o->extra[i] : std::string();
      return "<列なし>";
    };
    check(cellOf(ta, "Created") == "2026-10-12" && cellOf(tc, "Created") == "2026-10-12" && cellOf(epic, "Created") == "2026-10-12",
          "作った課題に作成日（その日）が入る");
    check(cellOf(ta, "Updated") == "<列なし>", "作っただけでは更新日の列は作らない");
    // 編集 → 完了
    goTo(2026, 10, 14);
    ss->setBaseDate(h.clk.todayIso());
    check(ss->setName(tb, "編集した課題"), "件名を編集");
    check(cellOf(tb, "Updated") == "2026-10-14" && cellOf(tc, "Updated") == "", "編集した課題だけに更新日が入る");
    goTo(2026, 10, 19);
    ss->setBaseDate(h.clk.todayIso());
    h.c.setStatusFor({ta}, "Done");
    check(ss->issue(ta)->resolved && fmtIso(*ss->issue(ta)->resolved) == "2026-10-19", "完了にすると完了日が入る");
    check(cellOf(ta, "Updated") == "2026-10-19", "完了にした課題の更新日");
    check(cellOf(ta, "Created") == "2026-10-12", "編集しても作成日は変わらない");
    h.c.setView(app::View::Stats);
    h.frame(3);
    auto& st = h.u.statsTrend;
    check(st.drawn && st.items == 3, "作成日を自動で記録したプロジェクトの推移が描かれる（作業 3 件）");
    check(st.weeks == 2, "バーンダウンは作成した週から基準日の週まで 2 週");
    check(st.leadN == 1, "リードタイム: 完了にした 1 件");
    check(st.agingN == 2, "滞留日数: 未完了の 2 件（更新日・作成日から）");
    check(st.agingRows.size() == 2 && st.agingRows[0].id == tc, "滞留は作成のままの課題が最も長い");
    {
      auto tr = computeTrends(*ss->tree(), ss->viewIssues(), ss->meta(), *ss->rows(), ss->calendar(), ss->baseDay());
      check(tr.ok() && tr->hasCreatedColumn && tr->hasUpdatedColumn, "Created・Updated の列がある");
      bool noCol = false;
      if (tr.ok())
        for (const auto& n : trendNotes(*tr)) noCol |= n.find("列がない") != std::string::npos;
      check(!noCol, "「CSV に Created の列がない」の注記は出ない");
    }
    // Undo で作成日・更新日も戻る
    check(ss->undo(), "元に戻す");
    check(!ss->issue(ta)->resolved && cellOf(ta, "Updated") == "" && cellOf(ta, "Created") == "2026-10-12",
          "元に戻すと完了日と更新日も戻り、作成日は残る");
    h.c.deleteCurrentProject();
    h.frame(2);
  }
  // 統計ビューの「状況の件数の推移（累積フロー）」（Issue #64）: 案内・図・ホバー・記録なし・「いま記録する」・開いたときの自動記録・狭い幅
  {
    const auto settingsPathOf = [&](const char* folder) {
      std::string found;
      for (const std::string& p : h.fs.paths())
        if (p.find(folder) != std::string::npos && p.size() > 21 && p.compare(p.size() - 21, 21, "project.settings.json") == 0) found = p;
      return found;
    };
    const auto goTo = [&](int y, int m, int d) { h.clk.set(LocalTime{y, m, d, 9, 0, 0}); };
    const std::string csvText =
        "Issue Id,Issue Type,Summary,Parent,Assignee,Status,Priority,Resolved,Duration (weeks)\n"
        "1,Epic,基盤,,,,High,,\n"
        "2,Task,設計,1,田中,Done,High,2026-10-13,1\n"
        "3,Task,実装,1,田中,In Progress,Medium,,2\n"
        "4,Task,評価,1,鈴木,To Do,Medium,,1\n"
        "5,Task,調整,,鈴木,To Do,Low,,1\n"
        "6,Milestone,節目,,,,Low,,0\n";
    goTo(2026, 10, 19);
    h.u.t = &ui::lightTokens();
    h.u.m.compact = false;
    h.u.m.scale = 1.0f;
    ui::applyStyle(*h.u.t, h.u.m);
    h.c.filter() = Filter{};
    h.c.clearSelection();
    h.u.detailOpen = false;
    // 作業のない新規プロジェクトに課題を足す: 開いたときは記録されず、記録 0 件の案内が出る
    h.c.createProject(app::SampleKind::Empty, "累積フロー空", "CFE");
    check(h.c.session() && h.c.session()->history().empty(), "作業のない新規プロジェクトには自動で記録しない");
    h.c.session()->addIssue("", "", "A");
    h.c.session()->addIssue("", "", "B");
    h.c.afterEdit();
    h.c.setView(app::View::Stats);
    h.clk.advanceMs(15000);  // 作成の通知（左下）が消えてから押す
    h.frame(4);
    auto& cf = h.u.statsCfd;
    check(cf.drawn && cf.recorded == 0 && cf.pending && !cf.chart, "記録が 0 件なら「記録が溜まるとここに出ます（いま 0 週ぶん）」の案内だけを出す");
    // 「いま記録する」を押すと 1 件になる（まだ案内。図は 2 点から）
    const auto recVisible = [&] { return cf.recA.y > 130 && cf.recB.y < 860; };
    const auto scrollToRec = [&] {
      ImGuiIO& io = ImGui::GetIO();
      io.AddMousePosEvent(700, 450);
      for (int k = 0; k < 20 && cf.recA.y < 130; ++k) {
        io.AddMouseWheelEvent(0, 20);
        h.frame(1);
      }
      for (int k = 0; k < 200 && !recVisible(); ++k) {
        io.AddMouseWheelEvent(0, -1);
        h.frame(2);
      }
    };
    scrollToRec();
    check(recVisible(), "「いま記録する」ボタンが見える位置までスクロールできる");
    if (recVisible()) {
      h.click((cf.recA.x + cf.recB.x) / 2, (cf.recA.y + cf.recB.y) / 2);
      h.frame(3);
      check(h.c.session()->history().points.size() == 1, "「いま記録する」で今週の点が 1 つ入る");
      check(cf.recorded == 1 && cf.pending && !cf.chart, "記録が 1 件なら案内だけで図は描かない（いま 1 週ぶん）");
      check(!h.c.toasts().empty() && h.c.toasts().back().text.find("記録しました") != std::string::npos, "記録したことを通知する");
      // もう一度押しても、同じ週なら点は増えない（変わりがなければそう知らせる）
      h.clk.advanceMs(15000);
      h.frame(2);
      scrollToRec();
      if (recVisible()) {
        h.click((cf.recA.x + cf.recB.x) / 2, (cf.recA.y + cf.recB.y) / 2);
        h.frame(3);
        check(h.c.session()->history().points.size() == 1, "同じ週にもう一度押しても点は増えない");
        check(h.c.toasts().back().text.find("変わりはありません") != std::string::npos, "変わりがなければそう知らせる");
      }
    }
    h.c.deleteCurrentProject();

    // 複数の週: 開いたときの自動記録（同じ週は設定ファイルを書き換えない）、週をまたぐと追加、間が空けば「記録なし」
    check(h.c.importText(csvText, "累積フロー", app::ImportMode::New).ok, "累積フロー用の取り込み");
    const std::string id = h.c.currentId();
    check(h.c.session()->history().points.size() == 1, "取り込んで開いたとき、今週の点が自動で入る");
    h.c.setView(app::View::Stats);
    h.frame(3);
    h.clk.advanceMs(1000);
    h.c.tick();
    h.clk.advanceMs(1000);
    h.c.tick();
    const std::string sp = settingsPathOf("累積フロー");
    check(!sp.empty(), "設定ファイルの場所");
    const std::string before = sp.empty() ? std::string() : h.fs.content(sp);
    const int writes = h.fs.writeCount();
    check(before.find("\"history\"") != std::string::npos, "設定ファイルに履歴が書かれる");
    check(h.c.openProject(id), "同じ週にもう一度開く");
    h.frame(3);
    h.clk.advanceMs(1000);
    h.c.tick();
    h.clk.advanceMs(1000);
    h.c.tick();
    check(h.c.session()->history().points.size() == 1, "同じ週に開き直しても点は増えない");
    check(h.fs.writeCount() == writes && (sp.empty() || h.fs.content(sp) == before), "同じ週に開き直しても設定ファイルは書き換えない（内容も書き込み回数も同じ）");
    goTo(2026, 10, 26);
    check(h.c.openProject(id), "翌週に開く");
    check(h.c.session()->history().points.size() == 2, "週が変わったら開いたとき 1 点足す");
    h.frame(3);
    check(cf.drawn && cf.recorded == 2 && cf.chart && cf.weeks == 2 && cf.gaps == 0, "2 週ぶんあれば図が描かれる");
    goTo(2026, 11, 9);  // 11/2 の週は開いていない
    check(h.c.openProject(id), "2 週あとに開く");
    h.frame(3);
    check(cf.chart && cf.recorded == 3 && cf.weeks == 4 && cf.gaps == 1 && cf.cols.size() == 4, "間の 1 週は「記録なし」として列に残る");

    // ホバー: 記録のある週・ない週とも値（または「記録なし」）のツールチップが出る
    ImGuiIO& io = ImGui::GetIO();
    const auto colsVisible = [&] { return cf.cols.size() == 4 && cf.cols.front().a.y > 130 && cf.cols.front().b.y < 860; };
    io.AddMousePosEvent(700, 450);
    for (int k = 0; k < 20; ++k) {
      io.AddMouseWheelEvent(0, 20);
      h.frame(1);
    }
    for (int k = 0; k < 200 && !colsVisible(); ++k) {
      io.AddMouseWheelEvent(0, -1);
      h.frame(2);
    }
    check(colsVisible(), "累積フロー図が見える位置までスクロールできる");
    if (colsVisible()) {
      for (int i : {0, 2, 3}) {  // 2 番目（添字 2）が記録なし
        const auto& col = cf.cols[static_cast<std::size_t>(i)];
        io.AddMousePosEvent((col.a.x + col.b.x) / 2, (col.a.y + col.b.y) / 2);
        h.frame(3);
        ImGuiWindow* tip = ImGui::FindWindowByName("##Tooltip_00");
        check(tip && tip->Active, i == 2 ? "記録のない週にマウスを載せると「記録なし」が出る" : "週の上にマウスを載せると 3 つの値が出る");
        io.AddMousePosEvent(5, 5);
        h.frame(2);
      }
      check(cf.cols[2].a.x > cf.cols[1].a.x && cf.chartX1 >= cf.cols.back().b.x - 0.5f, "列は左から順で、図の幅に収まる");
    }
    // ダーク・コンパクト・高DPI でも描ける
    h.u.t = &ui::darkTokens();
    h.u.m.compact = true;
    h.u.m.scale = 1.5f;
    ui::applyStyle(*h.u.t, h.u.m);
    h.frame(4);
    check(cf.chart && cf.recorded == 3, "ダーク・コンパクト・高DPI でも累積フロー図が描かれる");
    h.u.t = &ui::lightTokens();
    h.u.m.compact = false;
    h.u.m.scale = 1.0f;
    ui::applyStyle(*h.u.t, h.u.m);

    // 幅が狭い（ウィンドウが小さく、詳細パネルも開いている）とき: 104 週ぶんの列が幅に収まり、最後の列もホバーできる
    for (int i = 0; i < 110; ++i) {
      const Day d = *parseIso("2026-11-16") + i * 7;
      const Ymd ymd = civilFromDays(d);
      goTo(ymd.y, ymd.m, ymd.d);
      h.c.recordHistory(app::AppController::HistoryTrigger::Manual);
    }
    check(h.c.session()->history().points.size() == kHistoryMaxWeeks, "上限 104 週を超えたら古いものから落ちる");
    {
      ImGuiIO& io2 = ImGui::GetIO();
      io2.DisplaySize = ImVec2(900, 900);
      h.u.detailOpen = true;
      h.frame(4);
      check(cf.chart && cf.weeks == static_cast<int>(kHistoryMaxWeeks) && cf.cols.size() == kHistoryMaxWeeks, "104 週の列がすべて描かれる");
      check(cf.chartX1 < 700, "図は詳細パネルの左に収まる");
      const auto lastVisible = [&] { return !cf.cols.empty() && cf.cols.back().a.y > 130 && cf.cols.back().b.y < 860; };
      io2.AddMousePosEvent(300, 450);
      for (int k = 0; k < 20; ++k) {
        io2.AddMouseWheelEvent(0, 20);
        h.frame(1);
      }
      for (int k = 0; k < 200 && !lastVisible(); ++k) {
        io2.AddMouseWheelEvent(0, -1);
        h.frame(2);
      }
      check(lastVisible(), "狭い幅でも図が見える位置までスクロールできる");
      if (lastVisible()) {
        check(cf.cols.back().b.x <= cf.chartX1 + 0.5f && cf.cols.front().a.x >= 0, "最後の列が図の幅に収まる");
        const auto& lc = cf.cols.back();
        io2.AddMousePosEvent((lc.a.x + lc.b.x) / 2, (lc.a.y + lc.b.y) / 2);
        h.frame(3);
        ImGuiWindow* tip = ImGui::FindWindowByName("##Tooltip_00");
        check(tip && tip->Active, "狭い幅でも最後の列にマウスを載せると値が出る");
        io2.AddMousePosEvent(5, 5);
        h.frame(2);
      }
      io2.DisplaySize = ImVec2(1440, 900);
      h.u.detailOpen = false;
      h.frame(3);
    }
    goTo(2026, 10, 19);
    h.c.deleteCurrentProject();
    h.u.t = &ui::darkTokens();
    ui::applyStyle(*h.u.t, h.u.m);
    h.frame(2);
  }
  // 5,000 件で日付つきの推移（集計は毎フレーム。重くならない）
  {
    std::string text = "Issue Id,Issue Type,Summary,Parent,Assignee,Status,Priority,Created,Updated,Resolved,Duration (weeks)\n";
    const Day base = *parseIso("2026-10-19");
    const char* const who[] = {"田中", "鈴木", "佐藤", ""};
    for (int i = 0; i < 5000; ++i) {
      const Day created = base - 400 + (i * 37) % 380;
      const bool done = i % 3 == 0;
      text += std::to_string(i + 1) + ",Task,課題" + std::to_string(i + 1) + ",," + who[i % 4] + "," + (done ? "Done" : "To Do") + ",Medium," + fmtIso(created) + "," +
              fmtIso(created + (i * 11) % 300) + "," + (done ? fmtIso(created + i % 40) : std::string()) + ",1\n";
    }
    check(h.c.importText(text, "推移の大きい", app::ImportMode::New).ok, "5000件（日付つき）の取り込み");
    h.c.setView(app::View::Stats);
    h.clk.advanceMs(15000);
    h.frame(3);
    auto& st = h.u.statsTrend;
    check(st.drawn && st.items == 5000, "5000件でも推移を集計する");
    check(st.weeks == 27 && st.agingRows.size() == 10, "5000件: 27 週と滞留の上位 10 件");
    const auto t0 = std::chrono::steady_clock::now();
    h.frame(10);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / 10;
    std::fprintf(stderr, "ui_smoke: 5000件の統計ビュー（推移つき） 1フレーム %.1f ms\n", ms);
#ifdef ROADMAP_SANITIZED
    check(ms < 400, "5000件の統計ビュー（推移つき）が重くない（サニタイザ付き）");
#else
    check(ms < 100, "5000件の統計ビュー（推移つき）が重くない");
#endif
    h.c.deleteCurrentProject();
    h.c.setView(app::View::List);
    h.frame(2);
  }
  // HTML に書き出す（Issue #42）: コマンドパレット → ダイアログ → 保存ダイアログ（スタブ）→ IFileStore に書く → 通知
  {
    h.c.createProject(app::SampleKind::Hierarchy, "報告/テスト", "RPT");
    h.c.setView(app::View::Gantt);
    h.c.setUi("zoom", "month");
    h.c.setUi("colorBy", "float");
    h.c.clearSelection();
    h.frame(3);
    const auto dlgWindow = [&]() { return ImGui::FindWindowByName("###dlg"); };
    // 右下の主ボタン（位置は字の幅に左右されるので、窓の中を縦に探す）を押す。押せたら（ダイアログが閉じたら）true
    const auto pressPrimary = [&]() {
      ImGuiWindow* w = dlgWindow();
      if (!w) return false;
      for (float dy = 8; dy < 50 && h.u.dlg == ui::Dlg::ExportHtml; dy += 4) h.click(w->Pos.x + w->Size.x - 45, w->Pos.y + w->Size.y - dy);
      h.frame(2);
      return h.u.dlg != ui::Dlg::ExportHtml;
    };
    const auto openDlg = [&]() {
      h.u.paletteOpen = true;
      h.u.paletteFocus = true;
      h.u.paletteQuery = "HTML";
      h.frame(3);
      h.key(ImGuiKey_Enter);
      h.frame(3);
    };
    openDlg();
    check(h.u.dlg == ui::Dlg::ExportHtml, "コマンドパレットの「HTML に書き出す…」でダイアログが開く");
    check(!h.u.paletteOpen, "パレットは閉じる");
    check(h.u.rep.zoom == "month" && h.u.rep.colorBy == "float", "拡大率と色分けは今の画面から始める");
    check(!h.u.rep.dark && h.u.rep.gantt && h.u.rep.stats && h.u.rep.list, "既定は 3 つともライト");
    // すべての選択の組み合わせで描ける
    for (int bits = 0; bits < 8; ++bits) {
      h.u.rep.gantt = bits & 1;
      h.u.rep.stats = bits & 2;
      h.u.rep.list = bits & 4;
      h.u.repScope = bits % 2;
      h.frame(2);
    }
    h.u.rep.gantt = h.u.rep.stats = h.u.rep.list = true;
    h.u.repScope = 0;
    h.frame(2);
    // 何も選ばないと主ボタンは押せない（閉じない・保存ダイアログも出さない）
    h.u.rep.gantt = h.u.rep.stats = h.u.rep.list = false;
    h.frame(2);
    check(!pressPrimary(), "書き出す内容が空なら主ボタンは押せない");
    check(h.p.saveAsked.empty(), "このとき保存ダイアログは出さない");
    h.u.rep.gantt = h.u.rep.stats = h.u.rep.list = true;
    // 課題を 1 つも選んでいなければ「選んだ課題の配下」は使えない
    h.u.repScope = 2;
    h.frame(2);
    check(h.u.repScope == 0, "選択がなければ配下の範囲に出来ない");
    // 保存ダイアログをやめたら何も書かない
    h.p.pickedSave.reset();
    check(h.fs.makeDirs("out").ok(), "保存先のフォルダ");
    const auto files0 = h.fs.paths().size();
    check(pressPrimary(), "主ボタンでダイアログが閉じる");
    check(h.p.saveAsked.size() == 1 && h.p.saveAsked[0] == "報告_テスト_2026-10-19.html|html", "保存ダイアログの既定の名前は <プロジェクト名>_<日付>.html");
    check(h.fs.paths().size() == files0, "やめたら何も書かない");
    // 保存先を選ぶと IFileStore に書く
    h.p.pickedSave = "out/報告.html";
    openDlg();
    check(h.u.dlg == ui::Dlg::ExportHtml, "もう一度開く");
    h.u.rep.dark = true;
    check(pressPrimary(), "書き出す");
    check(h.fs.exists("out/報告.html"), "選んだ保存先に書く");
    if (h.fs.exists("out/報告.html")) {
      const std::string& html = h.fs.content("out/報告.html");
      check(html.compare(0, 15, "<!doctype html>") == 0 && html.find("color-scheme:dark") != std::string::npos, "HTML（ダークを選んだ）");
      check(html.find("<h1>報告/テスト</h1>") != std::string::npos && html.find("<script") == std::string::npos, "プロジェクト名が入り、スクリプトはない");
      check(html.find("拡大率: 月") != std::string::npos, "画面の拡大率が入る");
    }
    check(!h.c.toasts().empty() && h.c.toasts().back().action == NoticeAction::OpenFolder && h.c.toasts().back().arg == "out", "通知に「フォルダを開く」");
    // 通知の「フォルダを開く」を押す（位置は通知の幅に左右されるので、横に探す）
    h.frame(2);
    {
      const std::size_t n0 = h.p.openedFolders.size();
      // いちばん下の通知（8 + 40 の間隔で積む）の中段にボタンがある
      for (float x = 100; x < 700 && h.p.openedFolders.size() == n0; x += 5) h.click(x, 856);
      check(h.p.openedFolders.size() == n0 + 1 && h.p.openedFolders.back() == "out", "「フォルダを開く」で保存先のフォルダを開く");
      h.frame(2);
      check(h.c.toasts().empty() || h.c.toasts().back().action != NoticeAction::OpenFolder, "押したら通知は閉じる");
    }
    // 失敗（書き込めない）は通知で知らせ、落とさない・古いファイルを壊さない
    const std::string before = h.fs.content("out/報告.html");
    h.p.pickedSave = "out/報告.html";
    h.fs.setFailWrites(true);
    openDlg();
    check(pressPrimary(), "書けなくてもダイアログは閉じる");
    h.fs.setFailWrites(false);
    check(!h.c.toasts().empty() && h.c.toasts().back().text.find("書き出せません") == 0, "失敗の理由を通知する");
    check(h.fs.content("out/報告.html") == before, "古いファイルは壊さない");
    // .html でない保存先は断る
    h.p.pickedSave = "out/報告.exe";
    openDlg();
    check(pressPrimary(), "拡張子が違う保存先");
    check(!h.fs.exists("out/報告.exe") && h.c.toasts().back().text.find("書き出せません") == 0, "拡張子が違えば書かない");
    // 選んだ課題の配下・今の表示のまま
    h.c.select("11");
    h.p.pickedSave = "out/配下.html";
    openDlg();
    h.frame(2);
    h.u.repScope = 2;
    h.frame(2);
    check(h.u.repScope == 2, "課題を 1 つ選べば配下の範囲にできる");
    check(pressPrimary() && h.fs.exists("out/配下.html"), "配下を書き出す");
    if (h.fs.exists("out/配下.html")) check(h.fs.content("out/配下.html").find("とその配下") != std::string::npos, "範囲の注記");
    h.c.filter().q = "ZZZ-なし";
    h.p.pickedSave = "out/絞込.html";
    openDlg();
    h.frame(2);
    h.u.repScope = 1;
    check(pressPrimary() && h.fs.exists("out/絞込.html"), "今の表示のまま書き出す");
    if (h.fs.exists("out/絞込.html")) check(h.fs.content("out/絞込.html").find("表示範囲: 画面の表示のまま") != std::string::npos, "絞り込みの注記");
    h.c.filter() = {};
    // 閉じるボタン・Esc で何も書かない
    h.p.saveAsked.clear();
    openDlg();
    h.key(ImGuiKey_Escape);
    check(h.u.dlg == ui::Dlg::None && h.p.saveAsked.empty(), "Esc で閉じる");
    // ダイアログを開いたまま課題を消しても落ちない（描画のあとで書く）
    h.p.pickedSave = "out/消去.html";
    openDlg();
    h.c.deleteSelection();
    h.frame(2);
    pressPrimary();
    h.frame(2);
    h.u.dlg = ui::Dlg::None;
    h.frame(2);
    h.c.clearSelection();
    h.c.deleteCurrentProject();
    h.frame(2);
  }
  // 週次レポート（Issue #43）: コマンドパレット → ダイアログ（プレビュー・期間・項目・所感）→ コピー・保存（スタブ）→ 通知
  {
    h.c.createProject(app::SampleKind::Hierarchy, "週報/テスト", "WK");
    h.c.setView(app::View::Gantt);
    h.c.clearSelection();
    h.p.saveAsked.clear();
    h.p.pickedSave.reset();
    h.frame(3);
    const auto dlgWindow = [&]() { return ImGui::FindWindowByName("###dlg"); };
    const auto openWeekly = [&]() {
      h.u.paletteOpen = true;
      h.u.paletteFocus = true;
      h.u.paletteQuery = "週次";
      h.frame(3);
      h.key(ImGuiKey_Enter);
      h.frame(3);
    };
    openWeekly();
    check(h.u.dlg == ui::Dlg::Weekly, "コマンドパレットの「週次レポート…」でダイアログが開く");
    check(!h.u.paletteOpen, "パレットは閉じる");
    check(h.u.wkPreview.find("# 週報/テスト 週次レポート") == 0, "プレビューに Markdown の下書きが出る");
    check(h.u.wkPreview.find("## サマリー") != std::string::npos && h.u.wkPreview.find("## 今週の完了") != std::string::npos, "既定は今週・全項目");
    check(h.u.wkError.empty(), "エラーなし");
    check(h.u.wk.notes.empty() && h.c.weeklyNotes().empty(), "所感は空から始まる");
    // 週の頭の自動スナップショット（Issue #70）: 週報を一度も保存していなくても「先週から変わったこと」が出る
    check(h.c.session()->weekly().snaps.empty() && h.c.session()->weeklyAuto().snaps.size() == 1, "週報は未保存で、開いたとき週の頭が自動で入っている");
    check(h.u.wkPreview.find("## 先週から変わったこと") != std::string::npos && h.u.wkPreview.find("に自動で記録した週の頭の状態との比較です。") != std::string::npos,
          "週報を保存していなくても「先週から変わったこと」が出る（今週の頭と今）");
    check(h.u.wkPreview.find("### 新しく完了した課題（0 件）") != std::string::npos, "開いた直後は何も変わっていない");
    {
      // 未完了の作業を 1 つ完了にする → 「新しく完了した課題」に出る
      Session* ws = h.c.session();
      std::string pick;
      for (int id : ws->tree()->leafOrder) {
        const std::size_t k = static_cast<std::size_t>(id);
        if (ws->tree()->kind[k] == Kind::Ms || ws->rows()->at(k).done) continue;
        pick = ws->tree()->ids[k];
        break;
      }
      check(!pick.empty(), "未完了の作業がある");
      if (!pick.empty()) {
        ws->setStatus(pick, "Done");
        ws->setField(pick, [&](Issue& is) { is.resolved = parseIso("2026-10-19"); });
        h.c.afterEdit();
        h.frame(2);
        check(h.u.wkPreview.find("### 新しく完了した課題（1 件）") != std::string::npos, "今週の頭のあとに完了した課題が出る");
        check(h.c.session()->weekly().snaps.empty(), "プレビューでは手動のスナップショットを覚えない");
        ws->undo();
        h.c.afterEdit();
        h.frame(2);
        check(h.u.wkPreview.find("### 新しく完了した課題（0 件）") != std::string::npos, "元に戻すと出なくなる");
        check(h.c.session()->weeklyAuto().snaps.size() == 1, "元に戻しても自動のスナップショットは残る（取り消しの対象外）");
      }
    }
    // 期間の切り替え
    h.u.wkPeriod = 1;
    h.frame(2);
    check(h.u.wkPreview.find("## 先週の完了") != std::string::npos && h.u.wkPreview.find("## 今週の予定") != std::string::npos, "先週に切り替わる");
    h.u.wkPeriod = 2;
    h.u.wkFrom = "2026-10-01";
    h.u.wkTo = "2026-10-10";
    h.frame(2);
    check(h.u.wkPreview.find("## 期間内の完了") != std::string::npos, "任意の範囲");
    check(h.u.wkPreview.find("2026/10/1(木) 〜 2026/10/10(土)") != std::string::npos, "入力した範囲");
    h.u.wkTo = "あいう";  // 壊れた日付: 落ちず、今週の範囲を使う
    h.frame(2);
    check(h.u.wkPreview.find("## 期間内の完了") != std::string::npos && h.u.wkPreview.find("2026/10/19(月) 〜 2026/10/25(日)") != std::string::npos,
          "壊れた日付は今週の範囲にする");
    h.u.wkFrom = "2026-10-20";
    h.u.wkTo = "2026-10-01";  // 逆順
    h.frame(2);
    check(h.u.wkPreview.find("2026/10/1(木) 〜 2026/10/20(火)") != std::string::npos, "逆の範囲は入れ替える");
    h.u.wkPeriod = 0;
    h.frame(2);
    // 項目ごとのオン・オフ
    h.u.wk.sections.done = false;
    h.u.wk.sections.risks = false;
    h.frame(2);
    check(h.u.wkPreview.find("## 今週の完了") == std::string::npos && h.u.wkPreview.find("## 遅れ・余裕小") == std::string::npos, "オフにした項目は出ない");
    check(h.u.wkPreview.find("## 進行中") != std::string::npos, "ほかは残る");
    h.u.wk.sections.done = true;
    h.u.wk.sections.risks = true;
    // 所感を書くとプレビューに出る
    h.u.wk.notes = "- 来週は結合試験\n- 課題 <未定>";
    h.frame(2);
    check(h.u.wkPreview.find("- 来週は結合試験") != std::string::npos && h.u.wkPreview.find("\\<未定>") != std::string::npos, "所感がプレビューに出る");

    // ボタンの位置は字の幅に左右されるので、窓の下の行を探して押す。右から: Markdown をコピー・Markdown を保存…・HTML を保存…・閉じる
    const auto btnEffect = [&](float x, float y, const std::function<bool()>& effect) {
      h.click(x, y);
      return effect();
    };
    ImGuiWindow* w = dlgWindow();
    check(w != nullptr, "ダイアログの窓");
    float rowY = -1, rowDy = 0, xMd = -1, xHtml = -1;
    if (w) {
      const std::size_t clips0 = h.p.clipSet.size();
      for (float dy = 8; dy < 50 && rowY < 0 && h.u.dlg == ui::Dlg::Weekly; dy += 4)
        if (btnEffect(w->Pos.x + w->Size.x - 45, w->Pos.y + w->Size.y - dy, [&] { return h.p.clipSet.size() > clips0; })) {
          rowY = w->Pos.y + w->Size.y - dy;
          rowDy = dy;
        }
      check(rowY > 0, "「Markdown をコピー」を押せる");
      check(h.p.clipSet.size() == clips0 + 1, "クリップボードに書く（1回）");
      if (h.p.clipSet.size() > clips0) {
        const std::string& md = h.p.clipSet.back();
        check(md == h.u.wkPreview, "コピーした内容はプレビューと同じ");
        check(md.find("# 週報/テスト 週次レポート") == 0, "コピーした Markdown");
      }
      check(h.u.dlg == ui::Dlg::Weekly, "コピーしてもダイアログは開いたまま");
      check(!h.c.toasts().empty() && h.c.toasts().back().text.find("コピーしました") != std::string::npos, "コピーの通知");
      check(h.c.weeklyNotes() == h.u.wk.notes, "コピーすると所感をプロジェクトに覚える");
      check(h.c.session()->weekly().snaps.size() == 1, "コピーするとスナップショットを覚える");
      // 左へ探す
      const std::size_t saved0 = h.p.saveAsked.size();
      for (float x = w->Pos.x + w->Size.x - 12 - 150; x > w->Pos.x + 100 && xMd < 0; x -= 4)
        if (btnEffect(x, rowY, [&] { return h.p.saveAsked.size() > saved0; })) xMd = x;
      check(xMd > 0, "「Markdown を保存…」を押せる");
      check(h.p.saveAsked.size() == saved0 + 1 && h.p.saveAsked.back() == "週報_テスト_週報_2026-10-19.md|md", "保存ダイアログの既定の名前は <プロジェクト名>_週報_<月曜>.md");
      const std::size_t saved1 = h.p.saveAsked.size();
      for (float x = xMd - 4; x > w->Pos.x + 60 && xHtml < 0; x -= 4)
        if (btnEffect(x, rowY, [&] { return h.p.saveAsked.size() > saved1 && h.p.saveAsked.back().find("|html") != std::string::npos; })) xHtml = x;
      check(xHtml > 0, "「HTML を保存…」を押せる");
      check(h.p.saveAsked.back() == "週報_テスト_週報_2026-10-19.html|html", "HTML の既定の名前");
      check(h.u.dlg == ui::Dlg::Weekly, "保存ダイアログをやめてもダイアログは開いたまま");
    }
    // 保存先を選ぶと IFileStore に書く
    check(h.fs.makeDirs("out").ok(), "保存先のフォルダ");
    if (rowY > 0 && xMd > 0 && xHtml > 0) {
      h.p.pickedSave = "out/週報.md";
      h.click(xMd, rowY);
      check(h.fs.exists("out/週報.md"), "Markdown を選んだ保存先に書く");
      if (h.fs.exists("out/週報.md")) check(h.fs.content("out/週報.md") == h.u.wkPreview, "保存した内容はプレビューと同じ");
      check(!h.c.toasts().empty() && h.c.toasts().back().action == NoticeAction::OpenFolder && h.c.toasts().back().arg == "out", "通知に「フォルダを開く」");
      h.p.pickedSave = "out/週報.html";
      h.click(xHtml, rowY);
      check(h.fs.exists("out/週報.html"), "HTML を選んだ保存先に書く");
      if (h.fs.exists("out/週報.html")) {
        const std::string& html = h.fs.content("out/週報.html");
        check(html.compare(0, 15, "<!doctype html>") == 0 && html.find("<script") == std::string::npos, "HTML（スクリプトなし）");
        check(html.find("&lt;未定&gt;") != std::string::npos, "所感の < はエスケープされる");
      }
      // 失敗（書き込めない）は通知で知らせ、落とさない・古いファイルを壊さない
      const std::string before = h.fs.content("out/週報.md");
      h.u.wk.notes = "書き換えた所感";
      h.frame(2);
      h.p.pickedSave = "out/週報.md";
      h.fs.setFailWrites(true);
      h.click(xMd, rowY);
      h.fs.setFailWrites(false);
      check(!h.c.toasts().empty() && h.c.toasts().back().text.find("書き出せません") == 0, "失敗の理由を通知する");
      check(h.fs.content("out/週報.md") == before, "古いファイルは壊さない");
      // 拡張子が違う保存先は断る
      h.p.pickedSave = "out/週報.exe";
      h.click(xMd, rowY);
      check(!h.fs.exists("out/週報.exe") && h.c.toasts().back().text.find("書き出せません") == 0, "拡張子が違えば書かない");
      h.p.pickedSave.reset();
      // クリップボードに書けないときは通知する（覚えない）
      h.p.clipFail = true;
      const std::size_t clips1 = h.p.clipSet.size();
      h.u.wk.notes = "コピー失敗の所感";
      h.click(w->Pos.x + w->Size.x - 45, rowY);
      h.p.clipFail = false;
      check(h.p.clipSet.size() == clips1, "書けなければ何も入らない");
      check(!h.c.toasts().empty() && h.c.toasts().back().text.find("コピーできませんでした") != std::string::npos, "コピー失敗の通知");
      // 項目が全部オフならコピーしない
      h.u.wk.sections = WeeklySections{};
      h.u.wk.sections.summary = h.u.wk.sections.done = h.u.wk.sections.progress = h.u.wk.sections.next = h.u.wk.sections.risks = false;
      h.u.wk.sections.milestones = h.u.wk.sections.assignees = h.u.wk.sections.changes = h.u.wk.sections.notes = false;
      h.frame(2);
      h.click(w->Pos.x + w->Size.x - 45, w->Pos.y + w->Size.y - rowDy);  // 所感の欄が消えて窓が縮むので、下端からの位置で押す
      check(h.u.dlg == ui::Dlg::Weekly, "押してもダイアログは開いたまま");
      check(h.p.clipSet.size() == clips1, "項目が空ならコピーしない");
      h.u.wk.sections = WeeklySections{};
      h.frame(2);
    }
    // Esc で閉じると、所感をプロジェクトに覚える
    h.u.wk.notes = "閉じるときの所感";
    h.frame(2);
    h.key(ImGuiKey_Escape);
    check(h.u.dlg == ui::Dlg::None, "Esc で閉じる");
    check(h.c.weeklyNotes() == "閉じるときの所感", "閉じるときに所感を覚える");
    // 開き直すと前回の所感が下書きに残る
    openWeekly();
    check(h.u.dlg == ui::Dlg::Weekly && h.u.wk.notes == "閉じるときの所感", "前回の所感が下書きに残る");
    check(h.u.wkPreview.find("閉じるときの所感") != std::string::npos, "プレビューにも出る");
    // ダイアログを開いたまま課題を消しても落ちない（描画のあとで書く）
    h.c.select("11");
    h.c.deleteSelection();
    h.frame(3);
    check(h.u.dlg == ui::Dlg::Weekly && h.u.wkError.empty(), "課題を消してもプレビューは作り直される");
    h.u.dlg = ui::Dlg::None;
    h.frame(2);
    // 「…」メニューにも項目がある（無効にならない）。ダークテーマでも描ける
    h.u.t = &ui::darkTokens();
    ui::applyStyle(*h.u.t, h.u.m);
    openWeekly();
    h.frame(2);
    check(h.u.dlg == ui::Dlg::Weekly, "ダークでも開く");
    h.u.dlg = ui::Dlg::None;
    h.u.t = &ui::lightTokens();
    ui::applyStyle(*h.u.t, h.u.m);
    h.frame(2);
    h.c.deleteCurrentProject();
    h.frame(2);
    // 空のプロジェクトでも開ける（案内を出す）
    h.c.createProject(app::SampleKind::Empty, "空の週報", "");
    h.frame(3);
    h.u.dlg = ui::Dlg::Weekly;
    h.u.dlgOpening = true;
    h.frame(3);
    check(h.u.dlg == ui::Dlg::Weekly && h.u.wkPreview.find("課題がありません") != std::string::npos, "課題がないプロジェクトの週報");
    h.u.dlg = ui::Dlg::None;
    h.frame(2);
    h.c.deleteCurrentProject();
    h.frame(2);
  }
  // ベースライン比較（Issue #53）: 保存ダイアログ → 一覧 → ガントの計画のバー → リストの列と絞り込み → 統計の節 → 削除 → HTML・週次
  {
    h.c.createProject(app::SampleKind::Hierarchy, "計画/テスト", "BL");
    h.c.setView(app::View::Gantt);
    h.c.clearSelection();
    h.c.clearFilter();
    h.frame(3);
    Session* s = h.c.session();
    check(s && s->baselines().list.empty(), "はじめはベースラインがない");
    check(s->baselineCompare() == nullptr, "ベースラインがなければ比較もない");
    h.frame(2);
    check(h.u.planGeom.empty(), "ベースラインがなければ計画のバーは出ない");
    // 「…」のメニュー相当のコマンドパレットから保存ダイアログを開く
    const auto openByPalette = [&](const char* query) {
      h.u.paletteOpen = true;
      h.u.paletteFocus = true;
      h.u.paletteQuery = query;
      h.frame(3);
      h.key(ImGuiKey_Enter);
      h.frame(3);
    };
    openByPalette("ベースラインを保存");
    check(h.u.dlg == ui::Dlg::BaselineSave, "コマンドパレットの「ベースラインを保存…」でダイアログが開く");
    check(h.u.dName == "2026-10-19 の計画", "名前は日付入りの既定値");
    {
      // 入力欄がアクティブなので、外から書き換えずに打つ（Ctrl+A で全部選んで置き換える）
      ImGuiIO& io = ImGui::GetIO();
      io.AddKeyEvent(ImGuiMod_Ctrl, true);
      io.AddKeyEvent(ImGuiKey_A, true);
      h.frame();
      io.AddKeyEvent(ImGuiKey_A, false);
      io.AddKeyEvent(ImGuiMod_Ctrl, false);
      io.AddInputCharactersUTF8("キックオフ");
      h.frame(2);
    }
    h.key(ImGuiKey_Enter);
    h.frame(3);
    check(h.u.dlg == ui::Dlg::None, "Enter で保存してダイアログが閉じる");
    check(s->baselines().list.size() == 1 && s->baselines().list[0].name == "キックオフ", "保存したベースラインが一覧にある");
    check(s->shownBaseline() != nullptr, "保存したベースラインをそのまま表示する");
    // 管理ダイアログ（一覧）
    openByPalette("ベースラインの管理");
    check(h.u.dlg == ui::Dlg::BaselineManage, "コマンドパレットの「ベースラインの管理…」でダイアログが開く");
    h.frame(3);
    {
      const ImGuiWindow* w = ImGui::FindWindowByName("###dlg");
      check(w != nullptr && w->Active, "管理ダイアログを描いた");
    }
    h.key(ImGuiKey_Escape);
    h.frame(2);
    check(h.u.dlg == ui::Dlg::None, "Esc で閉じる");

    // ガント: 計画のバーが出る（描いた矩形の数・位置）。変えていないので全部ぴったり
    h.frame(3);
    check(!h.u.planGeom.empty(), "ガントに計画のバーが出る");
    check(h.u.planGeom.size() == h.u.barGeom.size(), "見えている行の数だけ計画のバーがある");
    {
      bool below = true, aligned = true, noLabel = true;
      for (const auto& pg : h.u.planGeom) {
        const auto it = std::find_if(h.u.barGeom.begin(), h.u.barGeom.end(), [&](const ui::UiState::BarGeom& b) { return b.id == pg.id; });
        if (it == h.u.barGeom.end()) {
          below = false;
          continue;
        }
        below = below && pg.y0 > it->cy && pg.y1 > pg.y0;
        aligned = aligned && (std::abs(pg.xl - it->xl) < 1.5f || pg.milestone);
        noLabel = noLabel && pg.label.empty();
      }
      check(below, "計画のバーは現在のバーの下にある");
      check(aligned, "日程を変えていなければ計画のバーは現在のバーと同じ位置から始まる");
      check(noLabel, "ぴったりなら差の文字は出ない");
    }
    // 末端の未完了の課題を延ばす（遅れ）
    std::string slip;
    {
      const Tree& T = *s->tree();
      for (int id : T.leafOrder)
        if (T.kind[static_cast<std::size_t>(id)] == Kind::Task && T.cat[static_cast<std::size_t>(id)] == Cat::Todo) {
          slip = T.ids[static_cast<std::size_t>(id)];
          break;
        }
    }
    check(!slip.empty(), "延ばせる課題がある");
    check(s->setField(slip, [](Issue& o) { o.durW = (o.durW ? *o.durW : 1.0) + 2.0; }), "課題を延ばす");
    h.c.afterEdit();
    h.frame(3);
    {
      const BaselineCompare* cmp = s->baselineCompare();
      check(cmp != nullptr && cmp->sum.late >= 1, "延ばした課題は遅れになる");
      const auto pg = std::find_if(h.u.planGeom.begin(), h.u.planGeom.end(), [&](const ui::UiState::PlanGeom& g) { return g.id == slip; });
      const auto bar = std::find_if(h.u.barGeom.begin(), h.u.barGeom.end(), [&](const ui::UiState::BarGeom& b) { return b.id == slip; });
      check(pg != h.u.planGeom.end() && bar != h.u.barGeom.end(), "延ばした課題の計画のバーと現在のバーを描いた");
      if (pg != h.u.planGeom.end() && bar != h.u.barGeom.end()) {
        check(pg->xr < bar->xr - 1.f, "計画のバーの右端は、延びた現在のバーの右端より手前");
        check(pg->label.rfind("! +", 0) == 0, "遅れは「! +N日」で表す（色だけに頼らない）");
      }
    }
    // ダークでも描ける
    h.u.t = &ui::darkTokens();
    ui::applyStyle(*h.u.t, h.u.m);
    h.frame(3);
    check(!h.u.planGeom.empty(), "ダークでも計画のバーが出る");
    h.u.t = &ui::lightTokens();
    ui::applyStyle(*h.u.t, h.u.m);
    h.frame(2);
    // 基準日を大きく動かしても、時間軸は計画のバーを含む（スクロールして見える範囲に計画のバーが入る）
    for (const int shift : {84, -28}) {
      const std::string keep = s->settings().baseDate;
      s->setBaseDate(fmtIso(*parseIso(keep) + shift));
      h.c.afterEdit();
      h.u.scrollToBase = false;
      h.frame(3);
      ImGuiIO& io = ImGui::GetIO();
      io.AddMousePosEvent(800, 400);
      for (int k = 0; k < 60; ++k) {  // 左端までスクロールして、計画のバーが見える範囲に出る
        io.AddMouseWheelEvent(-6.f, 0.f);
        h.frame(2);
      }
      bool anyVisible = false;
      for (const auto& pg : h.u.planGeom) anyVisible = anyVisible || (pg.xr > h.u.ganttLeft && pg.xl < 1440.f);
      check(!h.u.planGeom.empty() && anyVisible, "基準日を動かしても、計画のバーを軸の範囲で描ける");
      s->setBaseDate(keep);
      h.c.afterEdit();
      h.u.scrollToBase = true;
      h.frame(3);
    }

    // リスト: 列「計画との差」を足し、漏斗で絞り込み・並べ替え
    h.c.setUiSub("cols", "plan", true);
    h.c.setView(app::View::List);
    h.frame(3);
    const std::size_t allLeaves = h.c.visibleLeaves().size();
    h.u.openColFilter = "plan";
    h.frame(3);
    check(h.u.openColFilter.empty(), "列「計画との差」の漏斗が開く（要求を受け取った）");
    h.key(ImGuiKey_Escape);
    h.c.columnFilter().toggle("plan", kBaselineValueOnPlan);
    h.c.columnFilter().toggle("plan", kBaselineValueEarly);
    h.frame(3);
    {
      const auto only = h.c.visibleLeaves();
      check(!only.empty() && only.size() < allLeaves, "遅れだけに絞り込める");
      bool allLate = true;
      for (int id : only) {
        const BaselineCompare* cmp = s->baselineCompare();
        allLate = allLate && cmp && cmp->entries[static_cast<std::size_t>(id)].state == BaselineState::Late;
      }
      check(allLate, "絞り込んだ行は遅れだけ");
    }
    h.u.sortKey = "plan";
    h.u.sortDir = -1;
    h.frame(3);
    h.u.sortDir = 1;
    h.frame(2);
    h.u.sortDir = 0;
    h.u.sortKey.clear();
    h.c.clearFilter();
    h.frame(2);
    check(h.c.visibleLeaves().size() == allLeaves, "絞り込みを解除すると元に戻る");
    // ガントの左の表にも出せる
    h.c.setView(app::View::Gantt);
    h.frame(3);

    // 統計: 「計画との差」の節。遅れの大きい順の行を押すと課題を選んで詳細を開く
    h.c.setView(app::View::Stats);
    h.frame(4);
    check(h.u.statsPlan.drawn && h.u.statsPlan.tiles == 5, "統計に「計画との差」の節が出る");
    check(!h.u.statsPlan.lateRows.empty(), "遅れの大きい順の行が出る");
    if (!h.u.statsPlan.lateRows.empty()) {
      // 見える位置までスクロールする（前のテストのスクロールが残っている）
      ImGuiIO& io = ImGui::GetIO();
      io.AddMousePosEvent(700, 450);
      const auto visible = [&] {
        return !h.u.statsPlan.lateRows.empty() && h.u.statsPlan.lateRows[0].a.y > 130 && h.u.statsPlan.lateRows[0].b.y < 860;
      };
      for (int k = 0; k < 200 && !visible(); ++k) {
        const bool up = h.u.statsPlan.lateRows.empty() || h.u.statsPlan.lateRows[0].a.y <= 130;
        io.AddMouseWheelEvent(0, up ? 3.f : -3.f);
        h.frame(2);
      }
      check(visible(), "遅れの大きい順の一覧が見える位置までスクロールできる");
      if (visible()) {
        const std::string rid = h.u.statsPlan.lateRows[0].id;
        check(s->issue(rid) != nullptr, "行は実在の課題");
        const auto& row = h.u.statsPlan.lateRows[0];
        const ImVec2 mid((row.a.x + row.b.x) / 2, (row.a.y + row.b.y) / 2);
        h.u.detailOpen = false;
        h.c.clearSelection();
        io.AddMousePosEvent(mid.x, mid.y);
        h.frame(3);
        ImGuiWindow* tip = ImGui::FindWindowByName("##Tooltip_00");
        check(tip && tip->Active, "行にマウスを載せると説明が出る");
        h.click(mid.x, mid.y);
        h.frame(2);
        check(h.u.detailOpen && h.c.selection().size() == 1 && h.c.selection()[0] == rid, "行を押すと課題を選んで詳細を開く");
        h.u.detailOpen = false;
        h.c.clearSelection();
        h.frame(2);
      }
    }

    // HTML 書き出し・週次レポート
    {
      ExportOptions eo;
      eo.list = false;
      const auto html = h.c.buildReport(eo);
      check(html.ok() && html.value().find("class=\"plan\"") != std::string::npos && html.value().find("計画との差") != std::string::npos,
            "HTML にガントの計画のバーと統計の節が入る");
      WeeklyOptions wo;
      wo.sections.baseline = true;
      const auto md = h.c.renderWeekly(wo, false);
      check(md.ok() && md.value().find("## ベースラインとの差") != std::string::npos && md.value().find("キックオフ") != std::string::npos,
            "週次レポートに「ベースラインとの差」が入る");
      WeeklyOptions off;
      check(h.c.renderWeekly(off, false).value().find("ベースラインとの差") == std::string::npos, "週次レポートの既定はオフ");
    }
    // 週次レポートのダイアログ: 項目のオン・オフに載る
    h.c.setView(app::View::Gantt);
    openByPalette("週次");
    check(h.u.dlg == ui::Dlg::Weekly, "週次レポートが開く");
    check(!h.u.wk.sections.baseline && h.u.wkPreview.find("ベースラインとの差") == std::string::npos, "ダイアログの既定ではオフ");
    h.u.wk.sections.baseline = true;
    h.frame(3);
    check(h.u.wkPreview.find("## ベースラインとの差") != std::string::npos, "項目をオンにするとプレビューに出る");
    h.u.dlg = ui::Dlg::None;
    h.frame(2);
    // HTML 書き出しのダイアログ
    openByPalette("HTML に書き出す");
    check(h.u.dlg == ui::Dlg::ExportHtml && h.u.rep.baseline, "HTML 書き出しは既定でベースラインを入れる");
    h.frame(2);
    h.u.dlg = ui::Dlg::None;
    h.frame(2);

    // 名前の変更・5 つの上限・削除
    const std::string firstId = s->baselines().list[0].id;
    h.u.dlg = ui::Dlg::BaselineManage;
    h.u.dlgOpening = true;
    h.u.blRenameId = firstId;
    h.u.blRenameFocus = true;
    h.u.dText = "v1.0 合意";
    h.frame(3);
    check(h.u.dlg == ui::Dlg::BaselineManage, "名前の変更中も管理ダイアログは開いている");
    h.u.dlg = ui::Dlg::None;
    h.u.blRenameId.clear();
    h.frame(2);
    check(h.c.renameBaseline(firstId, "v1.0 合意") && s->baselines().list[0].name == "v1.0 合意", "名前を変えられる");
    for (int i = 0; i < 4; ++i) check(h.c.saveBaseline("追加" + std::to_string(i)).ok(), "5 つまで保存できる");
    check(!h.c.saveBaseline("6 つ目").ok(), "6 つ目は断る");
    check(h.c.toasts().back().text.find("5 つまで") != std::string::npos, "断る理由を通知に出す");
    openByPalette("ベースラインを保存");
    h.frame(3);
    check(h.u.dlg == ui::Dlg::BaselineSave, "5 つあっても保存ダイアログは開く（理由を出して保存はできない）");
    h.key(ImGuiKey_Enter);
    h.frame(2);
    check(s->baselines().list.size() == kBaselineMax, "いっぱいのとき Enter では保存されない");
    h.u.dlg = ui::Dlg::None;
    h.frame(2);
    // 表示するベースラインの切り替え
    h.c.setView(app::View::List);
    h.frame(3);
    h.c.showBaseline(s->baselines().list[1].id);
    h.frame(3);
    check(s->shownBaseline() && s->shownBaseline()->name == "追加0", "表示するベースラインを切り替えられる");
    h.c.showBaseline("");
    h.frame(3);
    check(s->baselineCompare() == nullptr, "表示しなければ比較は出ない");
    // 削除の確認 → 削除
    h.u.blDeleteId = firstId;
    h.u.dlg = ui::Dlg::ConfirmBaselineDelete;
    h.u.dlgOpening = true;
    h.frame(3);
    check(h.u.dlg == ui::Dlg::ConfirmBaselineDelete, "削除の確認が開く");
    h.u.dlg = ui::Dlg::None;
    h.frame(2);
    check(h.c.deleteBaseline(firstId) && s->baselines().list.size() == kBaselineMax - 1, "削除できる");
    h.c.showBaseline(s->baselines().list[0].id);
    h.c.setView(app::View::Gantt);
    h.frame(3);
    while (!s->baselines().list.empty()) h.c.deleteBaseline(s->baselines().list[0].id);
    h.frame(3);
    check(h.u.planGeom.empty(), "全部削除すると計画のバーも出ない");
    h.c.setView(app::View::Stats);
    h.frame(3);
    check(!h.u.statsPlan.drawn, "統計の節も出ない");
    h.c.setUiSub("cols", "plan", false);
    h.c.deleteCurrentProject();
    h.frame(2);
  }
  // ベースライン: 全課題を削除しても、マイルストーンだけでも、統計に「計画との差」（消えた課題など）が出る
  {
    h.c.createProject(app::SampleKind::Hierarchy, "計画/全削除", "BD");
    h.c.setView(app::View::Stats);
    h.c.clearFilter();
    h.frame(3);
    Session* s = h.c.session();
    check(h.c.saveBaseline("削除前").ok(), "ベースラインを保存する");
    const std::size_t before = s->issues().size();
    // マイルストーンだけ残す（マイルストーンとその祖先以外を消す。祖先は子がいるので作業にならない）
    {
      std::vector<std::string> del;
      const Tree& T = *s->tree();
      std::set<int> keep;
      for (int id : T.leafOrder)
        if (T.kind[static_cast<std::size_t>(id)] == Kind::Ms)
          for (int x = id; x >= 0; x = T.parentOf[static_cast<std::size_t>(x)]) keep.insert(x);
      for (int id : T.roots)
        if (!keep.count(id)) del.push_back(T.ids[static_cast<std::size_t>(id)]);
      for (int id : T.dfs)
        if (!keep.count(id) && (T.parentOf[static_cast<std::size_t>(id)] < 0 || keep.count(T.parentOf[static_cast<std::size_t>(id)])))
          del.push_back(T.ids[static_cast<std::size_t>(id)]);
      s->deleteIssues(del);
      h.c.afterEdit();
    }
    h.frame(4);
    {
      const Tree& T = *s->tree();
      int tasks = 0;
      for (int id : T.leafOrder)
        if (T.kind[static_cast<std::size_t>(id)] != Kind::Ms) ++tasks;
      check(tasks == 0 && !s->issues().empty() && s->issues().size() < before, "マイルストーンだけのプロジェクトにした");
    }
    check(h.u.statsPlan.drawn && h.u.statsPlan.tiles == 5, "作業が 0 件（マイルストーンだけ）でも「計画との差」の節が出る");
    // 全部削除
    {
      std::vector<std::string> all;
      for (const Issue& o : s->issues()) all.push_back(o.id);
      s->deleteIssues(all);
      h.c.afterEdit();
    }
    h.frame(4);
    check(s->issues().empty(), "全課題を削除した");
    check(h.u.statsPlan.drawn && h.u.statsPlan.tiles == 5, "全課題を削除しても「消えた課題」の比較が出る");
    const BaselineCompare* cmp = s->baselineCompare();
    check(cmp != nullptr && cmp->sum.removed >= 1, "消えた課題を数える");
    // ダークでも描ける
    h.u.t = &ui::darkTokens();
    ui::applyStyle(*h.u.t, h.u.m);
    h.frame(3);
    check(h.u.statsPlan.drawn, "ダークでも描ける");
    h.u.t = &ui::lightTokens();
    ui::applyStyle(*h.u.t, h.u.m);
    h.frame(2);
    h.c.deleteCurrentProject();
    h.frame(2);
  }
  // 「今日」ビュー（Issue #72）
  {
    const std::string csv =
        "Issue Id,Issue Type,Summary,Parent,Assignee,Status,Priority,Start date,Deadline,Outward issue link (Blocks),Duration (weeks)\n"
        "1,Epic,認証基盤,,,To Do,High,,2026-12-18,,\n"
        "2,Task,ログイン画面の実装,1,田中,In Progress,High,2026-10-14,2026-10-23,,0.6\n"
        "3,Task,パスワード再設定の設計,1,田中,To Do,Medium,,2026-10-16,,0.4\n"
        "4,Task,セッション管理のレビュー,1,田中,To Do,Medium,,2026-10-19,5,0.2\n"
        "5,Task,監査ログの出力,1,田中,To Do,Medium,,2026-11-13,,0.6\n"
        "6,Task,アカウントロックの仕様確認,1,田中,To Do,High,,2026-11-06,,0.4\n"
        "7,Task,結合テストの準備,1,佐藤,To Do,Medium,,2026-10-21,,0.4\n"
        "8,Milestone,認証 β 版リリース,1,田中,To Do,High,,2026-10-19,,0\n"
        "10,Task,DB インデックスの見直し,1,田中,In Progress,Medium,2026-10-12,2026-10-30,,1.5\n"
        "11,Task,キャッシュ戦略の調査,1,田中,To Do,Low,,2026-10-22,,0.4\n"
        "12,Task,期限なしの調査メモ,,田中,To Do,Low,,,,0.2\n";
    check(h.c.importText(csv, "今日のデモ", app::ImportMode::New).ok, "今日ビューの確認用の取り込み");
    h.c.prefs().me = "田中";
    h.c.session()->setBaseDate("2026-10-19");
    h.c.afterEdit();
    h.c.setView(app::View::Gantt);
    h.u.detailOpen = false;
    h.c.clearSelection();
    h.c.setUi("todayTab", "today");
    h.c.setUi("todayWho", "me");
    for (const auto& t : std::vector<app::Toast>(h.c.toasts())) h.c.dismissToast(t.id);
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1440, 900);
    Session* s = h.c.session();
    auto& tv = h.u.today;
    const auto rowOf = [&](const std::string& id) -> const ui::UiState::TodayRow* {
      for (const auto& r : tv.rows)
        if (r.id == id) return &r;
      return nullptr;
    };
    const auto mid = [](ImVec2 a, ImVec2 b) { return ImVec2((a.x + b.x) / 2, (a.y + b.y) / 2); };
    const auto secCount = [&](const char* key) {
      for (const auto& [k, n] : tv.sections)
        if (k == key) return n;
      return 0;
    };
    const auto clickRect = [&](ImVec2 a, ImVec2 b) {
      const ImVec2 m = mid(a, b);
      h.click(m.x, m.y);
    };
    const auto clearToasts = [&] {
      for (const auto& t : std::vector<app::Toast>(h.c.toasts())) h.c.dismissToast(t.id);
    };

    // キー 6 で開く。各節に正しい課題が入る
    h.key(ImGuiKey_6);
    h.frame(3);
    check(h.c.view() == app::View::Today, "キー 6 で今日ビューに切り替わる");
    check(tv.drawn && !tv.week && !tv.empty && !tv.baseNote, "今日ビューが描かれる（基準日は今日と同じなので案内なし）");
    check(tv.sections.size() == 5, "5 つの節がすべて出る（遅れ・期限・進行中・始められる・マイルストーン）");
    check(secCount("late") == 1 && secCount("due") == 2 && secCount("prog") == 1 && secCount("start") == 2 && secCount("ms") == 1,
          "節ごとの件数が正しい（自分 = 田中）");
    check(tv.total == 7, "今日の対象は 7 件");
    check(rowOf("3") && rowOf("3")->sec == "late", "期限を過ぎた課題は「遅れている」");
    check(rowOf("4") && rowOf("4")->sec == "due" && rowOf("2") && rowOf("2")->sec == "due", "今日が期限・終了予定");
    check(rowOf("10") && rowOf("10")->sec == "prog", "進行中");
    check(rowOf("6") && rowOf("6")->sec == "start" && rowOf("11") && rowOf("11")->sec == "start", "今日から始められる");
    check(!rowOf("5"), "先行が未完了の課題は始められる節に出ない");
    check(!rowOf("7"), "「自分」は佐藤の課題を出さない");
    check(!rowOf("12"), "日付のない課題は出ない");
    check(rowOf("8") && rowOf("8")->sec == "ms", "今日のマイルストーン");
    check(h.u.order.size() == 7, "画面の順（矢印キー用）が 7 件");

    // 担当の切り替え（自分／全員）
    clickRect(tv.whoAll.first, tv.whoAll.second);
    h.frame(2);
    check(h.c.uiStr("todayWho", "") == "all" && secCount("start") == 3 && rowOf("7") != nullptr, "「全員」に切り替えると佐藤の課題も出る");
    clickRect(tv.whoMe.first, tv.whoMe.second);
    h.frame(2);
    check(h.c.uiStr("todayWho", "") == "me" && secCount("start") == 2 && !rowOf("7"), "「自分」に戻すと自分の課題だけ");

    // チェックで完了にする（Undo 1 回で戻る。完了日は既存の動き = 今日と基準日の早い方）
    {
      const std::vector<Issue> before = s->issues();
      const std::size_t undos = s->undoSize();
      const auto* r = rowOf("6");
      check(r != nullptr, "完了にする課題の行");
      if (r) {
        clickRect(r->chkA, r->chkB);
        h.frame(2);
        const Issue* o = s->issue("6");
        check(o && statusCat(o->status) == Cat::Done && o->resolved == *parseIso("2026-10-19"), "チェックで完了になり、完了日が入る");
        check(s->undoSize() == undos + 1, "完了は Undo 1 回分");
        check(!rowOf("6") && secCount("start") == 1, "完了した課題は一覧から消える");
        bool undoToast = false;
        for (const auto& t : h.c.toasts()) undoToast |= t.action == NoticeAction::Undo && t.text.find("完了にしました") != std::string::npos;
        check(undoToast, "完了の通知に「元に戻す」がある");
        h.c.undo();
        h.frame(2);
        check(s->issues() == before, "Undo で元に戻る（状況・完了日とも）");
        check(rowOf("6") && secCount("start") == 2, "Undo で一覧に戻る");
      }
    }
    clearToasts();
    // 明日にずらす・来週にずらす（Undo 1 回で戻る。今日の節から外れる）
    {
      const std::vector<Issue> before = s->issues();
      const std::size_t undos = s->undoSize();
      const auto* r = rowOf("11");
      check(r != nullptr, "ずらす課題の行");
      if (r) {
        clickRect(r->tomA, r->tomB);
        h.frame(2);
        const Issue* o = s->issue("11");
        check(o && o->notBefore == *parseIso("2026-10-20") && o->deadline == *parseIso("2026-10-23"),
              "明日にずらすと、着手可能日が明日・期限が 1 営業日後になる");
        check(s->undoSize() == undos + 1, "ずらすのは Undo 1 回分");
        check(!rowOf("11") && secCount("start") == 1, "明日にずらした課題は今日の節から外れる");
        check(!h.c.toasts().empty() && h.c.toasts().back().action == NoticeAction::Undo, "ずらした通知に「元に戻す」がある");
        h.c.undo();
        h.frame(2);
        check(s->issues() == before && rowOf("11") != nullptr, "Undo で元に戻る");
      }
      const auto* r2 = rowOf("6");
      if (r2) {
        clickRect(r2->nxtA, r2->nxtB);
        h.frame(2);
        const Issue* o = s->issue("6");
        check(o && o->notBefore == *parseIso("2026-10-26") && o->deadline == *parseIso("2026-11-13"),
              "来週にずらすと、着手可能日が来週の月曜・期限が 5 営業日後になる");
        check(s->undoSize() == undos + 1, "来週にずらすのも Undo 1 回分");
        h.c.undo();
        h.frame(2);
        check(s->issues() == before, "Undo で元に戻る（来週）");
      }
    }
    clearToasts();
    // 親（まとめ）はずらせない。何も変えず理由を通知する
    {
      const std::vector<Issue> before = s->issues();
      check(!shiftIssue(*s, "1", ShiftTo::Tomorrow, h.clk.today()).ok, "親（まとめ）はずらせない");
      check(s->issues() == before && !s->takeNotices().empty(), "ずらせないときは何も変えず理由を通知する");
    }
    // 行を押すと詳細パネルが開く。ダブルクリックでリストの該当位置へ
    {
      const auto* r = rowOf("10");
      check(r != nullptr, "押す行");
      if (r) {
        h.click(r->a.x + 240, (r->a.y + r->b.y) / 2);
        h.frame(2);
        check(h.u.detailOpen && h.c.selection().size() == 1 && h.c.selection()[0] == "10", "行を押すとその課題を選んで詳細パネルが開く");
        check(h.c.view() == app::View::Today, "今日ビューのまま（詳細は横に開く）");
        const auto* r2 = rowOf("10");
        if (r2) {
          h.click(r2->a.x + 240, (r2->a.y + r2->b.y) / 2, 2);
          h.frame(3);
          check(h.c.view() == app::View::List && h.c.selection().size() == 1 && h.c.selection()[0] == "10",
                "ダブルクリックでリストの該当課題へ");
          check(h.u.revealId.empty(), "リストが行の位置へ進んだら目印を消す");
        }
      }
      h.u.detailOpen = false;
      h.c.clearSelection();
      h.c.setView(app::View::Today);
      h.frame(3);
    }
    // 今週: 日ごとの列・進行中の帯・サマリー
    clickRect(tv.tabWeek.first, tv.tabWeek.second);
    h.frame(3);
    check(h.c.uiStr("todayTab", "") == "week" && tv.drawn && tv.week, "「今週」に切り替わる");
    check(tv.colCounts[0] == 2 && tv.colCounts[1] == 0 && tv.colCounts[2] == 0 && tv.colCounts[3] == 1 && tv.colCounts[4] == 1,
          "日ごとの列の件数（月 2・木 1・金 1）");
    check(tv.completing == 4 && tv.elsewhere == 1, "サマリー: 今週の完了予定 4 件・今週の外の遅れ 1 件");
    check(tv.bands == 2 && tv.bandsShown == 2, "進行中の帯が 2 件");
    check(tv.colRects.size() == 5, "5 つの列");
    for (const auto& cr : tv.colRects) check(cr.second.x <= 1440.f, "列が幅に収まる");
    {
      std::set<std::string> uniq(h.u.order.begin(), h.u.order.end());
      check(uniq.size() == h.u.order.size(), "矢印キーの順に同じ課題が 2 回入らない（帯とカードの両方に出る課題）");
    }
    {
      const auto* r = rowOf("11");  // 木曜の列のカード
      check(r != nullptr && r->sec == "week", "木曜の列にカードが出る");
      if (r) {
        const std::vector<Issue> before = s->issues();
        clickRect(r->chkA, r->chkB);
        h.frame(2);
        check(s->issue("11") && statusCat(s->issue("11")->status) == Cat::Done, "カードのチェックでも完了になる");
        h.c.undo();
        h.frame(2);
        check(s->issues() == before, "Undo で元に戻る（今週）");
      }
      clearToasts();
      const auto* r2 = rowOf("4");  // 月曜の列
      check(r2 != nullptr, "月曜の列のカード");
      if (r2) {
        const std::vector<Issue> before = s->issues();
        clickRect(r2->tomA, r2->tomB);
        h.frame(3);
        check(s->issue("4") && s->issue("4")->notBefore == *parseIso("2026-10-20"), "カードの「明日」でもずらせる");
        h.c.undo();
        h.frame(2);
        check(s->issues() == before, "Undo で元に戻る（カードのずらし）");
      }
    }
    // 今週の外の遅れ → 「今日」へ
    if (tv.elsewhereBtn.second.x > tv.elsewhereBtn.first.x) {
      clickRect(tv.elsewhereBtn.first, tv.elsewhereBtn.second);
      h.frame(3);
      check(h.c.uiStr("todayTab", "") == "today" && tv.drawn && !tv.week, "「今日で見る」で今日に戻る");
    } else {
      check(false, "今週の外の遅れの案内が出ない");
    }
    clearToasts();
    // F2（名前の編集）: この表示に入力欄がないので、リストへ移して編集できるようにする（編集の状態だけが残ってキーを塞がない）
    {
      h.c.select("10");
      h.frame(2);
      h.key(ImGuiKey_F2);
      h.frame(3);
      check(h.c.view() == app::View::List && h.u.editField == ui::EditField::Name && h.u.editId == "10", "F2 でリストへ移って名前の編集に入る");
      h.key(ImGuiKey_Escape);
      h.frame(2);
      check(h.u.editField == ui::EditField::None, "Esc で編集が終わる");
      h.c.clearSelection();
      h.c.setView(app::View::Today);
      h.frame(3);
    }
    // 基準日が今日と違うときは画面の上に案内し、ボタンで今日に合わせられる
    s->setBaseDate("2026-10-12");
    h.c.afterEdit();
    h.frame(3);
    check(tv.baseNote && tv.baseBtn.second.x > tv.baseBtn.first.x, "基準日が今日と違う案内が出る");
    clickRect(tv.baseBtn.first, tv.baseBtn.second);
    h.frame(3);
    check(s->baseDay() == *parseIso("2026-10-19") && !tv.baseNote, "「基準日を今日にする」で案内が消える");
    // ダーク・コンパクト・高 DPI
    h.u.t = &ui::darkTokens();
    h.u.m.compact = true;
    h.u.m.scale = 1.5f;
    ui::applyStyle(*h.u.t, h.u.m);
    h.frame(4);
    check(tv.drawn && tv.total == 7 && !tv.rows.empty(), "ダーク・コンパクト・高DPI でも今日が描かれる");
    h.c.setUi("todayTab", "week");
    h.frame(4);
    check(tv.drawn && tv.week && tv.completing == 4, "ダーク・コンパクト・高DPI でも今週が描かれる");
    h.u.t = &ui::lightTokens();
    h.u.m.compact = false;
    h.u.m.scale = 1.0f;
    ui::applyStyle(*h.u.t, h.u.m);
    h.c.setUi("todayTab", "today");
    // 幅が狭い（960 + 詳細パネル）
    io.DisplaySize = ImVec2(960, 600);
    h.u.detailOpen = true;
    h.c.select("2");
    h.frame(4);
    check(tv.drawn && tv.total == 7, "狭い幅でも今日が描かれる");
    h.c.setUi("todayTab", "week");
    h.frame(4);
    check(tv.drawn && tv.week, "狭い幅でも今週が描かれる");
    h.u.detailOpen = false;
    h.c.clearSelection();
    h.c.setUi("todayTab", "today");
    io.DisplaySize = ImVec2(1440, 900);
    // 空: 担当が「自分」の課題がない（名前の食い違い）→ 案内。名前を変えるとその人の課題が出る
    h.c.prefs().me = "いない人";
    h.frame(3);
    check(tv.empty && tv.noMine && tv.total == 0, "担当が「自分」の課題がないときは、名前の食い違いを案内する");
    h.c.prefs().me = "佐藤";
    h.frame(3);
    check(!tv.empty && tv.total == 1 && rowOf("7") != nullptr, "自分の名前を変えると、その人の課題が出る");
    h.c.prefs().me = "田中";
    // すべて空: 完了済みだけのプロジェクト・空のプロジェクト
    {
      const std::string doneOnly =
          "Issue Id,Issue Type,Summary,Parent,Assignee,Status,Priority,Resolved,Duration (weeks)\n"
          "1,Task,終わった,,田中,Done,Medium,2026-10-13,1\n";
      check(h.c.importText(doneOnly, "完了だけ", app::ImportMode::New).ok, "完了だけの取り込み");
      h.c.setView(app::View::Today);
      h.frame(3);
      check(tv.drawn && tv.empty && tv.total == 0, "すべて空のときは「今日やることはありません」を出す");
      h.c.setUi("todayWho", "all");
      h.frame(3);
      check(tv.empty && !tv.noMine, "全員でも空なら案内を出す");
      h.c.setUi("todayWho", "me");
      h.c.createProject(app::SampleKind::Empty, "空の今日", "");
      h.c.setView(app::View::Today);
      h.frame(3);
      check(tv.drawn && tv.empty && tv.total == 0, "空のプロジェクトでも落ちない");
      h.c.setUi("todayTab", "week");
      h.frame(3);
      check(tv.drawn && tv.week && tv.completing == 0, "空のプロジェクトの今週も落ちない");
      h.c.setUi("todayTab", "today");
      h.c.deleteCurrentProject();
      h.c.deleteCurrentProject();
      h.frame(2);
    }
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
                           "app/ui/view_goals.cpp", "app/ui/view_stats.cpp", "app/ui/view_today.cpp", "app/ui/panel_detail.cpp", "app/ui/panel_attach.cpp", "app/ui/panel_links.cpp", "app/ui/palette.cpp", "app/ui/dialogs.cpp",
                           "app/ui/widgets.cpp", "app/ui/strings.hpp", "app/controller/app_controller.cpp",
                           "core/src/report.cpp", "core/src/baseline.cpp", "core/src/weekly.cpp", "core/src/weekly_md.cpp", "core/src/weekly_html.cpp", "core/src/weekly_state.cpp", "core/src/report_gantt.cpp", "core/src/report_stats.cpp", "core/src/report_list.cpp", "core/src/gantt_geom.cpp",
                           "core/src/today.cpp", "core/src/commands.cpp", "core/src/outline.cpp", "core/src/csv.cpp", "core/src/engine.cpp", "core/src/store.cpp", "core/src/attachments.cpp", "core/src/links.cpp", "core/src/load.cpp"};
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
