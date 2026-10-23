// 統計ビュー: 進み・状況の内訳・余裕の分布・週ごとの完了・担当別・ルート別・優先度別（集計は core/stats）
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "roadmap/history.hpp"
#include "roadmap/load.hpp"
#include "roadmap/mta.hpp"
#include "roadmap/stats.hpp"
#include "roadmap/text.hpp"
#include "roadmap/trends.hpp"
#include "ui/icons.hpp"
#include "ui/internal.hpp"

namespace roadmap::ui {

namespace {

struct Part {
  double v;
  ImU32 col;
};

// 横の積み上げ棒。合計が 0 のときは枠だけ
void stackedBar(const UiState& u, ImVec2 p, float w, float h, std::initializer_list<Part> parts) {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  double total = 0;
  for (const auto& x : parts) total += x.v;
  dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), u.t->surface3, u.m.s(3));
  if (total <= 0) return;
  float x0 = p.x;
  double acc = 0;
  for (const auto& x : parts) {
    if (x.v <= 0) continue;
    acc += x.v;
    const float x1 = p.x + static_cast<float>(w * (acc / total));
    if (x1 - x0 >= 1.f) dl->AddRectFilled(ImVec2(x0, p.y), ImVec2(x1, p.y + h), x.col, 0);
    x0 = x1;
  }
  dl->AddRect(p, ImVec2(p.x + w, p.y + h), u.t->line, u.m.s(3));
}

// 領域からはみ出る文字は切る
void clippedText(const UiState& u, ImVec2 p, float w, ImU32 col, const std::string& s) {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->PushClipRect(p, ImVec2(p.x + w, p.y + ImGui::GetTextLineHeight() + u.m.s(2)), true);
  dl->AddText(p, col, s.c_str());
  dl->PopClipRect();
}

std::string num(double v) {
  char b[32];
  std::snprintf(b, sizeof b, "%.1f", v);
  std::string s = b;
  if (s.size() > 2 && s.compare(s.size() - 2, 2, ".0") == 0) s.resize(s.size() - 2);
  return s;
}

std::string pct(double v) {
  char b[16];
  std::snprintf(b, sizeof b, "%d%%", static_cast<int>(v * 100 + 0.5));
  return b;
}

void sectionHead(Frame& f, const char* title, const char* note, float w) {
  UiState& u = f.u;
  ImGui::Dummy(ImVec2(0, u.m.s(10)));
  ImGui::PushFont(u.fontBold);
  ImGui::TextUnformatted(title);
  ImGui::PopFont();
  if (note && *note) {
    ImGui::SameLine(0, u.m.s(10));
    ImGui::TextDisabled("%s", note);
  }
  const ImVec2 q = ImGui::GetCursorScreenPos();
  ImGui::GetWindowDrawList()->AddLine(ImVec2(q.x, q.y + u.m.s(2)), ImVec2(q.x + w, q.y + u.m.s(2)),
                                      u.t->line);
  ImGui::Dummy(ImVec2(0, u.m.s(8)));
}

// 数字のタイル
void tile(Frame& f, const char* label, const std::string& value, ImU32 col, float w) {
  UiState& u = f.u;
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const float h = u.m.s(64);
  dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), u.t->surface, u.m.s(6));
  dl->AddRect(p, ImVec2(p.x + w, p.y + h), u.t->line, u.m.s(6));
  clippedText(u, ImVec2(p.x + u.m.s(10), p.y + u.m.s(8)), w - u.m.s(20), u.t->muted, label);
  ImGui::PushFont(u.fontLarge);
  dl->AddText(ImVec2(p.x + u.m.s(10), p.y + u.m.s(28)), col, value.c_str());
  ImGui::PopFont();
  ImGui::Dummy(ImVec2(w, h));
}

struct Legend {
  const char* label;
  ImU32 col;
  int n;
};

void legend(Frame& f, std::initializer_list<Legend> items) {
  UiState& u = f.u;
  ImDrawList* dl = ImGui::GetWindowDrawList();
  for (const auto& it : items) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float s = u.m.s(10);
    dl->AddRectFilled(ImVec2(p.x, p.y + u.m.s(4)), ImVec2(p.x + s, p.y + u.m.s(4) + s), it.col, u.m.s(2));
    ImGui::Dummy(ImVec2(s, s));
    ImGui::SameLine(0, u.m.s(5));
    if (it.n >= 0) ImGui::Text("%s %d", it.label, it.n);
    else ImGui::TextUnformatted(it.label);
    ImGui::SameLine(0, u.m.s(16));
  }
  ImGui::NewLine();
}

// 「完了 a／全 n件」のような1行の説明
std::string groupText(const GroupStat& g) {
  std::string s = "完了 " + std::to_string(g.nDone) + "／" + std::to_string(g.n) + "件";
  if (g.hasOpen) s += "・残り " + num(g.weeksOpen()) + "週";
  if (g.nLate > 0) s += "・遅れ " + std::to_string(g.nLate);
  else if (g.nTight > 0) s += "・余裕小 " + std::to_string(g.nTight);
  return s;
}

// 担当別・ルート別の1行。クリックされたら true
bool groupRow(Frame& f, const GroupStat& g, const std::string& title, const std::string& detail, float nameW, float barW, float w,
              bool clickable) {
  UiState& u = f.u;
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const float h = u.m.rowH();
  const bool hov = ImGui::IsMouseHoveringRect(p, ImVec2(p.x + w, p.y + h)) && ImGui::IsWindowHovered();
  if (hov && clickable) dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), u.t->hover, u.m.s(4));
  const float ty = p.y + (h - ImGui::GetTextLineHeight()) / 2;
  clippedText(u, ImVec2(p.x + u.m.s(6), ty), nameW - u.m.s(12), u.t->ink, title);
  const float bh = u.m.s(12);
  stackedBar(u, ImVec2(p.x + nameW, p.y + (h - bh) / 2), barW, bh,
             {{static_cast<double>(g.nDone), u.t->ok}, {static_cast<double>(g.nProg), u.t->accent},
              {static_cast<double>(g.nTodo), u.t->line2}});
  clippedText(u, ImVec2(p.x + nameW + barW + u.m.s(12), ty), std::max(w - nameW - barW - u.m.s(18), u.m.s(40)),
              g.nLate > 0 ? u.t->late : u.t->muted, detail);
  ImGui::Dummy(ImVec2(w, h));
  return hov && clickable && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
}


// 担当者で絞ってリストを開く（担当別の行・負荷の行を押したとき。名前が空 = 担当なし）
void openListFor(Frame& f, const std::string& who) {
  auto& flt = f.c.filter();
  flt.asg = who.empty() ? AsgFilter::Unassigned : AsgFilter::Name;
  flt.asgName = who;
  f.c.setView(app::View::List);
}

// 負荷の割合 → ヒートマップの塗り。80% 未満は薄い緑〜透明、80〜100% は橙、100% 超は赤。
// 色だけに頼らないよう、境は丸めたパーセントで決める（表示する数字と食い違わない）
ImU32 heatFill(const UiState& u, int pc) {
  if (pc <= 0) return 0;
  if (pc < 80) return withAlpha(u.t->ok, 0.10f + 0.40f * static_cast<float>(pc) / 80.f);
  const float k = static_cast<float>(pc - 80) / 20.f;
  if (pc <= 100) return withAlpha(u.t->tight, u.t->dark ? 0.80f + 0.15f * k : 0.30f + 0.28f * k);
  return withAlpha(u.t->late, 0.92f);
}

// セルの数字の色（塗りの上で読める色）。濃く塗るもの（100% 超・ダークの橙）は反転色
ImU32 heatInk(const UiState& u, int pc) {
  if (pc > 100 || (pc >= 80 && u.t->dark)) return u.t->accentInk;
  return u.t->ink;
}

// セルにマウスを載せたときの内訳（その週にその人が持つ作業。LoadCell::ids は最大 kLoadCellIds 件）
void loadTooltip(const UiState& u, const LoadReport& lr, const LoadRow& row, std::size_t k, const Tree& T,
                 const std::vector<ScheduleRow>& R) {
  const LoadCell& c = row.cells[k];
  const int pc = loadPercent(c.ratio);
  ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.f);
  ImGui::PushFont(u.fontBold);
  const bool folded = lr.lastFolded && k + 1 == lr.weekStarts.size();
  ImGui::Text("%s ・ %s%s", row.name.empty() ? "（担当なし）" : row.name.c_str(), fmtMd(lr.weekStarts[k]).c_str(),
              folded ? " 以降" : " の週");
  ImGui::PopFont();
  std::string line = "負荷 " + std::to_string(pc) + "%";
  if (pc > 100) line += "（オーバー）";
  line += "　作業 " + num(c.days) + " 営業日 ÷ 週の営業日 " + std::to_string(lr.workdays[k]);
  if (lr.cap > 1) line += " × 同時 " + std::to_string(lr.cap) + " 件";
  ImGui::TextDisabled("%s", line.c_str());
  for (int id : c.ids) {
    const ScheduleRow& r = R[static_cast<std::size_t>(id)];
    ImGui::Text("・%s  （%s〜%s）", T.name[static_cast<std::size_t>(id)].c_str(), fmtMd(r.asapStart).c_str(),
                fmtMd(r.asapFinish).c_str());
  }
  if (c.n > static_cast<int>(c.ids.size())) ImGui::TextDisabled("ほか %d 件", c.n - static_cast<int>(c.ids.size()));
  ImGui::PopTextWrapPos();
}

// 担当者×週のヒートマップ。見えている行だけ描く。押すと、その担当者で絞ったリストを開く
void drawLoadHeatmap(Frame& f, const LoadReport& lr, const Tree& T, const std::vector<ScheduleRow>& R, float W) {
  UiState& u = f.u;
  if (lr.load.empty() || lr.weekStarts.empty()) {
    ImGui::TextUnformatted("未完了の作業がないので、負荷はありません。");
    return;
  }
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const std::size_t nw = lr.weekStarts.size();
  const float nameW = std::clamp(W * 0.16f, u.m.s(90), u.m.s(140));
  const float tailW = u.m.s(72);  // 右の「最大 N%」
  const float cw = std::clamp((W - nameW - tailW) / static_cast<float>(nw), u.m.s(16), u.m.s(44));
  const float gridW = cw * static_cast<float>(nw);
  const bool showTail = nameW + gridW + tailW <= W + 0.5f;
  const float h = u.m.rowH();

  legend(f, {{"80% 未満", heatFill(u, 40), -1}, {"80〜100%", heatFill(u, 90), -1}, {"100% 超（!）", heatFill(u, 120), -1}});
  ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + W);
  if (lr.cap > 1)
    ImGui::TextDisabled("100%% = 同時に %d 件が動いている状態（資源制限）。週の営業日数は休業日を除きます。", lr.cap);
  else
    ImGui::TextDisabled("100%% = 週の営業日ぶんの作業が 1 人に入っている状態です（資源制限で同時件数を増やしたときは、その件数が 100%%）。");
  if (lr.lastFolded) ImGui::TextDisabled("最後の列は、それより先の週をまとめたものです。");
  ImGui::PopTextWrapPos();
  ImGui::Dummy(ImVec2(0, u.m.s(4)));

  // 見出し（週の月曜）
  {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const std::size_t every = cw < u.m.s(26) ? (cw < u.m.s(20) ? 4 : 2) : 1;
    for (std::size_t k = 0; k < nw; ++k) {
      if (k % every != 0) continue;
      const Ymd ymd = civilFromDays(lr.weekStarts[k]);
      const std::string lb = std::to_string(ymd.m) + "/" + std::to_string(ymd.d);
      const float tw = ImGui::CalcTextSize(lb.c_str()).x;
      dl->AddText(ImVec2(p.x + nameW + cw * static_cast<float>(k) + (cw - tw) / 2, p.y), k == 0 ? u.t->ink : u.t->muted, lb.c_str());
    }
    ImGui::Dummy(ImVec2(W, ImGui::GetTextLineHeight() + u.m.s(4)));
  }

  for (const LoadRow& row : lr.load) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    if (ImGui::IsRectVisible(p, ImVec2(p.x + W, p.y + h))) {
      const bool hov = ImGui::IsMouseHoveringRect(p, ImVec2(p.x + W, p.y + h)) && ImGui::IsWindowHovered();
      if (hov) dl->AddRectFilled(p, ImVec2(p.x + nameW, p.y + h), u.t->hover, u.m.s(4));
      const float ty = p.y + (h - ImGui::GetTextLineHeight()) / 2;
      clippedText(u, ImVec2(p.x + u.m.s(6), ty), nameW - u.m.s(12), u.t->ink, row.name.empty() ? std::string("（担当なし）") : row.name);
      const float gx = p.x + nameW;
      const float gap = std::max(1.f, u.m.s(1));
      dl->AddRectFilled(ImVec2(gx, p.y + gap), ImVec2(gx + gridW, p.y + h - gap), u.t->surface2, u.m.s(3));
      for (std::size_t k = 0; k < nw; ++k) {
        const int pc = loadPercent(row.cells[k].ratio);
        const ImVec2 a(gx + cw * static_cast<float>(k), p.y + gap), b(a.x + cw, p.y + h - gap);
        if (const ImU32 col = heatFill(u, pc)) dl->AddRectFilled(ImVec2(a.x + gap, a.y), ImVec2(b.x - gap, b.y), col, u.m.s(3));
        if (pc > 0) {
          // 数字（入りきらなければ、100% 超は「!」だけ）。色だけに頼らない
          const std::string digits = std::to_string(pc);
          const std::string cands[3] = {pc > 100 ? "!" + digits : digits, digits, pc > 100 ? std::string("!") : std::string()};
          for (const std::string& t : cands) {
            if (t.empty()) break;
            const ImVec2 ts = ImGui::CalcTextSize(t.c_str());
            if (ts.x > cw - u.m.s(3)) continue;
            dl->AddText(ImVec2(a.x + (cw - ts.x) / 2, p.y + (h - ts.y) / 2), heatInk(u, pc), t.c_str());
            break;
          }
        }
        u.statsLoadCells.push_back({row.name, static_cast<int>(k), a, b, pc});
      }
      if (showTail && row.peak > 0) {
        const int pk = loadPercent(row.peak);
        clippedText(u, ImVec2(gx + gridW + u.m.s(8), ty), tailW - u.m.s(8), pk > 100 ? u.t->late : u.t->muted,
                    "最大 " + std::to_string(pk) + "%");
      }
      if (hov) {
        const ImVec2 mp = ImGui::GetMousePos();
        if (mp.x >= gx && mp.x < gx + gridW) {
          const std::size_t k = std::min(static_cast<std::size_t>((mp.x - gx) / cw), nw - 1);
          if (row.cells[k].n > 0 && ImGui::BeginTooltip()) {
            loadTooltip(u, lr, row, k, T, R);
            ImGui::EndTooltip();
          }
        }
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) openListFor(f, row.name);
      }
    }
    ImGui::Dummy(ImVec2(W, h));
  }
}

