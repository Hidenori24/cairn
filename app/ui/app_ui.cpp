// 全体のレイアウト（上部バー・ツールバー・バナー・作業領域・重ねて出すもの）とキーボード（SPEC 10.1〜10.4）
#include "ui/app_ui.hpp"

#include <algorithm>
#include <imgui.h>
#include <imgui_stdlib.h>

#include <imgui_internal.h>  // ActivateItemByID

#include "roadmap/link_candidates.hpp"
#include "roadmap/text.hpp"
#include "ui/icons.hpp"
#include "ui/internal.hpp"

namespace roadmap::ui {

using app::AppController;
using app::View;

// ---------------------------------------------------------------- 小道具
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
  // 読み取り専用のあいだは、課題やプロジェクトを変えるダイアログを開かない（理由を通知する）
  if (f.c.readOnly() && (d == Dlg::EditProject || d == Dlg::ConfirmDelete || d == Dlg::TplApply || d == Dlg::PasteChildren ||
                         d == Dlg::BaselineSave || d == Dlg::ConfirmIssueDelete)) {
    f.c.canEdit();
    return;
  }
  f.u.dlg = d;
  f.u.dlgOpening = true;
}

void openContext(Frame& f, const std::string& id) {
  if (!f.c.isSelected(id)) f.c.select(id);
  f.u.ctxId = id;
  f.u.ctxOpen = true;
}

void openStatusMenu(Frame& f, const std::string& id) {
  if (!f.c.canEdit()) return;
  f.u.statusMenuId = id;
  f.u.statusMenuOpen = true;
}

void openDetail(Frame& f, const std::string& id, int tab) {
  if (!id.empty()) f.c.select(id);
  f.u.detailOpen = true;
  f.u.detailTab = tab;
}

void beginLinkPick(Frame& f, const std::string& anchor, bool anchorIsPred) {
  if (!f.c.session() || !f.c.session()->issue(anchor)) return;
  f.u.linkPick = {true, anchor, anchorIsPred, f.c.currentId()};
}

void cancelLinkPick(Frame& f) { f.u.linkPick = {}; }

const std::vector<std::string>& recentLinks(const Frame& f) {
  static const std::vector<std::string> none;
  return f.u.recentLinkProject == f.c.currentId() ? f.u.recentLinks : none;
}

bool linkIssues(Frame& f, const std::string& anchor, bool anchorIsPred, const std::string& other, bool announce) {
  Session* s = f.c.session();
  if (!s || !s->tree()) return false;
  const Tree& T = *s->tree();
  const int a = T.find(anchor), b = T.find(other);
  if (a < 0 || b < 0) return false;
  const int pred = anchorIsPred ? a : b, succ = anchorIsPred ? b : a;
  const LinkBlock blk = checkLink(T, s->viewIssues(), pred, succ);
  if (blk != LinkBlock::None) {
    f.c.toast(std::string(T.did[b]) + " は選べません: " + linkBlockReason(blk));
    return false;
  }
  const std::string pd = T.did[pred], sd = T.did[succ];
  if (!s->addLink(T.ids[pred], T.ids[succ])) {
    f.c.afterEdit();
    return false;
  }
  f.c.afterEdit();
  // 最近つないだ相手として覚える
  if (f.u.recentLinkProject != f.c.currentId()) {
    f.u.recentLinkProject = f.c.currentId();
    f.u.recentLinks.clear();
  }
  auto& rl = f.u.recentLinks;
  rl.erase(std::remove(rl.begin(), rl.end(), other), rl.end());
  rl.insert(rl.begin(), other);
  if (rl.size() > 10) rl.resize(10);
  if (announce) f.c.toast(pd + " → " + sd + " をつなぎました", NoticeAction::Undo);
  return true;
}

bool createLinkedIssue(Frame& f, const std::string& anchor, bool succ, bool sameStatus) {
  Session* s = f.c.session();
  const Issue* a = s ? s->issue(anchor) : nullptr;
  if (!a) return false;
  const std::string parent = a->parent, status = a->status;  // 課題を足すと a は無効になる
  // 作成・状況・つなぎを Undo 1回にまとめる。途中で失敗したら作った課題も戻す（何も残さない）
  s->beginBatch();
  const std::string nid = f.c.createIssue(parent, anchor);
  bool ok = !nid.empty();
  if (ok && sameStatus && !status.empty() && s->issue(nid) && s->issue(nid)->status != status) s->setStatus(nid, status);
  if (ok) ok = linkIssues(f, anchor, succ, nid, false);
  if (!ok && !nid.empty()) s->deleteIssues({nid});
  s->endBatch();
  f.c.afterEdit();
  if (!ok) return false;
  if (const Tree* T = s->tree(); T && T->find(anchor) >= 0 && T->find(nid) >= 0) {
    const std::string ad = T->did[static_cast<std::size_t>(T->find(anchor))], nd = T->did[static_cast<std::size_t>(T->find(nid))];
    f.c.toast(succ ? ad + " → " + nd + " を作ってつなぎました" : nd + " → " + ad + " を作ってつなぎました", NoticeAction::Undo);
  }
  beginEdit(f, nid, EditField::Name, true);  // 件名をその場で決める
  return true;
}

void rowClick(Frame& f, const std::string& id) {
  if (f.u.linkPick.active) {  // 相手を選ぶモード: つなげたら抜ける。つなげないときは理由を出して続ける
    // 行・カードを描いている途中で課題を変えない（描画が終わってから）
    const std::string anchor = f.u.linkPick.anchor, target = id;
    const bool anchorIsPred = f.u.linkPick.anchorIsPred;
    deferAfterFrame(f, [&f, anchor, target, anchorIsPred] {
      if (!f.u.linkPick.active || f.u.linkPick.anchor != anchor) return;
      if (linkIssues(f, anchor, anchorIsPred, target, true)) cancelLinkPick(f);
    });
    return;
  }
  const ImGuiIO& io = ImGui::GetIO();
  if (io.KeyShift) f.c.rangeSelect(id, f.u.order);
  else if (io.KeyCtrl) f.c.toggleSelect(id);
  else f.c.select(id);
}

