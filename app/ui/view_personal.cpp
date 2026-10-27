#include "ui/internal.hpp"
#include "roadmap/personal.hpp"
#include "roadmap/text.hpp"
#include <imgui_stdlib.h>
#include <algorithm>
#include <charconv>
#include <cmath>

namespace roadmap::ui {
namespace {
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
void persistDraft(Frame& f) {
  auto& state = f.u.personal;
  if (f.c.readOnly() || state.loadedKey.empty() || state.draft.empty()) return;
  auto data = personalFromJson(f.c.session()->personal());
  data["drafts"][state.loadedKey] = state.draft;
  f.c.session()->setPersonal(std::move(data));
  f.c.afterEdit();
}
void refreshDraft(Frame& f, bool restore) {
  auto& state = f.u.personal;
  Session& s = *f.c.session();
  const auto date = parseIso(state.date), from = parseIso(state.from), to = parseIso(state.to);
  if (!date || !from || !to) { state.error = "日付を YYYY-MM-DD で入力してください"; return; }
  const std::string key = personalDraftKey(state.member, *date);
  const auto data = personalFromJson(s.personal());
  if (restore && data["drafts"].contains(key) && data["drafts"][key].is_object()) {
    state.draft = data["drafts"][key];
    state.from = js(state.draft, "from"); state.to = js(state.draft, "to");
    state.baseline = js(state.draft, "baselineId");
  } else {
    const Baseline* baseline = nullptr;
    for (const auto& b : s.baselines().list) if (b.id == state.baseline) baseline = &b;
    const auto name = data["members"].contains(state.member) ? js(data["members"][state.member], "name") : "";
    auto draft = makePersonalDraft(data, s.issues(), state.member, *date, *from, *to, baseline, s.calendar(), f.c.weeklyMemberNotes(name));
    if (!draft) { state.error = draft.error(); return; }
    if (key == state.loadedKey && state.draft.is_object()) {
      for (const char* field : {"extra", "blockers", "next"}) if (state.draft.contains(field)) (*draft)[field] = state.draft[field];
      if (state.draft.contains("tasks") && state.draft["tasks"].is_array())
        for (auto& task : (*draft)["tasks"]) for (const auto& old : state.draft["tasks"])
          if (js(old, "id") == js(task, "id")) task["comment"] = js(old, "comment");
    }
    state.draft = std::move(*draft);
  }
  state.loadedKey = key; state.error.clear(); state.savedId.clear();
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
  if (ImGui::BeginTable(key, 6, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollX,
                        ImVec2(0, 0), m.s(700))) {
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
      const std::string commentKey = std::string(key) + std::to_string(i);
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
  std::stable_sort(out.begin(), out.end(), [](const Json* a, const Json* b) {
    if (js(*a, "date") != js(*b, "date")) return js(*a, "date") > js(*b, "date");
    return js(*a, "savedUtc") > js(*b, "savedUtc");
  });
  return out;
}
void personalHistory(Frame& f, const Json& data) {
  auto& u = f.u; auto& state = u.personal;
  std::size_t count = 0;
  for (auto it = data["members"].begin(); it != data["members"].end(); ++it) count = std::max(count, memberReports(data, it.key()).size());
  if (!count) { ImGui::TextDisabled("まだ保存された報告はありません。編集ページで報告を確定すると、ここに残ります。"); return; }
  ImGui::TextDisabled("左端は担当者、右はその人の報告を新しい順に表示します。横にスクロールして参照できます。");
  ImGui::BeginDisabled(state.historyOffset == 0);
  if (ImGui::Button("新しい報告へ")) state.historyOffset = std::max(0, state.historyOffset - 8);
  ImGui::EndDisabled(); ImGui::SameLine();
  ImGui::BeginDisabled(static_cast<std::size_t>(state.historyOffset + 8) >= count);
  if (ImGui::Button("さらに古い報告へ")) state.historyOffset += 8;
  ImGui::EndDisabled();
  const int slots = std::min(8, static_cast<int>(count) - state.historyOffset);
  if (ImGui::BeginTable("personal-history", slots + 1, ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY |
       ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg, ImGui::GetContentRegionAvail(), u.m.s(180 + slots * 270.f))) {
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
      if (enabled(*it) && ImGui::SmallButton("この人の報告を書く")) { state.member = it.key(); state.loadedKey.clear(); state.history = false; }
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
  auto data = personalFromJson(s->personal());
  syncPersonalMembers(data, s->issues());
  if (!c.readOnly() && data != s->personal()) { s->setPersonal(data); c.afterEdit(); }
  if (state.member.empty()) for (auto it = data["members"].begin(); it != data["members"].end(); ++it)
    if (enabled(*it)) { state.member = it.key(); break; }
  ImGui::BeginChild("personal-page", size);
  ImGui::SeparatorText("個人報告");
  if (ImGui::Button(state.history ? "報告を書く" : "保存した報告・履歴を見る")) { persistDraft(f); state.history = !state.history; }
  exportPanel(f, data);
  if (state.history) { personalHistory(f, data); ImGui::EndChild(); return; }
  if (ImGui::CollapsingHeader("報告対象者を管理")) {
    ImGui::BeginDisabled(c.readOnly());
    for (auto it = data["members"].begin(); it != data["members"].end(); ++it) {
      ImGui::PushID(it.key().c_str()); bool on = enabled(*it);
      if (ImGui::Checkbox("##enabled", &on)) { (*it)["enabled"] = on; s->setPersonal(data); c.afterEdit(); }
      ImGui::SameLine(); std::string name = js(*it, "name"); ImGui::SetNextItemWidth(u.m.s(230));
      ImGui::InputText("##name", &name);
      if (ImGui::IsItemDeactivatedAfterEdit()) {
        if (renamePersonalMember(data, it.key(), name)) { s->setPersonal(data); c.afterEdit(); } else c.toast("空欄や既存の担当者名には変更できません");
      }
      ImGui::PopID();
    }
    ImGui::SetNextItemWidth(u.m.s(230)); ImGui::InputTextWithHint("##new-member", "タスクを持たない人も追加できます", &state.newName);
    ImGui::SameLine(); if (ImGui::Button("追加") && !text::trim(state.newName).empty()) {
      addPersonalMember(data, state.newName); state.newName.clear(); s->setPersonal(data); c.afterEdit();
    }
    ImGui::EndDisabled();
  }
  const std::string name = data["members"].contains(state.member) ? js(data["members"][state.member], "name") : "対象者を選択";
  ImGui::SetNextItemWidth(u.m.s(200));
  if (ImGui::BeginCombo("対象者", name.c_str())) {
    for (auto it = data["members"].begin(); it != data["members"].end(); ++it) if (enabled(*it))
      if (ImGui::Selectable(js(*it, "name").c_str(), state.member == it.key())) {
        persistDraft(f); state.member = it.key(); state.loadedKey.clear();
      }
    ImGui::EndCombo();
  }
  if (state.member.empty()) { ImGui::TextWrapped("報告対象者がいません。「報告対象者を管理」で追加または選択してください。"); ImGui::EndChild(); return; }
  if (state.loadedKey.empty()) refreshDraft(f, true);
  for (auto pair : {std::pair<const char*, std::string*>{"報告日", &state.date}, {"開始日", &state.from}, {"終了日", &state.to}}) {
    ImGui::SetNextItemWidth(u.m.s(145)); ImGui::InputText(pair.first, pair.second);
  }
  if (ImGui::Button("報告日までの7日間")) {
    if (auto day = parseIso(state.date)) { state.from = fmtIso(*day - 6); state.to = state.date; }
  }
  ImGui::SameLine(); if (ImGui::Button("期間を適用 / 下書きを開く")) { persistDraft(f); refreshDraft(f, state.loadedKey != state.member + ":" + state.date); }
  ImGui::SetNextItemWidth(u.m.s(280));
  std::string baselineName = "ベースラインなし（予定率は比較しません）";
  for (const auto& b : s->baselines().list) if (b.id == state.baseline) baselineName = b.name;
  if (ImGui::BeginCombo("予定の基準", baselineName.c_str())) {
    if (ImGui::Selectable("比較しない", state.baseline.empty())) { state.baseline.clear(); refreshDraft(f, false); }
    for (const auto& b : s->baselines().list) if (ImGui::Selectable(b.name.c_str(), state.baseline == b.id)) { state.baseline = b.id; refreshDraft(f, false); }
    ImGui::EndCombo();
  }
  ImGui::TextDisabled("実績は任意入力。予定との差が±10ポイント以内は通常色、遅れは赤、先行は青。");
  if (!state.error.empty()) ImGui::TextWrapped("%s", state.error.c_str());
  if (state.draft.empty()) { ImGui::EndChild(); return; }
  ImGui::TextDisabled("表示中の期間: %s 〜 %s / 報告日 %s", js(state.draft, "from").c_str(), js(state.draft, "to").c_str(), js(state.draft, "date").c_str());
  ImGui::Checkbox("前回を表示", &state.previous);
  ImGui::SameLine(); ImGui::BeginDisabled(c.readOnly());
  if (ImGui::Button("タスクを更新")) refreshDraft(f, false);
  ImGui::SameLine(); if (ImGui::Button("下書きを保存")) { persistDraft(f); c.toast("下書きを保存しました"); }
  ImGui::SameLine(); if (ImGui::Button("報告を確定・履歴に保存")) {
    persistDraft(f); auto save = personalFromJson(s->personal());
    auto id = savePersonalReport(save, state.draft, c.clock());
    if (id) { s->setPersonal(std::move(save)); c.afterEdit(); state.savedId = *id; state.exportIds.insert(*id); c.toast("保存時点の報告を履歴に残しました"); } else state.error = id.error();
  }
  ImGui::EndDisabled();
  if (c.readOnly()) ImGui::TextDisabled("読み取り専用：閲覧・出力できます。下書きと実績は変更しません。");
  const bool side = state.previous && ImGui::GetContentRegionAvail().x > u.m.s(1050);
  const float right = side ? u.m.s(300) : 0;
  ImGui::BeginChild("personal-editor", ImVec2(ImGui::GetContentRegionAvail().x - right, 0));
  ImGui::BeginDisabled(c.readOnly());
  bool changed = taskRows(f, state.draft, false);
  if (state.draft["tasks"].empty()) ImGui::TextDisabled("この期間の完了タスク・進行中タスクはありません");
  ImGui::SeparatorText("タスク外の仕事");
  changed |= taskRows(f, state.draft, true);
  if (ImGui::Button("仕事を追加")) { state.draft["extra"].push_back(Json{{"name", ""}, {"status", "In Progress"}, {"comment", ""}}); changed = true; }
  ImGui::SeparatorText("困りごと（任意）"); changed |= textField("##blockers", state.draft, "blockers", u.m.s(70));
  ImGui::SeparatorText("次の期間の予定（任意）"); changed |= textField("##next", state.draft, "next", u.m.s(70));
  ImGui::EndDisabled();
  if (changed) persistDraft(f);
  if (!side && state.previous && ImGui::CollapsingHeader("前回の報告")) {
    const auto* previous = previousPersonalReport(s->personal(), state.member, parseIso(js(state.draft, "date")).value_or(c.clock().today()));
    if (previous) snapshotBody(f, *previous); else ImGui::TextDisabled("前回の報告はありません");
  }
  ImGui::EndChild();
  if (side) {
    ImGui::SameLine(); ImGui::BeginChild("personal-previous", ImVec2(0, 0), ImGuiChildFlags_Border);
    ImGui::SeparatorText("前回の報告");
    const auto* previous = previousPersonalReport(s->personal(), state.member, parseIso(js(state.draft, "date")).value_or(c.clock().today()));
    if (previous) snapshotBody(f, *previous); else ImGui::TextDisabled("前回の報告はありません");
    ImGui::EndChild();
  }
  ImGui::EndChild();
}
}
