// ギャップ（ゴール）ビュー（SPEC 10.7.3）とゴールのポップオーバー（10.7.2）。JS版 views/Goals.jsx と同じ
#include <algorithm>
#include <map>
#include <set>

#include "roadmap/text.hpp"
#include "ui/icons.hpp"
#include "ui/internal.hpp"

namespace roadmap::ui {

namespace {
// ゴールから、未完了の先行のうち最早完了が最も遅いもの（同日なら余裕が小さいもの）を遡った一本道
std::vector<int> drivingChain(const Tree& T, const std::vector<ScheduleRow>& R, int goal) {
  std::vector<int> chain;
  std::set<int> seen;
  int cur = goal;
  while (cur >= 0) {
    std::vector<int> ps;
    for (int p : T.preds[cur])
      if (T.cat[p] != Cat::Done) ps.push_back(p);
    if (ps.empty()) break;
    std::stable_sort(ps.begin(), ps.end(), [&](int a, int b) {
      if (R[b].asapFinish != R[a].asapFinish) return R[b].asapFinish < R[a].asapFinish;
      return R[a].floatDays < R[b].floatDays;
    });
    cur = ps[0];
    if (!seen.insert(cur).second) break;
    chain.insert(chain.begin(), cur);
  }
  return chain;
}

std::vector<int> allBlockers(const Tree& T, int goal) {
  std::set<int> seen;
  std::vector<int> st{goal}, out;
  while (!st.empty()) {
    const int x = st.back();
    st.pop_back();
    for (int p : T.preds[x])
      if (T.cat[p] != Cat::Done && seen.insert(p).second) {
        out.push_back(p);
        st.push_back(p);
      }
  }
  return out;
}

void stepRow(Frame& f, int id, const std::string& n) {
  UiState& u = f.u;
  Session* s = f.c.session();
  const Tree& T = *s->tree();
  const ScheduleRow& r = (*s->rows())[id];
  const Issue& o = s->issues()[id];
  ImGui::PushID(id);
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const float w = ImGui::GetContentRegionAvail().x, h = u.m.s(30);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  if (r.floatDays < 0 && !r.done) dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), u.t->lateBg, u.m.s(4));
  ImGui::SetNextItemAllowOverlap();
  if (ImGui::InvisibleButton("##step", ImVec2(w, h))) openDetail(f, T.ids[id], 0);
  if (ImGui::IsItemHovered()) dl->AddRect(p, ImVec2(p.x + w, p.y + h), u.t->line2, u.m.s(4));
  tip("詳細を開く");
  const float cy = p.y + (h - ImGui::GetTextLineHeight()) / 2;
  dl->AddText(ImVec2(p.x + u.m.s(6), cy), u.t->muted, n.c_str());
  ImGui::SetCursorScreenPos(ImVec2(p.x + u.m.s(26), p.y + (h - u.m.s(16)) / 2));
  typeIcon(u, o.type);
  ImGui::PushFont(u.fontMono);
  dl->AddText(ImVec2(p.x + u.m.s(48), cy), u.t->muted, T.did[id].c_str());
  ImGui::PopFont();
  const float nameX = p.x + u.m.s(110);
  const float colsW = u.m.s(470);
  dl->AddText(ImVec2(nameX, cy), u.t->ink, ellipsize(T.name[id], std::max(u.m.s(60), w - colsW - u.m.s(110))).c_str());
  float x = p.x + w - colsW;
  dl->AddText(ImVec2(x, cy), o.assignee.empty() ? u.t->muted : u.t->ink, o.assignee.empty() ? "担当未定" : ellipsize(o.assignee, u.m.s(80)).c_str());
  x += u.m.s(90);
  std::string when = r.asapStart == r.asapFinish ? fmtShort(r.asapFinish) : fmtShort(r.asapStart) + "〜" + fmtShort(r.asapFinish);
  if (r.leaf && T.kind[id] != Kind::Ms) when += "（" + csv::fmtNum(r.weeks) + "週）";
  ImGui::PushFont(u.fontMono);
  dl->AddText(ImVec2(x, cy), u.t->ink, when.c_str());
  x += u.m.s(130);
  dl->AddText(ImVec2(x, cy), u.t->muted, T.ownDl[id] ? ("期限 " + fmtShort(*T.ownDl[id])).c_str() : "期限なし");
  ImGui::PopFont();
  x += u.m.s(90);
  {
    const auto rc = riskColor(u, riskOf(r.floatDays, r.done));
    const std::string fl = r.done ? "完了" : r.floatDays < 0 ? "−" + std::to_string(-r.floatDays) + "営業日" : "余裕 " + std::to_string(r.floatDays) + "日";
    dl->AddText(ImVec2(x, cy), rc.fg, fl.c_str());
  }
  x += u.m.s(90);
  ImGui::SetCursorScreenPos(ImVec2(x, p.y + (h - ImGui::GetTextLineHeight() - u.m.s(4)) / 2));
  statusPill(u, "st", o.status, false);
  ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h + u.m.s(2)));
  ImGui::Dummy(ImVec2(w, 0));
  ImGui::PopID();
}

