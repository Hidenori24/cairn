// 編集操作・元に戻す・計算の更新（SPEC 9章。JS版 core.js の mutate など）
// Session は「いま開いているプロジェクト1つ」を持つ。UI はここだけを触る。
#pragma once

#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "roadmap/clock.hpp"
#include "roadmap/filter.hpp"
#include "roadmap/link_candidates.hpp"
#include "roadmap/outline.hpp"
#include "roadmap/schedule.hpp"
#include "roadmap/templates.hpp"

namespace roadmap {

enum class NoticeAction { None, Undo };
struct Notice {
  std::string text;
  NoticeAction action = NoticeAction::None;
};

enum class ExportKind { Jira, App };
enum class RelocateWhere { Before, After, Into };

// 選んだ課題を順につなぐ結果
struct ChainResult {
  bool ok = false;    // 失敗（件数不足・循環）なら false。すでにつながっているだけのときは true
  int added = 0;      // 足したつなぎの数
  int existing = 0;   // すでにあったので足さなかったつなぎの数
};

// 依存を足したらどうなるか（つなぐ前の確認。ガントのドラッグのプレビュー）
struct LinkPreview {
  struct Shift {
    int idx = -1;  // tree() の添字（葉）
    Day oldStart = 0, oldFinish = 0, newStart = 0, newFinish = 0;
  };
  bool ok = false;                 // つなげる（block == None で、計算もできる）
  LinkBlock block = LinkBlock::None;
  std::vector<Shift> shifts;       // 日程が動く葉（先行の影響を受けた後続・その下流）
  int delayDays = 0;               // 後続（相手）の配下の葉の最大の遅れ（営業日。遅れなければ 0）
};

class Session {
 public:
  explicit Session(ProjectData data, const IClock* clock = nullptr, std::vector<std::string> loadWarns = {});

  // ---- 状態 ----
  ProjectData& data() { return P_; }
  const ProjectData& data() const { return P_; }
  const std::vector<Issue>& issues() const { return P_.issues; }
  const ProjectMeta& meta() const { return P_.meta; }
  const ProjectSettings& settings() const { return P_.settings; }
  // 計算結果（ツリー・日程）と、それを計算したときの課題の写し。
  // 編集で作り直しても、古いものは releaseFrame() まで残す（描画中に参照が壊れないように）
  const Tree* tree() const { return comp_ ? &comp_->T : nullptr; }
  const std::vector<ScheduleRow>* rows() const { return comp_ ? &comp_->R : nullptr; }
  const Computed* computed() const { return comp_.get(); }
  // tree()・rows() と同じ時点の課題（添字が T と一致する）。画面はこちらを使う
  const std::vector<Issue>& viewIssues() const { return comp_ ? comp_->issues : P_.issues; }
  void releaseFrame() { retired_.clear(); }
  const Calendar& calendar() const { return cal_; }
  const std::string& error() const { return error_; }
  const std::vector<std::string>& loadWarns() const { return loadWarns_; }
  void setLoadWarns(std::vector<std::string> w) { loadWarns_ = std::move(w); }
  Day baseDay() const;
  int find(const std::string& id) const;  // 添字（なければ -1）
  const Issue* issue(const std::string& id) const;

  // 変更のたびに増える（保存の予約・再描画の判断に使う）
  std::uint64_t version() const { return version_; }
  // 通知（UI が取り出して表示する）
  std::vector<Notice> takeNotices();
  void notify(std::string text, NoticeAction a = NoticeAction::None);
  // 追加するときの値の引き継ぎ（設定。取り消しの対象外で、保存もしない）
  void setInherit(const InheritSpec& s) { inherit_ = s; }
  const InheritSpec& inherit() const { return inherit_; }

  // ---- 計算と取り消し ----
  // 再計算する。エラーなら文字列を返す
  std::optional<std::string> rebuild();
  bool mutate(const std::function<void(ProjectData&)>& fn, const std::function<void(Session&)>& post = {});
  bool canUndo() const { return !undo_.empty(); }
  bool canRedo() const { return !redo_.empty(); }
  bool undo();
  bool redo();
  void clearHistory();
  // 複数の操作を Undo 1回にまとめる（入れ子可）
  void beginBatch();
  void endBatch();
  std::size_t undoSize() const { return undo_.size(); }
  static constexpr std::size_t kUndoMax = 100;

