// 週次レポートの組み立て（構造体を作る）。文字にするのは weekly_md.cpp・weekly_html.cpp
#include "roadmap/weekly.hpp"

#include <algorithm>
#include <cstdint>
#include <map>
#include <unordered_map>
#include <unordered_set>

#include <cmath>

#include "roadmap/report.hpp"
#include "roadmap/text.hpp"
#include "weekly_internal.hpp"

namespace roadmap {

WeeklyRange weeklyRange(Day base, WeeklyPeriod p, Day from, Day to) {
  const Day mon = weeklyMondayOf(base);
  switch (p) {
    case WeeklyPeriod::ThisWeek: return {mon, mon + 6};
    case WeeklyPeriod::LastWeek: return {mon - 7, mon - 1};
    case WeeklyPeriod::Custom: break;
  }
  if (from > to) std::swap(from, to);
  const std::int64_t len = static_cast<std::int64_t>(to) - from + 1;
  if (len > kWeeklyMaxRangeDays) to = from + kWeeklyMaxRangeDays - 1;
  return {from, to};
}

namespace {

bool inRange(Day d, const WeeklyRange& r) { return d >= r.from && d <= r.to; }

int countWorkdays(const Calendar& cal, const WeeklyRange& r) {
  int n = 0;
  for (Day d = r.from; d <= r.to; ++d)
    if (cal.isWork(d)) ++n;
  return n;
}

// 並べ替えの鍵つきの添字
struct Cand {
  int idx;
  std::int64_t key;
};

void sortCands(std::vector<Cand>& v, const Tree& T) {
  std::stable_sort(v.begin(), v.end(), [&](const Cand& a, const Cand& b) {
    if (a.key != b.key) return a.key < b.key;
    return T.dfsPos[static_cast<std::size_t>(a.idx)] < T.dfsPos[static_cast<std::size_t>(b.idx)];
  });
}

}  // namespace

WeeklyReport buildWeeklyReport(const WeeklyInput& in, const WeeklyOptions& opt) {
  const Tree& T = in.tree;
  const Calendar& cal = in.calendar;
  WeeklyReport r;
  r.projectName = cleanText(in.projectName);
  r.base = in.base;
  r.period = opt.period;
  r.sections = opt.sections;
  r.notes = weeklySanitizeNotes(opt.notes);
  r.range = weeklyRange(in.base, opt.period, opt.from, opt.to);
  if (opt.period == WeeklyPeriod::Custom) {
    const Day lo = std::min(opt.from, opt.to), hi = std::max(opt.from, opt.to);
    r.rangeCapped = static_cast<std::int64_t>(hi) - lo + 1 > kWeeklyMaxRangeDays;
  }
  r.next = {r.range.to + 1, r.range.to + 7};
  r.rangeWorkdays = countWorkdays(cal, r.range);
  r.nextWorkdays = countWorkdays(cal, r.next);

  const std::size_t n = T.n > 0 ? static_cast<std::size_t>(T.n) : 0;
  if (n == 0 || in.issues.size() != n || in.rows.size() != n || T.dfs.size() != n || T.dfsPos.size() != n || T.kids.size() != n ||
      T.did.size() != n || T.name.size() != n || T.kind.size() != n || T.dl.size() != n || T.preds.size() != n || T.ids.size() != n) {
    r.empty = true;
    return r;
  }
  const std::size_t cap = std::max<std::size_t>(1, opt.maxItems);
  const auto inTree = [&](int id) { return id >= 0 && static_cast<std::size_t>(id) < n; };
  const auto assigneeOf = [&](int i) { return text::trim(in.issues[static_cast<std::size_t>(i)].assignee); };

  const auto makeItem = [&](int i) {
    const std::size_t k = static_cast<std::size_t>(i);
    const ScheduleRow& row = in.rows[k];
    WeeklyItem it;
    it.index = i;
    it.did = cleanText(T.did[k]);
    it.name = cleanText(T.name[k]);
    it.assignee = cleanText(assigneeOf(i));
    it.start = row.asapStart;
    it.finish = row.asapFinish;
    it.resolved = in.issues[k].resolved;
    it.deadline = T.dl[k];
    it.done = row.done;
    it.prog = row.prog;
    it.floatDays = row.floatDays;
    it.remainDays = row.asapFinish >= in.base ? cal.nd(in.base, row.asapFinish) : 0;
    return it;
  };
  const auto fill = [&](WeeklyList& list, std::vector<Cand>& c) {
    sortCands(c, T);
    list.total = static_cast<int>(c.size());
    for (std::size_t k = 0; k < c.size() && k < cap; ++k) list.items.push_back(makeItem(c[k].idx));
  };
  // 遅れ・余裕小の課題に、開始を決めている先行（未完了のうち最早の完了が一番遅いもの）を付ける
  const auto attachPred = [&](WeeklyItem& it) {
    int best = -1;
    for (int p : T.preds[static_cast<std::size_t>(it.index)]) {
      if (!inTree(p) || in.rows[static_cast<std::size_t>(p)].done) continue;
      if (best < 0) {
        best = p;
        continue;
      }
      const ScheduleRow& a = in.rows[static_cast<std::size_t>(p)];
      const ScheduleRow& b = in.rows[static_cast<std::size_t>(best)];
      if (a.asapFinish > b.asapFinish || (a.asapFinish == b.asapFinish && a.floatDays < b.floatDays)) best = p;
    }
    if (best < 0) return;
    const std::size_t k = static_cast<std::size_t>(best);
    it.predIndex = best;
    it.predDid = cleanText(T.did[k]);
    it.predName = cleanText(T.name[k]);
    it.predFinish = in.rows[k].asapFinish;
    it.predFloat = in.rows[k].floatDays;
  };

  std::vector<Cand> cDone, cProg, cStart, cFinish, cRisk;
  std::vector<Cand> cMs;  // key = 並べ替えの鍵
  struct Acc {
    int doneInPeriod = 0, inProgress = 0, open = 0, late = 0;
  };
  std::map<std::string, Acc> byWho;  // 名前順（std::map の順 = バイト順。順位にしない）

  for (int i : T.dfs) {
    if (!inTree(i)) continue;
    const std::size_t k = static_cast<std::size_t>(i);
    if (!T.kids[k].empty()) continue;  // 葉だけ
    const ScheduleRow& row = in.rows[k];
    const Issue& iss = in.issues[k];
    if (T.kind[k] == Kind::Ms) {
      const bool achievedInPeriod = row.done && iss.resolved && inRange(*iss.resolved, r.range);
      if (achievedInPeriod) cMs.push_back({i, static_cast<std::int64_t>(*iss.resolved) - 1000000000LL});  // 達成は先に
      else if (!row.done) cMs.push_back({i, static_cast<std::int64_t>(T.dl[k] ? *T.dl[k] : row.asapFinish)});
      continue;
    }
    Acc& acc = byWho[assigneeOf(i)];
    if (row.done) {
      if (iss.resolved) {
        if (inRange(*iss.resolved, r.range)) {
          cDone.push_back({i, *iss.resolved});
          acc.doneInPeriod++;
        }
      } else {
        r.summary.doneNoDate++;
      }
      continue;
    }
    acc.open++;
    if (row.floatDays < 0) acc.late++;
    if (row.prog) {
      acc.inProgress++;
      cProg.push_back({i, row.floatDays});
    }
    if (inRange(row.asapStart, r.next)) cStart.push_back({i, row.asapStart});
    if (inRange(row.asapFinish, r.next)) cFinish.push_back({i, row.asapFinish});
    if (row.floatDays <= kWeeklyTightDays) cRisk.push_back({i, row.floatDays});
  }
  fill(r.done, cDone);
  fill(r.progress, cProg);
  fill(r.nextStart, cStart);
  fill(r.nextFinish, cFinish);
  fill(r.risks, cRisk);
  for (WeeklyItem& it : r.risks.items) attachPred(it);

  // ---- サマリー ----
  const ProjectStats& st = in.stats;
  WeeklySummary& sm = r.summary;
  sm.progress = std::max(0.0, std::min(1.0, st.progress()));
  sm.tasks = st.tasks;
  sm.done = st.done;
  sm.prog = st.prog;
  sm.todo = st.todo;
  sm.late = st.late;
  sm.tight = st.tight;
  sm.msDone = st.msDone;
  sm.msLate = st.msLate;
  sm.milestones = st.milestones;
  sm.hasFinish = st.hasFinish;
  sm.finish = st.finish;
  sm.doneInPeriod = r.done.total;

  // ---- マイルストーン ----
  sortCands(cMs, T);
  r.milestonesTotal = static_cast<int>(cMs.size());
  for (std::size_t q = 0; q < cMs.size() && q < cap; ++q) {
    const int i = cMs[q].idx;
    const std::size_t k = static_cast<std::size_t>(i);
    const ScheduleRow& row = in.rows[k];
    WeeklyMilestone m;
    m.index = i;
    m.did = cleanText(T.did[k]);
    m.name = cleanText(T.name[k]);
    m.achieved = row.done;
    m.resolved = in.issues[k].resolved;
    m.deadline = T.dl[k];
    m.forecast = (row.done && m.resolved) ? *m.resolved : row.asapFinish;
    if (m.deadline) m.diffDays = cal.floatDays(m.forecast, *m.deadline);
    r.milestones.push_back(std::move(m));
  }

  // ---- 担当別（名前順。未割当は最後。順位にしない）----
  const auto weekIdx = [&]() -> int {
    const Day w = weeklyMondayOf(r.next.from);
    for (std::size_t q = 0; q < in.load.weekStarts.size(); ++q)
      if (in.load.weekStarts[q] == w) return static_cast<int>(q);
    return -1;
  }();
  std::vector<std::string> names;
  for (const auto& kv : byWho)
    if (!kv.first.empty()) names.push_back(kv.first);
  if (byWho.count(std::string()) != 0) names.push_back(std::string());
  r.assigneesTotal = static_cast<int>(names.size());
  for (std::size_t q = 0; q < names.size() && q < cap; ++q) {
    const Acc& a = byWho[names[q]];
    WeeklyAssigneeRow row;
    row.name = cleanText(names[q]);
    row.doneInPeriod = a.doneInPeriod;
    row.inProgress = a.inProgress;
    row.open = a.open;
    row.late = a.late;
    if (weekIdx >= 0) {
      row.loadNextPercent = 0;
      for (const LoadRow& lr : in.load.load) {
        if (text::trim(lr.name) != names[q]) continue;
        if (static_cast<std::size_t>(weekIdx) < lr.cells.size()) row.loadNextPercent = loadPercent(lr.cells[static_cast<std::size_t>(weekIdx)].ratio);
        break;
      }
    }
    r.assignees.push_back(std::move(row));
  }

  // ---- 先週のスナップショットとの比較 ----
  if (const WeeklySnapshot* prev = in.prev) {
    WeeklyChanges& ch = r.changes;
    ch.available = true;
    ch.since = prev->base;
    ch.truncated = prev->truncated;
    sm.hasPrev = true;
    sm.prevBase = prev->base;
    sm.progressDelta = sm.progress - prev->progress;
    sm.lateDelta = st.late - prev->late;
    sm.tightDelta = st.tight - prev->tight;
    sm.doneDelta = st.done - prev->done;
    if (prev->finish && st.hasFinish) {
      sm.hasFinishShift = true;
      sm.finishShiftDays = cal.floatDays(*prev->finish, st.finish);
    }

    std::unordered_map<std::string, const WeeklySnapItem*> before;
    before.reserve(prev->items.size());
    for (const WeeklySnapItem& it : prev->items) before.emplace(it.id, &it);

    std::vector<Cand> cLate, cDoneNew;
    std::vector<Cand> cMove;
    std::unordered_set<std::string> seenNow;
    int added = 0;
    for (int i : T.dfs) {
      if (!inTree(i)) continue;
      const std::size_t k = static_cast<std::size_t>(i);
      if (!T.kids[k].empty() || T.kind[k] == Kind::Ms) continue;
      const ScheduleRow& row = in.rows[k];
      const auto f = before.find(T.ids[k]);
      if (f == before.end()) {
        ++added;
        continue;
      }
      seenNow.insert(T.ids[k]);
      const WeeklySnapItem& was = *f->second;
      if (row.done) {
        if (was.st != 'd') cDoneNew.push_back({i, in.issues[k].resolved ? *in.issues[k].resolved : 0});
        continue;
      }
      if (row.floatDays < 0 && !was.late) cLate.push_back({i, row.floatDays});
      if (was.st != 'd' && was.finish && *was.finish != row.asapFinish) {
        const int shift = cal.floatDays(*was.finish, row.asapFinish);
        if (shift != 0) {
          const int mag = shift < 0 ? -shift : shift;
          cMove.push_back({i, -static_cast<std::int64_t>(mag)});  // 動きの大きい順
        }
      }
    }
    ch.added = prev->truncated ? 0 : added;
    int removed = 0;
    for (const WeeklySnapItem& it : prev->items)
      if (seenNow.find(it.id) == seenNow.end()) ++removed;
    ch.removed = removed;
    fill(ch.newlyLate, cLate);
    fill(ch.newlyDone, cDoneNew);
    // 動き: 鍵の同点は dfs の順（sortCands）。shift は添字から引き直す
    sortCands(cMove, T);
    ch.finishMovedTotal = static_cast<int>(cMove.size());
    for (std::size_t q = 0; q < cMove.size() && q < cap; ++q) {
      const int i = cMove[q].idx;
      const std::size_t k = static_cast<std::size_t>(i);
      const auto f = before.find(T.ids[k]);
      WeeklyFinishMove mv;
      mv.index = i;
      mv.did = cleanText(T.did[k]);
      mv.name = cleanText(T.name[k]);
      mv.assignee = cleanText(assigneeOf(i));
      mv.from = f != before.end() && f->second->finish ? *f->second->finish : in.rows[k].asapFinish;
      mv.to = in.rows[k].asapFinish;
      mv.shiftDays = cal.floatDays(mv.from, mv.to);
      ch.finishMoved.push_back(std::move(mv));
    }
  }
  return r;
}

WeeklySnapshot makeWeeklySnapshot(const WeeklyInput& in) {
  const Tree& T = in.tree;
  WeeklySnapshot s;
  s.base = in.base;
  const ProjectStats& st = in.stats;
  s.progress = std::max(0.0, std::min(1.0, st.progress()));
  s.tasks = st.tasks;
  s.done = st.done;
  s.late = st.late;
  s.tight = st.tight;
  if (st.hasFinish) s.finish = st.finish;
  const std::size_t n = T.n > 0 ? static_cast<std::size_t>(T.n) : 0;
  if (n == 0 || in.issues.size() != n || in.rows.size() != n || T.dfs.size() != n || T.kids.size() != n || T.kind.size() != n ||
      T.ids.size() != n)
    return s;
  for (int i : T.dfs) {
    if (i < 0 || static_cast<std::size_t>(i) >= n) continue;
    const std::size_t k = static_cast<std::size_t>(i);
    if (!T.kids[k].empty() || T.kind[k] == Kind::Ms) continue;
    if (T.ids[k].empty() || T.ids[k].size() > kWeeklyMaxIdBytes) continue;
    if (s.items.size() >= kWeeklyMaxSnapshotItems) {
      s.truncated = true;
      break;
    }
    const ScheduleRow& row = in.rows[k];
    WeeklySnapItem it;
    it.id = T.ids[k];
    it.st = row.done ? 'd' : row.prog ? 'p' : 't';
    if (row.done) it.finish = in.issues[k].resolved;
    else it.finish = row.asapFinish;
    it.late = !row.done && row.floatDays < 0;
    s.items.push_back(std::move(it));
  }
  return s;
}

// ---------------------------------------------------------------- ファイル名・保存先
std::string weeklyFileName(const std::string& projectName, Day periodStart, bool html) {
  std::string stem = reportFileName(projectName, "");  // "<名前>.html"
  const std::size_t dot = stem.rfind('.');
  if (dot != std::string::npos) stem.erase(dot);
  return stem + "_週報_" + fmtIso(periodStart) + (html ? ".html" : ".md");
}

Status checkWeeklyPath(const std::string& path, bool html) {
  if (html) return checkSavePath(path, {".html", ".htm"}, ".html");
  return checkSavePath(path, {".md", ".markdown", ".txt"}, ".md");
}

Status writeWeekly(IFileStore& fs, const std::string& path, const std::string& content, bool html) {
  if (Status s = checkWeeklyPath(path, html); !s.ok()) return s;
  if (const auto st = fs.stat(path); st && st->isDir) return Error{"同じ名前のフォルダがあります"};
  return fs.writeAtomic(path, content);
}

// ---------------------------------------------------------------- 文言
namespace weeklyfmt {

std::string doneTitle(const WeeklyReport& r) {
  switch (r.period) {
    case WeeklyPeriod::ThisWeek: return "今週の完了";
    case WeeklyPeriod::LastWeek: return "先週の完了";
    case WeeklyPeriod::Custom: break;
  }
  return "期間内の完了";
}

std::string nextTitle(const WeeklyReport& r) {
  switch (r.period) {
    case WeeklyPeriod::ThisWeek: return "来週の予定";
    case WeeklyPeriod::LastWeek: return "今週の予定";
    case WeeklyPeriod::Custom: break;
  }
  return "次の7日間の予定";
}

std::string loadTitle(const WeeklyReport& r) {
  switch (r.period) {
    case WeeklyPeriod::ThisWeek: return "来週の負荷";
    case WeeklyPeriod::LastWeek: return "今週の負荷";
    case WeeklyPeriod::Custom: break;
  }
  return "次の7日間の負荷";
}

std::string rangeText(const WeeklyRange& g) { return fmtLong(g.from) + " 〜 " + fmtLong(g.to); }

std::string workdaysText(int n) { return "営業日 " + std::to_string(n) + " 日"; }

std::string percent(double p) {
  if (!std::isfinite(p)) return "0%";
  return std::to_string(std::lround(std::max(0.0, std::min(1.0, p)) * 100.0)) + "%";
}

std::string deltaPt(double d) {
  if (!std::isfinite(d)) return "±0pt";
  const long v = std::lround(d * 100.0);
  if (v == 0) return "±0pt";
  return (v > 0 ? "+" : "-") + std::to_string(v > 0 ? v : -v) + "pt";
}

std::string deltaInt(int d) {
  if (d == 0) return "±0";
  return (d > 0 ? "+" : "-") + std::to_string(d > 0 ? d : -d);
}

std::string shiftText(int days) {
  if (days == 0) return "変わらず";
  const int a = days < 0 ? -days : days;
  return std::to_string(a) + " 営業日" + (days > 0 ? "遅くなった" : "早まった");
}

std::string floatPhrase(int floatDays) {
  if (floatDays < 0) return "遅れ " + std::to_string(-static_cast<long long>(floatDays)) + " 営業日";
  return "余裕 " + std::to_string(floatDays) + " 営業日";
}

std::string remainText(const WeeklyItem& it, Day base) {
  if (it.finish < base) return "予定日を過ぎている";
  return std::to_string(it.remainDays) + " 営業日";
}

std::string remainPhrase(const WeeklyItem& it, Day base) {
  if (it.finish < base) return "予定日を過ぎている";
  return "残り " + std::to_string(it.remainDays) + " 営業日";
}

std::string who(const std::string& name) { return name.empty() ? std::string("未割当") : name; }

std::string assigneeText(const std::string& name) { return "担当: " + who(name); }

std::string title(const std::string& did, const std::string& name) {
  if (did.empty()) return name;
  return name.empty() ? did : did + " " + name;
}

std::string diffPhrase(const WeeklyMilestone& m) {
  if (!m.deadline) return "期限なし";
  if (m.diffDays == 0) return m.achieved ? "期限どおりに達成" : "期限どおり";
  const long long a = m.diffDays < 0 ? -static_cast<long long>(m.diffDays) : m.diffDays;
  const std::string days = "期限より " + std::to_string(a) + " 営業日";
  if (m.achieved) return days + (m.diffDays < 0 ? "遅れて達成" : "早く達成");
  return days + (m.diffDays < 0 ? "遅れる見込み" : "早い見込み");
}

std::string loadText(int percentValue) { return percentValue < 0 ? std::string("-") : std::to_string(percentValue) + "%"; }

std::string moreText(int omitted) { return "ほか " + std::to_string(omitted) + " 件"; }

}  // namespace weeklyfmt

}  // namespace roadmap
