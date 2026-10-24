// 適用中の条件を見える形にし、ほかの条件を保ったまま1つずつ解除できるようにする。
#include <algorithm>
#include <functional>

#include "ui/internal.hpp"

namespace roadmap::ui {

void drawActiveFilters(Frame& f) {
  auto& c = f.c;
  auto& u = f.u;
  u.filterChips.clear();
  u.filterSummary.clear();
  u.clearFiltersBtn = {{0, 0}, {0, 0}};
  const Session* s = c.session();
  if (!s || !s->tree() || !c.filterActive()) return;

  struct Condition {
    std::string id, label;
    std::function<void()> clear;
    ImVec2 pos;
  };
  std::vector<Condition> conditions;
  const auto add = [&](const std::string& id, const std::string& label, std::function<void()> clear) {
    conditions.push_back({id, label, std::move(clear), {0, 0}});
  };
  const auto& filter = c.filter();
  if (!filter.text_q_empty()) add("query", "表示: 検索「" + filter.q + "」", [&c] { c.filter().q.clear(); });
  const auto& columns = c.columnFilter();
  if (!columns.name.empty()) add("name", "表示: 件名「" + columns.name + "」", [&c] { c.columnFilter().clear("name"); });
  static const std::map<std::string, std::string> names = {{"type", "種別"}, {"status", "状況"}, {"asg", "担当"}, {"prio", "優先度"},
      {"float", "余裕"}, {"pred", "先行"}, {"plan", "計画との差"}};
  for (const auto& [key, values] : columns.hide) {
    if (values.empty()) continue;
    const auto name = names.find(key);
    add("column:" + key, "表示: " + (name == names.end() ? key : name->second) + "（" + std::to_string(values.size()) + "種類を非表示）",
        [&c, key] { c.columnFilter().clear(key); });
  }
  if (filter.asg != AsgFilter::All) {
    const std::string who = filter.asg == AsgFilter::Name ? filter.asgName : filter.asg == AsgFilter::Unassigned ? S::kUnassigned : S::kAssigned;
    add("assignee", "強調: 担当 " + who, [&c] { c.filter().asg = AsgFilter::All; c.filter().asgName.clear(); });
  }
  if (filter.st != StFilter::All) {
    const char* status = filter.st == StFilter::Open ? "未完了" : filter.st == StFilter::Todo ? "未着手" :
        filter.st == StFilter::Prog ? "進行中" : "完了";
    add("status", std::string("強調: ") + status, [&c] { c.filter().st = StFilter::All; });
  }
  if (!filter.pr.empty()) add("priority", "強調: 優先度 " + filter.pr, [&c] { c.filter().pr.clear(); });
  if (filter.flt != FltFilter::None) {
    const char* risk = filter.flt == FltFilter::Late ? "遅れ" : filter.flt == FltFilter::Tight ? "余裕小" :
        filter.flt == FltFilter::Done ? "完了" : "未完了";
    add("risk", std::string("強調: ") + risk, [&c] { c.filter().flt = FltFilter::None; });
  }

  const auto& T = *s->tree();
  const auto shown = visSet(T, s->viewIssues(), *s->rows(), c.hideFilter(), &columns);
  const bool hasEmphasis = c.dimFilter().active();
  const auto matched = hasEmphasis ? visSet(T, s->viewIssues(), *s->rows(), filter, &columns) : shown;
  int nShown = 0, nMatched = 0;
  for (int id : T.leafOrder) {
    if (shown[static_cast<std::size_t>(id)]) ++nShown;
    if (matched[static_cast<std::size_t>(id)]) ++nMatched;
  }
  const bool applies = c.view() == app::View::Gantt || c.view() == app::View::List || c.view() == app::View::Board;
  u.filterSummary = applies ? "対象 " + std::to_string(nShown) + " / 全 " + std::to_string(T.leafOrder.size()) + " 件" :
      "このビューは全体を表示します。条件はガント・ボード・リストに適用します。";
  if (applies && hasEmphasis) u.filterSummary += "　強調 " + std::to_string(nMatched) + " 件";

  const ImVec2 p = ImGui::GetCursorScreenPos();
  const float width = ImGui::GetContentRegionAvail().x, pad = u.m.s(12), gap = u.m.s(6), rowH = u.m.s(32);
  float x = pad, y = u.m.s(34);
  std::vector<std::string> labels;
  for (auto& condition : conditions) {
    const float maxText = std::max(1.f, std::min(u.m.s(260), width - pad * 2) - u.m.s(40));
    labels.push_back(ellipsize(condition.label, maxText) + " ×");
    const float w = ImGui::CalcTextSize(labels.back().c_str()).x + u.m.s(20);
    if (x > pad && x + w > width - pad) { x = pad; y += rowH; }
    condition.pos = ImVec2(p.x + x, p.y + y);
    x += w + gap;
  }
  const float height = y + u.m.s(34);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled(p, ImVec2(p.x + width, p.y + height), u.t->surface);
  dl->AddLine(ImVec2(p.x, p.y + height - 1), ImVec2(p.x + width, p.y + height - 1), u.t->line);
  const float clearW = ImGui::CalcTextSize("すべて解除").x + u.m.s(20);
  dl->AddText(ImVec2(p.x + pad, p.y + u.m.s(8)), u.t->muted,
      ellipsize(u.filterSummary, std::max(1.f, width - clearW - pad * 3)).c_str());
  ImGui::SetCursorScreenPos(ImVec2(p.x + width - clearW - pad, p.y + u.m.s(2)));
  if (iconTextButton(u, "clear_conditions", nullptr, "すべて解除")) c.clearFilter();
  u.clearFiltersBtn = {ImGui::GetItemRectMin(), ImGui::GetItemRectMax()};
  for (std::size_t i = 0; i < conditions.size(); ++i) {
    auto& condition = conditions[i];
    ImGui::SetCursorScreenPos(condition.pos);
    if (iconTextButton(u, condition.id.c_str(), nullptr, labels[i].c_str(), false, true)) condition.clear();
    u.filterChips.push_back({condition.label, ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), true});
    tip(condition.label + "\nクリックでこの条件だけ解除します。表示の条件は課題を隠し、強調の条件はほかの課題を薄くします。");
  }
  ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + height));
  ImGui::Dummy(ImVec2(0, 0));
}

}  // namespace roadmap::ui
