#include "roadmap/gantt_geom.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <utility>

#include "roadmap/dependency_view.hpp"
#include "roadmap/text.hpp"

namespace roadmap {

// ---------------------------------------------------------------- 時間軸
GanttDayRange ganttDayRange(const Tree& T, const std::vector<ScheduleRow>& R, Day base) {
  GanttDayRange g;
  g.lo = base;
  g.hi = base + 120;
  for (int id : T.leafOrder) {
    const ScheduleRow& r = R[static_cast<std::size_t>(id)];
    g.lo = std::min(g.lo, r.asapStart);
    g.hi = std::max({g.hi, r.asapFinish + 21, r.alapFinish + 21});
    if (T.dl[static_cast<std::size_t>(id)]) g.hi = std::max(g.hi, *T.dl[static_cast<std::size_t>(id)] + 21);
  }
  return g;
}

Day ganttAxisStart(Day lo) { return lo - ((dow(lo) + 6) % 7) - 7; }

float ganttZoomPx(std::string_view zoom, float scale, float fitAvail, int days) {
  if (zoom == "day") return 22.f * scale;
  if (zoom == "month") return 2.4f * scale;
  if (zoom == "all") return std::max(0.9f, std::min(12.f, fitAvail / static_cast<float>(std::max(1, days))));
  return 6.f * scale;
}

std::string ganttStyleZoom(std::string_view zoom, float customPx) {
  if (customPx > 0) return customPx >= 14 ? "day" : customPx >= 4 ? "week" : "month";
  return std::string(zoom);
}

float ganttHeaderH(std::string_view styleZoom, float scale) {
  return (styleZoom == "day" || styleZoom == "week") ? 46.f * scale : 30.f * scale;
}

int ganttWeekLabelEvery(float weekW, float scale) { return weekW < 26.f * scale ? 3 : weekW < 40.f * scale ? 2 : 1; }

// ---------------------------------------------------------------- 行
GanttSpan ganttTaskBar(const GanttAxis& ax, Day s0, Day f0) {
  GanttSpan s;
  s.xl = ax.x(s0);
  s.w = std::max(static_cast<float>(f0 - s0 + 1) * ax.px, 3.f);
  s.xr = s.xl + s.w;
  return s;
}

GanttSpan ganttSpan(const GanttAxis& ax, const Tree& T, const ScheduleRow& r, int id, bool isPar, float scale) {
  GanttSpan sp;
  if (isPar) {
    sp.xl = ax.x(r.asapStart);
    sp.w = std::max(ax.x(r.asapFinish + 1) - sp.xl, 3.f);
    sp.xr = sp.xl + sp.w;
  } else if (T.kind[static_cast<std::size_t>(id)] == Kind::Ms) {
    const float x = ax.x(r.asapFinish) + ax.px / 2;
    sp.xl = x - 7.f * scale;
    sp.xr = x + 7.f * scale;
    sp.w = sp.xr - sp.xl;
  } else {
    sp = ganttTaskBar(ax, r.asapStart, r.asapFinish);
  }
  return sp;
}

GanttRowShape ganttRowShape(const GanttAxis& ax, const Tree& T, const ScheduleRow& r, int id, bool isPar) {
  GanttRowShape s;
  const std::size_t k = static_cast<std::size_t>(id);
  if (isPar) {
    s.kind = GanttRowShape::Kind::Summary;
    const float x0 = ax.x(r.asapStart), x1 = ax.x(r.asapFinish + 1), xl = ax.x(std::max(r.alapFinish, r.asapFinish) + 1);
    s.x0 = x0;
    s.w = std::max(x1 - x0, 3.f);
    s.x1 = x0 + s.w;
    if (!r.done && xl > x1) {
      s.hasSlack = true;
      s.slackX0 = x1;
      s.slackX1 = xl;
    }
  } else if (T.kind[k] == Kind::Ms) {
    s.kind = GanttRowShape::Kind::Milestone;
    s.msX = ax.x(r.asapFinish) + ax.px / 2;
  } else {
    s.kind = GanttRowShape::Kind::Task;
    const GanttSpan b = ganttTaskBar(ax, r.asapStart, r.asapFinish);
    s.x0 = b.xl;
    s.x1 = b.xr;
    s.w = b.w;
    if (!r.done && r.alapFinish > r.asapFinish) {
      s.hasSlack = true;
      s.slackX0 = ax.x(r.asapFinish + 1);
      s.slackX1 = ax.x(r.alapFinish + 1);
    }
    if (!r.done && r.alapFinish < r.asapFinish) {
      s.hasOver = true;
      s.overX0 = ax.x(r.alapFinish + 1);
      s.overX1 = ax.x(r.asapFinish + 1);
    }
  }
  if (T.ownDl[k]) {
    s.hasDeadline = true;
    s.dlX = ax.x(*T.ownDl[k]) + ax.px / 2;
  }
  return s;
}

// ---------------------------------------------------------------- 依存の矢印
std::vector<int> ganttRowIndex(const Tree& T, const std::vector<VisibleRow>& rows) {
  std::vector<int> rowOf(static_cast<std::size_t>(T.n), -1);
  for (std::size_t i = 0; i < rows.size(); ++i) {
    const int id = rows[i].id;
    if (id >= 0 && id < T.n) rowOf[static_cast<std::size_t>(id)] = static_cast<int>(i);
  }
  return rowOf;
}

int ganttVisibleRow(const Tree& T, const std::vector<int>& rowOf, int leaf) {
  // 親をたどる回数に上限を置く（壊れたツリーでも止まる）
  int guard = 0;
  for (int x = leaf; x >= 0 && guard <= T.n; x = T.parentOf[static_cast<std::size_t>(x)], ++guard)
    if (rowOf[static_cast<std::size_t>(x)] >= 0) return rowOf[static_cast<std::size_t>(x)];
  return -1;
}

void ganttForEachPair(const Tree& T, const std::vector<ScheduleRow>& R, const std::vector<int>& rowOf, int first, int last,
                      const std::function<void(const GanttPair&)>& fn) {
  for (int leaf : T.leafOrder)
    for (int sc : T.succs[static_cast<std::size_t>(leaf)]) {
      const int ra = ganttVisibleRow(T, rowOf, leaf), rb = ganttVisibleRow(T, rowOf, sc);
      if (ra < 0 || rb < 0 || ra == rb) continue;
      if (std::max(ra, rb) < first || std::min(ra, rb) > last) continue;
      GanttPair p;
      p.leaf = leaf;
      p.succ = sc;
      p.ra = ra;
      p.rb = rb;
      const bool zw = T.kind[static_cast<std::size_t>(sc)] == Kind::Ms && R[static_cast<std::size_t>(sc)].weeks == 0;
      p.conflict = isDateConflict(R[static_cast<std::size_t>(leaf)], R[static_cast<std::size_t>(sc)], zw);
      p.critical = isCriticalLink(R[static_cast<std::size_t>(leaf)], R[static_cast<std::size_t>(sc)]);
      fn(p);
    }
}

std::vector<GanttLink> ganttLinks(const Tree& T, const std::vector<ScheduleRow>& R, const std::vector<int>& rowOf, int first,
                                  int last) {
  std::map<std::pair<int, int>, GanttLink> m;
  ganttForEachPair(T, R, rowOf, first, last, [&](const GanttPair& p) {
    auto [it, fresh] = m.try_emplace({p.ra, p.rb});
    GanttLink& l = it->second;
    if (fresh) {
      l.a = p.ra;
      l.b = p.rb;
      l.succLeaf = p.succ;
    }
    l.conflict = l.conflict || p.conflict;
    l.critical = l.critical || p.critical;
  });
  std::vector<GanttLink> out;
  out.reserve(m.size());
  for (auto& kv : m) out.push_back(kv.second);
  return out;
}

GanttArrowEnds ganttArrowEnds(const GanttAxis& ax, const Tree& T, const std::vector<ScheduleRow>& R, int ia, int ib, float cy0,
                              float cy1, float scale) {
  const ScheduleRow& ra = R[static_cast<std::size_t>(ia)];
  const ScheduleRow& rb = R[static_cast<std::size_t>(ib)];
  const bool msA = T.isLeaf(ia) && T.kind[static_cast<std::size_t>(ia)] == Kind::Ms;
  const bool msB = T.isLeaf(ib) && T.kind[static_cast<std::size_t>(ib)] == Kind::Ms;
  GanttArrowEnds e;
  e.y0 = cy0;
  e.y1 = cy1;
  e.x0 = msA ? ax.x(ra.asapFinish) + ax.px / 2 + 7.f * scale : ax.x(ra.asapFinish + 1);
  e.x1 = msB ? ax.x(rb.asapStart) + ax.px / 2 - 7.f * scale : ax.x(rb.asapStart);
  return e;
}

std::vector<GanttPoint> ganttArrowRoute(const GanttArrowEnds& e, float rowH, float scale) {
  const float kk = 6.f * scale;
  if (e.x1 >= e.x0 + 12.f * scale) return {{e.x0, e.y0}, {e.x0 + kk, e.y0}, {e.x0 + kk, e.y1}, {e.x1, e.y1}};
  const float ym = e.y0 + (e.y1 > e.y0 ? rowH / 2 : -rowH / 2);
  return {{e.x0, e.y0}, {e.x0 + kk, e.y0}, {e.x0 + kk, ym}, {e.x1 - kk, ym}, {e.x1 - kk, e.y1}, {e.x1, e.y1}};
}

// ---------------------------------------------------------------- 色
std::vector<int> rootIndexMap(const Tree& T, const std::vector<Issue>& issues) {
  std::vector<int> m(static_cast<std::size_t>(T.n), 0);
  int n = 0;
  for (int r : T.roots)
    m[static_cast<std::size_t>(r)] = (!T.kids[static_cast<std::size_t>(r)].empty() || isEpic(issues[static_cast<std::size_t>(r)])) ? (n++ % 7) + 1 : 0;
  for (int id : T.dfs) m[static_cast<std::size_t>(id)] = m[static_cast<std::size_t>(T.rootOf[static_cast<std::size_t>(id)])];
  return m;
}

unsigned nameHash(const std::string& s) {
  unsigned h = 0;
  for (unsigned char ch : s) h = h * 31u + ch;
  return h;
}

GanttColorBy ganttColorByFromKey(std::string_view key) {
  if (key == "float") return GanttColorBy::Float;
  if (key == "asg") return GanttColorBy::Assignee;
  return GanttColorBy::Root;
}

const char* ganttColorByKey(GanttColorBy by) {
  switch (by) {
    case GanttColorBy::Float: return "float";
    case GanttColorBy::Assignee: return "asg";
    default: return "root";
  }
}

GanttBarColor ganttBarColor(GanttColorBy by, Risk risk, int rootIdx, const std::string& assignee) {
  GanttBarColor c;
  if (by == GanttColorBy::Float) {
    switch (risk) {
      case Risk::Late: c.kind = GanttBarColor::Kind::Late; break;
      case Risk::Tight: c.kind = GanttBarColor::Kind::TightEdge; break;
      case Risk::Done: c.kind = GanttBarColor::Kind::Palette; c.index = 0; break;
      default: c.kind = GanttBarColor::Kind::Accent; break;
    }
    return c;
  }
  c.kind = GanttBarColor::Kind::Palette;
  if (by == GanttColorBy::Assignee) c.index = static_cast<int>(nameHash(text::trim(assignee)) % 8);
  else c.index = ((rootIdx % 8) + 8) % 8;
  return c;
}

}  // namespace roadmap
