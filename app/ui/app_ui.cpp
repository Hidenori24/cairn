// 全体のレイアウト（上部バー・ツールバー・バナー・作業領域・重ねて出すもの）とキーボード（SPEC 10.1〜10.4）
#include "ui/app_ui.hpp"

#include <algorithm>
#include <imgui.h>
#include <imgui_stdlib.h>

#include <imgui_internal.h>  // ActivateItemByID

#include "roadmap/text.hpp"
#include "ui/icons.hpp"
#include "ui/internal.hpp"

namespace roadmap::ui {

using app::AppController;
using app::View;

// ---------------------------------------------------------------- 小道具
std::vector<int> rootIndexMap(const Tree& T, const std::vector<Issue>& issues) {
  std::vector<int> m(T.n, 0);
  int n = 0;
  for (int r : T.roots) m[r] = (!T.kids[r].empty() || isEpic(issues[r])) ? (n++ % 7) + 1 : 0;
  for (int id : T.dfs) m[id] = m[T.rootOf[id]];
  return m;
}

unsigned nameHash(const std::string& s) {
  unsigned h = 0;
  for (unsigned char ch : s) h = h * 31u + ch;
  return h;
}

bool isGoal(const Tree& T, const std::vector<Issue>& issues, int id) {
  return T.kids[id].empty() && isMilestone(issues[id]);
}

std::string generationName(int depth) {
  static const char* const kNames[] = {"最上位", "子", "孫", "ひ孫", "玄孫"};
  if (depth >= 0 && depth < 5) return kNames[depth];
  return "第" + std::to_string(depth + 1) + "世代";
}

ImU32 catColor(const UiState& u, Cat c) {
  if (c == Cat::Done) return u.t->ok;
  if (c == Cat::Prog) return u.t->accent;
  return u.t->muted;
}

void openDialog(Frame& f, Dlg d) {
  f.u.dlg = d;
  f.u.dlgOpening = true;
}

void openContext(Frame& f, const std::string& id) {
  if (!f.c.isSelected(id)) f.c.select(id);
  f.u.ctxId = id;
  f.u.ctxOpen = true;
}

void openStatusMenu(Frame& f, const std::string& id) {
  f.u.statusMenuId = id;
  f.u.statusMenuOpen = true;
}

void openDetail(Frame& f, const std::string& id, int tab) {
  if (!id.empty()) f.c.select(id);
  f.u.detailOpen = true;
  f.u.detailTab = tab;
}

void rowClick(Frame& f, const std::string& id) {
  const ImGuiIO& io = ImGui::GetIO();
  if (io.KeyShift) f.c.rangeSelect(id, f.u.order);
  else if (io.KeyCtrl) f.c.toggleSelect(id);
  else f.c.select(id);
}

void requestDelete(Frame& f, std::vector<std::string> ids) {
  Session* s = f.c.session();
  if (!s || ids.empty()) return;
  if (f.c.prefs().confirmDelete) {
    f.u.pendingDelete = std::move(ids);
    openDialog(f, Dlg::ConfirmIssueDelete);
    return;
  }
  s->deleteIssues(std::move(ids));
  f.c.afterEdit();
}

bool editing(const UiState& u, const std::string& id, EditField field) {
  return u.editField == field && u.editId == id;
}

void beginEdit(Frame& f, const std::string& id, EditField field, bool fresh) {
  const Session* s = f.c.session();
  if (!s) return;
  const Issue* o = s->issue(id);
  if (!o) return;
  const Tree* T = s->tree();
  f.u.editField = field;
  f.u.editId = id;
  f.u.editFresh = fresh;
  f.u.editFocus = true;
  if (field == EditField::Name) f.u.editBuf = T ? T->name[T->find(id)] : o->summary;
  else if (field == EditField::Assignee) f.u.editBuf = o->assignee;
  else if (field == EditField::Dur) {
    const int i = s->find(id);
    f.u.editBuf = s->rows() ? csv::fmtNum(s->rows()->at(i).weeks) : "1";
  }
  f.u.editStartValue = f.u.editBuf;
  f.u.editActive = false;
  f.u.editArmFrame = ImGui::GetFrameCount();
  f.u.editPendingChars.clear();
  f.c.select(id);
}

namespace {
// Tab を拾う。CallbackCompletion を使うと Tab で入力欄から抜けず、ここが呼ばれる
// （AllowTabInput は SetKeyboardFocusHere による編集開始を打ち消してしまうので使わない）
int tabCallback(ImGuiInputTextCallbackData* d) {
  if (d->EventFlag == ImGuiInputTextFlags_CallbackCompletion) *static_cast<int*>(d->UserData) = ImGui::GetIO().KeyShift ? -1 : 1;
  return 0;
}

bool commitEdit(Frame& f) {
  Session* s = f.c.session();
  if (!s) return false;
  const std::string id = f.u.editId;
  const std::string v = text::trim(f.u.editBuf);
  bool changed = false;
  if (v != text::trim(f.u.editStartValue)) {
    if (f.u.editField == EditField::Name) {
      if (!v.empty()) changed = s->setName(id, v);
    } else if (f.u.editField == EditField::Assignee) {
      changed = s->setField(id, [&](Issue& o) { o.assignee = v; });
    } else if (f.u.editField == EditField::Dur) {
      auto d = csv::parseDur(v, true);
      if (d && *d >= 0) changed = s->setDuration(id, text::jsRound(*d * 2) / 2);
    }
  }
  f.c.afterEdit();
  return changed;
}

void endEdit(UiState& u) {
  u.editField = EditField::None;
  u.editId.clear();
  u.editFresh = false;
  u.editActive = false;
  u.editPendingChars.clear();
}
}  // namespace

bool drawInlineEdit(Frame& f, float w) {
  UiState& u = f.u;
  ImGui::PushID("inline-edit");
  ImGui::SetNextItemWidth(w);
  if (u.editFocus) {
    // SetKeyboardFocusHere はナビゲーション経由で不確実なので、入力欄を直接アクティブにする
    ImGui::SetWindowFocus();
    ImGui::ActivateItemByID(ImGui::GetID("##e"));
    u.editFocus = false;
  }
  int tab = 0;
  ImGuiInputTextFlags fl = ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_CallbackCompletion;
  if (u.editField == EditField::Dur) fl |= ImGuiInputTextFlags_CharsDecimal;
  if (ImGui::GetActiveID() == ImGui::GetID("##e") && !u.editPendingChars.empty()) {
    // アクティブになる前に打った文字を、このフレームの入力の前に入れる
    ImGuiIO& io = ImGui::GetIO();
    ImVector<ImWchar> q;
    for (ImWchar ch : u.editPendingChars) q.push_back(ch);
    for (ImWchar ch : io.InputQueueCharacters) q.push_back(ch);
    io.InputQueueCharacters.swap(q);
    u.editPendingChars.clear();
  }
  ImGui::InputText("##e", &u.editBuf, fl, tabCallback, &tab);
  const bool active = ImGui::IsItemActive();
  u.editActive = active;
  const bool deactivated = ImGui::IsItemDeactivated();
  ImGui::PopID();
  const ImGuiIO& io = ImGui::GetIO();
  const std::string id = u.editId;
  const EditField field = u.editField;
  Session* s = f.c.session();
  if (!s) {
    endEdit(u);
    return false;
  }
  if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
    endEdit(u);
    return false;
  }
  if (enterPressed(u) && (active || deactivated)) {
    const bool fresh = u.editFresh;
    commitEdit(f);
    endEdit(u);
    if (field == EditField::Name && (io.KeyShift || io.KeyCtrl || fresh)) {
      const Issue* o = s->issue(id);
      std::string nid;
      if (io.KeyShift) nid = s->addIssue(id, "");
      else if (o) nid = s->addIssue(o->parent, id);
      f.c.afterEdit();
      if (!nid.empty()) beginEdit(f, nid, EditField::Name, true);
    }
    return false;
  }
  if (tab != 0 && field == EditField::Name) {
    commitEdit(f);
    if (tab > 0) s->indent(id);
    else s->outdent(id);
    f.c.afterEdit();
    u.editFocus = true;
    u.editBuf = s->tree() && s->find(id) >= 0 ? s->tree()->name[s->find(id)] : u.editBuf;
    u.editStartValue = u.editBuf;
    return true;
  }
  if (active && !io.KeyAlt && (ImGui::IsKeyPressed(ImGuiKey_UpArrow) || ImGui::IsKeyPressed(ImGuiKey_DownArrow))) {
    const int dir = ImGui::IsKeyPressed(ImGuiKey_UpArrow) ? -1 : 1;
    commitEdit(f);
    endEdit(u);
    auto it = std::find(u.order.begin(), u.order.end(), id);
    if (it != u.order.end()) {
      const auto k = (it - u.order.begin()) + dir;
      if (k >= 0 && k < static_cast<std::ptrdiff_t>(u.order.size())) beginEdit(f, u.order[k], field);
    }
    return false;
  }
  if (deactivated) {
    commitEdit(f);
    endEdit(u);
    return false;
  }
  return true;
}

