// 状況の件数の履歴（Issue #64。#54 の B）: 週に 1 回までの「未着手／進行中／完了」の件数を覚え、累積フロー図に使う。
//  - 保存先は project.settings.json の "history"（"weekly"・"baselines" とは別のキー。共有・バックアップの JSON には入れない）
//  - 持つのは件数だけ（課題の名前・担当者などの個人情報は入れない）
//  - 同じ週（月曜始まり）の点は置き換える（週の最後の状態が残る）。上限 kHistoryMaxWeeks 週で、超えたら古いものから落とす
// UI・Win32・ImGui に依存しない。例外は投げず、失敗は Result で返す。読み込みでは壊れた値を読み飛ばす。
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "roadmap/calendar.hpp"
#include "roadmap/result.hpp"

namespace roadmap {

struct Issue;
struct Computed;

// 覚えておく週の数（超えたら古いものから落とす）
inline constexpr std::size_t kHistoryMaxWeeks = 104;
// 件数として読む値の上限（これより大きい値は壊れたものとして読み飛ばす）
inline constexpr long long kHistoryMaxCount = 10000000;

// 1 週ぶんの点
struct HistoryPoint {
  std::string weekStart;  // その週の月曜（ISO 日付 "YYYY-MM-DD"）
  int todo = 0, doing = 0, done = 0;
  int total = 0;  // todo + doing + done（マイルストーンを除く作業の数）
  bool operator==(const HistoryPoint&) const = default;
};

struct History {
  std::vector<HistoryPoint> points;  // 週の昇順（同じ週は 1 つ）。最大 kHistoryMaxWeeks
  bool empty() const { return points.empty(); }
  bool operator==(const History&) const = default;
};

// ---------------------------------------------------------------- 記録
// 今日（today）の週の点を作る。数えるのはマイルストーンを除く作業（子を持たない課題）で、状況の分類は統計と同じ
// （完了／進行中／それ以外＝未着手）。課題の数と computed が合わないときはエラー
Result<HistoryPoint> captureHistoryPoint(const std::vector<Issue>& issues, const Computed& computed, Day today);

// 日付の週（月曜始まり）の月曜を ISO 日付で
std::string historyWeekStart(Day d);

// 点を入れる。同じ週の点があれば置き換え、なければ追加して昇順を保つ。上限を超えたら古いものから落とす。
// 点が壊れている（月曜でない日付・負の件数・合計の食い違い・大きすぎる値）ときは何も変えずにエラーを返す
Status recordHistory(History& h, HistoryPoint p);

// d の週（月曜始まり）の点があるか
bool historyHasWeek(const History& h, Day d);

// ---------------------------------------------------------------- 保存（settings.json の "history"）
// 読み込みでは壊れた値（型・日付・月曜でない日付・範囲・負数・合計の食い違い・重複・順序）を読み飛ばす
nlohmann::ordered_json historyToJson(const History& h);
History historyFromJson(const nlohmann::ordered_json& j);

// ---------------------------------------------------------------- 描画用
struct CfdWeek {
  Day start = 0;      // その週の月曜
  bool gap = false;   // 記録がない週（線を引かない）
  int todo = 0, doing = 0, done = 0, total = 0;  // gap のときは 0
};

struct CfdSeries {
  std::vector<CfdWeek> weeks;  // 週の昇順（連続した週）。最初の記録の週から、最後の記録か基準日の週のうち後のほうまで。最大 kHistoryMaxWeeks
  int recorded = 0;            // 記録のある週の数（weeks のうち gap でないもの）
  int maxTotal = 0;            // 合計の最大（縦軸の高さ）
  bool drawable() const { return recorded >= 2; }  // 面が描けるのは 2 点以上
};

// 履歴から累積フロー図の列を作る。記録のない週は gap（間の空き・基準日の週が未記録のときの末尾）。最初の記録より前は含めない
CfdSeries cfdSeries(const History& h, Day baseDay);

// 画面と HTML で同じ文言を出すための文（日本語）
// 記録が 2 点未満のときの案内 "記録が溜まるとここに出ます（いま N 週ぶん）"
std::string cfdPendingText(int recordedWeeks);
// 週の値（ホバー・HTML の title）: 記録のある週は "10/19 からの週\n未着手 3 件\n進行中 1 件\n完了 5 件\n合計 9 件"、gap は "10/19 からの週\n記録なし"
std::string cfdWeekText(const CfdWeek& w);

}  // namespace roadmap
