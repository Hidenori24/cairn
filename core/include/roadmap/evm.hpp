// EVM（出来高管理。Issue #65）: ベースライン（#53）の日程を計画として、スケジュール系の指標を出す。
//   BAC（総計画量）・PV（計画価値）・EV（出来高）・SV（スケジュール差異）・SPI（スケジュール効率）・
//   アーンドスケジュール（ES。EV に等しい PV が計画のどの時点か → 何営業日の遅れ／進みか）。
// 実績の工数（AC）がないので、コスト系（CV・CPI・EAC など）は出さない。
//  - 計画の日程は「ベースラインの開始〜完了」。計画量（見積りがあれば見積り、なければ期間。週）を、その間の営業日へ均等に配る。
//    ベースラインがないときは指標を出さない（案内の文だけ）。ベースラインを保存した日より前は計測しない。
//  - 計測の対象は、ベースラインを保存した時点で未完了だった末端の課題（完了済みの課題は日程が 1 日に潰れていることがあり、計画を表さない）。
//  - 出来高 EV は 0/100（既定）か 50/50（EvmSettings）。完了日は Issue::resolved。
//  - 営業日は Calendar に従う。数えなかった課題は理由ごとに件数を持つ。
// 集計は UI に依存しない。例外は投げず、失敗は Result で返す。issues・R は T と同じ時点のもの（Session::viewIssues()・rows() など）。
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "roadmap/baseline.hpp"
#include "roadmap/calendar.hpp"
#include "roadmap/engine.hpp"
#include "roadmap/evm_settings.hpp"
#include "roadmap/result.hpp"
#include "roadmap/trends.hpp"

namespace roadmap {

// S カーブの週の数の上限（基準日を含む。超える分は切る）
inline constexpr int kEvmMaxWeeks = 156;
// 完了予測がこれより先（基準日から何週）になるときは出さない
inline constexpr int kEvmMaxForecastWeeks = 104;
// 計画量（週）として数える上限。これを超える値は極端な値として数えない
inline constexpr double kEvmMaxAmount = 100000.0;

// 指標を出さない理由
enum class EvmReason {
  None,            // 出せる
  NoBaseline,      // ベースラインがない
  BeforeBaseline,  // 基準日がベースラインの保存日より前
  NoItems          // 計測の対象になる課題がない
};

// S カーブの 1 点（日付ごとの累積）
struct EvmPoint {
  Day date = 0;      // その日の終わりまでの累積。基準日の点は基準日
  double pv = 0;
  double ev = 0;
  bool hasEv = false;  // 基準日以前だけ EV を持つ（先の EV は分からない）
};

// 完了予測を出さない理由
enum class EvmForecastReason {
  None,        // 出せる
  Finished,    // 計測の対象がすべて完了している
  NoProgress,  // まだ出来高がない（予測の元がない）
  TooFar       // kEvmMaxForecastWeeks 週より先になる
};

struct EvmForecast {
  bool ok = false;
  EvmForecastReason reason = EvmForecastReason::NoProgress;
  Day finish = 0;      // BAC に達する見込みの日（営業日）
  Day finishWeek = 0;  // その週の月曜
};

struct EvmReport {
  EvmReason reason = EvmReason::NoBaseline;
  bool ok() const { return reason == EvmReason::None; }
  EvmSettings settings;  // 使った設定
  Day base = 0;          // 基準日

  // ---- 使ったベースライン ----
  std::string baselineId, baselineName, baselineAt;  // at は "YYYY-MM-DDTHH:MM"（分からなければ空）
  bool baselineTruncated = false;                    // ベースラインは課題が多くて一部だけ
  bool hasFrom = false;                              // 保存日が分かる
  Day from = 0;                                      // 計測を始める日（ベースラインの保存日）
  bool filtered = false;                             // 担当者・ルート課題で絞っている