void openTplApply(Frame& f) {
  f.u.dTargets = f.c.selection();
  f.u.dChoice = 0;
  openDialog(f, Dlg::TplApply);
}

void openTplSave(Frame& f, const std::string& id) {
  f.u.dTargets = {id};
  const Session* s = f.c.session();
  f.u.dName = s && s->tree() ? s->tree()->name[s->find(id)] : "";
  f.u.dText.clear();
  openDialog(f, Dlg::TplSave);
}

void drawNameCell(Frame& f, const VisibleRow& r, ImVec2 pos, ImVec2 size, const NameCellOpts& o) {
  UiState& u = f.u;
  Session* s = f.c.session();
  const Tree& T = *s->tree();
  const Issue& is = s->viewIssues()[r.id];
  const std::string& id = is.id;
  ImDrawList* dl = ImGui::GetWindowDrawList();
  float x = pos.x + (o.indent ? u.m.s(14) * static_cast<float>(r.depth) : 0);
  const float cy = pos.y + size.y / 2;
  // 開閉
  if (r.isPar) {
    ImGui::SetCursorScreenPos(ImVec2(x, cy - u.m.s(9)));
    ImGui::PushID(id.c_str());
    if (ImGui::InvisibleButton("##tg", ImVec2(u.m.s(18), u.m.s(18)))) s->setCollapsed(id, r.open), f.c.afterEdit();
    ImGui::PopID();
    drawIcon(dl, r.open ? "down" : "right", ImVec2(x + u.m.s(2), cy - u.m.s(7)), u.m.s(14), u.t->muted);
  }
  x += u.m.s(18);
  ImGui::SetCursorScreenPos(ImVec2(x, cy - u.m.s(8)));
  typeIcon(u, is.type);
  x += u.m.s(22);
  // 表示ID
  ImGui::PushFont(u.fontMono);
  const std::string did = T.did[r.id];
  const ImVec2 dsz = ImGui::CalcTextSize(did.c_str());
  dl->AddText(ImVec2(x, cy - dsz.y / 2), u.t->muted, did.c_str());
  ImGui::PopFont();
  x += dsz.x + u.m.s(6);
  const float right = pos.x + size.x - u.m.s(4);
  if (editing(u, id, EditField::Name)) {
    ImGui::SetCursorScreenPos(ImVec2(x, cy - ImGui::GetFrameHeight() / 2));
    drawInlineEdit(f, std::max(u.m.s(60), right - x));
    return;
  }
  const int nc = static_cast<int>(is.comments.size());
  const bool hov = ImGui::IsMouseHoveringRect(pos, ImVec2(pos.x + size.x, pos.y + size.y)) && o.showHoverButtons;
  float avail = right - x - (nc ? u.m.s(30) : 0) - (hov ? u.m.s(72) : 0);
  if (r.isPar) ImGui::PushFont(u.fontBold);
  const std::string nm = ellipsize(T.name[r.id], std::max(10.f, avail));
  const ImVec2 nsz = ImGui::CalcTextSize(nm.c_str());
  dl->AddText(ImVec2(x, cy - nsz.y / 2), u.t->ink, nm.c_str());
  if (r.isPar) ImGui::PopFont();
  x += nsz.x + u.m.s(6);
  if (nc) {
    drawIcon(dl, "message", ImVec2(x, cy - u.m.s(6)), u.m.s(12), u.t->muted);
    const std::string n = std::to_string(nc);
    dl->AddText(ImVec2(x + u.m.s(14), cy - ImGui::GetTextLineHeight() / 2), u.t->muted, n.c_str());
  }
  if (hov) {
    ImGui::PushID(id.c_str());
    // 削除（子も消える。通知の「元に戻す」で戻せる）
    ImGui::SetCursorScreenPos(ImVec2(right - u.m.s(72), cy - u.m.s(12)));
    const ImVec2 b0 = ImGui::GetCursorScreenPos();
    bool del = false;
    if (ImGui::InvisibleButton("##del", ImVec2(u.m.s(22), u.m.s(22)))) del = true;
    const bool delHov = ImGui::IsItemHovered();
    tip(r.isPar ? "削除（子の課題も削除。元に戻せます）" : "削除（元に戻せます）");
    drawIcon(dl, "trash", ImVec2(b0.x + u.m.s(3), b0.y + u.m.s(3)), u.m.s(16), delHov ? u.t->late : u.t->muted);
    ImGui::SetCursorScreenPos(ImVec2(right - u.m.s(48), cy - u.m.s(12)));
    const ImVec2 b = ImGui::GetCursorScreenPos();
    if (ImGui::InvisibleButton("##add", ImVec2(u.m.s(22), u.m.s(22)))) {
      deferAfterFrame(f, [&f, id] {
        Session* ss = f.c.session();
        if (!ss) return;
        const std::string nid = ss->addIssue(id, "");
        f.c.afterEdit();
        if (!nid.empty()) beginEdit(f, nid, EditField::Name, true);
      });
    }
    tip("子を追加");
    drawIcon(dl, "plus", ImVec2(b.x + u.m.s(3), b.y + u.m.s(3)), u.m.s(16), u.t->muted);
    ImGui::SetCursorScreenPos(ImVec2(right - u.m.s(24), cy - u.m.s(12)));
    const ImVec2 b2 = ImGui::GetCursorScreenPos();
    if (ImGui::InvisibleButton("##open", ImVec2(u.m.s(22), u.m.s(22)))) openDetail(f, id, 0);
    tip("詳細を開く");
    drawIcon(dl, "right", ImVec2(b2.x + u.m.s(3), b2.y + u.m.s(3)), u.m.s(16), u.t->muted);
    ImGui::PopID();
    if (del) deferAfterFrame(f, [&f, id] { requestDelete(f, {id}); });
  }
}

