// 日付と営業日カレンダー（SPEC 5章）。日付は 1970-01-01 からの日数（UTC）。
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace roadmap {

using Day = std::int32_t;
using OptDay = std::optional<Day>;

struct Ymd {
  int y = 1970;
  int m = 1;
  int d = 1;
};

Day daysFromCivil(int y, int m, int d);
Ymd civilFromDays(Day n);
int daysInMonth(int y, int m);
// 存在する日付なら Day。存在しない日付（2月30日など）は nullopt
OptDay makeDay(int y, int m, int d);
// 0=日曜 … 6=土曜。負の日でも正しい
int dow(Day n);

// "YYYY-MM-DD"（年の桁は詰めない。JS版の Sched.fmt と同じ）
std::string fmtIso(Day n);
std::string fmtIso(const OptDay& n);  // なしは空
// 厳密な "YYYY-MM-DD"
OptDay parseIso(std::string_view s);

// 画面表示: 2026/10/19(月)、10/19、10/19(月)
std::string fmtLong(Day n);
std::string fmtShort(Day n);
std::string fmtMd(Day n);
const char* dowName(Day n);  // 「日」…「土」

// 休業日テキスト（空白・カンマ・セミコロン・読点区切り）を読む。読めないトークンは無視
std::vector<Day> parseHolidays(std::string_view text);
std::string defaultHolidaysText();  // golden/holidays.txt の33日（改行区切り）

class Calendar {
 public:
  Calendar() = default;
  explicit Calendar(const std::vector<Day>& holidays);
  static Calendar fromText(std::string_view holidaysText) { return Calendar(parseHolidays(holidaysText)); }

  bool isHoliday(Day n) const { return hol_.count(n) != 0; }
  bool isWork(Day n) const {
    const int w = dow(n);
    return w >= 1 && w <= 5 && !isHoliday(n);
  }
  Day workday(Day n, int k) const;
  Day lastWork(Day n) const { return workday(n + 1, -1); }
  int nd(Day a, Day b) const;
  int floatDays(Day a, Day b) const { return b >= a ? nd(a, b) - 1 : -(nd(b, a) - 1); }
  static int weekdaysBetween(Day a, Day b);
  const std::unordered_set<Day>& holidays() const { return hol_; }

 private:
  std::unordered_set<Day> hol_;
};

}  // namespace roadmap
