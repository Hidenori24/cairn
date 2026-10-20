// ガント（SPEC 10.5）。1つのスクロール領域に、左の表（横スクロールで固定）とタイムライン（見出しは縦スクロールで固定）を描く。
// 見える範囲の行・矢印だけを描く（仮想化）。
#include <algorithm>
#include <cmath>
#include <map>
#include <set>

#include "roadmap/text.hpp"
#include "ui/icons.hpp"
#include "ui/internal.hpp"

namespace roadmap::ui {

namespace {

struct ColW {
  float name = 300, asg = 84, status = 92, dur = 52, flt = 62;
  bool showAsg = true, showStatus = true, showDur = true, showFloat = true;
};

ColW readCols(const app::AppController& c) {
  ColW w;
  w.name = static_cast<float>(c.uiInt("nameW", 300));
  const Session* s = c.session();
  const Json& ui = s->settings().ui;
  if (ui.contains("colW") && ui["colW"].is_object()) {
    const auto& cw = ui["colW"];
    const auto g = [&](const char* k, float& v) {
      if (cw.contains(k) && cw[k].is_number()) v = cw[k].get<float>();
    };
    g("asg", w.asg);
    g("status", w.status);
    g("dur", w.dur);
    g("float", w.flt);
  }
  w.showAsg = c.uiBool("cols", "asg", true);
  w.showStatus = c.uiBool("cols", "status", true);
  w.showDur = c.uiBool("cols", "dur", true);
  w.showFloat = c.uiBool("cols", "float", true);
  w.name = std::max(160.f, w.name);
  w.asg = std::max(48.f, w.asg);
  w.status = std::max(64.f, w.status);
  w.dur = std::max(40.f, w.dur);
  w.flt = std::max(48.f, w.flt);
  return w;
}

struct Axis {
  Day rs = 0, hi = 0;
  float px = 6;
  float left = 0;  // タイムラインの左端（スクリーン座標、スクロール込み）
  float x(Day d) const { return left + static_cast<float>(d - rs) * px; }
};

void hatch(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 col, float step) {
  dl->PushClipRect(a, b, true);
  for (float x = a.x - (b.y - a.y); x < b.x; x += step) dl->AddLine(ImVec2(x, b.y), ImVec2(x + (b.y - a.y), a.y), col, 1.0f);
  dl->PopClipRect();
}

void dashedRect(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 col, float dash) {
  const auto seg = [&](ImVec2 p, ImVec2 q) {
    const float len = std::hypot(q.x - p.x, q.y - p.y);
    for (float t = 0; t < len; t += dash * 2) {
      const float t2 = std::min(len, t + dash);
      dl->AddLine(ImVec2(p.x + (q.x - p.x) * t / len, p.y + (q.y - p.y) * t / len),
                  ImVec2(p.x + (q.x - p.x) * t2 / len, p.y + (q.y - p.y) * t2 / len), col, 1.5f);
    }
  };
  seg(a, ImVec2(b.x, a.y));
  seg(ImVec2(b.x, a.y), b);
  seg(b, ImVec2(a.x, b.y));
  seg(ImVec2(a.x, b.y), a);
}

void diamond(ImDrawList* dl, ImVec2 c, float r, ImU32 fill, ImU32 border) {
  const ImVec2 pts[4] = {ImVec2(c.x, c.y - r), ImVec2(c.x + r, c.y), ImVec2(c.x, c.y + r), ImVec2(c.x - r, c.y)};
  if (fill) dl->AddConvexPolyFilled(pts, 4, fill);
  if (border) dl->AddPolyline(pts, 4, border, ImDrawFlags_Closed, 1.5f);
}

std::vector<std::string> tipLines(const Session& s, int id) {
  const Tree& T = *s.tree();
  const ScheduleRow& r = (*s.rows())[id];
  const Issue& o = s.viewIssues()[id];
  std::vector<std::string> L;
  L.push_back(T.did[id] + "  " + T.name[id]);
  L.push_back("計画 " + fmtLong(r.asapStart) + " 〜 " + fmtLong(r.asapFinish) + (r.leaf ? "（" + csv::fmtNum(r.weeks) + "週）" : ""));
  if (!r.done) L.push_back("最遅 " + fmtLong(r.alapStart) + " 〜 " + fmtLong(r.alapFinish));
  if (r.done) L.push_back("完了");
  else if (r.floatDays < 0) L.push_back("余裕 −" + text::jsNumber(std::max(1.0, text::jsRound(std::abs(r.floatDays) / 5.0))) + "週（遅れ）");
  else L.push_back("余裕 " + std::to_string(r.floatDays) + "営業日");
  if (T.ownDl[id]) L.push_back("期限 " + fmtLong(*T.ownDl[id]));
  if (!o.assignee.empty()) L.push_back("担当 " + o.assignee);
  if (r.waited) L.push_back("担当者の別作業待ち");
  return L;
}

void showTip(const std::vector<std::string>& lines) {
  ImGui::BeginTooltip();
  for (std::size_t i = 0; i < lines.size(); ++i) {
    if (i == 0) ImGui::TextUnformatted(lines[0].c_str());
    else ImGui::TextDisabled("%s", lines[i].c_str());
  }
  ImGui::EndTooltip();
}

int workdaysIn(const Calendar& cal, Day a, Day b) {
  int n = 0;
  for (Day d = a; d <= b; ++d)
    if (cal.isWork(d)) ++n;
  return n;
}

}  // namespace

void drawGanttView(Frame& f, ImVec2 size) {
  app::AppController& c = f.c;
  UiState& u = f.u;
  Session* s = c.session();
  if (!s->tree()) return;
  const Tree& T = *s->tree();
  const auto& R = *s->rows();
  const auto& I = s->viewIssues();
  const Calendar& cal = s->calendar();
  const float RH = u.m.rowH();
  const std::string zoom = c.uiStr("zoom", "week");
  const float headH = (zoom == "day" || zoom == "week") ? u.m.s(46) : u.m.s(30);
  const std::string colorBy = c.uiStr("colorBy", "root");

  // 列
  ColW cw = readCols(c);
  if (u.colDrag.active) {
    const float nw = std::max(u.colDrag.col == "name" ? 160.f : 40.f,
                              std::round(u.colDrag.startW + (ImGui::GetMousePos().x - u.colDrag.startX) / u.m.scale));
    if (u.colDrag.col == "name") cw.name = nw;
    else if (u.colDrag.col == "asg") cw.asg = nw;
    else if (u.colDrag.col == "status") cw.status = nw;
    else if (u.colDrag.col == "dur") cw.dur = nw;
    else if (u.colDrag.col == "float") cw.flt = nw;
  }
  std::vector<ColDef> cols = {{Col::Grip, u.m.s(18)}, {Col::Name, u.m.s(cw.name)}};
  std::vector<std::pair<std::string, float>> resizable = {{"name", cw.name}};
  if (cw.showAsg) cols.push_back({Col::Asg, u.m.s(cw.asg)}), resizable.emplace_back("asg", cw.asg);
  if (cw.showStatus) cols.push_back({Col::Status, u.m.s(cw.status)}), resizable.emplace_back("status", cw.status);
  if (cw.showDur) cols.push_back({Col::Dur, u.m.s(cw.dur)}), resizable.emplace_back("dur", cw.dur);
  if (cw.showFloat) cols.push_back({Col::Float, u.m.s(cw.flt)}), resizable.emplace_back("float", cw.flt);
  float leftW = 0;
  for (const auto& cd : cols) leftW += cd.w;

  // 行
  const auto rows = c.visibleRows();
  const std::vector<char> dim = c.dimmedSet();
  u.order.clear();
  std::vector<int> rowOf(T.n, -1);
  for (std::size_t i = 0; i < rows.size(); ++i) {
    u.order.push_back(T.ids[rows[i].id]);
    rowOf[rows[i].id] = static_cast<int>(i);
  }

  // 時間軸
  const Day base = s->baseDay();
  Day lo = base, hi = base + 120;
  for (int id : T.leafOrder) {
    lo = std::min(lo, R[id].asapStart);
    hi = std::max({hi, R[id].asapFinish + 21, R[id].alapFinish + 21});
    if (T.dl[id]) hi = std::max(hi, *T.dl[id] + 21);
  }
  Axis ax;
  ax.rs = lo - ((dow(lo) + 6) % 7) - 7;
  ax.hi = hi;
  if (zoom == "day") ax.px = u.m.s(22);
  else if (zoom == "month") ax.px = u.m.s(2.4f);
  else if (zoom == "all")
    ax.px = std::max(0.9f, std::min(12.f, (size.x - leftW - u.m.s(24)) / static_cast<float>(hi - ax.rs + 1))) ;
  else ax.px = u.m.s(6);
  const float timelineW = static_cast<float>(hi - ax.rs + 1) * ax.px;
  const float contentW = leftW + timelineW, contentH = headH + RH * static_cast<float>(rows.size()) + u.m.s(40);

  // 初期スクロール（開いたとき・拡大率を変えたとき・「基準日へ」）
  if (u.scrolledProject != c.currentId() || u.lastZoom != zoom) {
    u.scrollToBase = true;
    u.scrolledProject = c.currentId();
    u.lastZoom = zoom;
  }

  ImGui::PushStyleColor(ImGuiCol_ChildBg, v4(u.t->surface));
  ImGui::BeginChild("##gantt", size, ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoMove);
  ImGui::PopStyleColor();
  if (u.scrollToBase) {
    ImGui::SetScrollX(zoom == "all" ? 0.f : std::max(0.f, static_cast<float>(base - ax.rs) * ax.px - u.m.s(90)));
    u.scrollToBase = false;
  }
  const ImVec2 wp = ImGui::GetWindowPos();
  const float sx = ImGui::GetScrollX(), sy = ImGui::GetScrollY();
  const float viewW = ImGui::GetWindowWidth(), viewH = ImGui::GetWindowHeight();
  ImGui::Dummy(ImVec2(contentW, contentH));
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ax.left = wp.x + leftW - sx;
  const float bodyTop = wp.y + headH;
  const auto rowY = [&](int i) { return bodyTop + RH * static_cast<float>(i) - sy; };
  const int first = std::max(0, static_cast<int>(sy / RH) - 1);
  const int last = std::min(static_cast<int>(rows.size()), static_cast<int>((sy + viewH) / RH) + 2);
  const ImVec2 tlMin(wp.x + leftW, bodyTop), tlMax(wp.x + viewW, wp.y + viewH);
  const std::vector<int> rootIdx = rootIndexMap(T, I);

  // 関係する行（カーソル行の先行・後続）
  const std::string cur = c.cursor();
  const int curIdx = cur.empty() ? -1 : T.find(cur);
  std::set<int> related;
  if (curIdx >= 0)
    for (int leaf : T.leaves[curIdx]) {
      for (int p : T.preds[leaf]) related.insert(p);
      for (int q : T.succs[leaf]) related.insert(q);
    }
  const auto visRow = [&](int leaf) {
    for (int x = leaf; x >= 0; x = T.parentOf[x])
      if (rowOf[x] >= 0) return rowOf[x];
    return -1;
  };
  std::set<int> relatedRows;
  for (int x : related)
    if (int r = visRow(x); r >= 0) relatedRows.insert(r);

  // ---- タイムラインの背景 ----
  dl->PushClipRect(tlMin, tlMax, true);
  {
    const Day d0 = std::max(ax.rs, ax.rs + static_cast<int>((sx) / ax.px) - 1);
    const Day d1 = std::min(ax.hi, ax.rs + static_cast<int>((sx + viewW - leftW) / ax.px) + 1);
    const float weekW = ax.px * 7;
    for (Day d = d0; d <= d1; ++d) {
      const int w = dow(d);
      const float x = ax.x(d);
      if (zoom == "day" && (w == 0 || w == 6)) dl->AddRectFilled(ImVec2(x, tlMin.y), ImVec2(x + ax.px, tlMax.y), u.t->wkend);
      if (w >= 1 && w <= 5 && cal.isHoliday(d)) dl->AddRectFilled(ImVec2(x, tlMin.y), ImVec2(x + std::max(ax.px, 1.f), tlMax.y), u.t->holbg);
      if (weekW >= u.m.s(18) && w == 1) dl->AddLine(ImVec2(x, tlMin.y), ImVec2(x, tlMax.y), u.t->rowline);
    }
  }
  dl->PopClipRect();

  // ---- 行 ----
  const auto colorOf = [&](int id, Risk risk) -> ImU32 {
    if (colorBy == "float") {
      switch (risk) {
        case Risk::Late: return u.t->late;
        case Risk::Tight: return u.t->tightEdge;
        case Risk::Done: return u.t->c[0];
        default: return u.t->accent;
      }
    }
    if (colorBy == "asg") return u.t->c[nameHash(text::trim(I[id].assignee)) % 8];
    return u.t->c[rootIdx[id]];
  };
  std::string hoverTipId;
  // バーのドラッグ中のプレビュー
  const bool dragging = u.barDrag.active && !u.barDrag.id.empty();
  const int dragIdx = dragging ? T.find(u.barDrag.id) : -1;
  int dragDelta = 0;
  if (dragging) dragDelta = static_cast<int>(std::lround((ImGui::GetMousePos().x - u.barDrag.startX) / ax.px));

  for (int i = first; i < last; ++i) {
    const VisibleRow& vr = rows[i];
    const int id = vr.id;
    const ScheduleRow& r = R[id];
    const float y = rowY(i);
    const Risk risk = riskOf(r.floatDays, r.done);
    const ImU32 bc = colorOf(id, risk);
    FadeScope fade(dl, isDimmed(u, dim, id, T.ids[id]), dimAlpha(c));  // 上の絞り込みに一致しない行は薄く
    // 左の表（横スクロールで固定）
    ImGui::PushClipRect(ImVec2(wp.x, bodyTop), ImVec2(wp.x + leftW, tlMax.y), true);
    drawTableRow(f, vr, ImVec2(wp.x, y), leftW, cols, false, relatedRows.count(i) > 0 || T.ids[id] == cur);
    ImGui::PopClipRect();
    // タイムライン
    ImGui::PushClipRect(tlMin, tlMax, true);
    const bool sel = c.isSelected(T.ids[id]);
    if (sel) dl->AddRectFilled(ImVec2(tlMin.x, y), ImVec2(tlMax.x, y + RH), u.t->selbg);
    if (vr.depth == 0) dl->AddLine(ImVec2(tlMin.x, y), ImVec2(tlMax.x, y), u.t->line);
    dl->AddLine(ImVec2(tlMin.x, y + RH - 1), ImVec2(tlMax.x, y + RH - 1), u.t->rowline);
    const float by0 = y + u.m.s(6), by1 = y + RH - u.m.s(6), cy = y + RH / 2;
    ImGui::PushID(T.ids[id].c_str());
    // 行の当たり判定（タイムライン部分）
    ImGui::SetCursorScreenPos(ImVec2(std::max(tlMin.x, ax.x(ax.rs)), y));
    ImGui::SetNextItemAllowOverlap();
    ImGui::InvisibleButton("##trow", ImVec2(std::max(1.f, std::min(timelineW, tlMax.x - tlMin.x)), RH));
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) rowClick(f, T.ids[id]);
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) openContext(f, T.ids[id]);
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) openDetail(f, T.ids[id], 0);

    if (vr.isPar) {
      const float x0 = ax.x(r.asapStart), x1 = ax.x(r.asapFinish + 1), xl = ax.x(std::max(r.alapFinish, r.asapFinish) + 1);
      if (!r.done && xl > x1) {
        const auto rc = riskColor(u, risk);
        dl->AddRectFilled(ImVec2(x1, cy - u.m.s(2)), ImVec2(xl, cy + u.m.s(2)), withAlpha(risk == Risk::Ok ? u.t->muted : rc.fg, 0.35f));
      }
      const float w = std::max(x1 - x0, 3.f);
      const float t0 = y + u.m.s(9), t1 = t0 + u.m.s(6);
      const ImU32 col = r.done ? withAlpha(bc, 0.5f) : bc;
      dl->AddRectFilled(ImVec2(x0, t0), ImVec2(x0 + w, t1), col);
      const ImVec2 lt[3] = {ImVec2(x0, t1), ImVec2(x0 + u.m.s(6), t1), ImVec2(x0, t1 + u.m.s(6))};
      const ImVec2 rt[3] = {ImVec2(x0 + w - u.m.s(6), t1), ImVec2(x0 + w, t1), ImVec2(x0 + w, t1 + u.m.s(6))};
      dl->AddTriangleFilled(lt[0], lt[1], lt[2], col);
      dl->AddTriangleFilled(rt[0], rt[1], rt[2], col);
      ImGui::SetCursorScreenPos(ImVec2(x0, t0));
      ImGui::SetNextItemAllowOverlap();
      ImGui::InvisibleButton("##sum", ImVec2(w, u.m.s(12)));
      if (ImGui::IsItemHovered()) hoverTipId = T.ids[id];
    } else if (T.kind[id] == Kind::Ms) {
      const float x = ax.x(r.asapFinish) + ax.px / 2;
      diamond(dl, ImVec2(x, cy), u.m.s(6) * 1.2f, r.done ? withAlpha(bc, 0.5f) : bc, risk == Risk::Late ? u.t->late : 0);
      ImGui::SetCursorScreenPos(ImVec2(x - u.m.s(8), cy - u.m.s(8)));
      ImGui::SetNextItemAllowOverlap();
      ImGui::InvisibleButton("##ms", ImVec2(u.m.s(16), u.m.s(16)));
      if (ImGui::IsItemHovered()) hoverTipId = T.ids[id];
    } else {
      const bool movable = !r.done && !r.pinned;
      Day s0 = r.asapStart, f0 = r.asapFinish;
      if (dragIdx == id) {
        if (u.barDrag.resize) f0 = std::max(s0, u.barDrag.origFinish + dragDelta);
        else {
          s0 = u.barDrag.origStart + dragDelta;
          f0 = u.barDrag.origFinish + dragDelta;
        }
      }
      // 余裕の影 / 遅れの帯
      if (!r.done && r.alapFinish > r.asapFinish) {
        const auto rc = riskColor(u, risk);
        const ImVec2 a(ax.x(r.asapFinish + 1), cy - u.m.s(3)), b(ax.x(r.alapFinish + 1), cy + u.m.s(3));
        hatch(dl, a, b, withAlpha(risk == Risk::Ok ? u.t->muted : rc.fg, 0.6f), u.m.s(5));
      }
      if (!r.done && r.alapFinish < r.asapFinish)
        dl->AddRectFilled(ImVec2(ax.x(r.alapFinish + 1), cy - u.m.s(3)), ImVec2(ax.x(r.asapFinish + 1), cy + u.m.s(3)),
                          withAlpha(u.t->late, 0.55f), u.m.s(2));
      const float x0 = ax.x(s0), bw = std::max(static_cast<float>(f0 - s0 + 1) * ax.px, 3.f);
      const ImVec2 a(x0, by0), b(x0 + bw, by1);
      dl->AddRectFilled(a, b, r.done ? withAlpha(bc, 0.45f) : bc, u.m.s(4));
      if (r.prog) hatch(dl, a, b, IM_COL32(255, 255, 255, 90), u.m.s(6));
      if (!r.done && r.floatDays < 0) dl->AddRect(a, b, u.t->late, u.m.s(4), 0, 2.0f);
      else if (!r.done && r.floatDays == 0) dl->AddRect(a, b, u.t->tightEdge, u.m.s(4), 0, 2.0f);
      if (r.waited) dashedRect(dl, a, b, u.t->dark ? IM_COL32(230, 233, 238, 255) : IM_COL32(0, 0, 0, 255), u.m.s(3));
      if (bw > u.m.s(34)) {
        if (r.done) drawIcon(dl, "check", ImVec2(x0 + u.m.s(5), cy - u.m.s(6)), u.m.s(12), IM_COL32_WHITE, 3.f);
        else {
          const std::string lab = csv::fmtNum(r.weeks) + "w";
          dl->AddText(ImVec2(x0 + u.m.s(6), cy - ImGui::GetTextLineHeight() / 2), IM_COL32_WHITE, lab.c_str());
        }
      }
      ImGui::SetCursorScreenPos(a);
      ImGui::SetNextItemAllowOverlap();
      ImGui::InvisibleButton("##bar", ImVec2(bw, by1 - by0));
      const bool barHov = ImGui::IsItemHovered();
      const bool overHandle = movable && ImGui::GetMousePos().x > b.x - u.m.s(6);
      if (barHov) {
        hoverTipId = T.ids[id];
        if (movable) ImGui::SetMouseCursor(overHandle ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_Hand);
      }
      if (movable && ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 3.f) && !u.barDrag.active) {
        u.barDrag.active = true;
        u.barDrag.id = T.ids[id];
        u.barDrag.resize = overHandle;
        u.barDrag.startX = ImGui::GetIO().MouseClickedPos[0].x;
        u.barDrag.origStart = r.asapStart;
        u.barDrag.origFinish = r.asapFinish;
      }
    }
    // 期限の菱形（白抜き・黒い縁）
    if (T.ownDl[id]) {
      const float x = ax.x(*T.ownDl[id]) + ax.px / 2;
      diamond(dl, ImVec2(x, cy), u.m.s(6), u.t->surface, u.t->dark ? u.t->ink : IM_COL32(0, 0, 0, 255));
      if (ImGui::IsMouseHoveringRect(ImVec2(x - u.m.s(6), cy - u.m.s(6)), ImVec2(x + u.m.s(6), cy + u.m.s(6))) &&
          ImGui::IsWindowHovered())
        ImGui::SetTooltip("期限 %s", fmtLong(*T.ownDl[id]).c_str());
    }
    ImGui::PopID();
    ImGui::PopClipRect();
  }

  // ---- 依存の矢印（見える範囲に関係するもの）----
  {
    dl->PushClipRect(tlMin, tlMax, true);
    struct Arrow {
      int a, b;      // 行
      int succLeaf;  // 色の判断
      bool hot;
    };
    std::map<std::pair<int, int>, Arrow> arrows;
    for (int leaf : T.leafOrder)
      for (int sc : T.succs[leaf]) {
        const int ra = visRow(leaf), rb = visRow(sc);
        if (ra < 0 || rb < 0 || ra == rb) continue;
        if (std::max(ra, rb) < first || std::min(ra, rb) > last) continue;
        const bool hot = curIdx >= 0 && (rows[ra].id == curIdx || rows[rb].id == curIdx ||
                                         std::find(T.leaves[curIdx].begin(), T.leaves[curIdx].end(), leaf) != T.leaves[curIdx].end() ||
                                         std::find(T.leaves[curIdx].begin(), T.leaves[curIdx].end(), sc) != T.leaves[curIdx].end());
        auto& ar = arrows[{ra, rb}];
        if (ar.a == ar.b) ar = {ra, rb, sc, hot};
        else ar.hot = ar.hot || hot;
      }
    for (int pass = 0; pass < 2; ++pass)
      for (const auto& [k, ar] : arrows) {
        if ((pass == 1) != ar.hot) continue;
        const int ia = rows[ar.a].id, ib = rows[ar.b].id;
        const ScheduleRow& ra = R[ia];
        const ScheduleRow& rb = R[ib];
        const bool msA = T.isLeaf(ia) && T.kind[ia] == Kind::Ms, msB = T.isLeaf(ib) && T.kind[ib] == Kind::Ms;
        const float y0 = rowY(ar.a) + RH / 2, y1 = rowY(ar.b) + RH / 2;
        const float x0 = msA ? ax.x(ra.asapFinish) + ax.px / 2 + u.m.s(7) : ax.x(ra.asapFinish + 1);
        const float x1 = msB ? ax.x(rb.asapStart) + ax.px / 2 - u.m.s(7) : ax.x(rb.asapStart);
        const ScheduleRow& sr = R[ar.succLeaf];
        ImU32 col = sr.floatDays < 0 ? u.t->late : sr.floatDays == 0 ? u.t->tightEdge : u.t->arrow;
        if (ar.hot) col = u.t->accent;
        else if (curIdx >= 0) col = withAlpha(col, 0.35f);
        if (!ar.hot && isDimmed(u, dim, ia, T.ids[ia]) && isDimmed(u, dim, ib, T.ids[ib])) col = withAlpha(col, dimAlpha(c));
        const float kk = u.m.s(6);
        std::vector<ImVec2> pts;
        if (x1 >= x0 + u.m.s(12)) {
          pts = {ImVec2(x0, y0), ImVec2(x0 + kk, y0), ImVec2(x0 + kk, y1), ImVec2(x1, y1)};
        } else {
          const float ym = y0 + (y1 > y0 ? RH / 2 : -RH / 2);
          pts = {ImVec2(x0, y0), ImVec2(x0 + kk, y0), ImVec2(x0 + kk, ym), ImVec2(x1 - kk, ym), ImVec2(x1 - kk, y1), ImVec2(x1, y1)};
        }
        dl->AddPolyline(pts.data(), static_cast<int>(pts.size()), col, ImDrawFlags_None, ar.hot ? 2.f : 1.3f);
        dl->AddTriangleFilled(ImVec2(x1, y1), ImVec2(x1 - u.m.s(6), y1 - u.m.s(4)), ImVec2(x1 - u.m.s(6), y1 + u.m.s(4)), col);
      }
    dl->PopClipRect();
  }

  // ---- 見出し（縦スクロールで上に固定）----
  {
    dl->PushClipRect(ImVec2(wp.x, wp.y), ImVec2(wp.x + viewW, wp.y + headH), true);
    dl->AddRectFilled(ImVec2(wp.x, wp.y), ImVec2(wp.x + viewW, wp.y + headH), u.t->surface2);
    dl->AddLine(ImVec2(wp.x, wp.y + headH - 1), ImVec2(wp.x + viewW, wp.y + headH - 1), u.t->line);
    // タイムラインの見出し
    dl->PushClipRect(ImVec2(wp.x + leftW, wp.y), ImVec2(wp.x + viewW, wp.y + headH), true);
    const Day d0 = std::max(ax.rs, ax.rs + static_cast<int>(sx / ax.px) - 40);
    const Day d1 = std::min(ax.hi, ax.rs + static_cast<int>((sx + viewW) / ax.px) + 2);
    float lastLabel = -1e9f;
    const float rowH2 = headH > u.m.s(40) ? headH / 2 : headH;
    for (Day d = d0; d <= d1; ++d) {
      const Ymd ymd = civilFromDays(d);
      const float x = ax.x(d);
      if (ymd.d == 1 || d == d0) {
        if (x - lastLabel >= u.m.s(58)) {
          const std::string l = std::to_string(ymd.y) + "/" + std::to_string(ymd.m);
          dl->AddText(ImVec2(std::max(x, wp.x + leftW) + u.m.s(4), wp.y + (rowH2 - ImGui::GetTextLineHeight()) / 2), u.t->ink, l.c_str());
          lastLabel = x;
        }
        if (ymd.d == 1) dl->AddLine(ImVec2(x, wp.y), ImVec2(x, wp.y + headH), u.t->line);
      }
      if (zoom == "day") {
        const int w = dow(d);
        const std::string n = std::to_string(ymd.d);
        const ImVec2 ts = ImGui::CalcTextSize(n.c_str());
        dl->AddText(ImVec2(x + (ax.px - ts.x) / 2, wp.y + rowH2 + (rowH2 - ts.y) / 2), (w == 0 || w == 6) ? u.t->muted : u.t->ink, n.c_str());
      } else if (zoom == "week" && dow(d) == 1) {
        const float weekW = ax.px * 7;
        const int every = weekW < u.m.s(26) ? 3 : weekW < u.m.s(40) ? 2 : 1;
        const int wk = (d - ax.rs) / 7;
        if (wk % every == 0) {
          const std::string l = fmtShort(d);
          dl->AddText(ImVec2(x + u.m.s(2), wp.y + rowH2 + (rowH2 - ImGui::GetTextLineHeight()) / 2), u.t->muted, l.c_str());
        }
      }
    }
    // 基準日
    {
      const float bx = ax.x(base) + ax.px / 2;
      const ImVec2 ts = ImGui::CalcTextSize("基準日");
      dl->AddRectFilled(ImVec2(bx - ts.x / 2 - u.m.s(4), wp.y + headH - ts.y - u.m.s(4)), ImVec2(bx + ts.x / 2 + u.m.s(4), wp.y + headH - 1),
                        u.t->accent, u.m.s(3));
      dl->AddText(ImVec2(bx - ts.x / 2, wp.y + headH - ts.y - u.m.s(2)), u.t->accentInk, "基準日");
    }
    dl->PopClipRect();
    dl->PopClipRect();
    // 基準日の縦線（本体）
    dl->PushClipRect(tlMin, tlMax, true);
    const float bx = ax.x(base) + ax.px / 2;
    dl->AddLine(ImVec2(bx, tlMin.y), ImVec2(bx, tlMax.y), withAlpha(u.t->accent, 0.8f), 1.5f);
    dl->PopClipRect();
    // 左の表の見出し（列幅は右端のドラッグで変える）
    ImGui::PushClipRect(ImVec2(wp.x, wp.y), ImVec2(wp.x + leftW, wp.y + headH), true);
    drawTableHeader(f, ImVec2(wp.x, wp.y), leftW, headH, cols, false);
    float x = wp.x + cols[0].w;
    std::size_t ri = 0;
    for (std::size_t k = 1; k < cols.size(); ++k) {
      x += cols[k].w;
      if (ri >= resizable.size()) break;
      const std::string key = resizable[ri++].first;
      ImGui::SetCursorScreenPos(ImVec2(x - u.m.s(4), wp.y));
      ImGui::PushID(key.c_str());
      ImGui::InvisibleButton("##rz", ImVec2(u.m.s(8), headH));
      if (ImGui::IsItemHovered() || ImGui::IsItemActive()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
      if (ImGui::IsItemActivated()) {
        u.colDrag = {key, ImGui::GetMousePos().x, cols[k].w / u.m.scale, true};
      }
      ImGui::PopID();
      dl->AddLine(ImVec2(x - 1, wp.y + u.m.s(8)), ImVec2(x - 1, wp.y + headH - u.m.s(8)), u.t->line);
    }
    ImGui::PopClipRect();
    // 左の表とタイムラインの境界
    dl->AddLine(ImVec2(wp.x + leftW, wp.y), ImVec2(wp.x + leftW, wp.y + viewH), u.t->line);
  }

  // 列幅のドラッグを確定
  if (u.colDrag.active && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
    const float nw = std::max(u.colDrag.col == "name" ? 160.f : 40.f,
                              std::round(u.colDrag.startW + (ImGui::GetMousePos().x - u.colDrag.startX) / u.m.scale));
    if (u.colDrag.col == "name") c.setUi("nameW", nw);
    else c.setUiSub("colW", u.colDrag.col, nw);
    u.colDrag.active = false;
  }

  // ---- バーのドラッグ ----
  if (dragging && dragIdx >= 0) {
    const Day aStart = R[dragIdx].asapStart;
    const int curDays = R[dragIdx].days;
    if (u.barDrag.resize) {
      const Day nf = std::max(aStart, u.barDrag.origFinish + dragDelta);
      const int days = std::max(1, workdaysIn(cal, aStart, nf));
      ImGui::SetTooltip("期間 %s週（%d営業日）", csv::fmtNum(days / 5.0).c_str(), days);
    } else {
      ImGui::SetTooltip("着手 %s", fmtLong(u.barDrag.origStart + dragDelta).c_str());
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
      const auto bd = u.barDrag;
      u.barDrag = {};
      if (dragDelta != 0) {
        if (bd.resize) {
          const Day nf = cal.lastWork(std::max(aStart, bd.origFinish + dragDelta));
          const int days = std::max(1, workdaysIn(cal, aStart, nf));
          if (days != curDays) {
            s->setDuration(bd.id, days / 5.0);
            c.afterEdit();
          }
        } else {
          // 右へ動かしたら次の営業日、左へなら直前の営業日に丸める（付録D-7）
          Day ns = bd.origStart + dragDelta;
          ns = dragDelta > 0 ? cal.workday(ns - 1, 1) : cal.lastWork(ns);
          if (ns != bd.origStart) {
            s->setEarliest(bd.id, ns);
            c.afterEdit();
          }
        }
      }
    }
  } else if (u.barDrag.active && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
    u.barDrag = {};
  }

  // ---- 行のドラッグ（並べ替え・階層変更）----
  if (u.rowDrag.active) {
    const ImVec2 mp = ImGui::GetMousePos();
    const int ri = static_cast<int>(std::floor((mp.y - bodyTop + sy) / RH));
    std::string target;
    RelocateWhere where = RelocateWhere::Into;
    if (ri >= 0 && ri < static_cast<int>(rows.size())) {
      target = T.ids[rows[ri].id];
      const float fy = (mp.y - rowY(ri)) / RH;
      where = fy < 0.28f ? RelocateWhere::Before : fy > 0.72f ? RelocateWhere::After : RelocateWhere::Into;
      const auto ds = s->descendantsOf(u.rowDrag.id);
      const bool bad = target == u.rowDrag.id || std::find(ds.begin(), ds.end(), target) != ds.end();
      const float y = rowY(ri);
      const ImU32 col = bad ? u.t->late : u.t->accent;
      if (where == RelocateWhere::Before) dl->AddLine(ImVec2(wp.x, y), ImVec2(wp.x + viewW, y), col, 2.f);
      else if (where == RelocateWhere::After) dl->AddLine(ImVec2(wp.x, y + RH), ImVec2(wp.x + viewW, y + RH), col, 2.f);
      else dl->AddRect(ImVec2(wp.x + 1, y + 1), ImVec2(wp.x + viewW - 1, y + RH - 1), col, 0, 0, 2.f);
      if (bad) target.clear();
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
      if (!target.empty()) {
        s->relocate(u.rowDrag.id, target, where);
        c.afterEdit();
      }
      u.rowDrag = {};
    }
  }

  // 空の表示
  if (rows.empty()) {
    if (I.empty()) {
      ImGui::SetCursorScreenPos(ImVec2(wp.x + viewW / 2 - u.m.s(140), wp.y + viewH / 2 - u.m.s(40)));
      ImGui::BeginGroup();
      ImGui::TextDisabled("%s", S::kEmptyProject);
      if (iconTextButton(u, "first", "plus", S::kFirstIssue, true)) {
        const std::string nid = s->addIssue("", "");
        c.afterEdit();
        if (!nid.empty()) beginEdit(f, nid, EditField::Name, true);
      }
      ImGui::SameLine();
      if (iconTextButton(u, "imp", "upload", S::kImportCsv)) openDialog(f, Dlg::Import);
      ImGui::EndGroup();
    } else {
      ImGui::SetCursorScreenPos(ImVec2(wp.x + u.m.s(24), bodyTop + u.m.s(24)));
      ImGui::TextDisabled("%s", S::kNoMatch);
    }
  }
  if (!hoverTipId.empty() && !u.barDrag.active && T.find(hoverTipId) >= 0) showTip(tipLines(*s, T.find(hoverTipId)));
  ImGui::EndChild();
}

}  // namespace roadmap::ui