void gapTrack(Frame& f, int goal) {
  UiState& u = f.u;
  Session* s = f.c.session();
  const Tree& T = *s->tree();
  const ScheduleRow& r = (*s->rows())[goal];
  const OptDay dl0 = T.dl[goal];
  const Day fc = r.asapFinish, b = s->baseDay();
  const Day lo = std::min(b, dl0 ? *dl0 : b), hi = std::max(dl0 ? *dl0 : fc, fc) + 4;
  const float span = static_cast<float>(std::max(hi - lo, 1));
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const float w = ImGui::GetContentRegionAvail().x - u.m.s(40), h = u.m.s(46);
  ImGui::Dummy(ImVec2(w, h));
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const float x0 = p.x + u.m.s(20);
  const auto X = [&](Day d) { return x0 + static_cast<float>(d - lo) / span * w; };
  const float ly = p.y + u.m.s(18);
  dl->AddLine(ImVec2(x0, ly), ImVec2(x0 + w, ly), u.t->line2, 2.f);
  const bool late = dl0 && fc > *dl0;
  if (dl0 && fc != *dl0)
    dl->AddRectFilled(ImVec2(X(std::min(*dl0, fc)), ly - u.m.s(4)), ImVec2(X(std::max(*dl0, fc)), ly + u.m.s(4)),
                      withAlpha(late ? u.t->late : u.t->ok, 0.6f), u.m.s(2));
  const auto mark = [&](Day d, ImU32 col, const std::string& label, float dy) {
    const float x = X(d);
    dl->AddCircleFilled(ImVec2(x, ly), u.m.s(5), col);
    const ImVec2 ts = ImGui::CalcTextSize(label.c_str());
    dl->AddText(ImVec2(x - ts.x / 2, ly + dy), col, label.c_str());
  };
  mark(b, u.t->accent, "基準日", u.m.s(8));
  if (dl0) mark(*dl0, u.t->ink, "期限 " + fmtShort(*dl0), -u.m.s(22));
  mark(fc, !dl0 ? u.t->muted : late ? u.t->late : u.t->ok, "見込み " + fmtShort(fc), u.m.s(8));
}

std::map<int, bool> gOpenOthers;

