// ベースライン比較（Issue #53）: 自分で選んだ時点の日程（見込みの開始日・完了日・期限と状況）を名前つきで保存し、
// いまの見込みとの差（営業日）を課題ごとに出す。
//  - 保存先は project.settings.json の "baselines"（週次レポートの "weekly" と同じ流儀。共有・バックアップの JSON には入れない）
//  - 課題は Issue Id で突き合わせる（表示 ID は階層で変わるため使わない）
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

// 保存できるベースラインの数
inline constexpr std::size_t kBaselineMax = 5;
// 1 つのベースラインに持つ課題の数の上限（超えた分は持たず、truncated を立てる）
inline constexpr std::size_t kBaselineMaxItems = 5000;
// Issue Id の長さの上限（バイト）
inline constexpr std::size_t kBaselineMaxIdBytes = 64;
// 名前の長さの上限（文字）
inline constexpr std::size_t kBaselineMaxNameChars = 40;
// 課題の件名（消えた課題を見分けるために保存する）の長さの上限（文字）
inline constexpr std::size_t kBaselineMaxTitleChars = 100;
// 統計などに出す「遅れの大きい順」の件数
inline constexpr std::size_t kBaselineTopN = 10;

// 課題 1 件の写し
struct BaselineItem {
  std::string id;       // Issue Id（CSV の値）
  char st = 't';        // 't' 未着手 / 'p' 進行中 / 'd' 完了
  bool parent = false;  // 親（まとめバー）だった
  OptDay start, finish, deadline;  // 見込みの開始日・完了日・期限（なければ空）
  std::string title;    // 件名（日付と状況のほかに持つ唯一の文字。担当者・工数は持たない）
  bool operator==(const BaselineItem&) const = default;
};

struct Baseline {
  std::string id;    // 設定の中の識別子（baselineAdd が振る）
  std::string name;  // 利用者が付けた名前（最大 kBaselineMaxNameChars 文字）
  std::string at;    // 保存した日時 "YYYY-MM-DDTHH:MM"（分からなければ空）
  bool truncated = false;  // 課題が多すぎて、先頭の分だけ持っている
  std::vector<BaselineItem> items;
  nlohmann::ordered_json extra = nlohmann::ordered_json::object();  // ベースライン項目内の未知のキー
  bool operator==(const Baseline&) const = default;
};

struct BaselineSet {
  std::vector<Baseline> list;  // 保存した順
  std::string shown;           // ガント・リスト・統計に出すベースラインの id（空 = 出さない）
  nlohmann::ordered_json extra = nlohmann::ordered_json::object();  // baselines 直下の未知のキー
  bool empty() const { return list.empty() && shown.empty() && extra.empty(); }
  bool operator==(const BaselineSet&) const = default;
};

// ---------------------------------------------------------------- 名前・保存・管理
// 名前の下ごしらえ: 壊れた UTF-8 は U+FFFD、制御文字・向きを変える文字は除き、前後の空白を落として
// kBaselineMaxNameChars 文字までにする。何も残らなければ空
std::string baselineSanitizeName(const std::string& s);

// いまの課題と日程（computed）を写す。名前は下ごしらえする（空なら「ベースライン」）。nowDT は "YYYY-MM-DDTHH:MM"。
// 課題の数と computed が合わないときはエラー。id は空（baselineAdd が振る）
Result<Baseline> captureBaseline(const std::vector<Issue>& issues, const Computed& computed, const std::string& name,
                                 const std::string& nowDT);

// 追加する。kBaselineMax を超えるときは理由つきで断る。成功したら振った id を返す
// （名前・課題を検査して整える: Issue Id が長すぎる・重複する課題は捨て、kBaselineMaxItems までに切る）
Result<std::string> baselineAdd(BaselineSet& set, Baseline b);
// 名前を変える（下ごしらえして空になる名前は断る）
Status baselineRename(BaselineSet& set, const std::string& id, const std::string& name);
// 削除する。見つからなければ false。表示中のものを消したら表示も外す
bool baselineRemove(BaselineSet& set, const std::string& id);
const Baseline* baselineFind(const BaselineSet& set, const std::string& id);
const Baseline* baselineShown(const BaselineSet& set);
// 表示するものを選ぶ。id が空なら外す。見つからなければ何もせず false
bool baselineSetShown(BaselineSet& set, const std::string& id);

// settings.json の "baselines" との変換。読み込みでは壊れた値を読み飛ばす（型・日付・状況・id・重複・件数の超過）
nlohmann::ordered_json baselinesToJson(const BaselineSet& set);
BaselineSet baselinesFromJson(const nlohmann::ordered_json& j);

