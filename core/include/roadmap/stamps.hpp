// アプリが自動で記録する作成日（Created）・更新日（Updated）（Issue #66）。
// どちらも既知の列ではなく、CSV の「未知の列」（ProjectMeta::extraHeaders / Issue::extra）に入れる
// （モデルの形を変えず、編集コマンドを通らない取り込み → 書き出しの往復を変えないため）。UI 非依存。
#pragma once

#include <string>
#include <unordered_set>
#include <vector>

#include "roadmap/calendar.hpp"
#include "roadmap/model.hpp"

namespace roadmap {

enum class StampKind { Created, Updated };

// 記録に使う見出し（新しく列を足すとき）
const char* stampHeader(StampKind kind);

// 見出しから列（meta.extraHeaders の添字）を探す。Created / 作成日 / 作成 / 作成日時、Updated / 更新日 / 更新 / 更新日時
// （大文字小文字・前後の空白は無視）。同じ見出しが複数あれば最初の列。なければ -1。推移（trends）の読み取りと共通
int findStampColumn(const ProjectMeta& meta, StampKind kind);

// 記録する日時（日付は Resolved と同じく min(今日, 基準日) を呼び出し側が決める）
struct StampWhen {
  Day day = 0;
  int hour = 0, minute = 0;  // Updated の列が JIRA の日時の書式のときだけ使う
};

struct StampResult {
  int created = 0;  // Created を入れた課題の数
  int updated = 0;  // Updated を入れた課題の数
};

// 編集の前後（before → after）を比べて、記録する。
//  - after にあって before にない id（新しい課題）には Created を入れる（複製などで値が入っていても上書きする）。
//  - 両方にあって中身が変わった課題には Updated を入れる。中身 = Issue の全項目（Created・Updated の欄は除く）。
//    課題の並び順・列を足しただけの空欄の違いは変えたことにしない。
//    先行・後続の変更（blocks の増減）は、つないだ相手の課題も変えたことにする。
//  - 列がなければ meta.extraHeaders の末尾に足し、after の全課題の extra を空文字で揃える。記録するものがなければ何も変えない。
//  - Created は新しい課題にしか書かない（既存の値は、JIRA の日時の書式でもそのまま）。
//  - Updated の書式: その列の最初の値が JIRA の日時（19/Oct/26 9:00 AM）ならその書式で、それ以外は Resolved と同じ YYYY-MM-DD。
// only が非 null なら、Updated を入れる課題を only の id に限る（Due date → Deadline 列の移し替えのような、
// 値は動くが利用者が編集していない課題を巻き込まないため）。
StampResult stampChanges(const std::vector<Issue>& before, ProjectMeta& meta, std::vector<Issue>& after, const StampWhen& when,
                         const std::unordered_set<std::string>* only = nullptr);

}  // namespace roadmap
