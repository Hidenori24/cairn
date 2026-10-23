// 週次レポートの文言（Markdown・HTML のレンダラが共有する。エスケープ前の文字列を返す）
#pragma once

#include <string>

#include "roadmap/weekly.hpp"

namespace roadmap::weeklyfmt {

// 見出し（期間に合わせて「今週」「先週」「期間内」）
std::string doneTitle(const WeeklyReport& r);
std::string nextTitle(const WeeklyReport& r);
// 担当別の表の「負荷」の列の見出し
std::string loadTitle(const WeeklyReport& r);
// 期間 "2026/10/19(月) 〜 2026/10/25(日)"
std::string rangeText(const WeeklyRange& g);
// 期間の説明 "営業日 5 日"（休業日を除く）
std::string workdaysText(int n);
// 0〜1 → "42%"（四捨五入）
std::string percent(double p);
// 進みの増減 "+5pt" "-2pt" "±0pt"
std::string deltaPt(double d);
// 件数の増減 "+1" "-2" "±0"
std::string deltaInt(int d);
// 営業日の増減 "3 営業日遅くなった" "2 営業日早まった" "営業日の差は 0（休業日・土日をまたいだ日付の違い）"
std::string shiftText(int days);
// 余裕 "余裕 5 営業日" / "遅れ 3 営業日" / 完了
std::string floatPhrase(int floatDays);
// 残りの値だけ "4 営業日" / "予定日を過ぎている"（表の「残り」の列）
std::string remainText(const WeeklyItem& it, Day base);
// 残り "残り 4 営業日" / "予定日を過ぎている"（最早の完了が基準日より前）
std::string remainPhrase(const WeeklyItem& it, Day base);
// 担当者 "担当: 田中"（空 = "担当: 未割当"）
std::string assigneeText(const std::string& name);
// 担当者の名前だけ（空 = "未割当"）
std::string who(const std::string& name);
// 件名 "DEV-2 件名"
std::string title(const std::string& did, const std::string& name);
// 期限と見込みの差 "期限より 3 営業日早い" など
std::string diffPhrase(const WeeklyMilestone& m);
// 負荷 "85%" / 分からなければ "-"
std::string loadText(int percent);
// 比べた相手の言い方: 手動 "2026/10/16(金) に書き出したときの状態" / 自動 "2026/10/19(月) に自動で記録した週の頭の状態"
std::string snapshotBasisText(bool automatic, Day d);
// 「先週から変わったこと」の見出しの下の 1 文（「…との比較です。」。終わりもスナップショットなら「…から…までの変化です。」）
std::string changesLeadText(const WeeklyChanges& c);
// 今週触った課題（Issue #71）
std::string touchedTitle(const WeeklyReport& r);
// 状況の名前 "未着手" "進行中" "完了"（スナップショットの 't' 'p' 'd' も）
std::string statusName(const WeeklyItem& it);
std::string statusName(char st);
// 触った日 "更新日 10/21" / "作成日 10/21"（Updated が期間にないとき）
std::string touchedOnText(const WeeklyTouchedItem& it);
// 何が変わったか "新規作成・状況 未着手 → 進行中・完了見込み 10/23 → 10/30（3 営業日遅くなった）・コメントあり"。
// 分かることがなければ "更新"（コメントだけなら "更新・コメントあり"）
std::string touchedChangeText(const WeeklyTouchedItem& it);
// 状況・日程の変化の比べた相手の説明（比べる相手がなければ、その旨）
std::string touchedBasisText(const WeeklyTouched& t);
// 「更新日のない N 件は数えていません。」
std::string touchedNoStampText(int n);
// Created・Updated の列がないとき
std::string touchedNoColumnText();
// 親のない課題のまとまりの見出し
std::string touchedNoParentText();
// 「ほか N 件」
std::string moreText(int omitted);
// ベースラインの保存日時 "YYYY-MM-DDTHH:MM" → "2026/10/19 09:30"（分からなければ空）
std::string baselineAtText(const std::string& at);
// 遅れ・早まりの 1 行の説明 "計画 10/23 → 見込み 10/30・計画より 5 営業日遅れ"
std::string baselineMoveText(const WeeklyBaselineRow& r);
// 計画になかった課題 "完了見込み 10/30" / 消えた課題 "計画の完了 10/23"
std::string baselineAddedText(const WeeklyBaselineRow& r);
std::string baselineRemovedText(const WeeklyBaselineRow& r);
// 件数の見出し "遅れ 3 件 ／ 早まり 2 件 ／ 計画どおり 10 件 ／ 計画になかった 1 件 ／ 消えた 1 件"
std::string baselineCountsText(const BaselineSummary& s);
// 営業日の差が 0 なのに完了日が違う課題があるときの注記（なければ空）
std::string baselineSameDayNote(const BaselineSummary& s);

}  // namespace roadmap::weeklyfmt
