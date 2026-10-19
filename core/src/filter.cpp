#include "roadmap/filter.hpp"

#include "roadmap/text.hpp"

namespace roadmap {

bool Filter::text_q_empty() const { return text::trim(q).empty(); }

ViewState viewStateFromUi(const Tree& T, const Json& ui) {
  ViewState vs;
  vs.collapsed.assign(T.n, 0);
  vs.hidden.assign(T.n, 0);
  const auto flags = [&](const char* key, std::vector<char>& out) {
    if (!ui.is_object() || !ui.contains(key) || !ui[key].is_object()) return;
    for (auto it = ui[key].begin(); it != ui[key].end(); ++it) {
      const int i = T.find(it.key());
      if (i >= 0 && it.value().is_boolean() && it.value().get<bool>()) out[i] = 1;
    }
  };
  flags("collapsed", vs.collapsed);
  flags("hide", vs.hidden);
  if (ui.is_object() && ui.contains("level") && ui["level"].is_number()) vs.level = ui["level"].get<int>();
  return vs;
}

std::vector<char> visSet(const Tree& T, const std::vector<Issue>& issues, const std::vector<ScheduleRow>& R,
                         const Filter& f) {
  const std::string q = text::toLower(text::trim(f.q));
  const auto textOk = [&](int id) {
    if (q.empty()) return true;
    const Issue& o = issues[id];
    const std::string hay = text::toLower(T.name[id] + " " + T.did[id] + " " + o.key + " " + text::join(o.labels, " ") +
                                          " " + o.assignee);
    return hay.find(q) != std::string::npos;
  };
  const auto leafOk = [&](int id, bool m) {
    const Issue& o = issues[id];
    const ScheduleRow& r = R[id];
    if (!m) return false;
    switch (f.asg) {
      case AsgFilter::Assigned:
        if (o.assignee.empty()) return false;
        break;
      case AsgFilter::Unassigned:
        if (!o.assignee.empty()) return false;
        break;
      case AsgFilter::Name:
        if (o.assignee != f.asgName) return false;
        break;
      default: break;
    }
    const Cat c = T.cat[id];
    switch (f.st) {
      case StFilter::Open:
        if (c == Cat::Done) return false;
        break;
      case StFilter::Todo:
        if (c != Cat::Todo) return false;
        break;
      case StFilter::Prog:
        if (c != Cat::Prog) return false;
        break;
      case StFilter::Done:
        if (c != Cat::Done) return false;
        break;
      default: break;
    }
    if (!f.pr.empty() && text::toLower(o.prio.empty() ? "Medium" : o.prio) != f.pr) return false;
    switch (f.flt) {
      case FltFilter::Late: return !r.done && r.floatDays < 0;
      case FltFilter::Tight: return !r.done && r.floatDays >= 0 && r.floatDays <= 10;
      case FltFilter::Done: return r.done;
      case FltFilter::Open: return !r.done;
      default: return true;
    }
  };
  std::vector<char> vis(T.n, 0);
  // 祖先が一致すれば配下は全部一致扱い（再帰を使わずに後順で）
  std::vector<char> m(T.n, 0);
  for (int id : T.dfs) {
    const int p = T.parentOf[id];
    m[id] = (p >= 0 && m[p]) || textOk(id);
  }
  for (auto it = T.dfs.rbegin(); it != T.dfs.rend(); ++it) {
    const int id = *it;
    if (T.kids[id].empty()) {
      vis[id] = leafOk(id, m[id] != 0);
    } else {
      bool any = false;
      for (int k : T.kids[id]) any |= vis[k] != 0;
      vis[id] = any;
    }
  }
  return vis;
}

std::vector<VisibleRow> visibleRows(const Tree& T, const std::vector<Issue>& issues, const std::vector<ScheduleRow>& R,
                                    const ViewState& vs, const Filter& f) {
  const auto vis = visSet(T, issues, R, f);
  const bool filtering = f.active();
  std::vector<VisibleRow> rows;
  std::vector<int> st;
  for (auto it = T.roots.rbegin(); it != T.roots.rend(); ++it) st.push_back(*it);
  while (!st.empty()) {
    const int id = st.back();
    st.pop_back();
    if (!vis[id]) continue;
    if (T.depth[id] == 0 && vs.hidden[id]) continue;
    const bool isPar = !T.kids[id].empty();
    const bool open = !vs.collapsed[id] || filtering;
    rows.push_back({id, T.depth[id], isPar, open});
    if (isPar && open && (!vs.level || T.depth[id] + 1 < vs.level))
      for (auto k = T.kids[id].rbegin(); k != T.kids[id].rend(); ++k) st.push_back(*k);
  }
  return rows;
}

std::vector<int> visibleLeaves(const Tree& T, const std::vector<Issue>& issues, const std::vector<ScheduleRow>& R,
                               const ViewState& vs, const Filter& f) {
  const auto vis = visSet(T, issues, R, f);
  std::vector<int> out;
  for (int id : T.dfs)
    if (T.kids[id].empty() && vis[id] && !vs.hidden[T.rootOf[id]]) out.push_back(id);
  return out;
}

}  // namespace roadmap
