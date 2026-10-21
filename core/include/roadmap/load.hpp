// 担当者別の負荷（週×担当）と、過去の完了（担当者別）の集計（統計ビューが使う。UI に依存しない）
#pragma once

#include <string>
#include <vector>

#include "roadmap/engine.hpp"
#include "roadmap/stats.hpp"

namespace roadmap {

// 負荷を出す最大の週の数（基準日の週を含む）。これより先は最後の週にまとめる
inline constexpr int kLoadWeeks = kFutureWeeks;
// セルに持つ課題の添字の上限（ツールチップ用。全件の数は LoadCell::n）
inline constexpr int kLoadCellIds = 8;

struct LoadCell {
  double days = 0;       // その週にその担当へ広がった作業の営業日数（均等に広げた合計）
  double ratio = 0;      // 負荷の割合 = days ÷（その週の営業日数 × 同時作業の上限）。1.0 = 100%
  int n = 0;             // そのセルに入る作業の数
  std::vector<int> ids;  // そのうち先頭の kLoadCellIds 件（Tree の添字。leafOrder の順）
};

struct LoadRow {
  std::string name;  // 担当者名（空 = 担当なし）
  double totalDays = 0;
  double peak = 0;   // セルの割合の最大
  std::vector<LoadCell> cells;  // LoadReport::weekStarts と同じ数
};

struct DoneRow {
  std::string name;  // 担当者名（空 = 担当なし）
  int n = 0;         // 完了した作業の数（過去 kPastWeeks 週）
  double weeks = 0;  // 完了した作業の期間（週）の合計
  std::vector<int> perWeek;  // 週ごとの完了数（LoadReport::doneWeekStarts と同じ数）
  double avgWeeks() const { return n > 0 ? weeks / n : 0; }
};

struct LoadReport {
  // ---- 負荷（未完了の作業。マイルストーンを除く）----
  std::vector<Day> weekStarts;  // 各週の月曜。基準日の週から。未完了の作業がなければ空
  std::vector<int> workdays;    // 各週の営業日数（休業日を除く）。最後の週が「以降」なら、その範囲全体の営業日数
  bool lastFolded = false;      // 最後の週が kLoadWeeks より先をまとめたもの
  int cap = 1;                  // 100% のときの同時作業数（資源制限をオンにしているときだけ 2 以上になりうる）
  std::vector<LoadRow> load;    // 負荷の合計が大きい順。担当なしは最後
  // ---- 成果（完了した作業。基準日の週を含まない過去 kPastWeeks 週）----
  std::vector<Day> doneWeekStarts;  // 古い週から kPastWeeks 個
  std::vector<DoneRow> done;        // 名前順（順位にしない）。担当なしは最後
};

// issues・R は T と同じ時点のもの（Session::viewIssues()・rows() など）。
// 負荷: 未完了の作業ごとに、最早の開始〜最早の完了の営業日へ、期間（ScheduleRow::days。0 日以下は 1 日）を均等に広げ、
//       担当者ごと・週ごと（月曜始まり）に足す。割合の分母は「その週の営業日数 × cap」（cap は resource.on のときだけ resource.cap）。
// 成果: Issue::resolved の日付が過去 kPastWeeks 週に入る完了済みの作業を、担当者ごとに数える。
LoadReport computeLoad(const Tree& T, const std::vector<Issue>& issues, const std::vector<ScheduleRow>& R,
                       const Calendar& cal, Day base, const ResourceLimit& resource);

// 割合 → 「85」のような整数のパーセント（四捨五入。巨大な値は 99999 に丸める）
int loadPercent(double ratio);

}  // namespace roadmap
