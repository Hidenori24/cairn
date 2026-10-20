#include "roadmap/stats.hpp"

#include <algorithm>
#include <unordered_map>

#include "roadmap/text.hpp"

namespace roadmap {

namespace {

Day weekStart(Day d) { return d - (dow(d) + 6) % 7; }  // 月曜

void addTo(GroupStat& g, const std::vector<ScheduleRow>& R, int id) {
  const ScheduleRow& r = R[id];
  g.n++;
  g.weeks += r.weeks;
  if (r.done) {
    g.nDone++;
    g.weeksDone += r.weeks;
    return;
  }
  (r.prog ? g.nProg : g.nTodo)++;
  if (r.floatDays < 0) g.nLate++;
  else if (r.floatDays <= 10) g.nTight++;
  g.finish = g.hasFinish ? std::max(g.finish, r.asapFinish) : r.asapFinish;
  g.hasFinish = true;
  g.minFloat = g.hasOpen ? std::min(g.minFloat, r.floatDays) : r.floatDays;
  g.hasOpen = true;
}

}  // namespace

int floatBucket(int fl) {
  if (fl < 0) return 0;
  if (fl <= 5) return 1;
  if (fl <= 10) return 2;
  if (fl <= 20) return 3;
  return 4;
}

ProjectStats computeStats(const Tree& T, const std::vector<Issue>& issues, const std::vector<ScheduleRow>& R, Day base) {
  ProjectStats s;
  std::unordered_map<std::string, std::size_t> byName;
  const Day nowWeek = weekStart(base);

  for (int id : T.leafOrder) {
    const ScheduleRow& r = R[id];
    if (T.kind[id] == Kind::Ms) {
      s.milestones++;
      if (r.done) s.msDone++;
      else if (r.floatDays < 0) s.msLate++;
      continue;
    }
    s.tasks++;
    s.weeks += r.weeks;
    const int pr = prioRank(issues[id].prio);
    if (r.done) {
      s.done++;
      s.weeksDone += r.weeks;
      s.prioDone[pr]++;
    } else {
      (r.prog ? s.prog : s.todo)++;
      s.prioOpen[pr]++;
      if (r.floatDays < 0) s.late++;
      else if (r.floatDays <= 10) s.tight++;
      else s.ok++;
      s.floatHist[floatBucket(r.floatDays)]++;
      s.finish = s.hasFinish ? std::max(s.finish, r.asapFinish) : r.asapFinish;
      s.hasFinish = true;
      if (!T.dl[id]) s.noDeadline++;
    }
    const std::string who = text::trim(issues[id].assignee);
    if (!r.done && who.empty()) s.unassigned++;
    auto it = byName.find(who);
    if (it == byName.end()) {
      it = byName.emplace(who, s.byAssignee.size()).first;
      s.byAssignee.push_back({});
      s.byAssignee.back().name = who;
    }
    addTo(s.byAssignee[it->second], R, id);
  }
  // 未完了の週が多い順。未割当（名前が空）は最後
  std::stable_sort(s.byAssignee.begin(), s.byAssignee.end(), [](const GroupStat& a, const GroupStat& b) {
    if (a.name.empty() != b.name.empty()) return b.name.empty();
    return a.weeksOpen() > b.weeksOpen();
  });

  // ルート課題ごと
  GroupStat alone;
  alone.name = "親のない単独の課題";
  for (int r : T.roots) {
    if (T.kids[r].empty()) {
      if (T.kind[r] != Kind::Ms) addTo(alone, R, r);
      continue;
    }
    GroupStat g;
    g.name = T.did[r] + "  " + T.name[r];
    for (int id : T.leaves[r])
      if (T.kind[id] != Kind::Ms) addTo(g, R, id);
    if (g.n > 0) s.byRoot.push_back(std::move(g));
  }
  if (alone.n > 0) s.byRoot.push_back(std::move(alone));

  // 週ごとの推移: 過去 kPastWeeks 週 + 基準日の週 + 先
  int lastWeek = 0;  // 基準日の週からの週数（見込みの最後）
  for (int id : T.leafOrder)
    if (T.kind[id] != Kind::Ms && !R[id].done)
      lastWeek = std::max(lastWeek, (weekStart(R[id].asapFinish) - nowWeek) / 7);
  lastWeek = std::min(lastWeek, kFutureWeeks - 1);
  s.nowIndex = kPastWeeks;
  s.weeksTimeline.resize(static_cast<std::size_t>(kPastWeeks + 1 + lastWeek));
  for (std::size_t k = 0; k < s.weeksTimeline.size(); ++k)
    s.weeksTimeline[k].start = nowWeek + (static_cast<Day>(k) - kPastWeeks) * 7;
  for (int id : T.leafOrder) {
    if (T.kind[id] == Kind::Ms) continue;
    const ScheduleRow& r = R[id];
    if (r.done) {
      if (!issues[id].resolved) continue;
      const int k = (weekStart(*issues[id].resolved) - nowWeek) / 7 + kPastWeeks;  // 週の境は 7 の倍数
      if (k >= 0 && k < s.nowIndex) s.weeksTimeline[static_cast<std::size_t>(k)].actual++;
    } else {
      const int k = std::min((weekStart(r.asapFinish) - nowWeek) / 7, kFutureWeeks - 1);
      s.weeksTimeline[static_cast<std::size_t>(std::max(k, 0) + kPastWeeks)].forecast++;
    }
  }
  return s;
}

}  // namespace roadmap
