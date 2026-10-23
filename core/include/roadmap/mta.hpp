// マイルストーンの予測の推移（MTA。Issue #75）: 週の頭のスナップショット（"weeklyAuto"。#70）に残した「マイルストーンの完了の見込み日」を
// 週ごとに並べ、前の記録からの動き（営業日）と、遅れの警告（しきい値の既定 kMtaDefaultLateDays 営業日）を出す。
//  - 新しい保存は持たない。スナップショットの "ms"（マイルストーンの Issue Id・状況・見込みの完了日）を読むだけ
//  - 対象は葉のマイルストーンだけ（B51）。Issue Id で追うので、途中で削除・追加されても混ざらない
//  - 嘘の線を引かない: 記録のない週は gap（線を切る）。記録のない課題は点を作らない。完了済みは完了日で止める
// 画面（統計）・HTML 書き出し・週次レポートが同じ結果と文言を使う。UI・Win32・ImGui に依存しない。例外は投げず、失敗は Result で返す。
#pragma once

#include <string>
#include <vector>

#include "roadmap/baseline.hpp"
#include "roadmap/calendar.hpp"
#include "roadmap/engine.hpp"
#include "roadmap/result.hpp"
#include "roadmap/weekly_state.hpp"

namespace roadmap {

// 遅れの警告のしきい値（営業日。前の記録から見込みの完了日がこれ以上後ろへ動いたら警告）
inline constexpr int kMtaDefaultLateDays = 5;
inline constexpr int kMtaMinLateDays = 1;
inline constexpr int kMtaMaxLateDays = 260;
// 横軸の週の数の上限（新しいほうを残す。「いま」の列は別）
inline constexpr int kMtaMaxWeeks = 52;
// 系列（マイルストーン）の数の上限（超えた分は持たない）
inline constexpr std::size_t kMtaMaxSeries = 500;

// しきい値を範囲に収める
int mtaClampLateDays(int v);

struct MtaOptions {
  int lateDays = kMtaDefaultLateDays;  // 範囲外は mtaClampLateDays で収める
};

// 横軸の 1 列
struct MtaColumn {
  Day week = 0;           // 記録の列: その週の月曜。「いま」の列: 基準日
  bool recorded = false;  // マイルストーンの記録がある週（false = 記録なし。線を切る）。「いま」は true
  bool now = false;       // 「いま」の列（最後の 1 列）
};

// 1 つのマイルストーンの 1 列ぶんの点
struct MtaPoint {
  bool has = false;       // 点がある（記録がある。「いま」は、いまの課題にあるとき）
  bool ended = false;     // 達成のあとで、線を止めた（記録はあるが、点にしない）
  bool achieved = false;  // この時点で達成済み（date は完了日）
  Day date = 0;           // 見込みの完了日（達成済みなら完了日）
  bool hasMove = false;   // 前の点からの動きがある（最初の点にはない）
  int move = 0;           // 前の点からの動き（営業日。正 = 遅れ）
  int sinceWeeks = 0;     // 動きの相手は何週前の列か（1 = 前の週。0 = 同じ週の頭〔「いま」だけ〕）
  bool late = false;      // move がしきい値以上（遅れ）
};

struct MtaSeries {
  std::string id;           // Issue Id
  std::string did, name;    // 表示 ID・件名（いまの課題から。削除されたものは空）
  int index = -1;           // Tree の添字（削除されたものは -1）
  bool removed = false;     // いまの課題にない（記録だけが残っている）
  bool achievedNow = false; // いま達成済み
  OptDay deadline;          // いまの期限（なければ空）
  OptDay plan;              // 表示中のベースラインの計画の完了日（なければ空）
  std::vector<MtaPoint> points;  // MtaReport::columns と同じ長さ
  int recorded = 0;         // 記録のある点の数（「いま」を除く）
  // 遅れの警告: 「いま」と、前週以前の最新の記録を比べる（前の週に記録がなければ、それより前で一番新しい記録）
  bool warn = false;
  int warnMove = 0;         // 営業日の動き（正 = 遅れ）
  Day warnFromWeek = 0;     // 比べた記録の週の月曜
  Day warnFrom = 0, warnTo = 0;  // 比べた記録の見込み日 → いまの見込み日
  int warnWeeks = 0;        // 何週前の記録か（基準日の週の月曜から数える）
};

struct MtaReport {
  Day base = 0;
  int lateDays = kMtaDefaultLateDays;
  int recordedWeeks = 0;                // マイルストーンの記録がある週の数（columns のうち recorded で「いま」でないもの）
  bool hasRecords() const { return recordedWeeks > 0; }
  bool drawable() const { return recordedWeeks >= 2; }  // 線が引けるのは 2 週以上（嘘の線を引かない）
  bool truncatedWeeks = false;          // 古い週を kMtaMaxWeeks に収めるために落とした
  std::vector<MtaColumn> columns;       // 古い週から。記録がなければ空。最後は「いま」
  std::vector<MtaSeries> series;        // いまのマイルストーン（深さ優先の順）→ 削除されたもの（記録に出た順）
  int warnings = 0;                     // warn の数
  bool empty() const { return series.empty(); }
};

// issues・rows は tree と同じ時点のもの（Session::viewIssues()・rows())。autos は Session::weeklyAuto()。
// baseline は表示中のベースラインとの比較（なければ nullptr。課題の数が合わないものは使わない）。
// 木と課題・日程の数が合わないときはエラー。課題が 0 件なら、記録だけの（削除された）系列を返す
Result<MtaReport> computeMta(const Tree& tree, const std::vector<Issue>& issues, const std::vector<ScheduleRow>& rows, const Calendar& cal,
                             const WeeklyAuto& autos, Day base, const BaselineCompare* baseline, const MtaOptions& opt = {});

// ---------------------------------------------------------------- 画面と HTML・週次レポートで同じ文言を出すための文（日本語）
// 記録が 2 週に満たないときの案内 "記録が溜まるとここに出ます（いま N 週ぶん）"
std::string mtaPendingText(int recordedWeeks);
// 列の見出し "10/19 の週" / "いま（10/21）"
std::string mtaColumnText(const MtaColumn& c);
// 系列の名前 "DEV-6 リリース"（削除されたものは "(削除された) <Issue Id>"）
std::string mtaSeriesName(const MtaSeries& s);
// 予測日 "11/20" / 達成 "11/20（達成）" / 点がないとき "-"（達成のあとの「止めた」点は "達成済み"）
std::string mtaDateText(const MtaPoint& p);
// 前の点からの動き: 最初は "-"、0 は "変わらず"、"+3 営業日（遅れ）" "-2 営業日（前倒し）"。しきい値以上の遅れは先頭に "! "。
// 前の週でなければ "・2 週前の記録から"、「いま」が同じ週の頭からなら "・週の頭から" を足す
std::string mtaMoveText(const MtaPoint& p);
// 警告の 1 文 "! DEV-6 リリース: 完了の見込みが 10/12 の週の記録から 6 営業日遅れました（11/13 → 11/23）"
std::string mtaWarnText(const MtaSeries& s);
// 警告の件名のあとの部分 "完了の見込みが 10/12 の週の記録から 6 営業日遅れました（11/13 → 11/23）"（mtaWarnText = "! " + 件名 + ": " + これ。
// Markdown など、件名だけをエスケープしたいとき用）
std::string mtaWarnTail(const MtaSeries& s);
// 期限・計画の線の見出し "期限 11/20" "計画 11/13" "期限・計画 11/20"（両方なければ空）
std::string mtaRefLineText(const MtaSeries& s);
// 点のホバー・HTML の title。点があるとき "DEV-6 リリース\n10/19 の週\n見込み 11/20\n前の点から +3 営業日（遅れ）"、
// 点がないとき "DEV-6 リリース\n10/19 の週\n記録なし"。col が範囲外なら空
std::string mtaPointText(const MtaSeries& s, const MtaColumn& c, std::size_t col);
// 凡例・表の見出し "DEV-6 リリース（期限 11/20・計画 11/13）"（達成済みは "（達成済み）" を足す）
std::string mtaLegendText(const MtaSeries& s);

// ---------------------------------------------------------------- 図の共通部分（画面と HTML で同じ見た目にする）
// 系列ごとの線種・印（色だけに頼らない）。k 番目の系列は kMtaStyles 種類を順に使う
enum class MtaMarker { Circle, Square, Triangle, Diamond, Cross, Plus };
inline constexpr int kMtaStyles = 6;
struct MtaStyle {
  int color = 0;          // ThemeColors::c の添字（0〜7）
  MtaMarker marker = MtaMarker::Circle;
  int dash = 0, gap = 0;  // 線の破線の長さと間（px。dash == 0 は実線）
  const char* name = "";  // 凡例用の説明 "実線・丸"
};
MtaStyle mtaStyleOf(std::size_t k);

// 縦軸（日付。上ほど後ろ = 遅れ）。shown の系列の点・期限・計画が入る範囲に余白を足し、目盛りを 3〜5 本。点がなければ ok = false
struct MtaYAxis {
  bool ok = false;
  Day lo = 0, hi = 0;
  std::vector<Day> ticks;  // 昇順
};
MtaYAxis mtaYAxis(const MtaReport& r, const std::vector<std::size_t>& shown);

// 初めに図に出す系列（最大 kMtaStyles 本）: 警告のあるもの → 記録のあるもの → それ以外、の順に、それぞれ元の並びで
std::vector<std::size_t> mtaDefaultShown(const MtaReport& r);

}  // namespace roadmap
