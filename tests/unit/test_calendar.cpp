#include <doctest/doctest.h>

#include "../test_util.hpp"
#include "roadmap/calendar.hpp"

using namespace roadmap;
using testutil::D;

TEST_CASE("calendar: 曜日（負の日でも正しい）") {
  CHECK(dow(0) == 4);   // 1970-01-01 は木曜
  CHECK(dow(-1) == 3);  // 1969-12-31 は水曜
  CHECK(dow(-4) == 0);
  CHECK(dow(D("2026-10-19")) == 1);  // 月曜
  CHECK(fmtIso(-1) == "1969-12-31");
  CHECK(*parseIso("1969-12-31") == -1);
}

TEST_CASE("calendar: 月末・うるう年・存在しない日") {
  CHECK(makeDay(2024, 2, 29).has_value());
  CHECK_FALSE(makeDay(2026, 2, 29).has_value());
  CHECK_FALSE(makeDay(2026, 2, 30).has_value());
  CHECK_FALSE(makeDay(2100, 2, 29).has_value());
  CHECK(makeDay(2000, 2, 29).has_value());
  CHECK_FALSE(makeDay(2026, 13, 1).has_value());
  CHECK_FALSE(makeDay(2026, 4, 31).has_value());
  CHECK(fmtIso(D("2026-12-31") + 1) == "2027-01-01");
}

TEST_CASE("calendar: workday / lastWork / floatDays") {
  const Calendar cal({D("2026-11-03")});
  const Day fri = D("2026-10-30"), mon = D("2026-11-02");
  CHECK(cal.workday(fri, 0) == fri);
  CHECK(cal.workday(fri, 1) == mon);
  CHECK(cal.workday(mon, 1) == D("2026-11-04"));  // 休業日を飛ばす
  CHECK(cal.workday(D("2026-11-04"), -1) == mon);
  CHECK(cal.workday(D("2026-10-31"), 1) == mon);  // 土曜から
  CHECK(cal.workday(D("2026-10-31"), 0) == D("2026-10-31"));
  CHECK(cal.lastWork(D("2026-11-01")) == fri);
  CHECK(cal.lastWork(mon) == mon);
  CHECK(cal.lastWork(D("2026-11-03")) == mon);
  CHECK(cal.nd(fri, D("2026-11-04")) == 3);
  CHECK(cal.floatDays(fri, D("2026-11-04")) == 2);
  CHECK(cal.floatDays(D("2026-11-04"), fri) == -2);
  CHECK(cal.floatDays(fri, fri) == 0);
  CHECK(Calendar::weekdaysBetween(fri, D("2026-11-04")) == 4);
}

TEST_CASE("calendar: 休業日テキスト（区切り4種・不正なトークンは無視）") {
  auto v = parseHolidays("2026-11-03 2026-11-23,2026-12-29;2026-12-30、2026-12-31\nxx 2026-02-30");
  REQUIRE(v.size() == 5);
  CHECK(v[4] == D("2026-12-31"));
  CHECK(parseHolidays(defaultHolidaysText()).size() == 33);
}

TEST_CASE("calendar: 表示の書式") {
  CHECK(fmtLong(D("2026-10-19")) == "2026/10/19(月)");
  CHECK(fmtShort(D("2026-10-19")) == "10/19");
  CHECK(fmtMd(D("2026-10-19")) == "10/19(月)");
}
