#include "roadmap/calendar.hpp"

#include <cstdlib>

#include "roadmap/csv.hpp"
#include "roadmap/text.hpp"

namespace roadmap {

// Howard Hinnant の days_from_civil / civil_from_days
Day daysFromCivil(int y, int m, int d) {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const int yoe = y - era * 400;
  const int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

Ymd civilFromDays(Day n) {
  const int z = n + 719468;
  const int era = (z >= 0 ? z : z - 146096) / 146097;
  const int doe = z - era * 146097;
  const int yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const int y = yoe + era * 400;
  const int doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const int mp = (5 * doy + 2) / 153;
  const int d = doy - (153 * mp + 2) / 5 + 1;
  const int m = mp + (mp < 10 ? 3 : -9);
  return {y + (m <= 2), m, d};
}

int daysInMonth(int y, int m) {
  static const int kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (m == 2) {
    const bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
    return leap ? 29 : 28;
  }
  return kDays[m - 1];
}

OptDay makeDay(int y, int m, int d) {
  if (m < 1 || m > 12 || d < 1 || d > daysInMonth(y, m)) return std::nullopt;
  return daysFromCivil(y, m, d);
}

int dow(Day n) { return ((n % 7) + 7 + 4) % 7; }

namespace {
std::string pad2(int v) { return (v < 10 ? "0" : "") + std::to_string(v); }
const char* const kDow[] = {"日", "月", "火", "水", "木", "金", "土"};
}  // namespace

std::string fmtIso(Day n) {
  const Ymd c = civilFromDays(n);
  return std::to_string(c.y) + "-" + pad2(c.m) + "-" + pad2(c.d);
}
std::string fmtIso(const OptDay& n) { return n ? fmtIso(*n) : std::string(); }

OptDay parseIso(std::string_view s) {
  if (s.size() != 10 || s[4] != '-' || s[7] != '-') return std::nullopt;
  for (int i : {0, 1, 2, 3, 5, 6, 8, 9})
    if (s[i] < '0' || s[i] > '9') return std::nullopt;
  const int y = std::atoi(std::string(s.substr(0, 4)).c_str());
  const int m = (s[5] - '0') * 10 + (s[6] - '0');
  const int d = (s[8] - '0') * 10 + (s[9] - '0');
  return makeDay(y, m, d);
}

const char* dowName(Day n) { return kDow[dow(n)]; }
std::string fmtLong(Day n) {
  const Ymd c = civilFromDays(n);
  return std::to_string(c.y) + "/" + std::to_string(c.m) + "/" + std::to_string(c.d) + "(" + dowName(n) + ")";
}
std::string fmtShort(Day n) {
  const Ymd c = civilFromDays(n);
  return std::to_string(c.m) + "/" + std::to_string(c.d);
}
std::string fmtMd(Day n) { return fmtShort(n) + "(" + dowName(n) + ")"; }

std::vector<Day> parseHolidays(std::string_view textIn) {
  std::vector<Day> out;
  for (const auto& tok : text::splitAny(textIn, U",;、", true)) {
    if (auto d = csv::parseDate(tok)) out.push_back(*d);
  }
  return out;
}

std::string defaultHolidaysText() {
  return "2026-11-03\n2026-11-23\n2026-12-29\n2026-12-30\n2026-12-31\n2027-01-01\n2027-01-02\n"
         "2027-01-03\n2027-01-11\n2027-02-11\n2027-02-23\n2027-03-22\n2027-04-29\n2027-05-03\n"
         "2027-05-04\n2027-05-05\n2027-07-19\n2027-08-11\n2027-09-20\n2027-09-23\n2027-10-11\n"
         "2027-11-03\n2027-11-23\n2027-12-29\n2027-12-30\n2027-12-31\n2028-01-01\n2028-01-02\n"
         "2028-01-03\n2028-01-10\n2028-02-11\n2028-02-23\n2028-03-20";
}

Calendar::Calendar(const std::vector<Day>& holidays) : hol_(holidays.begin(), holidays.end()) {}

Day Calendar::workday(Day n, int k) const {
  if (k == 0) return n;
  const int step = k > 0 ? 1 : -1;
  int c = k > 0 ? k : -k;
  // 休業日が続いても必ず終わる（土日以外の平日は有限個しか休めない前提）。念のため上限を置く
  long guard = 0;
  while (c > 0 && guard < 10'000'000) {
    n += step;
    if (isWork(n)) --c;
    ++guard;
  }
  return n;
}

int Calendar::nd(Day a, Day b) const {
  int c = 0;
  for (Day d = a; d <= b; ++d)
    if (isWork(d)) ++c;
  return c;
}

int Calendar::weekdaysBetween(Day a, Day b) {
  int c = 0;
  for (Day d = a; d <= b; ++d) {
    const int w = dow(d);
    if (w >= 1 && w <= 5) ++c;
  }
  return c;
}

}  // namespace roadmap
