// 週次レポート（Issue #43）: 今週の完了・来週の予定・遅れ・リスクを、見出しごとのリストを持つ構造体にまとめ、
// Markdown と HTML の別々のレンダラが文字にする。UI・Win32・ImGui に依存しない純粋な関数（Linux でテストできる）。
// 週は月曜始まり（統計・負荷と同じ）。担当者名・件名が出力に入る。
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "roadmap/calendar.hpp"
#include "roadmap/clock.hpp"
#include "roadmap/engine.hpp"
#include "roadmap/filestore.hpp"
#include "roadmap/load.hpp"
#include "roadmap/result.hpp"
#include "roadmap/stats.hpp"
#include "roadmap/weekly_state.hpp"

namespace roadmap {

// 1 つのリストに出す件数の上限（超えた分は件数だけ数える）
inline constexpr std::size_t kWeeklyMaxItems = 100;
// 任意の範囲の長さの上限（日）
inline constexpr int kWeeklyMaxRangeDays = 366;
// 余裕小の基準（日）。stats と同じ
inline constexpr int kWeeklyTightDays = 10;
// 1 行に出す件名の長さの上限（文字）
inline constexpr std::size_t kWeeklyMaxNameChars = 200;

enum class WeeklyPeriod {
  ThisWeek,  // 基準日の週（月曜〜日曜）
  LastWeek,  // その前の週
  Custom     // from〜to（両端を含む）
};

struct WeeklyRange {
  Day from = 0, to = 0;  // 両端を含む
};
// 期間。Custom は from > to なら入れ替え、長さを kWeeklyMaxRangeDays 日までにする（超えたら to を切り詰める）
WeeklyRange weeklyRange(Day base, WeeklyPeriod p, Day from, Day to);

// 出す見出し（項目ごとにオン・オフ）
struct WeeklySections {
  bool summary = true;      // サマリー
  bool done = true;         // 今週の完了
  bool progress = true;     // 進行中
  bool next = true;         // 来週の予定
  bool risks = true;        // 遅れ・余裕小
  bool milestones = true;   // マイルストーン
  bool assignees = true;    // 担当別
  bool changes = true;      // 先週から変わったこと（スナップショットがあるときだけ出る）
  bool notes = true;        // 所感・課題・お願い
  bool any() const { return summary || done || progress || next || risks || milestones || assignees || changes || notes; }
};

struct WeeklyOptions {
  WeeklyPeriod period = WeeklyPeriod::ThisWeek;
  Day from = 0, to = 0;  // period == Custom のとき
  WeeklySections sections;
  std::string notes;     // 所感（自由に書く欄）
  std::size_t maxItems = kWeeklyMaxItems;  // テストで小さくできる
};

// 入力。tree・issues・rows・stats・load は同じ時点のもの（Session::tree()・viewIssues()・rows() と、それから計算したもの）。
// 参照は呼び出しの間だけ使う
struct WeeklyInput {
  WeeklyInput(const Tree& t, const std::vector<Issue>& i, const std::vector<ScheduleRow>& r, const Calendar& c,
              const ProjectStats& s, const LoadReport& l)
      : tree(t), issues(i), rows(r), calendar(c), stats(s), load(l) {}
  const Tree& tree;
  const std::vector<Issue>& issues;
  const std::vector<ScheduleRow>& rows;
  const Calendar& calendar;
  const ProjectStats& stats;
  const LoadReport& load;
  Day base = 0;
  std::string projectName;
  const WeeklySnapshot* prev = nullptr;  // 先週のスナップショット（なければ「先週から変わったこと」は出ない）
};

// ---------------------------------------------------------------- 結果の構造体
// 課題 1 件分の行
struct WeeklyItem {
  int index = -1;       // Tree の添字
  std::string did;      // 表示 ID
  std::string name;     // 件名
  std::string assignee; // 担当者（空 = 未割当）
  Day start = 0, finish = 0;  // 最早の開始・完了
  OptDay resolved;      // 完了日
  OptDay deadline;      // 実効の期限
  bool done = false, prog = false;
  int floatDays = 0;    // 余裕（営業日）
  int remainDays = 0;   // 進行中: 基準日から最早の完了までの営業日数（基準日を含む。過ぎていれば 0）
  // 遅れ・余裕小: 開始を決めている先行の課題（未完了のうち最早の完了が一番遅いもの）。なければ predIndex = -1
  int predIndex = -1;
  std::string predDid, predName;
  Day predFinish = 0;
  int predFloat = 0;
};

struct WeeklyList {
  std::vector<WeeklyItem> items;  // 上限までの先頭
  int total = 0;                  // 該当した全部の数
  int omitted() const { return total - static_cast<int>(items.size()); }
};

struct WeeklyMilestone {
  int index = -1;
  std::string did, name;
  bool achieved = false;
  OptDay resolved;       // 達成した日
  OptDay deadline;       // 期限
  Day forecast = 0;      // 見込みの完了日（達成済みなら達成した日）
  int diffDays = 0;      // 期限と見込みの差（営業日）。期限があるときだけ意味がある。負 = 期限より遅れる見込み
};

struct WeeklyAssigneeRow {
  std::string name;       // 空 = 未割当
  int doneInPeriod = 0;   // 期間内に完了した作業の数
  int inProgress = 0;     // 進行中の作業の数
  int open = 0;           // 未完了の作業の数
  int loadNextPercent = -1;  // 来週の負荷（%）。分からなければ -1
  int late = 0;           // 未完了のうち遅れ
};

struct WeeklySummary {
  double progress = 0;  // 0〜1（期間ベース）
  int tasks = 0, done = 0, prog = 0, todo = 0;
  int late = 0, tight = 0;
  int milestones = 0, msDone = 0, msLate = 0;
  bool hasFinish = false;
  Day finish = 0;       // 完了の見込み日（未完了の作業の最早完了日の最大）
  int doneInPeriod = 0;    // 期間内に完了した作業の数
  int doneNoDate = 0;      // 完了だが完了日のない作業の数（期間の集計に入らない）
  // 先週のスナップショットとの比較
  bool hasPrev = false;
  Day prevBase = 0;
  double progressDelta = 0;  // 進みの増減（0〜1 の差）
  int lateDelta = 0, tightDelta = 0, doneDelta = 0;
  bool hasFinishShift = false;
  int finishShiftDays = 0;   // 完了の見込み日の変化（営業日。正 = 遅くなった）
};

struct WeeklyFinishMove {
  int index = -1;
  std::string did, name, assignee;
  Day from = 0, to = 0;  // 先週の見込み → 今の見込み
  int shiftDays = 0;     // 営業日（正 = 遅くなった）
};

struct WeeklyChanges {
  bool available = false;
  Day since = 0;  // 比べた相手（スナップショット）の基準日
  bool truncated = false;  // 比べた相手が一部の課題だけ
  std::vector<WeeklyFinishMove> finishMoved;  // 見込みの完了日が動いた（動きの大きい順）
  int finishMovedTotal = 0;
  WeeklyList newlyLate;  // 新たに遅れた
  WeeklyList newlyDone;  // 新しく完了した
  int added = 0;         // 新しく増えた作業の数
  int removed = 0;       // 無くなった作業の数
};

struct WeeklyReport {
  std::string projectName;
  Day base = 0;
  WeeklyPeriod period = WeeklyPeriod::ThisWeek;
  WeeklyRange range;      // 期間
  WeeklyRange next;       // 次の 7 日間（来週の予定）
  int rangeWorkdays = 0, nextWorkdays = 0;  // 営業日数（休業日を除く）
  bool rangeCapped = false;  // 任意の範囲を上限に切り詰めた
  WeeklySections sections;
  WeeklySummary summary;
  WeeklyList done;        // 期間内に完了した作業（完了日の順）
  WeeklyList progress;    // 進行中（余裕の小さい順）
  WeeklyList nextStart;   // 来週に最早の開始が来る未完了の作業
  WeeklyList nextFinish;  // 来週に最早の完了が来る未完了の作業
  WeeklyList risks;       // 余裕が負、または kWeeklyTightDays 日以内の未完了の作業（余裕の小さい順）
  std::vector<WeeklyMilestone> milestones;
  int milestonesTotal = 0;
  std::vector<WeeklyAssigneeRow> assignees;  // 名前順（順位にしない）。未割当は最後
  int assigneesTotal = 0;
  WeeklyChanges changes;
  std::string notes;      // 所感（そのまま）
  bool empty = false;     // 課題がない
};

// 週次レポートを組み立てる。tree・issues・rows の大きさが合わないとき・課題がないときは empty = true の空のレポート
WeeklyReport buildWeeklyReport(const WeeklyInput& in, const WeeklyOptions& opt);

// いまの状態を簡易スナップショットにする（書き出したときに設定へ残す）
WeeklySnapshot makeWeeklySnapshot(const WeeklyInput& in);

// ---------------------------------------------------------------- レンダラ
// Markdown の本文に入れる文字列のエスケープ（インライン用）。\ ` * _ ~ [ ] < > & | を \ で打ち消し、改行・制御文字は空白・除去、
// 行頭になる #・-・+・>・数字. も打ち消す。壊れた UTF-8 は U+FFFD。長すぎる文字列は「…」で切る
std::string escapeMarkdown(std::string_view s);

// Markdown（UTF-8、BOM なし、LF）。所感は Markdown のままだが、"<" は打ち消す
std::string weeklyMarkdown(const WeeklyReport& r);
// 1 つの HTML（外部のファイル・スクリプトを使わない。印刷向けのライト）。clock があれば出力日時を書く
std::string weeklyHtml(const WeeklyReport& r, const IClock* clock = nullptr);

// 既定のファイル名 "<プロジェクト名>_週報_<期間の最初の日>.md" / ".html"
std::string weeklyFileName(const std::string& projectName, Day periodStart, bool html);
// 保存先の検査: 空でない・制御文字を含まない・ファイル名がある・拡張子が html のとき .html/.htm、Markdown のとき .md/.markdown/.txt
Status checkWeeklyPath(const std::string& path, bool html);
// 検査して書く（IFileStore::writeAtomic）。既存のフォルダの名前には書かない
Status writeWeekly(IFileStore& fs, const std::string& path, const std::string& content, bool html);

}  // namespace roadmap
