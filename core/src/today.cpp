#include "roadmap/today.hpp"

#include <algorithm>
#include <utility>

#include "roadmap/commands.hpp"
#include "roadmap/text.hpp"

namespace roadmap {

namespace {

struct Entry {
  int idx = 0;
  int fl = 0;     // 余裕（営業日）
  int order = 0;  // 木の順（dfs での位置）
};

bool byFloat(const Entry& a, const Entry& b) {
  if (a.fl != b.fl) return a.fl < b.fl;
  return a.order < b.order;
}
bool byOrder(const Entry& a, const Entry& b) { return a.order < b.order; }

std::vector<std::string> idsOf(const Tree& T, std::vector<Entry>& v, bool (*less)(const Entry&, const Entry&)) {
  std::stable_sort(v.begin(), v.end(), less);
  std::vector<std::string> out;
  out.reserve(v.size());
  for (const Entry& e : v) out.push_back(T.ids[static_cast<std::size_t>(e.idx)]);
  return out;
}

struct BandEntry {
  Entry e;
  TodayBand b;
};

// 件名を code point で n 文字までにする（通知用）
std::string shortName(const std::string& s, std::size_t n) {
  std::size_t i = 0, count = 0;
  while (i < s.size() && count < n) {
    text::decodeAt(s, i);
    ++count;
  }
  if (i >= s.size()) return s;
  return s.substr(0, i) + "…";
}

Day firstWorkOnOrAfter(const Calendar& cal, Day d) { return cal.isWork(d) ? d : cal.workday(d, 1); }

}  // namespace

Day mondayOf(Day d) { return d - (dow(d) + 6) % 7; }

Result<TodayReport> computeToday(const Tree& T, const std::vector<Issue>& issues, const std::vector<ScheduleRow>& R, const Calendar& cal,
                                 Day today, const TodayFilter& filter) {
  const std::size_t n = static_cast<std::size_t>(T.n);
  if (issues.size() != n || R.size() != n || T.cat.size() != n || T.dl.size() != n || T.preds.size() != n)
    return Error{"課題と日程の数が合いません"};

  TodayReport rep;
  rep.today = today;
  rep.weekStart = mondayOf(today);
  rep.todayIsWork = cal.isWork(today);
  for (int k = 0; k < kTodayWeekDays; ++k) {
    TodayDay& d = rep.week[static_cast<std::size_t>(k)];
    d.day = rep.weekStart + k;
    d.holiday = cal.isHoliday(d.day);
    d.isToday = d.day == today;
  }
  const std::string who = text::trim(filter.assignee);

  std::vector<Entry> late, dueToday, inProg, canStart, ms;
  std::array<std::vector<Entry>, kTodayWeekDays> cols;
  std::vector<BandEntry> bands;

  for (const int i : T.leafOrder) {
    const std::size_t u = static_cast<std::size_t>(i);
    if (T.cat[u] == Cat::Done || R[u].done) continue;
    ++rep.openAll;
    const Issue& o = issues[u];
    if (filter.byAssignee && text::trim(o.assignee) != who) continue;

    const ScheduleRow& r = R[u];
    const OptDay dl = T.dl[u];
    const bool isMs = T.kind[u] == Kind::Ms;
    // 日付が 1 つでもある（自分の日付・実効の期限・先行）。日付のない課題は、最早の日付が「基準日から始まる」だけなので
    // 「今日が終了予定」「始められる」には出さない
    const bool hasDate = dl || o.start || o.due || o.deadline || o.notBefore || !T.preds[u].empty();
    const bool late1 = r.floatDays < 0 || (dl && *dl < today);
    const Entry e{i, r.floatDays, T.dfsPos[u]};

    // ---- 今日の節（1 つの課題は 1 つの節だけ）----
    if (late1) {
      late.push_back(e);
    } else if (isMs) {
      // マイルストーンは期間が 0 で、先行がなければ最早の日は常に基準日になる。期限があるときは期限の日だけで決める
      if (dl ? *dl == today : (hasDate && r.asapFinish == today)) ms.push_back(e);
    } else if ((dl && *dl == today) || (hasDate && r.asapFinish == today)) {
      dueToday.push_back(e);
    } else if (T.cat[u] == Cat::Prog) {
      inProg.push_back(e);
    } else if (T.cat[u] == Cat::Todo && hasDate && r.asapStart <= today && !(o.start && *o.start > today)) {
      bool free = true;
      for (const int p : T.preds[u])
        if (T.cat[static_cast<std::size_t>(p)] != Cat::Done) {
          free = false;
          break;
        }
      if (free) canStart.push_back(e);
    }

    // ---- 今週の列: 期限の日（なければ最早の完了の日）。土日は金曜の列に寄せる ----
    {
      OptDay day;
      if (dl) day = dl;
      else if (hasDate) day = r.asapFinish;
      bool placed = false;
      if (day) {
        Day d = *day;
        const int w = dow(d);
        if (w == 6) d -= 1;
        else if (w == 0) d -= 2;
        const Day col = d - rep.weekStart;
        if (*day >= rep.weekStart && *day <= rep.weekStart + 6 && col >= 0 && col < kTodayWeekDays) {
          cols[static_cast<std::size_t>(col)].push_back(e);
          ++rep.summary.completing;
          if (late1) ++rep.summary.late;
          else if (r.floatDays <= kTodayTightDays) ++rep.summary.tight;
          placed = true;
        }
      }
      if (late1 && !placed) ++rep.summary.lateElsewhere;
    }

    // ---- 進行中の帯 ----
    // 期間が今週にかかるものだけ（週の外のものは「今日」の進行中で見る。週の外の帯が件数と表示枠を食わないように）
    if (T.cat[u] == Cat::Prog && !isMs && r.asapStart <= rep.weekStart + 6 && r.asapFinish >= rep.weekStart) {
      const auto colOf = [&](Day d) { return d < rep.weekStart ? 0 : (d > rep.weekStart + 4 ? kTodayWeekDays - 1 : static_cast<int>(d - rep.weekStart)); };
      BandEntry b;
      b.e = e;
      b.b.id = T.ids[u];
      b.b.first = colOf(r.asapStart);
      b.b.last = std::max(b.b.first, colOf(r.asapFinish));
      b.b.cutLeft = r.asapStart < rep.weekStart;
      b.b.cutRight = r.asapFinish > rep.weekStart + 6;
      bands.push_back(std::move(b));
    }
  }

  rep.late = idsOf(T, late, byFloat);
  rep.dueToday = idsOf(T, dueToday, byFloat);
  rep.inProgress = idsOf(T, inProg, byFloat);
  rep.canStart = idsOf(T, canStart, byFloat);
  rep.milestones = idsOf(T, ms, byOrder);
  for (std::size_t k = 0; k < cols.size(); ++k) rep.week[k].ids = idsOf(T, cols[k], byFloat);
  std::stable_sort(bands.begin(), bands.end(), [](const BandEntry& a, const BandEntry& b) {
    if (a.b.first != b.b.first) return a.b.first < b.b.first;
    return byFloat(a.e, b.e);
  });
  rep.bands.reserve(bands.size());
  for (BandEntry& b : bands) rep.bands.push_back(std::move(b.b));
  return rep;
}

ShiftEdit planShift(const Issue& o, Cat cat, const Calendar& cal, Day today, ShiftTo to) {
  ShiftEdit e;
  e.start = o.start;
  e.due = o.due;
  e.deadline = o.deadline;
  e.notBefore = o.notBefore;
  e.days = to == ShiftTo::Tomorrow ? 1 : 5;
  e.target = to == ShiftTo::Tomorrow ? cal.workday(today, 1) : firstWorkOnOrAfter(cal, mondayOf(today) + 7);
  if (cat == Cat::Done) return e;
  const auto mv = [&](const OptDay& d) -> OptDay {
    if (!d) return d;
    return std::max(cal.workday(*d, e.days), e.target);
  };
  e.due = mv(o.due);
  e.deadline = mv(o.deadline);
  if (cat != Cat::Prog) {
    e.start = mv(o.start);
    e.notBefore = o.notBefore ? std::max(*o.notBefore, e.target) : e.target;
  }
  e.changed = e.start != o.start || e.due != o.due || e.deadline != o.deadline || e.notBefore != o.notBefore;
  return e;
}

ShiftResult shiftIssue(Session& s, const std::string& id, ShiftTo to, Day today) {
  ShiftResult out;
  const Issue* o = s.issue(id);
  const Tree* T = s.tree();
  const int i = T ? T->find(id) : -1;
  if (!o || i < 0) {
    s.notify("ずらす課題が見つかりません");
    return out;
  }
  if (!T->isLeaf(i)) {
    s.notify("まとめの課題（親）はずらせません。配下の課題をずらしてください");
    return out;
  }
  const Cat cat = T->cat[static_cast<std::size_t>(i)];
  if (cat == Cat::Done) {
    s.notify("完了した課題はずらせません");
    return out;
  }
  const ShiftEdit e = planShift(*o, cat, s.calendar(), today, to);
  out.target = e.target;
  if (!e.changed) {
    s.notify("この課題にはずらせる日付がありません");
    return out;
  }
  const std::string label = "「" + shortName(T->name[static_cast<std::size_t>(i)], 24) + "」";
  // ずらす前の最早の開始（動いた課題を数えるため。計算し直すと古い結果は捨てられるので値で控える）
  std::vector<Day> before;
  if (const auto* R = s.rows()) {
    before.reserve(R->size());
    for (const ScheduleRow& r : *R) before.push_back(r.asapStart);
  }
  const bool ok = s.setField(id, [&](Issue& x) {
    x.start = e.start;
    x.due = e.due;
    x.deadline = e.deadline;
    x.notBefore = e.notBefore;
  });
  if (!ok) return out;  // mutate が理由を通知している
  out.ok = true;
  std::string msg = label + "を" + (to == ShiftTo::Tomorrow ? "明日" : "来週") + "（" + fmtMd(e.target) + "）にずらしました";
  const Tree* T2 = s.tree();
  const auto* R2 = s.rows();
  const int i2 = T2 ? T2->find(id) : -1;
  if (T2 && R2 && i2 >= 0) {
    if (cat != Cat::Prog && e.notBefore && (*R2)[static_cast<std::size_t>(i2)].asapStart > *e.notBefore) {
      out.waited = true;
      msg += "。先行や基準日などのため、開始は " + fmtMd((*R2)[static_cast<std::size_t>(i2)].asapStart) + " になります";
    }
    if (before.size() == R2->size())
      for (std::size_t k = 0; k < R2->size(); ++k)
        if (static_cast<int>(k) != i2 && T2->isLeaf(static_cast<int>(k)) && (*R2)[k].asapStart != before[k]) ++out.moved;
    if (out.moved > 0) msg += "。後続などの日程が動いた課題: " + std::to_string(out.moved) + " 件";
  }
  s.notify(msg, NoticeAction::Undo);
  return out;
}

}  // namespace roadmap