// 担当者ごとの完了数の積み上げ縦棒（週ごと）と、完了した期間の合計の表。順位にしない（名前順）
void drawDoneByAssignee(Frame& f, const LoadReport& lr, float W) {
  UiState& u = f.u;
  if (lr.done.empty()) {
    ImGui::TextUnformatted("過去 8 週に完了日（Resolved）が入った作業はありません。");
    return;
  }
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const std::size_t nw = lr.doneWeekStarts.size();
  const auto colorOf = [&](const DoneRow& d) { return d.name.empty() ? u.t->line2 : u.t->c[nameHash(d.name) % 8]; };
  const auto nameOf = [](const DoneRow& d) { return d.name.empty() ? std::string("（担当なし）") : d.name; };
  std::vector<int> total(nw, 0);
  int mx = 1;
  for (std::size_t k = 0; k < nw; ++k) {
    for (const DoneRow& d : lr.done) total[k] += d.perWeek[k];
    mx = std::max(mx, total[k]);
  }
  const float chartH = u.m.s(110), axisH = u.m.s(20);
  const float bw = std::clamp(W * 0.6f / static_cast<float>(nw), u.m.s(30), u.m.s(72));
  const ImVec2 p = ImGui::GetCursorScreenPos();
  dl->AddLine(ImVec2(p.x, p.y + chartH), ImVec2(p.x + bw * static_cast<float>(nw), p.y + chartH), u.t->line);
  for (std::size_t k = 0; k < nw; ++k) {
    const float x = p.x + bw * static_cast<float>(k);
    const float unit = (chartH - u.m.s(18)) / static_cast<float>(mx);
    float y = p.y + chartH;
    for (const DoneRow& d : lr.done) {
      const int n = d.perWeek[k];
      if (n <= 0) continue;
      const float top = y - unit * static_cast<float>(n);
      dl->AddRectFilled(ImVec2(x + bw * 0.18f, top), ImVec2(x + bw * 0.82f, y), colorOf(d), 0);
      dl->AddLine(ImVec2(x + bw * 0.18f, top), ImVec2(x + bw * 0.82f, top), u.t->surface);  // 担当者の境
      y = top;
    }
    if (total[k] > 0) {
      const std::string ns = std::to_string(total[k]);
      const float tw = ImGui::CalcTextSize(ns.c_str()).x;
      dl->AddText(ImVec2(x + (bw - tw) / 2, y - ImGui::GetTextLineHeight() - u.m.s(1)), u.t->muted, ns.c_str());
    }
    const Ymd ymd = civilFromDays(lr.doneWeekStarts[k]);
    const std::string lb = std::to_string(ymd.m) + "/" + std::to_string(ymd.d);
    const float tw = ImGui::CalcTextSize(lb.c_str()).x;
    dl->AddText(ImVec2(x + (bw - tw) / 2, p.y + chartH + u.m.s(3)), u.t->muted, lb.c_str());
    if (ImGui::IsMouseHoveringRect(ImVec2(x, p.y), ImVec2(x + bw, p.y + chartH + axisH)) && ImGui::IsWindowHovered() &&
        ImGui::BeginTooltip()) {
      ImGui::Text("%s からの週", fmtMd(lr.doneWeekStarts[k]).c_str());
      ImGui::Text("完了 %d 件", total[k]);
      for (const DoneRow& d : lr.done)
        if (d.perWeek[k] > 0) ImGui::Text("・%s %d 件", nameOf(d).c_str(), d.perWeek[k]);
      ImGui::EndTooltip();
    }
  }
  ImGui::Dummy(ImVec2(W, chartH + axisH));
  ImGui::Dummy(ImVec2(0, u.m.s(6)));

  // 表: 担当者ごとの完了数と、完了した期間の合計（週）・平均
  const float nameW = std::clamp(W * 0.22f, u.m.s(120), u.m.s(200)), colW = std::clamp(W * 0.2f, u.m.s(90), u.m.s(160));
  const float h = u.m.rowH();
  const float x1 = nameW + u.m.s(24), x2 = nameW + colW + u.m.s(24), x3 = nameW + colW * 2 + u.m.s(24);
  {
    const ImVec2 q = ImGui::GetCursorScreenPos();
    dl->AddText(ImVec2(q.x + u.m.s(24), q.y), u.t->muted, "担当者");
    dl->AddText(ImVec2(q.x + x1, q.y), u.t->muted, "完了した数");
    dl->AddText(ImVec2(q.x + x2, q.y), u.t->muted, "期間の合計");
    dl->AddText(ImVec2(q.x + x3, q.y), u.t->muted, "平均の期間");
    ImGui::Dummy(ImVec2(W, ImGui::GetTextLineHeight() + u.m.s(4)));
  }
  int sumN = 0;
  double sumW = 0;
  const auto line = [&](ImU32 sw, const std::string& nm, int n, double wk, bool total_) {
    const ImVec2 q = ImGui::GetCursorScreenPos();
    if (ImGui::IsRectVisible(q, ImVec2(q.x + W, q.y + h))) {
      const float ty = q.y + (h - ImGui::GetTextLineHeight()) / 2;
      if (sw) dl->AddRectFilled(ImVec2(q.x + u.m.s(6), q.y + (h - u.m.s(10)) / 2), ImVec2(q.x + u.m.s(16), q.y + (h + u.m.s(10)) / 2), sw, u.m.s(2));
      const ImU32 ink = total_ ? u.t->muted : u.t->ink;
      clippedText(u, ImVec2(q.x + u.m.s(24), ty), nameW - u.m.s(30), ink, nm);
      dl->AddText(ImVec2(q.x + x1, ty), ink, (std::to_string(n) + " 件").c_str());
      dl->AddText(ImVec2(q.x + x2, ty), ink, (num(wk) + " 週").c_str());
      dl->AddText(ImVec2(q.x + x3, ty), ink, (n > 0 ? num(wk / n) + " 週" : std::string("—")).c_str());
      dl->AddLine(ImVec2(q.x, q.y + h), ImVec2(q.x + W, q.y + h), u.t->rowline);
    }
    ImGui::Dummy(ImVec2(W, h));
  };
  for (const DoneRow& d : lr.done) {
    line(colorOf(d), nameOf(d), d.n, d.weeks, false);
    sumN += d.n;
    sumW += d.weeks;
  }
  if (lr.done.size() > 1) line(0, "合計", sumN, sumW, true);
}

// ---------------------------------------------------------------- 推移（Issue #54 の A）

// 点線（線種でも区別する。色だけに頼らない）
void dashedLine(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 col, float th, float dash, float gap) {
  const float dx = b.x - a.x, dy = b.y - a.y;
  const float len = std::sqrt(dx * dx + dy * dy);
  if (len < 0.5f) return;
  const float ux = dx / len, uy = dy / len;
  for (float t = 0; t < len; t += dash + gap) {
    const float e = std::min(t + dash, len);
    dl->AddLine(ImVec2(a.x + ux * t, a.y + uy * t), ImVec2(a.x + ux * e, a.y + uy * e), col, th);
  }
}

// 折れ線を破線・点線で引く。破線の位相（パターンのどこまで進んだか）を点から点へ持ち越すので、
// 1 区間が dash より短くても（104 週を狭い幅に収めたとき）破線に見える
void dashedPolyline(ImDrawList* dl, const std::vector<ImVec2>& pts, ImU32 col, float th, float dash, float gap) {
  const float period = dash + gap;
  if (pts.size() < 2 || period <= 0.f) return;
  float pos = 0;  // パターンの先頭からの距離
  for (std::size_t k = 1; k < pts.size(); ++k) {
    const ImVec2 a = pts[k - 1], b = pts[k];
    const float dx = b.x - a.x, dy = b.y - a.y;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.001f) continue;
    const float ux = dx / len, uy = dy / len;
    float t = 0;
    while (t < len) {
      const float phase = std::fmod(pos, period);
      const bool on = phase < dash;
      const float step = std::min((on ? dash : period) - phase, len - t);
      if (on) dl->AddLine(ImVec2(a.x + ux * t, a.y + uy * t), ImVec2(a.x + ux * (t + step), a.y + uy * (t + step)), col, th);
      t += step;
      pos += step;
    }
  }
}

// 折れ線グラフの凡例（四角の見本と、実線・点線の見本）
void burnLegend(Frame& f) {
  UiState& u = f.u;
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const auto swatch = [&](const char* label, ImU32 col) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float s = u.m.s(10);
    dl->AddRectFilled(ImVec2(p.x, p.y + u.m.s(4)), ImVec2(p.x + s, p.y + u.m.s(4) + s), col, u.m.s(2));
    ImGui::Dummy(ImVec2(s, s));
    ImGui::SameLine(0, u.m.s(5));
    ImGui::TextUnformatted(label);
    ImGui::SameLine(0, u.m.s(16));
  };
  const auto sample = [&](const char* label, ImU32 col, bool dashed) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = u.m.s(24), y = p.y + ImGui::GetTextLineHeight() / 2;
    if (dashed) dashedLine(dl, ImVec2(p.x, y), ImVec2(p.x + w, y), col, u.m.s(2), u.m.s(4), u.m.s(3));
    else dl->AddLine(ImVec2(p.x, y), ImVec2(p.x + w, y), col, u.m.s(2));
    ImGui::Dummy(ImVec2(w, ImGui::GetTextLineHeight()));
    ImGui::SameLine(0, u.m.s(5));
    ImGui::TextUnformatted(label);
    ImGui::SameLine(0, u.m.s(16));
  };
  swatch("作った数", u.t->accent);
  swatch("完了した数", u.t->ok);
  sample("残り（実線）", u.t->ink, false);
  sample("ペース予測（点線）", u.t->accent, true);
  ImGui::NewLine();
}

