// 詳細パネル（SPEC 10.9）: 詳細 / 依存関係 / コメント
#include <algorithm>
#include <functional>
#include <unordered_map>

#include <imgui_internal.h>
#include <imgui.h>
#include <imgui_stdlib.h>

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
    f.c.select(T.ids[id]);
  ImGui::SameLine();
  if (iconButton(u, "rm", "x", "外す")) onRemove();
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
  // パンくず
  {
    std::vector<int> anc;
    for (int a = T.parentOf[i]; a >= 0; a = T.parentOf[a]) anc.insert(anc.begin(), a);
    for (std::size_t k = 0; k < anc.size(); ++k) {
      if (k) {
        ImGui::SameLine(0, u.m.s(2));
        ImGui::TextDisabled("›");
        ImGui::SameLine(0, u.m.s(2));
      }
      ImGui::PushID(anc[k]);
      if (ImGui::SmallButton(ellipsize(T.name[anc[k]], u.m.s(120)).c_str())) c.select(T.ids[anc[k]]);
      ImGui::PopID();
    }
  }
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
    field(f, id, "asg", o.assignee, half, applier(f, id, [](AC&, Session& ss, const std::string& k, const std::string& v) {
            ss.setField(k, [&](Issue& x) { x.assignee = text::trim(v); });
          }));
    if (ImGui::IsItemHovered() && !s->assignees().empty())
      ImGui::SetTooltip("候補: %s", text::join(s->assignees(), "、").c_str());
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
    std::string h = "期間（週）";
    if (!std0) h += " @" + s->settings().scenario;
    label(h.c_str());
    if (T.kind[i] == Kind::Ms && r.weeks == 0) {
      ImGui::TextUnformatted("◆ マイルストーン（期間0）");
    } else if (!r.done) {
      if (iconButton(u, "dmin", "minus", "1週減らす")) {
        s->setDuration(id, std::max(0.0, r.weeks - 1));
        c.afterEdit();
      }
      ImGui::SameLine();
      field(f, id, "dur", csv::fmtNum(r.weeks), u.m.s(80),
            applier(f, id, [](AC&, Session& ss, const std::string& k, const std::string& v) {
              if (auto d = csv::parseDur(v, true); d && *d >= 0) ss.setDuration(k, text::jsRound(*d * 2) / 2);
            }));
      ImGui::SameLine();
      if (iconButton(u, "dplus", "plus", "1週増やす")) {
        s->setDuration(id, r.weeks + 1);
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
      ImGui::TextUnformatted((csv::fmtNum(r.weeks) + "週").c_str());
    }
  } else {
    ImGui::TextDisabled("配下の %d 件から集計（完了 %d）", r.n, r.nDone);
  }
  const auto dateField = [&](const char* key, const char* l, const OptDay& cur,
                             std::function<void(Session&, const std::string&, OptDay)> fn) {
    label(l);
    field(f, id, key, fmtIso(cur), half,
          applier(f, id, [fn](AC& cc, Session& ss, const std::string& k, const std::string& raw) {
            const std::string v = text::trim(raw);
            if (v.empty()) fn(ss, k, std::nullopt);
            else if (auto d = csv::parseDate(v)) fn(ss, k, *d);
            else cc.toast("日付として読めません: " + v);
          }),
          false, "YYYY-MM-DD");
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
}

void linksTab(Frame& f, int i) {
  app::AppController& c = f.c;
  UiState& u = f.u;
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
  ImGui::SetNextItemWidth(-1);
  ImGui::InputTextWithHint("##addp", "表示ID・件名を入力して Enter", &addPred);
  if (ImGui::IsItemFocused() && enterPressed(u) && !text::trim(addPred).empty()) {
    if (auto ref = s->resolveRef(addPred)) {
      s->addLink(*ref, id);
      addPred.clear();
    } else {
      c.toast("「" + text::trim(addPred) + "」が見つかりません");
    }
    c.afterEdit();
  }
  ImGui::SeparatorText("後続（これが終わるのを待つ）");
  for (int k : succs) issuePill(f, k, [&, k] {
    s->removeLink(id, I[k].id);
    c.afterEdit();
  });
  ImGui::SetNextItemWidth(-1);
  ImGui::InputTextWithHint("##adds", "表示ID・件名を入力して Enter", &addSucc);
  if (ImGui::IsItemFocused() && enterPressed(u) && !text::trim(addSucc).empty()) {
    if (auto ref = s->resolveRef(addSucc)) {
      s->addLink(id, *ref);
      addSucc.clear();
    } else {
      c.toast("「" + text::trim(addSucc) + "」が見つかりません");
    }
    c.afterEdit();
  }
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
    ImGui::TextUnformatted(cm.who.c_str());
    ImGui::SameLine();
    std::string at = cm.at;
    if (at.size() == 16) at[10] = ' ';
    ImGui::TextDisabled("%s", at.c_str());
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - u.m.s(10));
    if (iconButton(u, "del", "trash", "削除")) del = k;
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
  ImGui::PushID(T.ids[i].c_str());
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
    if (statusPill(u, "st", o.status, true)) openStatusMenu(f, o.id);
    ImGui::SameLine();
  }
  floatChip(u, r);
  ImGui::SameLine(ImGui::GetWindowWidth() - u.m.s(84));
  if (iconButton(u, "ops", "more", "操作")) ImGui::OpenPopup("##dops");
  if (ImGui::BeginPopup("##dops")) {
    const std::string id = o.id;
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
      linksTab(f, i);
      ImGui::EndChild();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem(tCom.c_str(), nullptr, u.detailTab == 2 ? ImGuiTabItemFlags_SetSelected : 0)) {
      ImGui::BeginChild("##ct", ImVec2(0, 0));
      commentsTab(f, i);
      ImGui::EndChild();
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }
  u.detailTab = -1;  // SetSelected は1回だけ効かせる
  ImGui::PopID();
}

}  // namespace roadmap::ui
