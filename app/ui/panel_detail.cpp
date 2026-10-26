// 詳細パネル（SPEC 10.9）: 詳細 / 依存関係 / コメント
#include <algorithm>
#include <functional>
#include <unordered_map>

#include <imgui_internal.h>
#include <imgui.h>
#include <imgui_stdlib.h>

#include "roadmap/link_candidates.hpp"
#include "roadmap/other_links.hpp"
#include "roadmap/text.hpp"
#include "ui/icons.hpp"
#include "ui/internal.hpp"

namespace roadmap::ui {

namespace {

// 入力欄: フォーカスが外れたとき・Enter で確定、Esc で取り消し（変更がなければ何もしない）。
// 編集中に別の課題へ移ったときは、編集を始めた課題に確定する（pending）。
struct Pending {
  std::string issueId;
  std::string value, original;
  std::function<void(const std::string&)> apply;
  bool active = false;
} gPending;
std::unordered_map<ImGuiID, std::string> gBufs;

void flushPending(const std::string& currentIssue, bool force) {
  if (!gPending.active || (!force && gPending.issueId == currentIssue)) return;
  Pending p = std::move(gPending);
  gPending = {};
  if (p.value != p.original && p.apply) p.apply(p.value);
}

void field(Frame& f, const std::string& issueId, const char* key, const std::string& current, float w,
           std::function<void(const std::string&)> apply, bool multiline = false, const char* hint = nullptr) {
  UiState& u = f.u;
  ImGui::PushID(key);
  const ImGuiID gid = ImGui::GetID("##f");
  std::string& buf = gBufs[gid];
  const bool editingThis = ImGui::GetActiveID() == gid;
  if (!editingThis) buf = current;
  ImGui::SetNextItemWidth(w);
  if (multiline) ImGui::InputTextMultiline("##f", &buf, ImVec2(w, u.m.s(110)));
  else if (hint) ImGui::InputTextWithHint("##f", hint, &buf);
  else ImGui::InputText("##f", &buf);
  const bool active = ImGui::IsItemActive();
  bool commit = false;
  if (active) gPending = {issueId, buf, current, apply, true};
  if (active && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
    buf = current;
    gPending = {};
    ImGui::ClearActiveID();
  } else if (!multiline && active && enterPressed(u)) {
    commit = true;
    ImGui::ClearActiveID();
  } else if (ImGui::IsItemDeactivatedAfterEdit()) {
    commit = true;
  } else if (ImGui::IsItemDeactivated()) {
    gPending = {};
  }
  if (commit) {
    const std::string v = buf;
    gPending = {};
    if (v != current) apply(v);
  }
  ImGui::PopID();
}

void issuePill(Frame& f, int id, const std::function<void()>& onRemove) {
  UiState& u = f.u;
  Session* s = f.c.session();
  const Tree& T = *s->tree();
  ImGui::PushID(id);
  typeIcon(u, s->viewIssues()[id].type);
  ImGui::SameLine();
  const std::string l = T.did[id] + "  " + T.name[id];
  if (ImGui::Selectable(ellipsize(l, ImGui::GetContentRegionAvail().x - u.m.s(36)).c_str(), false, 0,
                        ImVec2(ImGui::GetContentRegionAvail().x - u.m.s(32), 0)))
    f.c.select(T.ids[id]);  // 課題をたどる操作は、読み取り専用でも使える
  u.issuePills.push_back({ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), T.ids[id]});
  ImGui::SameLine();
  if (iconButton(u, "rm", "x", "外す", !f.c.readOnly())) onRemove();
  ImGui::PopID();
}

// 後で呼ばれても安全なように、プロジェクトと課題を id で引き直してから適用する
std::function<void(const std::string&)> applier(Frame& f, const std::string& id,
                                                std::function<void(app::AppController&, Session&, const std::string&, const std::string&)> fn) {
  app::AppController* cp = &f.c;
  const std::string proj = f.c.currentId();
  return [cp, proj, id, fn](const std::string& v) {
    Session* s = cp->session();
    if (!s || cp->currentId() != proj || !s->issue(id)) return;
    fn(*cp, *s, id, v);
    cp->afterEdit();
  };
}

void detailTab(Frame& f, int i) {
  app::AppController& c = f.c;
  UiState& u = f.u;
  Session* s = c.session();
  const Tree& T = *s->tree();
  const auto& R = *s->rows();
  const Issue& o = s->viewIssues()[i];
  const ScheduleRow& r = R[i];
  const std::string id = o.id;
  const bool leaf = T.isLeaf(i);
  const float W = ImGui::GetContentRegionAvail().x;
  using AC = app::AppController;
  // パンくず（祖先のパス。押すとその課題へ移動。長いときは真ん中を「…」にして、マウスを載せると全体を出す）
  {
    std::vector<int> anc;
    for (int a = T.parentOf[i], guard = 0; a >= 0 && guard < T.n; a = T.parentOf[a], ++guard) anc.insert(anc.begin(), a);
    std::vector<std::string> names;
    for (int a : anc) names.push_back(ellipsize(T.name[a], u.m.s(120)));
    // 1つ分の余白（小ボタンの内側の余白・区切り）を足して幅を測る
    const float perItem = ImGui::GetStyle().FramePadding.x * 2 + u.m.s(4) + ImGui::CalcTextSize("›").x;
    const auto kept = text::ancestorPath(
        names,
        [&](const std::string& t) {
          int parts = 1;
          for (std::size_t p = t.find(" › "); p != std::string::npos; p = t.find(" › ", p + 1)) ++parts;
          return ImGui::CalcTextSize(t.c_str()).x - ImGui::CalcTextSize(" › ").x * static_cast<float>(parts - 1) +
                 perItem * static_cast<float>(parts);
        },
        W);
    for (std::size_t k = 0; k < kept.size(); ++k) {
      if (k) {
        ImGui::SameLine(0, u.m.s(2));
        ImGui::TextDisabled("›");
        ImGui::SameLine(0, u.m.s(2));
      }
      if (kept[k] < 0) {
        ImGui::TextDisabled("…");
        if (ImGui::IsItemHovered()) {
          std::vector<std::string> full;
          for (int a : anc) full.push_back(T.name[a]);
          ImGui::SetTooltip("%s", text::join(full, " › ").c_str());
        }
        continue;
      }
      const int a = anc[static_cast<std::size_t>(kept[k])];
      ImGui::PushID(a);
      if (ImGui::SmallButton(names[static_cast<std::size_t>(kept[k])].c_str())) c.select(T.ids[a]);
      ImGui::PopID();
    }
  }
  ImGui::BeginDisabled(c.readOnly());  // 読み取り専用では、項目を直せない（パンくずでの移動と閲覧はできる）
  ImGui::PushFont(u.fontLarge);
  field(f, id, "name", T.name[i], W, applier(f, id, [](AC&, Session& ss, const std::string& k, const std::string& v) {
          if (!text::trim(v).empty()) ss.setName(k, text::trim(v));
        }));
  ImGui::PopFont();
  ImGui::Spacing();
  ImGui::SeparatorText("基本");
  const float half = (W - u.m.s(12)) / 2;
  const auto label = [&](const char* l) { ImGui::TextDisabled("%s", l); };
  ImGui::BeginGroup();
  label("番号（表示用ID）");
  field(f, id, "tag", tagOf(o.summary).tag, half,
        applier(f, id, [](AC&, Session& ss, const std::string& k, const std::string& v) { ss.setTag(k, v); }), false,
        (o.key.empty() ? o.id : o.key).c_str());
  label("担当者");
  if (leaf) {
    const float pickW = u.m.s(28);
    ImGui::PushID("assignee-picker");
    field(f, id, "asg", o.assignee, std::max(1.f, half - pickW - ImGui::GetStyle().ItemSpacing.x),
          applier(f, id, [](AC&, Session& ss, const std::string& k, const std::string& v) {
            ss.setField(k, [&](Issue& x) { x.assignee = text::trim(v); });
          }));
    ImGui::SameLine();
    if (iconButton(u, "open", "down", "担当者候補から選ぶ", !s->assignees().empty())) ImGui::OpenPopup("##assignees");
    if (ImGui::BeginPopup("##assignees")) {
      for (const std::string& a : s->assignees()) {
        if (ImGui::Selectable(a.c_str(), a == o.assignee))
          s->setField(id, [&](Issue& x) { x.assignee = a; }), c.afterEdit();
      }
      ImGui::EndPopup();
    }
    ImGui::PopID();
  } else {
    ImGui::TextDisabled("—");
  }
  label("種別");
  ImGui::SetNextItemWidth(half);
  if (ImGui::BeginCombo("##type", o.type.c_str())) {
    for (const char* t : {"Epic", "Story", "Task", "Sub-task", "Bug", "Milestone"})
      if (ImGui::Selectable(t, o.type == t)) {
        s->setField(id, [&](Issue& x) { x.type = t; });
        c.afterEdit();
      }
    ImGui::EndCombo();
  }
  ImGui::EndGroup();
  ImGui::SameLine(0, u.m.s(12));
  ImGui::BeginGroup();
  label("状況");
  ImGui::SetNextItemWidth(half);
  if (leaf) {
    if (ImGui::BeginCombo("##status", o.status.empty() ? "To Do" : o.status.c_str())) {
      for (const char* st : {S::kStatusTodo, S::kStatusProg, S::kStatusDone})
        if (ImGui::Selectable(st, o.status == st)) c.setStatusFor({id}, st);
      ImGui::EndCombo();
    }
  } else {
    ImGui::TextDisabled("%d/%d 完了", r.nDone, r.n);
  }
  label("優先度");
  ImGui::SetNextItemWidth(half);
  if (ImGui::BeginCombo("##prio", o.prio.empty() ? "Medium" : o.prio.c_str())) {
    for (const char* p : kPriorities)
      if (ImGui::Selectable(p, o.prio == p)) c.setPrioFor({id}, p);
    ImGui::EndCombo();
  }
  label("ラベル");
  field(f, id, "labels", text::join(o.labels, ", "), half,
        applier(f, id, [](AC&, Session& ss, const std::string& k, const std::string& v) {
          std::vector<std::string> ls;
          for (auto& t : text::splitAny(v, U",、", false))
            if (auto tt = text::trim(t); !tt.empty()) ls.push_back(tt);
          ss.setField(k, [&](Issue& x) { x.labels = ls; });
        }));
  ImGui::EndGroup();
  label("親（表示ID・key・件名の一部。空欄 = 最上位）");
  field(f, id, "parent", T.parentOf[i] >= 0 ? T.did[T.parentOf[i]] : "", W,
        applier(f, id, [](AC& cc, Session& ss, const std::string& k, const std::string& raw) {
          const std::string v = text::trim(raw);
          if (v.empty()) {
            ss.reparent(k, "");
          } else if (auto ref = ss.resolveRef(v)) {
            const auto ds = ss.descendantsOf(k);
            if (*ref == k || std::find(ds.begin(), ds.end(), *ref) != ds.end()) cc.toast("自分の配下は親にできません");
            else ss.reparent(k, *ref);
          } else {
            cc.toast("「" + v + "」が見つかりません");
          }
        }));
  ImGui::SeparatorText("日程");
  const bool std0 = s->settings().scenario == kStdScenario;
  if (leaf) {
    bool durationDays = c.uiStr("detailDurationUnit", "weeks") == "days";
    std::string h = "期間";
    if (!std0) h += " @" + s->settings().scenario;
    label(h.c_str());
    ImGui::SameLine();
    ImGui::SetNextItemWidth(std::max(u.m.s(78), std::min(u.m.s(100), half - u.m.s(35))));
    if (ImGui::BeginCombo("##duration-unit", durationDays ? "営業日" : "週")) {
      if (ImGui::Selectable("週", !durationDays)) c.setUi("detailDurationUnit", "weeks");
      if (ImGui::Selectable("営業日", durationDays)) c.setUi("detailDurationUnit", "days");
      ImGui::EndCombo();
    }
    const double durationValue = durationDays ? r.weeks * 5.0 : r.weeks;
    if (T.kind[i] == Kind::Ms && r.weeks == 0) {
      ImGui::TextUnformatted("◆ マイルストーン（期間0）");
    } else if (!r.done) {
      const double step = durationDays ? 0.2 : 1.0;
      const char* unit = durationDays ? "営業日" : "週";
      if (iconButton(u, "dmin", "minus", (std::string("1") + unit + "減らす").c_str())) {
        s->setDuration(id, std::max(0.0, r.weeks - step));
        c.afterEdit();
      }
      ImGui::SameLine();
      field(f, id, "dur", csv::fmtNum(durationValue), std::max(u.m.s(40), half - u.m.s(72)),
            applier(f, id, [durationDays](AC&, Session& ss, const std::string& k, const std::string& v) {
              if (auto d = csv::parseDur(v, true); d && *d >= 0) {
                ss.setDuration(k, durationDays ? *d / 5.0 : text::jsRound(*d * 2) / 2);
              }
            }));
      ImGui::SameLine();
      if (iconButton(u, "dplus", "plus", (std::string("1") + unit + "増やす").c_str())) {
        s->setDuration(id, r.weeks + step);
        c.afterEdit();
      }
      if (!std0 && o.alt.find(s->settings().scenario)) {
        ImGui::SameLine();
        if (ImGui::SmallButton("標準に戻す")) {
          s->removeScenarioValue(id);
          c.afterEdit();
        }
      }
    } else {
      ImGui::TextUnformatted((csv::fmtNum(durationValue) + (durationDays ? "営業日" : "週")).c_str());
    }
    label("日程での扱い");
    ImGui::SameLine();
    const bool milestone = T.kind[i] == Kind::Ms;
    ImGui::SetNextItemWidth(std::max(u.m.s(80), std::min(u.m.s(150), half - u.m.s(35))));
    if (ImGui::BeginCombo("##task-kind", milestone ? "◆ マイルストーン" : "作業（タスク）")) {
      if (ImGui::Selectable("タスクにする", !milestone)) {
        s->setField(id, [&](Issue& x) {
          x.type = "Task";
          x.labels.erase(std::remove_if(x.labels.begin(), x.labels.end(), [](const std::string& l) {
            return text::icontains(l, "milestone") || l.find("マイルストーン") != std::string::npos;
          }), x.labels.end());
          if (std0) {
            if (!x.durW || *x.durW == 0) x.durW = 1.0;
          } else if (const double* w = x.alt.find(s->settings().scenario); !w || *w == 0) {
            x.alt.set(s->settings().scenario, 1.0);
          }
        });
        c.afterEdit();
      }
      if (ImGui::Selectable("マイルストーンにする", milestone)) {
        s->setField(id, [&](Issue& x) {
          x.type = "Milestone";
          x.durW = 0.0;
          for (const auto& [scenario, _] : x.alt.items()) x.alt.set(scenario, 0.0);
        });
        c.afterEdit();
      }
      ImGui::EndCombo();
    }
  } else {
    ImGui::TextDisabled("配下の %d 件から集計（完了 %d）", r.n, r.nDone);
  }
  const auto dateField = [&](const char* key, const char* l, const OptDay& cur,
                             std::function<void(Session&, const std::string&, OptDay)> fn) {
    label(l);
    const float pickerW = u.m.s(28);
    ImGui::PushID(key);
    field(f, id, key, fmtIso(cur), std::max(1.f, half - pickerW - ImGui::GetStyle().ItemSpacing.x),
          applier(f, id, [fn](AC& cc, Session& ss, const std::string& k, const std::string& raw) {
            const std::string v = text::trim(raw);
            if (v.empty()) fn(ss, k, std::nullopt);
            else if (auto d = csv::parseDate(v)) fn(ss, k, *d);
            else cc.toast("日付として読めません: " + v);
          }),
          false, "YYYY-MM-DD");
    ImGui::SameLine();
    if (iconButton(u, "calendar", "calendar", "カレンダーから選ぶ")) {
      const Day initial = cur.value_or(c.clock().today());
      const Ymd ym = civilFromDays(initial);
      ImGui::GetStateStorage()->SetInt(ImGui::GetID("##calendar-month"), ym.y * 12 + ym.m - 1);
      ImGui::OpenPopup("##calendar-popup");
    }
    if (ImGui::BeginPopup("##calendar-popup")) {
      const ImGuiID monthId = ImGui::GetID("##calendar-month");
      const Day today = c.clock().today();
      const Ymd todayYmd = civilFromDays(today);
      int monthIndex = ImGui::GetStateStorage()->GetInt(monthId, todayYmd.y * 12 + todayYmd.m - 1);
      int year = monthIndex / 12;
      int month = monthIndex % 12 + 1;
      if (ImGui::SmallButton("<")) {
        if (--month <= 0) month = 12, --year;
        ImGui::GetStateStorage()->SetInt(monthId, year * 12 + month - 1);
      }
      ImGui::SameLine();
      ImGui::Text("%d年%d月", year, month);
      ImGui::SameLine();
      if (ImGui::SmallButton(">")) {
        if (++month > 12) month = 1, ++year;
        ImGui::GetStateStorage()->SetInt(monthId, year * 12 + month - 1);
      }
      if (ImGui::BeginTable("##calendar-days", 7, ImGuiTableFlags_SizingStretchSame)) {
        for (const char* wd : {"日", "月", "火", "水", "木", "金", "土"}) {
          ImGui::TableNextColumn();
          ImGui::TextDisabled("%s", wd);
        }
        const Day first = *makeDay(year, month, 1);
        const int offset = dow(first);
        for (int cell = 0; cell < 42; ++cell) {
          ImGui::TableNextColumn();
          const Day d = first + cell - offset;
          const Ymd dayYmd = civilFromDays(d);
          const std::string dayText = std::to_string(dayYmd.d);
          const bool selected = cur && *cur == d;
          ImGui::PushID(cell);
          if (dayYmd.m != month) ImGui::PushStyleColor(ImGuiCol_Text, u.t->muted);
          if (ImGui::Selectable(dayText.c_str(), selected || d == today, ImGuiSelectableFlags_DontClosePopups,
                                ImVec2(-1, u.m.s(24)))) {
            fn(*s, id, d);
            c.afterEdit();
            ImGui::CloseCurrentPopup();
          }
          if (dayYmd.m != month) ImGui::PopStyleColor();
          ImGui::PopID();
        }
        ImGui::EndTable();
      }
      if (ImGui::SmallButton("今日")) {
        fn(*s, id, today);
        c.afterEdit();
        ImGui::CloseCurrentPopup();
      }
      if (cur) {
        ImGui::SameLine();
        if (ImGui::SmallButton("日付を消す")) {
          fn(*s, id, std::nullopt);
          c.afterEdit();
          ImGui::CloseCurrentPopup();
        }
      }
      ImGui::EndPopup();
    }
    ImGui::PopID();
  };
  ImGui::BeginGroup();
  dateField("dl", "期限", T.ownDl[i], [](Session& ss, const std::string& k, OptDay d) { ss.setDeadline(k, d); });
  if (leaf && !r.done && !r.pinned)
    dateField("nb", "着手可能日", o.notBefore, [](Session& ss, const std::string& k, OptDay d) { ss.setEarliest(k, d); });
  ImGui::EndGroup();
  ImGui::SameLine(0, u.m.s(12));
  ImGui::BeginGroup();
  if (leaf && T.cat[i] == Cat::Prog) {
    label("残り（週）");
    field(f, id, "rem", o.remW ? csv::fmtNum(*o.remW) : "", half,
          applier(f, id, [](AC&, Session& ss, const std::string& k, const std::string& v) {
            const auto d = csv::parseDur(v, true);
            ss.setField(k, [&](Issue& x) { x.remW = d; });
          }));
  }
  if (leaf && T.cat[i] != Cat::Todo)
    dateField("start", "実績開始", o.start,
              [](Session& ss, const std::string& k, OptDay d) { ss.setField(k, [&](Issue& x) { x.start = d; }); });
  if (leaf && T.cat[i] == Cat::Done)
    dateField("res", "完了日", o.resolved,
              [](Session& ss, const std::string& k, OptDay d) { ss.setField(k, [&](Issue& x) { x.resolved = d; }); });
  ImGui::EndGroup();
  // 計画の要約
  ImGui::Spacing();
  ImGui::TextDisabled("計画（最早）");
  ImGui::SameLine(u.m.s(110));
  ImGui::TextUnformatted((fmtLong(r.asapStart) + " 〜 " + fmtLong(r.asapFinish)).c_str());
  ImGui::TextDisabled("最遅");
  ImGui::SameLine(u.m.s(110));
  ImGui::TextUnformatted(r.done ? "—" : (fmtLong(r.alapStart) + " 〜 " + fmtLong(r.alapFinish)).c_str());
  ImGui::TextDisabled("余裕");
  ImGui::SameLine(u.m.s(110));
  floatChip(u, r);
  ImGui::SameLine();
  ImGui::TextUnformatted((std::to_string(r.floatDays) + "営業日 " + r.status).c_str());
  // 注記
  ImGui::PushTextWrapPos(0);
  if (r.waited)
    ImGui::TextColored(v4(u.t->tight), "担当者「%s」の別の作業が終わるのを待っています。先行だけなら %s から始められます。",
                       text::trim(o.assignee).c_str(), fmtLong(r.freeStart).c_str());
  if (leaf && o.notBefore && r.asapStart > *o.notBefore)
    ImGui::TextColored(v4(u.t->muted), "着手可能日の指定（%s）より後になります。先行や担当者の都合です。", fmtLong(*o.notBefore).c_str());
  if (!leaf) ImGui::TextDisabled("子があるため、期間と状況は配下から集計されます。期間を変えるときは配下の課題を編集してください。");
  ImGui::PopTextWrapPos();
  ImGui::SeparatorText("説明");
  field(f, id, "desc", o.desc, W,
        applier(f, id, [](AC&, Session& ss, const std::string& k, const std::string& v) {
          ss.setField(k, [&](Issue& x) { x.desc = text::trimEnd(v); });
        }),
        true);
  ImGui::TextDisabled("%s（%d階層目） ／ Issue Id %s%s", generationName(T.depth[i]).c_str(), T.depth[i] + 1, o.id.c_str(),
                      o.key.empty() ? "" : (" ／ " + o.key).c_str());
  ImGui::EndDisabled();
}

// 依存の相手の入力欄: 入力すると候補（表示ID・件名の部分一致）が出て、クリックか ↑↓ + Enter で選ぶ。
// 「画面で選ぶ」で一覧・ガント・ボードの行をクリックして選ぶこともできる。
struct CandState {
  bool open = false;
  int sel = -1;  // ↑↓ で選んだ候補（-1 = 選んでいない）
  std::string lastQuery;
  ImVec2 rectMin{0, 0}, rectMax{0, 0};  // 前のフレームの候補一覧の範囲（画面の座標）
} gCand[2];

void linkAdder(Frame& f, int i, LinkDir dir, std::string& buf) {
  app::AppController& c = f.c;
  UiState& u = f.u;
  Session* s = c.session();
  const Tree& T = *s->tree();
  const auto& I = s->viewIssues();
  const std::string id = I[i].id;
  const bool addPred = dir == LinkDir::AddPred;
  CandState& st = gCand[addPred ? 0 : 1];
  ImGui::PushID(addPred ? "addp" : "adds");
  const float btnW = ImGui::CalcTextSize("画面で選ぶ").x + u.m.s(20);
  ImGui::SetNextItemWidth(-(btnW + ImGui::GetStyle().ItemSpacing.x));
  ImGui::InputTextWithHint("##in", "表示ID・件名を入力して Enter", &buf);
  const bool active = ImGui::IsItemActive();
  const bool focused = ImGui::IsItemFocused();
  ImGui::SameLine();
  if (ImGui::Button("画面で選ぶ", ImVec2(btnW, 0))) {
    beginLinkPick(f, id, !addPred);  // 先行を足す = 自分が後続（起点）
    st.open = false;
  }
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("一覧・ガント・ボードの行（カード・バー）をクリックして選びます。Esc で中止");
  // 候補を出すのは、入力欄に入っている間と、候補をクリックする瞬間（入力欄は外れる）
  const bool overList = st.open && ImGui::IsMouseHoveringRect(st.rectMin, st.rectMax, false);
  // Enter を押したフレームは、入力欄はもう（Enter で）アクティブではないが、フォーカスは残っている
  const bool enter = focused && enterPressed(u) && (!text::trim(buf).empty() || st.sel >= 0);
  const bool show = active || overList || enter;
  st.open = show;
  if (!show) {
    st.sel = -1;
    ImGui::PopID();
    return;
  }
  if (buf != st.lastQuery) {
    st.lastQuery = buf;
    st.sel = -1;
  }
  const auto cands = linkCandidates(T, I, i, dir, buf, recentLinks(f), 50);
  // ↑↓ で、つなげられる候補だけを順にたどる（先頭の上は「選んでいない」）
  bool moved = false;
  if (active) {
    const auto step = [&](int d) {
      int k = st.sel;
      for (;;) {
        k += d;
        if (k < -1 || k >= static_cast<int>(cands.size())) return;
        if (k == -1 || cands[static_cast<std::size_t>(k)].block == LinkBlock::None) break;
      }
      st.sel = k;
      moved = true;
    };
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) step(1);
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) step(-1);
  }
  const auto done = [&] {
    buf.clear();
    st.sel = -1;
    st.open = false;
    st.lastQuery.clear();
  };
  // Enter: ↑↓ で選んだ候補、なければ入力を表示ID・件名として探す（今までどおり）
  if (enter) {
    if (st.sel >= 0 && st.sel < static_cast<int>(cands.size())) {
      if (linkIssues(f, id, !addPred, T.ids[cands[static_cast<std::size_t>(st.sel)].idx], false)) done();
    } else if (auto ref = s->resolveRef(buf)) {
      if (linkIssues(f, id, !addPred, *ref, false)) done();
    } else {
      c.toast("「" + text::trim(buf) + "」が見つかりません");
    }
    ImGui::PopID();
    return;
  }
  // 候補の一覧（入力欄の下に出す）
  const float lh = ImGui::GetTextLineHeightWithSpacing();
  const int rows = std::max(1, std::min<int>(static_cast<int>(cands.size()), 8));
  ImGui::PushStyleColor(ImGuiCol_ChildBg, v4(u.t->surface));
  ImGui::BeginChild("##cand", ImVec2(-1, static_cast<float>(rows) * lh + u.m.s(12)), ImGuiChildFlags_Border);
  ImGui::PopStyleColor();
  const float availW = ImGui::GetContentRegionAvail().x;
  int clicked = -1;
  for (std::size_t r = 0; r < cands.size(); ++r) {
    const LinkCandidate& cd = cands[r];
    const bool ok = cd.block == LinkBlock::None;
    const std::string tag = !ok ? linkBlockReason(cd.block) : cd.recent ? "最近" : cd.sibling ? "同じ階層" : "";
    const float tagW = tag.empty() ? 0.f : ImGui::CalcTextSize(tag.c_str()).x + u.m.s(8);
    ImGui::PushID(static_cast<int>(r));
    const bool isSel = static_cast<int>(r) == st.sel;
    if (ImGui::Selectable(ellipsize(T.did[cd.idx] + "  " + T.name[cd.idx], availW - tagW - u.m.s(12)).c_str(), isSel,
                          ok ? 0 : ImGuiSelectableFlags_Disabled))
      clicked = static_cast<int>(r);
    if (isSel && moved) ImGui::SetScrollHereY();
    if (!ok && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", tag.c_str());
    if (!tag.empty()) {
      ImGui::SameLine(availW - tagW);
      ImGui::TextDisabled("%s", tag.c_str());
    }
    ImGui::PopID();
  }
  if (cands.empty()) ImGui::TextDisabled("%s", text::trim(buf).empty() ? "候補がありません" : "該当する課題がありません");
  st.rectMin = ImGui::GetWindowPos();
  st.rectMax = ImVec2(st.rectMin.x + ImGui::GetWindowSize().x, st.rectMin.y + ImGui::GetWindowSize().y);
  ImGui::EndChild();
  if (clicked >= 0 && linkIssues(f, id, !addPred, T.ids[cands[static_cast<std::size_t>(clicked)].idx], false)) done();
  ImGui::PopID();
}