// 週ごとの作った数・完了した数（棒）と、残り（折れ線）。ペース予測は点線で先へ伸ばす
void drawBurn(Frame& f, const TrendReport& tr, float W) {
  UiState& u = f.u;
  UiState::StatsTrend& st = u.statsTrend;
  const TrendBurn& b = tr.burn;
  if (b.weeks.empty()) {
    ImGui::TextUnformatted("作成日（Created）のある作業がないので、バーンダウンは出せません。");
    return;
  }
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const int nPast = static_cast<int>(b.weeks.size());
  const int nFut = b.forecast.ok ? std::min(b.forecast.weeksAhead, kFutureWeeks) : 0;
  const int cols = nPast + nFut;
  // 列は W に収まるまで縮める（履歴 27 列 + 予測 26 列でも幅を超えない。最小幅は置かない）。狭いほど日付の見出しを間引く
  const float bw = std::min(W / static_cast<float>(cols), u.m.s(44));
  const float barsH = u.m.s(90), lineH = u.m.s(110), axisH = u.m.s(20);
  const int every = std::max(1, static_cast<int>(std::ceil(u.m.s(34) / bw)));
  const bool labels = bw >= u.m.s(28);
  const Day lastStart = b.weeks.back().start;
  const auto weekStartOf = [&](int k) { return lastStart + (k - (nPast - 1)) * 7; };

  burnLegend(f);
  ImGui::TextDisabled("作った数と完了した数（週ごと。棒の上の数字が件数）");
  const ImVec2 p1 = ImGui::GetCursorScreenPos();
  ImGui::Dummy(ImVec2(W, barsH + axisH));
  ImGui::TextDisabled("残り（その週の終わり。基準日の週は基準日。点線は今のペースで続けた場合）");
  const ImVec2 p2 = ImGui::GetCursorScreenPos();
  ImGui::Dummy(ImVec2(W, lineH + axisH));

  const auto colX = [&](int k) { return p1.x + bw * static_cast<float>(k); };
  st.chartX1 = p1.x + W;
  const auto xLabels = [&](const ImVec2& p, float chartH) {
    for (int k = 0; k < cols; ++k) {
      const bool now = k == b.nowIndex;
      if (k % every != 0 && !now) continue;
      const std::string lb = fmtShort(weekStartOf(k));
      const float tw = ImGui::CalcTextSize(lb.c_str()).x;
      dl->AddText(ImVec2(colX(k) + (bw - tw) / 2, p.y + chartH + u.m.s(3)), now ? u.t->ink : (k < nPast ? u.t->muted : withAlpha(u.t->muted, 0.7f)),
                  lb.c_str());
    }
  };
  // 基準日の週の強調
  const auto baseBand = [&](const ImVec2& p, float h) {
    dl->AddRectFilled(ImVec2(colX(b.nowIndex), p.y), ImVec2(colX(b.nowIndex) + bw, p.y + h), u.t->selbg);
    dl->AddLine(ImVec2(p.x, p.y + h), ImVec2(p.x + bw * static_cast<float>(cols), p.y + h), u.t->line);
  };
  baseBand(p1, barsH);
  baseBand(p2, lineH);

  // ---- 棒: 作った数（左）・完了した数（右）----
  {
    int mx = 1;
    for (const TrendWeek& w : b.weeks) mx = std::max({mx, w.created, w.resolved});
    const float unit = (barsH - u.m.s(18)) / static_cast<float>(mx);
    for (int k = 0; k < nPast; ++k) {
      const TrendWeek& w = b.weeks[static_cast<std::size_t>(k)];
      const float x = colX(k);
      const auto bar = [&](float x0, float x1, int n, ImU32 col) {
        if (n <= 0) return;
        const float h = std::max(unit * static_cast<float>(n), u.m.s(2));
        dl->AddRectFilled(ImVec2(x0, p1.y + barsH - h), ImVec2(x1, p1.y + barsH), col, u.m.s(2));
        if (labels) {
          const std::string ns = std::to_string(n);
          const float tw = ImGui::CalcTextSize(ns.c_str()).x;
          dl->AddText(ImVec2((x0 + x1 - tw) / 2, p1.y + barsH - h - ImGui::GetTextLineHeight() - u.m.s(1)), u.t->muted, ns.c_str());
        }
      };
      bar(x + bw * 0.12f, x + bw * 0.48f, w.created, u.t->accent);
      bar(x + bw * 0.52f, x + bw * 0.88f, w.resolved, u.t->ok);
    }
    xLabels(p1, barsH);
  }

  // ---- 折れ線: 残り ----
  {
    int mx = 1;
    for (const TrendWeek& w : b.weeks) mx = std::max(mx, w.remaining);
    const float top = u.m.s(18);
    const auto yOf = [&](double v) { return p2.y + lineH - (lineH - top) * static_cast<float>(v / static_cast<double>(mx)); };
    const auto ptOf = [&](int k, double v) { return ImVec2(colX(k) + bw / 2, yOf(v)); };
    for (int k = 1; k < nPast; ++k)
      dl->AddLine(ptOf(k - 1, b.weeks[static_cast<std::size_t>(k) - 1].remaining), ptOf(k, b.weeks[static_cast<std::size_t>(k)].remaining), u.t->ink,
                  u.m.s(2));
    for (int k = 0; k < nPast; ++k) {
      const int v = b.weeks[static_cast<std::size_t>(k)].remaining;
      const ImVec2 pt = ptOf(k, v);
      if (k == nPast - 1) {  // 基準日の週は白抜き（途中の値）
        dl->AddCircleFilled(pt, u.m.s(4), u.t->surface);
        dl->AddCircle(pt, u.m.s(4), u.t->ink, 0, u.m.s(2));
      } else {
        dl->AddCircleFilled(pt, u.m.s(3), u.t->ink);
      }
      if (labels || k == 0 || k == nPast - 1) {
        const std::string ns = std::to_string(v);
        const float tw = ImGui::CalcTextSize(ns.c_str()).x;
        dl->AddText(ImVec2(pt.x - tw / 2, pt.y - ImGui::GetTextLineHeight() - u.m.s(5)), u.t->muted, ns.c_str());
      }
    }
    if (b.forecast.ok && nFut > 0) {
      const double perWeek = b.forecast.perWeek;
      const bool reaches = b.forecast.weeksAhead <= nFut;
      const int endK = reaches ? nPast - 1 + b.forecast.weeksAhead : nPast - 1 + nFut;
      const double endV = reaches ? 0.0 : std::max(0.0, static_cast<double>(b.remainingNow) - perWeek * nFut);
      const ImVec2 a = ptOf(nPast - 1, b.remainingNow), e = ptOf(endK, endV);
      dashedLine(dl, a, e, u.t->accent, u.m.s(2), u.m.s(5), u.m.s(4));
      // 終点のしるし（ひし形）と、終わる週
      const float r = u.m.s(5);
      dl->AddQuadFilled(ImVec2(e.x, e.y - r), ImVec2(e.x + r, e.y), ImVec2(e.x, e.y + r), ImVec2(e.x - r, e.y), u.t->accent);
      const std::string lb = reaches ? "完了の見込み " + fmtShort(b.forecast.finishWeek) : std::string("さらに先");
      const float tw = ImGui::CalcTextSize(lb.c_str()).x;
      const ImVec2 tp(std::min(e.x - tw / 2, p2.x + W - tw), e.y - ImGui::GetTextLineHeight() - u.m.s(10));
      // 点線と重なっても読めるように、地の色の帯を敷く
      dl->AddRectFilled(ImVec2(tp.x - u.m.s(3), tp.y - u.m.s(1)), ImVec2(tp.x + tw + u.m.s(3), tp.y + ImGui::GetTextLineHeight() + u.m.s(1)),
                        u.t->bg, u.m.s(3));
      dl->AddText(tp, u.t->accent, lb.c_str());
    }
    xLabels(p2, lineH);
  }

  // ---- ホバー: 週ごとの値（予測の週は予測の値）----
  st.weekCols.clear();
  for (int k = 0; k < cols; ++k) {
    const ImVec2 a(colX(k), p1.y), c(colX(k) + bw, p2.y + lineH + axisH);
    if (k < nPast) st.weekCols.push_back({a, ImVec2(c.x, p1.y + barsH)});
    if (k == cols - 1) st.lastCol = {a, ImVec2(c.x, p1.y + barsH)};  // 予測の週も含めた最後の列
    if (!ImGui::IsMouseHoveringRect(a, c) || !ImGui::IsWindowHovered()) continue;
    if (k < nPast) {
      const TrendWeek& w = b.weeks[static_cast<std::size_t>(k)];
      ImGui::SetTooltip("%s からの週%s\n作った %d 件\n完了 %d 件\n残り %d 件", fmtMd(w.start).c_str(), w.partial ? "（基準日まで）" : "", w.created,
                        w.resolved, w.remaining);
    } else {
      const double left = std::max(0.0, static_cast<double>(b.remainingNow) - b.forecast.perWeek * static_cast<double>(k - (nPast - 1)));
      ImGui::SetTooltip("%s からの週（予測）\n残り およそ %d 件\n（直近 %d 週の平均 週 %s 件のペース）", fmtMd(weekStartOf(k)).c_str(),
                        static_cast<int>(left + 0.5), kTrendPaceWeeks, num(b.forecast.perWeek).c_str());
    }
  }
  ImGui::Dummy(ImVec2(0, u.m.s(4)));
  ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + W);
  const std::string line = trendForecastText(b.forecast, b.remainingNow);
  const bool bad = !b.forecast.ok && !line.empty() && line[0] == '!';
  ImGui::PushStyleColor(ImGuiCol_Text, bad ? u.t->tight : u.t->ink);
  ImGui::TextUnformatted(line.c_str());
  ImGui::PopStyleColor();
  ImGui::PopTextWrapPos();
}

// 階級別の棒（0-1 / 2-4 / 5-9 / 10-19 / 20+ 営業日）。bangLast なら最後の階級に「!」を付ける
void drawBuckets(Frame& f, const std::array<int, kTrendBuckets>& hist, int total, const ImU32 (&cols)[kTrendBuckets], bool bangLast, float W) {
  UiState& u = f.u;
  ImDrawList* dl = ImGui::GetWindowDrawList();
  int mx = 1;
  for (int h : hist) mx = std::max(mx, h);
  const float lw = u.m.s(110), cw = u.m.s(90);
  const float bw = std::max(W - lw - cw - u.m.s(8), u.m.s(80));
  for (int k = 0; k < kTrendBuckets; ++k) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = u.m.s(22);
    const float ty = p.y + (h - ImGui::GetTextLineHeight()) / 2;
    const std::string lb = std::string(bangLast && k == kTrendBuckets - 1 ? "! " : "") + trendBucketLabel(k) + " 営業日";
    dl->AddText(ImVec2(p.x + u.m.s(6), ty), bangLast && k == kTrendBuckets - 1 ? u.t->late : u.t->ink, lb.c_str());
    const int n = hist[static_cast<std::size_t>(k)];
    const float len = bw * static_cast<float>(n) / static_cast<float>(mx);
    if (len > 0.5f) dl->AddRectFilled(ImVec2(p.x + lw, p.y + u.m.s(4)), ImVec2(p.x + lw + len, p.y + h - u.m.s(4)), cols[k], u.m.s(3));
    std::string cnt = std::to_string(n) + " 件";
    if (total > 0) cnt += "（" + pct(static_cast<double>(n) / total) + "）";
    dl->AddText(ImVec2(p.x + lw + len + u.m.s(8), ty), u.t->muted, cnt.c_str());
    if (ImGui::IsMouseHoveringRect(p, ImVec2(p.x + W, p.y + h)) && ImGui::IsWindowHovered())
      ImGui::SetTooltip("%s 営業日: %d 件", trendBucketLabel(k), n);
    ImGui::Dummy(ImVec2(W, h));
  }
}

// 滞留の上位の一覧。行を押すと、その課題を選んで詳細を開く
void drawAgingTop(Frame& f, const TrendReport& tr, float W) {
  UiState& u = f.u;
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const float h = u.m.rowH();
  const float rankW = u.m.s(30), asgW = std::clamp(W * 0.22f, u.m.s(80), u.m.s(180)), daysW = u.m.s(96);
  const float nameW = std::max(W - rankW - asgW - daysW, u.m.s(80));
  {
    const ImVec2 q = ImGui::GetCursorScreenPos();
    dl->AddText(ImVec2(q.x + u.m.s(6), q.y), u.t->muted, "#");
    dl->AddText(ImVec2(q.x + rankW, q.y), u.t->muted, "課題");
    dl->AddText(ImVec2(q.x + rankW + nameW, q.y), u.t->muted, "担当");
    dl->AddText(ImVec2(q.x + rankW + nameW + asgW, q.y), u.t->muted, "動いていない日数");
    ImGui::Dummy(ImVec2(W, ImGui::GetTextLineHeight() + u.m.s(4)));
  }
  int rank = 0;
  for (const AgingItem& it : tr.aging.top) {
    ++rank;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool hov = ImGui::IsMouseHoveringRect(p, ImVec2(p.x + W, p.y + h)) && ImGui::IsWindowHovered();
    if (hov) dl->AddRectFilled(p, ImVec2(p.x + W, p.y + h), u.t->hover, u.m.s(4));
    const float ty = p.y + (h - ImGui::GetTextLineHeight()) / 2;
    const bool old = trendBucket(it.days) == kTrendBuckets - 1;
    dl->AddText(ImVec2(p.x + u.m.s(6), ty), u.t->muted, std::to_string(rank).c_str());
    clippedText(u, ImVec2(p.x + rankW, ty), nameW - u.m.s(10), u.t->ink, it.name);
    clippedText(u, ImVec2(p.x + rankW + nameW, ty), asgW - u.m.s(10), u.t->muted, it.assignee.empty() ? std::string("（担当なし）") : it.assignee);
    dl->AddText(ImVec2(p.x + rankW + nameW + asgW, ty), old ? u.t->late : u.t->ink, ((old ? "! " : "") + std::to_string(it.days) + " 営業日").c_str());
    dl->AddLine(ImVec2(p.x, p.y + h), ImVec2(p.x + W, p.y + h), u.t->rowline);
    u.statsTrend.agingRows.push_back({it.id, p, ImVec2(p.x + W, p.y + h)});
    if (hov) {
      const char* from = it.from == AgingFrom::Updated ? "更新日（Updated）" : it.from == AgingFrom::Start ? "開始日" : "作成日（Created）";
      ImGui::SetTooltip("%s\n最後に動いた日: %s（%s）\n押すと、この課題の詳細を開きます", it.id.c_str(), fmtLong(it.last).c_str(), from);
      if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) openDetail(f, it.id, 0);
    }
    ImGui::Dummy(ImVec2(W, h));
  }
}

