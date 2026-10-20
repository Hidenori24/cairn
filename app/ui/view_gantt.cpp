// ガント（SPEC 10.5）。1つのスクロール領域に、左の表（横スクロールで固定）とタイムライン（見出しは縦スクロールで固定）を描く。
// 見える範囲の行・矢印だけを描く（仮想化）。
#include <algorithm>
#include <cmath>
#include <map>
#include <set>

#include "roadmap/dependency_view.hpp"
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

// 折れ線を点線で描く
// （描く範囲 = クリップの外の点線は頂点を作らない。長い矢印で頂点が増えすぎないように）
void dashedPolyline(ImDrawList* dl, const std::vector<ImVec2>& pts, ImU32 col, float th, float dash) {
  const ImVec4 clip(dl->GetClipRectMin().x, dl->GetClipRectMin().y, dl->GetClipRectMax().x, dl->GetClipRectMax().y);
  for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
    const ImVec2 p = pts[i], q = pts[i + 1];
    const float len = std::hypot(q.x - p.x, q.y - p.y);
    if (len <= 0.f) continue;
    if (std::max(p.x, q.x) < clip.x || std::min(p.x, q.x) > clip.z || std::max(p.y, q.y) < clip.y || std::min(p.y, q.y) > clip.w) continue;
    for (float t = 0; t < len; t += dash * 2) {
      const float t2 = std::min(len, t + dash);
      const float xa = p.x + (q.x - p.x) * t / len, xb = p.x + (q.x - p.x) * t2 / len;
      const float ya = p.y + (q.y - p.y) * t / len, yb = p.y + (q.y - p.y) * t2 / len;
      if (std::max(xa, xb) < clip.x || std::min(xa, xb) > clip.z || std::max(ya, yb) < clip.y || std::min(ya, yb) > clip.w) continue;
      dl->AddLine(ImVec2(xa, ya), ImVec2(xb, yb), col, th);
    }
  }
}

// 折れ線の上で m にいちばん近い点: 距離と、始点からそこまでの道のりの割合（0〜1）
struct PolyHit {
  float dist = 1e9f;
  float frac = 0;
};
PolyHit nearestOnPolyline(const std::vector<ImVec2>& pts, ImVec2 m) {
  PolyHit best;
  float total = 0;
  for (std::size_t i = 0; i + 1 < pts.size(); ++i) total += std::hypot(pts[i + 1].x - pts[i].x, pts[i + 1].y - pts[i].y);
  if (total <= 0.f) return best;
  float run = 0;
  for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
    const ImVec2 p = pts[i], q = pts[i + 1];
    const float dx = q.x - p.x, dy = q.y - p.y, len2 = dx * dx + dy * dy, len = std::sqrt(len2);
    float t = len2 > 0.f ? ((m.x - p.x) * dx + (m.y - p.y) * dy) / len2 : 0.f;
    t = std::clamp(t, 0.f, 1.f);
    const float d = std::hypot(m.x - (p.x + dx * t), m.y - (p.y + dy * t));
    if (d < best.dist) {
      best.dist = d;
      best.frac = (run + len * t) / total;
    }
    run += len;
  }
  return best;
}

