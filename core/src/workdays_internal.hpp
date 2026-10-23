// 営業日の数え方（core の .cpp だけが使う）。休業日を前処理して使い回す。
// 平日の数は式で出し（範囲の長さに依らない）、休業日は平日にあたるものだけを並べ替えて二分探索で引く。
// Calendar::nd のような日ごとの反復をしないので、極端な日付でも遅くならない（trends・evm が使う）
#pragma once

#include <algorithm>
#include <climits>
#include <vector>

#include "roadmap/calendar.hpp"

namespace roadmap::detail {

// 日 n より前（1970-01-01 以前も含む）の平日（月〜金）の数。1970-01-01 は木曜
inline long long weekdaysBefore(long long n) {
  const long long k = n + 3;  // 月曜を 0 にずらす
  long long q = k / 7, r = k % 7;
  if (r < 0) {
    r += 7;
    --q;
  }
  return q * 5 + std::min<long long>(r, 5);
}

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
  bool isWork(Day d) const {
    const int w = dow(d);
    return w >= 1 && w <= 5 && !std::binary_search(hol_.begin(), hol_.end(), d);
  }

 private:
  std::vector<Day> hol_;  // 平日にあたる休業日（昇順）
};

}  // namespace roadmap::detail
