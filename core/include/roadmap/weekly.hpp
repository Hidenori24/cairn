// 週次レポート（Issue #43）: 今週の完了・来週の予定・遅れ・リスクを、見出しごとのリストを持つ構造体にまとめ、
// Markdown と HTML の別々のレンダラが文字にする。UI・Win32・ImGui に依存しない純粋な関数（Linux でテストできる）。
// 週は月曜始まり（統計・負荷と同じ）。担当者名・件名が出力に入る。
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "roadmap/baseline.hpp"
#include "roadmap/calendar.hpp"
#include "roadmap/clock.hpp"
#include "roadmap/engine.hpp"
#include "roadmap/filestore.hpp"
#include "roadmap/load.hpp"
#include "roadmap/mta.hpp"
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
  bool baseline = false;    // ベースラインとの差（表示するベースラインがあるときだけ出る。既定オフ。Issue #53）
  bool any() const {
    return summary || done || progress || next || risks || milestones || assignees || changes || notes || baseline;
  }
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
  // 比べる元。prev を直接渡したときはそれを使う（until = nullptr なら「いま」と比べる）。
  // prev がなく memo・autos があるときは、weeklyChooseCompare が期間に合わせて選ぶ（Issue #70）。どれもなければ「先週から変わったこと」は出ない
  const WeeklySnapshot* prev = nullptr;
  bool prevAuto = false;                  // prev は自動（週の頭）のスナップショット
  const WeeklySnapshot* until = nullptr;  // 比べる終わり（自動のスナップショット）。nullptr = いま
  const WeeklyMemo* memo = nullptr;       // 手動（コピー・保存したとき）のスナップショット
  const WeeklyAuto* autos = nullptr;      // 自動（週の頭）のスナップショット
  const BaselineCompare* baseline = nullptr;  // 表示するベースラインとの比較（なければ「ベースラインとの差」は出ない）
  int mtaLateDays = kMtaDefaultLateDays;      // マイルストーンの予測の遅れの警告のしきい値（営業日。autos があるとき、Issue #75）
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
  bool hasPrev = false;   // 「いま」と比べたときだけ立つ（自動のスナップショット同士の比較ではサマリーに増減を出さない）
  Day prevBase = 0;
  bool prevAuto = false;  // 比べた相手は自動（週の頭）のスナップショット
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
  bool sinceAuto = false;   // 比べた相手は自動（週の頭）のスナップショット（Issue #70）
  bool hasUntil = false;    // 比べた終わりもスナップショット（いまではない）。件名・担当・余裕はいまの値
  Day until = 0;            // その基準日
  bool truncated = false;  // 比べた相手が一部の課題だけ
  std::vector<WeeklyFinishMove> finishMoved;  // 見込みの完了日が動いた（動きの大きい順）
  int finishMovedTotal = 0;
  WeeklyList newlyLate;  // 新たに遅れた
  WeeklyList newlyDone;  // 新しく完了した
  int added = 0;         // 新しく増えた作業の数
  int removed = 0;       // 無くなった作業の数
};

// ベースラインとの差の 1 行
struct WeeklyBaselineRow {
  int index = -1;              // Tree の添字（消えた課題は -1）
  std::string id;              // Issue Id
  std::string did, name;       // 表示 ID・件名（消えた課題は did が空で、name は保存していた件名）
  std::string assignee;
  OptDay planFinish;           // 計画の完了日
  Day nowFinish = 0;           // いまの見込み（消えた課題では意味がない）
  int diffDays = 0;            // 完了日の差（営業日。+ は遅れ）
  bool differs = false;        // 完了日そのものが違う（営業日の差が 0 でも立つことがある）
  bool done = false;
};
struct WeeklyBaselineList {
  std::vector<WeeklyBaselineRow> items;  // 上限までの先頭
  int total = 0;
  int omitted() const { return total - static_cast<int>(items.size()); }
};
struct WeeklyBaseline {
  bool available = false;
  std::string name, at;        // ベースラインの名前・保存日時 "YYYY-MM-DDTHH:MM"
  bool truncated = false;      // 比べた相手は一部の課題だけ
  BaselineSummary sum;
  WeeklyBaselineList late;     // 遅れ（大きい順）
  WeeklyBaselineList early;    // 早まり（大きい順）
  WeeklyBaselineList added;    // 計画になかった
  WeeklyBaselineList removed;  // 消えた
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
  WeeklyBaseline baseline;
  // マイルストーンの予測の推移と遅れの警告（Issue #75）。autos（自動のスナップショット）を渡したときだけ。名前は cleanText 済み。
  // 警告は「遅れ・余裕小」の節に、推移の表は「マイルストーン」の節に出す
  bool mtaAvailable = false;
  MtaReport mta;          // series は警告のあるものを先頭に、上限（maxItems）までにする
  int mtaOmitted = 0;     // 上限で省いた系列の数
  std::string notes;      // 所感（そのまま）
  bool empty = false;     // 課題がない
};

// ---------------------------------------------------------------- 比べる元の選び方（Issue #70）
struct WeeklyCompare {
  const WeeklySnapshot* from = nullptr;  // 比べる元（nullptr = 元なし）
  bool fromAuto = false;
  const WeeklySnapshot* to = nullptr;    // 比べる終わり（nullptr = いま）
  explicit operator bool() const { return from != nullptr; }
};
// 期間に合わせて比べる元を選ぶ。週は月曜始まりで、自動のスナップショットは取った日の週の「頭の状態」として扱う。
//  - 今週: 今週の頭の自動と、いま。なければ、今週より前の手動・自動のうち新しいほう（同じ日なら自動）と、いま
//  - 先週: 先週の頭の自動（なければそれより前で一番近い自動）と、今週の頭の自動（なければ、いま）。
//          比べる元になる自動がなければ、従来どおり今週より前の手動と、いま
//  - 任意の範囲: 範囲の始まり以前で一番新しい自動と、範囲の終わり以前で一番新しい自動（範囲が今週に届くなら、いま）。
//          始まりの自動がなければ従来どおりの手動と、いま。終わりの自動が始まりと同じなら元なし
// base は基準日（週報の期間と同じ）。どちらも空なら元なし
WeeklyCompare weeklyChooseCompare(const WeeklyMemo& manual, const WeeklyAuto& autos, Day base, WeeklyPeriod period,
                                  const WeeklyRange& range);

// 週次レポートを組み立てる。tree・issues・rows の大きさが合わないとき・課題がないときは empty = true の空のレポート
WeeklyReport buildWeeklyReport(const WeeklyInput& in, const WeeklyOptions& opt);

// いまの状態を簡易スナップショットにする（書き出したときに設定へ残す）
WeeklySnapshot makeWeeklySnapshot(const WeeklyInput& in);
// 同じ（負荷を使わない）。自動のスナップショットを取る側（Session）が WeeklyInput を作らずに済むように
WeeklySnapshot makeWeeklySnapshot(const Tree& tree, const std::vector<Issue>& issues, const std::vector<ScheduleRow>& rows,
                                  const ProjectStats& stats, Day base);

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
