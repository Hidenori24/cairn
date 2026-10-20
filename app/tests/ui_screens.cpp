// 画面の確認用: UI をヘッドレスで描き、ImGui の描画データをソフトウェアでラスタライズして PPM 画像に書き出す。
// GPU も Windows も要らない。レイアウト・文字の欠け・色の確認に使う（Windows 実機での確認の代わりにはならない）。
// 使い方: roadmap_ui_screens <出力フォルダ> [日本語フォント.ttc] [フォント番号]
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include <imgui.h>

#include "app_controller.hpp"
#include "roadmap/gen.hpp"
#include "roadmap/schedule.hpp"
#include "ui/app_ui.hpp"

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

void render(const ImDrawData* dd, const Tex& tex, const std::string& path, ImVec4 bg) {
  Canvas cv(static_cast<int>(dd->DisplaySize.x), static_cast<int>(dd->DisplaySize.y), bg);
  for (int n = 0; n < dd->CmdListsCount; ++n) {
    const ImDrawList* l = dd->CmdLists[n];
    for (const ImDrawCmd& cmd : l->CmdBuffer) {
      if (cmd.UserCallback) continue;
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
  std::optional<std::string> pickFolder(const std::string&) override { return picked; }
  std::optional<std::string> picked;
  bool systemDark() override { return false; }
  void requestQuit() override {}
};

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
    for (auto k : {app::SampleKind::Regulation, app::SampleKind::Hierarchy}) {
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
  const auto shot = [&](const std::string& name) {
    frames(3);
    render(ImGui::GetDrawData(), tex, out + "/" + name + ".ppm", ui::v4(u.t->bg));
  };
  shot("00_welcome");
  c.createProject(app::SampleKind::Regulation, "サンプル（法規対応）", "");
  c.prefs().hintDismissed = false;
  c.select("25");
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
  c.createProject(app::SampleKind::Hierarchy, "階層デモ（セキュアブート）", "DEV");
  c.setView(app::View::Gantt);
  c.select("11");
  shot("08_hierarchy_gantt");
  c.setSelection({"12", "13", "14"}, "14");
  shot("09_bulk");
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
  // アプリの設定（保存先の切り替えの確認中）・削除の確認
  u.dlg = ui::Dlg::AppSettings;
  u.dlgOpening = true;
  frames(2);
  shot("17_app_settings");
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
  u.t = &ui::darkTokens();
  ui::applyStyle(*u.t, u.m);
  c.session()->setResource({true, 1});
  c.afterEdit();
  shot("12_dark_leveling");
  ImGui::DestroyContext();
  return 0;
}
