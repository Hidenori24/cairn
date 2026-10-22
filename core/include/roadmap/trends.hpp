// 進み方の推移（Issue #54 の A）: バーンダウン／バーンアップ・リードタイム・滞留日数（統計ビューと HTML が使う。UI に依存しない）
// CSV にある日付（Created・Resolved・Updated・Start date）だけで出す。状況の履歴は持たない。
#pragma once

#include <array>
#include <string>
#include <vector>

#include "roadmap/engine.hpp"
#include "roadmap/result.hpp"

namespace roadmap {

// 週の数: 基準日の週の前 kTrendPastWeeks 週 + 基準日の週（データの最初の週より前は出さない）
inline constexpr int kTrendPastWeeks = 26;
// ペース予測に使う直近の週の数（基準日の週を含まない）
inline constexpr int kTrendPaceWeeks = 4;
// ペース予測を出す条件: 直近の週のうち、完了のある週がこの数以上
inline constexpr int kTrendMinActiveWeeks = 2;
// 予測がこれより先（週）になるときは出さない
inline constexpr int kTrendMaxForecastWeeks = 104;
// 階級の数: 0-1 / 2-4 / 5-9 / 10-19 / 20+ 営業日
inline constexpr int kTrendBuckets = 5;
// 滞留の上位に出す件数
inline constexpr int kTrendTopN = 10;

// 営業日数 → 階級（0〜kTrendBuckets-1）。負は 0 として扱う
int trendBucket(int businessDays);
// 階級の見出し（"0-1" "2-4" "5-9" "10-19" "20+"）。範囲外は空
const char* trendBucketLabel(int bucket);

// 絞り込み（統計ビューの担当者・ルート課題の選択と同じ流儀）
struct TrendFilter {
  enum class Who { All, Unassigned, Name };
  Who who = Who::All;
  std::string name;  // Who::Name のとき（前後の空白を除いて完全一致）
  // ルート課題: kAllRoots = 絞らない、kStandalone = 親のない単独の課題（子のない根）、0 以上 = Tree の根の添字
  static constexpr int kAllRoots = -1;
  static constexpr int kStandalone = -2;
  int root = kAllRoots;
  bool active() const { return who != Who::All || root != kAllRoots; }
  bool operator==(const TrendFilter&) const = default;
};

// 週ごとの作った数・完了した数・残り
struct TrendWeek {
  Day start = 0;         // その週の月曜
  int created = 0;       // その週に作った数（Created）
  int resolved = 0;      // その週に完了した数（Resolved。状況が完了のものだけ）
  int remaining = 0;     // その週の終わり（基準日の週は基準日）の時点の残り = それまでに作った数 − それまでに完了した数
  bool partial = false;  // 基準日の週（終わりまで来ていない）
};

// ペース予測を出さない理由
enum class ForecastReason {
  None,             // 出せる
  NoData,           // 数えられる課題がない
  Finished,         // 残りが 0（予測は要らない）
  NoCompletions,    // 直近の週に完了がない（平均が 0）
  FewActiveWeeks,   // 直近の週のうち、完了のある週が kTrendMinActiveWeeks 未満
  TooFar            // 予測が kTrendMaxForecastWeeks 週より先
};

struct TrendForecast {
  bool ok = false;
  ForecastReason reason = ForecastReason::NoData;
  int windowDone = 0;        // 直近 kTrendPaceWeeks 週の完了数の合計
  int activeWeeks = 0;       // そのうち完了のある週の数
  double perWeek = 0;        // 平均（windowDone ÷ kTrendPaceWeeks）
  int weeksAhead = 0;        // 残りがなくなる週は、基準日の週から何週あとか（ceil(残り ÷ 平均)）
  Day finishWeek = 0;        // その週の月曜
};

struct TrendBurn {
  std::vector<TrendWeek> weeks;  // 古い週から。最後が基準日の週。数えられる課題がなければ空
  int nowIndex = -1;             // 基準日の週の添字（weeks が空なら -1）
  int remainingNow = 0;          // 基準日時点の残り
  TrendForecast forecast;
  // 数えなかった件数（絞り込み後の作業のうち）
  int noCreated = 0;             // 作成日（Created）がない
  int noResolved = 0;            // 状況は完了だが完了日（Resolved）がない
  int inconsistent = 0;          // 完了日が作成日より前
  int future = 0;                // 作成日が基準日より先
};

struct TrendLead {
  int n = 0;                              // 数えた件数
  double median = 0;                      // 中央値（営業日。件数が偶数なら中の 2 つの平均）
  int p90 = 0;                            // 90 パーセンタイル（nearest-rank。営業日）
  int maxDays = 0;
  std::array<int, kTrendBuckets> hist{};  // 階級別の度数
  int noDates = 0;                        // 完了だが作成日か完了日がなく、数えなかった
  int inconsistent = 0;                   // 完了日が作成日より前で、数えなかった
};

// 最後に動いた日の出どころ
enum class AgingFrom { Updated, Start, Created };

struct AgingItem {
  int idx = -1;         // Tree の添字
  std::string id;       // Issue Id
  std::string name;     // 表示用の件名（Tree::name）
  std::string assignee; // 担当（空 = なし）
  int days = 0;         // 最後に動いた日から基準日までの営業日
  Day last = 0;         // 最後に動いた日
  AgingFrom from = AgingFrom::Updated;
};

struct TrendAging {
  int n = 0;                              // 数えた件数（未完了の作業）
  std::array<int, kTrendBuckets> hist{};
  std::vector<AgingItem> top;             // 長い順（同じなら Tree の添字順）。最大 kTrendTopN 件
  int noDate = 0;                         // 最後に動いた日がわからず、数えなかった（Updated・Start date・Created のどれもない）
};

struct TrendReport {
  int items = 0;                  // 絞り込み後の作業の数（マイルストーンを除く）
  bool hasCreatedColumn = false;  // CSV に Created 列（作成日）がある
  bool hasUpdatedColumn = false;  // CSV に Updated 列（更新日）がある
  TrendBurn burn;
  TrendLead lead;
  TrendAging aging;
};

// issues・R は T と同じ時点のもの（Session::viewIssues()・rows() など）。meta は Created・Updated 列を探すのに使う
// （どちらも既知の列ではないので、Issue::extra に文字のまま入っている。見出しは Created / 作成日 など。値は parseDate で読む）。
// 数える対象は作業（子を持たない課題。マイルストーンを除く）。エラー（添字の食い違い・根の指定が範囲外）は戻り値で返す。
Result<TrendReport> computeTrends(const Tree& T, const std::vector<Issue>& issues, const ProjectMeta& meta,
                                  const std::vector<ScheduleRow>& R, const Calendar& cal, Day base, const TrendFilter& filter = {});

// 画面と HTML で同じ文言を出すための文（日本語）
// ペース予測の一文。出せないときは理由（先頭に「!」。色だけに頼らない）。remainingNow は基準日時点の残り
std::string trendForecastText(const TrendForecast& f, int remainingNow);
// 数えなかったものの注記（なければ空）。画面・HTML とも、図の下に並べる
std::vector<std::string> trendNotes(const TrendReport& r);

}  // namespace roadmap