// 「計画との差」の節（表示するベースラインがあるとき。Issue #53）。件数のタイル → 遅れの大きい順の上位。行を押すと課題を選んで詳細を開く
void drawPlanDiff(Frame& f, const BaselineCompare& bc, const Tree& T, const std::vector<Issue>& I, float W) {
  UiState& u = f.u;
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const std::string note = "「" + bc.baselineName + "」との比較（営業日の差。+ は計画より遅れ）";
  sectionHead(f, "計画との差", note.c_str(), W);
  const BaselineSummary& sm = bc.sum;
  {
    const int nt = 5;
    const float gap = u.m.s(8);
    const float tw = (W - gap * (nt - 1)) / nt;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    struct T5 {
      const char* l;
      int n;
      ImU32 col;
    } tl[] = {{"早まり", sm.early, sm.early ? u.t->ok : u.t->ink},
              {"計画どおり", sm.onPlan, u.t->ink},
              {"! 遅れ", sm.late, sm.late ? u.t->late : u.t->ink},
              {"計画になかった", sm.added, u.t->ink},
              {"消えた", sm.removed, u.t->ink}};
    for (int i = 0; i < nt; ++i) {
      ImGui::SetCursorScreenPos(ImVec2(p.x + static_cast<float>(i) * (tw + gap), p.y));
      tile(f, tl[i].l, std::to_string(tl[i].n) + " 件", tl[i].col, tw);
    }
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + u.m.s(64) + u.m.s(6)));
    u.statsPlan.tiles = nt;
  }
  if (bc.truncated) ImGui::TextDisabled("ベースラインは課題が多かったため、一部の課題だけです（「計画になかった」は数えません）。");
  if (sm.sameDayDiffers > 0)
    ImGui::TextDisabled("営業日の差が 0 でも完了日が違う課題が %d 件あります（休業日・土日をまたいだ日付の違い）。", sm.sameDayDiffers);
  ImGui::Dummy(ImVec2(0, u.m.s(4)));
  ImGui::PushFont(u.fontBold);
  ImGui::Text("遅れの大きい順（上位 %zu 件 / 遅れ %d 件）", bc.topLate.size(), bc.lateTotal);
  ImGui::PopFont();
  if (bc.topLate.empty()) {
    ImGui::TextDisabled("計画より遅れている課題はありません。");
    return;
  }
  const float h = u.m.rowH();
  const float rankW = u.m.s(30), asgW = std::clamp(W * 0.18f, u.m.s(70), u.m.s(150)), dateW = u.m.s(86), diffW = u.m.s(80);
  const float nameW = std::max(W - rankW - asgW - dateW * 2 - diffW, u.m.s(80));
  {
    const ImVec2 q = ImGui::GetCursorScreenPos();
    float x = q.x + u.m.s(6);
    dl->AddText(ImVec2(x, q.y), u.t->muted, "#");
    x = q.x + rankW;
    dl->AddText(ImVec2(x, q.y), u.t->muted, "課題");
    x += nameW;
    dl->AddText(ImVec2(x, q.y), u.t->muted, "担当");
    x += asgW;
    dl->AddText(ImVec2(x, q.y), u.t->muted, "計画の完了");
    x += dateW;
    dl->AddText(ImVec2(x, q.y), u.t->muted, "今の見込み");
    x += dateW;
    dl->AddText(ImVec2(x, q.y), u.t->muted, "差");
    ImGui::Dummy(ImVec2(W, ImGui::GetTextLineHeight() + u.m.s(4)));
  }
  int rank = 0;
  for (int idx : bc.topLate) {
    if (idx < 0 || static_cast<std::size_t>(idx) >= bc.entries.size() || static_cast<std::size_t>(idx) >= I.size()) continue;
    ++rank;
    const BaselineEntry& e = bc.entries[static_cast<std::size_t>(idx)];
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool hov = ImGui::IsMouseHoveringRect(p, ImVec2(p.x + W, p.y + h)) && ImGui::IsWindowHovered();
    if (hov) dl->AddRectFilled(p, ImVec2(p.x + W, p.y + h), u.t->hover, u.m.s(4));
    const float ty = p.y + (h - ImGui::GetTextLineHeight()) / 2;
    const std::string& asg = I[static_cast<std::size_t>(idx)].assignee;
    dl->AddText(ImVec2(p.x + u.m.s(6), ty), u.t->muted, std::to_string(rank).c_str());
    clippedText(u, ImVec2(p.x + rankW, ty), nameW - u.m.s(10), u.t->ink, T.did[static_cast<std::size_t>(idx)] + "  " + T.name[static_cast<std::size_t>(idx)]);
    clippedText(u, ImVec2(p.x + rankW + nameW, ty), asgW - u.m.s(10), u.t->muted, asg.empty() ? std::string("（担当なし）") : asg);
    dl->AddText(ImVec2(p.x + rankW + nameW + asgW, ty), u.t->muted, e.planFinish ? fmtMd(*e.planFinish).c_str() : "-");
    dl->AddText(ImVec2(p.x + rankW + nameW + asgW + dateW, ty), u.t->ink, fmtMd(e.nowFinish).c_str());
    dl->AddText(ImVec2(p.x + rankW + nameW + asgW + dateW * 2, ty), u.t->late, (baselineDiffMark(e) + " " + baselineDiffText(e)).c_str());
    dl->AddLine(ImVec2(p.x, p.y + h), ImVec2(p.x + W, p.y + h), u.t->rowline);
    u.statsPlan.lateRows.push_back({p, ImVec2(p.x + W, p.y + h), T.ids[static_cast<std::size_t>(idx)]});
    if (hov) {
      const auto lines = planTipLines(bc, e);
      ImGui::BeginTooltip();
      for (std::size_t q = 0; q < lines.size(); ++q) {
        if (q == 0) ImGui::TextUnformatted(lines[q].c_str());
        else ImGui::TextDisabled("%s", lines[q].c_str());
      }
      ImGui::TextDisabled("押すと、この課題の詳細を開きます");
      ImGui::EndTooltip();
      if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) openDetail(f, T.ids[static_cast<std::size_t>(idx)], 0);
    }
    ImGui::Dummy(ImVec2(W, h));
  }
}

// ---------------------------------------------------------------- 状況の件数の推移（累積フロー。Issue #64）

// 背景 bg に fg を不透明度 a で重ねた不透明な色（半透明の面を隣どうしで描くと、継ぎ目が濃い線に見えるので、あらかじめ混ぜておく）
ImU32 blendOver(ImU32 bg, ImU32 fg, float a) {
  const ImVec4 b = ImGui::ColorConvertU32ToFloat4(bg), f = ImGui::ColorConvertU32ToFloat4(fg);
  return ImGui::ColorConvertFloat4ToU32(ImVec4(b.x + (f.x - b.x) * a, b.y + (f.y - b.y) * a, b.z + (f.z - b.z) * a, 1.f));
}

// 凡例の見本: 面の色の四角に、境界の線種（実線・破線・点線）を重ねる
void cfdLegend(Frame& f, const CfdWeek* latest, float W) {
  UiState& u = f.u;
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const float x0 = ImGui::GetCursorScreenPos().x;
  // 幅に収まらないときは次の行へ（狭い画面・詳細パネルを開いたとき）
  const auto wrapIfNeeded = [&](float itemW) {
    const float x = ImGui::GetCursorScreenPos().x;
    if (x > x0 + 1.f && x + itemW > x0 + W) ImGui::NewLine();
  };
  const float swW = u.m.s(26);
  const auto sample = [&](const std::string& label, ImU32 fill, int style) {
    wrapIfNeeded(swW + u.m.s(5) + ImGui::CalcTextSize(label.c_str()).x);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = ImGui::GetTextLineHeight();
    dl->AddRectFilled(ImVec2(p.x, p.y + u.m.s(2)), ImVec2(p.x + swW, p.y + h - u.m.s(2)), blendOver(u.t->bg, fill, 0.55f), 0);
    const float y = p.y + u.m.s(2);
    if (style == 0) dl->AddLine(ImVec2(p.x, y), ImVec2(p.x + swW, y), u.t->ink, u.m.s(2));
    else if (style == 1) dashedLine(dl, ImVec2(p.x, y), ImVec2(p.x + swW, y), u.t->ink, u.m.s(2), u.m.s(5), u.m.s(4));
    else dashedLine(dl, ImVec2(p.x, y), ImVec2(p.x + swW, y), u.t->ink, u.m.s(2), u.m.s(2), u.m.s(3));
    ImGui::Dummy(ImVec2(swW, h));
    ImGui::SameLine(0, u.m.s(5));
    ImGui::TextUnformatted(label.c_str());
    ImGui::SameLine(0, u.m.s(16));
  };
  sample(latest ? "完了（実線） " + std::to_string(latest->done) : std::string("完了（実線）"), u.t->ok, 0);
  sample(latest ? "進行中（破線） " + std::to_string(latest->doing) : std::string("進行中（破線）"), u.t->accent, 1);
  sample(latest ? "未着手（点線） " + std::to_string(latest->todo) : std::string("未着手（点線）"), u.t->line2, 2);
  {  // 記録なし: 点線の枠
    const char* label = "記録なし（線を引かない）";
    wrapIfNeeded(swW + u.m.s(5) + ImGui::CalcTextSize(label).x);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = ImGui::GetTextLineHeight();
    const ImVec2 a(p.x, p.y + u.m.s(2)), b(p.x + swW, p.y + h - u.m.s(2));
    dl->AddRectFilled(a, b, withAlpha(u.t->muted, 0.08f));
    dashedLine(dl, a, ImVec2(b.x, a.y), u.t->muted, 1.f, u.m.s(2), u.m.s(2));
    dashedLine(dl, ImVec2(a.x, b.y), b, u.t->muted, 1.f, u.m.s(2), u.m.s(2));
    ImGui::Dummy(ImVec2(swW, h));
    ImGui::SameLine(0, u.m.s(5));
    ImGui::TextUnformatted(label);
  }
}

// 週ごとの未着手・進行中・完了の件数を積み上げた面。記録のない週は線を引かず「記録なし」と薄く出す
void drawCfdChart(Frame& f, const CfdSeries& cs, float W) {
  UiState& u = f.u;
  UiState::StatsCfd& sc = u.statsCfd;
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const int cols = static_cast<int>(cs.weeks.size());
  // 列は W に収まるまで縮める（最大 104 列でも幅を超えない）。狭いほど日付の見出しを間引く
  const float bw = std::min(W / static_cast<float>(cols), u.m.s(64));
  const float chartH = u.m.s(150), axisH = u.m.s(20), top = u.m.s(22);
  const int every = std::max(1, static_cast<int>(std::ceil(u.m.s(34) / bw)));

  const CfdWeek* latest = nullptr;
  for (int i = cols - 1; i >= 0 && !latest; --i)
    if (!cs.weeks[static_cast<std::size_t>(i)].gap) latest = &cs.weeks[static_cast<std::size_t>(i)];
  cfdLegend(f, latest, W);
  const ImVec2 p = ImGui::GetCursorScreenPos();
  ImGui::Dummy(ImVec2(W, chartH + axisH));
  sc.chartX1 = p.x + W;

  const float unit = (chartH - top) / static_cast<float>(std::max(1, cs.maxTotal));
  const auto xOf = [&](int i) { return p.x + bw * static_cast<float>(i) + bw / 2; };
  const auto yAt = [&](int v) { return p.y + chartH - unit * static_cast<float>(v); };
  const auto at = [&](int i) -> const CfdWeek& { return cs.weeks[static_cast<std::size_t>(i)]; };
  const float baseY = p.y + chartH;

  // 記録のない週: 薄い帯と点線の枠（連続する週はひとつの帯にして「記録なし」と書く）
  for (int i = 0; i < cols;) {
    if (!at(i).gap) {
      ++i;
      continue;
    }
    int j = i;
    while (j < cols && at(j).gap) ++j;
    const ImVec2 a(p.x + bw * static_cast<float>(i), p.y), b(p.x + bw * static_cast<float>(j), baseY);
    dl->AddRectFilled(a, b, withAlpha(u.t->muted, 0.08f));
    dashedLine(dl, a, ImVec2(a.x, b.y), withAlpha(u.t->muted, 0.5f), 1.f, u.m.s(2), u.m.s(3));
    dashedLine(dl, ImVec2(b.x, a.y), b, withAlpha(u.t->muted, 0.5f), 1.f, u.m.s(2), u.m.s(3));
    dashedLine(dl, a, ImVec2(b.x, a.y), withAlpha(u.t->muted, 0.5f), 1.f, u.m.s(2), u.m.s(3));
    const float tw = ImGui::CalcTextSize("記録なし").x;
    if (b.x - a.x >= tw + u.m.s(6))
      dl->AddText(ImVec2((a.x + b.x - tw) / 2, p.y + chartH / 2 - ImGui::GetTextLineHeight() / 2), withAlpha(u.t->muted, 0.8f), "記録なし");
    i = j;
  }
  dl->AddLine(ImVec2(p.x, baseY), ImVec2(p.x + bw * static_cast<float>(cols), baseY), u.t->line);

  // 隣り合う記録のある週のあいだだけ面で塗る。孤立した週は細い柱（前後を補って線を作らない）
  const auto quad = [&](float x0, float ya0, float yb0, float x1, float ya1, float yb1, ImU32 col) {
    dl->AddConvexPolyFilled(std::array<ImVec2, 4>{ImVec2(x0, ya0), ImVec2(x1, ya1), ImVec2(x1, yb1), ImVec2(x0, yb0)}.data(), 4, blendOver(u.t->bg, col, 0.55f));
  };
  const ImDrawListFlags oldFlags = dl->Flags;
  dl->Flags &= ~ImDrawListFlags_AntiAliasedFill;  // 隣どうしの面の継ぎ目に薄い線が出ないよう、面の縁は塗りつぶし（境界の線は別に引く）
  for (int i = 0; i + 1 < cols; ++i) {
    if (at(i).gap || at(i + 1).gap) continue;
    const CfdWeek &a = at(i), &b = at(i + 1);
    const float x0 = xOf(i), x1 = xOf(i + 1);
    quad(x0, baseY, yAt(a.done), x1, baseY, yAt(b.done), u.t->ok);
    quad(x0, yAt(a.done), yAt(a.done + a.doing), x1, yAt(b.done), yAt(b.done + b.doing), u.t->accent);
    quad(x0, yAt(a.done + a.doing), yAt(a.total), x1, yAt(b.done + b.doing), yAt(b.total), u.t->line2);
  }
  dl->Flags = oldFlags;
  // 境界の線。記録のある連続した週ごとにひとつの折れ線として引き、破線・点線の位相はその中で持ち越す
  // （週の組ごとにリセットすると、1 区間が破線より短い 104 週では実線に見えてしまう）。記録なしの区間でだけ切れる
  for (int i = 0; i < cols;) {
    if (at(i).gap) {
      ++i;
      continue;
    }
    int j = i;
    while (j < cols && !at(j).gap) ++j;
    if (j - i >= 2) {
      std::vector<ImVec2> done, doing, total;
      for (int k = i; k < j; ++k) {
        done.push_back(ImVec2(xOf(k), yAt(at(k).done)));
        doing.push_back(ImVec2(xOf(k), yAt(at(k).done + at(k).doing)));
        total.push_back(ImVec2(xOf(k), yAt(at(k).total)));
      }
      dl->AddPolyline(done.data(), static_cast<int>(done.size()), u.t->ink, ImDrawFlags_None, u.m.s(2));
      dashedPolyline(dl, doing, u.t->ink, u.m.s(2), u.m.s(5), u.m.s(4));
      dashedPolyline(dl, total, u.t->ink, u.m.s(2), u.m.s(2), u.m.s(3));
    }
    i = j;
  }
  const float colW = std::min(u.m.s(6), bw * 0.5f);
  for (int i = 0; i < cols; ++i) {
    const CfdWeek& w = at(i);
    if (w.gap) continue;
    const bool prevOk = i > 0 && !at(i - 1).gap, nextOk = i + 1 < cols && !at(i + 1).gap;
    if (!prevOk && !nextOk) {
      const float x = xOf(i) - colW / 2;
      dl->AddRectFilled(ImVec2(x, yAt(w.done)), ImVec2(x + colW, baseY), blendOver(u.t->bg, u.t->ok, 0.55f));
      dl->AddRectFilled(ImVec2(x, yAt(w.done + w.doing)), ImVec2(x + colW, yAt(w.done)), blendOver(u.t->bg, u.t->accent, 0.55f));
      dl->AddRectFilled(ImVec2(x, yAt(w.total)), ImVec2(x + colW, yAt(w.done + w.doing)), blendOver(u.t->bg, u.t->line2, 0.55f));
    }
    if (bw >= u.m.s(10) || !prevOk || !nextOk) dl->AddCircleFilled(ImVec2(xOf(i), yAt(w.total)), u.m.s(2.5f), u.t->ink);  // 列が細いときは、点が連なって点線を隠さないよう、連続した週の中の点は省く
  }
  // 最新の記録の合計（数字でも読める）
  if (latest) {
    const int li = static_cast<int>(latest - cs.weeks.data());
    const std::string lb = "合計 " + std::to_string(latest->total);
    const float tw = ImGui::CalcTextSize(lb.c_str()).x;
    dl->AddText(ImVec2(std::min(xOf(li) - tw, p.x + W - tw), yAt(latest->total) - ImGui::GetTextLineHeight() - u.m.s(4)), u.t->muted, lb.c_str());
  }
  // 横軸の週の見出し
  for (int i = 0; i < cols; ++i) {
    if (i % every != 0 && i != cols - 1) continue;
    const std::string lb = fmtShort(at(i).start);
    const float tw = ImGui::CalcTextSize(lb.c_str()).x;
    dl->AddText(ImVec2(xOf(i) - tw / 2, baseY + u.m.s(3)), at(i).gap ? withAlpha(u.t->muted, 0.7f) : u.t->muted, lb.c_str());
  }
  // ホバー: 週ごとの値（記録のない週は「記録なし」）
  sc.cols.clear();
  for (int i = 0; i < cols; ++i) {
    const ImVec2 a(p.x + bw * static_cast<float>(i), p.y), b(p.x + bw * static_cast<float>(i + 1), baseY + axisH);
    sc.cols.push_back({a, ImVec2(b.x, baseY)});
    if (ImGui::IsMouseHoveringRect(a, b) && ImGui::IsWindowHovered()) ImGui::SetTooltip("%s", cfdWeekText(at(i)).c_str());
  }
}

