#include "ui/internal.hpp"
#include "roadmap/personal.hpp"
#include "roadmap/text.hpp"
#include <imgui_stdlib.h>
#include <algorithm>
#include <charconv>
#include <cmath>

namespace roadmap::ui {
namespace {
using PersonalCard = UiState::PersonalEditor::Card;
std::string js(const Json& j, const char* key) {
  return j.is_object() && j.contains(key) && j[key].is_string() ? j[key].get<std::string>() : "";
}
std::optional<double> number(const Json& j, const char* key) {
  if (!j.is_object() || !j.contains(key) || !j[key].is_number()) return {};
  const double value = j[key].get<double>();
  return std::isfinite(value) && value >= 0 && value <= 100 ? std::optional<double>(value) : std::nullopt;
}
std::string percent(const Json& j, const char* key) {
  const auto value = number(j, key);
  return value ? csv::fmtNum(*value) + "%" : "—";
}
bool enabled(const Json& member) { return !member.contains("enabled") || member["enabled"] != false; }
void persistDraft(Frame& f, PersonalCard& card) {
  if (f.c.readOnly() || card.loadedKey.empty() || card.draft.empty()) return;
  auto data = personalFromJson(f.c.session()->personal());
  data["drafts"][card.loadedKey] = card.draft;
  f.c.session()->setPersonal(std::move(data));
  f.c.afterEdit();
}
void refreshDraft(Frame& f, const std::string& member, PersonalCard& card, bool restore) {
  auto& state = f.u.personal;
  Session& s = *f.c.session();
  const auto date = parseIso(state.date), from = parseIso(state.from), to = parseIso(state.to);
  if (!date || !from || !to) { card.error = "日付を YYYY-MM-DD で入力してください"; return; }
  const std::string key = personalDraftKey(member, *date);
  auto data = personalFromJson(s.personal());
  syncPersonalMembers(data, s.issues());
  if (restore && data["drafts"].contains(key) && data["drafts"][key].is_object()) {
    card.draft = data["drafts"][key];
  } else {
    const Baseline* baseline = nullptr;
    for (const auto& b : s.baselines().list) if (b.id == state.baseline) baseline = &b;
    const auto name = data["members"].contains(member) ? js(data["members"][member], "name") : "";
    auto draft = makePersonalDraft(data, s.issues(), member, *date, *from, *to, baseline, s.calendar(), f.c.weeklyMemberNotes(name));
    if (!draft) { card.error = draft.error(); return; }
    if (key == card.loadedKey && card.draft.is_object()) {
      for (const char* field : {"extra", "blockers", "next"}) if (card.draft.contains(field)) (*draft)[field] = card.draft[field];
      if (card.draft.contains("tasks") && card.draft["tasks"].is_array())
        for (auto& task : (*draft)["tasks"]) for (const auto& old : card.draft["tasks"])
          if (js(old, "id") == js(task, "id")) task["comment"] = js(old, "comment");
    }
    card.draft = std::move(*draft);
  }
  card.loadedKey = key; card.error.clear(); card.savedId.clear();
  if (!f.c.readOnly()) {
    auto latest = personalFromJson(s.personal());
    if (!latest["drafts"].contains(key) || latest["drafts"][key] != card.draft) {
      latest["drafts"][key] = card.draft;
      s.setPersonal(std::move(latest));
      f.c.afterEdit();
    }
  }
}
bool textField(const char* label, Json& object, const char* key, float height = 0) {
  std::string text = js(object, key);
  ImGui::SetNextItemWidth(-1);
  const bool changed = height > 0 ? ImGui::InputTextMultiline(label, &text, ImVec2(-1, height)) : ImGui::InputText(label, &text);
  if (changed) object[key] = text;
  return changed;
}

void exportButtons(Frame& f, const std::vector<std::string>& ids) {
  ImGui::BeginDisabled(ids.empty());
  for (const char* kind : {"HTML", "Markdown", "PDF"}) {
    const std::string type(kind);
    const std::string label = type + " を保存…";
    if (ImGui::Button(label.c_str())) {
      // 保存ダイアログを開く前に、選択した写しから出力内容を固定する。
      auto rendered = renderPersonalReports(f.c.session()->personal(), ids, type != "Markdown");
      if (!rendered) f.c.toast(rendered.error());
      else {
        const std::string ext = type == "PDF" ? "pdf" : type == "HTML" ? "html" : "md";
        const std::string name = "個人報告_" + f.c.clock().todayIso() + "." + ext;
        deferAfterFrame(f, [&f, type, ext, name, content = *rendered] {
          if (type == "PDF") {
            auto path = f.p.pickSaveFile(name, ext, "PDF ファイル"); if (!path) return;
            std::string error;
            if (f.p.printHtmlToPdf(content, *path, &error)) f.c.toast("PDF を保存しました");
            else f.c.toast("PDF を作成できません: " + error);
          } else {
            auto path = f.p.pickSaveFile(name, ext, type + " ファイル");
            if (path) (void)f.c.exportPersonal(*path, content);
          }
        });
      }
    }
    ImGui::SameLine();
  }
  if (ImGui::Button("印刷内容をプレビュー")) ImGui::OpenPopup("印刷プレビュー");
  ImGui::EndDisabled();
  ImGui::SetNextWindowSize(ImVec2(f.u.m.s(850), f.u.m.s(650)), ImGuiCond_Appearing);
  if (ImGui::BeginPopupModal("印刷プレビュー", nullptr, ImGuiWindowFlags_NoSavedSettings)) {
    if (ImGui::Button("閉じる")) ImGui::CloseCurrentPopup();
    auto markdown = renderPersonalReports(f.c.session()->personal(), ids, false);
    ImGui::TextDisabled("保存済みの内容を表示しています。印刷の改ページは HTML / PDF に適用されます。");
    if (markdown) ImGui::TextWrapped("%s", markdown->c_str());
    ImGui::EndPopup();
  }
}
void exportPanel(Frame& f, const Json& data) {
  auto& state = f.u.personal;
  if (!ImGui::CollapsingHeader("保存済みの報告を出力（複数人を一括選択可）")) return;
  if (ImGui::Button("この報告日の全員分を選択")) {
    state.exportIds.clear();
    for (auto member = data["members"].begin(); member != data["members"].end(); ++member) if (enabled(*member)) {
      std::string latest;
      for (auto report = data["reports"].begin(); report != data["reports"].end(); ++report)
        if (js(*report, "memberId") == member.key() && js(*report, "date") == state.date) latest = report.key();
      if (!latest.empty()) state.exportIds.insert(latest);
    }
  }
  ImGui::SameLine(); if (ImGui::Button("選択を解除")) state.exportIds.clear();
  for (auto member = data["members"].begin(); member != data["members"].end(); ++member) if (enabled(*member)) {
    bool found = false;
    for (const auto& report : data["reports"]) if (js(report, "memberId") == member.key() && js(report, "date") == state.date) found = true;
    if (!found) ImGui::TextDisabled("%s: %s の保存済み報告なし（出力には含めません）", js(*member, "name").c_str(), state.date.c_str());
  }
  ImGui::BeginChild("saved-report-choices", ImVec2(0, f.u.m.s(120)), ImGuiChildFlags_Border);
  for (auto it = data["reports"].begin(); it != data["reports"].end(); ++it) {
    bool chosen = state.exportIds.count(it.key()) != 0;
    const std::string label = js(*it, "memberName") + " / " + js(*it, "date") + " / 保存 " + js(*it, "savedAt") + "##" + it.key();
    if (ImGui::Checkbox(label.c_str(), &chosen)) {
      if (chosen) state.exportIds.insert(it.key()); else state.exportIds.erase(it.key());
    }
  }
  ImGui::EndChild();
  exportButtons(f, std::vector<std::string>(state.exportIds.begin(), state.exportIds.end()));
}
void snapshotBody(Frame& f, const Json& report) {
  ImGui::TextWrapped("%s  %s", js(report, "memberName").c_str(), js(report, "date").c_str());
  ImGui::TextDisabled("%s 〜 %s", js(report, "from").c_str(), js(report, "to").c_str());
  for (const char* section : {"tasks", "extra"}) {
    if (!report.contains(section) || !report[section].is_array()) continue;
    for (const auto& task : report[section]) {
      ImGui::Separator();
      ImGui::TextWrapped("%s", js(task, "name").c_str());
      ImGui::TextDisabled("%s  予定 %s / 実績 %s", js(task, "status").c_str(), percent(task, "planned").c_str(), percent(task, "actual").c_str());
      if (!js(task, "comment").empty()) ImGui::TextWrapped("%s", js(task, "comment").c_str());
    }
  }
  for (const char* field : {"blockers", "next"}) if (!js(report, field).empty()) {
    ImGui::SeparatorText(std::string(field) == "blockers" ? "困りごと" : "次の期間の予定");
    ImGui::TextWrapped("%s", js(report, field).c_str());
  }
  (void)f;
}
bool taskRows(Frame& f, Json& report, bool extra) {
  bool changed = false;
  const char* key = extra ? "extra" : "tasks";
  if (!report.contains(key) || !report[key].is_array()) report[key] = Json::array();
  auto& tasks = report[key];
  const auto& m = f.u.m;
  std::size_t lines = tasks.size() + 1;
  for (std::size_t i = 0; i < tasks.size(); ++i)
    if (!js(tasks[i], "comment").empty() || f.u.personal.comments.count(f.u.personal.member + ":" + key + std::to_string(i))) ++lines;
  const float rowHeight = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().CellPadding.y * 2;
  const float tableHeight = std::min(m.s(extra ? 240.f : 380.f), rowHeight * static_cast<float>(lines) + ImGui::GetStyle().ScrollbarSize + m.s(6));
  if (ImGui::BeginTable(key, 6, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY,
                        ImVec2(0, tableHeight), std::max(m.s(700), ImGui::GetContentRegionAvail().x))) {
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("課題", ImGuiTableColumnFlags_WidthStretch, 3);
    for (const char* name : {"状態", "予定", "実績", "前回", "コメント"}) ImGui::TableSetupColumn(name, ImGuiTableColumnFlags_WidthStretch, 1);
    ImGui::TableHeadersRow();
    int remove = -1;
    for (std::size_t i = 0; i < tasks.size(); ++i) {
      auto& task = tasks[i]; if (!task.is_object()) continue;
      ImGui::PushID(static_cast<int>(i));
      const std::string id = js(task, "id");
      if (!extra && statusCat(js(task, "status")) == Cat::Prog) {
        const auto current = personalPercent(f.c.session()->personal(), id);
        task["actual"] = current ? Json(*current) : Json(nullptr);
      }
      ImGui::TableNextRow();
      const int tone = progressTone(number(task, "actual"), number(task, "planned"));
      if (tone) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, withAlpha(tone < 0 ? f.u.t->late : f.u.t->accent, 0.12f));
      ImGui::TableNextColumn();
      if (extra) changed |= textField("##name", task, "name");
      else { ImGui::TextUnformatted(ellipsize(js(task, "name"), ImGui::GetContentRegionAvail().x).c_str()); tip(js(task, "name")); }
      ImGui::TableNextColumn();
      if (extra) {
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##status", js(task, "status").c_str())) {
          for (const char* st : {"To Do", "In Progress", "Done"}) if (ImGui::Selectable(st, js(task, "status") == st)) { task["status"] = st; changed = true; }
          ImGui::EndCombo();
        }
      } else statusPill(f.u, "status", js(task, "status"), false);
      ImGui::TableNextColumn(); ImGui::TextUnformatted(percent(task, "planned").c_str());
      ImGui::TableNextColumn();
      if (!extra && statusCat(js(task, "status")) == Cat::Prog) {
        const std::string currentInput = number(task, "actual") ? csv::fmtNum(*number(task, "actual")) : "";
        auto [inputIt, inserted] = f.u.personal.inputs.try_emplace(id, currentInput);
        (void)inserted;
        std::string& input = inputIt->second;
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##actual", "未入力", &input);
        if (ImGui::IsItemDeactivatedAfterEdit()) {
          double value = 0;
          const std::string clean = text::trim(input);
          const auto parsed = std::from_chars(clean.data(), clean.data() + clean.size(), value);
          if (clean.empty() || (parsed.ec == std::errc() && parsed.ptr == clean.data() + clean.size() && std::isfinite(value) && value >= 0 && value <= 100)) {
            const auto actual = clean.empty() ? std::nullopt : std::optional<double>(value);
            if (f.c.session()->setProgress(id, actual)) { task["actual"] = actual ? Json(*actual) : Json(nullptr); changed = true; }
          } else f.c.toast("進捗率は 0〜100、または空欄にしてください");
        }
        if (!ImGui::IsItemActive() && !ImGui::IsItemDeactivatedAfterEdit()) input = currentInput;
      } else ImGui::TextUnformatted(percent(task, "actual").c_str());
      ImGui::TableNextColumn(); ImGui::TextUnformatted(percent(task, "previous").c_str());
      ImGui::TableNextColumn();
      const std::string commentKey = f.u.personal.member + ":" + key + std::to_string(i);
      if (ImGui::SmallButton("コメント")) {
        if (f.u.personal.comments.count(commentKey)) f.u.personal.comments.erase(commentKey);
        else f.u.personal.comments.insert(commentKey);
      }
      if (extra) { ImGui::SameLine(); if (ImGui::SmallButton("×")) remove = static_cast<int>(i); }
      if (!js(task, "comment").empty() || f.u.personal.comments.count(commentKey)) {
        ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0);
        changed |= textField("##comment", task, "comment");
        tip("任意のコメント。書かなくても保存できます");
      }
      ImGui::PopID();
    }
    if (remove >= 0) { tasks.erase(tasks.begin() + remove); changed = true; }
    ImGui::EndTable();
  }
  return changed;
}
std::vector<const Json*> memberReports(const Json& data, const std::string& id) {
  std::vector<const Json*> out;
  for (const auto& report : data["reports"]) if (js(report, "memberId") == id) out.push_back(&report);
  std::reverse(out.begin(), out.end());
  std::stable_sort(out.begin(), out.end(), [](const Json* a, const Json* b) {
    if (js(*a, "date") != js(*b, "date")) return js(*a, "date") > js(*b, "date");
    return js(*a, "savedUtc") > js(*b, "savedUtc");
  });
  return out;
}
void memberHistoryPane(Frame& f, const Json& data, const std::string& member, Day before) {
  auto& state = f.u.personal;
  const auto all = memberReports(data, member);
  std::vector<const Json*> reports;
  for (const Json* report : all) {
    const auto day = parseIso(js(*report, "date"));
    if (day && *day < before) reports.push_back(report);
  }
  ImGui::SeparatorText("過去の報告");
  if (reports.empty()) {
    state.memberHistory.erase(member);
    ImGui::TextDisabled("この報告日より前の履歴はありません");
    return;
  }
  auto& selectedId = state.memberHistory[member];
  std::size_t index = 0;
  for (; index < reports.size(); ++index) if (js(*reports[index], "id") == selectedId) break;
  if (index == reports.size()) { index = 0; selectedId = js(*reports.front(), "id"); }
  ImGui::BeginDisabled(index == 0);
  if (ImGui::SmallButton("← 新しい")) { --index; selectedId = js(*reports[index], "id"); }
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::TextDisabled("%zu / %zu", index + 1, reports.size());
  ImGui::SameLine();
  ImGui::BeginDisabled(index + 1 >= reports.size());
  if (ImGui::SmallButton("→ 過去へ")) { ++index; selectedId = js(*reports[index], "id"); }
  ImGui::EndDisabled();
  ImGui::Separator();
  snapshotBody(f, *reports[index]);
}
void personalHistory(Frame& f, const Json& data) {
  auto& u = f.u; auto& state = u.personal;
  std::size_t count = 0;
  for (auto it = data["members"].begin(); it != data["members"].end(); ++it) count = std::max(count, memberReports(data, it.key()).size());
  if (!count) { ImGui::TextDisabled("まだ保存された報告はありません。編集ページで報告を確定すると、ここに残ります。"); return; }
  ImGui::TextWrapped("左端は担当者、右はその人の報告を新しい順に表示します。横にスクロールして参照できます。");
  ImGui::BeginDisabled(state.historyOffset == 0);
  if (ImGui::Button("新しい報告へ")) state.historyOffset = std::max(0, state.historyOffset - 8);
  ImGui::EndDisabled(); ImGui::SameLine();
  ImGui::BeginDisabled(static_cast<std::size_t>(state.historyOffset + 8) >= count);
  if (ImGui::Button("さらに古い報告へ")) state.historyOffset += 8;
  ImGui::EndDisabled();
  const int slots = std::min(8, static_cast<int>(count) - state.historyOffset);
  if (ImGui::BeginTable("personal-history", slots + 1, ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY |
       ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg, ImGui::GetContentRegionAvail(), u.m.s(180.f + static_cast<float>(slots) * 270.f))) {
    ImGui::TableSetupScrollFreeze(1, 1);
    ImGui::TableSetupColumn("担当者", ImGuiTableColumnFlags_WidthFixed, u.m.s(180));
    for (int col = 0; col < slots; ++col) {
      const int index = state.historyOffset + col;
      const std::string label = index == 0 ? "最新" : std::to_string(index) + "つ前";
      ImGui::TableSetupColumn(label.c_str(), ImGuiTableColumnFlags_WidthFixed, u.m.s(260));
    }
    ImGui::TableHeadersRow();
    for (auto it = data["members"].begin(); it != data["members"].end(); ++it) {
      const auto reports = memberReports(data, it.key());
      if (!enabled(*it) && reports.empty()) continue;
      ImGui::PushID(it.key().c_str()); ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0);
      avatar(u, js(*it, "name")); ImGui::SameLine(); ImGui::TextWrapped("%s", js(*it, "name").c_str());
      if (!enabled(*it)) ImGui::TextDisabled("対象外・履歴を保持");
      if (enabled(*it) && ImGui::SmallButton("報告カードへ戻る")) state.history = false;
      for (int col = 0; col < slots; ++col) {
        ImGui::TableSetColumnIndex(col + 1);
        const std::size_t index = static_cast<std::size_t>(state.historyOffset + col);
        if (index >= reports.size()) { ImGui::TextDisabled("報告なし"); continue; }
        const Json& report = *reports[index];
        int done = 0, progress = 0; std::string excerpt;
        if (report.contains("tasks") && report["tasks"].is_array()) for (const auto& task : report["tasks"]) {
          if (statusCat(js(task, "status")) == Cat::Done) ++done; else ++progress;
          if (excerpt.empty()) excerpt = js(task, "comment");
        }
        const std::string id = js(report, "id"); ImGui::PushID(id.c_str());
        const ImVec2 p = ImGui::GetCursorScreenPos(), box(ImGui::GetContentRegionAvail().x, u.m.s(105));
        const float amount = hoverAmount(u, "hover", ImGui::IsMouseHoveringRect(p, ImVec2(p.x + box.x, p.y + box.y)));
        if (u.m.modern) ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + box.x, p.y + box.y), blendColor(u.t->surface, u.t->hover, amount), u.m.s(8));
        if (ImGui::Selectable("##open", state.historyId == id, 0, box)) {
          state.historyId = id; state.historyDay = js(report, "date"); state.historyFade = 0;
          ImGui::OpenPopup("報告の履歴");
        }
        const ImVec2 after = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos(ImVec2(p.x + u.m.s(8), p.y + u.m.s(6)));
        ImGui::TextUnformatted(js(report, "date").c_str());
        ImGui::TextDisabled("%s 〜 %s", js(report, "from").c_str(), js(report, "to").c_str());
        ImGui::Text("完了 %d / 進行中 %d", done, progress);
        ImGui::TextDisabled("%s", ellipsize(excerpt, box.x - u.m.s(16)).c_str());
        ImGui::SetCursorScreenPos(after);
        ImGui::Dummy(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(u.m.s(850), u.m.s(650)), ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal("報告の履歴", nullptr, ImGuiWindowFlags_NoSavedSettings)) {
          if (ImGui::Button("閉じる")) ImGui::CloseCurrentPopup();
          ImGui::TextDisabled("保存時点の報告です。現在のタスクを変更しても、この内容は変わりません。");
          state.historyFade = motionStep(state.historyFade, true, ImGui::GetIO().DeltaTime, u.m.animate);
          ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * (0.5f + 0.5f * state.historyFade));
          exportButtons(f, {id});
          snapshotBody(f, report);
          ImGui::PopStyleVar();
          if (ImGui::CollapsingHeader("日々の進捗記録")) {
            ImGui::SetNextItemWidth(u.m.s(160)); ImGui::InputText("参照日", &state.historyDay);
            const auto day = parseIso(state.historyDay);
            if (!day) ImGui::TextDisabled("日付を YYYY-MM-DD で入力してください");
            else {
              ImGui::TextWrapped("その日までの最後の記録を表示します。記録のない日や未入力の値は補完しません。");
              if (report.contains("tasks") && report["tasks"].is_array()) for (const auto& task : report["tasks"]) {
                const Json* latest = nullptr;
                for (const auto& event : data["history"]) if (js(event, "id") == js(task, "id")) {
                  const auto when = parseIso(js(event, "at").substr(0, 10));
                  if (when && *when <= *day) latest = &event;
                }
                ImGui::TextWrapped("%s: %s  最終記録 %s", js(task, "name").c_str(), latest ? percent(*latest, "percent").c_str() : "未記録", latest ? js(*latest, "at").c_str() : "—");
                ImGui::PushID(js(task, "id").c_str());
                if (ImGui::TreeNode("変更履歴")) {
                  for (const auto& event : data["history"]) if (js(event, "id") == js(task, "id"))
                    ImGui::TextWrapped("%s  %s  %s  %s", js(event, "at").c_str(), percent(event, "percent").c_str(), js(event, "status").c_str(), js(event, "assignee").c_str());
                  ImGui::TreePop();
                }
                ImGui::PopID();
              }
            }
          }
          ImGui::EndPopup();
        }
        ImGui::PopID();
      }
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
}

}
void drawPersonalView(Frame& f, ImVec2 size) {
  auto& c = f.c; auto& u = f.u; auto& state = u.personal;
  Session* s = c.session(); if (!s) return;
  if (state.project != c.currentId()) {
    state = {}; state.project = c.currentId();
    state.date = state.to = c.clock().todayIso(); state.from = fmtIso(c.clock().today() - 6);
    state.baseline = s->baselines().shown;
  }
  auto& data = state.cache;
  if (state.cacheVersion != s->version() || data.empty()) {
    data = personalFromJson(s->personal());
    syncPersonalMembers(data, s->issues());
    if (!c.readOnly() && data != s->personal()) { s->setPersonal(data); c.afterEdit(); }
    state.cacheVersion = s->version();
  }
  if (state.member.empty()) for (auto it = data["members"].begin(); it != data["members"].end(); ++it)
    if (enabled(*it)) { state.member = it.key(); break; }
  ImGui::BeginChild("personal-page", size, ImGuiChildFlags_AlwaysUseWindowPadding);
  ImGui::SeparatorText("個人報告");
  if (ImGui::Button(state.history ? "報告カードへ戻る" : "保存した報告・履歴を見る")) state.history = !state.history;
  exportPanel(f, data);
  if (state.history) { personalHistory(f, data); ImGui::EndChild(); return; }
  if (ImGui::CollapsingHeader("報告対象者を管理")) {
    ImGui::BeginDisabled(c.readOnly());
    for (auto it = data["members"].begin(); it != data["members"].end(); ++it) {
      ImGui::PushID(it.key().c_str()); bool on = enabled(*it);
      if (ImGui::Checkbox("##enabled", &on)) { (*it)["enabled"] = on; s->setPersonal(data); c.afterEdit(); }
      ImGui::SameLine();
      const std::string currentName = js(*it, "name");
      auto [nameIt, inserted] = state.inputs.try_emplace("member:" + it.key(), currentName);
      (void)inserted;
      std::string& name = nameIt->second;
      ImGui::SetNextItemWidth(u.m.s(230)); ImGui::InputText("##name", &name);
      if (ImGui::IsItemDeactivatedAfterEdit()) {
        if (renamePersonalMember(data, it.key(), name)) { s->setPersonal(data); c.afterEdit(); } else c.toast("空欄や既存の担当者名には変更できません");
      }
      if (!ImGui::IsItemActive() && !ImGui::IsItemDeactivatedAfterEdit()) name = currentName;
      ImGui::PopID();
    }
    ImGui::SetNextItemWidth(u.m.s(230)); ImGui::InputTextWithHint("##new-member", "タスクを持たない人も追加できます", &state.newName);
    ImGui::SameLine(); if (ImGui::Button("追加") && !text::trim(state.newName).empty()) {
      addPersonalMember(data, state.newName); state.newName.clear(); s->setPersonal(data); c.afterEdit();
    }
    ImGui::EndDisabled();
  }
  bool anyMember = false;
  for (auto it = data["members"].begin(); it != data["members"].end(); ++it) anyMember |= enabled(*it);
  if (!anyMember) { ImGui::TextWrapped("報告対象者がいません。「報告対象者を管理」で追加または選択してください。"); ImGui::EndChild(); return; }
  const bool wideDates = ImGui::GetContentRegionAvail().x >= u.m.s(650);
  int dateIndex = 0;
  for (auto pair : {std::pair<const char*, std::string*>{"報告日", &state.date}, {"開始日", &state.from}, {"終了日", &state.to}}) {
    if (dateIndex++ > 0 && wideDates) ImGui::SameLine(0, u.m.s(20));
    ImGui::SetNextItemWidth(u.m.s(145)); ImGui::InputText(pair.first, pair.second);
  }
  if (ImGui::Button("報告日までの7日間")) {
    if (auto day = parseIso(state.date)) { state.from = fmtIso(*day - 6); state.to = state.date; }
  }
  ImGui::SameLine();
  if (ImGui::Button("14日間")) {
    if (auto day = parseIso(state.date)) { state.from = fmtIso(*day - 13); state.to = state.date; }
  }
  ImGui::SameLine(); bool applyPeriod = ImGui::Button("期間を全員に適用");
  ImGui::SetNextItemWidth(u.m.s(280));
  std::string baselineName = "ベースラインなし（予定率は比較しません）";
  for (const auto& b : s->baselines().list) if (b.id == state.baseline) baselineName = b.name;
  if (ImGui::BeginCombo("予定の基準", baselineName.c_str())) {
    if (ImGui::Selectable("比較しない", state.baseline.empty())) { state.baseline.clear(); applyPeriod = true; }
    for (const auto& b : s->baselines().list) if (ImGui::Selectable(b.name.c_str(), state.baseline == b.id)) { state.baseline = b.id; applyPeriod = true; }
    ImGui::EndCombo();
  }
  ImGui::TextWrapped("実績は任意入力。予定との差が±10ポイント以内は通常色、遅れは赤、先行は青。カードを上から見て、右の「→」で過去の報告へ進めます。");
  if (c.readOnly()) ImGui::TextDisabled("読み取り専用：閲覧・出力できます。下書きと実績は変更しません。");
  if (ImGui::BeginTable("personal-member-cards", 1, ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg | ImGuiTableFlags_PadOuterX,
                        ImVec2(0, 0))) {
    ImGui::TableSetupColumn("担当者", ImGuiTableColumnFlags_WidthStretch);
    for (auto it = data["members"].begin(); it != data["members"].end(); ++it) {
      if (!enabled(*it)) continue;
      const std::string member = it.key();
      state.member = member;
      auto& card = state.cards[member];
      ImGui::PushID(member.c_str());
      const auto date = parseIso(state.date);
      const std::string key = date ? personalDraftKey(member, *date) : std::string{};
      if (!key.empty() && applyPeriod) {
        if (card.loadedKey != key) {
          auto latest = personalFromJson(s->personal());
          card.draft = latest["drafts"].contains(key) && latest["drafts"][key].is_object()
                           ? latest["drafts"][key] : Json::object();
          card.loadedKey = key;
        }
        refreshDraft(f, member, card, false);
      } else if (!key.empty() && card.loadedKey != key) refreshDraft(f, member, card, true);
      ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0);
      avatar(u, js(*it, "name")); ImGui::SameLine();
      ImGui::TextUnformatted(js(*it, "name").c_str());
      if (!card.draft.empty()) {
        const int taskCount = card.draft.contains("tasks") && card.draft["tasks"].is_array() ? static_cast<int>(card.draft["tasks"].size()) : 0;
        ImGui::SameLine(); ImGui::TextDisabled("課題 %d 件", taskCount);
      }
      if (!card.error.empty()) ImGui::TextWrapped("%s", card.error.c_str());
      else if (card.draft.empty()) ImGui::TextDisabled("報告の下書きを読み込めませんでした");
      else {
        const bool pendingPeriod = state.date != js(card.draft, "date") || state.from != js(card.draft, "from") || state.to != js(card.draft, "to");
        ImGui::TextDisabled("%s 〜 %s / 報告日 %s", js(card.draft, "from").c_str(), js(card.draft, "to").c_str(), js(card.draft, "date").c_str());
        if (pendingPeriod) ImGui::TextDisabled("期間を全員に適用すると、このカードの対象期間も更新されます");
        if (ImGui::BeginTable("member-card-columns", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV)) {
          ImGui::TableSetupColumn("入力", ImGuiTableColumnFlags_WidthStretch, 2.15f);
          ImGui::TableSetupColumn("履歴", ImGuiTableColumnFlags_WidthStretch, 1.0f);
          ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0);
          ImGui::BeginDisabled(c.readOnly());
          if (ImGui::SmallButton("タスクを更新")) refreshDraft(f, member, card, false);
          ImGui::SameLine();
          const bool saveDraft = ImGui::SmallButton("下書きを保存");
          ImGui::SameLine();
          ImGui::BeginDisabled(pendingPeriod || !card.error.empty());
          const bool saveSnapshot = ImGui::SmallButton("報告を確定・履歴に保存");
          ImGui::EndDisabled();
          bool changed = taskRows(f, card.draft, false);
          if (card.draft["tasks"].empty()) ImGui::TextDisabled("この期間の完了タスク・進行中タスクはありません");
          ImGui::SeparatorText("タスク外の仕事");
          changed |= taskRows(f, card.draft, true);
          if (ImGui::Button("仕事を追加")) { card.draft["extra"].push_back(Json{{"name", ""}, {"status", "In Progress"}, {"comment", ""}}); changed = true; }
          ImGui::SeparatorText("困りごと（任意）"); changed |= textField("##blockers", card.draft, "blockers", u.m.s(60));
          ImGui::SeparatorText("次の期間の予定（任意）"); changed |= textField("##next", card.draft, "next", u.m.s(60));
          ImGui::EndDisabled();
          if (changed) persistDraft(f, card);
          if (saveDraft && !c.readOnly()) { persistDraft(f, card); c.toast("下書きを保存しました"); }
          if (saveSnapshot && !c.readOnly()) {
            refreshDraft(f, member, card, false);
            persistDraft(f, card);
            auto save = personalFromJson(s->personal());
            auto id = savePersonalReport(save, card.draft, c.clock());
            if (id) {
              s->setPersonal(std::move(save)); c.afterEdit();
              card.savedId = *id; state.exportIds.insert(*id); c.toast("保存時点の報告を履歴に残しました");
            } else card.error = id.error();
          }
          ImGui::TableSetColumnIndex(1);
          memberHistoryPane(f, s->personal(), member, date.value_or(c.clock().today()));
          ImGui::EndTable();
        }
      }
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
  ImGui::EndChild();
}
}
