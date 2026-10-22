#include "roadmap/trends.hpp"

#include <algorithm>
#include <climits>
#include <cmath>
#include <initializer_list>

#include "roadmap/csv.hpp"
#include "roadmap/stamps.hpp"
#include "roadmap/text.hpp"

namespace roadmap {

namespace {

Day weekStart(Day d) { return d - (dow(d) + 6) % 7; }  // 月曜（統計の週ごとの完了と同じ切り方）

// 日 n より前（1970-01-01 以前も含む）の平日（月〜金）の数。1970-01-01 は木曜
long long weekdaysBefore(long long n) {
  const long long k = n + 3;  // 月曜を 0 にずらす
  long long q = k / 7, r = k % 7;
  if (r < 0) {
    r += 7;
    --q;
  }
  return q * 5 + std::min<long long>(r, 5);
}

// 営業日の数え方を前処理して使い回す。平日の数は式で出し（範囲の長さに依らない）、休業日は平日にあたるものだけを
// 並べ替えて二分探索で引く。Calendar::nd のような日ごとの反復をしないので、極端な日付でも遅くならない
class WorkDays {
 public:
  explicit WorkDays(const Calendar& cal) {
    for (Day h : cal.holidays())
      if (const int w = dow(h); w >= 1 && w <= 5) hol_.push_back(h);
    std::sort(hol_.begin(), hol_.end());
  }
  // [a, b] の営業日の数（a > b なら 0）
  int count(Day a, Day b) const {
    if (b < a) return 0;
    const long long weekdays = weekdaysBefore(static_cast<long long>(b) + 1) - weekdaysBefore(a);
    const auto lo = std::lower_bound(hol_.begin(), hol_.end(), a);
    const auto hi = std::upper_bound(hol_.begin(), hol_.end(), b);
    const long long v = weekdays - (hi - lo);
    return v > INT_MAX ? INT_MAX : static_cast<int>(v);
  }
  // a の翌日から b までの営業日の数（b が a と同じ日かそれより前なら 0）。a が休業日でも、そこから先を数える
  int after(Day a, Day b) const { return b > a ? count(a + 1, b) : 0; }