// 状況の件数の推移の節。履歴は Session が覚えている（開いたとき・週次レポート・「いま記録する」で週に 1 回まで）
void drawCfd(Frame& f, Session& s, Day base, float W) {
  UiState& u = f.u;
  UiState::StatsCfd& sc = u.statsCfd;
  sc = UiState::StatsCfd{};
  sc.drawn = true;
  const History& hist = s.history();
  const CfdSeries cs = cfdSeries(hist, base);
  sc.recorded = cs.recorded;
  sc.weeks = static_cast<int>(cs.weeks.size());
  sc.gaps = sc.weeks - cs.recorded;

  ImGui::Dummy(ImVec2(0, u.m.s(14)));
  ImGui::PushFont(u.fontBold);
  ImGui::TextUnformatted("状況の件数の推移（累積フロー）");
  ImGui::PopFont();
  ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + W);
  ImGui::TextDisabled("アプリを開いたとき・週次レポートを出したとき・下のボタンで、週に 1 回までの件数を覚えています（最大 %d 週）。件数だけで、課題の名前は覚えません。絞り込みに関係なく、プロジェクト全体です。",
                      static_cast<int>(kHistoryMaxWeeks));
  ImGui::PopTextWrapPos();
  ImGui::Dummy(ImVec2(0, u.m.s(2)));
  if (ImGui::Button("いま記録する")) f.c.recordHistory(app::AppController::HistoryTrigger::Manual);
  sc.recA = ImGui::GetItemRectMin();
  sc.recB = ImGui::GetItemRectMax();
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("今週の件数を記録します。同じ週にもう記録があれば、いまの状態で置き換えます");
  ImGui::SameLine(0, u.m.s(10));
  ImGui::AlignTextToFramePadding();
  ImGui::TextDisabled("記録 %d 週ぶん", cs.recorded);
  ImGui::Dummy(ImVec2(0, u.m.s(4)));
  if (!cs.drawable()) {
    sc.pending = true;
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + W);
    ImGui::TextUnformatted(cfdPendingText(cs.recorded).c_str());
    ImGui::PopTextWrapPos();
    return;
  }
  sc.chart = true;
  drawCfdChart(f, cs, W);
  if (sc.gaps > 0) {
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + W);
    ImGui::TextDisabled("記録のない週が %d 週あります（点線の枠の帯。線は引かず、補いません）。", sc.gaps);
    ImGui::PopTextWrapPos();
  }
}

// ---- マイルストーンの予測の推移（Issue #75）----
// 印（点）。達成済みは中を背景色にした輪
void mtaMarkerShape(ImDrawList* dl, MtaMarker m, ImVec2 c, float r, ImU32 col, ImU32 bg, bool hollow, float th) {
  const ImU32 fill = hollow ? bg : col;
  switch (m) {
    case MtaMarker::Circle:
      dl->AddCircleFilled(c, r, fill);
      dl->AddCircle(c, r, col, 0, th);
      break;
    case MtaMarker::Square:
      dl->AddRectFilled(ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), fill);
      dl->AddRect(ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), col, 0, 0, th);
      break;
    case MtaMarker::Triangle: {
      const ImVec2 a(c.x, c.y - r - 1), b(c.x + r + 1, c.y + r), d(c.x - r - 1, c.y + r);
      dl->AddTriangleFilled(a, b, d, fill);
      dl->AddTriangle(a, b, d, col, th);
      break;
    }
    case MtaMarker::Diamond: {
      const ImVec2 a(c.x, c.y - r - 1), b(c.x + r + 1, c.y), d(c.x, c.y + r + 1), e(c.x - r - 1, c.y);
      dl->AddQuadFilled(a, b, d, e, fill);
      dl->AddQuad(a, b, d, e, col, th);
      break;
    }
    case MtaMarker::Cross:
      dl->AddLine(ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), col, th);
      dl->AddLine(ImVec2(c.x + r, c.y - r), ImVec2(c.x - r, c.y + r), col, th);
      break;
    case MtaMarker::Plus:
      dl->AddLine(ImVec2(c.x - r - 1, c.y), ImVec2(c.x + r + 1, c.y), col, th);
      dl->AddLine(ImVec2(c.x, c.y - r - 1), ImVec2(c.x, c.y + r + 1), col, th);
      break;
  }
}

// 線種（dash == 0 は実線）で折れ線を引く
void mtaPolyline(ImDrawList* dl, const std::vector<ImVec2>& pts, ImU32 col, float th, float dash, float gap) {
  if (pts.size() < 2) return;
  if (dash <= 0.f) dl->AddPolyline(pts.data(), static_cast<int>(pts.size()), col, ImDrawFlags_None, th);
  else dashedPolyline(dl, pts, col, th, dash, gap);
}

// 遅れの警告（統計の一番上）。前の週の記録から、しきい値以上遅れたマイルストーン
void drawMtaBand(Frame& f, const MtaReport* rep, float W) {
  UiState& u = f.u;
  UiState::StatsMta& sm = u.statsMta;
  if (!rep || rep->warnings == 0) return;
  ImGui::Dummy(ImVec2(0, u.m.s(10)));
  const ImVec2 p0 = ImGui::GetCursorScreenPos();
  const float pad = u.m.s(10), padY = u.m.s(6);
  ImGui::SetCursorScreenPos(ImVec2(p0.x + pad, p0.y + padY));
  ImGui::BeginGroup();
  ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + W - pad);
  ImGui::PushFont(u.fontBold);
  ImGui::PushStyleColor(ImGuiCol_Text, u.t->late);
  ImGui::Text("! 遅れの警告（マイルストーンの予測）");
  ImGui::PopStyleColor();
  ImGui::PopFont();
  ImGui::TextDisabled("完了の見込みが、前の週の記録から %d 営業日以上遅れたマイルストーンです（しきい値はアプリの設定で変えられます）。", rep->lateDays);
  constexpr int kMaxLines = 5;
  int shown = 0;
  for (const MtaSeries& s : rep->series) {
    if (!s.warn) continue;
    if (shown >= kMaxLines) break;
    ImGui::PushStyleColor(ImGuiCol_Text, u.t->late);
    ImGui::TextUnformatted(mtaWarnText(s).c_str());
    ImGui::PopStyleColor();
    ++shown;
  }
  if (rep->warnings > shown) ImGui::TextDisabled("ほか %d 件（下の「マイルストーンの予測の推移」で見られます）", rep->warnings - shown);
  ImGui::PopTextWrapPos();
  ImGui::EndGroup();
  const ImVec2 p1(ImGui::GetItemRectMax().x, ImGui::GetItemRectMax().y + padY);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled(ImVec2(p0.x, p0.y), ImVec2(p0.x + u.m.s(4), p1.y), u.t->late);  // 色だけに頼らない（「!」と文字でも分かる）
  dl->AddRect(ImVec2(p0.x, p0.y), ImVec2(p0.x + W, p1.y), withAlpha(u.t->late, 0.5f), u.m.s(3));
  sm.bandA = p0;
  sm.bandB = ImVec2(p0.x + W, p1.y);
  sm.bandLines = shown;
  ImGui::SetCursorScreenPos(ImVec2(p0.x, p1.y));
  ImGui::Dummy(ImVec2(W, u.m.s(4)));
}

// 凡例: 系列ごとの線種と印の見本 + 期限・計画・記録なし・「!」
void mtaLegend(Frame& f, const MtaReport& rep, const std::vector<std::size_t>& shown, float W) {
  UiState& u = f.u;
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const float x0 = ImGui::GetCursorScreenPos().x;
  const auto wrapIfNeeded = [&](float itemW) {
    const float x = ImGui::GetCursorScreenPos().x;
    if (x > x0 + 1.f && x + itemW > x0 + W) ImGui::NewLine();
  };
  const float swW = u.m.s(40);
  const auto sample = [&](const std::string& label, ImU32 col, float dash, float gap, float th, const MtaMarker* mk) {
    wrapIfNeeded(swW + u.m.s(5) + ImGui::CalcTextSize(label.c_str()).x);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = ImGui::GetTextLineHeight();
    const float y = p.y + h / 2;
    if (dash <= 0.f) dl->AddLine(ImVec2(p.x, y), ImVec2(p.x + swW, y), col, th);
    else dashedLine(dl, ImVec2(p.x, y), ImVec2(p.x + swW, y), col, th, dash, gap);
    if (mk) mtaMarkerShape(dl, *mk, ImVec2(p.x + swW / 2, y), u.m.s(3.6f), col, u.t->bg, false, u.m.s(2));
    ImGui::Dummy(ImVec2(swW, h));
    ImGui::SameLine(0, u.m.s(5));
    ImGui::TextUnformatted(label.c_str());
    ImGui::SameLine(0, u.m.s(16));
  };
  for (std::size_t order = 0; order < shown.size(); ++order) {
    const MtaStyle st = mtaStyleOf(order);
    const MtaMarker mk = st.marker;
    sample(mtaLegendText(rep.series[shown[order]]), u.t->c[st.color % 8], u.m.s(static_cast<float>(st.dash)), u.m.s(static_cast<float>(st.gap)), u.m.s(2), &mk);
  }
  sample("期限（長い破線の横線）", u.t->muted, u.m.s(10), u.m.s(5), u.m.s(1.4f), nullptr);
  sample("計画（点線の横線）", u.t->muted, u.m.s(2), u.m.s(3), u.m.s(1.4f), nullptr);
  {  // 記録なし: 点線の枠
    const char* label = "記録なし（線を引かない）";
    wrapIfNeeded(u.m.s(26) + u.m.s(5) + ImGui::CalcTextSize(label).x);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = ImGui::GetTextLineHeight();
    const ImVec2 a(p.x, p.y + u.m.s(2)), b(p.x + u.m.s(26), p.y + h - u.m.s(2));
    dl->AddRectFilled(a, b, withAlpha(u.t->muted, 0.08f));
    dashedLine(dl, a, ImVec2(b.x, a.y), u.t->muted, 1.f, u.m.s(2), u.m.s(2));
    dashedLine(dl, ImVec2(a.x, b.y), b, u.t->muted, 1.f, u.m.s(2), u.m.s(2));
    ImGui::Dummy(ImVec2(u.m.s(26), h));
    ImGui::SameLine(0, u.m.s(5));
    ImGui::TextUnformatted(label);
    ImGui::SameLine(0, u.m.s(16));
  }
  ImGui::TextUnformatted("! しきい値以上の遅れ");
}

