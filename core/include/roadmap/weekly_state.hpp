// 週次レポートがプロジェクトの設定（project.settings.json の "weekly"）に覚えるもの（Issue #43）:
//  - 所感（次の週の下書きに前回の所感を残す）
//  - 書き出したときの簡易スナップショット（課題ごとの状況・見込みの完了日。次回の「先週から変わったこと」に使う）
// UI・Win32 に依存しない。読み込み時に壊れた値を検査して読み飛ばす。
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "roadmap/calendar.hpp"

namespace roadmap {

// スナップショット 1 つに持つ課題の数の上限（超えた分は持たず、truncated を立てる）
inline constexpr std::size_t kWeeklyMaxSnapshotItems = 5000;
// 課題 1 件の Issue Id の長さの上限（バイト）
inline constexpr std::size_t kWeeklyMaxIdBytes = 64;
// 覚えておくスナップショットの数（基準日の週のものと、それより前の週のもの）
inline constexpr std::size_t kWeeklyMaxSnapshots = 2;
// 所感の長さの上限（バイト）
inline constexpr std::size_t kWeeklyMaxNoteBytes = 20000;

struct WeeklySnapItem {
  std::string id;  // Issue Id（CSV の値。表示 ID は階層で変わるので使わない）
  char st = 't';   // 't' 未着手 / 'p' 進行中 / 'd' 完了
  OptDay finish;   // 見込みの完了日（完了済みなら完了日。なければ最早の完了日）
  bool late = false;  // 未完了で余裕が負
  bool operator==(const WeeklySnapItem&) const = default;
};

struct WeeklySnapshot {
  Day base = 0;  // 書き出したときの基準日
  double progress = 0;  // 進み（0〜1）
  int tasks = 0, done = 0, late = 0, tight = 0;
  OptDay finish;  // プロジェクトの完了見込み日
  bool truncated = false;  // 課題が多すぎて、先頭の分だけ持っている
  std::vector<WeeklySnapItem> items;
  bool operator==(const WeeklySnapshot&) const = default;
};

struct WeeklyMemo {
  std::string notes;                  // 所感・課題・お願い
  std::vector<WeeklySnapshot> snaps;  // 基準日の古い順。同じ週のものは 1 つ
  bool empty() const { return notes.empty() && snaps.empty(); }
  bool operator==(const WeeklyMemo&) const = default;
};

// 日付の週（月曜始まり）の月曜
Day weeklyMondayOf(Day d);

// スナップショットを入れる。同じ週（月曜始まり）のものは置き換え、古いものは kWeeklyMaxSnapshots 件まで残す。
// items は上限に切り詰め（truncated を立てる）、Issue Id が長すぎる・重複する項目は捨てる
void weeklyPutSnapshot(WeeklyMemo& m, WeeklySnapshot s);
// base の週より前の週のスナップショットのうち、一番新しいもの（なければ nullptr）
const WeeklySnapshot* weeklyPreviousSnapshot(const WeeklyMemo& m, Day base);

// 所感を上限に切り詰める（文字の途中では切らない。不正な UTF-8 は U+FFFD にする。NUL などの制御文字は捨てる）
std::string weeklySanitizeNotes(const std::string& s);

// settings.json の "weekly" との変換。読み込みでは壊れた値を読み飛ばす（既定のまま）
nlohmann::ordered_json weeklyToJson(const WeeklyMemo& m);
WeeklyMemo weeklyFromJson(const nlohmann::ordered_json& j);

}  // namespace roadmap