// 折れ線の道のりの割合 frac の位置
ImVec2 pointAtFrac(const std::vector<ImVec2>& pts, float frac) {
  float total = 0;
  for (std::size_t i = 0; i + 1 < pts.size(); ++i) total += std::hypot(pts[i + 1].x - pts[i].x, pts[i + 1].y - pts[i].y);
  float want = total * frac, run = 0;
  for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
    const float len = std::hypot(pts[i + 1].x - pts[i].x, pts[i + 1].y - pts[i].y);
    if (len > 0.f && run + len >= want) {
      const float t = (want - run) / len;
      return ImVec2(pts[i].x + (pts[i + 1].x - pts[i].x) * t, pts[i].y + (pts[i + 1].y - pts[i].y) * t);
    }
    run += len;
  }
  return pts.empty() ? ImVec2(0, 0) : pts.back();
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
  if (u.customProject != c.currentId()) {  // Ctrl+ホイールで変えた幅は、プロジェクトを開き直したら段階に戻す
    u.customPx = 0;
    u.customProject = c.currentId();
  }
  // 見出しの書き方は、1日の幅で決める（段階のときは段階の名前のまま）
  std::string styleZoom = zoom;
  if (u.customPx > 0) styleZoom = u.customPx >= 14 ? "day" : u.customPx >= 4 ? "week" : "month";
  const float headH = (styleZoom == "day" || styleZoom == "week") ? u.m.s(46) : u.m.s(30);
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
  if (u.customPx > 0) ax.px = u.m.s(u.customPx);
  else if (zoom == "day") ax.px = u.m.s(22);
  else if (zoom == "month") ax.px = u.m.s(2.4f);
  else if (zoom == "all")
    ax.px = std::max(0.9f, std::min(12.f, (size.x - leftW - u.m.s(24)) / static_cast<float>(hi - ax.rs + 1))) ;
  else ax.px = u.m.s(6);
  // Ctrl+ホイール（時間軸の上）: 1日の幅を変える。マウスの下の日付を動かさない。
  // 画面全体の拡大縮小は、ガントが使わなかった Ctrl+ホイールを app_ui が受ける
  u.wheelConsumed = false;
  {
    const ImGuiIO& io = ImGui::GetIO();
    const ImVec2 org = ImGui::GetCursorScreenPos();
    const bool overTimeline =
        ImGui::IsMouseHoveringRect(ImVec2(org.x + leftW, org.y), ImVec2(org.x + size.x, org.y + size.y)) &&
        ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && u.dlg == Dlg::None && !u.paletteOpen &&
        !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);
    if (io.KeyCtrl && io.MouseWheel != 0.f && overTimeline) {
      u.wheelConsumed = true;
      const float oldPx = ax.px;
      const float next = std::clamp(oldPx / u.m.scale * std::pow(1.2f, io.MouseWheel), 0.9f, 40.f);
      if (std::abs(next * u.m.scale - oldPx) > 1e-4f) {
        ax.px = next * u.m.scale;
        const float dayF = (io.MousePos.x - (org.x + leftW - u.ganttSx)) / oldPx;  // マウスの位置の日（開始日から）
        u.pendingScrollX = org.x + leftW + dayF * ax.px - io.MousePos.x;
        u.hasPendingScroll = true;
        u.customPx = next;
        u.lastZoom = zoom;  // 段階が変わったとみなして基準日へ戻さない
      }
    }
  }
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
  if (u.hasPendingScroll) {
    ImGui::SetScrollX(std::max(0.f, u.pendingScrollX));
    u.hasPendingScroll = false;
    u.scrollToBase = false;
  } else if (u.scrollToBase) {
    ImGui::SetScrollX(zoom == "all" && u.customPx <= 0 ? 0.f : std::max(0.f, static_cast<float>(base - ax.rs) * ax.px - u.m.s(90)));
    u.scrollToBase = false;
  }
  const ImVec2 wp = ImGui::GetWindowPos();
  const float sx = ImGui::GetScrollX(), sy = ImGui::GetScrollY();
  u.ganttSx = sx;
  u.ganttLeft = wp.x + leftW;
  u.ganttPx = ax.px;
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

  // ---- 依存をつなぐドラッグ: バーの位置とハンドルの当たり判定 ----
  // ハンドルはバーの外側（左右の端の少し外）に出す。バーの中（右端の長さ変更・本体の移動）とは重ならない
  struct Span {
    float xl = 0, xr = 0, cy = 0;
  };
  const auto spanOf = [&](int i) {
    const VisibleRow& vr = rows[static_cast<std::size_t>(i)];
    const ScheduleRow& r = R[vr.id];
    Span sp;
    sp.cy = rowY(i) + RH / 2;
    if (vr.isPar) {
      sp.xl = ax.x(r.asapStart);
      sp.xr = sp.xl + std::max(ax.x(r.asapFinish + 1) - sp.xl, 3.f);
    } else if (T.kind[vr.id] == Kind::Ms) {
      const float x = ax.x(r.asapFinish) + ax.px / 2;
      sp.xl = x - u.m.s(7.f);
      sp.xr = x + u.m.s(7.f);
    } else {
      sp.xl = ax.x(r.asapStart);
      sp.xr = sp.xl + std::max(static_cast<float>(r.asapFinish - r.asapStart + 1) * ax.px, 3.f);
    }
    return sp;
  };
  const float handleR = u.m.s(6), handleGap = u.m.s(4);
  const auto handleCenter = [&](const Span& sp, bool right) {
    return ImVec2(right ? sp.xr + handleGap + handleR : sp.xl - handleGap - handleR, sp.cy);
  };
  // マウスの下の行と、その行のどのハンドルの上か（0 = なし 1 = 左 2 = 右）。ほかのドラッグ・選択モード・ダイアログの間は出さない
  int hotRow = -1, hotSide = 0;
  bool idle = false;  // ほかのドラッグ・選択モード・ダイアログがなく、マウスがガントの上にある
  {
    const ImVec2 mp = ImGui::GetMousePos();
    idle = !u.linkPick.active && !u.barDrag.active && !u.rowDrag.active && !u.colDrag.active && !u.linkDrag.active &&
           !u.arrowDrag.pending && !u.arrowDrag.active && u.dlg == Dlg::None && !u.paletteOpen &&
           u.editField == EditField::None && ImGui::IsWindowHovered();
    if (idle && mp.x >= tlMin.x && mp.x < tlMax.x && mp.y >= bodyTop && mp.y < tlMax.y) {
      const int ri = static_cast<int>(std::floor((mp.y - bodyTop + sy) / RH));
      if (ri >= 0 && ri < static_cast<int>(rows.size())) {
        const Span sp = spanOf(ri);
        // バーの近くにあるときだけ「その行」にする（遠いところでは出さない）
        if (mp.x >= sp.xl - handleGap - handleR * 3 && mp.x <= sp.xr + handleGap + handleR * 3) {
          hotRow = ri;
          const float rr = handleR + handleGap;
          for (int side = 2; side >= 1; --side) {
            const ImVec2 hc = handleCenter(sp, side == 2);
            if ((mp.x - hc.x) * (mp.x - hc.x) + (mp.y - hc.y) * (mp.y - hc.y) <= rr * rr) {
              hotSide = side;
              break;
            }
          }
        }
      }
    }
    // ハンドルを押したらドラッグを始める（この行のクリックにはしない）
    if (hotSide != 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
      u.linkDrag = {};
      u.linkDrag.active = true;
      u.linkDrag.id = T.ids[rows[static_cast<std::size_t>(hotRow)].id];
      u.linkDrag.fromRight = hotSide == 2;
      u.linkDrag.project = c.currentId();
      u.linkDrag.start = mp;
    }
  }

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

  // ---- 依存をたどる・クリティカルの強調（アプリの表示設定。プロジェクトごとに覚える）----
  const bool traceOn = curIdx >= 0 && c.uiBool("dep", "trace", false);
  const bool critOn = c.uiBool("dep", "crit", false);
  const std::vector<char> traceSet = traceOn ? traceDependencies(T, curIdx) : std::vector<char>{};

  // ---- 依存の矢印の形と色の判断（描くのは行のあと。マウスの当たり判定のため先に作る）----
  struct Arrow {
    int a = 0, b = 0;      // 行
    int succLeaf = 0;      // 色の判断
    bool hot = false;      // カーソル行に関係する
    bool traced = false;   // 選んだ課題の先行・後続の連鎖の中（たどる表示）
    bool conflict = false;  // 後続が先行より前の日付
    bool critical = false;  // 余裕 0 以下の連鎖
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    std::vector<ImVec2> pts;
  };
  std::vector<Arrow> arrows;
  {
    std::map<std::pair<int, int>, Arrow> amap;
    for (int leaf : T.leafOrder)
      for (int sc : T.succs[leaf]) {
        const int ra = visRow(leaf), rb = visRow(sc);
        if (ra < 0 || rb < 0 || ra == rb) continue;
        if (std::max(ra, rb) < first || std::min(ra, rb) > last) continue;
        const bool hot = curIdx >= 0 && (rows[ra].id == curIdx || rows[rb].id == curIdx ||
                                         std::find(T.leaves[curIdx].begin(), T.leaves[curIdx].end(), leaf) != T.leaves[curIdx].end() ||
                                         std::find(T.leaves[curIdx].begin(), T.leaves[curIdx].end(), sc) != T.leaves[curIdx].end());
        const bool traced = traceOn && traceSet[leaf] && traceSet[sc];
        const bool zw = T.kind[sc] == Kind::Ms && R[sc].weeks == 0;
        const bool conflict = isDateConflict(R[leaf], R[sc], zw);
        const bool critical = isCriticalLink(R[leaf], R[sc]);
        auto [it, fresh] = amap.try_emplace({ra, rb});
        Arrow& ar = it->second;
        if (fresh) {
          ar.a = ra;
          ar.b = rb;
          ar.succLeaf = sc;
        }
        ar.hot = ar.hot || hot;
        ar.traced = ar.traced || traced;
        ar.conflict = ar.conflict || conflict;
        ar.critical = ar.critical || critical;
      }
    for (auto& [k, ar] : amap) {
      const int ia = rows[ar.a].id, ib = rows[ar.b].id;
      const ScheduleRow& ra = R[ia];
      const ScheduleRow& rb = R[ib];
      const bool msA = T.isLeaf(ia) && T.kind[ia] == Kind::Ms, msB = T.isLeaf(ib) && T.kind[ib] == Kind::Ms;
      ar.y0 = rowY(ar.a) + RH / 2;
      ar.y1 = rowY(ar.b) + RH / 2;
      ar.x0 = msA ? ax.x(ra.asapFinish) + ax.px / 2 + u.m.s(7) : ax.x(ra.asapFinish + 1);
      ar.x1 = msB ? ax.x(rb.asapStart) + ax.px / 2 - u.m.s(7) : ax.x(rb.asapStart);
      const float kk = u.m.s(6);
      if (ar.x1 >= ar.x0 + u.m.s(12)) {
        ar.pts = {ImVec2(ar.x0, ar.y0), ImVec2(ar.x0 + kk, ar.y0), ImVec2(ar.x0 + kk, ar.y1), ImVec2(ar.x1, ar.y1)};
      } else {
        const float ym = ar.y0 + (ar.y1 > ar.y0 ? RH / 2 : -RH / 2);
        ar.pts = {ImVec2(ar.x0, ar.y0), ImVec2(ar.x0 + kk, ar.y0), ImVec2(ar.x0 + kk, ym),
                  ImVec2(ar.x1 - kk, ym), ImVec2(ar.x1 - kk, ar.y1), ImVec2(ar.x1, ar.y1)};
      }
      arrows.push_back(std::move(ar));
    }
  }
  // 課題に書かれたつなぎ（x の blocks に y）のうち、行 ra → 行 rb の矢印を作っているものが、行の課題そのものの1本だけか。
  // そのときだけ矢印をつかんで付け替え・削除できる（親や折りたたみをまたぐ依存は詳細パネルで）
  const auto editableArrow = [&](const Arrow& ar) {
    const int ia = rows[ar.a].id, ib = rows[ar.b].id;
    int n = 0;
    bool exact = false;
    for (int x = 0; x < T.n; ++x)
      for (const auto& bid : I[x].blocks) {
        const int y = T.find(bid);
        if (y < 0) continue;
        const auto reaches = [&](int issue, int row) {
          for (int lf : T.leaves[issue])
            if (visRow(lf) == row) return true;
          return false;
        };
        if (!reaches(x, ar.a) || !reaches(y, ar.b)) continue;
        ++n;
        if (x == ia && y == ib) exact = true;
      }
    return n == 1 && exact;
  };
  // マウスの下の矢印（ハンドル・ほかのドラッグ・ダイアログのないとき）
  int hoverArrow = -1;
  float hoverFrac = 0;
  {
    const ImVec2 mp = ImGui::GetMousePos();
    if (idle && hotSide == 0 && mp.x >= tlMin.x && mp.x < tlMax.x && mp.y >= bodyTop && mp.y < tlMax.y) {
      float best = u.m.s(4.f);
      for (std::size_t k = 0; k < arrows.size(); ++k) {
        const PolyHit h = nearestOnPolyline(arrows[k].pts, mp);
        if (h.dist <= best) {
          best = h.dist;
          hoverArrow = static_cast<int>(k);
          hoverFrac = h.frac;
        }
      }
    }
  }
  u.arrowHoverFrom.clear();
  u.arrowHoverTo.clear();
  bool hoverEditable = false;
  if (hoverArrow >= 0) {
    const Arrow& ar = arrows[static_cast<std::size_t>(hoverArrow)];
    u.arrowHoverFrom = T.ids[rows[ar.a].id];
    u.arrowHoverTo = T.ids[rows[ar.b].id];
    hoverEditable = editableArrow(ar);
    // 矢印の上で押した: 動かしたらつなぎ替え（まだクリックでは何もしない。行の選択・バーのドラッグにもしない）
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
      u.arrowDrag = {};
      u.arrowDrag.pending = true;
      u.arrowDrag.editable = hoverEditable;
      u.arrowDrag.from = u.arrowHoverFrom;
      u.arrowDrag.to = u.arrowHoverTo;
      u.arrowDrag.grabHead = hoverFrac > 0.5f;  // 近い方の端をつかむ
      u.arrowDrag.project = c.currentId();
      u.arrowDrag.start = ImGui::GetMousePos();
    }
  }
  u.arrowGeom.clear();
  for (const Arrow& ar : arrows) u.arrowGeom.push_back({T.ids[rows[ar.a].id], T.ids[rows[ar.b].id], ar.pts, ar.conflict, ar.critical});
  const bool arrowBusy = hoverArrow >= 0 || u.arrowDrag.pending || u.arrowDrag.active;

  // ---- タイムラインの背景 ----
  dl->PushClipRect(tlMin, tlMax, true);
  {
    const Day d0 = std::max(ax.rs, ax.rs + static_cast<int>((sx) / ax.px) - 1);
    const Day d1 = std::min(ax.hi, ax.rs + static_cast<int>((sx + viewW - leftW) / ax.px) + 1);
    const float weekW = ax.px * 7;
    for (Day d = d0; d <= d1; ++d) {
      const int w = dow(d);
      const float x = ax.x(d);
      if (styleZoom == "day" && (w == 0 || w == 6)) dl->AddRectFilled(ImVec2(x, tlMin.y), ImVec2(x + ax.px, tlMax.y), u.t->wkend);
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
    // 上の絞り込みに一致しない行は薄く。「依存をたどる」では、選んだ課題の連鎖にない行も薄く
    FadeScope fade(dl, isDimmed(u, dim, id, T.ids[id]) || (traceOn && !traceSet[id] && u.editId != T.ids[id]), dimAlpha(c));
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
    const bool onHandle = hotSide != 0 && hotRow == i;  // ハンドルの上のクリックは行の選択にしない
    if (!onHandle && !u.linkDrag.active && !arrowBusy) {
      if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) rowClick(f, T.ids[id]);
      if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) openContext(f, T.ids[id]);
      if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) openDetail(f, T.ids[id], 0);
    }

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
      const bool movable = !r.done && !r.pinned && !u.linkPick.active;
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
      if (movable && ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 3.f) && !u.barDrag.active &&
          !u.linkDrag.active && !arrowBusy) {
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
    // つかんで動かしている矢印は、元の場所では薄くする（ゴムひもが代わりに動く）
    const auto isDragged = [&](const Arrow& ar) {
      return u.arrowDrag.active && T.ids[rows[ar.a].id] == u.arrowDrag.from && T.ids[rows[ar.b].id] == u.arrowDrag.to;
    };
    // 重なりの順: 通常 → クリティカル → 関係する・たどる・衝突 → マウスの下
    const auto layerOf = [&](const Arrow& ar, bool hov) {
      if (hov) return 3;
      if (ar.hot || ar.traced || ar.conflict) return 2;
      return critOn && ar.critical ? 1 : 0;
    };
    for (int layer = 0; layer < 4; ++layer)
      for (std::size_t k = 0; k < arrows.size(); ++k) {
        const Arrow& ar = arrows[k];
        const bool hov = static_cast<int>(k) == hoverArrow;
        if (layerOf(ar, hov) != layer) continue;
        const int ia = rows[ar.a].id, ib = rows[ar.b].id;
        const ScheduleRow& sr = R[ar.succLeaf];
        const ImU32 base = sr.floatDays < 0 ? u.t->late : sr.floatDays == 0 ? u.t->tightEdge : u.t->arrow;
        const bool strong = ar.hot || ar.traced;
        ImU32 col = base;
        float th = 1.3f;
        if (strong) {
          col = u.t->accent;
          th = 2.f;
        } else if (curIdx >= 0) {
          col = withAlpha(base, traceOn ? std::min(0.35f, dimAlpha(c)) : 0.35f);
        }
        if (critOn) {
          if (ar.critical) {  // 余裕 0 以下の連鎖: 太く（遅れなら赤、ちょうどなら橙）
            th = std::max(th, 2.8f);
            if (!strong) col = sr.floatDays < 0 ? u.t->late : u.t->tightEdge;
          } else if (!strong) {
            col = withAlpha(base, 0.3f);
          }
        }
        if (!strong && !ar.conflict && isDimmed(u, dim, ia, T.ids[ia]) && isDimmed(u, dim, ib, T.ids[ib])) col = withAlpha(col, dimAlpha(c));
        if (ar.conflict) {  // 日付が合わない: 赤・太め・点線・「!」（色だけに頼らない）
          col = u.t->late;
          th = std::max(th, 2.4f);
        }
        if (hov) {
          if (!ar.conflict) col = u.t->accent;
          th += 1.2f;
          dl->AddPolyline(ar.pts.data(), static_cast<int>(ar.pts.size()), withAlpha(col, 0.25f), ImDrawFlags_None, th + u.m.s(5));
        }
        if (isDragged(ar)) col = withAlpha(col, 0.2f);
        if (ar.conflict && !isDragged(ar)) dashedPolyline(dl, ar.pts, col, th, u.m.s(5));
        else dl->AddPolyline(ar.pts.data(), static_cast<int>(ar.pts.size()), col, ImDrawFlags_None, th);
        dl->AddTriangleFilled(ImVec2(ar.x1, ar.y1), ImVec2(ar.x1 - u.m.s(6), ar.y1 - u.m.s(4)), ImVec2(ar.x1 - u.m.s(6), ar.y1 + u.m.s(4)), col);
        if (ar.conflict && !isDragged(ar)) {
          const ImVec2 mid = pointAtFrac(ar.pts, 0.5f);
          const ImVec2 ts = ImGui::CalcTextSize("!");
          dl->AddCircleFilled(mid, u.m.s(7), u.t->late);
          dl->AddText(ImVec2(mid.x - ts.x / 2, mid.y - ts.y / 2), IM_COL32_WHITE, "!");
        }
        if (hov && hoverEditable) {  // 両端のつまみ（近い方の端がつかまれる）
          const bool head = hoverFrac > 0.5f;
          const ImVec2 pt(ar.x0, ar.y0), ph(ar.x1, ar.y1);
          dl->AddCircleFilled(pt, u.m.s(head ? 3.f : 5.f), head ? col : u.t->surface);
          dl->AddCircle(pt, u.m.s(head ? 3.f : 5.f), col, 0, 2.f);
          dl->AddCircleFilled(ph, u.m.s(head ? 5.f : 3.f), head ? u.t->surface : col);
          dl->AddCircle(ph, u.m.s(head ? 5.f : 3.f), col, 0, 2.f);
        }
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
      if (styleZoom == "day") {
        const int w = dow(d);
        const std::string n = std::to_string(ymd.d);
        const ImVec2 ts = ImGui::CalcTextSize(n.c_str());
        dl->AddText(ImVec2(x + (ax.px - ts.x) / 2, wp.y + rowH2 + (rowH2 - ts.y) / 2), (w == 0 || w == 6) ? u.t->muted : u.t->ink, n.c_str());
      } else if (styleZoom == "week" && dow(d) == 1) {
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

  // ---- 依存をつなぐドラッグ（ハンドル・ゴムひも・つなぐ相手・日程の影響）----
  u.barGeom.clear();
  for (int i = first; i < last; ++i) {
    const Span sp = spanOf(i);
    u.barGeom.push_back({T.ids[rows[static_cast<std::size_t>(i)].id], sp.xl, sp.xr, sp.cy});
  }
  const auto drawHandle = [&](ImVec2 hc, bool hot, bool ok = true) {
    const ImU32 col = ok ? u.t->accent : u.t->late;
    dl->AddCircleFilled(hc, handleR, hot ? col : u.t->surface);
    dl->AddCircle(hc, handleR, col, 0, 2.f);
    dl->AddCircleFilled(hc, handleR * 0.38f, hot ? IM_COL32_WHITE : col);
  };
  // 相手の行とバーの強調（つなげないときは lineCol を赤にする）
  const auto highlightTarget = [&](int tgtRow, ImU32 lineCol) {
    const Span tsp = spanOf(tgtRow);
    const float y = rowY(tgtRow);
    dl->AddRectFilled(ImVec2(tlMin.x, y), ImVec2(tlMax.x, y + RH), withAlpha(lineCol, 0.10f));
    const float pad = u.m.s(3);
    dl->AddRectFilled(ImVec2(tsp.xl - pad, y + u.m.s(4)), ImVec2(tsp.xr + pad, y + RH - u.m.s(4)), withAlpha(lineCol, 0.18f), u.m.s(5));
    dl->AddRect(ImVec2(tsp.xl - pad, y + u.m.s(4)), ImVec2(tsp.xr + pad, y + RH - u.m.s(4)), lineCol, u.m.s(5), 0, 2.5f);
  };
  // 日程への影響のプレビュー（後続がずれる位置に薄いバー）
  const auto drawGhosts = [&](const LinkPreview* pv) {
    if (!pv) return;
    for (const auto& sh : pv->shifts) {
      const int rr = rowOf[static_cast<std::size_t>(sh.idx)];
      if (rr < first || rr >= last) continue;
      const float y = rowY(rr), cy = y + RH / 2;
      const ImU32 gc = sh.newStart > sh.oldStart ? u.t->late : u.t->accent;
      if (T.kind[static_cast<std::size_t>(sh.idx)] == Kind::Ms) {
        diamond(dl, ImVec2(ax.x(sh.newFinish) + ax.px / 2, cy), u.m.s(6) * 1.2f, withAlpha(gc, 0.3f), gc);
      } else {
        const ImVec2 ga(ax.x(sh.newStart), y + u.m.s(6)), gb(ax.x(sh.newFinish + 1), y + RH - u.m.s(6));
        dl->AddRectFilled(ga, ImVec2(std::max(gb.x, ga.x + 3.f), gb.y), withAlpha(gc, 0.22f), u.m.s(4));
        dl->AddRect(ga, ImVec2(std::max(gb.x, ga.x + 3.f), gb.y), gc, u.m.s(4), 0, 1.5f);
      }
      // 元の位置からの動き
      dl->AddLine(ImVec2(ax.x(sh.oldStart), cy), ImVec2(ax.x(sh.newStart), cy), withAlpha(gc, 0.8f), 1.5f);
    }
  };
  // ゴムひも（p0 から p1 まで。fromRight = p0 が先行側の右端から右へ出る。そうでなければ左へ出る）
  const auto drawRubber = [&](ImVec2 p0, ImVec2 p1, bool fromRight, ImU32 lineCol) {
    const float dir = fromRight ? 1.f : -1.f;
    const float dx = std::max(u.m.s(40), std::abs(p1.x - p0.x) * 0.5f);
    const ImVec2 c1(p0.x + dir * dx, p0.y), c2(p1.x - dir * dx, p1.y);
    dl->AddBezierCubic(p0, c1, c2, p1, withAlpha(lineCol, 0.25f), u.m.s(6));  // 太い薄い線 + 本線
    dl->AddBezierCubic(p0, c1, c2, p1, lineCol, u.m.s(2.2f));
    if (fromRight) {
      dl->AddTriangleFilled(p1, ImVec2(p1.x - u.m.s(8), p1.y - u.m.s(5)), ImVec2(p1.x - u.m.s(8), p1.y + u.m.s(5)), lineCol);
    } else {
      dl->AddCircleFilled(p1, u.m.s(4), lineCol);
    }
  };
  if (u.linkDrag.active && (T.find(u.linkDrag.id) < 0 || c.currentId() != u.linkDrag.project)) u.linkDrag = {};
  if (u.linkDrag.active) {
    // つかんでいるバーの課題 si と、マウスの下の行の課題 tgt（同じ課題なら「元の場所」で、相手なし）
    const int si = T.find(u.linkDrag.id);
    const bool fromRight = u.linkDrag.fromRight;
    const ImVec2 mp = ImGui::GetMousePos();
    int tgtRow = -1;
    if (mp.x >= wp.x && mp.x < wp.x + viewW && mp.y >= bodyTop && mp.y < wp.y + viewH) {
      const int ri = static_cast<int>(std::floor((mp.y - bodyTop + sy) / RH));
      if (ri >= 0 && ri < static_cast<int>(rows.size()) && rows[static_cast<std::size_t>(ri)].id != si) tgtRow = ri;
    }
    const int tgt = tgtRow >= 0 ? rows[static_cast<std::size_t>(tgtRow)].id : -1;
    LinkBlock blk = LinkBlock::None;
    const LinkPreview* pv = nullptr;
    if (tgt >= 0) {
      const int pred = fromRight ? si : tgt, succ = fromRight ? tgt : si;
      blk = checkLink(T, I, pred, succ);  // 循環・重複・親子は赤（つなげない）
      if (blk == LinkBlock::None) {
        const std::string key = T.ids[pred] + "\n" + T.ids[succ];
        if (u.linkDrag.previewKey != key || u.linkDrag.previewVer != s->version()) {
          u.linkDrag.preview = s->previewLink(T.ids[pred], T.ids[succ]);
          u.linkDrag.previewKey = key;
          u.linkDrag.previewVer = s->version();
        }
        pv = &u.linkDrag.preview;
      }
    }
    const bool ok = tgt >= 0 && blk == LinkBlock::None;
    const bool bad = tgt >= 0 && !ok;
    const ImU32 lineCol = bad ? u.t->late : ok ? u.t->accent : u.t->muted;

    // つなぐ相手の行とバーを強調（つなげないときは赤）、日程への影響のプレビュー、ゴムひも
    dl->PushClipRect(tlMin, tlMax, true);
    Span tsp;
    if (tgtRow >= 0) {
      tsp = spanOf(tgtRow);
      highlightTarget(tgtRow, lineCol);
    }
    drawGhosts(pv);
    dl->PopClipRect();
    {
      dl->PushClipRect(ImVec2(wp.x, bodyTop), ImVec2(wp.x + viewW, wp.y + viewH), true);
      const int srow = rowOf[static_cast<std::size_t>(si)];
      ImVec2 p0 = u.linkDrag.start;
      if (srow >= 0) {
        p0 = handleCenter(spanOf(srow), fromRight);
        drawHandle(p0, true, !bad);
      }
      ImVec2 p1 = mp;
      if (tgtRow >= 0) p1 = ImVec2(fromRight ? tsp.xl - u.m.s(2) : tsp.xr + u.m.s(2), tsp.cy);  // 吸着: 相手の端にぴたっと付く
      drawRubber(p0, p1, fromRight, lineCol);
      dl->PopClipRect();
    }

    // 理由・影響の表示（カーソルの近く）
    {
      const std::string a = T.did[static_cast<std::size_t>(fromRight ? si : std::max(tgt, 0))];
      const std::string b = T.did[static_cast<std::size_t>(fromRight ? std::max(tgt, 0) : si)];
      ImGui::BeginTooltip();
      if (tgt < 0) {
        ImGui::TextUnformatted("つなぐ相手のバーまでドラッグ（Esc で取り消し）");
      } else if (bad) {
        ImGui::PushStyleColor(ImGuiCol_Text, v4(u.t->late));
        ImGui::TextUnformatted((a + " → " + b + " はつなげません").c_str());
        ImGui::TextUnformatted(linkBlockReason(blk));
        ImGui::PopStyleColor();
      } else {
        ImGui::TextUnformatted((a + " → " + b + " をつなぐ").c_str());
        const std::string who = T.did[static_cast<std::size_t>(fromRight ? tgt : si)];  // 後続
        if (pv && pv->delayDays > 0) {
          ImGui::PushStyleColor(ImGuiCol_Text, v4(u.t->late));
          std::string t = who + " が " + std::to_string(pv->delayDays) + "営業日 遅れます";
          if (pv->shifts.size() > 1) t += "（影響 " + std::to_string(pv->shifts.size()) + "件）";
          ImGui::TextUnformatted(t.c_str());
          ImGui::PopStyleColor();
        } else if (pv && !pv->shifts.empty()) {
          ImGui::TextDisabled("%s", ("日程が変わる課題 " + std::to_string(pv->shifts.size()) + "件").c_str());
        } else {
          ImGui::TextDisabled("日程は変わりません");
        }
        ImGui::TextDisabled("離すとつなぎます（Esc で取り消し）");
      }
      ImGui::EndTooltip();
    }
    ImGui::SetMouseCursor(bad ? ImGuiMouseCursor_NotAllowed : ImGuiMouseCursor_Hand);

    // 離したら確定（相手がいなければ取り消し）。課題を変えるのは描画が終わってから
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
      const std::string src = u.linkDrag.id;
      u.linkDrag = {};
      if (tgt >= 0) {
        const std::string other = T.ids[static_cast<std::size_t>(tgt)];
        deferAfterFrame(f, [&f, src, other, fromRight] { linkIssues(f, src, fromRight, other, true); });
      }
    }
  } else if (hotRow >= 0) {
    // マウスを載せたバーの左右にハンドルを出す
    dl->PushClipRect(tlMin, tlMax, true);
    const Span sp = spanOf(hotRow);
    drawHandle(handleCenter(sp, false), hotSide == 1);
    drawHandle(handleCenter(sp, true), hotSide == 2);
    dl->PopClipRect();
    if (hotSide != 0) {
      ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
      ImGui::SetTooltip("%s", hotSide == 2 ? "ドラッグして後続につなぐ" : "ドラッグして先行につなぐ");
    }
  }

  // ---- 依存の矢印をつかんで付け替え・削除するドラッグ ----
  if (u.arrowDrag.pending || u.arrowDrag.active) {
    if (T.find(u.arrowDrag.from) < 0 || T.find(u.arrowDrag.to) < 0 || c.currentId() != u.arrowDrag.project) u.arrowDrag = {};
  }
  if (u.arrowDrag.pending) {
    const ImVec2 mp = ImGui::GetMousePos();
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
      u.arrowDrag = {};  // 動かさずに離した（クリック）。何もしない
    } else if (u.arrowDrag.editable && std::hypot(mp.x - u.arrowDrag.start.x, mp.y - u.arrowDrag.start.y) >= u.m.s(4.f)) {
      u.arrowDrag.pending = false;
      u.arrowDrag.active = true;
    }
  }
  if (u.arrowDrag.active) {
    const int fi = T.find(u.arrowDrag.from), ti = T.find(u.arrowDrag.to);
    const bool head = u.arrowDrag.grabHead;  // true = 後続の端をつかんで動かす（先行 fi は動かさない）
    const int fixedIdx = head ? fi : ti, origIdx = head ? ti : fi;
    const ImVec2 mp = ImGui::GetMousePos();
    const bool inTimeline = mp.x >= tlMin.x && mp.x < tlMax.x && mp.y >= bodyTop && mp.y < tlMax.y;
    // マウスの下のバー（バーの上だけ。空いている場所は「外す」になる）
    int tgtRow = -1;
    if (inTimeline) {
      const float pad = u.m.s(4);
      for (int i = first; i < last && tgtRow < 0; ++i) {
        const Span sp = spanOf(i);
        const float y = rowY(i);
        if (mp.x >= sp.xl - pad && mp.x <= sp.xr + pad && mp.y >= y + u.m.s(4) && mp.y <= y + RH - u.m.s(4)) tgtRow = i;
      }
    }
    const int tgt = tgtRow >= 0 ? rows[static_cast<std::size_t>(tgtRow)].id : -1;
    enum class Act { Cancel, Keep, Relink, Remove, Bad } act = Act::Cancel;
    LinkBlock blk = LinkBlock::None;
    const LinkPreview* pv = nullptr;
    int newPred = fi, newSucc = ti;
    if (tgt >= 0 && tgt == origIdx) {
      act = Act::Keep;  // 元の相手の上: そのまま
    } else if (tgt >= 0) {
      newPred = head ? fi : tgt;
      newSucc = head ? tgt : ti;
      const std::string key = T.ids[newPred] + "\n" + T.ids[newSucc];
      if (u.arrowDrag.previewKey != key || u.arrowDrag.previewVer != s->version()) {
        u.arrowDrag.preview = s->previewLink(T.ids[newPred], T.ids[newSucc], u.arrowDrag.from, u.arrowDrag.to);
        u.arrowDrag.previewKey = key;
        u.arrowDrag.previewVer = s->version();
      }
      blk = u.arrowDrag.preview.block;
      if (blk == LinkBlock::None && u.arrowDrag.preview.ok) {
        act = Act::Relink;
        pv = &u.arrowDrag.preview;
      } else {
        act = Act::Bad;
      }
    } else if (inTimeline) {
      act = Act::Remove;  // バーのない場所
    }
    const ImU32 lineCol = act == Act::Bad ? u.t->late : act == Act::Remove ? u.t->tightEdge : act == Act::Cancel ? u.t->muted : u.t->accent;

    dl->PushClipRect(tlMin, tlMax, true);
    if (tgtRow >= 0) highlightTarget(tgtRow, lineCol);
    drawGhosts(pv);
    dl->PopClipRect();
    {
      dl->PushClipRect(ImVec2(wp.x, bodyTop), ImVec2(wp.x + viewW, wp.y + viewH), true);
      const int frow = rowOf[static_cast<std::size_t>(fixedIdx)];
      ImVec2 p0 = u.arrowDrag.start;
      if (frow >= 0) {
        const Span fsp = spanOf(frow);
        p0 = ImVec2(head ? fsp.xr : fsp.xl, fsp.cy);  // 動かさない端（先行の右端 / 後続の左端）
        dl->AddCircleFilled(p0, u.m.s(4), lineCol);
      }
      ImVec2 p1 = mp;
      if (tgtRow >= 0) {
        const Span tsp = spanOf(tgtRow);
        p1 = ImVec2(head ? tsp.xl - u.m.s(2) : tsp.xr + u.m.s(2), tsp.cy);
      }
      drawRubber(p0, p1, head, lineCol);
      if (act == Act::Remove) {  // 外す印
        const float r = u.m.s(5);
        dl->AddLine(ImVec2(mp.x - r, mp.y - r), ImVec2(mp.x + r, mp.y + r), lineCol, 2.5f);
        dl->AddLine(ImVec2(mp.x - r, mp.y + r), ImVec2(mp.x + r, mp.y - r), lineCol, 2.5f);
      }
      dl->PopClipRect();
    }

    // 説明（カーソルの近く）
    {
      const std::string oa = T.did[static_cast<std::size_t>(fi)] + " → " + T.did[static_cast<std::size_t>(ti)];
      ImGui::BeginTooltip();
      switch (act) {
        case Act::Cancel:
          ImGui::TextUnformatted("つなぎ替える相手のバーへ（ここで離すと取り消し）");
          ImGui::TextDisabled("%s", oa.c_str());
          break;
        case Act::Keep:
          ImGui::TextUnformatted((oa + " のまま").c_str());
          ImGui::TextDisabled("離すと変更しません");
          break;
        case Act::Remove:
          ImGui::PushStyleColor(ImGuiCol_Text, v4(u.t->tightEdge));
          ImGui::TextUnformatted((oa + " を外します").c_str());
          ImGui::PopStyleColor();
          ImGui::TextDisabled("離すと外します（Esc で取り消し）");
          break;
        case Act::Bad:
          ImGui::PushStyleColor(ImGuiCol_Text, v4(u.t->late));
          ImGui::TextUnformatted((T.did[static_cast<std::size_t>(newPred)] + " → " + T.did[static_cast<std::size_t>(newSucc)] + " はつなげません").c_str());
          ImGui::TextUnformatted(linkBlockReason(blk));
          ImGui::PopStyleColor();
          break;
        case Act::Relink:
          ImGui::TextUnformatted((oa + " を " + T.did[static_cast<std::size_t>(newPred)] + " → " + T.did[static_cast<std::size_t>(newSucc)] + " に付け替える").c_str());
          if (pv && pv->delayDays > 0) {
            ImGui::PushStyleColor(ImGuiCol_Text, v4(u.t->late));
            std::string t = T.did[static_cast<std::size_t>(newSucc)] + " が " + std::to_string(pv->delayDays) + "営業日 遅れます";
            if (pv->shifts.size() > 1) t += "（影響 " + std::to_string(pv->shifts.size()) + "件）";
            ImGui::TextUnformatted(t.c_str());
            ImGui::PopStyleColor();
          } else if (pv && !pv->shifts.empty()) {
            ImGui::TextDisabled("%s", ("日程が変わる課題 " + std::to_string(pv->shifts.size()) + "件").c_str());
          } else {
            ImGui::TextDisabled("日程は変わりません");
          }
          ImGui::TextDisabled("離すと付け替えます（Esc で取り消し）");
          break;
      }
      ImGui::EndTooltip();
    }
    ImGui::SetMouseCursor(act == Act::Bad ? ImGuiMouseCursor_NotAllowed : ImGuiMouseCursor_Hand);

    // 離したら確定。課題を変えるのは描画が終わってから（Undo 1回）
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
      const std::string proj = u.arrowDrag.project, from = u.arrowDrag.from, to = u.arrowDrag.to;
      const std::string oa = T.did[static_cast<std::size_t>(fi)] + " → " + T.did[static_cast<std::size_t>(ti)];
      std::string nf, nt, msg;
      if (act == Act::Relink) {
        nf = T.ids[static_cast<std::size_t>(newPred)];
        nt = T.ids[static_cast<std::size_t>(newSucc)];
        msg = oa + " を " + T.did[static_cast<std::size_t>(newPred)] + " → " + T.did[static_cast<std::size_t>(newSucc)] + " に付け替えました";
      }
      u.arrowDrag = {};
      if (act == Act::Relink) {
        deferAfterFrame(f, [&f, proj, from, to, nf, nt, msg] {
          Session* ss = f.c.session();
          if (!ss || f.c.currentId() != proj) return;
          const bool ok = ss->relinkIssue(from, to, nf, nt);
          f.c.afterEdit();
          if (ok) f.c.toast(msg, NoticeAction::Undo);
        });
      } else if (act == Act::Remove) {
        const std::string rmsg = oa + " を外しました";
        deferAfterFrame(f, [&f, proj, from, to, rmsg] {
          Session* ss = f.c.session();
          if (!ss || f.c.currentId() != proj) return;
          const bool ok = ss->removeLink(from, to);
          f.c.afterEdit();
          if (ok) f.c.toast(rmsg, NoticeAction::Undo);
        });
      } else if (act == Act::Bad) {
        f.c.toast(std::string("つなぎ替えできません: ") + linkBlockReason(blk));
      }
    }
  }

  // 空の表示
  if (rows.empty()) {
    if (I.empty()) {
      ImGui::SetCursorScreenPos(ImVec2(wp.x + viewW / 2 - u.m.s(140), wp.y + viewH / 2 - u.m.s(40)));
      ImGui::BeginGroup();
      ImGui::TextDisabled("%s", S::kEmptyProject);
      if (iconTextButton(u, "first", "plus", S::kFirstIssue, true)) {
        const std::string nid = c.createIssue("", "");
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
  if (hoverArrow >= 0 && !u.arrowDrag.pending && !u.arrowDrag.active && !u.barDrag.active && !u.linkDrag.active) {
    // 矢印にマウスを載せた: 「A → B」と、日付の衝突・クリティカル・つなぎ替えのしかた
    const Arrow& ar = arrows[static_cast<std::size_t>(hoverArrow)];
    ImGui::BeginTooltip();
    ImGui::TextUnformatted((T.did[static_cast<std::size_t>(rows[ar.a].id)] + " → " + T.did[static_cast<std::size_t>(rows[ar.b].id)]).c_str());
    if (ar.conflict) {
      ImGui::PushStyleColor(ImGuiCol_Text, v4(u.t->late));
      ImGui::TextUnformatted("日付が合いません（後続が先行より前に始まります）");
      ImGui::PopStyleColor();
    }
    if (ar.critical) ImGui::TextDisabled("クリティカルな連鎖（余裕 0 以下）");
    if (hoverEditable) ImGui::TextDisabled("端をドラッグでつなぎ替え・バーのない所へ離すと外します");
    else ImGui::TextDisabled("親や折りたたみをまたぐ依存です。詳細パネルの依存関係で編集できます");
    ImGui::EndTooltip();
    if (hoverEditable) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
  } else if (!hoverTipId.empty() && !u.barDrag.active && !u.linkDrag.active && !u.arrowDrag.active && T.find(hoverTipId) >= 0) {
    showTip(tipLines(*s, T.find(hoverTipId)));
  }
  ImGui::EndChild();
}

}  // namespace roadmap::ui
