// 「今日・今週やること」の集計（Issue #72。画面の「今日」ビューが使う。UI に依存しない）
// 今日（IClock の今日。基準日ではない）を軸に、日程の計算結果（最早・最遅・余裕）から、
// 今日の節（遅れ・期限・進行中・着手待ち・マイルストーン）と、今週（月曜始まり）の日ごとの列を作る。
// 数える対象は葉（子を持たない課題）。親のまとめ行は出さない。完了済みは出さない。
#pragma once

#include <array>
#include <string>
#include <vector>

#include "roadmap/engine.hpp"
#include "roadmap/result.hpp"

namespace roadmap {

// 今週の列の数（月〜金）
inline constexpr int kTodayWeekDays = 5;
// 余裕小の境（日）。統計・上部バーと同じ（0〜10 日）
inline constexpr int kTodayTightDays = 10;

// 担当の絞り込み
struct TodayFilter {
  bool byAssignee = false;  // true = assignee（前後の空白を除いた完全一致）の課題だけ。false = 全員
  std::string assignee;
  bool operator==(const TodayFilter&) const = default;
};

// 今週の 1 日ぶんの列
struct TodayDay {
  Day day = 0;
  bool holiday = false;  // 休業日（カレンダーの休業日。月〜金のうち休みの日）
  bool isToday = false;
  std::vector<std::string> ids;  // その日が「期限／終了予定」の未完了の課題（Issue Id。余裕の小さい順 → 木の順）
};

// 進行中の帯（週の上段）。first〜last は列の添字（0 = 月 … 4 = 金）
struct TodayBand {
  std::string id;
  int first = 0, last = 0;
  bool cutLeft = false;   // 開始が今週より前（左へはみ出す）
  bool cutRight = false;  // 完了が今週より後（右へはみ出す）
};

struct TodayWeekSummary {
  int completing = 0;    // 今週の完了予定（列に載る課題の数）
  int late = 0;          // うち遅れ
  int tight = 0;         // うち余裕小（遅れではない）
  int lateElsewhere = 0; // 列に載らない遅れ（今週より前から遅れている・日付が今週の外）。「今日」の「遅れている」で見る
};

struct TodayReport {
  Day today = 0;
  Day weekStart = 0;          // 今週の月曜
  bool todayIsWork = true;    // 今日は営業日（土日・休業日なら false）
  // 今日の節。課題は 1 つの節にだけ入る（遅れ → 今日が期限・終了予定 → 進行中 → 今日から始められる の優先順。
  // マイルストーンは 遅れ → 今日のマイルストーン）。値は Issue Id
  std::vector<std::string> late;        // 遅れている
  std::vector<std::string> dueToday;    // 今日が期限・終了予定
  std::vector<std::string> inProgress;  // 進行中（余裕の小さい順）
  std::vector<std::string> canStart;    // 今日から始められる（先行がすべて完了・最早の開始が今日以前・未着手）
  std::vector<std::string> milestones;  // 今日のマイルストーン
  std::array<TodayDay, kTodayWeekDays> week;
  std::vector<TodayBand> bands;         // 進行中の帯
  TodayWeekSummary summary;
  // 担当の絞り込みを掛ける前の、未完了の葉の数（絞り込みで 0 件になったことを案内するため）
  int openAll = 0;
  int total() const {
    return static_cast<int>(late.size() + dueToday.size() + inProgress.size() + canStart.size() + milestones.size());
  }
};

// issues・R は T と同じ時点のもの（Session::viewIssues()・rows() など）。today は IClock の今日（基準日ではない）。
// エラー（添字の食い違い）は戻り値で返す。例外は投げない
Result<TodayReport> computeToday(const Tree& T, const std::vector<Issue>& issues, const std::vector<ScheduleRow>& R,
                                 const Calendar& cal, Day today, const TodayFilter& filter = {});

// 日（Day）の週の月曜
Day mondayOf(Day d);

// ---- その場の「ずらす」操作 ----
// 「明日にずらす」= 今日の次の営業日へ。「来週にずらす」= 5 営業日あと（来週の同じ曜日。休業日は数えない）
enum class ShiftTo { Tomorrow, NextWeek };

// ずらしたあとの日付（変えない項目は元のまま）。純粋関数（課題は変えない）
struct ShiftEdit {
  OptDay start, due, deadline, notBefore;
  bool changed = false;  // どれかの日付が変わる
  Day target = 0;        // ずらす先の最初の営業日（明日／来週の最初の営業日）
  int days = 0;          // ずらす営業日数（1 / 5）
};

// 規則（B48）: 開始日・期限（Due／Deadline）は営業日で days ぶん後ろへ。ただしずらす先（target）より前には置かない
// （過ぎた日付は target に揃える）。着手可能日（notBefore）は未着手のとき target 以降にする（すでに後ろならそのまま）。
// 進行中は開始と着手可能日を変えない（期限だけ）。完了は変えない
ShiftEdit planShift(const Issue& o, Cat cat, const Calendar& cal, Day today, ShiftTo to);

class Session;
struct ShiftResult {
  bool ok = false;   // 課題を変えた（Undo 1回）
  Day target = 0;
  int moved = 0;     // この課題のほかに、日程（最早の開始）が動いた課題の数
  bool waited = false;  // 先行などのため、ずらす先の日には始められない（通知に出した）
};
// 課題 id を Session::mutate で 1 回だけ編集する（Undo 1回）。親・完了・存在しない課題・変えられるものがないときは何も変えず
// 理由を通知して ok = false。先行との整合（先行が終わるまで待つ・後続の日程が動く）は通知に出す
ShiftResult shiftIssue(Session& s, const std::string& id, ShiftTo to, Day today);

}  // namespace roadmap