void goalCard(Frame& f, int id) {
  UiState& u = f.u;
  Session* s = f.c.session();
  const Tree& T = *s->tree();
  const auto& R = *s->rows();
  const ScheduleRow& r = R[id];
  const Issue& o = s->issues()[id];
  const OptDay dl0 = T.dl[id];
  const Day fc = r.asapFinish;
  const auto chain = drivingChain(T, R, id);
  const auto blockers = allBlockers(T, id);
  std::set<int> onChain(chain.begin(), chain.end());
  std::vector<int> others;
  for (int x : blockers)
    if (!onChain.count(x)) others.push_back(x);
  std::stable_sort(others.begin(), others.end(), [&](int a, int b) { return R[a].asapFinish < R[b].asapFinish; });
  const int gap = dl0 ? s->calendar().floatDays(std::min(*dl0, fc), std::max(*dl0, fc)) : 0;
  const bool late = dl0 && fc > *dl0;
  int lateSteps = 0;
  for (int x : chain) lateSteps += R[x].floatDays < 0;
  const bool top = prioRank(o.prio) == 0;

  ImGui::PushID(id);
  ImGui::PushStyleColor(ImGuiCol_ChildBg, v4(u.t->surface));
  ImGui::PushStyleColor(ImGuiCol_Border, v4(late ? u.t->late : top ? u.t->accent : u.t->line));
  ImGui::BeginChild("##goal", ImVec2(0, 0), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY);
  ImGui::PopStyleColor(2);
  typeIcon(u, o.type);
  ImGui::SameLine();
  ImGui::PushFont(u.fontMono);
  ImGui::TextDisabled("%s", T.did[id].c_str());
  ImGui::PopFont();
  ImGui::SameLine();
  ImGui::PushFont(u.fontLarge);
  ImGui::TextUnformatted(T.name[id].c_str());
  ImGui::PopFont();
  if (top) {
    ImGui::SameLine();
    chip(u, "最重要", u.t->accent, u.t->accentBg);
  }
  ImGui::BeginGroup();
  ImGui::TextDisabled("期限（理想）");
  ImGui::PushFont(u.fontMono);
  ImGui::TextUnformatted(dl0 ? fmtLong(*dl0).c_str() : "未設定");
  ImGui::PopFont();
  ImGui::EndGroup();
  ImGui::SameLine(0, u.m.s(30));
  ImGui::BeginGroup();
  ImGui::TextDisabled("見込み（現状）");
  ImGui::PushFont(u.fontMono);
  ImGui::TextUnformatted(fmtLong(fc).c_str());
  ImGui::PopFont();
  ImGui::EndGroup();
  ImGui::SameLine(0, u.m.s(30));
  ImGui::BeginGroup();
  std::string head, sub;
  ImU32 col = u.t->ok;
  if (!dl0) {
    head = "期限が未設定";
    sub = "日付を入れると、ここに差が出ます";
    col = u.t->muted;
  } else if (late) {
    head = std::to_string(gap) + "営業日 遅れる見込み";
    sub = lateSteps ? "遅れの原因: 下の連なり（" + std::to_string(lateSteps) + "件が期限超過）" : "下の連なりの積み上げで超過";
    col = u.t->late;
  } else {
    head = gap == 0 ? "余裕なし（ちょうど間に合う）" : std::to_string(gap) + "営業日の余裕";
    sub = lateSteps ? "ゴールには間に合うが、途中に期限超過が " + std::to_string(lateSteps) + " 件"
                    : "未完了の前提 " + std::to_string(blockers.size()) + " 件";
    col = r.floatDays <= 10 ? u.t->tight : u.t->ok;
  }
  ImGui::PushFont(u.fontBold);
  ImGui::TextColored(v4(col), "%s", head.c_str());
  ImGui::PopFont();
  ImGui::TextDisabled("%s", sub.c_str());
  ImGui::EndGroup();
  gapTrack(f, id);
  if (chain.empty()) {
    ImGui::TextDisabled("このゴールの前提に、未完了のタスクはありません。");
  } else {
    ImGui::TextUnformatted("いま日程を決めている連なり");
    ImGui::SameLine();
    ImGui::TextDisabled("（上から順にクリアしていく。1つ遅れるとゴールが遅れる）");
    for (std::size_t i = 0; i < chain.size(); ++i) stepRow(f, chain[i], std::to_string(i + 1));
    ImGui::TextDisabled("◎");
    ImGui::SameLine();
    ImGui::TextUnformatted((T.did[id] + "  " + T.name[id]).c_str());
  }
  if (!others.empty()) {
    bool& open = gOpenOthers[id];
    const std::string l = std::string(open ? "▼ " : "▶ ") + "並行して必要な未完了の前提 " + std::to_string(others.size()) + " 件";
    if (ImGui::Selectable(l.c_str(), false)) open = !open;
    if (open)
      for (int x : others) stepRow(f, x, "·");
  }
  ImGui::EndChild();
  ImGui::PopID();
  ImGui::Spacing();
}
}  // namespace

