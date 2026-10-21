// 統計ビュー: 進み・状況の内訳・余裕の分布・週ごとの完了・担当別・ルート別・優先度別（集計は core/stats）
#include <algorithm>
#include <cstdio>
#include <string>

#include "roadmap/load.hpp"
#include "roadmap/stats.hpp"
#include "roadmap/text.hpp"
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
}  // namespace

void drawStatsView(Frame& f, ImVec2 size) {
  UiState& u = f.u;
  app::AppController& c = f.c;
  Session* s = c.session();
  if (!s || !s->tree()) return;
  const Tree& T = *s->tree();
  const auto& R = *s->rows();
  const auto& I = s->viewIssues();
  const Day base = parseIso(s->settings().baseDate).value_or(0);
  const ProjectStats st = computeStats(T, I, R, base);

  ImGui::BeginChild("##stats", size, ImGuiChildFlags_None);
  ImGui::SetCursorPos(ImVec2(u.m.s(24), u.m.s(16)));
  ImGui::BeginGroup();
  const float W = std::clamp(size.x - u.m.s(48), u.m.s(320), u.m.s(980));
  ImGui::PushFont(u.fontLarge);
  ImGui::TextUnformatted("プロジェクトの統計");
  ImGui::PopFont();
  ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + W);
  ImGui::TextDisabled("絞り込みに関係なく、プロジェクト全体の集計です。数える対象は作業（子を持たない課題）で、マイルストーンは別に数えます。基準日は %s です。",
                      fmtLong(base).c_str());
  ImGui::PopTextWrapPos();

  if (st.tasks == 0) {
    ImGui::Dummy(ImVec2(0, u.m.s(12)));
    ImGui::TextUnformatted("集計できる作業がありません。課題を追加すると、ここに進みと内訳が出ます。");
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

  ImGui::EndGroup();
  ImGui::Dummy(ImVec2(0, u.m.s(40)));
  ImGui::EndChild();
}

}  // namespace roadmap::ui