// ---------------------------------------------------------------- 上部バー
namespace {

void drawTopBar(Frame& f) {
  AppController& c = f.c;
  UiState& u = f.u;
  Session* s = c.session();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 p0 = ImGui::GetCursorScreenPos();
  const float W = ImGui::GetContentRegionAvail().x, H = u.m.topBar();
  dl->AddRectFilled(p0, ImVec2(p0.x + W, p0.y + H), u.t->surface);
  dl->AddLine(ImVec2(p0.x, p0.y + H - 1), ImVec2(p0.x + W, p0.y + H - 1), u.t->line);
  const float by = p0.y + (H - u.m.s(28)) / 2;
  ImGui::SetCursorScreenPos(ImVec2(p0.x + u.m.s(12), by));

  // プロジェクト切替
  {
    const std::string name = s ? s->settings().name : "プロジェクト";
    const std::string key = s ? s->settings().key : "";
    const std::string label = ellipsize(name, u.m.s(220)) + (key.empty() ? "" : "  [" + key + "]") + "  ▼";
    if (iconTextButton(u, "proj", "folder", label.c_str())) ImGui::OpenPopup("##projmenu");
    ImGui::SetNextWindowSize(ImVec2(u.m.s(300), 0));
    if (ImGui::BeginPopup("##projmenu")) {
      for (const auto& pi : c.projects()) {
        const std::string l = pi.name + (pi.key.empty() ? "" : "  [" + pi.key + "]") + "##" + pi.id;
        if (ImGui::MenuItem(l.c_str(), nullptr, pi.id == c.currentId())) c.openProject(pi.id);
      }
      ImGui::Separator();
      if (ImGui::MenuItem("新しいプロジェクト")) openDialog(f, Dlg::NewProject);
      if (ImGui::MenuItem("名前・キー・削除", nullptr, false, s != nullptr)) openDialog(f, Dlg::EditProject);
      ImGui::Separator();
      if (ImGui::MenuItem(S::kImportCsv)) openDialog(f, Dlg::Import);
      if (ImGui::MenuItem(S::kExport, nullptr, false, s != nullptr)) openDialog(f, Dlg::Export);
      ImGui::EndPopup();
    }
  }
  ImGui::SameLine(0, u.m.s(10));
  {
    const ImVec2 q = ImGui::GetCursorScreenPos();
    dl->AddLine(ImVec2(q.x, p0.y + u.m.s(10)), ImVec2(q.x, p0.y + H - u.m.s(10)), u.t->line);
    ImGui::Dummy(ImVec2(1, 1));
  }
  ImGui::SameLine(0, u.m.s(10));
  // ビュー切替
  const View cur = c.view();
  const struct {
    View v;
    const char* icon;
    const char* label;
  } tabs[] = {{View::Gantt, "gantt", S::kViewGantt}, {View::Board, "board", S::kViewBoard},
              {View::List, "list", S::kViewList}, {View::Goals, "target", S::kViewGoals}};
  for (const auto& t : tabs) {
    ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, by));
    if (iconTextButton(u, t.label, t.icon, t.label, false, cur == t.v, s != nullptr)) c.setView(t.v);
    ImGui::SameLine(0, u.m.s(2));
  }
  ImGui::SameLine(0, u.m.s(12));
  // 絞り込み欄
  {
    ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, by));
    const float w = std::clamp(W * 0.18f, u.m.s(160), u.m.s(300));
    const ImVec2 q = ImGui::GetCursorScreenPos();
    if (u.focusSearch) {
      ImGui::SetKeyboardFocusHere();
      u.focusSearch = false;
    }
    ImGui::SetNextItemWidth(w);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(u.m.s(26), u.m.s(5)));
    ImGui::InputTextWithHint("##q", S::kSearchHint, &c.filter().q);
    ImGui::PopStyleVar();
    if (ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
      c.filter().q.clear();
      ImGui::SetWindowFocus(nullptr);
    }
    drawIcon(dl, "search", ImVec2(q.x + u.m.s(6), q.y + u.m.s(6)), u.m.s(14), u.t->muted);
    if (!c.filter().q.empty()) {
      ImGui::SameLine(0, 0);
      ImGui::SetCursorScreenPos(ImVec2(q.x + w - u.m.s(24), q.y + u.m.s(2)));
      if (iconButton(u, "clrq", "x", "消す")) c.filter().q.clear();
    }
  }
  // 状況チップ
  if (s && s->tree()) {
    const Summary sm = s->summary();
    ImGui::SameLine(0, u.m.s(10));
    ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, by + u.m.s(2)));
    auto& flt = c.filter().flt;
    if (chipButton(u, "late", std::string(S::kLate) + " " + std::to_string(sm.late), u.t->late, u.t->lateBg, sm.late == 0))
      flt = flt == FltFilter::Late ? FltFilter::None : FltFilter::Late;
    ImGui::SameLine(0, u.m.s(4));
    if (chipButton(u, "tight", std::string(S::kTight) + " " + std::to_string(sm.tight), u.t->tight, u.t->tightBg, sm.tight == 0))
      flt = flt == FltFilter::Tight ? FltFilter::None : FltFilter::Tight;
    ImGui::SameLine(0, u.m.s(4));
    chip(u, "完了 " + std::to_string(sm.done) + " / 全体 " + std::to_string(sm.total), u.t->muted, u.t->surface3);
    // ゴールボタン（ルート課題が1つもなければ出さない）
    const Tree& T = *s->tree();
    bool anyRoot = false;
    for (int r : T.roots) anyRoot |= !T.kids[r].empty() || isEpic(s->viewIssues()[r]);
    if (anyRoot) {
      ImGui::SameLine(0, u.m.s(6));
      ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, by));
      if (iconTextButton(u, "goal", "flag", S::kGoal)) ImGui::OpenPopup("##goalpop");
      drawGoalPopover(f);
    }
  }
  // 右側: 保存状態 / Ctrl+K / 元に戻す / やり直す / ＋課題 / その他
  const float rightW = u.m.s(150 + 36 + 32 * 2 + 80 + 36);
  ImGui::SameLine(0, 0);
  const float rx = p0.x + W - rightW - u.m.s(12);
  if (ImGui::GetCursorScreenPos().x < rx) ImGui::SetCursorScreenPos(ImVec2(rx, by));
  else ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x + u.m.s(8), by));
  {
    std::string label;
    ImU32 fg, bg;
    switch (c.saveState()) {
      case app::SaveState::Saving:
        label = S::kSaving;
        fg = u.t->accent;
        bg = u.t->accentBg;
        break;
      case app::SaveState::Error:
        label = S::kSaveError;
        fg = u.t->late;
        bg = u.t->lateBg;
        break;
      default:
        label = std::string(S::kSaved) + (c.savedAt().empty() ? "" : " " + c.savedAt());
        fg = u.t->ok;
        bg = u.t->okBg;
    }
    ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, by + u.m.s(2)));
    if (chipButton(u, "save", label, fg, bg)) openDialog(f, Dlg::Folder);
    tip("保存先: " + c.dataDir() + (c.dataDirFallback ? "（既定の場所に書けないため）" : ""));
  }
  ImGui::SameLine(0, u.m.s(6));
  ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, by));
  if (iconButton(u, "palette", "cmd", "コマンドパレット (Ctrl+K)")) {
    u.paletteOpen = true;
    u.paletteFocus = true;
  }
  ImGui::SameLine(0, u.m.s(2));
  if (iconButton(u, "undo", "undo", "元に戻す (Ctrl+Z)", s && s->canUndo())) c.undo();
  ImGui::SameLine(0, u.m.s(2));
  if (iconButton(u, "redo", "redo", "やり直す (Ctrl+Shift+Z)", s && s->canRedo())) c.redo();
  ImGui::SameLine(0, u.m.s(6));
  if (iconTextButton(u, "add", nullptr, S::kAddIssue, true, false, s != nullptr)) {
    const std::string id = s->addIssue("", "");
    c.afterEdit();
    if (!id.empty()) beginEdit(f, id, EditField::Name, true);
  }
  ImGui::SameLine(0, u.m.s(4));
  if (iconButton(u, "more", "more", S::kMore)) ImGui::OpenPopup("##more");
  if (ImGui::BeginPopup("##more")) {
    if (ImGui::MenuItem("アプリの設定…", "Ctrl+,")) openDialog(f, Dlg::AppSettings);
    if (ImGui::MenuItem("プロジェクト設定", nullptr, false, s != nullptr)) openDialog(f, Dlg::Settings);
    if (ImGui::MenuItem("担当者の負荷", nullptr, false, s != nullptr)) openDialog(f, Dlg::Workload);
    if (ImGui::MenuItem("読み込み時の注意", nullptr, false, s != nullptr)) openDialog(f, Dlg::Warnings);
    ImGui::Separator();
    ImGui::TextDisabled("テーマ");
    for (const auto& [k, l] : {std::pair<const char*, const char*>{"auto", "自動"}, {"light", "ライト"}, {"dark", "ダーク"}})
      if (ImGui::MenuItem(l, nullptr, c.prefs().theme == k)) c.setTheme(k);
    ImGui::TextDisabled("密度");
    if (ImGui::MenuItem("標準", nullptr, c.prefs().density != "compact")) c.setDensity("normal");
    if (ImGui::MenuItem("コンパクト", nullptr, c.prefs().density == "compact")) c.setDensity("compact");
    ImGui::Separator();
    if (ImGui::MenuItem("保存先フォルダ")) openDialog(f, Dlg::Folder);
    if (ImGui::MenuItem("ショートカット一覧", "?")) openDialog(f, Dlg::Shortcuts);
    ImGui::EndPopup();
  }
  ImGui::SetCursorScreenPos(ImVec2(p0.x, p0.y + H));
  ImGui::Dummy(ImVec2(0, 0));
}

