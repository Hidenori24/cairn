// プロジェクトの統計（画面の「統計」ビューが使う集計。UI に依存しない）
#pragma once

#include <string>
#include <vector>

#include "roadmap/engine.hpp"

namespace roadmap {

// 余裕の分布の階級（未完了の作業）: 遅れ（<0日）/ 0〜5日 / 6〜10日 / 11〜20日 / 21日以上
inline constexpr int kFloatBuckets = 5;
// 過去の完了を数える週の数（基準日の週を含まない）
inline constexpr int kPastWeeks = 8;
// 先の完了予定を数える最大の週の数（基準日の週を含む）。これより先は最後の週にまとめる
inline constexpr int kFutureWeeks = 26;

struct GroupStat {
  std::string name;  // 担当者名（空 = 未割当）/ ルート課題の「表示ID 件名」
  int n = 0;         // 作業の数（マイルストーンを除く）
  int nDone = 0, nProg = 0, nTodo = 0;
  int nLate = 0, nTight = 0;     // 未完了の遅れ・余裕小
  double weeks = 0;              // 全体の期間（週）
  double weeksDone = 0;          // うち完了分
  bool hasFinish = false;
  Day finish = 0;                // 未完了の作業の最早完了日の最大
  int minFloat = 0;              // 未完了の作業の余裕の最小（未完了が無ければ 0）
  bool hasOpen = false;
  double weeksOpen() const { return weeks - weeksDone; }
  double progress() const {  // 0〜1。期間が無いときは件数で
    if (weeks > 0) return weeksDone / weeks;
    return n > 0 ? static_cast<double>(nDone) / n : 0;
  }
};

struct WeekBucket {
  Day start = 0;      // その週の月曜
  int actual = 0;     // 実績: 完了した数（Resolved の日付）。基準日の週より前だけ
  int forecast = 0;   // 見込み: 最早の完了が入る数（未完了の作業）。基準日の週以降だけ
};

struct ProjectStats {
  int tasks = 0;  // 作業の数（葉。マイルストーンを除く）
  int done = 0, prog = 0, todo = 0;
  int late = 0, tight = 0, ok = 0;  // 未完了の作業の内訳
  int unassigned = 0;               // 未完了で担当者がいない
  int noDeadline = 0;               // 未完了で実効の期限が無い
  int milestones = 0, msDone = 0, msLate = 0;
  double weeks = 0, weeksDone = 0;
  bool hasFinish = false;
  Day finish = 0;  // 未完了の作業の最早完了日の最大（プロジェクトの完了見込み）
  int floatHist[kFloatBuckets] = {0, 0, 0, 0, 0};
  int prioOpen[5] = {0, 0, 0, 0, 0}, prioDone[5] = {0, 0, 0, 0, 0};  // Highest…Lowest
  std::vector<GroupStat> byAssignee;  // 未完了の週が多い順。未割当は最後
  std::vector<GroupStat> byRoot;      // ルート課題の順。親のない単独の作業は最後に1つにまとめる
  std::vector<WeekBucket> weeksTimeline;  // 過去 kPastWeeks 週 + 基準日の週 + 先 …（最大 kFutureWeeks 週）
  int nowIndex = 0;                       // weeksTimeline で基準日の週の添字
  double progress() const {
    if (weeks > 0) return weeksDone / weeks;
    return tasks > 0 ? static_cast<double>(done) / tasks : 0;
  }
};

// issues・R は T と同じ時点のもの（Session::viewIssues()・rows() など）
ProjectStats computeStats(const Tree& T, const std::vector<Issue>& issues, const std::vector<ScheduleRow>& R, Day base);

// 余裕（日）がどの階級に入るか（0〜kFloatBuckets-1）
int floatBucket(int floatDays);

}  // namespace roadmap
