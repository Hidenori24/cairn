// 統計ビュー: 進み・状況の内訳・余裕の分布・週ごとの完了・担当別・ルート別・優先度別（集計は core/stats）
#include <algorithm>
#include <cstdio>
#include <string>

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
      if (groupRow(f, g, g.name.empty() ? std::string("（担当なし）") : g.name, groupText(g), nameW, barW, W, true)) {
        auto& flt = c.filter();
        flt.asg = g.name.empty() ? AsgFilter::Unassigned : AsgFilter::Name;
        flt.asgName = g.name;
        c.setView(app::View::List);
      }
    }
  }

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