  // ---- 課題の操作（すべて mutate を通る）----
  std::string addIssue(std::string parentId, std::string afterId, std::string name = "");
  bool deleteIssues(std::vector<std::string> ids);
  bool moveIssue(std::string id, int dir);
  bool reparent(std::string id, std::string newParent);
  bool indent(std::string id);
  bool outdent(std::string id);
  bool relocate(std::string id, std::string target, RelocateWhere where);
  bool setField(std::string id, const std::function<void(Issue&)>& fn);
  bool setStatus(std::string id, std::string st);
  bool setTag(std::string id, std::string tag);
  bool setName(std::string id, std::string name);
  bool setDuration(std::string id, double w);
  bool removeScenarioValue(std::string id);
  bool setEarliest(std::string id, OptDay day);
  bool setDeadline(std::string id, OptDay day);
  bool addLink(std::string from, std::string to);
  // from → to をつないだときの日程の変化を、課題を変えずに計算する（循環などでつなげない組は ok=false と理由）
  LinkPreview previewLink(const std::string& from, const std::string& to) const;
  bool removeLink(std::string from, std::string to);
  // Blocks 以外のリンク（Relates など。Issue #12）。日程には影響しない。値は未知の列に入る
  bool addOtherLink(std::string id, std::string kind, bool outward, std::string ref);
  bool removeOtherLink(std::string id, std::size_t col);
  // 選んだ課題を A→B→C と順につなぐ（終了→開始）。order は画面の表示順（空なら木の順）。
  // 親子の両方が選ばれていれば子は数えない。すでにあるつなぎは足さない。
  // 1つでも循環になるなら何も変えず、理由を通知する。全体で Undo 1回
  ChainResult chainIssues(std::vector<std::string> ids, const std::vector<std::string>& order = {});
  // 選んだ課題どうしのつなぎをすべて外す（選ばない課題とのつなぎは残す）。外した数を返す。Undo 1回
  int unchainIssues(const std::vector<std::string>& ids);
  bool addComment(std::string id, std::string text, std::string who);
  bool delComment(std::string id, std::size_t index);
  bool bulk(std::vector<std::string> ids, const std::function<void(Issue&)>& fn);
  int applyTemplate(std::vector<std::string> parentIds, const Template& tpl);
  // 貼り付けた複数行（parseOutline の結果）を parentId の子としてまとめて作る（Undo 1回）。作った件数（失敗は 0）
  int addOutline(const std::string& parentId, const std::vector<OutlineItem>& items);

  // ---- 設定の変更（取り消しの対象外。保存はする）----
  void setBaseDate(const std::string& iso);
  void setHolidays(const std::string& text);
  void setResource(ResourceLimit r);
  void setScenario(const std::string& name);
  void addScenario(const std::string& name);
  void setNameKey(const std::string& name, const std::string& key);
  Json& ui() { return P_.settings.ui; }
  void touchUi();  // ui を書き換えたら呼ぶ（保存の予約）
  void setCollapsed(std::string id, bool collapsed);
  bool isCollapsed(const std::string& id) const;
  // 置き換えの取り込み（Undo で戻せる）
  void replaceIssues(std::vector<Issue> issues, ProjectMeta meta, std::vector<std::string> warns);

  // ---- 参照 ----
  std::optional<std::string> resolveRef(const std::string& text) const;
  std::vector<std::string> descendantsOf(const std::string& id) const;
  std::vector<std::string> siblings(const std::string& id) const;
  std::vector<VisibleRow> visibleRows(const Filter& f, const ColumnFilter* cf = nullptr) const;
  std::vector<int> visibleLeaves(const Filter& f, const ColumnFilter* cf = nullptr) const;
  std::vector<char> matchSet(const Filter& f) const;  // 添字ごとに条件に一致するか（visSet）
  Summary summary() const;
  std::string exportText(ExportKind kind) const;
  std::vector<std::string> assignees() const;  // 担当者名（重複なし・並べ替え済み）
  std::vector<std::string> allWarns() const;   // 計算の警告 + 取り込みの警告

 private:
  struct Snapshot {
    std::vector<Issue> issues;
    ProjectMeta meta;
    std::string scn;
    bool operator==(const Snapshot&) const = default;
  };
  Snapshot snapshot() const { return {P_.issues, P_.meta, P_.settings.scenario}; }
  void restore(Snapshot s);
  void persist() { ++version_; }
  Issue newIssue(const std::string& parentId, const std::string& name) const;
  // 引き継ぎを反映する。引き継いだ項目名（「担当者」など）と元（親 / 直前の兄弟）を返す
  std::vector<std::string> applyInherit(Issue& o, const std::string& parentId, const std::string& afterId,
                                        bool& fromSibling) const;
  std::string todayMinBase() const;
  Day todayDay() const;
  std::string nowDT() const;

  ProjectData P_;
  const IClock* clock_;
  Calendar cal_;
  std::shared_ptr<Computed> comp_;
  std::vector<std::shared_ptr<Computed>> retired_;
  std::string error_;
  std::vector<std::string> loadWarns_;
  std::deque<Snapshot> undo_;
  std::vector<Snapshot> redo_;
  std::vector<Notice> notices_;
  InheritSpec inherit_;
  std::uint64_t version_ = 0;
  int batchDepth_ = 0;
  std::optional<Snapshot> batchSnap_;
};

// 日本語を意識した簡易照合（カタカナをひらがなに寄せてからコードポイント順。SPEC 14-2）
int jaCompare(const std::string& a, const std::string& b);

}  // namespace roadmap