void drawGoalsView(Frame& f, ImVec2 size) {
  UiState& u = f.u;
  Session* s = f.c.session();
  if (!s->tree()) return;
  const Tree& T = *s->tree();
  const auto& R = *s->rows();
  const auto& I = s->issues();
  ImGui::BeginChild("##goals", size, ImGuiChildFlags_None);
  ImGui::SetCursorPos(ImVec2(u.m.s(24), u.m.s(16)));
  ImGui::BeginGroup();
  ImGui::PushItemWidth(-u.m.s(24));
  std::vector<int> all;
  for (int id : T.leafOrder)
    if (isGoal(T, I, id)) all.push_back(id);
  if (all.empty()) {
    ImGui::PushFont(u.fontLarge);
    ImGui::TextUnformatted("ゴール（マイルストーン）がありません");
    ImGui::PopFont();
    ImGui::TextWrapped("課題の種別を Milestone にすると、ここに期限と見込みの差、律速になっている連なりが出ます。");
  } else {
    std::vector<int> open, done;
    for (int id : all) (T.cat[id] == Cat::Done ? done : open).push_back(id);
    std::stable_sort(open.begin(), open.end(), [&](int a, int b) {
      const int pa = prioRank(I[a].prio), pb = prioRank(I[b].prio);
      if (pa != pb) return pa < pb;
      if (!T.dl[a] != !T.dl[b]) return static_cast<bool>(T.dl[a]);
      const Day da = T.dl[a] ? *T.dl[a] : 0, db = T.dl[b] ? *T.dl[b] : 0;
      if (da != db) return da < db;
      return R[a].asapFinish < R[b].asapFinish;
    });
    int nLate = 0, nNone = 0;
    for (int id : open) {
      if (T.dl[id] && R[id].asapFinish > *T.dl[id]) ++nLate;
      if (!T.dl[id]) ++nNone;
    }
    ImGui::PushFont(u.fontLarge);
    ImGui::TextUnformatted("ゴールと現状のギャップ");
    ImGui::PopFont();
    ImGui::PushTextWrapPos(size.x - u.m.s(40));
    ImGui::TextDisabled("期限＝ゴールに設定した日付（理想）、見込み＝いまの前提と期間で最短に終わる日（現状）。優先度が高いゴール、次に期限が近いゴールの順です。");
    ImGui::PopTextWrapPos();
    const auto stat = [&](int n, const char* l, ImU32 col) {
      ImGui::PushFont(u.fontBold);
      ImGui::TextColored(v4(col), "%d", n);
      ImGui::PopFont();
      ImGui::SameLine(0, u.m.s(4));
      ImGui::TextUnformatted(l);
      ImGui::SameLine(0, u.m.s(20));
    };
    stat(static_cast<int>(open.size()), "未達のゴール", u.t->ink);
    stat(nLate, "期限に間に合わない", nLate ? u.t->late : u.t->ink);
    stat(nNone, "期限が未設定", nNone ? u.t->tight : u.t->ink);
    stat(static_cast<int>(done.size()), "達成済み", u.t->ink);
    ImGui::NewLine();
    ImGui::Spacing();
    for (int id : open) goalCard(f, id);
    if (!done.empty()) {
      std::vector<std::string> names;
      for (int id : done) names.push_back(T.did[id] + " " + T.name[id]);
      ImGui::PushTextWrapPos(size.x - u.m.s(40));
      ImGui::TextDisabled("達成済み: %s", text::join(names, " ／ ").c_str());
      ImGui::PopTextWrapPos();
    }
  }
  ImGui::PopItemWidth();
  ImGui::EndGroup();
  ImGui::Dummy(ImVec2(0, u.m.s(40)));
  ImGui::EndChild();
}

void drawGoalPopover(Frame& f) {
  app::AppController& c = f.c;
  UiState& u = f.u;
  Session* s = c.session();
  ImGui::SetNextWindowSize(ImVec2(u.m.s(380), 0));
  if (!ImGui::BeginPopup("##goalpop")) return;
  const Tree& T = *s->tree();
  const auto& R = *s->rows();
  const auto rootIdx = rootIndexMap(T, s->issues());
  ImGui::TextUnformatted("ルート課題ごとの余裕");
  ImGui::Separator();
  for (int r : T.roots) {
    if (T.kids[r].empty() && !isEpic(s->issues()[r])) continue;
    const std::string& id = T.ids[r];
    ImGui::PushID(id.c_str());
    bool show = !c.uiBool("hide", id.c_str(), false);
    if (ImGui::Checkbox("##show", &show)) c.setUiSub("hide", id, !show);
    tip("ガント・ボード・リストでこのルートを出す");
    ImGui::SameLine();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + u.m.s(6), p.y + ImGui::GetTextLineHeight() / 2 + u.m.s(3)), u.m.s(5),
                                                u.t->c[rootIdx[r]]);
    ImGui::Dummy(ImVec2(u.m.s(14), 1));
    ImGui::SameLine();
    if (ImGui::Selectable(ellipsize(T.name[r], u.m.s(150)).c_str(), c.isSelected(id), 0, ImVec2(u.m.s(150), 0))) c.select(id);
    ImGui::SameLine();
    ImGui::TextDisabled("%d/%d", R[r].nDone, R[r].n);
    ImGui::SameLine();
    {
      const ScheduleRow& row = R[r];
      const Risk risk = riskOf(row.floatDays, row.done);
      const auto rc = riskColor(u, risk);
      chip(u, row.done ? "完了" : floatText(row), rc.fg, rc.bg);
    }
    if (T.ownDl[r]) {
      ImGui::SameLine();
      ImGui::TextDisabled("〜%s", fmtShort(*T.ownDl[r]).c_str());
    }
    ImGui::PopID();
  }
  ImGui::EndPopup();
}

}  // namespace roadmap::ui
