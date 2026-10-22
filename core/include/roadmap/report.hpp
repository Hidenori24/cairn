// 報告の書き出し（Issue #42）: ガント・統計・リストを、外部ファイルを読まない 1 つの HTML（インライン SVG）にする。
// UI・Win32・ImGui に依存しない純粋な関数（Linux でテストできる）。スクリプトは入れない。
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
#include "roadmap/filter.hpp"
#include "roadmap/gantt_geom.hpp"
#include "roadmap/result.hpp"

namespace roadmap {

// ガントに出す行の上限。超えたら先頭のこれだけを出し、注記を付ける（リストには全部出る。SVG の要素数と大きさを抑える）
inline constexpr std::size_t kReportMaxGanttRows = 5000;
// リストの行の上限
inline constexpr std::size_t kReportMaxListRows = 20000;
// 統計の 1 つの表・図に出す行（担当者・ルート課題）の上限
inline constexpr std::size_t kReportMaxStatRows = 200;
// ガントの時間軸の日数の上限（極端な日付で出力が膨らまないように）
inline constexpr int kReportMaxAxisDays = 4000;
// ガントの依存の矢印の断片（チャンクごとの線）の上限。超えた分は省いて注記を付ける
inline constexpr std::size_t kReportMaxArrowPieces = 40000;
// 行数がこれ以下なら、バーに説明（マウスを載せると出るツールチップ）を付ける
inline constexpr std::size_t kReportDetailRows = 600;

enum class ReportScope {
  All,      // プロジェクト全体（すべて開いた状態）
  Current,  // 画面に出ている行のまま（絞り込み・折りたたみ）
  Subtree   // 選んだ課題とその配下
};

struct ExportOptions {
  bool gantt = true, stats = true, list = true;
  bool dark = false;                 // false = 印刷向けのライト
  std::string zoom = "week";         // day / week / month / all
  std::string colorBy = "root";      // root / float / asg
  ReportScope scope = ReportScope::All;
  bool showDeps = true;              // ガントに依存の矢印を出す
  std::size_t maxGanttRows = kReportMaxGanttRows;  // テストで小さくできる
};

// 書き出しの入力。tree・issues・rows は同じ時点のもの（Session::tree()・viewIssues()・rows()）。参照は呼び出しの間だけ使う
struct ExportInput {
  ExportInput(const Tree& t, const std::vector<Issue>& i, const std::vector<ScheduleRow>& r, const Calendar& c)
      : tree(t), issues(i), rows(r), calendar(c) {}
  const Tree& tree;
  const std::vector<Issue>& issues;
  const std::vector<ScheduleRow>& rows;
  const Calendar& calendar;
  const ProjectMeta* meta = nullptr;  // 統計の「推移」が Created・Updated 列を探すのに使う（なければ日付の列なし扱い）
  Day base = 0;
  std::string projectName;
  ResourceLimit resource;
  const IClock* clock = nullptr;        // 出力日時（なければ書かない）。テストでは FixedClock
  std::vector<VisibleRow> currentRows;  // 画面に出ている行（ReportScope::Current）
  std::vector<char> dimmed;             // 添字ごと。薄く描く課題（空 = なし。ReportScope::Current のとき使う）
  float dimAlpha = 0.3f;
  std::string filterLabel;              // 今の絞り込みの名前（空 = なし。表示範囲の注記に出す）
  int subtreeRoot = -1;                 // ReportScope::Subtree の根の課題（Tree の添字）
  // 表示するベースラインとの比較（Session::baselineCompare()。null = 出さない）。ガントに計画のバーと差、統計に「計画との差」の節を出す。
  // 課題の数が合わないものは使わない
  const BaselineCompare* baseline = nullptr;
};

// 1 つの HTML（UTF-8、BOM なし）。課題が 0 件でも、案内を書いた HTML を返す
std::string exportHtml(const ExportInput& in, const ExportOptions& opt);

// 書き出す行（ガントとリストが使う）。範囲外の添字は捨てる
std::vector<VisibleRow> reportRows(const ExportInput& in, ReportScope scope);

// HTML・SVG の本文と属性に入れる文字列のエスケープ。& < > " ' を実体参照にし、制御文字（NUL など）と
// 向きを変える制御文字は取り除く。壊れた UTF-8 は U+FFFD にする。改行・タブは空白にする
std::string escapeXml(std::string_view s);

// 本文に入れる文字列の下ごしらえ（エスケープはしない）: 制御文字（NUL など）と向きを変える制御文字は取り除き、
// 壊れた UTF-8 は U+FFFD、改行・タブ・行区切りは空白にする。Markdown など HTML 以外の出力でも使う
std::string cleanText(std::string_view s);

// 既定のファイル名 "<プロジェクト名>_<日付>.html"。使えない文字（\ / : * ? " < > |・制御文字）は "_"、
// Windows の予約名と末尾の "."・空白は避ける。名前が空なら "報告"
std::string reportFileName(const std::string& projectName, const std::string& isoDate);

// 保存先の検査（拡張子の候補は小文字で "." つき。extHint = 違っていたときに案内する拡張子）。週次レポートも使う
Status checkSavePath(const std::string& path, const std::vector<std::string>& exts, const std::string& extHint);

// 保存先のパスの検査: 空でない・制御文字を含まない・ファイル名がある・拡張子が .html か .htm
Status checkReportPath(const std::string& path);

// 検査して書く（IFileStore::writeAtomic）。既存のフォルダの名前には書かない
Status writeReport(IFileStore& fs, const std::string& path, const std::string& html);

}  // namespace roadmap