// ---------------------------------------------------------------- ツールバー
template <class T>
bool combo(const char* id, float w, const char* preview, const std::vector<std::pair<T, std::string>>& items, T& cur) {
  bool changed = false;
  ImGui::SetNextItemWidth(w);
  if (ImGui::BeginCombo(id, preview)) {
    for (const auto& [v, l] : items)
      if (ImGui::Selectable(l.c_str(), v == cur)) {
        cur = v;
        changed = true;
      }
    ImGui::EndCombo();
  }
  return changed;
}

void drawToolBar(Frame& f) {
  AppController& c = f.c;
  UiState& u = f.u;
  Session* s = c.session();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 p0 = ImGui::GetCursorScreenPos();
  const float W = ImGui::GetContentRegionAvail().x, H = u.m.toolBar();
  dl->AddRectFilled(p0, ImVec2(p0.x + W, p0.y + H), u.t->surface2);
  dl->AddLine(ImVec2(p0.x, p0.y + H - 1), ImVec2(p0.x + W, p0.y + H - 1), u.t->line);
  ImGui::SetCursorScreenPos(ImVec2(p0.x, p0.y));
  ImGui::BeginChild("##toolbar", ImVec2(W, H), ImGuiChildFlags_None,
                    ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground);
  const float by = (H - ImGui::GetFrameHeight()) / 2;
  ImGui::SetCursorPos(ImVec2(u.m.s(12), by));
  if (!s) {
    ImGui::Dummy(ImVec2(0, 0));
    ImGui::EndChild();
    return;
  }
  Filter& fl = c.filter();
  // 担当者
  {
    std::string preview = S::kAll;
    if (fl.asg == AsgFilter::Name) preview = fl.asgName;
    else if (fl.asg == AsgFilter::Unassigned) preview = S::kUnassigned;
    else if (fl.asg == AsgFilter::Assigned) preview = S::kAssigned;
    const ImVec2 q = ImGui::GetCursorScreenPos();
    ImGui::SetNextItemWidth(u.m.s(130));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(u.m.s(24), ImGui::GetStyle().FramePadding.y));
    if (ImGui::BeginCombo("##asg", preview.c_str())) {
      if (ImGui::Selectable(S::kAll, fl.asg == AsgFilter::All)) fl.asg = AsgFilter::All;
      for (const auto& a : s->assignees())
        if (ImGui::Selectable(a.c_str(), fl.asg == AsgFilter::Name && fl.asgName == a)) {
          fl.asg = AsgFilter::Name;
          fl.asgName = a;
        }
      if (ImGui::Selectable(S::kUnassigned, fl.asg == AsgFilter::Unassigned)) fl.asg = AsgFilter::Unassigned;
      if (ImGui::Selectable(S::kAssigned, fl.asg == AsgFilter::Assigned)) fl.asg = AsgFilter::Assigned;
      ImGui::EndCombo();
    }
    ImGui::PopStyleVar();
    drawIcon(ImGui::GetWindowDrawList(), "user", ImVec2(q.x + u.m.s(6), q.y + u.m.s(5)), u.m.s(14), u.t->muted);
  }
  ImGui::SameLine();
  {
    const std::vector<std::pair<StFilter, std::string>> items = {
        {StFilter::All, "状況: すべて"}, {StFilter::Open, "状況: 未完了"}, {StFilter::Todo, "状況: To Do"},
        {StFilter::Prog, "状況: 進行中"}, {StFilter::Done, "状況: 完了"}};
    std::string pv;
    for (const auto& [v, l] : items)
      if (v == fl.st) pv = l;
    combo("##st", u.m.s(120), pv.c_str(), items, fl.st);
  }
  ImGui::SameLine();
  {
    std::vector<std::pair<std::string, std::string>> items = {{"", "優先度: すべて"}};
    for (const char* p : kPriorities) items.emplace_back(text::toLower(p), std::string("優先度: ") + p);
    std::string pv;
    for (const auto& [v, l] : items)
      if (v == fl.pr) pv = l;
    combo("##pr", u.m.s(130), pv.c_str(), items, fl.pr);
  }
  const View v = c.view();
  if (v == View::Gantt) {
    ImGui::SameLine(0, u.m.s(12));
    const std::string zoom = c.uiStr("zoom", "week");
    for (const auto& [k, l] : {std::pair<const char*, const char*>{"day", "日"}, {"week", "週"}, {"month", "月"}, {"all", "全体"}}) {
      if (iconTextButton(u, k, nullptr, l, false, zoom == k && u.customPx <= 0)) {
        c.setUi("zoom", k);
        u.customPx = 0;
        u.scrollToBase = true;
      }
      ImGui::SameLine(0, u.m.s(2));
    }
    ImGui::SameLine(0, u.m.s(10));
    {
      const std::vector<std::pair<std::string, std::string>> items = {{"root", "色: Epic"}, {"float", "色: 余裕"}, {"asg", "色: 担当者"}};
      std::string cur = c.uiStr("colorBy", "root"), pv;
      for (const auto& [k, l] : items)
        if (k == cur) pv = l;
      if (combo("##color", u.m.s(110), pv.c_str(), items, cur)) c.setUi("colorBy", cur);
    }
    ImGui::SameLine();
    {
      std::vector<std::pair<int, std::string>> items = {{0, "階層: すべて"}};
      for (int i = 1; i <= 4; ++i) items.emplace_back(i, std::to_string(i) + "階層まで");
      int cur = c.uiInt("level", 0);
      std::string pv;
      for (const auto& [k, l] : items)
        if (k == cur) pv = l;
      if (combo("##level", u.m.s(110), pv.c_str(), items, cur)) c.setUi("level", cur);
    }
  }
  if (v == View::Gantt || v == View::List) {
    ImGui::SameLine();
    if (iconTextButton(u, "expand", nullptr, "展開 ▼")) ImGui::OpenPopup("##expand");
    if (ImGui::BeginPopup("##expand")) {
      const Tree* T = s->tree();
      const auto setAll = [&](int maxOpenDepth) {
        if (!T) return;
        Json col = Json::object();
        for (int id : T->dfs)
          if (!T->kids[id].empty() && (maxOpenDepth < 0 || T->depth[id] >= maxOpenDepth)) col[T->ids[id]] = true;
        c.setUi("collapsed", col);
      };
      if (ImGui::MenuItem("すべて開く")) c.setUi("collapsed", Json::object());
      if (ImGui::MenuItem("すべて閉じる")) setAll(-1);
      if (v == View::Gantt && ImGui::MenuItem("2階層目まで開く")) setAll(1);
      ImGui::EndPopup();
    }
    if (v == View::Gantt) {
      ImGui::SameLine();
      if (iconTextButton(u, "tobase", "calendar", "基準日へ")) u.scrollToBase = true;
    }
  }
  if (v == View::Board) {
    ImGui::SameLine(0, u.m.s(12));
    const std::vector<std::pair<std::string, std::string>> items = {{"none", "スイムレーン: なし"}, {"asg", "スイムレーン: 担当者"}, {"root", "スイムレーン: ルート課題"}};
    std::string cur = c.uiStr("lane", "none"), pv;
    for (const auto& [k, l] : items)
      if (k == cur) pv = l;
    if (combo("##lane", u.m.s(170), pv.c_str(), items, cur)) c.setUi("lane", cur);
  }
  if (c.filterActive()) {
    ImGui::SameLine(0, u.m.s(12));
    const std::string l = "絞り込み解除（" + c.filterLabel() + "）";
    if (iconTextButton(u, "clrf", "x", l.c_str())) c.clearFilter();
  }
  // 右側
  const auto warns = s->allWarns();
  const float rightW = u.m.s(140 + 130 + 150) + (warns.empty() ? 0 : u.m.s(80));
  ImGui::SameLine(0, 0);
  const float rx = ImGui::GetWindowWidth() - rightW - u.m.s(12);
  ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX() + u.m.s(12), rx));
  if (!warns.empty()) {
    const std::string l = "注意 " + std::to_string(warns.size());
    if (iconTextButton(u, "warns", "warn", l.c_str())) openDialog(f, Dlg::Warnings);
    ImGui::SameLine();
  }
  {
    ImGui::SetNextItemWidth(u.m.s(130));
    std::string base = s->settings().baseDate;
    ImGui::InputText("##base", &base, ImGuiInputTextFlags_EnterReturnsTrue);
    if (ImGui::IsItemDeactivatedAfterEdit()) {
      if (auto d = csv::parseDate(base)) {
        s->setBaseDate(fmtIso(*d));
        c.afterEdit();
      }
    }
    tip("基準日（YYYY-MM-DD。変更すると再計算します）");
  }
  ImGui::SameLine();
  {
    const Tree* T = s->tree();
    std::vector<std::pair<std::string, std::string>> items;
    if (T)
      for (const auto& sc : T->scenarios) items.emplace_back(sc, sc);
    else items.emplace_back(kStdScenario, kStdScenario);
    std::string cur = s->settings().scenario;
    if (combo("##scn", u.m.s(140), ("シナリオ: " + cur).c_str(), items, cur)) {
      s->setScenario(cur);
      c.afterEdit();
    }
  }
  ImGui::SameLine();
  {
    const ResourceLimit r = s->settings().resource;
    const std::string l = r.on ? "同時作業の制限 " + std::to_string(r.cap) : "同時作業の制限";
    if (iconTextButton(u, "res", "user", l.c_str(), false, r.on)) {
      s->setResource({!r.on, r.cap});
      c.afterEdit();
    }
    tip("担当者ごとの同時作業の制限（同時に持てる件数はプロジェクト設定で変えられます）");
  }
  ImGui::EndChild();
  ImGui::SetCursorScreenPos(ImVec2(p0.x, p0.y + H));
  ImGui::Dummy(ImVec2(0, 0));
}

