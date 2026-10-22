// 画面の確認用: UI をヘッドレスで描き、ImGui の描画データをソフトウェアでラスタライズして PPM 画像に書き出す。
// GPU も Windows も要らない。レイアウト・文字の欠け・色の確認に使う（Windows 実機での確認の代わりにはならない）。
// 使い方: roadmap_ui_screens <出力フォルダ> [日本語フォント.ttc] [フォント番号]
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

#include <imgui.h>
#include <imgui_internal.h>

#include "app_controller.hpp"
#include "roadmap/gen.hpp"
#include "roadmap/schedule.hpp"
#include "roadmap/links.hpp"
#include "ui/app_ui.hpp"
#include "ui/internal.hpp"

using namespace roadmap;

namespace {

struct Canvas {
  int w, h;
  std::vector<float> px;  // RGB
  Canvas(int W, int H, ImVec4 bg) : w(W), h(H), px(static_cast<std::size_t>(W) * H * 3) {
    for (int i = 0; i < W * H; ++i) {
      px[i * 3] = bg.x;
      px[i * 3 + 1] = bg.y;
      px[i * 3 + 2] = bg.z;
    }
  }
  void save(const std::string& path) const {
    std::ofstream f(path, std::ios::binary);
    f << "P6\n" << w << " " << h << "\n255\n";
    for (float v : px) f.put(static_cast<char>(std::clamp(static_cast<int>(std::pow(v, 1.0f) * 255.f + 0.5f), 0, 255)));
  }
};

struct Tex {
  int w = 0, h = 0;
  const unsigned char* rgba = nullptr;
};

float edge(const ImVec2& a, const ImVec2& b, float x, float y) { return (b.x - a.x) * (y - a.y) - (b.y - a.y) * (x - a.x); }

void raster(Canvas& cv, const Tex& tex, const ImDrawVert& v0, const ImDrawVert& v1, const ImDrawVert& v2, const ImVec4& clip) {
  const float area = edge(v0.pos, v1.pos, v2.pos.x, v2.pos.y);
  if (std::fabs(area) < 1e-6f) return;
  const int x0 = std::max(static_cast<int>(std::floor(std::max(clip.x, std::min({v0.pos.x, v1.pos.x, v2.pos.x})))), 0);
  const int x1 = std::min(static_cast<int>(std::ceil(std::min(clip.z, std::max({v0.pos.x, v1.pos.x, v2.pos.x})))), cv.w);
  const int y0 = std::max(static_cast<int>(std::floor(std::max(clip.y, std::min({v0.pos.y, v1.pos.y, v2.pos.y})))), 0);
  const int y1 = std::min(static_cast<int>(std::ceil(std::min(clip.w, std::max({v0.pos.y, v1.pos.y, v2.pos.y})))), cv.h);
  const ImVec4 c0 = ImGui::ColorConvertU32ToFloat4(v0.col), c1 = ImGui::ColorConvertU32ToFloat4(v1.col),
               c2 = ImGui::ColorConvertU32ToFloat4(v2.col);
  for (int y = y0; y < y1; ++y)
    for (int x = x0; x < x1; ++x) {
      const float px = static_cast<float>(x) + 0.5f, py = static_cast<float>(y) + 0.5f;
      float w0 = edge(v1.pos, v2.pos, px, py) / area, w1 = edge(v2.pos, v0.pos, px, py) / area, w2 = edge(v0.pos, v1.pos, px, py) / area;
      if (w0 < -1e-4f || w1 < -1e-4f || w2 < -1e-4f) continue;
      const float u = w0 * v0.uv.x + w1 * v1.uv.x + w2 * v2.uv.x, v = w0 * v0.uv.y + w1 * v1.uv.y + w2 * v2.uv.y;
      ImVec4 col(w0 * c0.x + w1 * c1.x + w2 * c2.x, w0 * c0.y + w1 * c1.y + w2 * c2.y, w0 * c0.z + w1 * c1.z + w2 * c2.z,
                 w0 * c0.w + w1 * c1.w + w2 * c2.w);
      const int tx = std::clamp(static_cast<int>(u * static_cast<float>(tex.w)), 0, tex.w - 1);
      const int ty = std::clamp(static_cast<int>(v * static_cast<float>(tex.h)), 0, tex.h - 1);
      const unsigned char* t = tex.rgba + (static_cast<std::size_t>(ty) * tex.w + tx) * 4;
      col.x *= t[0] / 255.f;
      col.y *= t[1] / 255.f;
      col.z *= t[2] / 255.f;
      col.w *= t[3] / 255.f;
      float* d = &cv.px[(static_cast<std::size_t>(y) * cv.w + x) * 3];
      d[0] = d[0] * (1 - col.w) + col.x * col.w;
      d[1] = d[1] * (1 - col.w) + col.y * col.w;
      d[2] = d[2] * (1 - col.w) + col.z * col.w;
    }
}

// 画像のテクスチャ（NullPlatform が預かる。ImTextureID は 1 からの番号）
struct ImgTex {
  int w = 0, h = 0;
  std::vector<unsigned char> rgba;
};
std::vector<ImgTex> gImages;

void render(const ImDrawData* dd, const Tex& fontTex, const std::string& path, ImVec4 bg) {
  Canvas cv(static_cast<int>(dd->DisplaySize.x), static_cast<int>(dd->DisplaySize.y), bg);
  for (int n = 0; n < dd->CmdListsCount; ++n) {
    const ImDrawList* l = dd->CmdLists[n];
    for (const ImDrawCmd& cmd : l->CmdBuffer) {
      if (cmd.UserCallback) continue;
      Tex tex = fontTex;
      const auto id = reinterpret_cast<std::uintptr_t>(cmd.GetTexID());
      if (id >= 1 && id <= gImages.size()) tex = {gImages[id - 1].w, gImages[id - 1].h, gImages[id - 1].rgba.data()};
      for (unsigned i = 0; i + 2 < cmd.ElemCount; i += 3) {
        const ImDrawIdx* idx = l->IdxBuffer.Data + cmd.IdxOffset + i;
        const ImDrawVert* vb = l->VtxBuffer.Data + cmd.VtxOffset;
        raster(cv, tex, vb[idx[0]], vb[idx[1]], vb[idx[2]], cmd.ClipRect);
      }
    }
  }
  cv.save(path);
  std::printf("wrote %s\n", path.c_str());
}

struct NullPlatform : ui::IPlatform {
  std::optional<std::pair<std::string, std::string>> openTextFile() override { return std::nullopt; }
  bool saveTextFile(const std::string&, const std::string&) override { return true; }
  void openFolder(const std::string&) override {}
  std::optional<std::string> pickSaveFile(const std::string&, const std::string&, const std::string&) override { return savePath; }
  std::optional<std::string> savePath;
  std::optional<std::string> pickFolder(const std::string&) override { return picked; }
  std::optional<std::string> picked;
  bool systemDark() override { return false; }
  void requestQuit() override {}
  ImTextureID createTexture(const unsigned char* rgba, int w, int h) override {
    gImages.push_back({w, h, std::vector<unsigned char>(rgba, rgba + static_cast<std::size_t>(w) * h * 4)});
    return reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(gImages.size()));
  }
};

// 24bit の BMP（画面の確認用の絵。グラデーションと図形）
std::string makeBmp(int w, int h, int variant) {
  const int rowBytes = (w * 3 + 3) / 4 * 4;
  std::string s;
  const auto put32 = [&](std::uint32_t v) { for (int i = 0; i < 4; ++i) s.push_back(static_cast<char>((v >> (8 * i)) & 0xff)); };
  const auto put16 = [&](std::uint16_t v) { s.push_back(static_cast<char>(v & 0xff)); s.push_back(static_cast<char>(v >> 8)); };
  s += "BM";
  put32(static_cast<std::uint32_t>(54 + rowBytes * h)); put32(0); put32(54);
  put32(40); put32(static_cast<std::uint32_t>(w)); put32(static_cast<std::uint32_t>(h)); put16(1); put16(24);
  put32(0); put32(static_cast<std::uint32_t>(rowBytes * h)); put32(2835); put32(2835); put32(0); put32(0);
  for (int y = h - 1; y >= 0; --y) {
    for (int x = 0; x < w; ++x) {
      const bool box = x > w / 4 && x < w * 3 / 4 && y > h / 3 && y < h * 2 / 3;
      int r = x * 255 / w, g = y * 255 / h, b = variant ? 200 - x * 120 / w : 120 + y * 100 / h;
      if (box) r = 250, g = 250, b = 250;
      s.push_back(static_cast<char>(b)); s.push_back(static_cast<char>(g)); s.push_back(static_cast<char>(r));
    }
    for (int k = w * 3; k < rowBytes; ++k) s.push_back('\0');
  }
  return s;
}

}  // namespace