  // ---- 計測の対象 ----
  int items = 0;                       // 数えた課題（末端の作業）
  int byEstimate = 0, byDuration = 0;  // 計画量に見積りを使った数 / 期間を使った数
  int doneCount = 0, progCount = 0;    // 数えた課題のうち、いま完了 / 進行中
  // 数えなかった課題（絞り込み後）
  int added = 0;           // ベースラインにない（保存したあとに増えた）
  int doneAtBaseline = 0;  // ベースラインを保存した時点で完了していた
  int noPlanDate = 0;      // ベースラインに完了日がない
  int noAmount = 0;        // 見積りも期間もない（極端な値を含む）
  int noResolved = 0;      // 完了だが完了日（Resolved）がない
  int gone = 0;            // ベースラインにあって、いまは作業として見つからない（絞り込みのときは数えない）

  // ---- 基準日の時点の指標（ok() のとき）----
  double bac = 0;            // 総計画量（週）
  double pv = 0;             // 基準日の終わりまでの計画価値（週）
  double ev = 0;             // 基準日の終わりまでの出来高（週）
  double sv = 0;             // EV − PV
  bool hasSpi = false;       // PV が 0 のときは出せない
  double spi = 0;            // EV ÷ PV
  bool warn = false;         // SPI が設定のしきい値より小さい
  bool hasEs = false;
  double es = 0;             // アーンドスケジュール（計画の最初の営業日から数えた営業日。小数）
  double at = 0;             // 実際の経過（同じ起点から基準日までの営業日）
  double esDelta = 0;        // at − es（営業日。+ は遅れ、− は進み）
  Day planStart = 0, planEnd = 0;  // 計画の最初の日・最後の日

  // ---- S カーブ ----
  std::vector<EvmPoint> curve;  // 日付の昇順。最初は計測を始める日。基準日の点を必ず含む
  int nowIndex = -1;            // curve で基準日の添字
  bool curveClipped = false;    // 週の数の上限で切った
  EvmForecast forecast;
};

// 使うベースラインを選ぶ: wantedId があって見つかればそれ、なければ表示しているもの、それもなければ一番新しく保存したもの。なければ nullptr
const Baseline* evmPickBaseline(const BaselineSet& set, const std::string& wantedId);

// baseline が nullptr なら reason = NoBaseline の空のレポート。
// 絞り込み（担当者・ルート課題）は TrendFilter と同じ（絞るとき「ベースラインから消えた」は数えない）。
// エラー（課題と計算結果の数の食い違い・根の指定が範囲外）は戻り値で返す
Result<EvmReport> computeEvm(const Tree& T, const std::vector<Issue>& issues, const std::vector<ScheduleRow>& R, const Calendar& cal, Day base,
                             const Baseline* baseline, const EvmSettings& settings, const TrendFilter& filter = {});

// ---------------------------------------------------------------- 画面と書き出しで同じ文言を出すための文（日本語）
// 週の量 "12.5 週"（小数 1 桁。".0" は落とす）
std::string evmAmountText(double weeks);
// 符号つき "+2.5 週" "-1 週" "±0 週"
std::string evmSignedAmountText(double weeks);
// SPI "0.85"（出せないとき "-"）
std::string evmSpiText(const EvmReport& r);
// ES の遅れ（営業日。+ は遅れ）"+3.5" "-2" "±0"（出せないとき "-"）
std::string evmEsDeltaText(const EvmReport& r);
// ES の文 "計画より 3.5 営業日 遅れています" "…進んでいます" "計画どおりです"
std::string evmEsSentence(const EvmReport& r);
// 警告の印: warn のとき "!"、それ以外は空（色だけに頼らない）
std::string evmWarnMark(const EvmReport& r);
// 指標を出せない理由の案内（ok() のときは空）。ベースラインがないときは「ベースラインを保存してください」
std::string evmReasonText(const EvmReport& r);
// 計画量の内訳 "計画量: 見積り 12 件・期間 30 件"
std::string evmAmountSourceText(const EvmReport& r);
// 計算の前提（折りたたみに出す行）。ベースライン名・保存日、計画量、進行中の扱い、計測の範囲、営業日
std::vector<std::string> evmAssumptions(const EvmReport& r);
// 数えなかったものの注記（なければ空）
std::vector<std::string> evmNotes(const EvmReport& r);
// 完了予測の一文（出せないときは理由。先頭の「!」は色だけに頼らない印）
std::string evmForecastText(const EvmReport& r);

}  // namespace roadmap
