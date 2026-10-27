#include "roadmap/commands.hpp"
#include "roadmap/personal.hpp"
#include "roadmap/other_links.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

#include "roadmap/issue_name.hpp"
#include "roadmap/stamps.hpp"
#include "roadmap/stats.hpp"
#include "roadmap/text.hpp"
#include "roadmap/weekly.hpp"

namespace roadmap {

namespace {
const SystemClock& systemClock() {
  static const SystemClock c;
  return c;
}

void eraseId(std::vector<Issue>& v, std::size_t idx) { v.erase(v.begin() + static_cast<std::ptrdiff_t>(idx)); }

std::ptrdiff_t indexOf(const std::vector<Issue>& v, const std::string& id) {
  for (std::size_t i = 0; i < v.size(); ++i)
    if (v[i].id == id) return static_cast<std::ptrdiff_t>(i);
  return -1;
}

// afterId の配下の最後の後ろの位置
std::size_t afterSubtree(const std::vector<Issue>& v, std::ptrdiff_t i, const std::string& rootId) {
  std::size_t j = static_cast<std::size_t>(i + 1);
  std::unordered_set<std::string> sub{rootId};
  while (j < v.size() && sub.count(v[j].parent)) {
    sub.insert(v[j].id);
    ++j;
  }
  return j;
}
}  // namespace

Session::Session(ProjectData data, const IClock* clock, std::vector<std::string> loadWarns)
    : P_(std::move(data)), clock_(clock ? clock : &systemClock()), loadWarns_(std::move(loadWarns)) {
  if (!P_.settings.ui.is_object()) P_.settings.ui = Json::object();
  rebuild();
}

Day Session::baseDay() const {
  if (auto d = parseIso(P_.settings.baseDate)) return *d;
  if (auto d = csv::parseDate(P_.settings.baseDate)) return *d;
  return clock_->today();
}

int Session::find(const std::string& id) const {
  if (!comp_) {
    const auto i = indexOf(P_.issues, id);
    return static_cast<int>(i);
  }
  return comp_->T.find(id);
}

const Issue* Session::issue(const std::string& id) const {
  const int i = find(id);
  return i < 0 ? nullptr : &P_.issues[i];
}

std::vector<Notice> Session::takeNotices() {
  std::vector<Notice> out;
  out.swap(notices_);
  return out;
}
void Session::notify(std::string t, NoticeAction a) { notices_.push_back({std::move(t), a}); }

bool Session::refuseReadOnly() {
  if (!readOnly_) return false;
  const char* const msg = "読み取り専用で開いているため、変更できません";
  if (notices_.empty() || notices_.back().text != msg) notify(msg);  // 続けて断っても通知を重ねない
  return true;
}

std::optional<std::string> Session::rebuild() {
  cal_ = Calendar::fromText(P_.settings.holidays);
  auto t = buildTree(P_.issues, P_.meta);
  if (comp_) retired_.push_back(comp_);
  if (retired_.size() > 64) retired_.erase(retired_.begin());  // releaseFrame が呼ばれない使い方でも増えすぎないように
  if (!t) {
    comp_.reset();
    error_ = t.error();
    return error_;
  }
  error_.clear();
  const auto& sc = t->scenarios;
  if (std::find(sc.begin(), sc.end(), P_.settings.scenario) == sc.end()) P_.settings.scenario = kStdScenario;
  ScheduleOptions o;
  o.base = baseDay();
  o.scenario = P_.settings.scenario;
  o.resource = P_.settings.resource;
  auto c = std::make_shared<Computed>();
  c->T = std::move(t).value();
  c->R = computeSchedule(c->T, P_.issues, cal_, o);
  c->issues = P_.issues;
  c->declPreds = declaredPreds(c->T, c->issues);
  comp_ = std::move(c);
  return std::nullopt;
}

void Session::restore(Snapshot s) {
  P_.issues = std::move(s.issues);
  P_.meta = std::move(s.meta);
  P_.settings.scenario = std::move(s.scn);
}

void Session::stampEdits(const Snapshot& before, const std::unordered_set<std::string>* only) {
  // 日付は Resolved・開始日の自動設定（setStatus）と同じ min(今日, 基準日)。作成日 → 更新日 → 完了日の順がずれないようにする
  const Day today = todayDay();
  StampWhen w;
  w.day = std::min(today, baseDay());
  if (w.day == today) {
    const LocalTime t = clock_->now();
    w.hour = t.h;
    w.minute = t.mi;
  }
  stampChanges(before.issues, P_.meta, P_.issues, w, only);
}

bool Session::mutate(const std::function<void(ProjectData&)>& fn, const std::function<void(Session&)>& post) {
  if (refuseReadOnly()) return false;
  Snapshot snap = snapshot();
  const auto only = stampOnly_;  // この編集だけに効かせる
  stampOnly_.reset();
  fn(P_);
  // 作成日・更新日の記録は、編集の入口のここで 1 回だけ（個別のコマンドに書かない）。
  // post がある編集は、post が結果を直しうる（着手可能日が動かせず元に戻る）ので、直した後に記録する
  if (!post) stampEdits(snap, only ? &*only : nullptr);
  auto err = rebuild();
  if (!err && post) {
    post(*this);
    stampEdits(snap, only ? &*only : nullptr);
    err = rebuild();
  }
  if (err) {
    restore(snap);
    rebuild();
    notify("その編集はできません: " + *err);
    ++version_;
    return false;
  }
  if (batchDepth_ == 0 && !(snapshot() == snap)) {
    undo_.push_back(std::move(snap));
    if (undo_.size() > kUndoMax) undo_.pop_front();
    redo_.clear();
  }
  ++contentVersion_;
  persist();
  return true;
}

void Session::beginBatch() {
  if (batchDepth_++ == 0) batchSnap_ = snapshot();
}

void Session::endBatch() {
  if (batchDepth_ == 0 || --batchDepth_ > 0) return;
  if (batchSnap_ && !(snapshot() == *batchSnap_)) {
    undo_.push_back(std::move(*batchSnap_));
    if (undo_.size() > kUndoMax) undo_.pop_front();
    redo_.clear();
  }
  batchSnap_.reset();
}

bool Session::undo() {
  if (undo_.empty() || refuseReadOnly()) return false;
  redo_.push_back(snapshot());
  restore(std::move(undo_.back()));
  undo_.pop_back();
  rebuild();
  ++contentVersion_;
  persist();
  return true;
}

bool Session::redo() {
  if (redo_.empty() || refuseReadOnly()) return false;
  undo_.push_back(snapshot());
  restore(std::move(redo_.back()));
  redo_.pop_back();
  rebuild();
  ++contentVersion_;
  persist();
  return true;
}

void Session::clearHistory() {
  undo_.clear();
  redo_.clear();
}

Day Session::todayDay() const { return clock_->today(); }
std::string Session::nowDT() const { return clock_->nowDT(); }

Issue Session::newIssue(const std::string& parentId, const std::string& name) const {
  const Issue* p = parentId.empty() ? nullptr : issue(parentId);
  Issue o;
  o.id = nextIssueId(P_.issues, P_.settings.attachments, P_.settings.links);
  o.key = P_.settings.key.empty() ? "" : P_.settings.key + "-" + o.id;
  o.type = !p ? "Epic" : (isEpic(*p) ? "Task" : "Sub-task");
  o.summary = name.empty() ? kDefaultIssueName : name;
  o.parent = parentId;
  o.prio = "Medium";
  o.status = "To Do";
  o.durW = 1;
  o.extra.assign(P_.meta.extraHeaders.size(), "");
  return o;
}

void Session::setCollapsed(std::string id, bool c) {
  Json& ui = P_.settings.ui;
  if (!ui.contains("collapsed") || !ui["collapsed"].is_object()) ui["collapsed"] = Json::object();
  ui["collapsed"][id] = c;
}

bool Session::isCollapsed(const std::string& id) const {
  const Json& ui = P_.settings.ui;
  if (!ui.contains("collapsed") || !ui["collapsed"].is_object()) return false;
  const auto& c = ui["collapsed"];
  auto it = c.find(id);
  return it != c.end() && it->is_boolean() && it->get<bool>();
}

void Session::persist() {
  if (!readOnly_) recordPersonal(P_.settings.personal, P_.issues, *clock_);
  ++version_;
}
void Session::setPersonal(Json data) {
  if (refuseReadOnly()) return;
  P_.settings.personal = personalFromJson(data);
  persist();
}
bool Session::setProgress(const std::string& id, std::optional<double> value) {
  const Issue* task = issue(id);
  if (refuseReadOnly() || !task || statusCat(task->status) != Cat::Prog ||
      std::any_of(P_.issues.begin(), P_.issues.end(), [&](const Issue& child) { return child.parent == id; })) return false;
  if (!setPersonalPercent(P_.settings.personal, id, value)) return false;
  persist();
  return true;
}
void Session::touchUi() { persist(); }

void Session::setWeeklyNotes(const std::string& notes) {
  if (readOnly_) return;  // 週報の下書きの所感は、読み取り専用のあいだは覚えない（通知は出さない。書き出し側が呼ぶため）
  const std::string clean = weeklySanitizeNotes(notes);
  if (clean == P_.settings.weekly.notes) return;
  P_.settings.weekly.notes = clean;
  persist();
}

void Session::setWeeklyMemberNotes(const std::string& assignee, const std::string& notes) {
  if (readOnly_) return;
  const std::string name = text::trim(text::sanitizeUtf8(assignee));
  if (name.size() > kWeeklyMaxAssigneeNameBytes) return;
  const std::string clean = weeklySanitizeNotes(notes);
  auto& saved = P_.settings.weekly.memberNotes;
  const auto it = saved.find(name);
  if ((it != saved.end() && it->second == clean) || (it == saved.end() && clean.empty())) return;
  if (clean.empty()) saved.erase(name);
  else {
    if (it == saved.end() && saved.size() >= kWeeklyMaxMemberNotes) return;
    saved[name] = clean;
  }
  persist();
}

void Session::putWeeklySnapshot(WeeklySnapshot s) {
  if (readOnly_) return;
  weeklyPutSnapshot(P_.settings.weekly, std::move(s));
  persist();
}

Result<HistoryResult> Session::recordHistory(HistoryMode mode) {
  if (readOnly_) return Error{"読み取り専用で開いているため、記録できません"};
  if (!comp_) return Error{"日程を計算できていないため、履歴を記録できません"};
  History& h = P_.settings.history;
  const Day today = todayDay();
  const bool have = historyHasWeek(h, today);
  if (mode == HistoryMode::IfMissing && have) return HistoryResult::AlreadyHave;  // 開くたびに設定ファイルを書き換えない
  auto pt = captureHistoryPoint(comp_->issues, *comp_, today);
  if (!pt.ok()) return Error{pt.error()};
  if (mode == HistoryMode::IfMissing && pt.value().total == 0) return HistoryResult::Skipped;  // 作業のないプロジェクトに空の点は足さない
  if (have) {
    const auto it = std::find_if(h.points.begin(), h.points.end(), [&](const HistoryPoint& x) { return x.weekStart == pt.value().weekStart; });
    if (it != h.points.end() && *it == pt.value()) return HistoryResult::Unchanged;  // 変わらなければ保存を予約しない
  }
  const Status st = ::roadmap::recordHistory(h, std::move(pt).value());
  if (!st.ok()) return Error{st.error()};
  persist();
  return have ? HistoryResult::Replaced : HistoryResult::Recorded;
}

Result<WeeklyAutoResult> Session::recordWeeklyAuto(WeeklyAutoMode mode) {
  if (readOnly_) return Error{"読み取り専用で開いているため、週の頭のスナップショットを取れません"};
  if (!comp_) return Error{"日程を計算できていないため、週の頭のスナップショットを取れません"};
  WeeklyAuto& a = P_.settings.weeklyAuto;
  const Day today = todayDay();
  const bool have = weeklyAutoHasWeek(a, today);
  if (mode == WeeklyAutoMode::IfMissing && have) return WeeklyAutoResult::AlreadyHave;  // 開くたびに設定ファイルを書き換えない
  const Day base = baseDay();
  const ProjectStats stats = computeStats(comp_->T, comp_->issues, comp_->R, base);
  WeeklySnapshot snap = makeWeeklySnapshot(comp_->T, comp_->issues, comp_->R, stats, base);
  if (mode == WeeklyAutoMode::IfMissing && snap.tasks == 0 && snap.items.empty()) return WeeklyAutoResult::Skipped;  // 作業のないプロジェクトには足さない
  snap.base = today;  // 週の判定は IClock の今日（履歴と同じ）。取った日として残す
  if (have) {
    const WeeklySnapshot* cur = weeklyAutoOfWeek(a, today);
    if (cur) snap.extra = cur->extra;  // 新しい版が足した未知のキーは、取り直しても残す
    WeeklyAuto probe;
    weeklyAutoPut(probe, snap);  // 保存と同じ下ごしらえをして比べる
    if (cur && probe.snaps.size() == 1 && *cur == probe.snaps[0]) return WeeklyAutoResult::Unchanged;  // 変わらなければ保存を予約しない
  }
  weeklyAutoPut(a, std::move(snap));
  persist();
  return have ? WeeklyAutoResult::Replaced : WeeklyAutoResult::Recorded;
}

Result<std::string> Session::addBaseline(const std::string& name) {
  if (readOnly_) return Error{"読み取り専用で開いているため、保存できません"};
  if (!comp_) return Error{"日程を計算できていないため、ベースラインを保存できません"};
  auto b = captureBaseline(comp_->issues, *comp_, name, nowDT());
  if (!b.ok()) return Error{b.error()};
  auto id = baselineAdd(P_.settings.baselines, std::move(b).value());
  if (!id.ok()) return id;
  baselineSetShown(P_.settings.baselines, id.value());
  ++baselineRev_;
  persist();
  return id;
}

Status Session::renameBaseline(const std::string& id, const std::string& name) {
  if (readOnly_) return Error{"読み取り専用で開いているため、変更できません"};
  const Status st = baselineRename(P_.settings.baselines, id, name);
  if (!st.ok()) return st;
  ++baselineRev_;
  persist();
  return st;
}

bool Session::removeBaseline(const std::string& id) {
  if (readOnly_) return false;
  if (!baselineRemove(P_.settings.baselines, id)) return false;
  ++baselineRev_;
  persist();
  return true;
}

bool Session::showBaseline(const std::string& id) {
  if (P_.settings.baselines.shown == id) return id.empty() || baselineFind(P_.settings.baselines, id) != nullptr;
  if (!baselineSetShown(P_.settings.baselines, id)) return false;
  ++baselineRev_;
  persist();
  return true;
}

void Session::setEvmSettings(EvmSettings s) {
  if (refuseReadOnly()) return;
  if (!std::isfinite(s.spiWarn)) s.spiWarn = kEvmSpiWarnDefault;
  s.spiWarn = std::clamp(s.spiWarn, kEvmSpiWarnMin, kEvmSpiWarnMax);
  s.extra = P_.settings.evm.extra;  // 未知のキーは画面から変えない（書き戻すだけ）
  if (P_.settings.evm == s) return;
  P_.settings.evm = std::move(s);
  persist();
}

const BaselineCompare* Session::baselineCompare() const {
  const Baseline* b = shownBaseline();
  if (!b || !comp_) return nullptr;
  if (cmp_ && cmpFor_ == comp_ && cmpId_ == b->id && cmpRev_ == baselineRev_) return cmp_.get();
  auto r = compareBaseline(*b, comp_->issues, *comp_, cal_);
  if (cmp_) cmpRetired_.push_back(std::move(cmp_));  // 描画中の参照が壊れないよう、フレームの終わりまで残す
  cmp_.reset();
  cmpFor_.reset();
  if (!r.ok()) return nullptr;
  cmp_ = std::make_shared<BaselineCompare>(std::move(r).value());
  cmpFor_ = comp_;
  cmpId_ = b->id;
  cmpRev_ = baselineRev_;
  return cmp_.get();
}

std::vector<std::string> Session::applyInherit(Issue& o, const std::string& parentId, const std::string& afterId,
                                               bool& fromSibling) const {
  std::vector<std::string> got;
  fromSibling = false;
  if (!inherit_.on) return got;
  const Issue* src = nullptr;
  if (!afterId.empty()) {
    const Issue* a = issue(afterId);
    if (a && a->parent == parentId) {
      src = a;
      fromSibling = true;
    }
  }
  if (!src && !parentId.empty()) src = issue(parentId);
  if (!src) return got;
  // 値があるものだけ写す（空のものは既定のまま）
  if (fromSibling && inherit_.type && !src->type.empty() && src->type != o.type) {
    o.type = src->type;
    got.push_back("種別");
  }
  if (inherit_.assignee && !src->assignee.empty()) {
    o.assignee = src->assignee;
    got.push_back("担当者");
  }
  if (inherit_.prio && !src->prio.empty() && src->prio != o.prio) {
    o.prio = src->prio;
    got.push_back("優先度");
  }
  if (inherit_.labels && !src->labels.empty()) {
    o.labels = src->labels;
    got.push_back("ラベル");
  }
  if (inherit_.deadline) {
    // 画面に出ている「その課題自身の期限」を写す。Deadline 列の無い CSV では Due date が期限なので、そちらへ
    OptDay dl = src->deadline;
    if (!P_.meta.hasDeadlineCol) {
      dl = std::nullopt;
      const int si = find(src->id);
      if (comp_ && si >= 0) dl = comp_->T.ownDl[si];
    }
    if (dl) {
      (P_.meta.hasDeadlineCol ? o.deadline : o.due) = dl;
      got.push_back("期限");
    }
  }
  if (fromSibling && inherit_.duration && src->durW && src->durW != o.durW) {
    o.durW = src->durW;
    got.push_back("期間");
  }
  return got;
}

std::string Session::addIssue(std::string parentId, std::string afterId, std::string name) {
  Issue o = newIssue(parentId, name);
  bool fromSibling = false;
  const auto inherited = applyInherit(o, parentId, afterId, fromSibling);
  const std::string id = o.id;
  const bool ok = mutate([&](ProjectData& P) {
    if (!afterId.empty()) {
      const auto i = indexOf(P.issues, afterId);
      const std::size_t j = afterSubtree(P.issues, i, afterId);
      P.issues.insert(P.issues.begin() + static_cast<std::ptrdiff_t>(j), std::move(o));
    } else {
      P.issues.push_back(std::move(o));
    }
    if (!parentId.empty()) setCollapsed(parentId, false);
  });
  if (ok && !inherited.empty()) {
    std::string t = fromSibling ? "直前の兄弟から引き継ぎました（" : "親から引き継ぎました（";
    for (std::size_t i = 0; i < inherited.size(); ++i) t += (i ? "・" : "") + inherited[i];
    notify(t + "）", NoticeAction::Undo);
  }
  return ok ? id : std::string();
}

std::vector<std::string> Session::descendantsOf(const std::string& id) const {
  std::vector<std::string> out;
  if (!comp_) return out;
  const Tree& T = comp_->T;
  const int i = T.find(id);
  if (i < 0) return out;
  std::vector<int> st{i};
  while (!st.empty()) {
    const int x = st.back();
    st.pop_back();
    for (int k : T.kids[x]) {
      out.push_back(T.ids[k]);
      st.push_back(k);
    }
  }
  return out;
}

std::vector<std::string> Session::siblings(const std::string& id) const {
  std::vector<std::string> out;
  if (!comp_) return out;
  const Tree& T = comp_->T;
  const int i = T.find(id);
  if (i < 0) return out;
  const int p = T.parentOf[i];
  for (int k : p >= 0 ? T.kids[p] : T.roots) out.push_back(T.ids[k]);
  return out;
}

bool Session::deleteIssues(std::vector<std::string> ids) {
  std::unordered_set<std::string> gone;
  for (const auto& id : ids) {
    gone.insert(id);
    for (const auto& k : descendantsOf(id)) gone.insert(k);
  }
  const std::size_t n = gone.size();
  const bool ok = mutate([&](ProjectData& P) {
    std::vector<Issue> keep;
    keep.reserve(P.issues.size());
    for (auto& o : P.issues)
      if (!gone.count(o.id)) keep.push_back(std::move(o));
    P.issues = std::move(keep);
    for (auto& o : P.issues)
      o.blocks.erase(std::remove_if(o.blocks.begin(), o.blocks.end(), [&](const std::string& b) { return gone.count(b) > 0; }),
                     o.blocks.end());
  });
  if (ok) notify(std::to_string(n) + "件を削除しました", NoticeAction::Undo);
  return ok;
}

bool Session::moveIssue(std::string id, int dir) {
  const auto sb = siblings(id);
  const auto it = std::find(sb.begin(), sb.end(), id);
  if (it == sb.end()) return false;
  const auto i = it - sb.begin();
  const auto j = i + dir;
  if (j < 0 || j >= static_cast<std::ptrdiff_t>(sb.size())) return false;
  return mutate([&](ProjectData& P) {
    const auto a = indexOf(P.issues, sb[i]);
    const auto b = indexOf(P.issues, sb[j]);
    std::swap(P.issues[a], P.issues[b]);
  });
}

bool Session::reparent(std::string id, std::string newParent) {
  if (!issue(id)) return false;
  return mutate([&](ProjectData& P) {
    const auto idx = indexOf(P.issues, id);
    Issue o = std::move(P.issues[idx]);
    eraseId(P.issues, static_cast<std::size_t>(idx));
    o.parent = newParent;
    P.issues.push_back(std::move(o));
    if (!newParent.empty()) setCollapsed(newParent, false);
  });
}

bool Session::indent(std::string id) {
  const auto sb = siblings(id);
  const auto it = std::find(sb.begin(), sb.end(), id);
  if (it == sb.end() || it == sb.begin()) return false;
  return reparent(id, *(it - 1));
}

bool Session::outdent(std::string id) {
  if (!comp_) return false;
  const Tree& T = comp_->T;
  const int i = T.find(id);
  if (i < 0 || T.parentOf[i] < 0) return false;
  const int p = T.parentOf[i];
  const std::string pid = T.ids[p];
  const std::string gp = T.parentOf[p] >= 0 ? T.ids[T.parentOf[p]] : "";
  return mutate([&](ProjectData& P) {
    const auto idx = indexOf(P.issues, id);
    Issue o = std::move(P.issues[idx]);
    eraseId(P.issues, static_cast<std::size_t>(idx));
    o.parent = gp;
    const auto pi = indexOf(P.issues, pid);
    P.issues.insert(P.issues.begin() + (pi + 1), std::move(o));
  });
}

bool Session::relocate(std::string id, std::string target, RelocateWhere where) {
  if (id == target || !issue(id) || !issue(target)) return false;
  const auto ds = descendantsOf(id);
  if (std::find(ds.begin(), ds.end(), target) != ds.end()) return false;
  return mutate([&](ProjectData& P) {
    const auto idx = indexOf(P.issues, id);
    Issue o = std::move(P.issues[idx]);
    eraseId(P.issues, static_cast<std::size_t>(idx));
    if (where == RelocateWhere::Into) {
      o.parent = target;
      P.issues.push_back(std::move(o));
      setCollapsed(target, false);
      return;
    }
    auto ti = indexOf(P.issues, target);
    o.parent = P.issues[ti].parent;
    if (where == RelocateWhere::After) ti = static_cast<std::ptrdiff_t>(afterSubtree(P.issues, ti, target));
    P.issues.insert(P.issues.begin() + ti, std::move(o));
  });
}

bool Session::setField(std::string id, const std::function<void(Issue&)>& fn) {
  if (!issue(id)) return false;
  return mutate([&](ProjectData& P) { fn(P.issues[indexOf(P.issues, id)]); });
}

bool Session::setStatus(std::string id, std::string st) {
  if (!issue(id)) return false;
  const Day v = std::min(todayDay(), baseDay());
  return mutate([&](ProjectData& P) {
    Issue& o = P.issues[indexOf(P.issues, id)];
    const Cat c = statusCat(st);
    // 完了から戻したとき（再オープン）は完了日を消す。残すと、もう一度完了にしたときに古い日付が使われ、推移がずれる
    if (statusCat(o.status) == Cat::Done && c != Cat::Done) o.resolved = std::nullopt;
    o.status = st;
    if (c == Cat::Done && !o.resolved) o.resolved = v;
    if (c == Cat::Prog && !o.start) o.start = v;
  });
}

bool Session::completeIssue(std::string id, Day resolved) {
  if (!issue(id)) return false;
  beginBatch();  // 状況と完了日をまとめて Undo 1回にする
  const bool ok = mutate([&](ProjectData& P) {
    Issue& o = P.issues[indexOf(P.issues, id)];
    o.status = "Done";
    o.resolved = resolved;
  });
  endBatch();
  return ok;
}

bool Session::setTag(std::string id, std::string tagIn) {
  const Issue* cur = issue(id);
  if (!cur || !comp_) return false;
  std::string tag;
  for (char c : tagIn)
    if (c != '[' && c != ']') tag += c;
  tag = text::trim(tag);
  const TagName t = tagOf(cur->summary);
  if (tag == t.tag) return true;
  if (!tag.empty()) {
    const Tree& T = comp_->T;
    const std::string low = text::toLower(tag);
    for (int i = 0; i < T.n; ++i)
      if (T.ids[i] != id && text::toLower(T.did[i]) == low) {
        notify("「" + tag + "」はすでに別の課題（" + T.name[i] + "）で使われています");
        return false;
      }
  }
  return mutate([&](ProjectData& P) {
    P.issues[indexOf(P.issues, id)].summary = (tag.empty() ? "" : "[" + tag + "] ") + t.name;
  });
}

bool Session::setName(std::string id, std::string name) {
  if (!issue(id)) return false;
  return mutate([&](ProjectData& P) {
    Issue& o = P.issues[indexOf(P.issues, id)];
    const TagName t = tagOf(o.summary);
    o.summary = (t.tag.empty() ? "" : "[" + t.tag + "] ") + (name.empty() ? std::string("（無題）") : name);
  });
}

bool Session::setDuration(std::string id, double w) {
  if (!issue(id)) return false;
  return mutate([&](ProjectData& P) {
    Issue& o = P.issues[indexOf(P.issues, id)];
    if (P.settings.scenario == kStdScenario) o.durW = w;
    else o.alt.set(P.settings.scenario, w);
  });
}

bool Session::removeScenarioValue(std::string id) {
  if (!issue(id)) return false;
  return mutate([&](ProjectData& P) { P.issues[indexOf(P.issues, id)].alt.erase(P.settings.scenario); });
}

bool Session::setEarliest(std::string id, OptDay day) {
  if (!issue(id)) return false;
  return mutate([&](ProjectData& P) { P.issues[indexOf(P.issues, id)].notBefore = day; },
                [&](Session& s) {
                  const int i = s.find(id);
                  if (day && s.comp_ && i >= 0 && s.comp_->R[i].asapStart != *day) {
                    s.P_.issues[indexOf(s.P_.issues, id)].notBefore = std::nullopt;
                    s.notify("その日には動かせません（先行や、同じ担当者の作業が終わるまで待つため）");
                  }
                });
}

bool Session::setDeadline(std::string id, OptDay day) {
  if (!issue(id)) return false;
  std::vector<std::pair<std::string, OptDay>> own;
  if (!P_.meta.hasDeadlineCol && comp_)
    for (int i = 0; i < comp_->T.n; ++i)
      if (comp_->T.ownDl[i]) own.emplace_back(comp_->T.ids[i], comp_->T.ownDl[i]);
  stampOnly_ = std::unordered_set<std::string>{id};  // 下の移し替えで値が動くだけの課題は、更新日を入れない
  return mutate([&](ProjectData& P) {
    // Deadline 列の無いCSVでは、これまで期限として使っていた Due date を Deadline 列へ移してから設定する
    if (!P.meta.hasDeadlineCol) {
      for (const auto& [k, d] : own) P.issues[indexOf(P.issues, k)].deadline = d;
      P.meta.hasDeadlineCol = true;
    }
    P.issues[indexOf(P.issues, id)].deadline = day;
  });
}

bool Session::addLink(std::string from, std::string to) {
  if (from == to) {
    notify("自分自身には依存できません");
    return false;
  }
  const Issue* f = issue(from);
  if (!f || !issue(to)) return false;
  if (std::find(f->blocks.begin(), f->blocks.end(), to) != f->blocks.end()) {
    notify("すでに設定されています");
    return false;
  }
  return mutate([&](ProjectData& P) { P.issues[indexOf(P.issues, from)].blocks.push_back(to); });
}

LinkPreview Session::previewLink(const std::string& from, const std::string& to, const std::string& dropFrom,
                                 const std::string& dropTo) const {
  LinkPreview out;
  if (!comp_) return out;
  const Tree& T = comp_->T;
  const int a = T.find(from), b = T.find(to);
  // 課題の写しで計算する（本物の課題・取り消しの履歴は触らない）
  std::vector<Issue> issues;
  if (dropFrom.empty()) {
    out.block = checkLink(T, comp_->issues, a, b);
    if (out.block != LinkBlock::None) return out;
    issues = comp_->issues;
  } else {
    // 付け替え: 元のつなぎを外した状態で、つなげるかを判定する
    const int d = T.find(dropFrom);
    if (d < 0) return out;
    issues = comp_->issues;
    auto& bl = issues[static_cast<std::size_t>(d)].blocks;
    bl.erase(std::remove(bl.begin(), bl.end(), dropTo), bl.end());
    auto t0 = buildTree(issues, P_.meta);
    if (!t0 || t0->n != T.n) return out;
    out.block = checkLink(*t0, issues, a, b);
    if (out.block != LinkBlock::None) return out;
  }
  issues[static_cast<std::size_t>(a)].blocks.push_back(to);
  auto t = buildTree(issues, P_.meta);
  if (!t || t->n != T.n) return out;
  ScheduleOptions o;
  o.base = baseDay();
  o.scenario = P_.settings.scenario;
  o.resource = P_.settings.resource;
  const auto R2 = computeSchedule(*t, issues, cal_, o);
  const auto& R = comp_->R;
  if (R2.size() != R.size()) return out;
  out.ok = true;
  const auto& targetLeaves = T.leaves[static_cast<std::size_t>(b)];
  for (int i = 0; i < T.n; ++i) {
    if (!T.isLeaf(i)) continue;
    const auto& r0 = R[static_cast<std::size_t>(i)];
    const auto& r1 = R2[static_cast<std::size_t>(i)];
    if (r0.asapStart == r1.asapStart && r0.asapFinish == r1.asapFinish) continue;
    out.shifts.push_back({i, r0.asapStart, r0.asapFinish, r1.asapStart, r1.asapFinish});
    if (r1.asapStart > r0.asapStart && std::find(targetLeaves.begin(), targetLeaves.end(), i) != targetLeaves.end())
      out.delayDays = std::max(out.delayDays, cal_.floatDays(r0.asapStart, r1.asapStart));
  }
  return out;
}

bool Session::relinkIssue(std::string oldFrom, std::string oldTo, std::string newFrom, std::string newTo) {
  const Issue* of = issue(oldFrom);
  if (!of || !issue(oldTo) || !issue(newFrom) || !issue(newTo)) return false;
  if (std::find(of->blocks.begin(), of->blocks.end(), oldTo) == of->blocks.end()) return false;  // 元のつなぎがない
  if (oldFrom == newFrom && oldTo == newTo) return false;
  // 元のつなぎを外した状態でつなげるか（循環・重複・親子・自分自身）
  const LinkPreview pv = previewLink(newFrom, newTo, oldFrom, oldTo);
  if (pv.block != LinkBlock::None) {
    notify(std::string("つなぎ替えできません: ") + linkBlockReason(pv.block));
    return false;
  }
  return mutate([&](ProjectData& P) {
    auto& ob = P.issues[static_cast<std::size_t>(indexOf(P.issues, oldFrom))].blocks;
    const auto pos = static_cast<std::size_t>(std::find(ob.begin(), ob.end(), oldTo) - ob.begin());
    ob.erase(std::remove(ob.begin(), ob.end(), oldTo), ob.end());
    if (oldFrom == newFrom) ob.insert(ob.begin() + static_cast<std::ptrdiff_t>(std::min(pos, ob.size())), newTo);  // 並びを保つ
    else P.issues[static_cast<std::size_t>(indexOf(P.issues, newFrom))].blocks.push_back(newTo);
  });
}

bool Session::removeLink(std::string from, std::string to) {
  if (!issue(from)) return false;
  return mutate([&](ProjectData& P) {
    auto& b = P.issues[indexOf(P.issues, from)].blocks;
    b.erase(std::remove(b.begin(), b.end(), to), b.end());
  });
}

bool Session::addOtherLink(std::string id, std::string kind, bool outward, std::string ref) {
  const auto ix = indexOf(P_.issues, id);
  if (ix < 0) return false;
  if (const std::string why = roadmap::checkAddOtherLink(P_.meta, P_.issues, static_cast<std::size_t>(ix), kind, outward, ref);
      !why.empty()) {
    notify(why);
    return false;
  }
  return mutate([&](ProjectData& P) {
    roadmap::addOtherLink(P.meta, P.issues, static_cast<std::size_t>(indexOf(P.issues, id)), kind, outward, ref);
  });
}

bool Session::removeOtherLink(std::string id, std::size_t col) {
  const auto ix = indexOf(P_.issues, id);
  if (ix < 0) return false;
  return mutate([&](ProjectData& P) {
    roadmap::removeOtherLink(P.meta, P.issues, static_cast<std::size_t>(indexOf(P.issues, id)), col);
  });
}

ChainResult Session::chainIssues(std::vector<std::string> ids, const std::vector<std::string>& order) {
  ChainResult res;
  if (!comp_) return res;
  const Tree& T = comp_->T;
  // 存在する課題だけ（重複なし）
  std::vector<int> picked;
  std::unordered_set<int> seen;
  for (const auto& id : ids)
    if (const int i = T.find(id); i >= 0 && seen.insert(i).second) picked.push_back(i);
  // 親子の両方が選ばれているときは親だけ（子は親のつなぎに含まれる）
  std::vector<int> kept;
  for (const int i : picked) {
    bool anc = false;
    for (int p = T.parentOf[i]; p >= 0 && !anc; p = T.parentOf[p]) anc = seen.count(p) > 0;
    if (!anc) kept.push_back(i);
  }
  if (kept.size() < 2) {
    notify("つなぐ課題を2件以上選んでください");
    return res;
  }
  // 並び: 画面の表示順（全員が含まれているとき）、そうでなければ木の順
  std::unordered_map<std::string, std::size_t> pos;
  for (std::size_t k = 0; k < order.size(); ++k) pos.emplace(order[k], k);
  const bool byView = std::all_of(kept.begin(), kept.end(), [&](int i) { return pos.count(T.ids[i]) > 0; });
  const auto key = [&](int i) { return byView ? pos[T.ids[i]] : static_cast<std::size_t>(T.dfsPos[i]); };
  std::stable_sort(kept.begin(), kept.end(), [&](int a, int b) { return key(a) < key(b); });
  // 足すつなぎ（すでにあるものは足さない）
  std::vector<std::pair<std::string, std::string>> add;
  for (std::size_t k = 0; k + 1 < kept.size(); ++k) {
    const Issue& a = comp_->issues[kept[k]];
    const std::string& to = T.ids[kept[k + 1]];
    if (std::find(a.blocks.begin(), a.blocks.end(), to) != a.blocks.end()) ++res.existing;
    else add.emplace_back(T.ids[kept[k]], to);
  }
  if (add.empty()) {
    notify("すでにつながっています");
    res.ok = true;
    return res;
  }
  // 1回の mutate で足す。循環になれば mutate が巻き戻して理由を通知する
  if (!mutate([&](ProjectData& P) {
        for (const auto& [from, to] : add) P.issues[indexOf(P.issues, from)].blocks.push_back(to);
      }))
    return res;
  res.ok = true;
  res.added = static_cast<int>(add.size());
  notify(std::to_string(res.added) + "件のつなぎを追加しました" +
             (res.existing > 0 ? "（" + std::to_string(res.existing) + "件は設定済み）" : ""),
         NoticeAction::Undo);
  return res;
}

PredPlan Session::setPredecessors(const std::string& id, std::string_view text) {
  PredPlan plan;
  const int self = comp_ ? comp_->T.find(id) : -1;
  if (self < 0) {
    plan.ok = false;
    plan.problems.push_back({"", "課題が見つかりません"});
    return plan;
  }
  plan = planPredEdit(comp_->T, comp_->issues, self, text);
  if (!plan.ok || !plan.changed()) return plan;
  // 1回の mutate で足す・外す。循環になれば mutate が巻き戻して理由を通知する
  if (!mutate([&](ProjectData& P) {
        for (const auto& pid : plan.add) {
          auto& b = P.issues[static_cast<std::size_t>(indexOf(P.issues, pid))].blocks;
          if (std::find(b.begin(), b.end(), id) == b.end()) b.push_back(id);
        }
        for (const auto& pid : plan.remove) {
          auto& b = P.issues[static_cast<std::size_t>(indexOf(P.issues, pid))].blocks;
          b.erase(std::remove(b.begin(), b.end(), id), b.end());
        }
      })) {
    plan.ok = false;
    plan.problems.push_back({"", "先行を変更できませんでした"});
    return plan;
  }
  std::string msg = "先行を更新しました（";
  if (!plan.add.empty()) msg += "追加 " + std::to_string(plan.add.size()) + "件";
  if (!plan.add.empty() && !plan.remove.empty()) msg += "・";
  if (!plan.remove.empty()) msg += "削除 " + std::to_string(plan.remove.size()) + "件";
  notify(msg + "）", NoticeAction::Undo);
  return plan;
}

int Session::unchainIssues(const std::vector<std::string>& ids) {
  std::unordered_set<std::string> sel(ids.begin(), ids.end());
  std::vector<std::pair<std::string, std::string>> del;
  for (const auto& o : P_.issues) {
    if (!sel.count(o.id)) continue;
    for (const auto& b : o.blocks)
      if (b != o.id && sel.count(b)) del.emplace_back(o.id, b);
  }
  if (del.empty()) {
    notify("外すつなぎがありません");
    return 0;
  }
  if (!mutate([&](ProjectData& P) {
        for (const auto& [from, to] : del) {
          auto& b = P.issues[indexOf(P.issues, from)].blocks;
          b.erase(std::remove(b.begin(), b.end(), to), b.end());
        }
      }))
    return 0;
  notify(std::to_string(del.size()) + "件のつなぎを外しました", NoticeAction::Undo);
  return static_cast<int>(del.size());
}

bool Session::addComment(std::string id, std::string textIn, std::string whoIn) {
  const std::string t = text::trim(textIn);
  if (t.empty() || !issue(id)) return false;
  const std::string who = text::replaceAll(whoIn.empty() ? "自分" : whoIn, ";", "；");
  const std::string at = nowDT();
  return mutate([&](ProjectData& P) { P.issues[indexOf(P.issues, id)].comments.push_back({at, who, t}); });
}

bool Session::delComment(std::string id, std::size_t index) {
  const Issue* o = issue(id);
  if (!o || index >= o->comments.size()) return false;
  return mutate([&](ProjectData& P) {
    auto& c = P.issues[indexOf(P.issues, id)].comments;
    c.erase(c.begin() + static_cast<std::ptrdiff_t>(index));
  });
}

bool Session::bulk(std::vector<std::string> ids, const std::function<void(Issue&)>& fn) {
  return mutate([&](ProjectData& P) {
    for (const auto& id : ids) {
      const auto i = indexOf(P.issues, id);
      if (i >= 0) fn(P.issues[i]);
    }
  });
}

int Session::applyTemplate(std::vector<std::string> parentIds, const Template& tpl) {
  if (tpl.items.empty() || parentIds.empty() || !comp_) return 0;
  int made = 0;
  const auto keep = comp_;  // 編集前のツリー（JS版と同じく、配下の数・表示名はここから）
  const Tree& T = keep->T;
  const bool ok = mutate([&](ProjectData& P) {
    for (const auto& pid : parentIds) {
      const auto parIdx = indexOf(P.issues, pid);
      if (parIdx < 0) continue;
      const Issue par = P.issues[parIdx];
      const std::string ptag = tagOf(par.summary).tag;
      const int ti = T.find(pid);
      const std::string pname = ti >= 0 ? T.name[ti] : par.summary;
      std::unordered_map<std::string, int> cnt;
      cnt[pid] = ti >= 0 ? static_cast<int>(T.kids[ti].size()) : 0;
      std::unordered_map<std::string, std::string> tagOfId;
      tagOfId[pid] = ptag;
      std::size_t at = afterSubtree(P.issues, parIdx, pid);
      std::vector<std::string> made1;
      std::vector<std::string> lastAt;
      for (const auto& it : tpl.items) {
        const int lv = std::max(0, std::min(it.lv, static_cast<int>(lastAt.size())));
        const std::string up = lv == 0 ? pid : lastAt[lv - 1];
        const Issue* upObj = nullptr;
        if (lv == 0) upObj = &par;
        Issue o;
        // newIssue（採番は配列の現在の状態から）
        o.id = nextIssueId(P.issues, P.settings.attachments, P.settings.links);
        o.key = P.settings.key.empty() ? "" : P.settings.key + "-" + o.id;
        o.parent = up;
        o.prio = "Medium";
        o.status = "To Do";
        o.extra.assign(P.meta.extraHeaders.size(), "");
        if (!upObj) {
          const auto ui = indexOf(P.issues, up);
          if (ui >= 0) upObj = &P.issues[ui];
        }
        o.type = !it.t.empty() ? it.t : (upObj && isEpic(*upObj) ? "Task" : "Sub-task");
        cnt[up] += 1;
        const std::string& upTag = tagOfId[up];
        const std::string tag = upTag.empty() ? "" : upTag + "-" + std::to_string(cnt[up]);
        tagOfId[o.id] = tag;
        o.summary = (tag.empty() ? "" : "[" + tag + "] ") + text::replaceAll(it.n, "{名前}", pname);
        o.durW = it.w ? *it.w : 1.0;
        o.labels = it.lab;
        o.desc = it.d;
        made1.push_back(o.id);
        lastAt.resize(static_cast<std::size_t>(lv));
        lastAt.push_back(o.id);
        P.issues.insert(P.issues.begin() + static_cast<std::ptrdiff_t>(at++), std::move(o));
        ++made;
      }
      for (std::size_t i = 0; i < tpl.items.size(); ++i)
        for (int j : tpl.items[i].after) {
          if (j < 0 || j >= static_cast<int>(made1.size()) || j == static_cast<int>(i)) continue;
          auto& b = P.issues[indexOf(P.issues, made1[j])].blocks;
          if (std::find(b.begin(), b.end(), made1[i]) == b.end()) b.push_back(made1[i]);
        }
      setCollapsed(pid, false);
    }
  });
  if (ok) notify(std::to_string(made) + "件の子タスクを追加しました", NoticeAction::Undo);
  return ok ? made : 0;
}

int Session::addOutline(const std::string& parentId, const std::vector<OutlineItem>& items) {
  if (items.empty() || !comp_ || !issue(parentId)) return 0;
  int made = 0;
  const auto keep = comp_;  // 編集前のツリー（既存の子の数はここから）
  const Tree& T = keep->T;
  const bool ok = mutate([&](ProjectData& P) {
    const auto parIdx = indexOf(P.issues, parentId);
    if (parIdx < 0) return;
    const Issue par = P.issues[parIdx];
    const std::string ptag = tagOf(par.summary).tag;
    const int ti = T.find(parentId);
    std::unordered_map<std::string, int> cnt;
    cnt[parentId] = ti >= 0 ? static_cast<int>(T.kids[ti].size()) : 0;
    std::unordered_map<std::string, std::string> tagOfId;
    tagOfId[parentId] = ptag;
    std::size_t at = afterSubtree(P.issues, parIdx, parentId);
    std::vector<std::string> lastAt;  // 深さごとの直前の課題
    for (const auto& it : items) {
      const int lv = std::max(0, std::min(it.lv, static_cast<int>(lastAt.size())));
      const std::string up = lv == 0 ? parentId : lastAt[static_cast<std::size_t>(lv) - 1];
      Issue o;
      o.id = nextIssueId(P.issues, P.settings.attachments, P.settings.links);
      o.key = P.settings.key.empty() ? "" : P.settings.key + "-" + o.id;
      o.parent = up;
      o.prio = "Medium";
      o.status = "To Do";
      o.durW = 1;
      o.extra.assign(P.meta.extraHeaders.size(), "");
      o.type = (lv == 0 && isEpic(par)) ? "Task" : "Sub-task";
      cnt[up] += 1;
      const std::string& upTag = tagOfId[up];
      // 親に番号タグがあれば、テンプレートと同じく続き番号を付ける（貼り付けたタグは付け替える）
      std::string name = it.name;
      if (!upTag.empty()) {
        const std::string bare = tagOf(name).name;
        if (!bare.empty()) name = bare;
      }
      const std::string tag = upTag.empty() ? "" : upTag + "-" + std::to_string(cnt[up]);
      tagOfId[o.id] = tag;
      o.summary = (tag.empty() ? "" : "[" + tag + "] ") + name;
      lastAt.resize(static_cast<std::size_t>(lv));
      lastAt.push_back(o.id);
      P.issues.insert(P.issues.begin() + static_cast<std::ptrdiff_t>(at++), std::move(o));
      ++made;
    }
    setCollapsed(parentId, false);
  });
  if (ok && made > 0) notify(std::to_string(made) + "件の子課題を追加しました", NoticeAction::Undo);
  return ok ? made : 0;
}

// ---------------------------------------------------------------- 設定
void Session::setBaseDate(const std::string& iso) {
  if (refuseReadOnly()) return;
  P_.settings.baseDate = iso;
  rebuild();
  persist();
}
void Session::setHolidays(const std::string& t) {
  if (refuseReadOnly()) return;
  P_.settings.holidays = t;
  rebuild();
  persist();
}
void Session::setResource(ResourceLimit r) {
  if (refuseReadOnly()) return;
  P_.settings.resource = r;
  rebuild();
  persist();
}
void Session::setScenario(const std::string& name) {
  if (refuseReadOnly()) return;
  P_.settings.scenario = name.empty() ? kStdScenario : name;
  rebuild();
  persist();
}
void Session::addScenario(const std::string& nameIn) {
  if (refuseReadOnly()) return;
  const std::string name = text::trim(nameIn);
  if (name.empty() || name == kStdScenario) return;
  auto& sc = P_.meta.scenarios;
  if (std::find(sc.begin(), sc.end(), name) == sc.end()) sc.push_back(name);
  P_.settings.scenario = name;
  rebuild();
  ++contentVersion_;  // シナリオ名は CSV の列に入る
  persist();
  notify("シナリオ「" + name + "」を追加しました。期間を変えると、このシナリオだけに保存されます");
}
void Session::setNameKey(const std::string& name, const std::string& key) {
  if (refuseReadOnly()) return;
  if (!name.empty()) P_.settings.name = name;
  P_.settings.key = text::toUpper(key);
  persist();
}

void Session::replaceIssues(std::vector<Issue> issues, ProjectMeta meta, std::vector<std::string> warns) {
  if (refuseReadOnly()) return;
  Snapshot snap = snapshot();
  P_.issues = std::move(issues);
  P_.meta = std::move(meta);
  P_.settings.ui["collapsed"] = Json::object();
  P_.settings.ui["hide"] = Json::object();
  P_.settings.scenario = kStdScenario;
  rebuild();
  undo_.push_back(std::move(snap));
  if (undo_.size() > kUndoMax) undo_.pop_front();
  redo_.clear();
  loadWarns_ = std::move(warns);
  ++contentVersion_;
  persist();
  notify(std::to_string(P_.issues.size()) + "件を読み込みました（置き換え）");
}

// ---------------------------------------------------------------- 参照
std::optional<std::string> Session::resolveRef(const std::string& in) const {
  if (!comp_) return std::nullopt;
  const Tree& T = comp_->T;
  const std::string s = text::trim(in);
  if (s.empty()) return std::nullopt;
  for (int i = 0; i < T.n; ++i)
    if (T.did[i] == s || P_.issues[i].key == s || P_.issues[i].id == s) return P_.issues[i].id;
  const std::string low = text::toLower(s);
  for (int i = 0; i < T.n; ++i)
    if (text::toLower(T.did[i]) == low || text::toLower(P_.issues[i].key) == low) return P_.issues[i].id;
  int hit = -1, count = 0;
  for (int i = 0; i < T.n; ++i)
    if (text::toLower(T.name[i]).find(low) != std::string::npos) {
      hit = i;
      ++count;
    }
  if (count == 1) return P_.issues[hit].id;
  return std::nullopt;
}

namespace {
// 列「計画との差」の絞り込みがあるときだけ、表示するベースラインとの比較から値を入れた絞り込みを作る
bool needsPlanValues(const ColumnFilter* cf) {
  if (!cf) return false;
  const auto it = cf->hide.find("plan");
  return it != cf->hide.end() && !it->second.empty();
}
}  // namespace

ColumnFilter Session::withPlanValues(const ColumnFilter& cf) const {
  ColumnFilter out = cf;
  out.planValues.clear();
  const BaselineCompare* c = baselineCompare();
  if (!c) return out;
  out.planValues.reserve(c->entries.size());
  for (const BaselineEntry& e : c->entries) out.planValues.push_back(baselineColumnValue(e));
  return out;
}

std::vector<VisibleRow> Session::visibleRows(const Filter& f, const ColumnFilter* cfIn) const {
  ColumnFilter local;
  const ColumnFilter* cf = cfIn;
  if (needsPlanValues(cfIn)) {
    local = withPlanValues(*cfIn);
    cf = &local;
  }
  if (!comp_) return {};
  return roadmap::visibleRows(comp_->T, comp_->issues, comp_->R, viewStateFromUi(comp_->T, P_.settings.ui), f, cf);
}

std::vector<int> Session::visibleLeaves(const Filter& f, const ColumnFilter* cfIn) const {
  ColumnFilter local;
  const ColumnFilter* cf = cfIn;
  if (needsPlanValues(cfIn)) {
    local = withPlanValues(*cfIn);
    cf = &local;
  }
  if (!comp_) return {};
  return roadmap::visibleLeaves(comp_->T, comp_->issues, comp_->R, viewStateFromUi(comp_->T, P_.settings.ui), f, cf);
}

std::vector<char> Session::matchSet(const Filter& f) const {
  if (!comp_) return {};
  return visSet(comp_->T, comp_->issues, comp_->R, f);
}

Summary Session::summary() const { return comp_ ? summarize(comp_->T, comp_->R) : Summary{}; }

std::string Session::exportText(ExportKind kind) const {
  if (kind == ExportKind::App) return exportAppCsv(P_.issues, P_.meta);
  return exportJiraCsv(P_.issues, P_.meta, comp_.get());
}

std::vector<std::string> Session::assignees() const {
  std::vector<std::string> out;
  std::unordered_set<std::string> seen;
  for (const auto& o : P_.issues) {
    const std::string a = text::trim(o.assignee);
    if (!a.empty() && seen.insert(a).second) out.push_back(a);
  }
  std::stable_sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) { return jaCompare(a, b) < 0; });
  return out;
}

std::vector<std::string> Session::allWarns() const {
  std::vector<std::string> out;
  if (comp_) out = comp_->T.warns;
  out.insert(out.end(), loadWarns_.begin(), loadWarns_.end());
  return out;
}

int jaCompare(const std::string& a, const std::string& b) {
  const auto key = [](const std::string& s) {
    std::u32string u = text::toU32(text::toLower(s));
    for (char32_t& c : u)
      if (c >= 0x30A1 && c <= 0x30F6) c -= 0x60;  // カタカナ → ひらがな
    return u;
  };
  const auto ka = key(a), kb = key(b);
  if (ka < kb) return -1;
  if (kb < ka) return 1;
  return a < b ? -1 : (b < a ? 1 : 0);
}

}  // namespace roadmap