// ---------------------------------------------------------------- バナー・通知
void drawBanners(Frame& f) {
  AppController& c = f.c;
  UiState& u = f.u;
  Session* s = c.session();
  const auto banner = [&](const std::string& msg, ImU32 fg, ImU32 bg, const char* btn, auto&& onBtn) {
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const float W = ImGui::GetContentRegionAvail().x, H = u.m.s(34);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, ImVec2(p0.x + W, p0.y + H), bg);
    drawIcon(dl, "warn", ImVec2(p0.x + u.m.s(12), p0.y + u.m.s(9)), u.m.s(16), fg);
    dl->AddText(ImVec2(p0.x + u.m.s(36), p0.y + (H - ImGui::GetTextLineHeight()) / 2), fg,
                ellipsize(msg, W - u.m.s(180)).c_str());
    if (btn) {
      ImGui::SetCursorScreenPos(ImVec2(p0.x + W - u.m.s(130), p0.y + u.m.s(3)));
      if (iconTextButton(u, msg.c_str(), nullptr, btn)) onBtn();
    }
    ImGui::SetCursorScreenPos(ImVec2(p0.x, p0.y + H));
    ImGui::Dummy(ImVec2(0, 0));
  };
  if (c.saveState() == app::SaveState::Error)
    banner("保存できません: " + c.saveError(), u.t->late, u.t->lateBg, "再試行", [&] { c.retrySave(); });
  if (s && !s->error().empty())
    banner(s->error(), u.t->late, u.t->lateBg, "元に戻す", [&] { c.undo(); });
}