// 折れ線: 横軸は週（記録のない週は帯）と「いま」、縦軸は完了の見込み日（上ほど遅れ）。期限・計画は横線
void drawMtaChart(Frame& f, const MtaReport& rep, const std::vector<std::size_t>& shown, float W) {
  UiState& u = f.u;
  UiState::StatsMta& sm = u.statsMta;
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const MtaYAxis ax = mtaYAxis(rep, shown);
  if (!ax.ok) {
    ImGui::TextUnformatted("図に出すマイルストーンを選んでください（記録のあるもの）。");
    return;
  }
  const int nc = static_cast<int>(rep.columns.size());
  const float left = u.m.s(46), right = std::min(u.m.s(150), W * 0.3f), top = u.m.s(16), plotH = u.m.s(210), axisH = u.m.s(22);
  const float plotW = std::max(W - left - right, u.m.s(120));
  const float colW = plotW / static_cast<float>(nc);
  const int every = std::max(1, static_cast<int>(std::ceil(u.m.s(46) / colW)));
  const ImVec2 p = ImGui::GetCursorScreenPos();
  ImGui::Dummy(ImVec2(W, top + plotH + axisH));
  sm.chart = true;
  sm.chartX1 = p.x + W;
  const float plotRight = p.x + left + plotW;
  const double span = std::max(1.0, static_cast<double>(ax.hi - ax.lo));
  const auto xOf = [&](int i) { return p.x + left + colW * (static_cast<float>(i) + 0.5f); };
  const auto yOf = [&](Day d) { return p.y + top + static_cast<float>((static_cast<double>(ax.hi) - static_cast<double>(d)) / span) * plotH; };
  const float baseY = p.y + top + plotH;

  // 記録のない週: 薄い帯と点線の枠（連続する週はひとつの帯）
  for (int i = 0; i < nc;) {
    const MtaColumn& c = rep.columns[static_cast<std::size_t>(i)];
    if (c.recorded || c.now) {
      ++i;
      continue;
    }
    int j = i;
    while (j < nc && !rep.columns[static_cast<std::size_t>(j)].recorded && !rep.columns[static_cast<std::size_t>(j)].now) ++j;
    const ImVec2 a(p.x + left + colW * static_cast<float>(i), p.y + top), b(p.x + left + colW * static_cast<float>(j), baseY);
    dl->AddRectFilled(a, b, withAlpha(u.t->muted, 0.08f));
    dashedLine(dl, a, ImVec2(a.x, b.y), withAlpha(u.t->muted, 0.5f), 1.f, u.m.s(2), u.m.s(3));
    dashedLine(dl, ImVec2(b.x, a.y), b, withAlpha(u.t->muted, 0.5f), 1.f, u.m.s(2), u.m.s(3));
    dashedLine(dl, a, ImVec2(b.x, a.y), withAlpha(u.t->muted, 0.5f), 1.f, u.m.s(2), u.m.s(3));
    const float tw = ImGui::CalcTextSize("記録なし").x;
    if (b.x - a.x >= tw + u.m.s(6)) dl->AddText(ImVec2((a.x + b.x - tw) / 2, p.y + top + plotH / 2 - ImGui::GetTextLineHeight() / 2), withAlpha(u.t->muted, 0.8f), "記録なし");
    i = j;
  }
  // 目盛りの横線と日付
  for (Day t : ax.ticks) {
    const float y = yOf(t);
    dl->AddLine(ImVec2(p.x + left, y), ImVec2(plotRight, y), u.t->line);
    const std::string lb = fmtShort(t);
    dl->AddText(ImVec2(p.x + left - u.m.s(6) - ImGui::CalcTextSize(lb.c_str()).x, y - ImGui::GetTextLineHeight() / 2), u.t->muted, lb.c_str());
  }
  dl->AddText(ImVec2(p.x, p.y), u.t->muted, "遅れ ↑");
  dashedLine(dl, ImVec2(xOf(nc - 1), p.y + top), ImVec2(xOf(nc - 1), baseY), withAlpha(u.t->muted, 0.7f), 1.f, u.m.s(3), u.m.s(3));  // 「いま」の列

  struct Label {
    float y;
    std::string text;
    ImU32 col;
  };
  std::vector<Label> labels;
  // 期限（長い破線）・計画（点線）
  for (std::size_t order = 0; order < shown.size(); ++order) {
    const MtaSeries& s = rep.series[shown[order]];
    const MtaStyle st = mtaStyleOf(order);
    const ImU32 col = withAlpha(u.t->c[st.color % 8], 0.85f);
    if (s.deadline) {
      const float y = yOf(*s.deadline);
      dashedLine(dl, ImVec2(p.x + left, y), ImVec2(plotRight, y), col, u.m.s(1.4f), u.m.s(10), u.m.s(5));
      labels.push_back({y, std::string(s.plan && *s.plan == *s.deadline ? "期限・計画 " : "期限 ") + fmtShort(*s.deadline) + "（" + s.did + "）", u.t->c[st.color % 8]});
    }
    if (s.plan && !(s.deadline && *s.plan == *s.deadline)) {
      const float y = yOf(*s.plan);
      dashedLine(dl, ImVec2(p.x + left, y), ImVec2(plotRight, y), col, u.m.s(1.4f), u.m.s(2), u.m.s(3));
      labels.push_back({y, "計画 " + fmtShort(*s.plan) + "（" + s.did + "）", u.t->c[st.color % 8]});
    }
  }
  // 系列の線と印
  for (std::size_t order = 0; order < shown.size(); ++order) {
    const MtaSeries& s = rep.series[shown[order]];
    const MtaStyle st = mtaStyleOf(order);
    const ImU32 col = u.t->c[st.color % 8];
    std::vector<ImVec2> run;
    const auto flush = [&] {
      mtaPolyline(dl, run, col, u.m.s(2), u.m.s(static_cast<float>(st.dash)), u.m.s(static_cast<float>(st.gap)));
      run.clear();
    };
    int lastI = -1;
    for (int i = 0; i < nc; ++i) {
      const MtaPoint& pt = s.points[static_cast<std::size_t>(i)];
      if (!pt.has) {
        flush();
        continue;
      }
      run.push_back(ImVec2(xOf(i), yOf(pt.date)));
      lastI = i;
    }
    flush();
    for (int i = 0; i < nc; ++i) {
      const MtaPoint& pt = s.points[static_cast<std::size_t>(i)];
      if (!pt.has) continue;
      const ImVec2 c(xOf(i), yOf(pt.date));
      mtaMarkerShape(dl, st.marker, c, u.m.s(3.6f), col, u.t->bg, pt.achieved, u.m.s(2));
      if (pt.late) {  // 「!」の丸い印（色だけに頼らない）
        const ImVec2 bc(c.x, c.y - u.m.s(15));
        dl->AddCircleFilled(bc, u.m.s(7), u.t->late);
        const float tw = ImGui::CalcTextSize("!").x;
        dl->AddText(ImVec2(bc.x - tw / 2, bc.y - ImGui::GetTextLineHeight() / 2), IM_COL32(255, 255, 255, 255), "!");
      }
      if (pt.achieved) dl->AddText(ImVec2(c.x + u.m.s(7), c.y + u.m.s(2)), u.t->muted, "達成");
    }
    if (lastI >= 0) labels.push_back({yOf(s.points[static_cast<std::size_t>(lastI)].date), s.did.empty() ? mtaSeriesName(s) : s.did, col});
  }
  dl->AddLine(ImVec2(p.x + left, baseY), ImVec2(plotRight, baseY), u.t->line);
  // 右の余白の文字: 重ならないよう上から間隔を空ける
  std::stable_sort(labels.begin(), labels.end(), [](const Label& a, const Label& b) { return a.y < b.y; });
  const float lineH = ImGui::GetTextLineHeight();
  float prev = p.y + top - lineH;
  for (Label& l : labels) {
    l.y = std::max(l.y - lineH / 2, prev + lineH);
    prev = l.y;
  }
  float next = baseY + lineH;
  for (std::size_t k = labels.size(); k-- > 0;) {
    labels[k].y = std::min(labels[k].y, next - lineH);
    next = labels[k].y;
  }
  for (const Label& l : labels) clippedText(u, ImVec2(plotRight + u.m.s(6), l.y), right - u.m.s(8), l.col, l.text);
  // 横軸の週の見出し
  for (int i = 0; i < nc; ++i) {
    const MtaColumn& c = rep.columns[static_cast<std::size_t>(i)];
    if (!c.now && i % every != 0) continue;
    const std::string lb = c.now ? std::string("いま") : fmtShort(c.week);
    const float tw = ImGui::CalcTextSize(lb.c_str()).x;
    dl->AddText(ImVec2(xOf(i) - tw / 2, baseY + u.m.s(3)), c.now ? u.t->ink : u.t->muted, lb.c_str());
  }
  // ホバー: 列ごとに、選んだ系列の値（記録のない週は「記録なし」）
  sm.cols.clear();
  for (int i = 0; i < nc; ++i) {
    const ImVec2 a(p.x + left + colW * static_cast<float>(i), p.y), b(p.x + left + colW * static_cast<float>(i + 1), baseY + axisH);
    sm.cols.push_back({a, ImVec2(b.x, baseY)});
    if (ImGui::IsMouseHoveringRect(a, b) && ImGui::IsWindowHovered()) {
      std::string t;
      for (std::size_t order = 0; order < shown.size(); ++order) {
        if (!t.empty()) t += "\n\n";
        t += mtaPointText(rep.series[shown[order]], rep.columns[static_cast<std::size_t>(i)], static_cast<std::size_t>(i));
      }
      ImGui::SetTooltip("%s", t.c_str());
    }
  }
}

// 表: マイルストーンごとに、週ごとの予測日と前の点からの動き。行を押すと、そのマイルストーンの詳細を開く
void drawMtaTable(Frame& f, const MtaReport& rep, float W) {
  UiState& u = f.u;
  UiState::StatsMta& sm = u.statsMta;
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const float h = u.m.rowH();
  const float nameW = std::clamp(W * 0.34f, u.m.s(120), u.m.s(340)), weekW = u.m.s(104), dateW = u.m.s(104);
  const float moveW = std::max(W - nameW - weekW - dateW, u.m.s(80));
  {
    const ImVec2 q = ImGui::GetCursorScreenPos();
    dl->AddText(ImVec2(q.x + u.m.s(6), q.y), u.t->muted, "マイルストーン");
    dl->AddText(ImVec2(q.x + nameW, q.y), u.t->muted, "週");
    dl->AddText(ImVec2(q.x + nameW + weekW, q.y), u.t->muted, "予測日");
    dl->AddText(ImVec2(q.x + nameW + weekW + dateW, q.y), u.t->muted, "前の点からの動き");
    ImGui::Dummy(ImVec2(W, ImGui::GetTextLineHeight() + u.m.s(4)));
  }
  constexpr std::size_t kMaxRows = 300;
  std::size_t rows = 0, omitted = 0;
  for (const MtaSeries& s : rep.series) {
    bool first = true;
    for (std::size_t c = 0; c < s.points.size(); ++c) {
      const MtaPoint& pt = s.points[c];
      if (!pt.has && !pt.ended) continue;
      if (rows >= kMaxRows) {
        ++omitted;
        continue;
      }
      ++rows;
      const ImVec2 p = ImGui::GetCursorScreenPos();
      const bool hov = !s.removed && ImGui::IsMouseHoveringRect(p, ImVec2(p.x + W, p.y + h)) && ImGui::IsWindowHovered();
      if (hov) dl->AddRectFilled(p, ImVec2(p.x + W, p.y + h), u.t->hover, u.m.s(4));
      const float ty = p.y + (h - ImGui::GetTextLineHeight()) / 2;
      if (first) clippedText(u, ImVec2(p.x + u.m.s(6), ty), nameW - u.m.s(12), s.warn ? u.t->late : u.t->ink, (s.warn ? "! " : "") + mtaLegendText(s));
      clippedText(u, ImVec2(p.x + nameW, ty), weekW - u.m.s(8), u.t->muted, mtaColumnText(rep.columns[c]));
      clippedText(u, ImVec2(p.x + nameW + weekW, ty), dateW - u.m.s(8), u.t->ink, mtaDateText(pt));
      clippedText(u, ImVec2(p.x + nameW + weekW + dateW, ty), moveW - u.m.s(8), pt.late ? u.t->late : u.t->ink, mtaMoveText(pt));
      dl->AddLine(ImVec2(p.x, p.y + h), ImVec2(p.x + W, p.y + h), u.t->rowline);
      if (first && !s.removed) sm.rows.push_back({s.id, p, ImVec2(p.x + W, p.y + h)});
      if (hov) {
        ImGui::SetTooltip("%s\n押すと、このマイルストーンの詳細を開きます", mtaPointText(s, rep.columns[c], c).c_str());
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) openDetail(f, s.id, 0);
      }
      ImGui::Dummy(ImVec2(W, h));
      first = false;
    }
  }
  sm.tableRows = static_cast<int>(rows);
  if (omitted > 0) {
    ImGui::TextDisabled("ほか %d 行は省きました。", static_cast<int>(omitted));
  }
}

