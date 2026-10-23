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

// ベースラインの節の見出し・件数・消えた課題（現在の課題がなくても作れる部分）
void fillBaselineHeader(WeeklyBaseline& wb, const BaselineCompare& bc, std::size_t cap) {
  wb.available = true;
  wb.name = cleanText(bc.baselineName);
  wb.at = bc.baselineAt;
  wb.truncated = bc.truncated;
  wb.sum = bc.sum;
  for (const BaselineEntry& e : bc.removed) {
    if (e.parent) continue;
    ++wb.removed.total;
    if (wb.removed.items.size() >= cap) continue;
    WeeklyBaselineRow row;
    row.id = cleanText(e.id);
    row.name = cleanText(e.title);
    row.planFinish = e.planFinish;
    wb.removed.items.push_back(std::move(row));
  }
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
    // 課題が 0 件（全部削除した）ときは、入力の食い違いではないので、有効な比較（消えた課題）は出す
    if (n == 0 && in.issues.empty() && in.rows.empty() && opt.sections.baseline && in.baseline && in.baseline->entries.empty())
      fillBaselineHeader(r.baseline, *in.baseline, std::max<std::size_t>(1, opt.maxItems));
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
    for (std::size_t q = 0; q < in.load.weekStarts.size(); ++q) {
      if (in.load.weekStarts[q] != w) continue;
      // 最後の列が 26 週より先をまとめたものなら、複数の週の平均なので、その週の負荷としては使わない（不明にする）
      if (in.load.lastFolded && q + 1 == in.load.weekStarts.size()) return -1;
      return static_cast<int>(q);
    }
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
  // 比べる元: 直接渡された prev、なければ memo・autos から期間に合わせて選ぶ（Issue #70）。終わり（until）が nullptr なら「いま」
  WeeklyCompare cmp;
  if (in.prev) {
    cmp = {in.prev, in.prevAuto, in.until};
  } else if (in.memo || in.autos) {
    static const WeeklyMemo kNoMemo;
    static const WeeklyAuto kNoAuto;
    cmp = weeklyChooseCompare(in.memo ? *in.memo : kNoMemo, in.autos ? *in.autos : kNoAuto, in.base, opt.period, r.range);
  }
  if (const WeeklySnapshot* prev = cmp.from) {
    const WeeklySnapshot* until = cmp.to;  // 比べる終わりのスナップショット（nullptr = いま）
    WeeklyChanges& ch = r.changes;
    ch.available = true;
    ch.since = prev->base;
    ch.sinceAuto = cmp.fromAuto;
    ch.hasUntil = until != nullptr;
    ch.until = until ? until->base : 0;
    ch.truncated = prev->truncated || (until && until->truncated);
    if (!until) {
      // いまとの比較: サマリーに増減を出す。自動のスナップショット同士の比較では、いまの値と並べると食い違うので出さない
      sm.hasPrev = true;
      sm.prevBase = prev->base;
      sm.prevAuto = cmp.fromAuto;
      sm.progressDelta = sm.progress - prev->progress;
      sm.lateDelta = st.late - prev->late;
      sm.tightDelta = st.tight - prev->tight;
      sm.doneDelta = st.done - prev->done;
      if (prev->finish && st.hasFinish) {
        sm.hasFinishShift = true;
        sm.finishShiftDays = cal.floatDays(*prev->finish, st.finish);
      }
    }

    std::unordered_map<std::string, const WeeklySnapItem*> before;
    before.reserve(prev->items.size());
    for (const WeeklySnapItem& it : prev->items) before.emplace(it.id, &it);
    std::unordered_map<std::string, const WeeklySnapItem*> after;  // 比べる終わりがスナップショットのときだけ
    if (until) {
      after.reserve(until->items.size());
      for (const WeeklySnapItem& it : until->items) after.emplace(it.id, &it);
    }

    std::vector<Cand> cLate, cDoneNew;
    std::vector<Cand> cMove;
    std::unordered_set<std::string> seenNow;
    int added = 0;
    for (int i : T.dfs) {
      if (!inTree(i)) continue;
      const std::size_t k = static_cast<std::size_t>(i);
      if (!T.kids[k].empty() || T.kind[k] == Kind::Ms) continue;
      const ScheduleRow& row = in.rows[k];
      // 終わり側の状態: いまなら今の行、スナップショットならその中の値（その時点になかった課題は数えない）
      bool nowDone = row.done, nowLate = !row.done && row.floatDays < 0;
      OptDay nowFinish = row.asapFinish;
      if (until) {
        const auto g = after.find(T.ids[k]);
        if (g == after.end()) continue;
        nowDone = g->second->st == 'd';
        nowLate = g->second->late && !nowDone;
        nowFinish = g->second->finish;
      }
      const auto f = before.find(T.ids[k]);
      if (f == before.end()) {
        if (!until) ++added;
        continue;
      }
      seenNow.insert(T.ids[k]);
      const WeeklySnapItem& was = *f->second;
      if (nowDone) {
        if (was.st != 'd') {
          // 並べ替えの鍵は完了日。終わりがスナップショットなら保存した値（あとで書き換えた完了日を使わない）
          const OptDay doneOn = until ? nowFinish : in.issues[k].resolved;
          cDoneNew.push_back({i, doneOn ? *doneOn : 0});
        }
        continue;
      }
      if (nowLate && !was.late) cLate.push_back({i, row.floatDays});
      if (was.st != 'd' && was.finish && nowFinish && *was.finish != *nowFinish) {
        // 日付が違えば出す。営業日の差が 0（休業日の変更で土日・休業日をまたいだだけ）でも隠さない
        const int shift = cal.floatDays(*was.finish, *nowFinish);
        const int mag = shift < 0 ? -shift : shift;
        cMove.push_back({i, -static_cast<std::int64_t>(mag)});  // 動きの大きい順（差が 0 のものは最後）
      }
    }
    int removed = 0;
    if (until) {
      // 終わりもスナップショット: 終わりにあって元にない課題が増えた作業、元にあって終わりにない課題が無くなった作業
      // （いまの課題の木には依らない。どちらかが一部だけのスナップショットなら数え違えるので数えない）
      if (!ch.truncated) {
        for (const WeeklySnapItem& it : until->items)
          if (before.find(it.id) == before.end()) ++added;
        for (const WeeklySnapItem& it : prev->items)
          if (after.find(it.id) == after.end()) ++removed;
      }
      ch.added = added;
    } else {
      ch.added = prev->truncated ? 0 : added;
      for (const WeeklySnapItem& it : prev->items)
        if (seenNow.find(it.id) == seenNow.end()) ++removed;
    }
    ch.removed = removed;
    fill(ch.newlyLate, cLate);
    fill(ch.newlyDone, cDoneNew);
    if (until) {
      // 表示する完了日も、保存した値にする（makeItem は今の Issue::resolved を入れるため）
      for (WeeklyItem& it : ch.newlyDone.items) {
        const auto g = after.find(T.ids[static_cast<std::size_t>(it.index)]);
        it.resolved = g != after.end() ? g->second->finish : OptDay{};
      }
    }
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
      if (until) {
        const auto g = after.find(T.ids[k]);
        if (g != after.end() && g->second->finish) mv.to = *g->second->finish;
      }
      mv.shiftDays = cal.floatDays(mv.from, mv.to);
      ch.finishMoved.push_back(std::move(mv));
    }
  }

  // ---- ベースラインとの差 ----
  if (opt.sections.baseline && in.baseline && in.baseline->entries.size() == n) {
    const BaselineCompare& bc = *in.baseline;
    WeeklyBaseline& wb = r.baseline;
    fillBaselineHeader(wb, bc, cap);
    const auto rowOf = [&](int i) {
      const std::size_t k = static_cast<std::size_t>(i);
      const BaselineEntry& e = bc.entries[k];
      WeeklyBaselineRow row;
      row.index = i;
      row.id = cleanText(e.id);
      row.did = cleanText(T.did[k]);
      row.name = cleanText(T.name[k]);
      row.assignee = cleanText(assigneeOf(i));
      row.planFinish = e.planFinish;
      row.nowFinish = e.nowFinish;
      row.diffDays = e.finishDiff;
      row.differs = e.finishDiffers;
      row.done = e.done;
      return row;
    };
    std::vector<Cand> cLate, cEarly, cAdded;
    for (int i : T.dfs) {
      if (!inTree(i)) continue;
      const std::size_t k = static_cast<std::size_t>(i);
      if (!T.kids[k].empty()) continue;  // 親のまとめは子の差を言い換えたものなので数えない
      const BaselineEntry& e = bc.entries[k];
      if (e.state == BaselineState::Late) cLate.push_back({i, -static_cast<std::int64_t>(e.finishDiff)});
      else if (e.state == BaselineState::Early) cEarly.push_back({i, static_cast<std::int64_t>(e.finishDiff)});  // 早い順（負の大きい方が先）
      else if (e.state == BaselineState::Added) cAdded.push_back({i, 0});
    }
    const auto fillB = [&](WeeklyBaselineList& list, std::vector<Cand>& c) {
      sortCands(c, T);
      list.total = static_cast<int>(c.size());
      for (std::size_t q = 0; q < c.size() && q < cap; ++q) list.items.push_back(rowOf(c[q].idx));
    };
    fillB(wb.late, cLate);
    fillB(wb.early, cEarly);
    fillB(wb.added, cAdded);
  }

  // ---- マイルストーンの予測の推移と遅れの警告（Issue #75）----
  if (in.autos) {
    MtaOptions mo;
    mo.lateDays = in.mtaLateDays;
    Result<MtaReport> m = computeMta(T, in.issues, in.rows, cal, *in.autos, in.base, in.baseline, mo);
    if (m.ok()) {
      r.mta = std::move(m).value();
      r.mtaAvailable = true;
      std::stable_partition(r.mta.series.begin(), r.mta.series.end(), [](const MtaSeries& s) { return s.warn; });  // 警告を先頭に
      if (r.mta.series.size() > cap) {
        r.mtaOmitted = static_cast<int>(r.mta.series.size() - cap);
        r.mta.series.resize(cap);
      }
      for (MtaSeries& s : r.mta.series) {  // 件名・Issue Id は他の節と同じ下ごしらえ（制御文字・壊れた UTF-8）
        s.id = cleanText(s.id);
        s.did = cleanText(s.did);
        s.name = cleanText(s.name);
      }
    }
  }
  return r;
}