// Blocks 以外の JIRA リンク（Relates・Duplicate など）。日程には使わない。値は CSV の「未知の列」に入っていて、
// 書き出しでは元の列名（Outward issue link (Relates) など）のまま戻る（Issue #12）
struct OtherLinkForm {
  std::string kind = "Relates";
  std::string ref;
  bool outward = true;
} gOtherForm;

void otherLinksSection(Frame& f, int i) {
  app::AppController& c = f.c;
  UiState& u = f.u;
  Session* s = c.session();
  const Tree& T = *s->tree();
  const auto& I = s->viewIssues();
  const std::string id = I[i].id;
  OtherLinkForm& form = gOtherForm;
  ImGui::SeparatorText("関連リンク（日程には影響しません）");
  const auto links = otherLinksOf(s->meta(), I, static_cast<std::size_t>(i));
  // 種類と向きごとにまとめる（最初に出てきた順）
  struct Group {
    std::string kind;
    bool outward;
    std::vector<std::size_t> items;
  };
  std::vector<Group> groups;
  for (std::size_t k = 0; k < links.size(); ++k) {
    auto it = std::find_if(groups.begin(), groups.end(), [&](const Group& g) {
      return g.outward == links[k].outward && text::toLower(g.kind) == text::toLower(links[k].kind);
    });
    if (it == groups.end()) {
      groups.push_back({links[k].kind, links[k].outward, {}});
      it = groups.end() - 1;
    }
    it->items.push_back(k);
  }
  if (groups.empty()) {
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("関連リンクはありません。JIRA の Relates・Duplicate などのリンクを、日程とは別に控えておけます。");
    ImGui::PopTextWrapPos();
  }
  for (const Group& g : groups) {
    ImGui::TextDisabled("%s（%s）", g.kind.empty() ? "リンク" : g.kind.c_str(), g.outward ? "外向き" : "内向き");
    for (std::size_t k : g.items) {
      const OtherLink& l = links[k];
      const std::size_t col = l.col;
      ImGui::PushID(static_cast<int>(col));
      const auto remove = [&f, id, col] {
        deferAfterFrame(f, [&f, id, col] {
          Session* ss = f.c.session();
          if (!ss || !ss->issue(id)) return;
          ss->removeOtherLink(id, col);
          f.c.afterEdit();
        });
      };
      if (l.target >= 0 && l.target < T.n) {
        issuePill(f, l.target, remove);
      } else {
        // 相手がこのプロジェクトにないとき（JIRA 側だけにある課題など）は文字のまま
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(l.ref.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("（このプロジェクトにない課題）");
        ImGui::SameLine();
        if (iconButton(u, "rm", "x", "外す", !c.readOnly())) remove();
      }
      ImGui::PopID();
    }
  }
  // 追加
  ImGui::PushID("otherlink");
  ImGui::BeginDisabled(c.readOnly());  // 読み取り専用では、リンクを足せない
  ImGui::Spacing();
  const float spacing = ImGui::GetStyle().ItemSpacing.x;
  const float pickW = ImGui::CalcTextSize("候補").x + u.m.s(20);
  ImGui::SetNextItemWidth(std::max(u.m.s(80), ImGui::GetContentRegionAvail().x * 0.5f - pickW - spacing));
  ImGui::InputTextWithHint("##kind", "種類（例: Relates）", &form.kind);
  ImGui::SameLine();
  if (ImGui::Button("候補", ImVec2(pickW, 0))) ImGui::OpenPopup("##kinds");
  if (ImGui::BeginPopup("##kinds")) {
    std::vector<std::string> kinds = otherLinkKinds(s->meta());
    for (const char* d : {"Relates", "Duplicate", "Cloners"})
      if (std::none_of(kinds.begin(), kinds.end(), [&](const std::string& k) { return text::iequals(k, d); }))
        kinds.push_back(d);
    for (const auto& k : kinds)
      if (ImGui::Selectable(k.c_str())) form.kind = k;
    ImGui::EndPopup();
  }
  ImGui::SameLine();
  if (ImGui::RadioButton("外向き", form.outward)) form.outward = true;
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("この課題から相手へのリンク（JIRA の Outward issue link 列）");
  ImGui::SameLine();
  if (ImGui::RadioButton("内向き", !form.outward)) form.outward = false;
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("相手からこの課題へのリンク（JIRA の Inward issue link 列）");
  const float addW = ImGui::CalcTextSize("追加").x + u.m.s(20);
  ImGui::SetNextItemWidth(-(addW + spacing));
  const bool enter = ImGui::InputTextWithHint("##ref", "相手の表示ID・キー・件名（JIRA 側だけの課題はキーを入力）", &form.ref,
                                              ImGuiInputTextFlags_EnterReturnsTrue);
  ImGui::SameLine();
  const bool click = ImGui::Button("追加", ImVec2(addW, 0));
  if ((enter || click) && !text::trim(form.ref).empty()) {
    deferAfterFrame(f, [&f, id] {
      Session* ss = f.c.session();
      if (!ss || !ss->issue(id)) return;
      const std::string ref = text::trim(gOtherForm.ref);
      // プロジェクトの課題に当たれば、JIRA の CSV と同じくキー（なければ Issue Id）で持つ
      std::string stored = ref;
      if (const auto rid = ss->resolveRef(ref))
        if (const Issue* t = ss->issue(*rid)) stored = t->key.empty() ? t->id : t->key;
      if (ss->addOtherLink(id, gOtherForm.kind, gOtherForm.outward, stored)) gOtherForm.ref.clear();
      f.c.afterEdit();
    });
  }
  ImGui::EndDisabled();
  ImGui::PushTextWrapPos(0);
  ImGui::TextDisabled("ここに入れたリンクは日程の計算に使いません。CSV には元の列名（Outward issue link (Relates) など）で書き出します。");
  ImGui::PopTextWrapPos();
  ImGui::PopID();
}

void linksTab(Frame& f, int i) {
  app::AppController& c = f.c;
  Session* s = c.session();
  const Tree& T = *s->tree();
  const auto& I = s->viewIssues();
  const std::string id = I[i].id;
  static std::string addPred, addSucc;
  // 直接の先行 = この課題を blocks に持つ課題
  std::vector<int> preds, succs;
  for (int k = 0; k < T.n; ++k)
    if (std::find(I[k].blocks.begin(), I[k].blocks.end(), id) != I[k].blocks.end()) preds.push_back(k);
  for (const auto& b : I[i].blocks)
    if (int k = T.find(b); k >= 0) succs.push_back(k);
  ImGui::SeparatorText("先行（これが終わってから始める）");
  for (int k : preds) issuePill(f, k, [&, k] {
    s->removeLink(I[k].id, id);
    c.afterEdit();
  });
  ImGui::BeginDisabled(c.readOnly());  // 読み取り専用では、つなぎを足せない
  linkAdder(f, i, LinkDir::AddPred, addPred);
  ImGui::EndDisabled();
  ImGui::SeparatorText("後続（これが終わるのを待つ）");
  for (int k : succs) issuePill(f, k, [&, k] {
    s->removeLink(id, I[k].id);
    c.afterEdit();
  });
  ImGui::BeginDisabled(c.readOnly());  // 読み取り専用では、つなぎを足せない
  linkAdder(f, i, LinkDir::AddSucc, addSucc);
  ImGui::EndDisabled();
  // 親の依存を含む実効の先行が直接の先行と違うとき
  if (T.isLeaf(i)) {
    std::vector<int> eff = T.preds[i];
    bool differs = eff.size() != preds.size();
    for (int p : eff) differs |= std::find(preds.begin(), preds.end(), p) == preds.end();
    if (differs && !eff.empty()) {
      std::vector<std::string> names;
      for (int p : eff) names.push_back(T.did[p]);
      ImGui::PushTextWrapPos(0);
      ImGui::TextDisabled("親に付いた依存関係も含めた実効の先行: %s", text::join(names, ", ").c_str());
      ImGui::PopTextWrapPos();
    }
  }
  otherLinksSection(f, i);
}

void commentsTab(Frame& f, int i) {
  app::AppController& c = f.c;
  UiState& u = f.u;
  Session* s = c.session();
  const Issue& o = s->viewIssues()[i];
  const std::string id = o.id;
  static std::string body;
  static std::string who;
  if (who.empty()) who = c.prefs().me;
  std::size_t del = static_cast<std::size_t>(-1);
  for (std::size_t k = 0; k < o.comments.size(); ++k) {
    const Comment& cm = o.comments[k];
    ImGui::PushID(static_cast<int>(k));
    avatar(u, cm.who.empty() ? "?" : cm.who);
    ImGui::SameLine();
    ImGui::BeginGroup();
    const float deleteW = u.m.s(28);
    const float authorW = std::max(0.f, ImGui::GetContentRegionAvail().x - deleteW - ImGui::GetStyle().ItemSpacing.x);
    ImGui::TextUnformatted(ellipsize(cm.who, authorW).c_str());
    ImGui::SameLine(ImGui::GetContentRegionMax().x - deleteW);
    if (iconButton(u, "del", "trash", "コメントを削除")) del = k;
    std::string at = cm.at;
    if (at.size() == 16) at[10] = ' ';
    ImGui::TextDisabled("%s", at.c_str());
    ImGui::PushTextWrapPos(0);
    ImGui::TextUnformatted(cm.text.c_str());
    ImGui::PopTextWrapPos();
    ImGui::EndGroup();
    ImGui::Separator();
    ImGui::PopID();
  }
  if (del != static_cast<std::size_t>(-1)) {
    s->delComment(id, del);
    c.afterEdit();
  }
  ImGui::InputTextMultiline("##body", &body, ImVec2(-1, u.m.s(80)));
  const bool post = ImGui::IsItemFocused() && ImGui::GetIO().KeyCtrl && enterPressed(u);
  ImGui::SetNextItemWidth(u.m.s(140));
  ImGui::InputTextWithHint("##who", "投稿者", &who);
  ImGui::SameLine();
  if ((ImGui::Button("投稿") || post) && !text::trim(body).empty()) {
    if (s->addComment(id, body, who)) {
      c.prefs().me = text::replaceAll(who.empty() ? "自分" : who, ";", "；");
      c.savePrefs();
      body.clear();
    }
    c.afterEdit();
  }
}

}  // namespace

