// 時計の差し替え口（テストでは固定の時計を使う）
#pragma once

#include <cstdint>
#include <string>

#include "roadmap/calendar.hpp"

namespace roadmap {

struct LocalTime {
  int y = 2026, mo = 1, d = 1, h = 0, mi = 0, s = 0;
};

class IClock {
 public:
  virtual ~IClock() = default;
  virtual LocalTime now() const = 0;           // ローカル時刻
  virtual std::int64_t epochMs() const = 0;    // UTC のミリ秒（id の採番・ISO時刻）
  virtual std::int64_t monotonicMs() const = 0;  // タイマー用

  Day today() const;
  std::string todayIso() const;
  std::string nowDT() const;      // "YYYY-MM-DDTHH:MM"
  std::string stamp() const;      // "YYYYMMDD-HHMMSS"（バックアップのファイル名）
  std::string isoUtc() const;     // "2026-10-19T09:00:00.000Z"
  std::string hhmm() const;       // "18:36"
};

class SystemClock final : public IClock {
 public:
  LocalTime now() const override;
  std::int64_t epochMs() const override;
  std::int64_t monotonicMs() const override;
};

class FixedClock final : public IClock {
 public:
  explicit FixedClock(LocalTime t, std::int64_t epoch = 1792400000000) : t_(t), epoch_(epoch) {}
  LocalTime now() const override { return t_; }
  std::int64_t epochMs() const override { return epoch_; }
  std::int64_t monotonicMs() const override { return mono_; }
  void set(LocalTime t) { t_ = t; }
  void advanceMs(std::int64_t ms) {
    mono_ += ms;
    epoch_ += ms;
  }

 private:
  LocalTime t_;
  std::int64_t epoch_;
  std::int64_t mono_ = 0;
};

}  // namespace roadmap
