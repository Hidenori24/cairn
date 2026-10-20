#include "roadmap/clock.hpp"

#include <chrono>
#include <ctime>

namespace roadmap {

namespace {
std::string p2(int v) { return (v < 10 ? "0" : "") + std::to_string(v); }
}  // namespace

Day IClock::today() const {
  const LocalTime t = now();
  return daysFromCivil(t.y, t.mo, t.d);
}
std::string IClock::todayIso() const { return fmtIso(today()); }
std::string IClock::nowDT() const {
  const LocalTime t = now();
  return todayIso() + "T" + p2(t.h) + ":" + p2(t.mi);
}
std::string IClock::stamp() const {
  const LocalTime t = now();
  return std::to_string(t.y) + p2(t.mo) + p2(t.d) + "-" + p2(t.h) + p2(t.mi) + p2(t.s);
}
std::string IClock::hhmm() const {
  const LocalTime t = now();
  return p2(t.h) + ":" + p2(t.mi);
}
std::string IClock::isoUtc() const {
  const std::int64_t ms = epochMs();
  const std::int64_t sec = ms >= 0 ? ms / 1000 : (ms - 999) / 1000;
  const int msPart = static_cast<int>(ms - sec * 1000);
  const Day day = static_cast<Day>(sec >= 0 ? sec / 86400 : (sec - 86399) / 86400);
  const int sod = static_cast<int>(sec - static_cast<std::int64_t>(day) * 86400);
  std::string m3 = std::to_string(msPart);
  while (m3.size() < 3) m3 = "0" + m3;
  return fmtIso(day) + "T" + p2(sod / 3600) + ":" + p2(sod / 60 % 60) + ":" + p2(sod % 60) + "." + m3 + "Z";
}

LocalTime SystemClock::now() const {
  const std::time_t tt = std::time(nullptr);
  std::tm tm{};
#ifdef _WIN32
  localtime_s(&tm, &tt);
#else
  localtime_r(&tt, &tm);
#endif
  return {tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec};
}
std::int64_t SystemClock::epochMs() const {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}
std::int64_t SystemClock::monotonicMs() const {
  using namespace std::chrono;
  return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

}  // namespace roadmap