int main(int argc, char** argv) {
  const std::string out = argc > 1 ? argv[1] : ".";
  const std::string fontPath = argc > 2 ? argv[2] : "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc";
  const int fontNo = argc > 3 ? std::atoi(argv[3]) : 0;
  MemFileStore fs;
  FixedClock clk{LocalTime{2026, 10, 19, 18, 36, 0}};
  app::AppController c{fs, clk, "data", "prefs.json"};
  ui::UiState u;
  NullPlatform p;
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.DisplaySize = ImVec2(1440, 900);
  io.IniFilename = nullptr;
  io.DeltaTime = 1.f / 60.f;
  ImFontGlyphRangesBuilder rb;
  rb.AddRanges(io.Fonts->GetGlyphRangesJapanese());
  static const ImWchar extra[] = {0x2000, 0x206F, 0x2190, 0x21FF, 0x2200, 0x22FF, 0x25A0, 0x25FF, 0x2600, 0x27BF, 0};
  rb.AddRanges(extra);
  for (ImWchar ch : ui::uiGlyphs()) rb.AddChar(ch);
  {
    // データにある文字も入れる（Windows 版はデータを開くたびに足りない字を足してフォントを作り直す）
    MemFileStore tmpFs;
    app::AppController tmp{tmpFs, clk, "d", "p.json"};
    tmp.start();
    for (auto k : {app::SampleKind::Hierarchy}) {
      tmp.createProject(k, "x", "");
      std::set<ImWchar> g;
      ui::collectGlyphs(tmp, g);
      for (ImWchar ch : g) rb.AddChar(ch);
    }
  }
  ImVector<ImWchar> ranges;
  rb.BuildRanges(&ranges);
  ImFontConfig cfg;
  cfg.FontNo = fontNo;
  cfg.OversampleH = 1;
  u.fontBody = io.Fonts->AddFontFromFileTTF(fontPath.c_str(), 13.f, &cfg, ranges.Data);
  if (!u.fontBody) {
    std::fprintf(stderr, "フォントを読めません: %s\n", fontPath.c_str());
    return 1;
  }
  u.fontMono = u.fontBold = u.fontBody;
  u.fontLarge = io.Fonts->AddFontFromFileTTF(fontPath.c_str(), 17.f, &cfg, ranges.Data);
  Tex tex;
  unsigned char* pixels;
  io.Fonts->GetTexDataAsRGBA32(&pixels, &tex.w, &tex.h);
  tex.rgba = pixels;
  ui::applyStyle(*u.t, u.m);
  c.start();

  const auto frames = [&](int n) {
    for (int i = 0; i < n; ++i) {
      clk.advanceMs(16);
      ImGui::NewFrame();
      ui::drawApp(c, u, p);
      ImGui::Render();
    }
  };
  // 環境変数 ROADMAP_SHOT_ONLY があれば、その文字で始まる名前の画像だけを書く（ディスクと時間の節約。状態は同じだけ進める）
  std::string onlyBuf;
#ifdef _MSC_VER
  {
    char* v = nullptr;
    std::size_t n = 0;
    if (_dupenv_s(&v, &n, "ROADMAP_SHOT_ONLY") == 0 && v) onlyBuf = v;
    std::free(v);
  }
#else
  if (const char* v = std::getenv("ROADMAP_SHOT_ONLY")) onlyBuf = v;
#endif
  const char* const only = onlyBuf.empty() ? nullptr : onlyBuf.c_str();
  const auto shot = [&](const std::string& name) {
    frames(3);
    if (only && name.rfind(only, 0) != 0) return;
    render(ImGui::GetDrawData(), tex, out + "/" + name + ".ppm", ui::v4(u.t->bg));
  };
  shot("00_welcome");
  {
    // 機能の確認用の汎用データ（golden/samples。画面には出さないテスト専用のサンプル）
    std::ifstream in(std::string(ROADMAP_SOURCE_DIR) + "/golden/samples/scenario_demo.csv", std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    c.importText(ss.str(), "サンプル", app::ImportMode::New);
  }
  c.prefs().hintDismissed = false;
  c.select("9");
  shot("01_gantt_week");
  c.setUi("zoom", "day");
  shot("02_gantt_day");
  c.setUi("zoom", "all");
  c.setUi("colorBy", "float");
  shot("03_gantt_all_float");
  c.setUi("zoom", "week");
  c.setUi("colorBy", "root");
  u.detailOpen = true;
  shot("04_gantt_detail");
  u.detailOpen = false;
  c.setView(app::View::List);
  shot("05_list");
  c.setView(app::View::Board);
  c.setUi("lane", "root");
  shot("06_board");
  c.setView(app::View::Goals);
  shot("07_goals");
  c.setView(app::View::Stats);
  shot("07b_stats");
  c.createProject(app::SampleKind::Hierarchy, "階層デモ（セキュアブート）", "DEV");
  c.setView(app::View::Gantt);
  c.select("11");
  shot("08_hierarchy_gantt");
  c.setView(app::View::Stats);
  shot("08b_stats_hierarchy");
  c.setView(app::View::Gantt);
  c.setSelection({"12", "13", "14"}, "14");
  shot("09_bulk");
  // 先行の列（Issue #9）: 表に出した状態・入力中の誤り（赤）・入力中の候補
  {
    c.setUiSub("cols", "pred", true);
    c.clearSelection();
    c.setView(app::View::Gantt);
    shot("24_pred_gantt");
    c.setView(app::View::List);
    shot("25_pred_list");
    ui::Frame fr{c, u, p};
    ui::beginEdit(fr, "14", ui::EditField::Pred);
    u.editBuf += ", DEV-999, 14";
    shot("26_pred_edit_error");
    u.editField = ui::EditField::None;
    u.editId.clear();
    frames(2);  // 入力欄の状態を捨てる
    ui::beginEdit(fr, "14", ui::EditField::Pred);
    u.editBuf = "DEV-1";
    shot("27_pred_edit_suggest");
    u.editField = ui::EditField::None;
    u.editId.clear();
    c.setUiSub("cols", "pred", false);
    c.setView(app::View::Gantt);
    c.setSelection({"12", "13", "14"}, "14");
  }
  u.paletteOpen = true;
  u.paletteFocus = true;
  shot("10_palette");
  u.paletteOpen = false;
  frames(2);
  u.dlg = ui::Dlg::Settings;
  u.dlgOpening = true;
  shot("11_settings");
  u.dlg = ui::Dlg::None;
  frames(2);
  // 上の絞り込み（一致しないものは薄く）・列の絞り込み・行のごみ箱
  c.clearSelection();
  c.filter().st = StFilter::Done;
  shot("13_dim_gantt");
  c.setView(app::View::Board);
  c.setUi("lane", "none");
  shot("14_dim_board");
  c.filter() = {};
  c.setView(app::View::List);
  u.openColFilter = "asg";
  shot("15_colfilter_popup");
  ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, true);
  frames(1);
  ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, false);
  c.columnFilter().toggle("status", "Done");
  c.setView(app::View::Gantt);
  ImGui::GetIO().AddMousePosEvent(200, 190);
  shot("16_colfilter_hover");
  c.clearFilter();
  ImGui::GetIO().AddMousePosEvent(-1, -1);
  // ガントの時間軸を Ctrl+ホイールで変えた幅
  c.setView(app::View::Gantt);
  c.setUi("zoom", "week");
  u.customProject = c.currentId();
  u.customPx = 16.f;
  shot("20_gantt_custom_wide");
  u.customPx = 3.2f;
  shot("21_gantt_custom_narrow");
  u.customPx = 0;
  // 依存の相手: 詳細パネルの候補一覧・画面でクリックして選ぶモード
  {
    c.setView(app::View::Gantt);
    c.select("22");  // 依存のない課題（先行の入力欄が上のほうにある）
    u.detailOpen = true;
    u.detailTab = 1;
    frames(3);
    ImGuiIO& io2 = ImGui::GetIO();
    for (float y = 150; y < 700 && !io2.WantTextInput; y += 4) {
      io2.AddMousePosEvent(1150, y);
      io2.AddMouseButtonEvent(0, true);
      frames(1);
      io2.AddMouseButtonEvent(0, false);
      frames(1);
    }
    shot("22_link_picker");
    io2.AddKeyEvent(ImGuiKey_Escape, true);
    frames(1);
    io2.AddKeyEvent(ImGuiKey_Escape, false);
    frames(2);
    io2.AddMousePosEvent(-1, -1);
    u.linkPick = {true, "22", true, c.currentId()};
    shot("23_link_pick_mode");
    u.linkPick = {};
    // Blocks 以外の関連リンク（日程には使わない）
    if (roadmap::Session* ss = c.session()) {
      ss->addOtherLink("22", "Relates", true, "9");
      ss->addOtherLink("22", "Relates", false, "10");
      ss->addOtherLink("22", "Duplicate", true, "PRJ-999");
      c.afterEdit();
      shot("24_other_links");
      for (int k = 0; k < 3; ++k) ss->undo();
      c.afterEdit();
    }
    u.detailOpen = false;
    frames(2);
  }
  // 依存のドラッグ（Issue #8）: バーに載せるとハンドル → ドラッグ中のゴムひも・日程の影響 → つなげない相手は赤
  {
    c.setView(app::View::Gantt);
    c.setUi("zoom", "week");
    c.clearSelection();
    u.customPx = 0;
    u.scrollToBase = true;
    frames(3);
    ImGuiIO& io2 = ImGui::GetIO();
    const Session* s = c.session();
    const Tree& T = *s->tree();
    const auto onScreen = [&](const ui::UiState::BarGeom& g) {
      return g.xl > u.ganttLeft + 40 && g.xr < 1300 && g.cy > 170 && g.cy < 700 && g.xr - g.xl > 12;
    };
    // 先行 A（上の行）→ 後続 B（下の行）で、つなぐと B が遅れる組と、すでにつながっている組（逆向きは循環）を探す
    using Geom = ui::UiState::BarGeom;
    std::optional<Geom> ga, gb, ca, cb;
    for (const auto& a : u.barGeom)
      for (const auto& b : u.barGeom) {
        const int ia = T.find(a.id), ib = T.find(b.id);
        if (ia < 0 || ib < 0 || !T.isLeaf(ia) || !T.isLeaf(ib) || !onScreen(a) || !onScreen(b) || b.cy <= a.cy) continue;
        if (!ga && checkLink(T, s->viewIssues(), ia, ib) == LinkBlock::None && s->previewLink(a.id, b.id).delayDays > 0) ga = a, gb = b;
        if (!ca && std::find(T.succs[ia].begin(), T.succs[ia].end(), ib) != T.succs[ia].end()) ca = a, cb = b;
      }
    const float hd = u.m.s(10.f);
    const auto key = [&](ImGuiKey k) {
      io2.AddKeyEvent(k, true);
      frames(1);
      io2.AddKeyEvent(k, false);
      frames(1);
    };
    if (ga && gb) {
      io2.AddMousePosEvent((ga->xl + ga->xr) / 2, ga->cy);
      shot("24_link_hover");
      io2.AddMousePosEvent(ga->xr + hd, ga->cy);
      frames(2);
      io2.AddMouseButtonEvent(0, true);
      frames(2);
      io2.AddMousePosEvent((gb->xl + gb->xr) / 2, gb->cy);
      shot("25_link_drag_ok");
      io2.AddMousePosEvent(900, 840);  // 相手のいない場所
      shot("25b_link_drag_free");
      key(ImGuiKey_Escape);  // 取り消し
      io2.AddMouseButtonEvent(0, false);
      frames(3);
    }
    if (ca && cb) {
      // ca（先行）→ cb（後続）はすでにある。後続の右のハンドルから先行へドラッグすると循環で赤
      io2.AddMousePosEvent(cb->xr + hd, cb->cy);
      frames(2);
      io2.AddMouseButtonEvent(0, true);
      frames(2);
      io2.AddMousePosEvent((ca->xl + ca->xr) / 2, ca->cy);
      shot("26_link_drag_bad");
      key(ImGuiKey_Escape);
      io2.AddMouseButtonEvent(0, false);
      frames(3);
    }
    io2.AddMousePosEvent(-1, -1);
    frames(2);
  }
  // 依存のドラッグ（Issue #8）一覧・ボード: 行・カードに載せるとハンドル → ドラッグ中 → つなげない相手は赤。ライト・ダーク
  {
    using Slot = ui::UiState::LinkSlot;
    ImGuiIO& io4 = ImGui::GetIO();
    const auto listBoardShots = [&](const std::string& tag) {
      for (const app::View view : {app::View::List, app::View::Board}) {
        const bool isList = view == app::View::List;
        const std::string nm = isList ? "list" : "board";
        c.setView(view);
        c.clearSelection();
        u.detailOpen = false;
        u.sortKey.clear();
        u.sortDir = 0;
        io4.AddMousePosEvent(-1, -1);
        frames(3);
        const Session* s = c.session();
        const Tree& T = *s->tree();
        // 先行 A（上）→ 後続 B（下）で、つなぐと B が遅れる組と、すでにつながっている組（逆向きは循環）を探す
        std::optional<Slot> sa, sb, ca, cb;
        for (const auto& a : u.linkSlots)
          for (const auto& b : u.linkSlots) {
            if (a.idx < 0 || b.idx < 0 || a.idx == b.idx || !T.isLeaf(a.idx) || !T.isLeaf(b.idx) || b.a.y <= a.a.y || a.a.y < 160 ||
                b.b.y > 700)
              continue;
            if (!sa && checkLink(T, s->viewIssues(), a.idx, b.idx) == LinkBlock::None && s->previewLink(T.ids[a.idx], T.ids[b.idx]).delayDays > 0)
              sa = a, sb = b;
            if (!ca && std::find(T.succs[a.idx].begin(), T.succs[a.idx].end(), b.idx) != T.succs[a.idx].end()) ca = a, cb = b;
          }
        const auto key = [&](ImGuiKey k) {
          io4.AddKeyEvent(k, true);
          frames(1);
          io4.AddKeyEvent(k, false);
          frames(1);
        };
        if (sa && sb) {
          const float cx = isList ? sb->a.x + 150 : (sb->a.x + sb->b.x) / 2, cy = (sb->a.y + sb->b.y) / 2;
          io4.AddMousePosEvent(isList ? sa->a.x + 150 : (sa->a.x + sa->b.x) / 2, (sa->a.y + sa->b.y) / 2);
          shot("27_link_" + nm + "_hover_" + tag);
          io4.AddMousePosEvent(sa->hr.x, sa->hr.y);
          shot("27b_link_" + nm + "_handle_" + tag);
          io4.AddMouseButtonEvent(0, true);
          frames(2);
          io4.AddMousePosEvent(cx, cy);
          shot("28_link_" + nm + "_drag_ok_" + tag);
          io4.AddMousePosEvent(isList ? 700.f : 1100.f, 840.f);  // 相手のいない場所
          shot("28b_link_" + nm + "_drag_free_" + tag);
          key(ImGuiKey_Escape);
          io4.AddMouseButtonEvent(0, false);
          frames(3);
          // 左のハンドル（つかんだ方が後続）
          io4.AddMousePosEvent(sb->hl.x, sb->hl.y);
          frames(2);
          io4.AddMouseButtonEvent(0, true);
          frames(2);
          io4.AddMousePosEvent(isList ? sa->a.x + 150 : (sa->a.x + sa->b.x) / 2, (sa->a.y + sa->b.y) / 2);
          shot("28c_link_" + nm + "_drag_left_" + tag);
          key(ImGuiKey_Escape);
          io4.AddMouseButtonEvent(0, false);
          frames(3);
        }
        if (ca && cb) {
          // ca（先行）→ cb（後続）はすでにある。後続の右のハンドルから先行へドラッグすると循環で赤
          io4.AddMousePosEvent(cb->hr.x, cb->hr.y);
          frames(2);
          io4.AddMouseButtonEvent(0, true);
          frames(2);
          io4.AddMousePosEvent(isList ? ca->a.x + 150 : (ca->a.x + ca->b.x) / 2, (ca->a.y + ca->b.y) / 2);
          shot("29_link_" + nm + "_drag_bad_" + tag);
          key(ImGuiKey_Escape);
          io4.AddMouseButtonEvent(0, false);
          frames(3);
        }
        io4.AddMousePosEvent(-1, -1);
        frames(2);
      }
    };
    listBoardShots("light");
    u.t = &ui::darkTokens();
    ui::applyStyle(*u.t, u.m);
    listBoardShots("dark");
    u.t = &ui::lightTokens();
    ui::applyStyle(*u.t, u.m);
    frames(2);
  }
  // 依存のドラッグ（Issue #8）: 空いた場所で離すと出るメニュー「新しい後続（先行）の課題を作ってつなぐ」を、ガント・一覧・ボードで
  {
    ImGuiIO& io5 = ImGui::GetIO();
    const auto menuShots = [&](const std::string& tag) {
      for (const app::View view : {app::View::Gantt, app::View::List, app::View::Board}) {
        const std::string nm = view == app::View::Gantt ? "gantt" : view == app::View::List ? "list" : "board";
        c.setView(view);
        c.setUi("zoom", "week");
        c.clearSelection();
        u.detailOpen = false;
        u.sortKey.clear();
        u.sortDir = 0;
        u.customPx = 0;
        u.scrollToBase = true;
        io5.AddMousePosEvent(-1, -1);
        frames(3);
        const Tree& T = *c.session()->tree();
        // つかむハンドル（右）と離す場所
        ImVec2 hp(-1, -1);
        bool found = false;
        if (view == app::View::Gantt) {
          for (const auto& g : u.barGeom) {
            const int i = T.find(g.id);
            if (!found && i >= 0 && T.isLeaf(i) && g.xl > u.ganttLeft + 40 && g.xr < 1300 && g.cy > 220 && g.cy < 520 && g.xr - g.xl > 12)
              hp = ImVec2(g.xr + u.m.s(10.f), g.cy), found = true;
          }
        } else {
          for (const auto& sl : u.linkSlots)
            if (!found && sl.idx >= 0 && T.isLeaf(sl.idx) && sl.a.y > 200 && sl.b.y < 520 && sl.a.x > 10 && sl.b.x < 1430)
              hp = sl.hr, found = true;
        }
        if (!found) continue;
        io5.AddMousePosEvent(hp.x, hp.y);
        frames(2);
        io5.AddMouseButtonEvent(0, true);
        frames(2);
        io5.AddMousePosEvent(view == app::View::Board ? 700.f : 900.f, view == app::View::Board ? 600.f : 835.f);  // 行・カードのない場所
        shot("30a_link_" + nm + "_drag_empty_" + tag);  // 空き領域の上: 離すとメニューを出す、のツールチップ
        io5.AddMouseButtonEvent(0, false);
        frames(4);
        shot("30b_link_" + nm + "_menu_" + tag);
        // 「新しい後続の課題を作ってつなぐ」: 作った課題の件名をその場で編集している様子。取り消しは Undo 1回
        {
          const auto& it = u.linkMenu.succItem;
          io5.AddMousePosEvent((it.first.x + it.second.x) / 2, (it.first.y + it.second.y) / 2);
          frames(2);
          io5.AddMouseButtonEvent(0, true);
          frames(2);
          io5.AddMouseButtonEvent(0, false);
          frames(4);
          shot("30c_link_" + nm + "_created_" + tag);
        }
        io5.AddKeyEvent(ImGuiKey_Escape, true);
        frames(1);
        io5.AddKeyEvent(ImGuiKey_Escape, false);
        frames(2);
        c.undo();
        frames(2);
        io5.AddMousePosEvent(-1, -1);
        frames(2);
      }
    };
    menuShots("light");
    u.t = &ui::darkTokens();
    ui::applyStyle(*u.t, u.m);
    menuShots("dark");
    u.t = &ui::lightTokens();
    ui::applyStyle(*u.t, u.m);
    frames(2);
  }
  // 依存の矢印（Issue #10）: ホバー・つなぎ替え・外す・たどる・クリティカル・日付の衝突を、ライト・ダークで
  {
    using Geom = ui::UiState::BarGeom;
    using AGeom = ui::UiState::ArrowGeom;
    ImGuiIO& io3 = ImGui::GetIO();
    const auto arrowShots = [&](const std::string& tag) {
      c.setView(app::View::Gantt);
      c.setUi("zoom", "week");
      c.clearSelection();
      u.customPx = 0;
      u.scrollToBase = true;
      u.detailOpen = false;
      frames(3);
      Session* s = c.session();
      const auto linked = [&](const std::string& from, const std::string& to) {
        const Issue* o = c.session()->issue(from);
        return o && std::find(o->blocks.begin(), o->blocks.end(), to) != o->blocks.end();
      };
      const auto onScreen = [&](ImVec2 pt) { return pt.x > u.ganttLeft + 24 && pt.x < 1300 && pt.y > 170 && pt.y < 700; };
      const auto at = [&](const AGeom& a, float frac) {
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
      int nConflict = 0, nCritical = 0;
      for (const auto& a : u.arrowGeom) nConflict += a.conflict, nCritical += a.critical;
      std::printf("arrows=%zu conflict=%d critical=%d\n", u.arrowGeom.size(), nConflict, nCritical);
      // つなぎ替えできる矢印（先行・後続とも画面の葉）
      std::optional<AGeom> pick;
      for (const auto& a : u.arrowGeom) {
        bool ok = a.pts.size() >= 4 && linked(a.from, a.to);
        for (const auto& pt : a.pts) ok = ok && onScreen(pt);
        const Tree& T = *s->tree();
        ok = ok && T.find(a.from) >= 0 && T.find(a.to) >= 0 && T.isLeaf(T.find(a.from)) && T.isLeaf(T.find(a.to));
        if (!ok) continue;
        for (float fr : {0.75f, 0.7f, 0.8f, 0.65f, 0.85f}) {
          const ImVec2 pt = at(a, fr);
          io3.AddMousePosEvent(pt.x, pt.y);
          frames(2);
          if (u.arrowHoverFrom == a.from && u.arrowHoverTo == a.to) {
            pick = a;
            break;
          }
        }
        if (pick) break;
      }
      if (pick) {
        shot("40_arrow_hover_" + tag);
        io3.AddMouseButtonEvent(0, true);
        frames(2);
        std::optional<Geom> tg;
        for (const auto& g : u.barGeom) {
          const Tree& T = *s->tree();
          const int i = T.find(g.id);
          if (i < 0 || !T.isLeaf(i) || g.id == pick->from || g.id == pick->to || g.xl < u.ganttLeft + 30 || g.xr > 1300 || g.cy < 175 ||
              g.cy > 700 || g.xr - g.xl < 12)
            continue;
          if (s->previewLink(pick->from, g.id, pick->from, pick->to).block == LinkBlock::None) {
            tg = g;
            break;
          }
        }
        if (tg) {
          io3.AddMousePosEvent((tg->xl + tg->xr) / 2, tg->cy);
          shot("41_arrow_reattach_" + tag);
        }
        io3.AddMousePosEvent(u.ganttLeft + 6, 520);  // バーのない場所（外す）
        shot("42_arrow_remove_" + tag);
        io3.AddKeyEvent(ImGuiKey_Escape, true);
        frames(1);
        io3.AddKeyEvent(ImGuiKey_Escape, false);
        frames(1);
        io3.AddMouseButtonEvent(0, false);
        frames(2);
      }
      io3.AddMousePosEvent(-1, -1);
      frames(2);
      // 依存をたどる + クリティカルの強調
      std::string withLinks;
      {
        const Tree& T = *s->tree();
        for (int i : T.leafOrder)
          if (!T.preds[i].empty() && !T.succs[i].empty()) {
            withLinks = T.ids[i];
            break;
          }
      }
      if (!withLinks.empty()) c.select(withLinks);
      c.setUiSub("dep", "trace", true);
      shot("43_trace_" + tag);
      c.setUiSub("dep", "trace", false);
      c.clearSelection();
      c.setUiSub("dep", "crit", true);
      shot("44_critical_" + tag);
      c.setUiSub("dep", "crit", false);
      frames(2);
    };
    c.setView(app::View::Gantt);
    arrowShots("light");
    // 日付の衝突（進行中の後続が先行より前から始まっている）と、余裕 0 の連鎖（期限ちょうど）を作って見る
    Session* s3 = c.session();
    const std::size_t undo3 = s3->undoSize();
    {
      std::string dev7;
      for (int i = 0; i < s3->tree()->n; ++i)
        if (s3->tree()->did[i] == "DEV-7") dev7 = s3->tree()->ids[i];
      s3->setField(dev7, [&](Issue& o) {
        o.status = "In Progress";
        o.start = s3->baseDay() - 7;
      });
      c.afterEdit();
      frames(2);
      std::vector<std::pair<std::string, Day>> ends;  // 末端の課題（後続なし）を、いまの完了日を期限にする
      for (int i : s3->tree()->leafOrder)
        if (s3->tree()->succs[i].empty() && !(*s3->rows())[i].done) ends.emplace_back(s3->tree()->ids[i], (*s3->rows())[i].asapFinish);
      for (const auto& [id, d] : ends) s3->setDeadline(id, d);
      c.afterEdit();
      frames(2);
    }
    c.setUiSub("dep", "crit", true);
    shot("45_conflict_critical_light");
    c.setUiSub("dep", "crit", false);
    u.t = &ui::darkTokens();
    ui::applyStyle(*u.t, u.m);
    arrowShots("dark");
    c.setUiSub("dep", "crit", true);
    shot("45_conflict_critical_dark");
    c.setUiSub("dep", "crit", false);
    while (s3->undoSize() > undo3 && s3->undo()) {
    }
    c.afterEdit();
    u.t = &ui::lightTokens();
    ui::applyStyle(*u.t, u.m);
    io3.AddMousePosEvent(-1, -1);
    frames(2);
  }
  // アプリの設定（保存先の切り替えの確認中）・削除の確認
  u.dlg = ui::Dlg::AppSettings;
  u.dlgOpening = true;
  frames(2);
  shot("17_app_settings");
  c.prefs().newNameFormat = "custom";
  c.select("11");
  shot("17b_app_settings_newname");
  c.prefs().newNameFormat = "keep";
  u.pendingDir = "D:/共有/projects";
  u.pendingCount = 0;
  u.pendingCopy = true;
  shot("18_app_settings_switch");
  u.pendingDir.clear();
  u.dlg = ui::Dlg::None;
  frames(2);
  u.pendingDelete = {"11"};
  u.dlg = ui::Dlg::ConfirmIssueDelete;
  u.dlgOpening = true;
  shot("19_confirm_delete");
  u.dlg = ui::Dlg::None;
  frames(2);
  // 貼り付けで子課題をまとめて作る（プレビュー）
  u.pasteParent = "11";
  u.paste = parseOutline("要件の整理\n\t画面の洗い出し\n\t\t\t飛んだ階層\n\tデータ項目\n- 設計\n  - [ ] 画面設計\n  - [ ] DB設計\n実装\nテスト\t山田\t2\nレビュー\n公開準備\n振り返り\n記録の整理\n");
  u.dlg = ui::Dlg::PasteChildren;
  u.dlgOpening = true;
  shot("22_paste_children");
  u.dlg = ui::Dlg::None;
  frames(2);
  // 添付: 詳細パネルの一覧・行とカードのバッジ・実行ファイルの警告
  {
    const std::pair<const char*, std::size_t> files[] = {
        {"仕様書_v3.pdf", 2300000}, {"画面キャプチャ.bmp", 48000}, {"議事録 2026-10-19.docx", 18500}, {"setup.exe", 2},
        {"写真.bmp", 0}, {"壊れた画像.png", 30}};
    std::vector<std::string> paths;
    for (const auto& [name, size] : files) {
      const std::string n = name;
      fs.put("ext/" + n, n == "画面キャプチャ.bmp" ? makeBmp(320, 200, 0) : n == "写真.bmp" ? makeBmp(640, 360, 1) : std::string(size, 'x'));
      paths.push_back(std::string("ext/") + name);
    }
    c.attachFiles("11", paths);
    c.attachFiles("12", {paths[1]});
    c.setView(app::View::List);
    c.select("11");
    u.detailOpen = true;
    u.detailTab = 3;
    shot("40_attach_tab");
    c.setView(app::View::Board);
    shot("41_attach_board");
    c.setView(app::View::Gantt);
    c.select("13");  // 添付のない課題
    shot("42_attach_empty");
    c.select("11");
    frames(2);
    // 行・カードのバッジにマウスを載せたときのツールチップ（先頭 4 件。画像はサムネイル、そのほかは名前だけ。残りは「ほか N 件」）と、
    // 添付タブの項目にマウスを載せたときの詳細（SHA-256 の先頭 12 文字）
    {
      ImGuiIO& hio = ImGui::GetIO();
      c.attachFiles("12", {paths[4], paths[0], paths[2], paths[5]});
      c.attachFiles("8", {paths[1], paths[4], paths[0], paths[2], paths[5]});
      c.setView(app::View::List);
      c.clearSelection();
      u.detailOpen = false;
      hio.AddMousePosEvent(5, 890);
      frames(4);
      const auto badgeMid = [&](const std::string& id) {
        for (const auto& b : u.attachBadges)
          if (b.id == id && b.a.y > 120 && b.b.y < 850) return ImVec2((b.a.x + b.b.x) / 2, (b.a.y + b.b.y) / 2);
        return ImVec2(-1, -1);
      };
      if (const ImVec2 m = badgeMid("12"); m.x >= 0) {
        hio.AddMousePosEvent(m.x, m.y);
        frames(30);
        shot("48_attach_tip_list");
      }
      c.setView(app::View::Board);
      hio.AddMousePosEvent(700, 500);
      for (int k = 0; k < 8; ++k) {
        hio.AddMouseWheelEvent(0, 10);
        frames(2);
      }
      if (const ImVec2 m = badgeMid("8"); m.x >= 0) {
        hio.AddMousePosEvent(m.x, m.y);
        frames(30);
        shot("48b_attach_tip_board");
      }
      c.setView(app::View::List);
      c.select("11");
      u.detailOpen = true;
      u.detailTab = 3;
      hio.AddMousePosEvent(5, 890);
      frames(6);
      if (u.attachRows.size() > 1) {
        const auto& r = u.attachRows[0];  // 仕様書_v3.pdf
        hio.AddMousePosEvent(r.first.x + 220, (r.first.y + r.second.y) / 2);
        frames(30);
        shot("49_attach_hash");
      }
      // 詳細パネルは開いたままにする（このあとのドラッグ・サムネイルの大きさの画面が添付タブを映すため）
      hio.AddMousePosEvent(5, 890);
      frames(4);
    }
    // サムネイルをつかんで別の課題の行へ（移動／Ctrl でコピー）と、サムネイルを大きくした一覧
    {
      c.setView(app::View::List);
      c.select("11");
      frames(4);
      ImGuiIO& dio = ImGui::GetIO();
      if (u.attachRows.size() > 1) {
        const auto& r = u.attachRows[1];  // 画面キャプチャ.bmp
        const ImVec2 from(r.first.x + 60, (r.first.y + r.second.y) / 2);
        ImVec2 to(600, 300);
        for (const auto& t : u.dropTargets)
          if (t.id == "12") to = ImVec2((t.a.x + t.b.x) / 2, (t.a.y + t.b.y) / 2);
        dio.AddMousePosEvent(from.x, from.y);
        frames(2);
        dio.AddMouseButtonEvent(0, true);
        frames(2);
        dio.AddMousePosEvent(from.x + 12, from.y + 4);
        frames(2);
        dio.AddMousePosEvent(to.x, to.y);
        frames(2);
        shot("46_attach_drag_move");
        dio.AddKeyEvent(ImGuiMod_Ctrl, true);
        shot("46b_attach_drag_copy");
        dio.AddKeyEvent(ImGuiMod_Ctrl, false);
        dio.AddMousePosEvent(900, 3);  // 何もないところ
        shot("46c_attach_drag_nowhere");
        dio.AddKeyEvent(ImGuiKey_Escape, true);
        frames(2);
        dio.AddKeyEvent(ImGuiKey_Escape, false);
        dio.AddMouseButtonEvent(0, false);
        frames(3);
      }
      c.setUi("attachThumb", 84);
      shot("47_attach_big_thumb");
      c.setUi("attachThumb", 36);
      frames(2);
    }
    ui::Frame fr{c, u, p};
    ui::openAttachment(fr, "11", 3);
    shot("43_attach_warn");
    u.dlg = ui::Dlg::None;
    u.attachWarnPath.clear();
    frames(2);
    ui::openImagePreview(fr, "11", 4);  // 写真.bmp
    shot("44_attach_preview");
    u.dlg = ui::Dlg::None;
    frames(2);
    ui::openImagePreview(fr, "11", 5);  // 壊れた画像.png
    shot("45_attach_preview_broken");
    u.dlg = ui::Dlg::None;
    u.detailOpen = false;
    frames(2);
  }
  u.t = &ui::darkTokens();
  ui::applyStyle(*u.t, u.m);
  c.session()->setResource({true, 1});
  c.afterEdit();
  shot("12_dark_leveling");
  // 親の名前の見え方（並べ替え中の平らな一覧・その場編集・詳細パネル）を、ライト・ダーク・コンパクトで
  const auto parentShots = [&](const std::string& tag) {
    c.setView(app::View::List);
    c.select("6");
    u.sortKey = "asg";
    u.sortDir = 1;
    shot("30_flat_list_" + tag);
    u.sortKey.clear();
    u.sortDir = 0;
    u.editField = ui::EditField::Name;
    u.editId = "6";
    u.editBuf = "候補の洗い出し";
    u.editStartValue = u.editBuf;
    u.editFocus = true;
    shot("31_edit_parent_" + tag);
    u.editField = ui::EditField::None;
    u.editId.clear();
    u.detailOpen = true;
    shot("32_detail_path_" + tag);
    u.detailOpen = false;
  };
  parentShots("dark");
  u.t = &ui::lightTokens();
  ui::applyStyle(*u.t, u.m);
  parentShots("light");
  c.setDensity("compact");
  u.m.compact = true;  // Windows 版は毎フレーム prefs から入れている
  ui::applyStyle(*u.t, u.m);
  parentShots("compact");
  u.m.compact = false;
  c.setDensity("normal");
  c.createProject(app::SampleKind::EmbeddedSw, "組込みソフトウェア開発", "DEV");
  c.setView(app::View::Gantt);
  c.setUi("zoom", "all");
  shot("17_embedded_template");
  c.setView(app::View::Stats);
  shot("17b_embedded_stats");
  // 統計ビューの担当者別の負荷（ヒートマップ）と完了の推移: 組込み雛形・階層デモ × ライト・ダーク
  {
    // 統計のページをスクロールして、ヒートマップの先頭の行が y の位置に来るようにする
    const auto scrollStatsTo = [&](float y) {
      c.setView(app::View::Stats);
      clk.advanceMs(15000);  // 通知が消えるまで待つ
      frames(3);
      ImGui::GetIO().AddMousePosEvent(700, 450);
      for (int k = 0; k < 120; ++k) {
        if (!u.statsLoadCells.empty() && u.statsLoadCells.front().a.y < y) break;
        ImGui::GetIO().AddMouseWheelEvent(0, -1);
        frames(2);
      }
    };
    const auto statsShots = [&](const std::string& name) {
      scrollStatsTo(360);
      shot(name + "_load");
      for (int k = 0; k < 14; ++k) {
        ImGui::GetIO().AddMouseWheelEvent(0, -1);
        frames(2);
      }
      shot(name + "_done");
      // セルにマウスを載せたときのツールチップ
      scrollStatsTo(360);
      if (!u.statsLoadCells.empty()) {
        std::size_t best = 0;
        for (std::size_t k = 0; k < u.statsLoadCells.size(); ++k)
          if (u.statsLoadCells[k].percent > u.statsLoadCells[best].percent) best = k;
        const auto& cell = u.statsLoadCells[best];
        ImGui::GetIO().AddMousePosEvent((cell.a.x + cell.b.x) / 2, (cell.a.y + cell.b.y) / 2);
        frames(2);
        shot(name + "_tooltip");
        ImGui::GetIO().AddMousePosEvent(5, 5);
        frames(2);
      }
    };
    u.t = &ui::lightTokens();
    ui::applyStyle(*u.t, u.m);
    statsShots("50_stats_load_embedded_light");
    u.t = &ui::darkTokens();
    ui::applyStyle(*u.t, u.m);
    statsShots("51_stats_load_embedded_dark");
    c.createProject(app::SampleKind::Hierarchy, "階層デモ（統計）", "DEV");
    statsShots("52_stats_load_hierarchy_dark");
    u.t = &ui::lightTokens();
    ui::applyStyle(*u.t, u.m);
    c.session()->setResource({true, 1});
    c.afterEdit();
    statsShots("53_stats_load_hierarchy_light_resource");
  }
  // 統計ビューの「推移」（Issue #54 の A）: バーンダウン（ペース予測つき）・リードタイム・滞留日数 × ライト・ダーク、
  // ホバー、予測が出ない場合、Created 列がない場合
  {
    const Day base = *parseIso("2026-10-19");
    const auto trendCsv = [&](bool withCols, int recentDone) {
      std::string t = "Issue Id,Issue Type,Summary,Parent,Assignee,Status,Priority,";
      if (withCols) t += "Created,Updated,";
      t += "Resolved,Duration (weeks)\n";
      t += withCols ? "1,Epic,基盤,,,,High,2026-06-01,,,\n2,Epic,画面,,,,High,2026-06-01,,,\n" : "1,Epic,基盤,,,,High,,\n2,Epic,画面,,,,High,,\n";
      const char* const who[] = {"田中", "鈴木", "佐藤", "高橋"};
      for (int i = 0; i < 72; ++i) {
        const Day created = base - 140 + i * 2;
        // 前半はほぼ完了、後半は recentDone 件ごとに完了
        const bool done = i < 48 ? (i % 4 != 3) : (recentDone > 0 && i % recentDone == 0);
        const Day resolved = std::min<Day>(created + 6 + (i * 7) % 28, base - 1);
        const Day updated = std::min<Day>(created + (i * 5) % 70, base);
        t += std::to_string(i + 3) + ",Task,課題" + std::to_string(i + 1) + "の作業," + (i < 36 ? "1" : "2") + "," + who[i % 4] + "," +
             (done ? "Done" : (i % 5 == 0 ? "In Progress" : "To Do")) + ",Medium,";
        if (withCols) t += fmtIso(created) + "," + (done ? std::string() : (i % 6 == 0 ? std::string() : fmtIso(updated))) + ",";
        t += (done ? fmtIso(resolved) : std::string()) + ",1\n";
      }
      return t;
    };
    const auto scrollTo = [&](const std::function<bool()>& reached) {
      c.setView(app::View::Stats);
      clk.advanceMs(15000);
      frames(3);
      ImGui::GetIO().AddMousePosEvent(700, 450);
      for (int k = 0; k < 30; ++k) {  // いったん先頭へ戻す（前のプロジェクトのスクロール位置が残っている）
        ImGui::GetIO().AddMouseWheelEvent(0, 20);
        frames(1);
      }
      for (int k = 0; k < 200 && !reached(); ++k) {
        ImGui::GetIO().AddMouseWheelEvent(0, -1);
        frames(2);
      }
    };
    // ポインタを統計の外へ出してから撮る（ツールチップが写り込まない）。スクロールはポインタが統計の上にあるときだけ効く
    const auto awayShot = [&](const std::string& name) {
      ImGui::GetIO().AddMousePosEvent(5, 5);
      frames(2);
      shot(name);
    };
    const auto overStats = [&] {
      ImGui::GetIO().AddMousePosEvent(700, 450);
      frames(2);  // ホバー先の更新（次のフレームでホイールが統計に届く）
    };
    const auto trendShots = [&](const std::string& name) {
      scrollTo([&] { return u.statsTrend.drawn && u.statsTrend.whoA.y < 150; });
      awayShot(name + "_burn");
      // 週の上にマウスを載せたときのツールチップ
      if (u.statsTrend.weekCols.size() > 5) {
        const auto& col = u.statsTrend.weekCols[u.statsTrend.weekCols.size() - 3];
        ImGui::GetIO().AddMousePosEvent((col.a.x + col.b.x) / 2, (col.a.y + col.b.y) / 2);
        frames(2);
        shot(name + "_tip");
      }
      overStats();
      for (int k = 0; k < 6; ++k) {  // リードタイムの節が上に来るところまで（下まで行き過ぎない）
        ImGui::GetIO().AddMouseWheelEvent(0, -1);
        frames(2);
      }
      awayShot(name + "_lead");
      scrollTo([&] { return !u.statsTrend.agingRows.empty() && u.statsTrend.agingRows.back().b.y < 880; });
      awayShot(name + "_aging");
    };
    c.importText(trendCsv(true, 3), "推移（作成日・更新日あり）", app::ImportMode::New);
    u.t = &ui::lightTokens();
    ui::applyStyle(*u.t, u.m);
    trendShots("60_trends_light");
    u.t = &ui::darkTokens();
    ui::applyStyle(*u.t, u.m);
    trendShots("61_trends_dark");
    // ペース予測が出ない場合（直近に完了がほとんどない）
    u.t = &ui::lightTokens();
    ui::applyStyle(*u.t, u.m);
    {
      // 書き出した HTML の「推移」（ブラウザで開いて確かめる。ライト・ダーク）
      for (const bool dark : {false, true}) {
        ExportOptions opt;
        opt.gantt = opt.list = false;
        opt.dark = dark;
        const auto html = c.buildReport(opt);
        if (html.ok()) {
          const std::string file = dark ? "html_trends_dark.html" : "html_trends_light.html";
          std::ofstream o(out + "/" + file, std::ios::binary);
          o << html.value();
          std::printf("wrote %s/%s (%zu KB)\n", out.c_str(), file.c_str(), html.value().size() / 1024);
        }
      }
    }
    c.importText(trendCsv(true, 0), "推移（直近の完了なし）", app::ImportMode::New);
    scrollTo([&] { return u.statsTrend.drawn && u.statsTrend.whoA.y < 150; });
    awayShot("62_trends_noforecast");
    // 担当者で絞った状態
    u.statsTrend.who = TrendFilter::Who::Name;
    u.statsTrend.name = "鈴木";
    awayShot("63_trends_filtered");
    // Created・Updated の列がない CSV
    c.importText(trendCsv(false, 3), "推移（日付の列なし）", app::ImportMode::New);
    scrollTo([&] { return u.statsTrend.drawn && u.statsTrend.whoA.y < 150; });
    awayShot("64_trends_nocols");
    c.deleteCurrentProject();
    c.deleteCurrentProject();
    c.deleteCurrentProject();
    u.t = &ui::lightTokens();
    ui::applyStyle(*u.t, u.m);
  }
  // 統計ビューの「状況の件数の推移（累積フロー）」（Issue #64）: ライト・ダーク・記録なしの週あり・案内（記録 1 件）・狭い幅・ホバー
  {
    const auto goTo = [&](Day d) {
      const Ymd ymd = civilFromDays(d);
      clk.set(LocalTime{ymd.y, ymd.m, ymd.d, 18, 36, 0});
    };
    const auto cfdScroll = [&](const std::function<bool()>& reached) {
      c.setView(app::View::Stats);
      clk.advanceMs(15000);
      frames(3);
      ImGui::GetIO().AddMousePosEvent(700, 450);
      for (int k = 0; k < 40; ++k) {
        ImGui::GetIO().AddMouseWheelEvent(0, 20);
        frames(1);
      }
      for (int k = 0; k < 300 && !reached(); ++k) {
        ImGui::GetIO().AddMouseWheelEvent(0, -1);
        frames(2);
      }
    };
    const auto cfdAway = [&](const std::string& name) {
      ImGui::GetIO().AddMousePosEvent(5, 5);
      frames(2);
      shot(name);
    };
    const auto cfdVisible = [&] {
      return u.statsCfd.drawn && u.statsCfd.recA.y > 130 && u.statsCfd.recA.y < 520 &&
             (!u.statsCfd.chart || (!u.statsCfd.cols.empty() && u.statsCfd.cols.back().b.y < 880));
    };
    const Day first = *parseIso("2026-07-27");
    c.createProject(app::SampleKind::Hierarchy, "累積フロー（階層デモ）", "CFD");
    Session* s = c.session();
    cfdScroll(cfdVisible);
    cfdAway("65_cfd_pending");  // 記録 1 件: 案内だけ
    // 12 週ぶんを少しずつ進めながら記録する。第 4・5・8 週は開かなかった（記録なし）
    std::vector<std::string> leafIds;
    for (int id : s->tree()->leafOrder)
      if (s->tree()->kind[static_cast<std::size_t>(id)] != Kind::Ms) leafIds.push_back(s->tree()->ids[static_cast<std::size_t>(id)]);
    for (int w = 0; w <= 12; ++w) {
      const std::size_t doneN = leafIds.size() * static_cast<std::size_t>(w) * static_cast<std::size_t>(w) / (12 * 12 + 40);
      const std::size_t doingN = std::min(leafIds.size() - doneN, std::size_t(4 + w / 2));
      for (std::size_t k = 0; k < leafIds.size(); ++k) {
        const std::string st = k < doneN ? "Done" : (k < doneN + doingN ? "In Progress" : "To Do");
        s->setField(leafIds[k], [&](Issue& o) {
          o.status = st;
          if (st != "Done") o.resolved = OptDay{};
        });
      }
      c.afterEdit();
      if (w == 4 || w == 5 || w == 8) continue;  // 開かなかった週
      goTo(first + w * 7 + 2);
      c.recordHistory(app::AppController::HistoryTrigger::Manual);
    }
    goTo(*parseIso("2026-10-19"));
    u.t = &ui::lightTokens();
    ui::applyStyle(*u.t, u.m);
    cfdScroll(cfdVisible);
    cfdAway("66_cfd_light");
    if (u.statsCfd.cols.size() > 6) {  // 記録のない週（第 4 週）にマウスを載せる
      const auto& col = u.statsCfd.cols[4];
      ImGui::GetIO().AddMousePosEvent((col.a.x + col.b.x) / 2, (col.a.y + col.b.y) / 2);
      frames(2);
      shot("66b_cfd_gap_tip");
      const auto& col2 = u.statsCfd.cols[10];  // 記録のある週
      ImGui::GetIO().AddMousePosEvent((col2.a.x + col2.b.x) / 2, (col2.a.y + col2.b.y) / 2);
      frames(2);
      shot("66c_cfd_week_tip");
    }
    u.t = &ui::darkTokens();
    ui::applyStyle(*u.t, u.m);
    cfdScroll(cfdVisible);
    cfdAway("67_cfd_dark");
    // 狭い幅（詳細パネルを開く）
    u.t = &ui::lightTokens();
    ui::applyStyle(*u.t, u.m);
    io.DisplaySize = ImVec2(900, 900);
    u.detailOpen = true;
    cfdScroll(cfdVisible);
    cfdAway("68_cfd_narrow");
    // 未着手・進行中・完了がどれもある状態にしてから（境界が重ならない）
    for (std::size_t k = 0; k < leafIds.size(); ++k) {
      const std::string st = k < 5 ? "Done" : (k < 9 ? "In Progress" : "To Do");
      s->setField(leafIds[k], [&](Issue& o) {
        o.status = st;
        if (st != "Done") o.resolved = OptDay{};
      });
    }
    c.afterEdit();
    // 104 週・件数が一定・幅が最小（破線・点線が 1 区間より長いパターンで続くことを確かめる）
    for (int w = 0; w < 110; ++w) {
      goTo(first + 12 * 7 + w * 7 + 2);
      c.recordHistory(app::AppController::HistoryTrigger::Manual);
    }
    io.DisplaySize = ImVec2(620, 900);
    frames(4);
    ImGui::GetIO().AddMousePosEvent(150, 450);  // 詳細パネルの上ではなく、統計の上でスクロールする
    for (int k = 0; k < 60; ++k) {
      ImGui::GetIO().AddMouseWheelEvent(0, -2);
      frames(1);
    }
    clk.advanceMs(15000);  // 記録の通知（左下）が消えてから撮る
    frames(3);
    cfdAway("69_cfd_104w_narrow");
    io.DisplaySize = ImVec2(1440, 900);
    u.detailOpen = false;
    goTo(*parseIso("2026-10-19"));
    // 書き出した HTML（ブラウザで開いて確かめる。ライト・ダーク）
    for (const bool dark : {false, true}) {
      ExportOptions opt;
      opt.gantt = opt.list = false;
      opt.dark = dark;
      const auto html = c.buildReport(opt);
      if (html.ok()) {
        const std::string file = dark ? "html_cfd_dark.html" : "html_cfd_light.html";
        std::ofstream o(out + "/" + file, std::ios::binary);
        o << html.value();
        std::printf("wrote %s/%s (%zu KB)\n", out.c_str(), file.c_str(), html.value().size() / 1024);
      }
    }
    c.deleteCurrentProject();
    goTo(*parseIso("2026-10-19"));
    u.t = &ui::lightTokens();
    ui::applyStyle(*u.t, u.m);
  }
  // HTML に書き出す（Issue #42）: 「…」メニュー・ダイアログ・通知と、書き出した HTML（ブラウザで開いて確かめる）
  {
    const auto dumpHtml = [&](const std::string& file, ExportOptions opt) {
      const auto html = c.buildReport(opt);
      if (!html.ok()) {
        std::fprintf(stderr, "報告を作れません: %s\n", html.error().c_str());
        return;
      }
      std::ofstream o(out + "/" + file, std::ios::binary);
      o << html.value();
      std::printf("wrote %s/%s (%zu KB)\n", out.c_str(), file.c_str(), html.value().size() / 1024);
    };
    u.t = &ui::lightTokens();
    ui::applyStyle(*u.t, u.m);
    c.createProject(app::SampleKind::Hierarchy, "階層デモ（セキュアブート）", "DEV");
    c.setView(app::View::Gantt);
    c.setUi("zoom", "week");
    c.setUi("colorBy", "root");
    c.clearSelection();
    c.select("11");
    frames(3);
    // 「…」メニュー（右上のボタンを探して押す）
    {
      ImGuiIO& mio = ImGui::GetIO();
      ImGuiContext& g = *ImGui::GetCurrentContext();
      for (float x = 1334; x < 1344 && g.OpenPopupStack.Size == 0; x += 4) {
        mio.AddMousePosEvent(x, 24);
        mio.AddMouseButtonEvent(0, true);
        frames(1);
        mio.AddMouseButtonEvent(0, false);
        frames(1);
      }
      shot("60_more_menu");
      mio.AddKeyEvent(ImGuiKey_Escape, true);
      frames(1);
      mio.AddKeyEvent(ImGuiKey_Escape, false);
      frames(2);
      mio.AddMousePosEvent(-1, -1);
    }
    u.dlg = ui::Dlg::ExportHtml;
    u.dlgOpening = true;
    shot("61_export_html_dialog");
    u.rep.dark = true;
    u.rep.zoom = "all";
    u.repScope = 2;
    shot("61b_export_html_dialog_options");
    u.dlg = ui::Dlg::None;
    frames(2);
    // 通知（書き出しました + フォルダを開く）
    {
      ExportOptions o;
      o.gantt = o.stats = o.list = true;
      (void)fs.makeDirs("out");
      (void)c.exportReport("out/階層デモ_2026-10-19.html", o);
      shot("62_export_toast");
      clk.advanceMs(15000);
      frames(3);
    }
    // 書き出した HTML
    ExportOptions o;
    dumpHtml("html_hierarchy_light.html", o);
    o.dark = true;
    dumpHtml("html_hierarchy_dark.html", o);
    o.dark = false;
    o.zoom = "day";
    o.colorBy = "float";
    dumpHtml("html_hierarchy_day_float.html", o);
    o.zoom = "all";
    o.colorBy = "asg";
    dumpHtml("html_hierarchy_all_asg.html", o);
    o.zoom = "month";
    o.scope = ReportScope::Subtree;
    dumpHtml("html_hierarchy_subtree.html", o);
    c.createProject(app::SampleKind::EmbeddedSw, "組込みソフトウェア開発", "DEV");
    ExportOptions e;
    dumpHtml("html_embedded_light.html", e);
    e.dark = true;
    dumpHtml("html_embedded_dark.html", e);
    e.dark = false;
    e.zoom = "all";
    dumpHtml("html_embedded_all.html", e);
    // 絞り込み（担当者）と、薄くする行
    c.createProject(app::SampleKind::Hierarchy, "絞り込み", "DEV");
    c.filter().flt = FltFilter::Open;
    c.filter().q = "";
    ExportOptions f;
    f.scope = ReportScope::Current;
    dumpHtml("html_hierarchy_current.html", f);
    c.filter() = {};
    // 大きなプロジェクトと、危険な文字を入れた件名
    {
      auto g = generateProject({5000, 7});
      c.importText(exportAppCsv(g.issues, g.meta), "大きい", app::ImportMode::New);
      ExportOptions big;
      dumpHtml("html_big.html", big);
      big.zoom = "day";
      dumpHtml("html_big_day.html", big);
    }
    {
      const std::string csv =
          "Issue Id,Issue Type,Summary,Parent,Assignee,Status,Priority,Duration (weeks),Deadline,Outward issue link (Blocks)\n"
          "1,Epic,\"<script>alert(1)</script>\",,\"\"\"onmouseover=\"\"alert(2)\",,High,,,\n"
          "2,Task,\"A & B <b>太字</b>\",1,\"田中 & 鈴木\",In Progress,High,2,2026-11-06,3\n"
          "3,Task,\"'引用' \"\"二重\"\" </text><text>\",1,\"<img src=x onerror=alert(3)>\",To Do,Highest,3,,\n";
      c.importText(csv, "<危険>&名前", app::ImportMode::New);
      ExportOptions x;
      dumpHtml("html_escape.html", x);
    }
  }
  // 週次レポート（Issue #43）: ダイアログと、書き出した Markdown・HTML（ブラウザで開いて確かめる）
  {
    const auto dumpWeekly = [&](const std::string& stem, const WeeklyOptions& opt) {
      for (const bool html : {false, true}) {
        const auto text = c.renderWeekly(opt, html);
        if (!text.ok()) {
          std::fprintf(stderr, "週次レポートを作れません: %s\n", text.error().c_str());
          return;
        }
        const std::string file = stem + (html ? ".html" : ".md");
        std::ofstream o(out + "/" + file, std::ios::binary);
        o << text.value();
        std::printf("wrote %s/%s (%zu KB)\n", out.c_str(), file.c_str(), text.value().size() / 1024);
      }
    };
    u.t = &ui::lightTokens();
    ui::applyStyle(*u.t, u.m);
    c.createProject(app::SampleKind::Hierarchy, "階層デモ（セキュアブート）", "DEV");
    c.setView(app::View::Gantt);
    c.clearSelection();
    Session* ws = c.session();
    // 先週の状態（基準日 10/12）を書き出して覚えてから、基準日を進めて変更を入れる
    ws->setBaseDate("2026-10-12");
    WeeklyOptions first;
    first.period = WeeklyPeriod::ThisWeek;
    c.recordWeekly(first);
    ws->setBaseDate("2026-10-19");
    {
      // 未完了の作業のうち先頭の 2 件を、今週の月・水に完了にする
      std::vector<std::string> pick;
      for (int id : ws->tree()->leafOrder) {
        const std::size_t k = static_cast<std::size_t>(id);
        if (ws->tree()->kind[k] == Kind::Ms || ws->rows()->at(k).done) continue;
        pick.push_back(ws->tree()->ids[k]);
        if (pick.size() >= 2) break;
      }
      const char* const when[] = {"2026-10-19", "2026-10-21"};
      for (std::size_t q = 0; q < pick.size(); ++q) {
        ws->setStatus(pick[q], "Done");
        ws->setField(pick[q], [&](Issue& is) { is.resolved = parseIso(when[q]); });
      }
    }
    c.setWeeklyNotes("- 良かった点: 署名検証の方式設計が前倒しで完了\n- 課題: 鍵管理の外部レビューが未定。来週早めに日程を決めたい\n- お願い: 量産移管チームの担当者を確認してください");
    u.dlg = ui::Dlg::Weekly;
    u.dlgOpening = true;
    shot("63_weekly_dialog");
    u.wkPeriod = 1;
    u.wk.sections.risks = false;
    shot("63b_weekly_dialog_last_week");
    u.dlg = ui::Dlg::None;
    frames(2);
    WeeklyOptions o;
    o.notes = c.weeklyNotes();
    dumpWeekly("weekly_hierarchy_this", o);
    o.period = WeeklyPeriod::LastWeek;
    dumpWeekly("weekly_hierarchy_last", o);
    o.period = WeeklyPeriod::Custom;
    o.from = *parseIso("2026-10-01");
    o.to = *parseIso("2026-10-18");
    dumpWeekly("weekly_hierarchy_custom", o);
    // 階層デモの基準日を年末にして、休業日・年またぎ
    ws->setBaseDate("2026-12-30");
    WeeklyOptions ye;
    dumpWeekly("weekly_hierarchy_yearend", ye);
    // 組込みの雛形
    c.createProject(app::SampleKind::EmbeddedSw, "組込みソフトウェア開発", "DEV");
    WeeklyOptions emb;
    dumpWeekly("weekly_embedded", emb);
    // 危険な文字を入れた件名・担当者・所感
    {
      const std::string csv =
          "Issue Id,Issue Type,Summary,Parent,Assignee,Status,Priority,Resolved,Duration (weeks),Deadline,Outward issue link (Blocks)\n"
          "1,Epic,\"<script>alert(1)</script>\",,\"\"\"onmouseover=\"\"alert(2)\",,High,,,,\n"
          "2,Task,\"A | B [リンク](http://evil.example) `x` <b>太字</b> & #1\",1,\"田中 | 鈴木\",Done,High,2026-10-20,2,,3\n"
          "3,Task,\"# 見出し風 > 引用風\",1,\"<img src=x onerror=alert(3)>\",In Progress,Highest,,3,2026-10-30,\n";
      c.importText(csv, "<危険>&名前|[x]", app::ImportMode::New);
      c.session()->setBaseDate("2026-10-21");
      WeeklyOptions x;
      x.notes = "<script>alert(4)</script>\n- 所感の箇条書き | 表";
      dumpWeekly("weekly_escape", x);
    }
    // 空のプロジェクト
    c.createProject(app::SampleKind::Empty, "空", "");
    dumpWeekly("weekly_empty", WeeklyOptions{});
  }
  // ベースライン比較（Issue #53）: ガント・リスト・統計（ライト・ダーク）と、保存・管理のダイアログ、HTML・週次の出力
  {
    u.t = &ui::lightTokens();
    ui::applyStyle(*u.t, u.m);
    c.createProject(app::SampleKind::Hierarchy, "階層デモ（セキュアブート）", "DEV");
    c.setView(app::View::Gantt);
    c.clearSelection();
    c.setUi("zoom", "week");
    c.setUi("colorBy", "root");
    Session* bs = c.session();
    bs->setBaseDate("2026-10-19");
    c.afterEdit();
    // 計画を保存してから、いくつかの課題を延ばし・縮め・消し・足す
    (void)c.saveBaseline("キックオフ");
    std::vector<std::string> leaves;
    for (int id : bs->tree()->leafOrder) {
      const std::size_t k = static_cast<std::size_t>(id);
      if (bs->tree()->kind[k] == Kind::Ms || bs->rows()->at(k).done) continue;
      leaves.push_back(bs->tree()->ids[k]);
    }
    for (std::size_t q = 0; q < leaves.size(); ++q) {
      const std::string id = leaves[q];
      if (q % 5 == 0) bs->setField(id, [](Issue& is) { is.durW = (is.durW ? *is.durW : 1.0) + 2.0; });        // 遅れ
      else if (q % 5 == 2) bs->setField(id, [](Issue& is) { is.durW = std::max(0.2, (is.durW ? *is.durW : 1.0) - 0.4); });  // 早まり
    }
    if (leaves.size() > 3) bs->deleteIssues({leaves[3]});  // 最後の課題を消すと、次に足す課題が同じ Issue Id になるので別の課題を消す
    bs->addIssue("", "", "追加した課題");
    c.afterEdit();
    c.setUiSub("cols", "plan", true);
    c.select(leaves.empty() ? std::string() : leaves[0]);
    // 通知が重なって見づらいので、画面の確認の前に片づける
    const auto clearToasts = [&] {
      for (const auto& t : std::vector<app::Toast>(c.toasts())) c.dismissToast(t.id);
    };
    // 統計は「計画との差」の節が見える位置までスクロールする
    const auto scrollStatsToPlan = [&] {
      ImGuiIO& sio = ImGui::GetIO();
      sio.AddMousePosEvent(700, 450);
      const auto visible = [&] {
        return !u.statsPlan.lateRows.empty() && u.statsPlan.lateRows[0].a.y > 200 && u.statsPlan.lateRows[0].b.y < 700;
      };
      for (int k = 0; k < 200 && !visible(); ++k) {
        const bool up = u.statsPlan.lateRows.empty() || u.statsPlan.lateRows[0].a.y <= 200;
        sio.AddMouseWheelEvent(0, up ? 2.f : -2.f);
        frames(2);
      }
    };
    clearToasts();
    shot("64_baseline_gantt");
    c.setView(app::View::List);
    shot("64c_baseline_list");
    c.setView(app::View::Stats);
    frames(3);
    scrollStatsToPlan();
    shot("64e_baseline_stats");
    c.setView(app::View::Gantt);
    u.dlg = ui::Dlg::BaselineSave;
    u.dlgOpening = true;
    shot("64g_baseline_save_dialog");
    u.dlg = ui::Dlg::None;
    (void)c.saveBaseline("v1.0 合意");
    clearToasts();
    u.dlg = ui::Dlg::BaselineManage;
    u.dlgOpening = true;
    shot("64h_baseline_manage_dialog");
    u.dlg = ui::Dlg::None;
    c.showBaseline(bs->baselines().list[0].id);
    // ダーク
    u.t = &ui::darkTokens();
    ui::applyStyle(*u.t, u.m);
    shot("64b_baseline_gantt_dark");
    c.setView(app::View::List);
    shot("64d_baseline_list_dark");
    c.setView(app::View::Stats);
    frames(3);
    scrollStatsToPlan();
    shot("64f_baseline_stats_dark");
    u.t = &ui::lightTokens();
    ui::applyStyle(*u.t, u.m);
    // 書き出し（ブラウザで開いて確かめる）
    {
      ExportOptions eo;
      const auto html = c.buildReport(eo);
      if (html.ok()) {
        std::ofstream o(out + "/html_baseline.html", std::ios::binary);
        o << html.value();
      }
      eo.dark = true;
      const auto dark = c.buildReport(eo);
      if (dark.ok()) {
        std::ofstream o(out + "/html_baseline_dark.html", std::ios::binary);
        o << dark.value();
      }
      WeeklyOptions wo;
      wo.sections.baseline = true;
      for (const bool html2 : {false, true}) {
        const auto text = c.renderWeekly(wo, html2);
        if (text.ok()) {
          std::ofstream o(out + (html2 ? "/weekly_baseline.html" : "/weekly_baseline.md"), std::ios::binary);
          o << text.value();
        }
      }
    }
  }
  // 関連リンク（Issue #60）: 添付タブの「リンク」の節（一覧・断った理由・右クリックのメニュー・編集中）、実行ファイルの警告、
  // 行・カードのバッジとツールチップ。ライト・ダークの両方
  {
    c.createProject(app::SampleKind::Hierarchy, "リンクの画面", "LNK");
    c.addLink("11", "設計書（最新）", "https://example.com/wiki/design/spec");
    c.addLink("11", "", "\\\\ファイルサーバー\\共有\\案件A\\設計\\仕様書_v3.xlsx");
    c.addLink("11", "", "S:\\案件A\\共有フォルダ\\");
    c.addLink("11", "ビルド手順", "C:\\tools\\build_all.bat");
    c.addLink("11", "GitHub の PR", "https://github.com/Hidenori24/task_management_tool/pull/61");
    c.addLink("12", "議事録", "https://example.com/minutes");
    c.addLink("12", "", "\\\\ファイルサーバー\\共有\\議事録\\");
    for (int k = 0; k < 8; ++k) c.addLink("8", "リンク" + std::to_string(k + 1), "https://example.com/p/" + std::to_string(k + 1));
    c.clearSelection();
    ImGuiIO& lio = ImGui::GetIO();
    for (const bool dark : {false, true}) {
      u.t = dark ? &ui::darkTokens() : &ui::lightTokens();
      ui::applyStyle(*u.t, u.m);
      const std::string tag = dark ? "dark" : "light";
      u.linkTargetBuf.clear();
      u.linkTitleBuf.clear();
      u.linkError.clear();
      u.linkEditing = false;
      c.setView(app::View::List);
      c.select("11");
      u.detailOpen = true;
      u.detailTab = 3;
      lio.AddMousePosEvent(5, 890);
      frames(6);
      shot("70_links_tab_" + tag);
      // 断った理由（入力は残す）
      u.linkTargetBuf = "javascript:alert(1)";
      u.linkError = classifyLink(u.linkTargetBuf).reason;
      shot("71_links_error_" + tag);
      u.linkTargetBuf.clear();
      u.linkError.clear();
      frames(3);  // 理由の行がなくなった並びで、行の位置を取り直す
      // 右クリックのメニュー
      if (u.linkRows.size() > 1) {
        const auto& r = u.linkRows[1];
        const ImVec2 at(r.first.x + 150, (r.first.y + r.second.y) / 2);
        lio.AddMousePosEvent(at.x, at.y);
        frames(2);
        lio.AddMouseButtonEvent(1, true);
        frames(2);
        lio.AddMouseButtonEvent(1, false);
        frames(3);
        shot("72_links_menu_" + tag);
        lio.AddKeyEvent(ImGuiKey_Escape, true);
        frames(2);
        lio.AddKeyEvent(ImGuiKey_Escape, false);
        frames(2);
        // 行に載せて少し待つと、場所の全体
        lio.AddMousePosEvent(5, 890);
        frames(3);
        lio.AddMousePosEvent(at.x, at.y);
        frames(30);
        shot("73_links_row_tip_" + tag);
        lio.AddMousePosEvent(5, 890);
        frames(3);
      }
      // 編集中
      u.linkEditing = true;
      u.linkEditIssue = "11";
      u.linkEditIndex = 0;
      u.linkEditOrig = c.linksOf("11")[0].target;
      u.linkTargetBuf = c.linksOf("11")[0].target;
      u.linkTitleBuf = c.linksOf("11")[0].title;
      shot("74_links_edit_" + tag);
      u.linkEditing = false;
      u.linkTargetBuf.clear();
      u.linkTitleBuf.clear();
      // 実行ファイルの警告
      {
        ui::Frame lf{c, u, p};
        ui::openLinkAt(lf, "11", 3, false);
        shot("75_links_warn_" + tag);
        u.dlg = ui::Dlg::None;
        u.linkWarnTarget.clear();
        frames(2);
      }
      // 行のバッジとツールチップ（一覧・ボード）
      u.detailOpen = false;
      c.clearSelection();
      c.setView(app::View::List);
      lio.AddMousePosEvent(5, 890);
      for (int k = 0; k < 8; ++k) {
        lio.AddMouseWheelEvent(0, 10);
        frames(2);
      }
      frames(4);
      shot("76_links_badges_list_" + tag);
      const auto badgeMid = [&](const std::string& id) {
        for (const auto& b : u.linkBadges)
          if (b.id == id && b.a.y > 120 && b.b.y < 850) return ImVec2((b.a.x + b.b.x) / 2, (b.a.y + b.b.y) / 2);
        return ImVec2(-1, -1);
      };
      if (const ImVec2 m = badgeMid("8"); m.x >= 0) {
        lio.AddMousePosEvent(m.x, m.y);
        frames(30);
        shot("77_links_tip_list_" + tag);
      }
      c.setView(app::View::Board);
      lio.AddMousePosEvent(700, 500);
      for (int k = 0; k < 8; ++k) {
        lio.AddMouseWheelEvent(0, 10);
        frames(2);
      }
      shot("78_links_badges_board_" + tag);
      if (const ImVec2 m = badgeMid("8"); m.x >= 0) {
        lio.AddMousePosEvent(m.x, m.y);
        frames(30);
        shot("79_links_tip_board_" + tag);
      }
      lio.AddMousePosEvent(5, 890);
      frames(3);
      c.setView(app::View::Gantt);
      shot("79b_links_badges_gantt_" + tag);
    }
    u.t = &ui::lightTokens();
    ui::applyStyle(*u.t, u.m);
  }
  // GitHub の PR・Issue のリンクの状態（Issue #61）: 状態のラベル（open / draft / merged / closed）・失敗・未確認、
  // 完了の提案の通知（注意つき）。ライト・ダークの両方
  {
    c.createProject(app::SampleKind::Hierarchy, "GitHub の画面", "GHS");
    auto fake = std::make_shared<FakeRemoteStatus>();
    c.setRemoteStatus(fake, false);
    const auto info = [](RemoteState s, const char* at = "") {
      RemoteInfo r;
      r.state = s;
      r.closedAt = at;
      return r;
    };
    const auto key = [](const char* url) { return parseGitHubUrl(url)->canonicalUrl(); };
    const char* const kUrls[] = {"https://github.com/acme/app/pull/101", "https://github.com/acme/app/pull/102", "https://github.com/acme/app/issues/103",
                                 "https://github.com/acme/app/pull/104", "https://github.com/acme/app/pull/105", "https://github.com/acme/app/issues/106"};
    c.addLink("11", "ログイン画面の実装", kUrls[0]);
    c.addLink("11", "古い案（取り下げ）", kUrls[1]);
    c.addLink("11", "仕様の確認 Issue", kUrls[2]);
    c.addLink("11", "レビュー待ち", kUrls[3]);
    c.addLink("11", "下書き", kUrls[4]);
    c.addLink("11", "まだ確認していない Issue", kUrls[5]);
    c.addLink("11", "設計書", "https://example.com/wiki/design");
    fake->set(key(kUrls[0]), info(RemoteState::Merged, "2026-10-15T03:00:00Z"));
    fake->set(key(kUrls[1]), info(RemoteState::ClosedUnmerged, "2026-10-16T03:00:00Z"));
    fake->set(key(kUrls[2]), info(RemoteState::ClosedNotPlanned, "2026-10-14T03:00:00Z"));
    fake->set(key(kUrls[3]), info(RemoteState::Open));
    fake->set(key(kUrls[4]), info(RemoteState::Draft));
    // 12 番は PR 2 件が閉じている（マージなしを含む → 完了の提案）。13 番は失敗
    c.addLink("12", "機能 A", "https://github.com/acme/app/pull/201");
    c.addLink("12", "機能 A の取り下げた案", "https://github.com/acme/app/pull/202");
    fake->set(key("https://github.com/acme/app/pull/201"), info(RemoteState::Merged, "2026-10-12T03:00:00Z"));
    fake->set(key("https://github.com/acme/app/pull/202"), info(RemoteState::ClosedUnmerged, "2026-10-13T03:00:00Z"));
    c.addLink("13", "つながらない PR", "https://github.com/acme/app/pull/301");
    fake->fail(key("https://github.com/acme/app/pull/301"), "gh が見つかりません。インストールして、gh auth login でログインしてください");
    for (std::size_t k = 0; k < 5; ++k) c.checkRemote("11", k);
    frames(4);
    c.clearSelection();
    for (const bool dark : {false, true}) {
      u.t = dark ? &ui::darkTokens() : &ui::lightTokens();
      ui::applyStyle(*u.t, u.m);
      const std::string tag = dark ? "dark" : "light";
      c.setView(app::View::List);
      c.select("11");
      u.detailOpen = true;
      u.detailTab = 3;
      ImGui::GetIO().AddMousePosEvent(5, 890);
      frames(6);
      shot("80_github_states_" + tag);
      // 完了の提案の通知（マージなしを含む）と、失敗の通知
      c.checkRemote("12");
      c.checkRemote("13");
      frames(4);
      shot("81_github_toasts_" + tag);
      // 通知を消して、次の周回でも同じ状態から始める
      for (const auto& t : std::vector<app::Toast>(c.toasts())) c.dismissToast(t.id);
      frames(2);
    }
    u.detailOpen = false;
    u.t = &ui::lightTokens();
    ui::applyStyle(*u.t, u.m);
    c.setRemoteStatus(nullptr);
  }
  // 編集中のロック（Issue #62）: 他の人が編集中のプロジェクトを読み取り専用で開いた画面（帯・古いロック・引き継ぎの確認・アプリの設定）
  {
    MemLockStore ls{fs};
    CountingRandom rng{9};
    c.setUtcOffsetMinutes(540);
    for (const auto& t : std::vector<app::Toast>(c.toasts())) c.dismissToast(t.id);
    c.flushNow();  // プロジェクトのフォルダができてから、他の人のロックを置く
    const std::string lockPath = lockPathIn(c.currentProjectDir());
    const auto putLock = [&](std::int64_t idleMs) {
      LockInfo i;
      i.user = "田中";
      i.machine = "PC-TANAKA";
      i.openedAt = clk.epochMs() - 25 * 60 * 1000;
      i.heartbeatAt = clk.epochMs() - idleMs;
      i.token = "tanaka-token";
      fs.put(lockPath, lockToText(i));
    };
    putLock(12 * 1000);
    c.setLockStore(&ls, &rng, "PC-SCREEN");  // 開いているプロジェクトのロックを確かめる → 読み取り専用
    for (const bool dark : {false, true}) {
      u.t = dark ? &ui::darkTokens() : &ui::lightTokens();
      ui::applyStyle(*u.t, u.m);
      const std::string tag = dark ? "dark" : "light";
      c.setView(app::View::Gantt);
      c.select("11");
      frames(3);
      shot("90_readonly_gantt_" + tag);
      c.setView(app::View::List);
      u.detailOpen = true;
      u.detailTab = 0;
      shot("91_readonly_list_detail_" + tag);
      u.detailOpen = false;
      u.dlg = ui::Dlg::ConfirmTakeOver;
      u.dlgOpening = true;
      shot("92_readonly_takeover_confirm_" + tag);
      u.dlg = ui::Dlg::None;
      frames(2);
      u.dlg = ui::Dlg::AppSettings;
      u.dlgOpening = true;
      shot("93_readonly_app_settings_" + tag);
      u.dlg = ui::Dlg::None;
      frames(2);
    }
    // 古いロック（持ち主がいないようなもの）: 帯の文言と、軽い確認
    clk.advanceMs(31 * 1000);  // 持ち主の様子を見直す間隔
    putLock(4 * 60 * 1000);
    frames(3);
    c.setView(app::View::Gantt);
    shot("94_readonly_stale_gantt");
    u.dlg = ui::Dlg::ConfirmTakeOver;
    u.dlgOpening = true;
    shot("95_readonly_stale_takeover_confirm");
    u.dlg = ui::Dlg::None;
    frames(2);
    u.t = &ui::lightTokens();
    ui::applyStyle(*u.t, u.m);
    c.setLockStore(nullptr, nullptr, "");
  }
  ImGui::DestroyContext();
  return 0;
}