WeeklyCompare weeklyChooseCompare(const WeeklyMemo& manual, const WeeklyAuto& autos, Day base, WeeklyPeriod period, const WeeklyRange& range) {
  const Day thisMon = weeklyMondayOf(base);
  const WeeklySnapshot* manualPrev = weeklyPreviousSnapshot(manual, base);  // 従来: 基準日の週より前で一番新しい手動
  const WeeklyCompare legacy{manualPrev, false, nullptr};
  switch (period) {
    case WeeklyPeriod::ThisWeek: {
      if (const WeeklySnapshot* head = weeklyAutoOfWeek(autos, thisMon)) return {head, true, nullptr};
      const WeeklySnapshot* older = weeklyAutoBefore(autos, thisMon);
      // 今週の頭がなければ、今週より前の手動・自動のうち新しいほう（同じ日なら自動）
      if (older && (!manualPrev || older->base >= manualPrev->base)) return {older, true, nullptr};
      return legacy;
    }
    case WeeklyPeriod::LastWeek: {
      const Day lastMon = thisMon - 7;
      const WeeklySnapshot* from = weeklyAutoOfWeek(autos, lastMon);
      if (!from) from = weeklyAutoBefore(autos, lastMon);  // 先週の頭がなければ、より前で一番近い自動
      if (!from) return legacy;
      return {from, true, weeklyAutoOfWeek(autos, thisMon)};  // 今週の頭がなければ「いま」
    }
    case WeeklyPeriod::Custom: {
      const WeeklySnapshot* from = weeklyAutoAtOrBefore(autos, range.from);
      if (!from) return legacy;
      if (range.to >= thisMon) return {from, true, nullptr};  // 範囲が今週に届く: いま
      const WeeklySnapshot* to = weeklyAutoAtOrBefore(autos, range.to);
      if (!to || to == from) return {};  // 始まりと終わりを分けるスナップショットがない: 元なし
      return {from, true, to};
    }
  }
  return {};
}