// 「マイルストーンの予測の推移」の節。図に出すマイルストーンの選択（プロジェクトごと）→ 凡例 → 折れ線 → 表
void drawMta(Frame& f, const Result<MtaReport>& res, const Tree& T, float W) {
  UiState& u = f.u;
  UiState::StatsMta& sm = u.statsMta;
  sm.drawn = true;
  ImGui::Dummy(ImVec2(0, u.m.s(14)));
  sm.secY = ImGui::GetCursorScreenPos().y;
  ImGui::PushFont(u.fontBold);
  ImGui::TextUnformatted("マイルストーンの予測の推移");
  ImGui::PopFont();
  ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + W);
  ImGui::TextDisabled("アプリを開いたとき、週に 1 回まで、マイルストーンの完了の見込み日を覚えています（最大 %d 週）。線が上へ動けば遅れ、下へ動けば前倒し、同じ高さなら安定です。絞り込みに関係なく、プロジェクト全体です。",
                      static_cast<int>(kWeeklyAutoMaxSnapshots));
  ImGui::PopTextWrapPos();
  if (!res.ok()) {
    ImGui::TextUnformatted("推移を集計できません。");
    return;
  }
  const MtaReport& rep = res.value();
  sm.series = static_cast<int>(rep.series.size());
  sm.recorded = rep.recordedWeeks;
  sm.warnings = rep.warnings;
  sm.columns = static_cast<int>(rep.columns.size());
  for (const MtaColumn& c : rep.columns)
    if (!c.recorded && !c.now) ++sm.gaps;
  if (rep.series.empty()) {
    ImGui::TextUnformatted("マイルストーンがありません。種別が Milestone の課題を足すと、ここに予測の推移が出ます。");
    return;
  }
  (void)T;
  ImGui::TextDisabled("前の記録から %d 営業日以上遅れると「!」を付け、統計の上に警告を出します（アプリの設定で変えられます）。", rep.lateDays);
  ImGui::Dummy(ImVec2(0, u.m.s(4)));
  if (!rep.drawable()) {
    sm.pending = true;
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + W);
    ImGui::TextUnformatted(mtaPendingText(rep.recordedWeeks).c_str());
    ImGui::PopTextWrapPos();
    return;
  }
  // 図に出すマイルストーン（初めは警告のあるもの → 記録のあるもの）
  if (!sm.inited) {
    sm.inited = true;
    sm.shownIds.clear();
    for (std::size_t i : mtaDefaultShown(rep)) sm.shownIds.push_back(rep.series[i].id);
  }
  const auto isShown = [&](const std::string& id) { return std::find(sm.shownIds.begin(), sm.shownIds.end(), id) != sm.shownIds.end(); };
  int nSel = 0;
  for (const MtaSeries& s : rep.series) nSel += isShown(s.id) ? 1 : 0;
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted("図に出すマイルストーン");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(u.m.s(240));
  const std::string pv = std::to_string(nSel) + " 本 / 全 " + std::to_string(rep.series.size()) + " 本";
  const bool selOpen = ImGui::BeginCombo("##mtaSel", pv.c_str());
  sm.selA = ImGui::GetItemRectMin();
  sm.selB = ImGui::GetItemRectMax();
  if (selOpen) {
    for (std::size_t i = 0; i < rep.series.size(); ++i) {
      const MtaSeries& s = rep.series[i];
      ImGui::PushID(static_cast<int>(i));
      const bool on = isShown(s.id);
      const std::string lb = std::string(on ? "[x] " : "[ ] ") + (s.warn ? "! " : "") + mtaLegendText(s) + (s.recorded == 0 ? "　（記録なし）" : "");
      if (ImGui::Selectable(lb.c_str(), on, ImGuiSelectableFlags_DontClosePopups)) {
        if (on) sm.shownIds.erase(std::remove(sm.shownIds.begin(), sm.shownIds.end(), s.id), sm.shownIds.end());
        else sm.shownIds.push_back(s.id);
      }
      ImGui::PopID();
    }
    ImGui::EndCombo();
  }
  std::vector<std::size_t> shown;
  for (std::size_t i = 0; i < rep.series.size(); ++i)
    if (isShown(rep.series[i].id)) shown.push_back(i);
  sm.shownN = static_cast<int>(shown.size());
  ImGui::Dummy(ImVec2(0, u.m.s(4)));
  mtaLegend(f, rep, shown, W);
  drawMtaChart(f, rep, shown, W);
  if (sm.gaps > 0) {
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + W);
    ImGui::TextDisabled("記録のない週が %d 週あります（点線の枠の帯。線は引かず、補いません）。", sm.gaps);
    ImGui::PopTextWrapPos();
  }
  if (rep.truncatedWeeks) ImGui::TextDisabled("古い週は省きました（最大 %d 週）。", kMtaMaxWeeks);
  ImGui::Dummy(ImVec2(0, u.m.s(8)));
  ImGui::Text("週ごとの予測日（すべてのマイルストーン。行を押すと詳細を開きます）");
  drawMtaTable(f, rep, W);
}

// 推移の節。担当者・ルート課題の選択（この節の中だけ。上の絞り込みや他の節には効かない）→ 集計 → 3 つの図
void drawTrendBody(Frame& f, Session& s, const Tree& T, const std::vector<ScheduleRow>& R, const std::vector<Issue>& I, Day base, float W) {
  UiState& u = f.u;
  UiState::StatsTrend& st = u.statsTrend;
  if (st.projectId != f.c.currentId()) {  // プロジェクトを切り替えたら選択を初期化する
    st = UiState::StatsTrend{};
    st.projectId = f.c.currentId();
  }
  if (!st.rootId.empty()) {  // 消えた・ルートでなくなった課題は外す
    const int r = T.find(st.rootId);
    if (r < 0 || T.parentOf[static_cast<std::size_t>(r)] >= 0 || T.kids[static_cast<std::size_t>(r)].empty()) st.rootId.clear();
  }
  sectionHead(f, "推移", "作成日・完了日・更新日から出しています（最後の図だけ、記録した件数から出します）", W);
  ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + W);
  ImGui::TextDisabled("週は月曜始まりです。営業日は休業日を除きます。CSV の Created（作成日）・Resolved（完了日）・Updated（更新日）を読みます。");
  ImGui::PopTextWrapPos();
  ImGui::Dummy(ImVec2(0, u.m.s(4)));

  // ---- 絞り込み（担当者・ルート課題）----
  {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("担当者");
    ImGui::SameLine();
    std::string pv = "全員";
    if (st.who == TrendFilter::Who::Unassigned) pv = "担当なし";
    else if (st.who == TrendFilter::Who::Name) pv = st.name;
    ImGui::SetNextItemWidth(u.m.s(180));
    const bool whoOpen = ImGui::BeginCombo("##trendWho", pv.c_str());
    st.whoA = ImGui::GetItemRectMin();
    st.whoB = ImGui::GetItemRectMax();
    if (whoOpen) {
      if (ImGui::Selectable("全員", st.who == TrendFilter::Who::All)) st.who = TrendFilter::Who::All;
      if (ImGui::Selectable("担当なし", st.who == TrendFilter::Who::Unassigned)) st.who = TrendFilter::Who::Unassigned;
      for (const std::string& a : s.assignees())
        if (ImGui::Selectable(a.c_str(), st.who == TrendFilter::Who::Name && st.name == a)) {
          st.who = TrendFilter::Who::Name;
          st.name = a;
        }
      ImGui::EndCombo();
    }
    ImGui::SameLine(0, u.m.s(16));
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("ルート課題");
    ImGui::SameLine();
    std::string rv = "すべて";
    if (st.standalone) rv = "親のない単独の課題";
    else if (!st.rootId.empty()) {
      const int r = T.find(st.rootId);
      if (r >= 0) rv = T.did[static_cast<std::size_t>(r)] + "  " + T.name[static_cast<std::size_t>(r)];
    }
    ImGui::SetNextItemWidth(u.m.s(260));
    const bool rootOpen = ImGui::BeginCombo("##trendRoot", rv.c_str());
    st.rootA = ImGui::GetItemRectMin();
    st.rootB = ImGui::GetItemRectMax();
    if (rootOpen) {
      if (ImGui::Selectable("すべて", !st.standalone && st.rootId.empty())) {
        st.standalone = false;
        st.rootId.clear();
      }
      bool anyAlone = false;
      for (int r : T.roots)
        if (T.kids[static_cast<std::size_t>(r)].empty() && T.kind[static_cast<std::size_t>(r)] != Kind::Ms) anyAlone = true;
      if (anyAlone && ImGui::Selectable("親のない単独の課題", st.standalone)) {
        st.standalone = true;
        st.rootId.clear();
      }
      for (int r : T.roots) {
        const std::size_t ri = static_cast<std::size_t>(r);
        if (T.kids[ri].empty()) continue;
        ImGui::PushID(r);
        if (ImGui::Selectable((T.did[ri] + "  " + T.name[ri]).c_str(), !st.standalone && st.rootId == T.ids[ri])) {
          st.standalone = false;
          st.rootId = T.ids[ri];
        }
        ImGui::PopID();
      }
      ImGui::EndCombo();
    }
  }
  ImGui::Dummy(ImVec2(0, u.m.s(6)));

  TrendFilter flt;
  flt.who = st.who;
  flt.name = st.name;
  if (st.standalone) flt.root = TrendFilter::kStandalone;
  else if (!st.rootId.empty()) flt.root = T.find(st.rootId);
  const Result<TrendReport> res = computeTrends(T, I, s.meta(), R, s.calendar(), base, flt);
  if (!res.ok()) {
    ImGui::TextUnformatted("推移を集計できません。");
    return;
  }
  const TrendReport& tr = res.value();
  st.drawn = true;
  st.items = tr.items;
  st.weeks = static_cast<int>(tr.burn.weeks.size());
  st.forecastOk = tr.burn.forecast.ok;
  st.leadN = tr.lead.n;
  st.agingN = tr.aging.n;
  if (tr.items == 0) {
    ImGui::TextUnformatted("この条件に当てはまる作業がありません。");
    return;
  }
  ImGui::TextDisabled("対象の作業 %d 件（マイルストーンを除く）", tr.items);

  // ---- バーンダウン／バーンアップ ----
  ImGui::Dummy(ImVec2(0, u.m.s(6)));
  ImGui::PushFont(u.fontBold);
  ImGui::TextUnformatted("バーンダウン／バーンアップ");
  ImGui::PopFont();
  drawBurn(f, tr, W);

  // ---- リードタイム ----
  ImGui::Dummy(ImVec2(0, u.m.s(14)));
  ImGui::PushFont(u.fontBold);
  ImGui::TextUnformatted("リードタイム（作成 → 完了の営業日）");
  ImGui::PopFont();
  if (tr.lead.n == 0) {
    ImGui::TextUnformatted("作成日と完了日の両方がある完了済みの作業がないので、リードタイムは出せません。");
  } else {
    ImGui::Text("完了 %d 件　中央値 %s 営業日　90 パーセンタイル %d 営業日　最長 %d 営業日", tr.lead.n, num(tr.lead.median).c_str(), tr.lead.p90,
                tr.lead.maxDays);
    const ImU32 cols[kTrendBuckets] = {u.t->accent, u.t->accent, u.t->accent, u.t->accent, u.t->accent};
    drawBuckets(f, tr.lead.hist, tr.lead.n, cols, false, W);
  }

  // ---- 滞留日数 ----
  ImGui::Dummy(ImVec2(0, u.m.s(14)));
  ImGui::PushFont(u.fontBold);
  ImGui::TextUnformatted("滞留日数（未完了で、最後に動いた日から基準日までの営業日）");
  ImGui::PopFont();
  if (tr.aging.n == 0) {
    ImGui::TextUnformatted("最後に動いた日のわかる未完了の作業がないので、滞留は出せません。");
  } else {
    ImGui::TextDisabled("最後に動いた日は、更新日（Updated）、なければ開始日、なければ作成日（Created）です。基準日より先の日付は 0 日です。");
    const ImU32 cols[kTrendBuckets] = {u.t->ok, u.t->ok, u.t->tight, u.t->tight, u.t->late};
    drawBuckets(f, tr.aging.hist, tr.aging.n, cols, true, W);
    ImGui::Dummy(ImVec2(0, u.m.s(8)));
    ImGui::Text("長く動いていない順（上位 %d 件。行を押すと詳細を開きます）", static_cast<int>(tr.aging.top.size()));
    drawAgingTop(f, tr, W);
  }

  // ---- 数えなかったもの ----
  const std::vector<std::string> notes = trendNotes(tr);
  if (!notes.empty()) {
    ImGui::Dummy(ImVec2(0, u.m.s(8)));
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + W);
    for (const std::string& n : notes) ImGui::TextDisabled("・%s", n.c_str());
    ImGui::PopTextWrapPos();
  }
}

