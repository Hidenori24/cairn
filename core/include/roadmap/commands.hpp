// 編集操作・元に戻す・計算の更新（SPEC 9章。JS版 core.js の mutate など）
// Session は「いま開いているプロジェクト1つ」を持つ。UI はここだけを触る。
#pragma once

#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "roadmap/clock.hpp"
#include "roadmap/filter.hpp"
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

class Session {
 public:
  explicit Session(ProjectData data, const IClock* clock = nullptr, std::vector<std::string> loadWarns = {});

  // ---- 状態 ----
  ProjectData& data() { return P_; }
  const ProjectData& data() const { return P_; }
  const std::vector<Issue>& issues() const { return P_.issues; }
  const ProjectMeta& meta() const { return P_.meta; }
  const ProjectSettings& settings() const { return P_.settings; }
  const Tree* tree() const { return comp_ ? &comp_->T : nullptr; }
  const std::vector<ScheduleRow>* rows() const { return comp_ ? &comp_->R : nullptr; }
  const Computed* computed() const { return comp_ ? &*comp_ : nullptr; }
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

  // ---- 計算と取り消し ----
  // 再計算する。エラーなら文字列を返す
  std::optional<std::string> rebuild();
  bool mutate(const std::function<void(ProjectData&)>& fn, const std::function<void(Session&)>& post = {});
  bool canUndo() const { return !undo_.empty(); }
  bool canRedo() const { return !redo_.empty(); }
  bool undo();
  bool redo();
  void clearHistory();
  std::size_t undoSize() const { return undo_.size(); }
  static constexpr std::size_t kUndoMax = 100;

  // ---- 課題の操作（すべて mutate を通る）----
  std::string addIssue(const std::string& parentId, const std::string& afterId, const std::string& name = "");
  bool deleteIssues(const std::vector<std::string>& ids);
  bool moveIssue(const std::string& id, int dir);
  bool reparent(const std::string& id, const std::string& newParent);
  bool indent(const std::string& id);
  bool outdent(const std::string& id);
  bool relocate(const std::string& id, const std::string& target, RelocateWhere where);
  bool setField(const std::string& id, const std::function<void(Issue&)>& fn);
  bool setStatus(const std::string& id, const std::string& st);
  bool setTag(const std::string& id, const std::string& tag);
  bool setName(const std::string& id, const std::string& name);
  bool setDuration(const std::string& id, double w);
  bool removeScenarioValue(const std::string& id);
  bool setEarliest(const std::string& id, OptDay day);
  bool setDeadline(const std::string& id, OptDay day);
  bool addLink(const std::string& from, const std::string& to);
  bool removeLink(const std::string& from, const std::string& to);
  bool addComment(const std::string& id, const std::string& text, const std::string& who);
  bool delComment(const std::string& id, std::size_t index);
  bool bulk(const std::vector<std::string>& ids, const std::function<void(Issue&)>& fn);
  int applyTemplate(const std::vector<std::string>& parentIds, const Template& tpl);

  // ---- 設定の変更（取り消しの対象外。保存はする）----
  void setBaseDate(const std::string& iso);
  void setHolidays(const std::string& text);
  void setResource(ResourceLimit r);
  void setScenario(const std::string& name);
  void addScenario(const std::string& name);
  void setNameKey(const std::string& name, const std::string& key);
  Json& ui() { return P_.settings.ui; }
  void touchUi();  // ui を書き換えたら呼ぶ（保存の予約）
  void setCollapsed(const std::string& id, bool collapsed);
  bool isCollapsed(const std::string& id) const;
  // 置き換えの取り込み（Undo で戻せる）
  void replaceIssues(std::vector<Issue> issues, ProjectMeta meta, std::vector<std::string> warns);

  // ---- 参照 ----
  std::optional<std::string> resolveRef(const std::string& text) const;
  std::vector<std::string> descendantsOf(const std::string& id) const;
  std::vector<std::string> siblings(const std::string& id) const;
  std::vector<VisibleRow> visibleRows(const Filter& f) const;
  std::vector<int> visibleLeaves(const Filter& f) const;
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
  std::string todayMinBase() const;
  Day todayDay() const;
  std::string nowDT() const;

  ProjectData P_;
  const IClock* clock_;
  Calendar cal_;
  std::optional<Computed> comp_;
  std::string error_;
  std::vector<std::string> loadWarns_;
  std::deque<Snapshot> undo_;
  std::vector<Snapshot> redo_;
  std::vector<Notice> notices_;
  std::uint64_t version_ = 0;
};

// 日本語を意識した簡易照合（カタカナをひらがなに寄せてからコードポイント順。SPEC 14-2）
int jaCompare(const std::string& a, const std::string& b);

}  // namespace roadmap