void drawDetailPanel(Frame& f, ImVec2 size) {
  (void)size;
  app::AppController& c = f.c;
  UiState& u = f.u;
  Session* s = c.session();
  if (!s || !s->tree()) return;
  const Tree& T = *s->tree();
  const std::string cur = c.cursor();
  const int i = cur.empty() ? -1 : T.find(cur);
  if (i < 0) {
    flushPending("", true);
    ImGui::Dummy(ImVec2(0, u.m.s(40)));
    ImGui::TextDisabled("%s", S::kSelectHint);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - u.m.s(20));
    if (iconButton(u, "close", "x", "閉じる")) u.detailOpen = false;
    return;
  }
  flushPending(T.ids[i], false);
  u.issuePills.clear();
  ImGui::PushID(T.ids[i].c_str());
  // パネル全体が、開いている課題へのファイルのドロップ先
  registerDropTarget(f, T.ids[i], ImGui::GetWindowPos(),
                     ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowSize().x, ImGui::GetWindowPos().y + ImGui::GetWindowSize().y));
  const Issue& o = s->viewIssues()[i];
  const ScheduleRow& r = (*s->rows())[i];
  // 上部
  typeIcon(u, o.type);
  ImGui::SameLine();
  ImGui::PushFont(u.fontMono);
  ImGui::TextDisabled("%s", T.did[i].c_str());
  ImGui::PopFont();
  ImGui::SameLine();
  if (T.isLeaf(i)) {
    if (statusPill(u, "st", o.status, !c.readOnly())) openStatusMenu(f, o.id);
    ImGui::SameLine();
  }
  floatChip(u, r);
  ImGui::SameLine(ImGui::GetWindowWidth() - u.m.s(84));
  if (iconButton(u, "ops", "more", "操作")) ImGui::OpenPopup("##dops");
  if (ImGui::BeginPopup("##dops")) {
    const std::string id = o.id;
    ImGui::BeginDisabled(c.readOnly());
    if (ImGui::MenuItem("子を追加")) {
      const auto nid = c.createIssue(id, "");
      if (!nid.empty()) c.select(nid);
    }
    if (ImGui::MenuItem("同じ階層に追加")) {
      const auto nid = c.createIssue(o.parent, id);
      if (!nid.empty()) c.select(nid);
    }
    if (ImGui::MenuItem("階層を下げる")) s->indent(id), c.afterEdit();
    if (ImGui::MenuItem("階層を上げる")) s->outdent(id), c.afterEdit();
    if (ImGui::MenuItem("上へ")) s->moveIssue(id, -1), c.afterEdit();
    if (ImGui::MenuItem("下へ")) s->moveIssue(id, 1), c.afterEdit();
    ImGui::Separator();
    if (ImGui::MenuItem("削除")) requestDelete(f, {id});
    ImGui::EndDisabled();
    ImGui::EndPopup();
  }
  ImGui::SameLine();
  if (iconButton(u, "close", "x", "閉じる")) u.detailOpen = false;
  // タブ
  int nPred = 0;
  for (const auto& x : s->viewIssues())
    if (std::find(x.blocks.begin(), x.blocks.end(), o.id) != x.blocks.end()) ++nPred;
  const std::string tLinks = "依存関係 " + std::to_string(nPred + static_cast<int>(o.blocks.size())) + "###links";
  const std::string tCom = "コメント " + std::to_string(o.comments.size()) + "###comments";
  // 「添付」タブの中にリンクの節もある。リンクがあるときだけ、数をタブにも出す
  const std::string tAtt = "添付 " + std::to_string(c.attachmentCount(o.id)) +
                           (c.linkCountOf(o.id) ? "・リンク " + std::to_string(c.linkCountOf(o.id)) : std::string()) + "###attach";
  if (ImGui::BeginTabBar("##dtabs")) {
    if (ImGui::BeginTabItem("詳細", nullptr, u.detailTab == 0 ? ImGuiTabItemFlags_SetSelected : 0)) {
      if (ImGui::IsItemClicked()) u.detailTab = 0;
      ImGui::BeginChild("##dt", ImVec2(0, 0));
      detailTab(f, i);
      ImGui::EndChild();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem(tLinks.c_str(), nullptr, u.detailTab == 1 ? ImGuiTabItemFlags_SetSelected : 0)) {
      ImGui::BeginChild("##lt", ImVec2(0, 0));
      linksTab(f, i);  // 変更する部品だけを読み取り専用で無効にしている（先行・後続をたどる選択は使える）
      ImGui::EndChild();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem(tCom.c_str(), nullptr, u.detailTab == 2 ? ImGuiTabItemFlags_SetSelected : 0)) {
      ImGui::BeginChild("##ct", ImVec2(0, 0));
      ImGui::BeginDisabled(c.readOnly());
      commentsTab(f, i);
      ImGui::EndDisabled();
      ImGui::EndChild();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem(tAtt.c_str(), nullptr, u.detailTab == 3 ? ImGuiTabItemFlags_SetSelected : 0)) {
      ImGui::BeginChild("##at", ImVec2(0, 0));
      drawAttachTab(f, i);
      ImGui::EndChild();
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }
  u.detailTab = -1;  // SetSelected は1回だけ効かせる
  ImGui::PopID();
}

}  // namespace roadmap::ui