 private:
  std::vector<Day> hol_;  // 平日にあたる休業日（昇順）
};

OptDay dateAt(const Issue& s, int col) {
  if (col < 0 || static_cast<std::size_t>(col) >= s.extra.size()) return std::nullopt;
  return csv::parseDate(s.extra[static_cast<std::size_t>(col)]);
}

}  // namespace

int trendBucket(int d) {
  if (d <= 1) return 0;
  if (d <= 4) return 1;
  if (d <= 9) return 2;
  if (d <= 19) return 3;
  return 4;
}

const char* trendBucketLabel(int b) {
  static const char* const kLabels[kTrendBuckets] = {"0-1", "2-4", "5-9", "10-19", "20+"};
  return b >= 0 && b < kTrendBuckets ? kLabels[b] : "";
}

namespace {
std::string numText(double v) {
  // 小数 1 桁（ロケールに依存しない）。".0" は落とす
  const long long t = std::llround(v * 10.0);
  std::string s = std::to_string(t / 10) + "." + std::to_string(t % 10);
  if (s.size() > 2 && s.compare(s.size() - 2, 2, ".0") == 0) s.resize(s.size() - 2);
  return s;
}
}  // namespace

std::string trendForecastText(const TrendForecast& f, int remainingNow) {
  switch (f.reason) {
    case ForecastReason::None:
      return "直近 " + std::to_string(kTrendPaceWeeks) + " 週の完了 " + std::to_string(f.windowDone) + " 件（週 " + numText(f.perWeek) +
             " 件）のペースだと、残り " + std::to_string(remainingNow) + " 件は " + fmtMd(f.finishWeek) + " の週（" + std::to_string(f.weeksAhead) +
             " 週後）に終わる見込みです（点線）。";
    case ForecastReason::NoData:
      return "数えられる課題がないので、ペースの予測は出していません。";
    case ForecastReason::Finished:
      return "残りがないので、ペースの予測は要りません。";
    case ForecastReason::NoCompletions:
      return "! 直近 " + std::to_string(kTrendPaceWeeks) + " 週に完了がないので、ペースの予測は出していません。";
    case ForecastReason::FewActiveWeeks:
      return "! 直近 " + std::to_string(kTrendPaceWeeks) + " 週のうち完了のある週が " + std::to_string(f.activeWeeks) + " 週（" +
             std::to_string(kTrendMinActiveWeeks) + " 週未満）なので、ペースの予測は出していません。";
    case ForecastReason::TooFar:
      return "! このペース（週 " + numText(f.perWeek) + " 件）だと " + std::to_string(kTrendMaxForecastWeeks) + " 週より先になるので、ペースの予測は出していません。";
  }
  return {};
}

std::vector<std::string> trendNotes(const TrendReport& r) {
  std::vector<std::string> out;
  if (!r.hasCreatedColumn) {
    out.push_back("CSV に Created（作成日）の列がないので、バーンダウンとリードタイムは出せません。");
  } else if (r.burn.noCreated > 0) {
    out.push_back("作成日（Created）のない " + std::to_string(r.burn.noCreated) + " 件は数えていません。");
  }
  if (r.burn.noResolved > 0)
    out.push_back("完了日（Resolved）のない完了済みの " + std::to_string(r.burn.noResolved) + " 件は数えていません。");
  if (r.burn.inconsistent > 0)
    out.push_back(std::to_string(r.burn.inconsistent) + " 件は完了日が作成日より前（日付の矛盾）で除外しました。");
  if (r.burn.future > 0) out.push_back(std::to_string(r.burn.future) + " 件は作成日が基準日より先で除外しました。");
  if (r.aging.noDate > 0)
    out.push_back("最後に動いた日（Updated・開始日・Created）がわからない未完了の " + std::to_string(r.aging.noDate) + " 件は、滞留に数えていません。");
  return out;
}

Result<TrendReport> computeTrends(const Tree& T, const std::vector<Issue>& issues, const ProjectMeta& meta,
                                  const std::vector<ScheduleRow>& R, const Calendar& cal, Day base, const TrendFilter& filter) {
  const std::size_t n = static_cast<std::size_t>(T.n);
  if (issues.size() != n || R.size() != n) return Error{"課題と計算結果の数が合いません。"};
  if (filter.root != TrendFilter::kAllRoots && filter.root != TrendFilter::kStandalone) {
    if (filter.root < 0 || filter.root >= T.n || T.parentOf[static_cast<std::size_t>(filter.root)] >= 0)
      return Error{"絞り込みのルート課題が見つかりません。"};
  }

  TrendReport out;
  const int createdCol = findStampColumn(meta, StampKind::Created);  // 見出しの別名は stamps と共通
  const int updatedCol = findStampColumn(meta, StampKind::Updated);
  out.hasCreatedColumn = createdCol >= 0;
  out.hasUpdatedColumn = updatedCol >= 0;

  const WorkDays wd(cal);
  const Day nowWeek = weekStart(base);
  const std::string wantName = text::trim(filter.name);

  std::vector<Day> createdDays, resolvedDays;  // 数えられる課題の作成日・完了日
  std::vector<int> leads;                      // リードタイム（営業日）
  struct Age {
    int days;
    int idx;
    Day last;
    AgingFrom from;
  };
  std::vector<Age> ages;

  for (int id : T.leafOrder) {
    const std::size_t i = static_cast<std::size_t>(id);
    if (T.kind[i] == Kind::Ms) continue;
    const Issue& s = issues[i];
    const std::string who = text::trim(s.assignee);
    if (filter.who == TrendFilter::Who::Unassigned && !who.empty()) continue;
    if (filter.who == TrendFilter::Who::Name && who != wantName) continue;
    if (filter.root == TrendFilter::kStandalone) {
      if (!T.kids[static_cast<std::size_t>(T.rootOf[i])].empty()) continue;
    } else if (filter.root >= 0 && T.rootOf[i] != filter.root) {
      continue;
    }
    ++out.items;

    const bool done = R[i].done;
    const OptDay created = dateAt(s, createdCol);
    // 作成日が基準日より先のものは、どの集計にも入れず件数だけ出す（Created のない課題の Updated・開始日のフォールバックは保つ）
    if (created && *created > base) {
      ++out.burn.future;
      continue;
    }

    // ---- 滞留日数: 未完了で、最後に動いた日（Updated → Start date → Created）から基準日まで ----
    if (!done) {
      AgingFrom from = AgingFrom::Updated;
      OptDay last = dateAt(s, updatedCol);
      if (!last) {
        last = s.start;
        from = AgingFrom::Start;
      }
      if (!last) {
        last = created;
        from = AgingFrom::Created;
      }
      if (!last) {
        ++out.aging.noDate;
      } else {
        const int days = *last >= base ? 0 : wd.after(*last, base);  // 未来の日付は 0 日
        ages.push_back({days, id, *last, from});
        ++out.aging.n;
        ++out.aging.hist[static_cast<std::size_t>(trendBucket(days))];
      }
    }

    // ---- 作成日・完了日（バーンダウンとリードタイム）----
    if (!created) {
      ++out.burn.noCreated;
      if (done) ++out.lead.noDates;
      continue;
    }
    if (!done) {
      createdDays.push_back(*created);
      continue;
    }
    if (!s.resolved) {
      ++out.burn.noResolved;
      ++out.lead.noDates;
      continue;
    }
    if (*s.resolved < *created) {
      ++out.burn.inconsistent;
      ++out.lead.inconsistent;
      continue;
    }
    createdDays.push_back(*created);
    resolvedDays.push_back(*s.resolved);
    leads.push_back(wd.after(*created, *s.resolved));
  }

  // ---- 滞留: 長い順の上位 ----
  {
    const std::size_t top = std::min<std::size_t>(ages.size(), kTrendTopN);
    std::partial_sort(ages.begin(), ages.begin() + static_cast<std::ptrdiff_t>(top), ages.end(), [](const Age& a, const Age& b) {
      return a.days != b.days ? a.days > b.days : a.idx < b.idx;
    });
    for (std::size_t k = 0; k < top; ++k) {
      const std::size_t i = static_cast<std::size_t>(ages[k].idx);
      AgingItem it;
      it.idx = ages[k].idx;
      it.id = issues[i].id;
      it.name = T.name[i];
      it.assignee = text::trim(issues[i].assignee);
      it.days = ages[k].days;
      it.last = ages[k].last;
      it.from = ages[k].from;
      out.aging.top.push_back(std::move(it));
    }
  }

  // ---- リードタイム ----
  if (!leads.empty()) {
    std::sort(leads.begin(), leads.end());
    const std::size_t m = leads.size();
    out.lead.n = static_cast<int>(m);
    out.lead.median = m % 2 == 1 ? leads[m / 2] : (static_cast<double>(leads[m / 2 - 1]) + leads[m / 2]) / 2.0;
    const std::size_t rank = (90 * m + 99) / 100;  // nearest-rank: ceil(0.9 × m)（整数で数える）
    out.lead.p90 = leads[rank - 1];
    out.lead.maxDays = leads.back();
    for (int d : leads) ++out.lead.hist[static_cast<std::size_t>(trendBucket(d))];
  }

  // ---- バーンダウン: 週ごと（月曜始まり。基準日の週の前 kTrendPastWeeks 週まで）----
  if (createdDays.empty()) {
    out.burn.forecast.reason = ForecastReason::NoData;
    return out;
  }
  const Day minCreated = *std::min_element(createdDays.begin(), createdDays.end());
  const Day firstWeek = std::max(weekStart(minCreated), nowWeek - 7 * kTrendPastWeeks);
  const int W = (nowWeek - firstWeek) / 7 + 1;
  const std::size_t nw = static_cast<std::size_t>(W);
  std::vector<int> created(nw, 0), resolved(nw, 0);
  int createdBefore = 0, resolvedBefore = 0;
  for (Day d : createdDays) {  // 作成日は基準日以前
    if (d < firstWeek) ++createdBefore;
    else ++created[static_cast<std::size_t>((weekStart(d) - firstWeek) / 7)];
  }
  for (Day d : resolvedDays) {
    if (d > base) continue;  // 基準日より先の完了は、まだ完了していないものとして残りに残す
    if (d < firstWeek) ++resolvedBefore;
    else ++resolved[static_cast<std::size_t>((weekStart(d) - firstWeek) / 7)];
  }
  int cumCreated = createdBefore, cumResolved = resolvedBefore;
  out.burn.weeks.resize(nw);
  for (std::size_t k = 0; k < nw; ++k) {
    TrendWeek& w = out.burn.weeks[k];
    cumCreated += created[k];
    cumResolved += resolved[k];
    w.start = firstWeek + static_cast<Day>(k) * 7;
    w.created = created[k];
    w.resolved = resolved[k];
    w.remaining = cumCreated - cumResolved;
    w.partial = k + 1 == nw;
  }
  out.burn.nowIndex = W - 1;
  out.burn.remainingNow = out.burn.weeks.back().remaining;

  // ---- ペース予測: 直近 kTrendPaceWeeks 週（基準日の週を含まない）の完了数の平均 ----
  TrendForecast& f = out.burn.forecast;
  for (int j = 1; j <= kTrendPaceWeeks; ++j) {
    const int k = W - 1 - j;
    if (k < 0) break;
    const int c = resolved[static_cast<std::size_t>(k)];
    f.windowDone += c;
    if (c > 0) ++f.activeWeeks;
  }
  f.perWeek = static_cast<double>(f.windowDone) / kTrendPaceWeeks;
  if (out.burn.remainingNow <= 0) {
    f.reason = ForecastReason::Finished;
  } else if (f.windowDone == 0) {
    f.reason = ForecastReason::NoCompletions;
  } else if (f.activeWeeks < kTrendMinActiveWeeks) {
    f.reason = ForecastReason::FewActiveWeeks;
  } else {
    const long long weeks = (static_cast<long long>(out.burn.remainingNow) * kTrendPaceWeeks + f.windowDone - 1) / f.windowDone;  // 切り上げ
    if (weeks > kTrendMaxForecastWeeks) {
      f.reason = ForecastReason::TooFar;
    } else {
      f.ok = true;
      f.reason = ForecastReason::None;
      f.weeksAhead = static_cast<int>(weeks);
      f.finishWeek = nowWeek + 7 * f.weeksAhead;
    }
  }
  return out;
}

}  // namespace roadmap