void drawToasts(Frame& f) {
  AppController& c = f.c;
  UiState& u = f.u;
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  float y = vp->WorkPos.y + vp->WorkSize.y - u.m.s(16);
  const auto toasts = c.toasts();
  for (auto it = toasts.rbegin(); it != toasts.rend(); ++it) {
    const auto& t = *it;
    const float w = std::min(u.m.s(520), ImGui::CalcTextSize(t.text.c_str()).x + u.m.s(140));
    const float h = u.m.s(40);
    y -= h;
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + u.m.s(16), y));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, v4(u.t->dark ? u.t->surface3 : u.t->ink));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(u.m.s(12), u.m.s(8)));
    const std::string wid = "##toast" + std::to_string(t.id);
    ImGui::Begin(wid.c_str(), nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
    ImGui::PushStyleColor(ImGuiCol_Text, v4(u.t->dark ? u.t->ink : u.t->surface));
    ImGui::SetCursorPosY(u.m.s(10));
    ImGui::TextUnformatted(ellipsize(t.text, w - u.m.s(130)).c_str());
    ImGui::PopStyleColor();
    if (t.action == NoticeAction::Undo) {
      ImGui::SameLine(w - u.m.s(120));
      ImGui::SetCursorPosY(u.m.s(6));
      if (ImGui::SmallButton(S::kUndo)) c.runToastAction(t.id);
    }
    ImGui::SameLine(w - u.m.s(36));
    ImGui::SetCursorPosY(u.m.s(6));
    if (iconButton(u, "close", "x", "閉じる")) c.dismissToast(t.id);
    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    y -= u.m.s(8);
  }
}

// ---------------------------------------------------------------- コンテキストメニュー・状況メニュー
void drawContextMenu(Frame& f) {
  AppController& c = f.c;
  UiState& u = f.u;
  Session* s = c.session();
  if (u.ctxOpen) {
    ImGui::OpenPopup("##ctx");
    u.ctxOpen = false;
  }
  if (!s || !ImGui::BeginPopup("##ctx")) return;
  const auto sel = c.selection();
  const bool multi = sel.size() > 1;
  const std::string id = u.ctxId;
  const Tree* T = s->tree();
  const int idx = T ? T->find(id) : -1;
  if (idx < 0) {
    ImGui::EndPopup();
    return;
  }
  if (multi) ImGui::TextDisabled("%d件を選択", static_cast<int>(sel.size()));
  else {
    if (ImGui::MenuItem("詳細を開く", "Space")) openDetail(f, id, 0);
    if (ImGui::MenuItem("名前を変更", "F2")) beginEdit(f, id, EditField::Name);
    ImGui::Separator();
    if (ImGui::MenuItem("子を追加", "Shift+N")) {
      const auto nid = s->addIssue(id, "");
      c.afterEdit();
      if (!nid.empty()) beginEdit(f, nid, EditField::Name, true);
    }
    if (ImGui::MenuItem("同じ階層に追加", "N")) {
      const auto nid = s->addIssue(s->issue(id)->parent, id);
      c.afterEdit();
      if (!nid.empty()) beginEdit(f, nid, EditField::Name, true);
    }
  }
  if (ImGui::MenuItem("テンプレから子タスクを追加…")) openTplApply(f);
  if (!multi && ImGui::MenuItem("この子タスクをテンプレとして保存…", nullptr, false, !T->kids[idx].empty())) openTplSave(f, id);
  ImGui::Separator();
  ImGui::TextDisabled("状況");
  for (const char* st : {S::kStatusTodo, S::kStatusProg, S::kStatusDone}) {
    const bool curSt = !multi && (s->issue(id)->status.empty() ? std::string(S::kStatusTodo) : s->issue(id)->status) == st;
    if (ImGui::MenuItem(st, nullptr, curSt)) c.setStatusFor(sel, st);
  }
  ImGui::Separator();
  const auto sb = s->siblings(id);
  const auto pos = std::find(sb.begin(), sb.end(), id) - sb.begin();
  if (ImGui::MenuItem("階層を下げる", "Tab", false, multi || pos > 0)) c.indentSelection();
  if (ImGui::MenuItem("階層を上げる", "Shift+Tab", false, multi || T->parentOf[idx] >= 0)) c.outdentSelection();
  if (ImGui::MenuItem("上へ", "Alt+↑", false, multi || pos > 0)) c.moveSelection(-1);
  if (ImGui::MenuItem("下へ", "Alt+↓", false, multi || pos + 1 < static_cast<std::ptrdiff_t>(sb.size()))) c.moveSelection(1);
  ImGui::Separator();
  ImGui::PushStyleColor(ImGuiCol_Text, v4(u.t->late));
  if (ImGui::MenuItem("削除", "Del")) requestDelete(f, c.selection());
  ImGui::PopStyleColor();
  ImGui::EndPopup();
}

void drawStatusMenu(Frame& f) {
  AppController& c = f.c;
  UiState& u = f.u;
  Session* s = c.session();
  if (u.statusMenuOpen) {
    ImGui::OpenPopup("##stmenu");
    u.statusMenuOpen = false;
  }
  if (!s || !ImGui::BeginPopup("##stmenu")) return;
  const Issue* o = s->issue(u.statusMenuId);
  if (o) {
    const std::string cur = o->status.empty() ? S::kStatusTodo : o->status;
    for (const char* st : {S::kStatusTodo, S::kStatusProg, S::kStatusDone})
      if (ImGui::MenuItem(st, nullptr, cur == st)) {
        // 選択に含まれている課題なら選択全体に
        if (c.isSelected(u.statusMenuId) && c.selection().size() > 1) c.setStatusFor(c.selection(), st);
        else c.setStatusFor({u.statusMenuId}, st);
      }
  }
  ImGui::EndPopup();
}