// ---------------------------------------------------------------- 比較
enum class BaselineState {
  OnPlan,   // 計画どおり（完了日の営業日の差が 0）
  Late,     // 遅れ（完了日が計画より後）
  Early,    // 早まり（完了日が計画より前）
  Added,    // 計画になかった（ベースラインにない）
  Removed,  // 消えた（いまの課題にない）
  NoDate    // 計画に日付がなく、比べられない
};
const char* baselineStateText(BaselineState s);  // 日本語の名前

struct BaselineEntry {
  int index = -1;           // Tree の添字（消えた課題は -1）
  std::string id;           // Issue Id
  std::string title;        // 消えた課題の件名（保存していたもの。いまある課題は空）
  BaselineState state = BaselineState::Added;
  bool done = false;        // いま完了している（完了した課題も、実際の完了日で早い・遅いを見る）
  bool parent = false;      // 親（まとめ）。集計には数えない
  char planSt = 't';        // 計画のときの状況
  OptDay planStart, planFinish, planDeadline;
  Day nowStart = 0, nowFinish = 0;  // いまの見込み（消えた課題では意味がない）
  bool hasStartDiff = false, hasFinishDiff = false;
  int startDiff = 0;        // 開始日の差（営業日。+ は遅れ）
  int finishDiff = 0;       // 完了日の差（営業日。+ は遅れ）
  bool startDiffers = false, finishDiffers = false;  // 日付そのものが違う（営業日の差が 0 でも立つことがある）
};

struct BaselineSummary {
  int early = 0, onPlan = 0, late = 0;  // 末端の課題の数（親のまとめは数えない）
  int added = 0, removed = 0, noDate = 0;
  int doneNow = 0;          // いま完了している末端の課題の数
  int sameDayDiffers = 0;   // 営業日の差が 0 なのに完了日が違う課題の数（休業日・土日をまたぐ違い。onPlan に含む）
  int compared = 0;         // early + onPlan + late
};

struct BaselineCompare {
  std::string baselineId, baselineName, baselineAt;
  bool truncated = false;               // 比べた相手は一部の課題だけ
  std::vector<BaselineEntry> entries;   // Tree の添字ごと（いまある課題すべて）
  std::vector<BaselineEntry> removed;   // 消えた課題（ベースラインの順）
  BaselineSummary sum;
  std::vector<int> topLate;             // 遅れの大きい順の末端（Tree の添字。kBaselineTopN 件まで。同じ大きさは表の順）
  int lateTotal = 0;                    // 遅れた末端の数
};

// ベースラインと、いまの課題・日程を Issue Id で突き合わせる。課題の数と computed が合わなければエラー
Result<BaselineCompare> compareBaseline(const Baseline& baseline, const std::vector<Issue>& issues, const Computed& computed,
                                        const Calendar& calendar);

// 営業日の差: to が後なら (from, to] の営業日の数、前なら -(to, from]。同じ日は 0。休業日・土日でも数え間違えず、長い期間でも速い
int baselineWorkdayDiff(const Calendar& cal, Day from, Day to);

// ---------------------------------------------------------------- 表示の文言（画面と HTML 書き出しが共有する）
// 完了日の差 "+3日" "-2日" "±0日"。計画になかった "新規"、消えた "消えた"、比べられない "-"
std::string baselineDiffText(const BaselineEntry& e);
// 遅れのとき "!"（色だけに頼らない印）。それ以外は空
std::string baselineDiffMark(const BaselineEntry& e);
// 営業日の差が 0 なのに日付が違うときの注記（休業日・土日をまたぐ日付の違い）。なければ空
std::string baselineDiffNote(const BaselineEntry& e);
// ガントに差の文字を出すか: 遅れ・早まり・計画になかった、または営業日の差が 0 でも完了日が違うとき
bool baselineShowLabel(const BaselineEntry& e);
// 列の絞り込みの値 "遅れ" "早い" "計画どおり" "計画になかった" "比較なし"
std::string baselineColumnValue(const BaselineEntry& e);
inline constexpr const char* kBaselineValueLate = "遅れ";
inline constexpr const char* kBaselineValueEarly = "早い";
inline constexpr const char* kBaselineValueOnPlan = "計画どおり";
inline constexpr const char* kBaselineValueAdded = "計画になかった";
inline constexpr const char* kBaselineValueNone = "比較なし";

}  // namespace roadmap