void requestDelete(Frame& f, std::vector<std::string> ids) {
  Session* s = f.c.session();
  if (!s || ids.empty() || !f.c.canEdit()) return;
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
  if (!s || !f.c.canEdit()) return;  // 読み取り専用では、その場編集を始めない
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
  } else if (field == EditField::Pred) {
    const int i = T ? T->find(id) : -1;
    f.u.editBuf = i >= 0 && s->computed() ? formatPredsForEdit(*T, s->viewIssues(), s->computed()->declPreds[static_cast<std::size_t>(i)]) : "";
    f.u.editCursorEnd = true;  // 既存の先行の後ろに足せるように、カーソルは末尾
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
struct EditCb {
  int tab = 0;
  bool toEnd = false;  // 最初のフレームでカーソルを末尾に置く
};
int tabCallback(ImGuiInputTextCallbackData* d) {
  auto* cb = static_cast<EditCb*>(d->UserData);
  if (d->EventFlag == ImGuiInputTextFlags_CallbackCompletion) cb->tab = ImGui::GetIO().KeyShift ? -1 : 1;
  else if (d->EventFlag == ImGuiInputTextFlags_CallbackAlways && cb->toEnd) {
    d->CursorPos = d->SelectionStart = d->SelectionEnd = d->BufTextLen;
    cb->toEnd = false;
  }
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
    } else if (f.u.editField == EditField::Pred) {
      const PredPlan pp = s->setPredecessors(id, v);  // 問題があれば何も変えない（呼び出し側が先に確かめる）
      changed = pp.ok && pp.changed();
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
  u.editCursorEnd = false;
  u.editPendingChars.clear();
}

// 先行列の入力欄の下（なければ上）に、エラーの理由（赤）か候補（薄く）を出す。操作はできない表示だけ
void drawPredHint(const UiState& u, const Tree& T, const std::vector<Issue>& issues, int self, const PredPlan& plan,
                  const std::string& buf, ImVec2 itemMin, ImVec2 itemMax) {
  std::vector<std::pair<std::string, bool>> lines;  // 文字, 赤か
  if (!plan.ok) {
    const std::size_t shown = std::min<std::size_t>(plan.problems.size(), 3);
    for (std::size_t i = 0; i < shown; ++i) lines.emplace_back(plan.problems[i].reason, true);
    if (plan.problems.size() > shown) lines.emplace_back("ほか" + std::to_string(plan.problems.size() - shown) + "件", true);
    lines.emplace_back("Enter では確定しません。直すか Esc で取り消します", false);
  }
  // 入力中の項目の候補（誤りがあるときも、直す手がかりとして出す）
  const auto sg = predSuggestions(T, issues, self, buf, plan.ok ? 5 : 3);
  if (!sg.empty()) {
    lines.emplace_back("候補（Tab で先頭を入力）", false);
    for (const int i : sg) lines.emplace_back(T.did[static_cast<std::size_t>(i)] + "  " + T.name[static_cast<std::size_t>(i)], false);
  }
  if (lines.empty()) return;
  const float pad = u.m.s(6);
  const float lh = ImGui::GetTextLineHeight();
  float w = 0;
  for (const auto& l : lines) w = std::max(w, ImGui::CalcTextSize(l.first.c_str()).x);
  const ImVec2 disp = ImGui::GetIO().DisplaySize;
  w = std::min(w, std::max(u.m.s(160), disp.x - 2 * pad - u.m.s(8)));
  const float h = lh * static_cast<float>(lines.size()) + 2 * pad;
  float y = itemMax.y + u.m.s(2);
  if (y + h > disp.y) y = itemMin.y - h - u.m.s(2);
  const float x = std::max(u.m.s(4), std::min(itemMin.x, disp.x - (w + 2 * pad) - u.m.s(4)));
  ImDrawList* fg = ImGui::GetForegroundDrawList();
  fg->PushClipRect(ImVec2(0, 0), disp, false);
  fg->AddRectFilled(ImVec2(x, y), ImVec2(x + w + 2 * pad, y + h), u.t->surface, u.m.s(4));
  fg->AddRect(ImVec2(x, y), ImVec2(x + w + 2 * pad, y + h), plan.ok ? u.t->line2 : u.t->late, u.m.s(4));
  float ty = y + pad;
  for (const auto& l : lines) {
    fg->PushClipRect(ImVec2(x + pad, ty), ImVec2(x + pad + w, ty + lh), true);
    fg->AddText(ImVec2(x + pad, ty), l.second ? u.t->late : u.t->muted, l.first.c_str());
    fg->PopClipRect();
    ty += lh;
  }
  fg->PopClipRect();
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
  EditCb cb;
  cb.toEnd = u.editCursorEnd;
  u.editCursorEnd = false;
  ImGuiInputTextFlags fl = ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_CallbackCompletion;
  if (u.editField == EditField::Dur) fl |= ImGuiInputTextFlags_CharsDecimal;
  if (u.editField == EditField::Pred) fl |= ImGuiInputTextFlags_CallbackAlways;  // カーソルを末尾に置く
  if (ImGui::GetActiveID() == ImGui::GetID("##e") && !u.editPendingChars.empty()) {
    // アクティブになる前に打った文字を、このフレームの入力の前に入れる
    ImGuiIO& io = ImGui::GetIO();
    ImVector<ImWchar> q;
    for (ImWchar ch : u.editPendingChars) q.push_back(ch);
    for (ImWchar ch : io.InputQueueCharacters) q.push_back(ch);
    io.InputQueueCharacters.swap(q);
    u.editPendingChars.clear();
  }
  ImGui::InputText("##e", &u.editBuf, fl, tabCallback, &cb);
  const int tab = cb.tab;
  const ImVec2 itemMin = ImGui::GetItemRectMin(), itemMax = ImGui::GetItemRectMax();
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
  // 先行列: 入力が変わっていれば検証する。問題があれば確定しない（理由は入力欄の近くに赤で出す）
  const Tree* tr = s->tree();
  const bool isPred = field == EditField::Pred && tr;
  const bool predEdited = isPred && text::trim(u.editBuf) != text::trim(u.editStartValue);
  PredPlan pp;
  if (predEdited) pp = planPredEdit(*tr, s->viewIssues(), tr->find(id), u.editBuf);
  const bool predBlocked = predEdited && !pp.ok;
  const auto hint = [&] {
    if (isPred && (predBlocked || predEdited))
      drawPredHint(u, *tr, s->viewIssues(), tr->find(id), pp, u.editBuf, itemMin, itemMax);
  };
  if (enterPressed(u) && (active || deactivated)) {
    if (predBlocked) {  // 確定せず、入力を続けさせる
      u.editFocus = true;
      u.editCursorEnd = true;
      hint();
      return true;
    }
    const bool fresh = u.editFresh;
    commitEdit(f);
    endEdit(u);
    if (field == EditField::Name && (io.KeyShift || io.KeyCtrl || fresh)) {
      // 続けて課題を足す（Shift = 子、それ以外 = 兄弟）。行・カードを描いている途中で課題の数を変えない（描画が終わってから）
      const bool asChild = io.KeyShift;
      deferAfterFrame(f, [&f, id, asChild] {
        const Session* ss = f.c.session();
        const Issue* o = ss ? ss->issue(id) : nullptr;
        if (!o) return;
        const std::string nid = asChild ? f.c.createIssue(id, "") : f.c.createIssue(o->parent, id);
        if (!nid.empty()) beginEdit(f, nid, EditField::Name, true);
      });
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
  if (tab > 0 && isPred) {  // Tab: 入力中の項目を、先頭の候補で置き換える
    const std::string next = completePredText(*tr, s->viewIssues(), tr->find(id), u.editBuf);
    if (next != u.editBuf) {
      u.editBuf = next;
      u.editFocus = true;
      u.editCursorEnd = true;
    }
    return true;
  }
  if (active && !io.KeyAlt && !predBlocked && (ImGui::IsKeyPressed(ImGuiKey_UpArrow) || ImGui::IsKeyPressed(ImGuiKey_DownArrow))) {
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
    if (predBlocked) f.c.toast("先行の入力に誤りがあるため、反映しませんでした");
    else commitEdit(f);
    endEdit(u);
    return false;
  }
  hint();
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

namespace {
// その場編集中の入力欄の上に、親の名前（`要件定義 › 認証 ›`）を薄く出す。行の高さは変えない。
// 上に置けない（表の先頭で見出しに隠れる）ときは行の下に置く
void drawEditParentHint(const UiState& u, const Tree& T, int idx, ImVec2 inputPos, float rowH) {
  const std::string tail = " ›";
  const ImVec2 clipMin = ImGui::GetWindowDrawList()->GetClipRectMin();
  const ImVec2 clipMax = ImGui::GetWindowDrawList()->GetClipRectMax();
  const float pad = u.m.s(6);
  const float maxW = std::max(u.m.s(80), std::min(u.m.s(420), clipMax.x - inputPos.x - u.m.s(8)) - 2 * pad);
  const float tailW = ImGui::CalcTextSize(tail.c_str()).x;
  const std::string path = ancestorLabel(T, idx, maxW - tailW);
  if (path.empty()) return;
  const std::string label = path + tail;
  const ImVec2 ts = ImGui::CalcTextSize(label.c_str());
  const ImVec2 sz(ts.x + 2 * pad, ts.y + u.m.s(4));
  float y = inputPos.y - sz.y;
  if (y < clipMin.y) y = inputPos.y + rowH;
  const ImVec2 a(std::max(clipMin.x, inputPos.x - u.m.s(2)), y);
  const ImVec2 b(a.x + sz.x, a.y + sz.y);
  ImDrawList* fg = ImGui::GetForegroundDrawList();
  fg->PushClipRect(clipMin, clipMax, false);
  fg->AddRectFilled(a, b, u.t->surface, u.m.s(4));
  fg->AddRect(a, b, u.t->line2, u.m.s(4));
  fg->AddText(ImVec2(a.x + pad, a.y + u.m.s(2)), u.t->muted, label.c_str());
  fg->PopClipRect();
}
}  // namespace

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
    drawEditParentHint(u, T, r.id, ImVec2(x, pos.y), size.y);
    return;
  }
  const int nc = static_cast<int>(is.comments.size());
  const int na = static_cast<int>(f.c.attachmentCount(id));
  const int nl = static_cast<int>(f.c.linkCountOf(id));
  const bool hov = ImGui::IsMouseHoveringRect(pos, ImVec2(pos.x + size.x, pos.y + size.y)) && o.showHoverButtons;
  float avail = right - x - (nc ? u.m.s(30) : 0) - (na ? u.m.s(30) : 0) - (nl ? u.m.s(30) : 0) - (hov ? u.m.s(72) : 0);
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
    x += u.m.s(30);
  }
  if (na) {
    bool clicked = false;
    ImGui::PushID(id.c_str());
    x += drawAttachBadge(f, id, ImVec2(x, pos.y), cy, na, &clicked) + u.m.s(8);
    ImGui::PopID();
    if (clicked) openDetail(f, id, 3);
  }
  if (nl) {  // 関連リンクの数（添付のクリップとは別のアイコン。Issue #60）
    bool clicked = false;
    ImGui::PushID(id.c_str());
    x += drawLinkBadge(f, id, ImVec2(x, pos.y), cy, nl, &clicked) + u.m.s(8);
    ImGui::PopID();
    if (clicked) openDetail(f, id, 3);
  }
  if (o.showPath && r.depth > 0) {
    // 平らな一覧: 件名の右に、親の名前を薄く。残りの幅に収まらなければ真ん中を省略
    const float room = right - x - (hov ? u.m.s(72) : 0);
    if (room >= u.m.s(60)) {
      const std::string path = ancestorLabel(T, r.id, room);
      if (!path.empty()) {
        const ImVec2 psz = ImGui::CalcTextSize(path.c_str());
        dl->AddText(ImVec2(x, cy - psz.y / 2), withAlpha(u.t->muted, 0.85f), path.c_str());
        if (ImGui::IsMouseHoveringRect(ImVec2(x, pos.y), ImVec2(x + psz.x, pos.y + size.y)) && ImGui::IsWindowHovered())
          ImGui::SetTooltip("親: %s", text::join(ancestorNames(T, r.id), " › ").c_str());
      }
    }
  }
  if (hov) {
    ImGui::PushID(id.c_str());
    bool del = false;
    if (!f.c.readOnly()) {  // 読み取り専用では、削除と子の追加は出さない
      // 削除（子も消える。通知の「元に戻す」で戻せる）
      ImGui::SetCursorScreenPos(ImVec2(right - u.m.s(72), cy - u.m.s(12)));
      const ImVec2 b0 = ImGui::GetCursorScreenPos();
      if (ImGui::InvisibleButton("##del", ImVec2(u.m.s(22), u.m.s(22)))) del = true;
      const bool delHov = ImGui::IsItemHovered();
      tip(r.isPar ? "削除（子の課題も削除。元に戻せます）" : "削除（元に戻せます）");
      drawIcon(dl, "trash", ImVec2(b0.x + u.m.s(3), b0.y + u.m.s(3)), u.m.s(16), delHov ? u.t->late : u.t->muted);
      ImGui::SetCursorScreenPos(ImVec2(right - u.m.s(48), cy - u.m.s(12)));
      const ImVec2 b = ImGui::GetCursorScreenPos();
      if (ImGui::InvisibleButton("##add", ImVec2(u.m.s(22), u.m.s(22)))) {
        deferAfterFrame(f, [&f, id] {
          if (!f.c.session()) return;
          const std::string nid = f.c.createIssue(id, "");
          if (!nid.empty()) beginEdit(f, nid, EditField::Name, true);
        });
      }
      tip("子を追加");
      drawIcon(dl, "plus", ImVec2(b.x + u.m.s(3), b.y + u.m.s(3)), u.m.s(16), u.t->muted);
    }
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
  const float rowH = H / 2;
  float by = p0.y + (rowH - u.m.s(28)) / 2;
  const std::string saved = c.saveState() == app::SaveState::Error ? S::kSaveError : c.saveState() == app::SaveState::Saving ? S::kSaving
      : std::string(S::kSaved) + (c.savedAt().empty() ? "" : " " + c.savedAt());
  const std::string scaleLabel = std::to_string(c.prefs().uiScale) + "% ▼";
  const float rightW = ImGui::CalcTextSize(saved.c_str()).x + ImGui::CalcTextSize("レポート ▼").x +
      ImGui::CalcTextSize(S::kAddIssue).x + ImGui::CalcTextSize(scaleLabel.c_str()).x + u.m.s(16 + 28 * 4 + 20 * 3 + 34);
  const float rx = p0.x + W - rightW - u.m.s(12);
  struct QuickAction { const char* id; const char* label; const char* icon; bool enabled; };
  const std::vector<QuickAction> quickActions = {
      {"view-gantt", S::kViewGantt, "gantt", s != nullptr}, {"view-board", S::kViewBoard, "board", s != nullptr},
      {"view-list", S::kViewList, "list", s != nullptr}, {"view-goals", S::kViewGoals, "target", s != nullptr},
      {"view-stats", S::kViewStats, "chart", s != nullptr}, {"view-today", S::kViewToday, "sun", s != nullptr},
      {"add-issue", S::kAddIssue, "plus", s != nullptr && !c.readOnly()},
      {"html", "HTML レポート", "download", s && s->tree()}, {"weekly", "週次レポート", "calendar", s && s->tree()},
      {"csv", "CSV・共有用 JSON", "download", s != nullptr}, {"workload", "担当者の負荷", "chart", s != nullptr}};
  bool openQuickAccessFromProject = false;
  u.headerControls.clear();
  u.reportMenuItems.clear();
  const auto remember = [&](const std::string& id) {
    u.headerControls[id] = {ImGui::GetItemRectMin(), ImGui::GetItemRectMax()};
  };
  ImGui::SetCursorScreenPos(ImVec2(p0.x + u.m.s(12), by));

  // プロジェクト切替
  {
    const std::string name = s ? s->settings().name : "プロジェクト";
    const std::string key = s ? s->settings().key : "";
    const std::string label = ellipsize(name + (key.empty() ? "" : "  [" + key + "]"), std::min(W * 0.28f, u.m.s(240))) + "  ▼";
    if (iconTextButton(u, "proj", "folder", label.c_str())) ImGui::OpenPopup("##projmenu");
    remember("project");
    ImGui::SetNextWindowSize(ImVec2(u.m.s(300), 0));
    if (ImGui::BeginPopup("##projmenu")) {
      const auto menuItem = [&](const AppController::ProjectItem& pi) {
        const std::string l = pi.name + (pi.key.empty() ? "" : "  [" + pi.key + "]") + "##" + pi.id;
        if (ImGui::MenuItem(l.c_str(), nullptr, pi.id == c.currentId())) c.openProject(pi.id);
      };
      const auto recent = c.recentProjects();
      const auto all = c.projects();
      if (!recent.empty()) {
        ImGui::SeparatorText("最近使ったプロジェクト");
        for (const auto& pi : recent) menuItem(pi);
        ImGui::Separator();
      }
      if (!all.empty()) {
        ImGui::SeparatorText("すべてのプロジェクト");
        for (const auto& pi : all) menuItem(pi);
      }
      ImGui::Separator();
      if (ImGui::MenuItem("新しいプロジェクト")) openDialog(f, Dlg::NewProject);
      if (ImGui::MenuItem("名前・キー・削除", nullptr, false, s != nullptr && !c.readOnly())) openDialog(f, Dlg::EditProject);
      ImGui::Separator();
      if (ImGui::MenuItem(S::kImportCsv)) openDialog(f, Dlg::Import);
      if (ImGui::MenuItem(S::kExport, nullptr, false, s != nullptr)) openDialog(f, Dlg::Export);
      ImGui::Separator();
      if (ImGui::MenuItem("クイックアクセスを設定…")) openQuickAccessFromProject = true;
      ImGui::EndPopup();
    }
  }
  if (openQuickAccessFromProject) ImGui::OpenPopup("##quickaccess");
  // よく使う操作をプロジェクト切替の右に固定する。設定は端末ごとに保存。
  ImGui::SameLine(0, u.m.s(6));
  const float quickStart = ImGui::GetCursorScreenPos().x;
  const float quickRoom = std::max(0.0f, rx - quickStart - u.m.s(6));
  const int visibleQuickCount = std::min<int>(static_cast<int>(c.prefs().quickAccess.size()),
      std::max(0, static_cast<int>((quickRoom - u.m.s(40)) / u.m.s(30))));
  for (int i = 0; i < visibleQuickCount; ++i) {
    const std::string& id = c.prefs().quickAccess[static_cast<std::size_t>(i)];
    const auto it = std::find_if(quickActions.begin(), quickActions.end(), [&](const QuickAction& a) { return id == a.id; });
    if (it == quickActions.end()) continue;
    ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, by));
    if (iconButton(u, ("quick:" + id).c_str(), it->icon, it->label, it->enabled)) {
      if (id == "view-gantt") c.setView(View::Gantt);
      else if (id == "view-board") c.setView(View::Board);
      else if (id == "view-list") c.setView(View::List);
      else if (id == "view-goals") c.setView(View::Goals);
      else if (id == "view-stats") c.setView(View::Stats);
      else if (id == "view-today") c.setView(View::Today);
      else if (id == "add-issue") {
        const std::string issue = c.createIssue("", "");
        if (!issue.empty()) beginEdit(f, issue, EditField::Name, true);
      } else if (id == "html") openDialog(f, Dlg::ExportHtml);
      else if (id == "weekly") openDialog(f, Dlg::Weekly);
      else if (id == "csv") { u.exportKind = "app"; openDialog(f, Dlg::Export); }
      else if (id == "workload") openDialog(f, Dlg::Workload);
    }
    ImGui::SameLine(0, u.m.s(2));
  }
  if (quickRoom >= u.m.s(36)) {
    ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, by));
    if (iconButton(u, "quick-config", "sliders", "クイックアクセスを設定")) ImGui::OpenPopup("##quickaccess");
  }
  if (ImGui::BeginPopup("##quickaccess")) {
    if (!u.imeComposing && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
      ImGui::CloseCurrentPopup();
      u.popupEscFrame = ImGui::GetFrameCount();
    }
    ImGui::TextDisabled("上部に固定する操作（最大5件）");
    for (std::size_t i = 0; i < quickActions.size(); ++i) {
      const auto& action = quickActions[i];
      if (i == 6 || i == 7 || i == 10) ImGui::Separator();
      const auto& pinned = c.prefs().quickAccess;
      const bool selected = std::find(pinned.begin(), pinned.end(), action.id) != pinned.end();
      const bool enabled = selected || pinned.size() < 5;
      if (ImGui::MenuItem(action.label, nullptr, selected, enabled)) {
        auto& ids = c.prefs().quickAccess;
        const auto found = std::find(ids.begin(), ids.end(), action.id);
        if (found == ids.end()) ids.push_back(action.id);
        else ids.erase(found);
        c.savePrefs();
      }
    }
    ImGui::EndPopup();
  }
  // 右側: 保存状態 / Ctrl+K / 元に戻す / やり直す / ＋課題 / その他
  ImGui::SameLine(0, 0);
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
    remember("save");
    tip("保存先: " + c.dataDir() + (c.dataDirFallback ? "（既定の場所に書けないため）" : ""));
  }
  ImGui::SameLine(0, u.m.s(6));
  ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, by));
  if (iconButton(u, "palette", "cmd", "コマンドパレット (Ctrl+K)")) {
    u.paletteOpen = true;
    u.paletteFocus = true;
  }
  remember("palette");
  ImGui::SameLine(0, u.m.s(2));
  const bool ro = c.readOnly();
  if (iconButton(u, "undo", "undo", "元に戻す (Ctrl+Z)", s && s->canUndo() && !ro)) c.undo();
  remember("undo");
  ImGui::SameLine(0, u.m.s(2));
  if (iconButton(u, "redo", "redo", "やり直す (Ctrl+Shift+Z)", s && s->canRedo() && !ro)) c.redo();
  remember("redo");
  ImGui::SameLine(0, u.m.s(6));
  if (iconTextButton(u, "reports", nullptr, "レポート ▼", false, false, s != nullptr)) ImGui::OpenPopup("##reports");
  remember("reports");
  tip("HTML 報告・週報・CSV を書き出す");
  const ImVec2 reportAnchor = u.headerControls["reports"].second;
  ImGui::SetNextWindowPos(ImVec2(reportAnchor.x, reportAnchor.y + u.m.s(4)), ImGuiCond_Appearing, ImVec2(1, 0));
  if (ImGui::BeginPopup("##reports")) {
    if (!u.imeComposing && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
      ImGui::CloseCurrentPopup();
      u.popupEscFrame = ImGui::GetFrameCount();
    }
    ImGui::TextDisabled("共有・報告");
    const auto item = [&](const char* label, Dlg dlg, const char* kind = nullptr, bool enabled = true) {
      if (ImGui::MenuItem(label, nullptr, false, enabled)) {
        if (kind) u.exportKind = kind;
        openDialog(f, dlg);
      }
      u.reportMenuItems.push_back({label, ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), enabled});
    };
    item("HTML レポート…", Dlg::ExportHtml, nullptr, s && s->tree());
    item("週次レポート…", Dlg::Weekly, nullptr, s && s->tree());
    ImGui::Separator();
    item("CSV・共有用 JSON…", Dlg::Export, "app");
    item("全プロジェクトをバックアップ…", Dlg::Export, "backup");
    ImGui::EndPopup();
  }
  ImGui::SameLine(0, u.m.s(6));
  if (iconTextButton(u, "add", nullptr, S::kAddIssue, true, false, s != nullptr && !ro)) {
    const std::string id = c.createIssue("", "");
    if (!id.empty()) beginEdit(f, id, EditField::Name, true);
  }
  remember("add");
  ImGui::SameLine(0, u.m.s(6));
  if (iconTextButton(u, "scale", nullptr, scaleLabel.c_str())) ImGui::OpenPopup("##scale");
  remember("scale");
  tip("文字とボタンの大きさを変更します（Windows の表示倍率に追加）");
  const ImVec2 scaleAnchor = u.headerControls["scale"].second;
  ImGui::SetNextWindowPos(ImVec2(scaleAnchor.x, scaleAnchor.y + u.m.s(4)), ImGuiCond_Appearing, ImVec2(1, 0));
  if (ImGui::BeginPopup("##scale")) {
    if (!u.imeComposing && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
      ImGui::CloseCurrentPopup();
      u.popupEscFrame = ImGui::GetFrameCount();
    }
    ImGui::TextDisabled("文字・ボタンの大きさ");
    for (int pct : {100, 125, 150, 175, 200}) {
      const std::string label = std::to_string(pct) + (pct == 100 ? "%（標準）" : "%");
      if (ImGui::MenuItem(label.c_str(), nullptr, c.prefs().uiScale == pct)) c.setUiScale(pct);
      remember("scale:" + std::to_string(pct));
    }
    ImGui::Separator();
    if (ImGui::MenuItem("表示設定…")) openDialog(f, Dlg::AppSettings);
    ImGui::TextDisabled("Ctrl+ホイールで調整 / Ctrl+0で100%%");
    ImGui::EndPopup();
  }
  ImGui::SameLine(0, u.m.s(4));
  if (iconButton(u, "more", "more", S::kMore)) ImGui::OpenPopup("##more");
  remember("more");
  if (ImGui::BeginPopup("##more")) {
    if (ImGui::MenuItem("アプリの設定…", "Ctrl+,")) openDialog(f, Dlg::AppSettings);
    if (ImGui::MenuItem("プロジェクト設定", nullptr, false, s != nullptr)) openDialog(f, Dlg::Settings);
    if (ImGui::MenuItem("担当者の負荷", nullptr, false, s != nullptr)) openDialog(f, Dlg::Workload);
    if (ImGui::MenuItem("読み込み時の注意", nullptr, false, s != nullptr)) openDialog(f, Dlg::Warnings);
    ImGui::Separator();
    if (ImGui::MenuItem("ベースラインを保存…", nullptr, false, s != nullptr && s->tree() != nullptr && !c.readOnly())) openDialog(f, Dlg::BaselineSave);
    if (ImGui::MenuItem("ベースラインの管理…", nullptr, false, s != nullptr)) openDialog(f, Dlg::BaselineManage);
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
  // 表示の切り替え・検索は、プロジェクトの操作と別の行に置く。
  dl->AddLine(ImVec2(p0.x + u.m.s(12), p0.y + rowH), ImVec2(p0.x + W - u.m.s(12), p0.y + rowH), u.t->line);
  by += rowH;
  ImGui::SetCursorScreenPos(ImVec2(p0.x + u.m.s(12), by));
  // ビュー切替
  const View cur = c.view();
  const struct {
    View v;
    const char* icon;
    const char* label;
  } tabs[] = {{View::Gantt, "gantt", S::kViewGantt}, {View::Board, "board", S::kViewBoard},
              {View::List, "list", S::kViewList}, {View::Goals, "target", S::kViewGoals}, {View::Stats, "chart", S::kViewStats},
              {View::Today, "sun", S::kViewToday}, {View::Personal, "user", "個人報告"}};
  const float searchW = std::clamp(W * 0.25f, u.m.s(180), u.m.s(360));
  float labelledW = searchW + u.m.s(24);
  for (const auto& tab : tabs) labelledW += ImGui::CalcTextSize(tab.label).x + u.m.s(42);
  if (s && s->tree()) {
    const Summary sm = s->summary();
    const std::string summary = std::string(S::kLate) + " " + std::to_string(sm.late) + S::kTight + " " + std::to_string(sm.tight) +
        "完了 " + std::to_string(sm.done) + " / 全体 " + std::to_string(sm.total) + S::kGoal;
    labelledW += ImGui::CalcTextSize(summary.c_str()).x + u.m.s(108);
  }
  const bool narrow = W < labelledW;
  for (const auto& t : tabs) {
    ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, by));
    const bool pressed = narrow ? iconButton(u, t.label, t.icon, t.label, s != nullptr, cur == t.v)
                                : iconTextButton(u, t.label, t.icon, t.label, false, cur == t.v, s != nullptr);
    if (pressed) c.setView(t.v);
    remember(std::string("view:") + t.label);
    if (!narrow) tip(t.label);
    ImGui::SameLine(0, u.m.s(2));
  }
  ImGui::SameLine(0, u.m.s(12));
  // 絞り込み欄
  {
    ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, by));
    const float w = searchW;
    const ImVec2 q = ImGui::GetCursorScreenPos();
    if (u.focusSearch) {
      ImGui::SetWindowFocus();  // Esc で外したフォーカスも戻してから入力欄へ移す
      ImGui::SetKeyboardFocusHere();
      u.focusSearch = false;
    }
    const float clearButtonW = c.filter().q.empty() ? 0.f : u.m.s(28);
    ImGui::SetNextItemWidth(w - clearButtonW);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(u.m.s(26), u.m.s(5)));
    ImGui::InputTextWithHint("##q", S::kSearchHint, &c.filter().q);
    ImGui::PopStyleVar();
    remember("search");
    // InputText は Esc を受けたフレームで非アクティブになる。そのフレームも解除する。
    if ((ImGui::IsItemActive() || ImGui::IsItemDeactivated()) && !u.imeComposing && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
      c.filter().q.clear();
      ImGui::SetWindowFocus(nullptr);
    }
    drawIcon(dl, "search", ImVec2(q.x + u.m.s(6), q.y + u.m.s(6)), u.m.s(14), u.t->muted);
    if (!c.filter().q.empty()) {
      ImGui::SameLine(0, 0);
      ImGui::SetCursorScreenPos(ImVec2(q.x + w - clearButtonW, q.y + u.m.s(2)));
      if (iconButton(u, "clrq", "x", "消す")) c.filter().q.clear();
      remember("searchClear");
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
    remember("late");
    ImGui::SameLine(0, u.m.s(4));
    if (chipButton(u, "tight", std::string(S::kTight) + " " + std::to_string(sm.tight), u.t->tight, u.t->tightBg, sm.tight == 0))
      flt = flt == FltFilter::Tight ? FltFilter::None : FltFilter::Tight;
    remember("tight");
    ImGui::SameLine(0, u.m.s(4));
    chip(u, "完了 " + std::to_string(sm.done) + " / 全体 " + std::to_string(sm.total), u.t->muted, u.t->surface3);
    remember("summary");
    // ゴールボタン（ルート課題が1つもなければ出さない）
    const Tree& T = *s->tree();
    bool anyRoot = false;
    for (int r : T.roots) anyRoot |= !T.kids[r].empty() || isEpic(s->viewIssues()[r]);
    if (anyRoot) {
      ImGui::SameLine(0, u.m.s(6));
      ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, by));
      if (iconTextButton(u, "goal", "flag", S::kGoal)) ImGui::OpenPopup("##goalpop");
      remember("goal");
      drawGoalPopover(f);
    }
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
  const float W = ImGui::GetContentRegionAvail().x;
  // 狭いガントでは横スクロールバーの分だけ高さを取り、操作の下端を切らない。
  const bool scrollRoom = s && c.view() == View::Gantt && W < u.m.s(s->baselines().list.empty() ? 1280.f : 1450.f);
  const float H = u.m.toolBar() + (scrollRoom ? ImGui::GetStyle().ScrollbarSize : 0.f);
  dl->AddRectFilled(p0, ImVec2(p0.x + W, p0.y + H), u.t->surface2);
  dl->AddLine(ImVec2(p0.x, p0.y + H - 1), ImVec2(p0.x + W, p0.y + H - 1), u.t->line);
  ImGui::SetCursorScreenPos(ImVec2(p0.x, p0.y));
  std::string planLabel = "計画: " + (s ? s->settings().baseDate : std::string("基準日"));
  if (s && s->settings().scenario != kStdScenario) planLabel += " / " + ellipsize(s->settings().scenario, u.m.s(80));
  if (s && s->settings().resource.on) planLabel += " / 制限 " + std::to_string(s->settings().resource.cap);
  planLabel += " ▼";
  const float planW = ImGui::CalcTextSize(planLabel.c_str()).x + u.m.s(40);
  const auto warns = s ? s->allWarns() : std::vector<std::string>{};
  const float warnW = warns.empty() ? 0.f : ImGui::CalcTextSize(("注意 " + std::to_string(warns.size())).c_str()).x + u.m.s(44);
  ImGui::BeginChild("##toolbar", ImVec2(std::max(u.m.s(160), W - planW - warnW - u.m.s(24)), H), ImGuiChildFlags_None,
                    ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground);
  const float by = (u.m.toolBar() - std::max(ImGui::GetFrameHeight(), u.m.s(28))) / 2;
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
  if ((v == View::Gantt || v == View::List || v == View::Stats) && !s->baselines().list.empty()) {
    // 表示するベースライン（計画との差の基準）。ガント・リスト・統計と、HTML・週次レポートに効く
    ImGui::SameLine();
    std::vector<std::pair<std::string, std::string>> items = {{"", "計画: 表示しない"}};
    for (const Baseline& b : s->baselines().list) items.emplace_back(b.id, "計画: " + b.name);
    std::string cur = s->baselines().shown, pv = "計画: 表示しない";
    for (const auto& [k, l] : items)
      if (k == cur) pv = l;
    if (combo("##baseline", u.m.s(170), pv.c_str(), items, cur)) c.showBaseline(cur);
    tip("計画（ベースライン）と比べて、ずれた分を出します");
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
      ImGui::SameLine();
      const bool progressFill = c.uiBool("progress", "fill", false), progressLine = c.uiBool("progress", "line", false);
      if (iconTextButton(u, "progress", nullptr, "進捗 ▼", false, progressFill || progressLine)) ImGui::OpenPopup("##progress");
      if (ImGui::BeginPopup("##progress")) {
        if (ImGui::MenuItem("バー内の進捗", nullptr, progressFill)) c.setUiSub("progress", "fill", !progressFill);
        if (ImGui::MenuItem("イナズマ線", nullptr, progressLine)) c.setUiSub("progress", "line", !progressLine);
        ImGui::TextDisabled("予定は選択した計画と基準日で比較します");
        ImGui::TextDisabled("実績未入力・計画なしの行では線を切ります");
        ImGui::TextDisabled("バー内: 実績の塗り / 予定の印。外枠: 期限の余裕");
        ImGui::EndPopup();
      }
      // 依存の見せ方（選んだ課題の連鎖をたどる・余裕 0 の連鎖を強調）。どちらかがオンなら押した見た目にする
      ImGui::SameLine();
      const bool depTrace = c.uiBool("dep", "trace", false), depCrit = c.uiBool("dep", "crit", false);
      if (iconTextButton(u, "dep", nullptr, "依存 ▼", false, depTrace || depCrit)) ImGui::OpenPopup("##dep");
      if (ImGui::BeginPopup("##dep")) {
        if (ImGui::MenuItem("選んだ課題の先行・後続をたどる（ほかを薄く）", nullptr, depTrace)) c.setUiSub("dep", "trace", !depTrace);
        if (ImGui::MenuItem("クリティカルパスを強調（余裕 0）", nullptr, depCrit)) c.setUiSub("dep", "crit", !depCrit);
        ImGui::EndPopup();
      }
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
  ImGui::EndChild();
  // 日程の計算条件は横スクロールの外に置き、いつでも開けるようにする。
  float x = p0.x + W - planW - warnW - u.m.s(12);
  const float y = p0.y + (u.m.toolBar() - u.m.s(28)) / 2;
  if (!warns.empty()) {
    ImGui::SetCursorScreenPos(ImVec2(x, y));
    if (iconTextButton(u, "warns", "warn", ("注意 " + std::to_string(warns.size())).c_str())) openDialog(f, Dlg::Warnings);
    x += warnW;
  }
  ImGui::SetCursorScreenPos(ImVec2(x, y));
  if (iconTextButton(u, "plan", "calendar", planLabel.c_str())) ImGui::OpenPopup("##planmenu");
  u.headerControls["plan"] = {ImGui::GetItemRectMin(), ImGui::GetItemRectMax()};
  tip("基準日・シナリオ・担当者の同時作業の制限");
  const ImVec2 planAnchor = u.headerControls["plan"].second;
  ImGui::SetNextWindowPos(ImVec2(planAnchor.x, planAnchor.y + u.m.s(4)), ImGuiCond_Appearing, ImVec2(1, 0));
  if (ImGui::BeginPopup("##planmenu")) {
    if (!u.imeComposing && !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
      ImGui::CloseCurrentPopup();
      u.popupEscFrame = ImGui::GetFrameCount();
    }
    ImGui::TextDisabled("日程の計算条件");
    ImGui::BeginDisabled(c.readOnly());  // プロジェクトの設定は、読み取り専用では変えられない
    {
      ImGui::TextUnformatted("基準日");
      ImGui::SetNextItemWidth(u.m.s(150));
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
    const bool today = ImGui::Button("今日にする");
    u.headerControls["planToday"] = {ImGui::GetItemRectMin(), ImGui::GetItemRectMax()};
    if (today) {
      s->setBaseDate(c.clock().todayIso());
      c.afterEdit();
    }
    ImGui::Spacing();
    {
      ImGui::TextUnformatted("シナリオ");
      const Tree* T = s->tree();
      std::vector<std::pair<std::string, std::string>> items;
      if (T)
        for (const auto& sc : T->scenarios) items.emplace_back(sc, sc);
      else items.emplace_back(kStdScenario, kStdScenario);
      std::string cur = s->settings().scenario;
      if (combo("##scn", u.m.s(240), ("シナリオ: " + cur).c_str(), items, cur)) {
        s->setScenario(cur);
        c.afterEdit();
      }
    }
    ImGui::Spacing();
    {
      const ResourceLimit r = s->settings().resource;
      const std::string l = r.on ? "同時作業の制限 " + std::to_string(r.cap) : "同時作業の制限";
      if (iconTextButton(u, "res", "user", l.c_str(), false, r.on)) {
        s->setResource({!r.on, r.cap});
        c.afterEdit();
      }
      tip("担当者ごとの同時作業の制限（同時に持てる件数はプロジェクト設定で変えられます）");
    }
    ImGui::Spacing();
    if (ImGui::Button("プロジェクト設定…")) {
      ImGui::CloseCurrentPopup();
      openDialog(f, Dlg::Settings);
    }
    ImGui::EndDisabled();
    ImGui::EndPopup();
  }
  ImGui::SetCursorScreenPos(ImVec2(p0.x, p0.y + H));
  ImGui::Dummy(ImVec2(0, 0));
}

// ---------------------------------------------------------------- バナー・通知
// 読み取り専用の帯（他の人が編集中・ロックを作れない・ロックを失った。Issue #62）。
// 「◯◯さん（マシン名）が 14:05 から編集中です。読み取り専用で開いています」と、「再確認」「編集を引き継ぐ」
void drawReadOnlyBand(Frame& f) {
  AppController& c = f.c;
  UiState& u = f.u;
  const AppController::LockStatus& st = c.lockStatus();
  const std::string msg = c.readOnlyBand();
  const ImVec2 p0 = ImGui::GetCursorScreenPos();
  const float W = ImGui::GetContentRegionAvail().x, H = u.m.s(40);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const bool warn = st.kind == AppController::LockKind::Failed;
  const ImU32 fg = warn ? u.t->late : u.t->tight, bg = warn ? u.t->lateBg : u.t->tightBg;
  dl->AddRectFilled(p0, ImVec2(p0.x + W, p0.y + H), bg);
  dl->AddLine(ImVec2(p0.x, p0.y + H - 1), ImVec2(p0.x + W, p0.y + H - 1), warn ? u.t->late : u.t->tightEdge);
  drawIcon(dl, "lock", ImVec2(p0.x + u.m.s(12), p0.y + (H - u.m.s(16)) / 2), u.m.s(16), fg);
  const bool canTake = st.kind == AppController::LockKind::Other || st.kind == AppController::LockKind::Lost;
  const char* const kRecheck = "再確認";
  const char* const kTake = "編集を引き継ぐ";
  const float wRecheck = ImGui::CalcTextSize(kRecheck).x + u.m.s(20), wTake = ImGui::CalcTextSize(kTake).x + u.m.s(20);
  const float btnsW = wRecheck + (canTake ? wTake + u.m.s(8) : 0) + u.m.s(12);
  const float textW = std::max(u.m.s(60), W - btnsW - u.m.s(48));
  dl->AddText(ImVec2(p0.x + u.m.s(36), p0.y + (H - ImGui::GetTextLineHeight()) / 2), u.t->ink, ellipsize(msg, textW).c_str());
  ImGui::SetCursorScreenPos(ImVec2(p0.x + u.m.s(36), p0.y));
  ImGui::InvisibleButton("##roband", ImVec2(textW, H));  // 長い文言は、マウスを載せると全文を出す
  if (ImGui::IsItemHovered()) tip(msg);
  float x = p0.x + W - u.m.s(12);
  const float by = p0.y + (H - u.m.s(28)) / 2;
  if (canTake) {
    x -= wTake;
    ImGui::SetCursorScreenPos(ImVec2(x, by));
    if (iconTextButton(u, "ro_takeover", nullptr, kTake)) openDialog(f, Dlg::ConfirmTakeOver);
    u.roTakeoverBtn = {ImGui::GetItemRectMin(), ImGui::GetItemRectMax()};
    x -= u.m.s(8);
  } else {
    u.roTakeoverBtn = {{0, 0}, {0, 0}};
  }
  x -= wRecheck;
  ImGui::SetCursorScreenPos(ImVec2(x, by));
  if (iconTextButton(u, "ro_recheck", nullptr, kRecheck)) c.recheckLock();
  u.roRecheckBtn = {ImGui::GetItemRectMin(), ImGui::GetItemRectMax()};
  u.roBandText = msg;
  u.roBandShown = true;
  ImGui::SetCursorScreenPos(ImVec2(p0.x, p0.y + H));
  ImGui::Dummy(ImVec2(0, 0));
}

void drawBanners(Frame& f) {
  AppController& c = f.c;
  UiState& u = f.u;
  Session* s = c.session();
  u.roBandShown = false;
  if (s && c.readOnly()) drawReadOnlyBand(f);
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
  // 依存の相手を画面で選ぶモード
  if (u.linkPick.active) {
    const Tree* T = s ? s->tree() : nullptr;
    const int ai = T ? T->find(u.linkPick.anchor) : -1;
    if (ai < 0 || c.currentId() != u.linkPick.project) {
      cancelLinkPick(f);
    } else {
      const ImVec2 p0 = ImGui::GetCursorScreenPos();
      const float W = ImGui::GetContentRegionAvail().x, H = u.m.s(34);
      ImDrawList* dl = ImGui::GetWindowDrawList();
      dl->AddRectFilled(p0, ImVec2(p0.x + W, p0.y + H), u.t->accentBg);
      const std::string who = "「" + ellipsize(T->did[ai] + " " + T->name[ai], u.m.s(260)) + "」";
      const std::string msg = u.linkPick.anchorIsPred ? who + " を先行にします。後続にする課題をクリックしてください（Esc で中止）"
                                                      : who + " を後続にします。先行にする課題をクリックしてください（Esc で中止）";
      dl->AddText(ImVec2(p0.x + u.m.s(12), p0.y + (H - ImGui::GetTextLineHeight()) / 2), u.t->accent,
                  ellipsize(msg, W - u.m.s(120)).c_str());
      ImGui::SetCursorScreenPos(ImVec2(p0.x + W - u.m.s(90), p0.y + u.m.s(3)));
      if (iconTextButton(u, "linkpick_cancel", nullptr, "中止")) cancelLinkPick(f);
      ImGui::SetCursorScreenPos(ImVec2(p0.x, p0.y + H));
      ImGui::Dummy(ImVec2(0, 0));
    }
  }
}

void drawToasts(Frame& f) {
  AppController& c = f.c;
  UiState& u = f.u;
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  float y = vp->WorkPos.y + vp->WorkSize.y - u.m.s(16);
  const auto toasts = c.toasts();
  for (auto it = toasts.rbegin(); it != toasts.rend(); ++it) {
    const auto& t = *it;
    // 「フォルダを開く」は「元に戻す」より幅が広いので、通知の幅とボタンの位置を広げる（元に戻す・動作なしの通知は変えない）。
    // 完了の提案（リンク先の PR・Issue がすべて閉じた。Issue #61）は文が長く、注意書きまで読んでもらうので、幅を広げて折り返す。
    // 長すぎて入りきらない文も、省略せずに折り返す（失敗の理由を読めるように）
    const bool complete = t.action == NoticeAction::Complete && !t.arg.empty();
    const bool openFolder = t.action == NoticeAction::OpenFolder && !t.arg.empty();
    const char* const folderLabel = complete ? "完了にする" : "フォルダを開く";
    const float folderW = ImGui::CalcTextSize(folderLabel).x + u.m.s(20);
    const float textW = ImGui::CalcTextSize(t.text.c_str()).x;
    const float cap = openFolder ? u.m.s(640) : u.m.s(520);
    const float pad = (openFolder || complete) ? u.m.s(70) + folderW : u.m.s(130);  // 文の右に残す幅（ボタン・閉じる）
    const bool wrap = complete || textW + pad > cap;
    float w = openFolder ? std::min(cap, textW + u.m.s(70) + folderW) : std::min(cap, textW + u.m.s(140));
    if (complete) w = std::min(u.m.s(720), vp->WorkSize.x - u.m.s(32));
    else if (wrap) w = cap;
    const float wrapW = w - pad;
    // 描くときは PushTextWrapPos(wrapW)（窓の左端からの位置）で、文字は左の余白 padL から始まるので、折り返す幅は wrapW - padL。
    // 高さを測るときも同じ幅で測る（広く測ると、描くほうが先に折り返して 1 行増え、窓の高さが足りず末尾が切れる）
    const float padL = u.m.s(12);
    const float textH = wrap ? ImGui::CalcTextSize(t.text.c_str(), nullptr, false, wrapW - padL).y : ImGui::GetTextLineHeight();
    const float h = complete ? std::max(u.m.s(64), textH + u.m.s(24)) : wrap ? std::max(u.m.s(40), textH + u.m.s(20)) : u.m.s(40);
    const float btnY = wrap ? h / 2 - ImGui::GetFrameHeight() / 2 : u.m.s(6);
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
    if (wrap) {
      ImGui::SetCursorPos(ImVec2(padL, (h - textH) / 2));
      ImGui::PushTextWrapPos(wrapW);
      ImGui::TextUnformatted(t.text.c_str());
      ImGui::PopTextWrapPos();
    } else {
      ImGui::SetCursorPosY(u.m.s(10));
      ImGui::TextUnformatted(ellipsize(t.text, openFolder ? w - u.m.s(70) - folderW : w - u.m.s(130)).c_str());
    }
    ImGui::PopStyleColor();
    if (complete) {
      // 押すまで課題は変わらない。押したら描画が終わってから完了にする（Undo 1回で戻る）
      ImGui::SameLine(w - u.m.s(50) - folderW);
      ImGui::SetCursorPosY(btnY);
      if (ImGui::Button(folderLabel)) {
        const int id = t.id;
        deferAfterFrame(f, [&f, id] { f.c.runToastAction(id); });
      }
      u.toastActionBtn = {ImGui::GetItemRectMin(), ImGui::GetItemRectMax()};
    } else if (t.action == NoticeAction::Undo) {
      ImGui::SameLine(w - u.m.s(120));
      ImGui::SetCursorPosY(btnY);
      if (ImGui::SmallButton(S::kUndo)) c.runToastAction(t.id);
    } else if (openFolder) {
      ImGui::SameLine(w - u.m.s(50) - folderW);
      ImGui::SetCursorPosY(btnY);
      if (ImGui::SmallButton(folderLabel)) {
        const std::string folder = t.arg;
        const int id = t.id;
        deferAfterFrame(f, [&f, folder, id] {
          f.p.openFolder(folder);
          f.c.dismissToast(id);
        });
      }
    }
    ImGui::SameLine(w - u.m.s(36));
    ImGui::SetCursorPosY(btnY);
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
  const bool ro = c.readOnly();
  if (multi) ImGui::TextDisabled("%d件を選択", static_cast<int>(sel.size()));
  else {
    if (ImGui::MenuItem("詳細を開く", "Space")) openDetail(f, id, 0);
    ImGui::BeginDisabled(ro);  // 読み取り専用では、変更する項目を使えない
    if (ImGui::MenuItem("名前を変更", "F2")) beginEdit(f, id, EditField::Name);
    ImGui::Separator();
    if (ImGui::MenuItem("子を追加", "Shift+N")) {
      const auto nid = c.createIssue(id, "");
      if (!nid.empty()) beginEdit(f, nid, EditField::Name, true);
    }
    if (ImGui::MenuItem("同じ階層に追加", "N")) {
      const auto nid = c.createIssue(s->issue(id)->parent, id);
      if (!nid.empty()) beginEdit(f, nid, EditField::Name, true);
    }
    ImGui::EndDisabled();
  }
  ImGui::BeginDisabled(ro);
  if (ImGui::MenuItem("テンプレから子タスクを追加…")) openTplApply(f);
  if (!multi && ImGui::MenuItem("この子タスクをテンプレとして保存…", nullptr, false, !T->kids[idx].empty())) openTplSave(f, id);
  if (!multi) {
    ImGui::Separator();
    if (ImGui::MenuItem("先行にする…", "後続を選ぶ")) beginLinkPick(f, id, true);
    if (ImGui::MenuItem("後続にする…", "先行を選ぶ")) beginLinkPick(f, id, false);
  }
  ImGui::Separator();
  ImGui::TextDisabled("状況");
  for (const char* st : {S::kStatusTodo, S::kStatusProg, S::kStatusDone}) {
    const bool curSt = !multi && (s->issue(id)->status.empty() ? std::string(S::kStatusTodo) : s->issue(id)->status) == st;
    if (ImGui::MenuItem(st, nullptr, curSt)) c.setStatusFor(sel, st);
  }
  ImGui::Separator();
  if (multi) {
    // 画面の表示順に A→B→C と、終了→開始でつなぐ（循環・重複は core が見る）
    ImGui::TextDisabled("先行・後続");
    if (ImGui::MenuItem("順につなぐ（表示順・終了→開始）")) c.chainSelection(u.order);
    if (ImGui::MenuItem("つなぎを外す（選んだ課題どうし）")) c.unchainSelection();
    ImGui::Separator();
  }
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
  ImGui::EndDisabled();
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
  ImGui::SetNextWindowSize(ImVec2(std::min(w - u.m.s(32), u.m.s(1160)), h));
  ImGui::PushStyleColor(ImGuiCol_WindowBg, v4(u.t->surface));
  ImGui::Begin("##bulk", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
  ImGui::AlignTextToFramePadding();
  ImGui::Text("%d件を選択", static_cast<int>(sel.size()));
  ImGui::BeginDisabled(c.readOnly());  // 読み取り専用では、まとめて変える操作を使えない（選択解除だけ使える）
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
  if (ImGui::SmallButton("順につなぐ")) c.chainSelection(u.order);
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("表示順に A→B→C と、終了→開始でつなぎます");
  ImGui::SameLine();
  if (ImGui::SmallButton("つなぎを外す")) c.unchainSelection();
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("選んだ課題どうしのつなぎを外します");
  ImGui::SameLine();
  if (ImGui::SmallButton("階層を下げる")) c.indentSelection();
  ImGui::SameLine();
  if (ImGui::SmallButton("階層を上げる")) c.outdentSelection();
  ImGui::SameLine();
  ImGui::PushStyleColor(ImGuiCol_Text, v4(u.t->late));
  if (ImGui::SmallButton("削除")) requestDelete(f, c.selection());
  ImGui::PopStyleColor();
  ImGui::EndDisabled();
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
  ImGui::BeginChild("##welcome", ImVec2(w, u.m.s(390)), ImGuiChildFlags_Border);
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
  if (ImGui::Button("組込みソフトウェア開発の雛形", bs)) c.createProject(app::SampleKind::EmbeddedSw, "組込みソフトウェア開発", "DEV");
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
    if (u.popupEscFrame == ImGui::GetFrameCount()) return;  // 上部のメニューを閉じた Esc
    if (u.linkMenu.escFrame == ImGui::GetFrameCount()) return;  // 依存のメニューを閉じた Esc（選択などは変えない）
    if (u.attachDragEscFrame == ImGui::GetFrameCount()) return;  // 添付のドラッグを取り消した Esc（選択などは変えない）
    if (u.attachDrag.active) {  // 添付のサムネイルのドラッグを取り消し
      u.attachDrag = {};
      u.attachDragCancelled = true;  // ボタンを離すまで、同じつかみ方から始め直さない
      return;
    }
    if (u.linkDrag.active) {  // バーの端からのドラッグ（依存をつなぐ）を取り消し
      u.linkDrag = {};
      return;
    }
    if (u.arrowDrag.pending || u.arrowDrag.active) {  // 依存の矢印のドラッグ（付け替え・削除）を取り消し
      u.arrowDrag = {};
      return;
    }
    if (u.linkPick.active) {  // 依存の相手を選ぶモードを中止
      cancelLinkPick(f);
      return;
    }
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
  // 入力欄の外の Ctrl+V: クリップボードの複数行を、選んだ課題の子としてまとめて作る（テキストがなく画像があれば、その画像を添付）
  if (s && io.KeyCtrl && !io.KeyShift && !io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_V, false)) {
    pasteFromClipboard(f);
    return;
  }
  if (io.KeyCtrl || !s) return;
  if (!io.KeyShift && !io.KeyAlt) {
    if (ImGui::IsKeyPressed(ImGuiKey_1, false)) c.setView(View::Gantt);
    if (ImGui::IsKeyPressed(ImGuiKey_2, false)) c.setView(View::Board);
    if (ImGui::IsKeyPressed(ImGuiKey_3, false)) c.setView(View::List);
    if (ImGui::IsKeyPressed(ImGuiKey_4, false)) c.setView(View::Goals);
    if (ImGui::IsKeyPressed(ImGuiKey_5, false)) c.setView(View::Stats);
    if (ImGui::IsKeyPressed(ImGuiKey_6, false)) c.setView(View::Today);
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
  if (c.prefs().hintDismissed || !c.session() || c.view() != View::Gantt || c.readOnly()) return;  // 読み取り専用では、編集のヒントを出さない
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
  add(c.readOnlyBand());  // ロックの持ち主の名前・マシン名（課題のデータに出てこない字のことがある。Issue #62）
  add(c.prefs().me);
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
  for (const auto& [id, list] : s->links())  // 関連リンクの表示名・場所（日本語のパスなど。Issue #60）
    for (const auto& l : list) {
      add(l.title);
      add(l.target);
    }
}

void drawApp(AppController& c, UiState& u, IPlatform& p) {
  u.m.modern = c.prefs().appearance == "modern";
  u.m.animate = u.m.modern && c.prefs().motion == "subtle" && !p.reducedMotion();
  applyStyle(*u.t, u.m);
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
  u.ro = c.readOnly();
  if (u.ro) {  // 読み取り専用に切り替わったら、途中の編集・ドラッグを捨てる
    u.editField = EditField::None;
    u.barDrag = {};
    u.rowDrag = {};
    u.linkDrag = {};
    u.arrowDrag = {};
    u.linkPick = {};
    u.attachDrag = {};
  }
  if (c.currentId() != u.imageProject) {  // プロジェクトを切り替えたら、画像のテクスチャを全部捨てる
    releaseImages(f);
    u.imageProject = c.currentId();
    // リンクの入力欄・編集中の状態・警告中の場所も捨てる（別のプロジェクトの場所を持ち越さない）
    u.linkTargetBuf.clear();
    u.linkTitleBuf.clear();
    u.linkError.clear();
    u.linkEditing = false;
    u.linkEditIssue.clear();
    u.linkEditOrig.clear();
    u.linkWarnTarget.clear();
  }
  processDrop(f);  // エクスプローラーからのドロップ（前のフレームに描いたドロップ先と照らす）
  processAttachDrag(f);  // 添付のサムネイルのドラッグ（離した位置を、同じく前のフレームのドロップ先と照らす）
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
  drawActiveFilters(f);
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
      case View::Stats: drawStatsView(f, mainSize); break;
      case View::Today: drawTodayView(f, mainSize); break;
      case View::Personal: drawPersonalView(f, mainSize); break;
      default: drawGanttView(f, mainSize); break;
    }
  }
  if (u.linkDrag.active && (!s || c.view() != u.linkDrag.view)) u.linkDrag = {};  // 始めたビューが描かれない間にドラッグを残さない
  if ((u.arrowDrag.pending || u.arrowDrag.active) && (!s || c.view() != View::Gantt)) u.arrowDrag = {};
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
    drawLinkMenu(f);
    drawDialogs(f);
    drawPalette(f);
    ImGui::End();
  }
  drawToasts(f);
  drawAttachDrag(f);  // ドラッグ中の添付をカーソルに付ける（いちばん手前）
  handleZoom(f);
  drawZoomHint(f);
  handleKeys(f);
  ImGui::PopFont();
  for (auto& fn : u.deferred) fn();  // 行・カードの途中で頼まれた操作（削除・子の追加）は、描画が終わってから
  u.deferred.clear();
  trimImages(f);  // しばらく使っていない画像のテクスチャを解放する
  u.dropTargets = std::move(u.dropBuild);  // 次のフレームのドロップ判定に使う
  u.dropBuild.clear();
  u.attachBadges = std::move(u.attachBadgeBuild);
  u.attachBadgeBuild.clear();
  u.linkBadges = std::move(u.linkBadgeBuild);
  u.linkBadgeBuild.clear();
  u.openColFilter.clear();  // 見出しが出ていない列の要求は捨てる
  c.afterEdit();
  c.releaseFrame();
}

}  // namespace roadmap::ui