WeeklySnapshot makeWeeklySnapshot(const WeeklyInput& in) {
  return makeWeeklySnapshot(in.tree, in.issues, in.rows, in.stats, in.base);
}

WeeklySnapshot makeWeeklySnapshot(const Tree& T, const std::vector<Issue>& issues, const std::vector<ScheduleRow>& rows,
                                  const ProjectStats& st, Day base) {
  WeeklySnapshot s;
  s.base = base;
  s.progress = std::max(0.0, std::min(1.0, st.progress()));
  s.tasks = st.tasks;
  s.done = st.done;
  s.late = st.late;
  s.tight = st.tight;
  if (st.hasFinish) s.finish = st.finish;
  const std::size_t n = T.n > 0 ? static_cast<std::size_t>(T.n) : 0;
  if (n == 0 || issues.size() != n || rows.size() != n || T.dfs.size() != n || T.kids.size() != n || T.kind.size() != n ||
      T.ids.size() != n)
    return s;
  for (int i : T.dfs) {
    if (i < 0 || static_cast<std::size_t>(i) >= n) continue;
    const std::size_t k = static_cast<std::size_t>(i);
    if (!T.kids[k].empty()) continue;
    if (T.kind[k] == Kind::Ms) {  // マイルストーンは items ではなく milestones に（Issue #75）
      if (T.ids[k].empty() || T.ids[k].size() > kWeeklyMaxIdBytes || s.milestones.size() >= kWeeklyMaxSnapshotMilestones) continue;
      const ScheduleRow& mr = rows[k];
      WeeklySnapItem mi;
      mi.id = T.ids[k];
      mi.st = mr.done ? 'd' : mr.prog ? 'p' : 't';
      mi.finish = mr.done && issues[k].resolved ? issues[k].resolved : OptDay(mr.asapFinish);
      mi.late = !mr.done && mr.floatDays < 0;
      s.milestones.push_back(std::move(mi));
      continue;
    }
    if (T.ids[k].empty() || T.ids[k].size() > kWeeklyMaxIdBytes) {
      s.truncated = true;  // 入らなかった課題がある: 次の週に「増えた」と数え違えないよう、不完全の印を付ける
      continue;
    }
    if (s.items.size() >= kWeeklyMaxSnapshotItems) {
      s.truncated = true;
      break;
    }
    const ScheduleRow& row = rows[k];
    WeeklySnapItem it;
    it.id = T.ids[k];
    it.st = row.done ? 'd' : row.prog ? 'p' : 't';
    if (row.done) it.finish = issues[k].resolved;
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
  if (days == 0) return "営業日の差は 0（休業日・土日をまたいだ日付の違い）";
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

std::string snapshotBasisText(bool automatic, Day d) {
  return fmtLong(d) + (automatic ? " に自動で記録した週の頭の状態" : " に書き出したときの状態");
}

std::string changesLeadText(const WeeklyChanges& c) {
  if (!c.hasUntil) return snapshotBasisText(c.sinceAuto, c.since) + "との比較です。";
  return snapshotBasisText(c.sinceAuto, c.since) + "から、" + snapshotBasisText(true, c.until) +
         "までの変化です。課題の件名・担当者・余裕はいまの値です。";
}

std::string baselineAtText(const std::string& at) {
  if (at.size() != 16) return {};
  return at.substr(0, 4) + "/" + at.substr(5, 2) + "/" + at.substr(8, 2) + " " + at.substr(11, 5);
}

std::string baselineMoveText(const WeeklyBaselineRow& r) {
  std::string s = "計画 " + (r.planFinish ? fmtMd(*r.planFinish) : std::string("-")) + " → 見込み " + fmtMd(r.nowFinish) + "・";
  if (r.diffDays == 0) return s + "営業日の差は 0（休業日・土日をまたいだ日付の違い）";
  const int a = r.diffDays < 0 ? -r.diffDays : r.diffDays;
  return s + "計画より " + std::to_string(a) + " 営業日" + (r.diffDays > 0 ? "遅れ" : "早い");
}

std::string baselineAddedText(const WeeklyBaselineRow& r) { return "完了見込み " + fmtMd(r.nowFinish); }

std::string baselineRemovedText(const WeeklyBaselineRow& r) {
  return "計画の完了 " + (r.planFinish ? fmtMd(*r.planFinish) : std::string("-"));
}

std::string baselineCountsText(const BaselineSummary& s) {
  return "遅れ " + std::to_string(s.late) + " 件 ／ 早まり " + std::to_string(s.early) + " 件 ／ 計画どおり " + std::to_string(s.onPlan) +
         " 件 ／ 計画になかった " + std::to_string(s.added) + " 件 ／ 消えた " + std::to_string(s.removed) + " 件";
}

std::string baselineSameDayNote(const BaselineSummary& s) {
  if (s.sameDayDiffers <= 0) return {};
  return "営業日の差が 0 でも完了日が違う課題が " + std::to_string(s.sameDayDiffers) + " 件あります（休業日・土日をまたいだ日付の違い）。";
}

}  // namespace weeklyfmt

}  // namespace roadmap