// ---------------------------------------------------------------- まとめ編集バー（2件以上の選択）
void drawBulkBar(Frame& f, ImVec2 origin, float w) {
  AppController& c = f.c;
  UiState& u = f.u;
  const auto sel = c.selection();
  if (sel.size() < 2) return;
  const float h = u.m.s(44);
  ImGui::SetNextWindowPos(ImVec2(origin.x + u.m.s(16), origin.y - h - u.m.s(12)));
  ImGui::SetNextWindowSize(ImVec2(std::min(w - u.m.s(32), u.m.s(980)), h));
  ImGui::PushStyleColor(ImGuiCol_WindowBg, v4(u.t->surface));
  ImGui::Begin("##bulk", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
  ImGui::AlignTextToFramePadding();
  ImGui::Text("%d件を選択", static_cast<int>(sel.size()));
  for (const char* st : {S::kStatusTodo, S::kStatusProg, S::kStatusDone}) {
    ImGui::SameLine();
    if (ImGui::SmallButton(st)) c.setStatusFor(sel, st);
  }
  ImGui::SameLine();
  ImGui::SetNextItemWidth(u.m.s(110));
  if (ImGui::BeginCombo("##bprio", "優先度")) {
    for (const char* p : kPriorities)
      if (ImGui::Selectable(p)) c.setPrioFor(sel, p);
    ImGui::EndCombo();
  }
  ImGui::SameLine();
  static std::string who;
  ImGui::SetNextItemWidth(u.m.s(120));
  ImGui::InputTextWithHint("##bwho", "担当者（Enter）", &who);
  if (ImGui::IsItemFocused() && enterPressed(u)) {
    c.setAssigneeFor(sel, who);
    who.clear();
  }
  ImGui::SameLine();
  if (ImGui::SmallButton("テンプレ")) openTplApply(f);
  ImGui::SameLine();
  if (ImGui::SmallButton("階層を下げる")) c.indentSelection();
  ImGui::SameLine();
  if (ImGui::SmallButton("階層を上げる")) c.outdentSelection();
  ImGui::SameLine();
  ImGui::PushStyleColor(ImGuiCol_Text, v4(u.t->late));
  if (ImGui::SmallButton("削除")) requestDelete(f, c.selection());
  ImGui::PopStyleColor();
  ImGui::SameLine();
  if (ImGui::SmallButton("選択解除")) c.clearSelection();
  ImGui::End();
  ImGui::PopStyleColor();
}

// ---------------------------------------------------------------- はじめに
void drawWelcome(Frame& f, ImVec2 size) {
  AppController& c = f.c;
  UiState& u = f.u;
  const float w = std::min(size.x - u.m.s(40), u.m.s(560));
  ImGui::SetCursorPos(ImVec2((size.x - w) / 2, std::max(u.m.s(40), size.y * 0.18f)));
  ImGui::BeginChild("##welcome", ImVec2(w, u.m.s(340)), ImGuiChildFlags_Border);
  ImGui::PushFont(u.fontLarge);
  ImGui::TextUnformatted(S::kWelcome);
  ImGui::PopFont();
  ImGui::TextWrapped("データフォルダにプロジェクトがありません。始め方を選んでください。選んだものだけをデータフォルダに書きます。");
  ImGui::AlignTextToFramePadding();
  ImGui::TextDisabled("保存先: %s", c.dataDir().c_str());
  if (!c.dataDirLocked()) {
    ImGui::SameLine();
    if (ImGui::SmallButton("変更…")) openDialog(f, Dlg::AppSettings);
  }
  ImGui::Spacing();
  const ImVec2 bs(w - u.m.s(24), u.m.s(36));
  if (ImGui::Button("空のプロジェクトを作る", bs)) c.createProject(app::SampleKind::Empty, "新しいプロジェクト", "");
  if (ImGui::Button("サンプル（階層デモ）", bs)) c.createProject(app::SampleKind::Hierarchy, "階層デモ（セキュアブート）", "DEV");
  if (ImGui::Button(S::kImportCsv, bs)) openDialog(f, Dlg::Import);
  ImGui::EndChild();
}

// ---------------------------------------------------------------- 画面全体の拡大縮小
// 一般的な操作に合わせる: Ctrl+ホイール（タッチパッドのピンチも同じ）、Ctrl+「+」「−」、Ctrl+0 で100%
// （ガントの時間軸の上の Ctrl+ホイールは、ガントが時間軸の拡大縮小に使う）
void handleZoom(Frame& f) {
  AppController& c = f.c;
  UiState& u = f.u;
  ImGuiIO& io = ImGui::GetIO();
  const std::int64_t now = c.clock().monotonicMs();
  int dir = 0;
  bool reset = false;
  if (io.KeyCtrl && !io.KeyAlt) {
    if (ImGui::IsKeyPressed(ImGuiKey_Equal, true) || ImGui::IsKeyPressed(ImGuiKey_KeypadAdd, true)) dir = 1;
    else if (ImGui::IsKeyPressed(ImGuiKey_Minus, true) || ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract, true)) dir = -1;
    else if (ImGui::IsKeyPressed(ImGuiKey_0, false) || ImGui::IsKeyPressed(ImGuiKey_Keypad0, false)) reset = true;
    // ホイール: 1ノッチで1段階。細かい回転はためる。段階の作り直し（フォント）が続かないように 0.12 秒あける
    if (io.MouseWheel != 0.f && !u.wheelConsumed) {
      u.zoomAcc += io.MouseWheel;
      if (std::abs(u.zoomAcc) >= 1.f && now - u.zoomLastStepMs >= 120) {
        dir = u.zoomAcc > 0 ? 1 : -1;
        u.zoomAcc -= static_cast<float>(dir);
      }
    }
  } else {
    u.zoomAcc = 0;
  }
  if (dir != 0 || reset) {
    const bool changed = reset ? c.setUiScale(100) : c.stepUiScale(dir);
    u.zoomLastStepMs = now;
    u.zoomHint = std::to_string(c.prefs().uiScale) + "%" + (changed ? "" : (reset ? "" : "（これ以上変えられません）"));
    u.zoomHintUntilMs = now + 1200;
  }
}

// 倍率の表示（変えた直後に、画面の上の真ん中へ少しの間）
void drawZoomHint(Frame& f) {
  UiState& u = f.u;
  const std::int64_t now = f.c.clock().monotonicMs();
  if (u.zoomHint.empty() || now >= u.zoomHintUntilMs) return;
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  const ImVec2 ts = ImGui::CalcTextSize(u.zoomHint.c_str());
  const float pad = u.m.s(12);
  const ImVec2 a(vp->WorkPos.x + (vp->WorkSize.x - ts.x) / 2 - pad, vp->WorkPos.y + u.m.s(64));
  const ImVec2 b(a.x + ts.x + pad * 2, a.y + ts.y + pad);
  dl->AddRectFilled(a, b, IM_COL32(24, 28, 36, 225), u.m.s(8));
  dl->AddText(ImVec2(a.x + pad, a.y + pad / 2), IM_COL32(255, 255, 255, 255), u.zoomHint.c_str());
}