// 推移の節: 日付から出す 3 つの図（担当者・ルート課題の選択つき）→ 記録した件数の累積フロー → マイルストーンの予測の推移
void drawTrends(Frame& f, Session& s, const Tree& T, const std::vector<ScheduleRow>& R, const std::vector<Issue>& I, Day base, const Result<MtaReport>& mta,
                float W) {
  drawTrendBody(f, s, T, R, I, base, W);
  drawCfd(f, s, base, W);
  drawMta(f, mta, T, W);
}
}  // namespace

void drawStatsView(Frame& f, ImVec2 size) {
  UiState& u = f.u;
  app::AppController& c = f.c;
  u.statsTrend.drawn = false;  // 推移を描かないフレーム（課題がない・別のビュー）では、前の結果を残さない
  u.statsTrend.weekCols.clear();
  u.statsTrend.agingRows.clear();
  u.statsPlan = UiState::StatsPlan{};
  u.statsCfd = UiState::StatsCfd{};  // 描かないフレームでは前の結果を残さない
  u.statsMta.resetDraw();
  Session* s = c.session();
  if (!s || !s->tree()) return;
  const Tree& T = *s->tree();
  const auto& R = *s->rows();
  const auto& I = s->viewIssues();
  const Day base = parseIso(s->settings().baseDate).value_or(0);
  const ProjectStats st = computeStats(T, I, R, base);
  // マイルストーンの予測の推移と遅れの警告（週の頭のスナップショットから。Issue #75）。プロジェクトを切り替えたら図の選択を初期化する
  if (u.statsMta.projectId != c.currentId()) {
    u.statsMta = UiState::StatsMta{};
    u.statsMta.projectId = c.currentId();
  }
  MtaOptions mtaOpt;
  mtaOpt.lateDays = c.prefs().mtaLateDays;
  const Result<MtaReport> mta = computeMta(T, I, R, s->calendar(), s->weeklyAuto(), base, s->baselineCompare(), mtaOpt);

  ImGui::BeginChild("##stats", size, ImGuiChildFlags_None);
  ImGui::SetCursorPos(ImVec2(u.m.s(24), u.m.s(16)));
  ImGui::BeginGroup();
  const float W = std::clamp(size.x - u.m.s(48), u.m.s(320), u.m.s(980));
  ImGui::PushFont(u.fontLarge);
  ImGui::TextUnformatted("プロジェクトの統計");
  ImGui::PopFont();
  ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + W);
  ImGui::TextDisabled("絞り込みに関係なく、プロジェクト全体の集計です（「推移」の節だけ、その中の選択欄で担当者・ルート課題を絞れます）。数える対象は作業（子を持たない課題）で、マイルストーンは別に数えます。基準日は %s です。",
                      fmtLong(base).c_str());
  ImGui::PopTextWrapPos();
  drawMtaBand(f, mta.ok() ? &mta.value() : nullptr, W);  // 遅れの警告は統計の一番上

  if (st.tasks == 0) {
    ImGui::Dummy(ImVec2(0, u.m.s(12)));
    ImGui::TextUnformatted("集計できる作業がありません。課題を追加すると、ここに進みと内訳が出ます。");
    // 作業が 0 件（マイルストーンだけ・全部削除）でも、計画との差（消えた課題など）は出す
    if (const BaselineCompare* bc0 = s->baselineCompare()) {
      drawPlanDiff(f, *bc0, T, I, W);
      u.statsPlan.drawn = true;
    }
    ImGui::EndGroup();
    ImGui::Dummy(ImVec2(0, u.m.s(40)));
    ImGui::EndChild();
    return;
  }

  // ---- 全体 ----
  sectionHead(f, "全体", nullptr, W);
  {
    const int nt = 6;
    const float gap = u.m.s(8);
    const float tw = (W - gap * (nt - 1)) / nt;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const std::string fin = st.hasFinish ? fmtShort(st.finish) : std::string("—");
    struct T6 {
      const char* l;
      std::string v;
      ImU32 col;
    } tl[] = {{"進み（期間ベース）", pct(st.progress()), u.t->ink},
              {"作業", std::to_string(st.done) + " / " + std::to_string(st.tasks), u.t->ink},
              {"遅れ", std::to_string(st.late), st.late ? u.t->late : u.t->ink},
              {"余裕小（10日以内）", std::to_string(st.tight), st.tight ? u.t->tight : u.t->ink},
              {"完了の見込み", fin, u.t->ink},
              {"担当なし", std::to_string(st.unassigned), st.unassigned ? u.t->tight : u.t->ink}};
    for (int i = 0; i < nt; ++i) {
      ImGui::SetCursorScreenPos(ImVec2(p.x + static_cast<float>(i) * (tw + gap), p.y));
      tile(f, tl[i].l, tl[i].v, tl[i].col, tw);
    }
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + u.m.s(64) + u.m.s(10)));
    const ImVec2 q = ImGui::GetCursorScreenPos();
    stackedBar(u, q, W, u.m.s(16),
               {{static_cast<double>(st.done), u.t->ok}, {static_cast<double>(st.prog), u.t->accent},
                {static_cast<double>(st.todo), u.t->line2}});
    ImGui::Dummy(ImVec2(W, u.m.s(16)));
    ImGui::Dummy(ImVec2(0, u.m.s(4)));
    legend(f, {{"完了", u.t->ok, st.done}, {"進行中", u.t->accent, st.prog}, {"未着手", u.t->line2, st.todo}});
    std::string extra = "全体 " + num(st.weeks) + " 週のうち " + num(st.weeksDone) + " 週が完了";
    if (st.milestones > 0)
      extra += "　マイルストーン " + std::to_string(st.msDone) + " / " + std::to_string(st.milestones) + " 達成" +
               (st.msLate > 0 ? "（遅れ " + std::to_string(st.msLate) + "）" : "");
    if (st.noDeadline > 0) extra += "　期限なしの未完了 " + std::to_string(st.noDeadline) + " 件";
    ImGui::TextDisabled("%s", extra.c_str());
  }

  // ---- 余裕の分布 ----
  sectionHead(f, "余裕の分布", "未完了の作業", W);
  {
    static const char* const labels[kFloatBuckets] = {"遅れ", "0〜5日", "6〜10日", "11〜20日", "21日以上"};
    const ImU32 cols[kFloatBuckets] = {u.t->late, u.t->tight, u.t->tight, u.t->ok, u.t->ok};
    int mx = 1;
    for (int h : st.floatHist) mx = std::max(mx, h);
    const float lw = u.m.s(90), cw = u.m.s(40);
    const float bw = std::max(W - lw - cw - u.m.s(8), u.m.s(80));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (int k = 0; k < kFloatBuckets; ++k) {
      const ImVec2 p = ImGui::GetCursorScreenPos();
      const float h = u.m.s(22);
      const float ty = p.y + (h - ImGui::GetTextLineHeight()) / 2;
      dl->AddText(ImVec2(p.x + u.m.s(6), ty), u.t->ink, labels[k]);
      const float len = bw * static_cast<float>(st.floatHist[k]) / static_cast<float>(mx);
      if (len > 0.5f) dl->AddRectFilled(ImVec2(p.x + lw, p.y + u.m.s(4)), ImVec2(p.x + lw + len, p.y + h - u.m.s(4)), cols[k], u.m.s(3));
      dl->AddText(ImVec2(p.x + lw + len + u.m.s(8), ty), u.t->muted, std::to_string(st.floatHist[k]).c_str());
      ImGui::Dummy(ImVec2(W, h));
    }
  }

  // ---- 計画との差（表示するベースラインがあるとき） ----
  u.statsPlan = UiState::StatsPlan{};
  if (const BaselineCompare* bc = s->baselineCompare()) {
    drawPlanDiff(f, *bc, T, I, W);
    u.statsPlan.drawn = true;
  }

  // ---- 週ごとの完了 ----
  sectionHead(f, "週ごとの完了", "過去は完了日（Resolved）、先は最早の完了日", W);
  {
    const auto& wk = st.weeksTimeline;
    int mx = 1;
    for (const auto& b : wk) mx = std::max({mx, b.actual, b.forecast});
    const float chartH = u.m.s(110);
    const float axisH = u.m.s(20);
    const float bw = std::clamp(W / static_cast<float>(wk.size()), u.m.s(10), u.m.s(48));
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddLine(ImVec2(p.x, p.y + chartH), ImVec2(p.x + bw * static_cast<float>(wk.size()), p.y + chartH), u.t->line);
    const int every = bw < u.m.s(26) ? (bw < u.m.s(16) ? 4 : 2) : 1;
    for (std::size_t k = 0; k < wk.size(); ++k) {
      const float x = p.x + bw * static_cast<float>(k);
      const bool now = static_cast<int>(k) == st.nowIndex;
      if (now) dl->AddRectFilled(ImVec2(x, p.y), ImVec2(x + bw, p.y + chartH), u.t->selbg);
      const int n = wk[k].actual + wk[k].forecast;
      if (n > 0) {
        const float hh = (chartH - u.m.s(18)) * static_cast<float>(n) / static_cast<float>(mx);
        dl->AddRectFilled(ImVec2(x + bw * 0.18f, p.y + chartH - hh), ImVec2(x + bw * 0.82f, p.y + chartH),
                          wk[k].actual > 0 ? u.t->ok : u.t->accent, u.m.s(2));
        const std::string ns = std::to_string(n);
        const float tw = ImGui::CalcTextSize(ns.c_str()).x;
        dl->AddText(ImVec2(x + (bw - tw) / 2, p.y + chartH - hh - ImGui::GetTextLineHeight() - u.m.s(1)), u.t->muted, ns.c_str());
      }
      if (static_cast<int>(k) % every == 0 || now) {
        const Ymd ymd = civilFromDays(wk[k].start);
        const std::string lb = std::to_string(ymd.m) + "/" + std::to_string(ymd.d);
        const float tw = ImGui::CalcTextSize(lb.c_str()).x;
        dl->AddText(ImVec2(x + (bw - tw) / 2, p.y + chartH + u.m.s(3)), now ? u.t->ink : u.t->muted, lb.c_str());
      }
      const ImVec2 a(x, p.y), b(x + bw, p.y + chartH + axisH);
      if (ImGui::IsMouseHoveringRect(a, b) && ImGui::IsWindowHovered()) {
        ImGui::SetTooltip("%s からの週\n%s %d 件", fmtMd(wk[k].start).c_str(), static_cast<int>(k) < st.nowIndex ? "完了" : "完了の見込み",
                          n);
      }
    }
    ImGui::Dummy(ImVec2(W, chartH + axisH));
    legend(f, {{"完了した数", u.t->ok, -1}, {"完了の見込み", u.t->accent, -1}});
  }

  // ---- 担当別 ----
  sectionHead(f, "担当別", "行を押すと、その担当者の作業をリストで見られます", W);
  {
    const float nameW = u.m.s(140), barW = std::clamp(W * 0.3f, u.m.s(100), u.m.s(300));
    for (const GroupStat& g : st.byAssignee) {
      if (groupRow(f, g, g.name.empty() ? std::string("（担当なし）") : g.name, groupText(g), nameW, barW, W, true)) openListFor(f, g.name);
    }
  }

  // ---- 担当者別の負荷（週ごと）と完了 ----
  const LoadReport lr = computeLoad(T, I, R, s->calendar(), base, s->settings().resource);
  u.statsLoadCells.clear();
  u.statsLoadRows = static_cast<int>(lr.load.size());
  u.statsDoneRows = static_cast<int>(lr.done.size());
  sectionHead(f, "担当者別の負荷（週ごと）", "未完了の作業を、最早の開始〜完了の営業日に均等に広げた量です。押すと、その担当者の作業をリストで見られます", W);
  drawLoadHeatmap(f, lr, T, R, W);
  sectionHead(f, "担当者別の完了（過去 8 週）", "完了日（Resolved）の週ごと。順位ではなく、誰が何を終えたかの記録です", W);
  drawDoneByAssignee(f, lr, W);

  // ---- ルート別 ----
  sectionHead(f, "ルート課題別", "Epic など最上位の課題ごとの進み", W);
  {
    const float nameW = std::clamp(W * 0.32f, u.m.s(140), u.m.s(320)), barW = std::clamp(W * 0.25f, u.m.s(90), u.m.s(260));
    for (const GroupStat& g : st.byRoot) {
      std::string d = pct(g.progress()) + "・";
      d += g.hasFinish ? "見込み " + fmtShort(g.finish) + "・余裕 " + std::to_string(g.minFloat) + "日" : std::string("すべて完了");
      if (g.nLate > 0) d += "・遅れ " + std::to_string(g.nLate);
      groupRow(f, g, g.name, d, nameW, barW, W, false);
    }
  }

  // ---- 優先度別 ----
  sectionHead(f, "優先度別", nullptr, W);
  {
    static const char* const names[5] = {"Highest", "High", "Medium", "Low", "Lowest"};
    const float nameW = u.m.s(100), barW = std::clamp(W * 0.3f, u.m.s(100), u.m.s(300));
    for (int k = 0; k < 5; ++k) {
      GroupStat g;
      g.nDone = st.prioDone[k];
      g.nTodo = st.prioOpen[k];
      g.n = g.nDone + g.nTodo;
      if (g.n == 0) continue;
      groupRow(f, g, names[k], "完了 " + std::to_string(g.nDone) + "／未完了 " + std::to_string(g.nTodo), nameW, barW, W, false);
    }
  }

  // ---- 推移（作成日・完了日・更新日から。担当者・ルート課題の選択はこの節の中）----
  drawTrends(f, *s, T, R, I, base, mta, W);

  ImGui::EndGroup();
  ImGui::Dummy(ImVec2(0, u.m.s(40)));
  ImGui::EndChild();
}

}  // namespace roadmap::ui
