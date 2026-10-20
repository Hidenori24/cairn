// 画面の文言（SPEC 11: 文言は1か所に集め、将来の翻訳を妨げない）
#pragma once

namespace roadmap::ui::S {

inline constexpr const char* kAppTitle = "プロジェクト管理";
inline constexpr const char* kViewGantt = "ガント";
inline constexpr const char* kViewBoard = "ボード";
inline constexpr const char* kViewList = "リスト";
inline constexpr const char* kViewGoals = "ギャップ";
inline constexpr const char* kViewStats = "統計";
inline constexpr const char* kSearchHint = "件名・ID・ラベルで絞り込み  ( / )";
inline constexpr const char* kLate = "遅れ";
inline constexpr const char* kTight = "余裕小";
inline constexpr const char* kGoal = "ゴール";
inline constexpr const char* kSaved = "保存済み";
inline constexpr const char* kSaving = "保存中…";
inline constexpr const char* kSaveError = "保存できません";
inline constexpr const char* kAddIssue = "＋ 課題";
inline constexpr const char* kUndo = "元に戻す";
inline constexpr const char* kRedo = "やり直す";
inline constexpr const char* kMore = "その他";
inline constexpr const char* kAll = "全員";
inline constexpr const char* kUnassigned = "未割当";
inline constexpr const char* kAssigned = "担当者あり";
inline constexpr const char* kNoMatch = "条件に合う課題がありません。";
inline constexpr const char* kEmptyProject = "まだ課題がありません";
inline constexpr const char* kFirstIssue = "最初の課題を追加";
inline constexpr const char* kImportCsv = "CSVを取り込む";
inline constexpr const char* kExport = "書き出し";
inline constexpr const char* kHint =
    "ダブルクリックで名前を編集 ／ N で行を追加 ／ Tab で階層を下げる ／ バーをドラッグで日程変更 ／ Ctrl+K でコマンド";
inline constexpr const char* kSelectHint = "課題を選ぶと、ここで詳しく見られます";
inline constexpr const char* kSortNote =
    "並べ替え中は、末端の課題だけをフラットに表示します。見出しをもう一度押すと解除します。";
inline constexpr const char* kPaletteHint = "コマンドや課題を検索…";
inline constexpr const char* kNoWarns = "注意はありません";
inline constexpr const char* kStatusTodo = "To Do";
inline constexpr const char* kStatusProg = "In Progress";
inline constexpr const char* kStatusDone = "Done";
inline constexpr const char* kUntitled = "（無題）";
inline constexpr const char* kWelcome = "はじめに";

}  // namespace roadmap::ui::S
