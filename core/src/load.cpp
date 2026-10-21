#include "roadmap/load.hpp"

#include <algorithm>
#include <map>
#include <unordered_map>

#include "roadmap/text.hpp"

namespace roadmap {

namespace {

Day weekStart(Day d) { return d - (dow(d) + 6) % 7; }  // 月曜（統計の週ごとの完了と同じ切り方）

// 1 件の作業を広げる営業日の範囲の上限（日）。これより長い予定は、ここまでで切る（極端な日付での長い反復を避ける）
constexpr long long kMaxSpanDays = 3660;

Day clampSpan(Day s, Day f) {
  const long long lim = static_cast<long long>(s) + kMaxSpanDays;
  return static_cast<Day>(std::min<long long>(f, lim));
}

}  // namespace

int loadPercent(double ratio) {
  if (!(ratio > 0)) return 0;  // 負・NaN も 0
  const double v = ratio * 100.0 + 0.5;
  if (!(v < 99999.0)) return 99999;
  return static_cast<int>(v);
}

LoadReport computeLoad(const Tree& T, const std::vector<Issue>& issues, const std::vector<ScheduleRow>& R,
                       const Calendar& cal, Day base, const ResourceLimit& resource) {
  LoadReport out;
  const Day nowWeek = weekStart(base);
  out.cap = resource.on ? std::max(1, resource.cap) : 1;
  for (int k = 0; k < kPastWeeks; ++k) out.doneWeekStarts.push_back(nowWeek + (k - kPastWeeks) * 7);

  // ---- 成果: 過去 kPastWeeks 週に完了した作業（担当者ごと）----
  {
    std::map<std::string, DoneRow> by;
    for (int id : T.leafOrder) {
      if (T.kind[id] == Kind::Ms || !R[id].done || !issues[id].resolved) continue;
      const int k = (weekStart(*issues[id].resolved) - nowWeek) / 7 + kPastWeeks;  // 週の境は 7 の倍数
      if (k < 0 || k >= kPastWeeks) continue;
      const std::string who = text::trim(issues[id].assignee);
      auto it = by.find(who);
      if (it == by.end()) {
        it = by.emplace(who, DoneRow{}).first;
        it->second.name = who;
        it->second.perWeek.assign(static_cast<std::size_t>(kPastWeeks), 0);
      }
      DoneRow& d = it->second;
      d.n++;
      d.weeks += R[id].weeks;
      d.perWeek[static_cast<std::size_t>(k)]++;
    }
    // 名前順（順位にしない）。担当なし（名前が空）は最後
    for (auto& [name, d] : by)
      if (!name.empty()) out.done.push_back(std::move(d));
    const auto none = by.find("");
    if (none != by.end()) out.done.push_back(std::move(none->second));
  }

  // ---- 負荷: 未完了の作業（マイルストーンを除く）----
  std::vector<int> open;
  int lastWeek = 0;    // 基準日の週からの週数（最後の作業の完了が入る週）
  Day maxFinish = 0;
  for (int id : T.leafOrder) {
    if (T.kind[id] == Kind::Ms || R[id].done) continue;
    const Day s = R[id].asapStart;
    const Day f = clampSpan(s, std::max(R[id].asapFinish, s));
    if (open.empty() || f > maxFinish) maxFinish = f;
    lastWeek = std::max(lastWeek, (weekStart(f) - nowWeek) / 7);
    open.push_back(id);
  }
  if (open.empty()) return out;

  const int N = std::min(lastWeek + 1, kLoadWeeks);
  const std::size_t nw = static_cast<std::size_t>(N);
  out.lastFolded = lastWeek + 1 > kLoadWeeks;
  for (int k = 0; k < N; ++k) {
    const Day ws = nowWeek + k * 7;
    Day end = ws + 6;
    if (k == N - 1 && out.lastFolded) end = std::max(end, clampSpan(ws, maxFinish));  // 以降をまとめた範囲
    out.weekStarts.push_back(ws);
    out.workdays.push_back(cal.nd(ws, end));
  }

  std::unordered_map<std::string, std::size_t> idx;
  std::vector<int> wc(nw, 0);  // 1 件の作業が各週に持つ営業日の数
  for (int id : open) {
    const ScheduleRow& r = R[id];
    const std::string who = text::trim(issues[id].assignee);
    auto it = idx.find(who);
    if (it == idx.end()) {
      it = idx.emplace(who, out.load.size()).first;
      out.load.emplace_back();
      out.load.back().name = who;
      out.load.back().cells.resize(nw);
    }
    LoadRow& row = out.load[it->second];

    const Day s = r.asapStart;
    const Day f = clampSpan(s, std::max(r.asapFinish, s));
    const double days = std::max(r.days, 1);
    const auto weekOf = [&](Day d) {  // d が基準日の週より前なら -1
      if (d < nowWeek) return -1;
      return std::min((weekStart(d) - nowWeek) / 7, N - 1);
    };
    std::fill(wc.begin(), wc.end(), 0);
    int nWork = 0;
    for (Day d = s; d <= f; ++d) {
      if (!cal.isWork(d)) continue;
      ++nWork;
      const int k = weekOf(d);
      if (k >= 0) wc[static_cast<std::size_t>(k)]++;
    }
    const auto put = [&](int k, double amount) {
      LoadCell& c = row.cells[static_cast<std::size_t>(k)];
      c.days += amount;
      row.totalDays += amount;
      c.n++;
      if (static_cast<int>(c.ids.size()) < kLoadCellIds) c.ids.push_back(id);
    };
    if (nWork == 0) {
      // 範囲に営業日がない（休業日だけ）: 最初の営業日の週にまとめて置く
      const int k = weekOf(cal.isWork(s) ? s : cal.workday(s, 1));
      if (k >= 0) put(k, days);
      continue;
    }
    const double per = days / nWork;
    for (int k = 0; k < N; ++k)
      if (wc[static_cast<std::size_t>(k)] > 0) put(k, per * wc[static_cast<std::size_t>(k)]);
  }

  for (LoadRow& row : out.load)
    for (std::size_t k = 0; k < nw; ++k) {
      LoadCell& c = row.cells[k];
      const double denom = static_cast<double>(out.workdays[k]) * out.cap;
      c.ratio = denom > 0 ? c.days / denom : 0;  // 営業日 0 の週は 0
      row.peak = std::max(row.peak, c.ratio);
    }
  // 負荷の合計が大きい順。担当なしは最後
  std::stable_sort(out.load.begin(), out.load.end(), [](const LoadRow& a, const LoadRow& b) {
    if (a.name.empty() != b.name.empty()) return b.name.empty();
    return a.totalDays > b.totalDays;
  });
  return out;
}

}  // namespace roadmap
