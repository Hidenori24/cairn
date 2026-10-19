#include "roadmap/engine.hpp"

#include <algorithm>
#include <functional>
#include <limits>
#include <queue>
#include <tuple>
#include <unordered_set>

#include "roadmap/text.hpp"

namespace roadmap {

namespace {
std::string statOf(int fl) { return fl < 0 ? "遅れ" : (fl <= 10 ? "余裕小" : "OK"); }
}  // namespace

Risk riskOf(int fl, bool done) {
  if (done) return Risk::Done;
  if (fl < 0) return Risk::Late;
  if (fl <= 10) return Risk::Tight;
  return Risk::Ok;
}

std::string floatText(const ScheduleRow& r) {
  if (r.done) return "完了";
  if (r.floatDays < 0) {
    const double w = std::max(1.0, text::jsRound(std::abs(r.floatDays) / 5.0));
    return "−" + text::jsNumber(w) + "週";
  }
  return std::to_string(r.floatDays) + "日";
}

Result<Tree> buildTree(const std::vector<Issue>& issues, const ProjectMeta& meta) {
  Tree T;
  const int n = static_cast<int>(issues.size());
  T.n = n;
  T.ids.resize(n);
  T.kids.assign(n, {});
  T.parentOf.assign(n, -1);
  T.depth.assign(n, 0);
  T.rootOf.assign(n, -1);
  T.leaves.assign(n, {});
  T.dfsPos.assign(n, -1);
  T.did.resize(n);
  T.name.resize(n);
  T.cat.resize(n);
  T.baseWeeks.assign(n, 0);
  T.kind.assign(n, Kind::Task);
  T.preds.assign(n, {});
  T.succs.assign(n, {});
  T.ownDl.assign(n, std::nullopt);
  T.dl.assign(n, std::nullopt);
  T.byId.reserve(static_cast<std::size_t>(n) * 2);
  for (int i = 0; i < n; ++i) {
    const Issue& o = issues[i];
    T.ids[i] = o.id;
    T.byId[o.id] = i;
    const TagName t = tagOf(o.summary);
    T.did[i] = !t.tag.empty() ? t.tag : (!o.key.empty() ? o.key : o.id);
    T.name[i] = !t.name.empty() ? t.name : o.summary;
    T.cat[i] = statusCat(o.status);
  }
  for (int i = 0; i < n; ++i) {
    const Issue& o = issues[i];
    const int p = o.parent.empty() ? -1 : T.find(o.parent);
    if (p >= 0 && o.parent != o.id) {
      T.kids[p].push_back(i);
      T.parentOf[i] = p;
    } else {
      T.roots.push_back(i);
    }
  }
  // 親ループの検出
  {
    std::vector<int> mark(n, -1);
    for (int i = 0; i < n; ++i) {
      int p = i;
      while (p >= 0) {
        if (mark[p] == i) return Error{"親子関係がループしています: " + T.did[p]};
        mark[p] = i;
        p = T.parentOf[p];
      }
    }
  }
  // DFS 順・深さ・ルート・葉（再帰を使わない）
  {
    struct Frame {
      int id;
      std::size_t k;
    };
    std::vector<Frame> st;
    for (int r : T.roots) {
      st.push_back({r, 0});
      T.depth[r] = 0;
      T.rootOf[r] = r;
      T.dfsPos[r] = static_cast<int>(T.dfs.size());
      T.dfs.push_back(r);
      while (!st.empty()) {
        Frame& f = st.back();
        if (f.k < T.kids[f.id].size()) {
          const int c = T.kids[f.id][f.k++];
          T.depth[c] = T.depth[f.id] + 1;
          T.rootOf[c] = r;
          T.dfsPos[c] = static_cast<int>(T.dfs.size());
          T.dfs.push_back(c);
          st.push_back({c, 0});
        } else {
          const int id = f.id;
          st.pop_back();
          if (T.kids[id].empty()) {
            T.leaves[id] = {id};
          } else {
            auto& L = T.leaves[id];
            for (int c : T.kids[id]) L.insert(L.end(), T.leaves[c].begin(), T.leaves[c].end());
          }
        }
      }
    }
  }
  // 期間（週）
  std::vector<std::string> missing;
  for (int id : T.dfs) {
    if (!T.kids[id].empty()) continue;
    const Issue& o = issues[id];
    std::optional<double> w = o.durW;
    if (!w) w = o.estW;
    if (!w && o.start && o.due && *o.due >= *o.start)
      w = std::max(1, Calendar::weekdaysBetween(*o.start, *o.due)) / 5.0;
    if (!w) {
      w = 1;
      missing.push_back(T.did[id]);
    }
    T.baseWeeks[id] = *w;
    T.kind[id] = (*w == 0 || isMilestone(o)) ? Kind::Ms : Kind::Task;
  }
  if (!missing.empty()) {
    std::vector<std::string> head(missing.begin(), missing.begin() + std::min<std::size_t>(10, missing.size()));
    std::string w = "期間が読み取れず1週と仮置き: " + text::join(head, ", ");
    if (missing.size() > 10) w += " ほか" + std::to_string(missing.size() - 10) + "件";
    T.warns.push_back(std::move(w));
  }
  // 依存関係（葉どうしに展開）
  {
    int overlap = 0;
    std::unordered_set<long long> edges;
    for (int a = 0; a < n; ++a) {
      for (const auto& bid : issues[a].blocks) {
        const int b = T.find(bid);
        if (b < 0) continue;
        for (int la : T.leaves[a]) {
          for (int lb : T.leaves[b]) {
            if (la == lb) {
              ++overlap;
              continue;
            }
            const long long key = static_cast<long long>(la) * n + lb;
            if (edges.insert(key).second) {
              T.preds[lb].push_back(la);
              T.succs[la].push_back(lb);
            }
          }
        }
      }
    }
    if (overlap) T.warns.push_back("親子の間（または同じ配下）の依存関係は無視しました");
  }
  // トポロジカル順（取り出せるものが複数あれば dfs 順の位置が小さいものから）
  std::vector<int> leafIds;
  for (int id : T.dfs)
    if (T.kids[id].empty()) leafIds.push_back(id);
  {
    std::vector<int> indeg(n, 0);
    for (int id : leafIds) indeg[id] = static_cast<int>(T.preds[id].size());
    std::priority_queue<std::pair<int, int>, std::vector<std::pair<int, int>>, std::greater<>> q;
    for (int id : leafIds)
      if (indeg[id] == 0) q.emplace(T.dfsPos[id], id);
    while (!q.empty()) {
      const int x = q.top().second;
      q.pop();
      T.leafOrder.push_back(x);
      for (int s : T.succs[x])
        if (--indeg[s] == 0) q.emplace(T.dfsPos[s], s);
    }
    if (T.leafOrder.size() != leafIds.size()) {
      std::vector<bool> in(n, false);
      for (int x : T.leafOrder) in[x] = true;
      std::vector<std::string> bad;
      for (int id : leafIds)
        if (!in[id]) bad.push_back(T.did[id]);
      std::vector<std::string> head(bad.begin(), bad.begin() + std::min<std::size_t>(6, bad.size()));
      return Error{"先行・後続の関係がループしています: " + text::join(head, ", ") + (bad.size() > 6 ? " ほか" : "")};
    }
  }
  // 期限（自身の明示期限と、祖先からの継承）
  const bool hasCol = meta.hasDeadlineCol;
  for (int id : T.dfs) {
    const Issue& o = issues[id];
    if (hasCol) T.ownDl[id] = o.deadline;
    else if (T.kids[id].empty() && T.succs[id].empty()) T.ownDl[id] = o.due;
  }
  for (int id : leafIds) {
    OptDay best = T.ownDl[id];
    if (hasCol)
      for (int p = T.parentOf[id]; p >= 0; p = T.parentOf[p]) {
        const OptDay& d = T.ownDl[p];
        if (d && (!best || *d < *best)) best = d;
      }
    T.dl[id] = best;
  }
  {
    bool anyDl = false;
    int noDl = 0;
    for (int id : leafIds) {
      if (T.dl[id] && T.cat[id] != Cat::Done) anyDl = true;
      if (T.succs[id].empty() && !T.dl[id] && T.cat[id] != Cat::Done) ++noDl;
    }
    if (noDl)
      T.warns.push_back(anyDl ? "期限が無い末端の課題" + std::to_string(noDl) + "件は、最も遅い期限に合わせています"
                              : "期限（Deadline）が1件も無いため、余裕は0として表示します。期限を入れると逆算できます");
  }
  T.scenarios.push_back(kStdScenario);
  const auto addScn = [&](const std::string& s) {
    if (std::find(T.scenarios.begin(), T.scenarios.end(), s) == T.scenarios.end()) T.scenarios.push_back(s);
  };
  for (const auto& s : meta.scenarios) addScn(s);
  for (const auto& o : issues)
    for (const auto& [s, v] : o.alt.items()) addScn(s);
  return T;
}

std::vector<ScheduleRow> computeSchedule(const Tree& T, const std::vector<Issue>& issues, const Calendar& cal,
                                         const ScheduleOptions& opt) {
  const int n = T.n;
  const Day base = opt.base;
  const Day first = cal.workday(base - 1, 1);
  const bool stdScn = opt.scenario == kStdScenario;
  std::vector<int> days(n, 0);
  std::vector<double> wk(n, 0);
  std::vector<char> zw(n, 0), pinned(n, 0);
  std::vector<std::pair<Day, Day>> asap(n), freeR(n), alap(n);

  const auto wOf = [&](int id) {
    if (!stdScn)
      if (const double* v = issues[id].alt.find(opt.scenario)) return *v;
    return T.baseWeeks[id];
  };
  const auto startOf = [&](int id) {
    const Issue& o = issues[id];
    Day s0 = first;
    if (o.notBefore) s0 = std::max(s0, cal.isWork(*o.notBefore) ? *o.notBefore : cal.workday(*o.notBefore, 1));
    if (!T.preds[id].empty()) {
      Day pm = std::numeric_limits<Day>::min();
      for (int p : T.preds[id]) pm = std::max(pm, asap[p].second);
      s0 = std::max(s0, zw[id] ? pm : cal.workday(pm, 1));
    }
    return s0;
  };
  // 1) 先行関係だけで決まる最早
  for (int id : T.leafOrder) {
    const Issue& o = issues[id];
    const Cat cat = T.cat[id];
    wk[id] = wOf(id);
    days[id] = std::max(static_cast<int>(text::jsRound(wk[id] * 5)), 1);
    zw[id] = T.kind[id] == Kind::Ms && wk[id] == 0;
    Day s, f;
    if (cat == Cat::Done) {
      f = o.resolved ? *o.resolved : (o.due ? *o.due : cal.workday(first, -1));
      s = o.start ? *o.start : f;
      if (s > f) s = f;
      pinned[id] = 1;
    } else if (cat == Cat::Prog && o.start && *o.start <= base) {
      s = *o.start;
      f = o.remW ? cal.workday(first, std::max(static_cast<int>(text::jsRound(*o.remW * 5)), 1) - 1)
                 : std::max(cal.workday(s, days[id] - 1), first);
      pinned[id] = 1;
    } else {
      s = startOf(id);
      f = zw[id] ? s : cal.workday(s, days[id] - 1);
    }
    asap[id] = {s, f};
    freeR[id] = {s, f};
  }
  // 2) 期限から逆算した最遅
  OptDay projEnd;
  for (int id : T.leafOrder)
    if (T.dl[id] && T.cat[id] != Cat::Done) projEnd = projEnd ? std::max(*projEnd, *T.dl[id]) : *T.dl[id];
  for (auto it = T.leafOrder.rbegin(); it != T.leafOrder.rend(); ++it) {
    const int id = *it;
    if (T.cat[id] == Cat::Done) {
      alap[id] = asap[id];
      continue;
    }
    bool have = false;
    Day f2 = 0;
    const auto push = [&](Day v) {
      f2 = have ? std::min(f2, v) : v;
      have = true;
    };
    if (T.dl[id]) push(cal.lastWork(*T.dl[id]));
    for (int s : T.succs[id])
      if (T.cat[s] != Cat::Done) push(zw[s] ? alap[s].first : cal.workday(alap[s].first, -1));
    if (!have) f2 = projEnd ? cal.lastWork(*projEnd) : asap[id].second;
    alap[id] = {zw[id] ? f2 : cal.workday(f2, -(days[id] - 1)), f2};
  }
  // 3) 担当者ごとの同時作業数の制限
  if (opt.resource.on) {
    const int cap = std::max(1, opt.resource.cap);
    std::unordered_map<std::string, std::vector<std::pair<Day, Day>>> busy;
    std::vector<char> done3(n, 0);
    std::vector<std::string> whoOf(n);
    for (int id : T.leafOrder) whoOf[id] = text::trim(issues[id].assignee);
    const auto useRes = [&](int id) {
      return !whoOf[id].empty() && T.kind[id] != Kind::Ms && T.cat[id] != Cat::Done;
    };
    for (int id : T.leafOrder)
      if (pinned[id]) {
        done3[id] = 1;
        if (useRes(id)) busy[whoOf[id]].push_back(asap[id]);
      }
    const auto fit = [&](const std::string& person, Day est, int d) {
      const auto& list = busy[person];
      Day t = est;
      std::vector<std::pair<Day, Day>> ov, act;
      for (int guard = 0; guard < 5000; ++guard) {
        const Day f = cal.workday(t, d - 1);
        ov.clear();
        for (const auto& b : list)
          if (b.first <= f && b.second >= t) ov.push_back(b);
        if (static_cast<int>(ov.size()) < cap) return t;
        std::vector<Day> pts{t};
        for (const auto& b : ov)
          if (b.first > t) pts.push_back(b.first);
        bool bad = false;
        for (std::size_t q = 0; q < pts.size() && !bad; ++q) {
          act.clear();
          for (const auto& b : ov)
            if (b.first <= pts[q] && b.second >= pts[q]) act.push_back(b);
          if (static_cast<int>(act.size()) >= cap) bad = true;
        }
        if (!bad) return t;
        Day mn = act[0].second;
        for (const auto& b : act) mn = std::min(mn, b.second);
        t = cal.workday(mn, 1);
      }
      return t;
    };
    std::vector<int> k(n, 0), remain(n, 0);
    for (std::size_t i = 0; i < T.leafOrder.size(); ++i) k[T.leafOrder[i]] = static_cast<int>(i);
    // eligible = 未確定で、先行がすべて確定済み。最遅開始が早い順（同じなら leafOrder の位置）
    using Key = std::tuple<Day, int, int>;
    std::priority_queue<Key, std::vector<Key>, std::greater<>> elig;
    for (int id : T.leafOrder) {
      if (done3[id]) continue;
      int r = 0;
      for (int p : T.preds[id])
        if (!done3[p]) ++r;
      remain[id] = r;
      if (r == 0) elig.emplace(alap[id].first, k[id], id);
    }
    while (!elig.empty()) {
      const int pick = std::get<2>(elig.top());
      elig.pop();
      const Day est = startOf(pick);
      Day st = est;
      if (useRes(pick)) {
        st = fit(whoOf[pick], est, days[pick]);
        busy[whoOf[pick]].emplace_back(st, cal.workday(st, days[pick] - 1));
      }
      asap[pick] = {st, zw[pick] ? st : cal.workday(st, days[pick] - 1)};
      done3[pick] = 1;
      for (int s : T.succs[pick])
        if (!done3[s] && --remain[s] == 0) elig.emplace(alap[s].first, k[s], s);
    }
  }
  std::vector<ScheduleRow> R(n);
  for (int id : T.leafOrder) {
    const bool done = T.cat[id] == Cat::Done;
    const int fl = done ? 0 : cal.floatDays(asap[id].second, alap[id].second);
    ScheduleRow& r = R[id];
    r.leaf = true;
    r.done = done;
    r.prog = T.cat[id] == Cat::Prog;
    r.weeks = wk[id];
    r.days = days[id];
    r.asapStart = asap[id].first;
    r.asapFinish = asap[id].second;
    r.freeStart = freeR[id].first;
    r.waited = !done && asap[id].first > freeR[id].first;
    r.pinned = pinned[id] != 0;
    r.alapStart = alap[id].first;
    r.alapFinish = alap[id].second;
    r.floatDays = fl;
    r.status = done ? "完了" : statOf(fl);
    r.n = 1;
    r.nDone = done ? 1 : 0;
  }
  // 親のまとめ（深い方から）
  for (auto it = T.dfs.rbegin(); it != T.dfs.rend(); ++it) {
    const int id = *it;
    if (T.kids[id].empty()) continue;
    const auto& L = T.leaves[id];
    ScheduleRow& r = R[id];
    int nAct = 0, minF = 0;
    bool anyProg = false, firstF = true;
    r.asapStart = r.alapStart = std::numeric_limits<Day>::max();
    r.asapFinish = r.alapFinish = std::numeric_limits<Day>::min();
    for (int x : L) {
      const ScheduleRow& c = R[x];
      if (!c.done) {
        ++nAct;
        anyProg |= c.prog;
        minF = firstF ? c.floatDays : std::min(minF, c.floatDays);
        firstF = false;
      }
      r.asapStart = std::min(r.asapStart, c.asapStart);
      r.asapFinish = std::max(r.asapFinish, c.asapFinish);
      r.alapStart = std::min(r.alapStart, c.alapStart);
      r.alapFinish = std::max(r.alapFinish, c.alapFinish);
    }
    const int nDone = static_cast<int>(L.size()) - nAct;
    r.leaf = false;
    r.done = nAct == 0;
    r.prog = nAct > 0 && (nDone > 0 || anyProg);
    r.floatDays = nAct ? minF : 0;
    r.status = nAct ? statOf(minF) : "完了";
    r.n = static_cast<int>(L.size());
    r.nDone = nDone;
  }
  return R;
}

std::vector<Workload> workload(const Tree& T, const std::vector<Issue>& issues, const std::vector<ScheduleRow>& R,
                               const Calendar& cal) {
  std::vector<Workload> by;
  std::unordered_map<std::string, std::size_t> idx;
  for (int id : T.leafOrder) {
    const std::string w = text::trim(issues[id].assignee);
    const ScheduleRow& r = R[id];
    if (w.empty() || r.done || T.kind[id] == Kind::Ms) continue;
    auto it = idx.find(w);
    if (it == idx.end()) {
      it = idx.emplace(w, by.size()).first;
      by.push_back({w, 0, 0, 0, {}, 0, 0});
    }
    Workload& x = by[it->second];
    x.n++;
    x.weeks += r.weeks;
    x.last = std::max(x.last, r.asapFinish);
    x.ids.push_back(id);
    if (r.waited) x.wait++;
  }
  for (auto& x : by) {
    int peak = 0;
    for (int pid : x.ids) {
      const Day p = R[pid].freeStart;
      int c = 0;
      for (int id : x.ids)
        if (R[id].freeStart <= p && cal.workday(R[id].freeStart, std::max(R[id].days, 1) - 1) >= p) ++c;
      peak = std::max(peak, c);
    }
    x.peak = peak;
  }
  std::stable_sort(by.begin(), by.end(), [](const Workload& a, const Workload& b) { return b.weeks < a.weeks; });
  return by;
}

Summary summarize(const Tree& T, const std::vector<ScheduleRow>& R) {
  Summary s;
  for (int id : T.leafOrder) {
    const ScheduleRow& r = R[id];
    s.total++;
    if (r.done) s.done++;
    else if (r.floatDays < 0) s.late++;
    else if (r.floatDays <= 10) s.tight++;
  }
  return s;
}

}  // namespace roadmap