// ---------------------------------------------------------------- キーボード（SPEC 10.4.4）
void handleKeys(Frame& f) {
  AppController& c = f.c;
  UiState& u = f.u;
  ImGuiIO& io = ImGui::GetIO();
  // Ctrl+, はアプリの設定（ほかのダイアログが開いていないとき）
  if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Comma, false) && u.dlg == Dlg::None) {
    openDialog(f, Dlg::AppSettings);
    return;
  }
  // Ctrl+K はいつでも
  if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_K, false)) {
    u.paletteOpen = !u.paletteOpen;
    u.paletteFocus = u.paletteOpen;
    u.paletteQuery.clear();
    u.paletteSel = 0;
    return;
  }
  const bool modal = u.dlg != Dlg::None || u.paletteOpen || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);
  if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !io.WantTextInput) {
    if (modal) return;  // ダイアログ・パレット・メニューはそれぞれで閉じる
    if (!c.selection().empty()) c.clearSelection();
    else if (u.detailOpen) u.detailOpen = false;
    return;
  }
  if (modal || io.WantTextInput || u.editField != EditField::None) return;
  Session* s = c.session();
  if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
    if (io.KeyShift) c.redo();
    else c.undo();
    return;
  }
  if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y, false)) {
    c.redo();
    return;
  }
  if (io.KeyCtrl || !s) return;
  if (!io.KeyShift && !io.KeyAlt) {
    if (ImGui::IsKeyPressed(ImGuiKey_1, false)) c.setView(View::Gantt);
    if (ImGui::IsKeyPressed(ImGuiKey_2, false)) c.setView(View::Board);
    if (ImGui::IsKeyPressed(ImGuiKey_3, false)) c.setView(View::List);
    if (ImGui::IsKeyPressed(ImGuiKey_4, false)) c.setView(View::Goals);
  }
  for (ImWchar ch : io.InputQueueCharacters) {
    if (ch == '/') u.focusSearch = true;
    if (ch == '?') openDialog(f, Dlg::Shortcuts);
  }
  const std::string cur = c.cursor();
  if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) || ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
    const int dir = ImGui::IsKeyPressed(ImGuiKey_UpArrow) ? -1 : 1;
    if (io.KeyAlt) c.moveSelection(dir);
    else c.moveCursor(dir, io.KeyShift, u.order);
  }
  const Tree* T = s->tree();
  if (T && !cur.empty() && T->find(cur) >= 0) {
    const int i = T->find(cur);
    const bool isPar = !T->kids[i].empty();
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
      if (isPar && s->isCollapsed(cur)) {
        s->setCollapsed(cur, false);
        c.afterEdit();
      } else {
        c.moveCursor(1, false, u.order);
      }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
      if (isPar && !s->isCollapsed(cur)) {
        s->setCollapsed(cur, true);
        c.afterEdit();
      } else if (T->parentOf[i] >= 0) {
        c.select(T->ids[T->parentOf[i]]);
      }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F2, false) || enterPressed(u)) beginEdit(f, cur, EditField::Name);
  }
  if (ImGui::IsKeyPressed(ImGuiKey_N, false) || ImGui::IsKeyPressed(ImGuiKey_Insert, false)) {
    const std::string id = (io.KeyShift && !cur.empty()) ? c.addChild() : c.addSibling();
    if (!id.empty()) beginEdit(f, id, EditField::Name, true);
  }
  if (ImGui::IsKeyPressed(ImGuiKey_Tab, false) && !c.selection().empty()) {
    if (io.KeyShift) c.outdentSelection();
    else c.indentSelection();
  }
  if ((ImGui::IsKeyPressed(ImGuiKey_Delete, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false)) &&
      !c.selection().empty())
    requestDelete(f, c.selection());
  if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) u.detailOpen = !u.detailOpen;
}

void drawHintBar(Frame& f, ImVec2 origin, float w) {
  AppController& c = f.c;
  UiState& u = f.u;
  if (c.prefs().hintDismissed || !c.session() || c.view() != View::Gantt) return;
  const float h = u.m.s(34);
  ImGui::SetNextWindowPos(ImVec2(origin.x, origin.y - h));
  ImGui::SetNextWindowSize(ImVec2(w, h));
  ImGui::PushStyleColor(ImGuiCol_WindowBg, v4(u.t->accentBg));
  ImGui::Begin("##hint", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                     ImGuiWindowFlags_NoFocusOnAppearing);
  ImGui::SetCursorPosY(u.m.s(8));
  ImGui::TextColored(v4(u.t->accent), "%s", S::kHint);
  ImGui::SameLine(w - u.m.s(40));
  ImGui::SetCursorPosY(u.m.s(3));
  if (iconButton(u, "hintx", "x", "閉じる")) c.dismissHint();
  ImGui::End();
  ImGui::PopStyleColor();
}

}  // namespace

const std::vector<ImWchar>& uiGlyphs() {
  static const std::vector<ImWchar> v = {
#include "ui/ui_glyphs.inc"
  };
  return v;
}

void collectGlyphs(const AppController& c, std::set<ImWchar>& out) {
  const Session* s = c.session();
  const auto add = [&](const std::string& str) {
    for (std::size_t i = 0; i < str.size();) {
      const char32_t cp = text::decodeAt(str, i);
      if (cp >= 0x80 && cp <= 0xFFFF) out.insert(static_cast<ImWchar>(cp));
    }
  };
  for (const auto& p : c.projects()) add(p.name);
  if (!s) return;
  for (const auto& o : s->viewIssues()) {
    add(o.summary);
    add(o.assignee);
    add(o.desc);
    for (const auto& l : o.labels) add(l);
    for (const auto& cm : o.comments) {
      add(cm.who);
      add(cm.text);
    }
  }
  for (const auto& w : s->allWarns()) add(w);
}

void drawApp(AppController& c, UiState& u, IPlatform& p) {
  Frame f{c, u, p};
  if (u.imeCooldown > 0) --u.imeCooldown;
  // その場編集を始めたが入力欄がまだアクティブでない間に打った文字は、取っておいてアクティブになったら入れる（B13）。
  // 編集を始めたフレームの文字（N など、始めるのに押したキー）は前のフレームで捨てられているので入らない
  // 入力欄が出ないまま（行が見えないなど）なら 30 フレームでやめる（ほかの入力を塞がないように）
  if (u.editField != EditField::None && !u.editActive && ImGui::GetFrameCount() - u.editArmFrame <= 30) {
    ImGuiIO& io = ImGui::GetIO();
    for (ImWchar ch : io.InputQueueCharacters) u.editPendingChars.push_back(ch);
    io.InputQueueCharacters.resize(0);
  }
  c.tick();
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(vp->WorkPos);
  ImGui::SetNextWindowSize(vp->WorkSize);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
  ImGui::Begin("##root", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
  ImGui::PopStyleVar(3);
  ImGui::PushFont(u.fontBody);

  drawTopBar(f);
  drawToolBar(f);
  drawBanners(f);

  const ImVec2 area = ImGui::GetContentRegionAvail();
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  Session* s = c.session();
  const float detailW = (u.detailOpen && s) ? std::min(u.m.s(400), area.x * 0.45f) : 0;
  const ImVec2 mainSize(area.x - detailW, area.y);
  ImGui::BeginChild("##main", mainSize, ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
  if (!s) {
    drawWelcome(f, mainSize);
  } else {
    switch (c.view()) {
      case View::Board: drawBoardView(f, mainSize); break;
      case View::List: drawListView(f, mainSize); break;
      case View::Goals: drawGoalsView(f, mainSize); break;
      default: drawGanttView(f, mainSize); break;
    }
  }
  ImGui::EndChild();
  if (detailW > 0) {
    ImGui::SameLine(0, 0);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, v4(u.t->surface));
    ImGui::BeginChild("##detail", ImVec2(detailW, area.y), ImGuiChildFlags_Border);
    drawDetailPanel(f, ImVec2(detailW, area.y));
    ImGui::EndChild();
    ImGui::PopStyleColor();
  }
  ImGui::PopFont();
  ImGui::End();

  ImGui::PushFont(u.fontBody);
  const ImVec2 bottomLeft(origin.x, origin.y + area.y);
  drawHintBar(f, bottomLeft, mainSize.x);
  drawBulkBar(f, bottomLeft, mainSize.x);
  {
    // ルートウィンドウの外でポップアップを扱うため、透明な土台に載せる
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(ImVec2(1, 1));
    ImGui::Begin("##overlay", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
    drawContextMenu(f);
    drawStatusMenu(f);
    drawDialogs(f);
    drawPalette(f);
    ImGui::End();
  }
  drawToasts(f);
  handleZoom(f);
  drawZoomHint(f);
  handleKeys(f);
  ImGui::PopFont();
  for (auto& fn : u.deferred) fn();  // 行・カードの途中で頼まれた操作（削除・子の追加）は、描画が終わってから
  u.deferred.clear();
  u.openColFilter.clear();  // 見出しが出ていない列の要求は捨てる
  c.afterEdit();
  c.releaseFrame();
}

}  // namespace roadmap::ui
