// 関連リンク（SPEC 10.10・Issue #60）: 詳細パネルの「添付」タブの中の「リンク」の節、行・カードのリンクのバッジ、リンクを開く操作。
// URL・パス・表示名は機微なので、ログには出さない（通知にも件数と理由だけを出す）。
#include <algorithm>

#include <imgui.h>
#include <imgui_stdlib.h>

#include "roadmap/attachments.hpp"
#include "roadmap/links.hpp"
#include "roadmap/text.hpp"
#include "ui/icons.hpp"
#include "ui/internal.hpp"

namespace roadmap::ui {

namespace {
constexpr std::size_t kNone = static_cast<std::size_t>(-1);
constexpr std::size_t kTipMaxItems = 6;  // バッジのツールチップに並べるリンクの数（先頭から）

void clearEdit(UiState& u) {
  u.linkEditing = false;
  u.linkEditIssue.clear();
  u.linkEditOrig.clear();
  u.linkEditIndex = 0;
}
}  // namespace

const char* linkIconName(LinkKind kind, bool risky) {
  if (risky) return "warn";
  switch (kind) {
    case LinkKind::Web: return "globe";
    case LinkKind::Folder: return "folder";
    default: return "file";
  }
}

void openLinkNow(Frame& f, const std::string& target) {
  std::string why;
  if (!f.p.openLink(target, &why)) f.c.toast("開けませんでした（" + (why.empty() ? std::string("理由は不明です") : why) + "）");
}

void openLinkAt(Frame& f, const std::string& issueId, std::size_t index, bool parentFolder) {
  const auto& list = f.c.linksOf(issueId);
  if (index >= list.size()) return;
  const std::string target = list[index].target;  // 写しを使う（このあとの操作で一覧が変わっても壊れない）
  // 許可リストにないものは開かない（読み込み時にも検査しているが、ここでも確かめる）
  const LinkClass k = classifyLink(target);
  if (!k.ok()) {
    f.c.toast("このリンクは開けません（" + k.reason + "）");
    return;
  }
  if (parentFolder) {
    const std::string dir = linkFolderOf(k.target);
    if (dir.empty()) {
      f.c.toast("この場所には、開く親のフォルダがありません");
      return;
    }
    openLinkNow(f, dir);
    return;
  }
  // 実行ファイルなどは、添付と同じく開く前に確認する（存在は確かめない）
  if (isRiskyTarget(k.target)) {
    f.u.linkWarnTarget = k.target;
    openDialog(f, Dlg::LinkOpenWarn);
    return;
  }
  openLinkNow(f, k.target);
}

int linkReorderSlot(const UiState& u, const std::string& issueId, ImVec2 pos) {
  if (u.linkRowsIssue != issueId || u.linkRows.empty() || ImGui::GetFrameCount() - u.linkRowsFrame > 1) return -1;
  const auto& first = u.linkRows.front();
  const auto& last = u.linkRows.back();
  const float pad = (first.second.y - first.first.y) / 2;
  const auto& clip = u.linkRowsClip;
  if (pos.x < clip.first.x || pos.x >= clip.second.x || pos.y < clip.first.y || pos.y >= clip.second.y) return -1;
  if (pos.x < first.first.x || pos.x >= first.second.x || pos.y < first.first.y - pad || pos.y >= last.second.y + pad) return -1;
  int slot = 0;
  for (const auto& r : u.linkRows)
    if ((r.first.y + r.second.y) / 2 < pos.y) ++slot;
  return slot;
}

namespace {
// バッジのツールチップ: 先頭から 6 件を 1 行ずつ（種類のアイコン・表示名・場所を薄く）。残りは「ほか N 件」。
// 描画中にリンクの一覧が変わらないよう、先に写してから描く。出した行は u.linkTipLines に残す（煙テスト用）
void drawLinkTooltip(Frame& f, const std::string& issueId) {
  UiState& u = f.u;
  const auto& list = f.c.linksOf(issueId);
  if (list.empty()) return;
  const std::size_t shown = std::min(list.size(), kTipMaxItems);
  std::vector<Link> items(list.begin(), list.begin() + static_cast<std::ptrdiff_t>(shown));
  const std::size_t more = list.size() - shown;
  u.linkTipLines.clear();
  ImGui::BeginTooltip();
  const std::string head = "リンク " + std::to_string(list.size()) + " 件（クリックで一覧）";
  ImGui::TextUnformatted(head.c_str());
  u.linkTipLines.push_back(head);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const float ic = u.m.s(14.f);
  for (const Link& l : items) {
    const LinkClass k = classifyLink(l.target);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    drawIcon(dl, linkIconName(k.kind, isRiskyTarget(l.target)), ImVec2(p.x, p.y + (ImGui::GetTextLineHeight() - ic) / 2), ic,
             u.t->muted);
    ImGui::SetCursorScreenPos(ImVec2(p.x + ic + u.m.s(6.f), p.y));
    const std::string title = ellipsize(l.title, u.m.s(220.f));
    ImGui::TextUnformatted(title.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("%s", ellipsize(l.target, u.m.s(260.f)).c_str());
    u.linkTipLines.push_back(l.title);
  }
  if (more > 0) {
    const std::string tail = "ほか " + std::to_string(more) + " 件";
    ImGui::TextDisabled("%s", tail.c_str());
    u.linkTipLines.push_back(tail);
  }
  ImGui::EndTooltip();
}
}  // namespace

float drawLinkBadge(Frame& f, const std::string& issueId, ImVec2 pos, float cy, int count, bool* clicked) {
  UiState& u = f.u;
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const std::string n = std::to_string(count);
  const float w = u.m.s(16) + ImGui::CalcTextSize(n.c_str()).x + u.m.s(8);
  bool hov = false;
  if (clicked) {
    const ImVec2 b0(pos.x - u.m.s(2), cy - u.m.s(9));
    ImGui::SetCursorScreenPos(b0);
    if (ImGui::InvisibleButton("##lnk", ImVec2(w, u.m.s(18)))) *clicked = true;
    hov = ImGui::IsItemHovered();
    u.linkBadgeBuild.push_back({b0, ImVec2(b0.x + w, b0.y + u.m.s(18)), issueId});
    // つかんでいる最中（添付・リンクのドラッグ）は出さない。載せて少し待ったときだけ
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort) && !u.attachDrag.active) drawLinkTooltip(f, issueId);
  }
  const ImU32 col = hov ? u.t->accent : u.t->muted;
  drawIcon(dl, "extlink", ImVec2(pos.x, cy - u.m.s(6)), u.m.s(12), col);
  dl->AddText(ImVec2(pos.x + u.m.s(14), cy - ImGui::GetTextLineHeight() / 2), col, n.c_str());
  return w;
}

void drawLinksSection(Frame& f, int i) {
  app::AppController& c = f.c;
  UiState& u = f.u;
  Session* s = c.session();
  const Issue& o = s->viewIssues()[i];
  const std::string id = o.id;
  const auto& list = c.linksOf(id);
  // 別の課題に切り替わったら、書きかけの入力・断った理由・編集中の状態を捨てる（書きかけの場所を別の課題に付けてしまわないように）
  if (!u.linkRowsIssue.empty() && u.linkRowsIssue != id) {
    clearEdit(u);
    u.linkTargetBuf.clear();
    u.linkTitleBuf.clear();
    u.linkError.clear();
  }
  // 編集中のリンクがなくなった・別の課題に切り替わったときは、編集をやめる
  if (u.linkEditing && (u.linkEditIssue != id || u.linkEditIndex >= list.size() || list[u.linkEditIndex].target != u.linkEditOrig)) {
    clearEdit(u);
    u.linkTargetBuf.clear();
    u.linkTitleBuf.clear();
  }
  u.linkCtxItems.clear();
  const float W = ImGui::GetContentRegionAvail().x;

  ImGui::SeparatorText(("リンク " + std::to_string(list.size()) + " 件").c_str());
  ImGui::PushTextWrapPos(0);
  ImGui::TextDisabled("URL・社内のファイル・フォルダの場所を付けます（ファイルはコピーしません）。");
  ImGui::TextDisabled("URL・パスがプロジェクトの設定ファイルに入ります。");
  ImGui::PopTextWrapPos();
  ImGui::Spacing();

  // ---- 入力欄 ----
  if (u.linkEditing) ImGui::TextColored(v4(u.t->accent), "リンクを編集中");
  if (u.linkFocusTarget) {
    ImGui::SetKeyboardFocusHere();
    u.linkFocusTarget = false;
  }
  ImGui::SetNextItemWidth(W);
  bool enter = ImGui::InputTextWithHint("##ltarget", "場所（https://… ・ \\\\サーバー\\共有\\… ・ C:\\…）", &u.linkTargetBuf,
                                        ImGuiInputTextFlags_EnterReturnsTrue);
  if (ImGui::IsItemEdited()) u.linkError.clear();
  u.linkTargetBox = {ImGui::GetItemRectMin(), ImGui::GetItemRectMax()};
  ImGui::SetNextItemWidth(W);
  enter |= ImGui::InputTextWithHint("##ltitle", "表示名（省略すると URL の末尾やファイル名）", &u.linkTitleBuf,
                                    ImGuiInputTextFlags_EnterReturnsTrue);
  u.linkTitleBox = {ImGui::GetItemRectMin(), ImGui::GetItemRectMax()};
  if (enter && u.imeComposing) enter = false;
  bool submit = enter;
  if (ImGui::Button(u.linkEditing ? "保存" : "リンクを追加")) submit = true;
  u.linkAddBtn = {ImGui::GetItemRectMin(), ImGui::GetItemRectMax()};
  bool paste = false, cancel = false;
  ImGui::SameLine();
  if (u.linkEditing) {
    if (ImGui::Button("やめる")) cancel = true;
    u.linkCancelBtn = {ImGui::GetItemRectMin(), ImGui::GetItemRectMax()};
  } else {
    if (ImGui::Button("貼り付けて追加")) paste = true;
    u.linkPasteBtn = {ImGui::GetItemRectMin(), ImGui::GetItemRectMax()};
    tip("クリップボードのテキストの各行を、1 行 1 リンクとして追加します（20 行まで）");
  }
  if (!u.linkError.empty()) {
    ImGui::PushTextWrapPos(0);
    ImGui::TextColored(v4(u.t->late), "%s", u.linkError.c_str());
    ImGui::PopTextWrapPos();
  }
  // 追加・編集・貼り付けは、描画が終わってから行う（描画中に一覧を変えない）
  if (submit) {
    const std::string target = u.linkTargetBuf, title = u.linkTitleBuf;
    deferAfterFrame(f, [&f, id, target, title] {
      UiState& us = f.u;
      std::string why;
      const bool editing = us.linkEditing && us.linkEditIssue == id;
      const bool ok = editing ? f.c.editLink(id, us.linkEditIndex, title, target, &why) : f.c.addLink(id, title, target, &why);
      if (ok) {
        clearEdit(us);
        us.linkTargetBuf.clear();
        us.linkTitleBuf.clear();
        us.linkError.clear();
      } else {
        us.linkError = why;
      }
    });
  }
  if (paste) {
    deferAfterFrame(f, [&f, id] {
      const auto clip = f.p.clipboardText();
      if (!clip || text::trim(*clip).empty()) {
        f.c.toast("クリップボードにテキストがありません");
        return;
      }
      f.c.addLinksFromText(id, *clip);
      f.u.linkError.clear();
    });
  }
  if (cancel) {
    clearEdit(u);
    u.linkTargetBuf.clear();
    u.linkTitleBuf.clear();
    u.linkError.clear();
  }

  // ---- 一覧 ----
  u.linkRows.clear();
  u.linkRowsIssue = id;
  u.linkRowsFrame = ImGui::GetFrameCount();
  u.linkRowsClip = {ImGui::GetWindowDrawList()->GetClipRectMin(), ImGui::GetWindowDrawList()->GetClipRectMax()};
  if (list.empty()) {
    ImGui::Spacing();
    ImGui::TextDisabled("リンクはまだありません。");
    return;
  }
  ImGui::Spacing();
  ImGui::PushTextWrapPos(0);
  ImGui::TextDisabled("クリックで開く／右クリックでメニュー／つかんで別の課題の行・カードへ離すと移動（Ctrl でコピー）。この一覧の中では並べ替え。");
  ImGui::PopTextWrapPos();
  ImGui::Separator();
  enum class Act { None, Open, OpenFolder, Copy, Edit, Delete, Up, Down };
  Act act = Act::None;
  std::size_t actIdx = kNone;
  const auto pick = [&act, &actIdx](Act a, std::size_t k) {
    act = a;
    actIdx = k;
  };
  const float btns = u.m.s(86);
  const float dragPx = u.m.s(5);
  for (std::size_t k = 0; k < list.size(); ++k) {
    const Link& l = list[k];
    ImGui::PushID(static_cast<int>(k));
    const LinkClass kind = classifyLink(l.target);
    const bool risky = isRiskyTarget(l.target);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float textH = ImGui::GetTextLineHeight() * 2 + u.m.s(2);
    const float rowH = textH + u.m.s(10);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    u.linkRows.emplace_back(p, ImVec2(p.x + W, p.y + rowH));
    const bool beingDragged = u.attachDrag.active && u.attachDrag.link && u.attachDrag.issueId == id && u.attachDrag.index == k;
    // 行全体: 押して離したら開く（つかんで動かしたときは開かない）。つかんで動かすと、別の課題へ移す・コピーする
    ImGui::SetNextItemAllowOverlap();
    const bool released = ImGui::InvisibleButton("##row", ImVec2(W - btns, rowH));
    const bool hov = ImGui::IsItemHovered();
    const bool hovDetail = ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort);
    const bool grabbed = ImGui::IsItemActive();
    if (released && ImGui::GetIO().MouseDragMaxDistanceSqr[0] < dragPx * dragPx) pick(Act::Open, k);
    ImGui::OpenPopupOnItemClick("##lctx", ImGuiPopupFlags_MouseButtonRight);
    if (hov && !u.attachDrag.active) {
      dl->AddRectFilled(p, ImVec2(p.x + W, p.y + rowH), u.t->hover, u.m.s(4));
      ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    if (grabbed && !u.attachDrag.active && !u.attachDragCancelled && ImGui::IsMouseDragging(ImGuiMouseButton_Left, dragPx)) {
      u.attachDrag.active = true;
      u.attachDrag.link = true;
      u.attachDrag.issueId = id;
      u.attachDrag.index = k;
      u.attachDrag.project = c.currentId();
    }
    // 左: 種類のアイコン（Web・ファイル・フォルダ。実行ファイルなどは警告の印）
    const float box = u.m.s(24);
    const ImVec2 b0(p.x + u.m.s(4), p.y + (rowH - box) / 2);
    drawIcon(dl, linkIconName(kind.kind, risky), ImVec2(b0.x + (box - u.m.s(18)) / 2, b0.y + (box - u.m.s(18)) / 2), u.m.s(18),
             risky ? u.t->tight : u.t->muted);
    const float tx = b0.x + box + u.m.s(8);
    const float tw = W - btns - (tx - p.x) - u.m.s(8);
    const float ty = p.y + (rowH - textH) / 2;
    dl->AddText(ImVec2(tx, ty), u.t->ink, ellipsize(l.title, tw).c_str());
    dl->AddText(ImVec2(tx, ty + ImGui::GetTextLineHeight() + u.m.s(2)), u.t->muted, ellipsize(l.target, tw).c_str());
    if (beingDragged) dl->AddRectFilled(p, ImVec2(p.x + W, p.y + rowH), withAlpha(u.t->surface, 0.55f));  // つかんでいる行は薄く
    // 行に載せて少し待つと、場所の全体（長いときは折り返す）
    if (hovDetail && !u.attachDrag.active && !ImGui::IsPopupOpen("##lctx")) {
      ImGui::BeginTooltip();
      ImGui::PushTextWrapPos(u.m.s(420));
      ImGui::TextUnformatted(l.title.c_str());
      ImGui::TextDisabled("%s", l.target.c_str());
      if (risky) ImGui::TextColored(v4(u.t->tight), "実行ファイルやスクリプトです。開く前に確認します");
      ImGui::PopTextWrapPos();
      ImGui::EndTooltip();
    }
    // 右クリックのメニュー
    if (ImGui::BeginPopup("##lctx")) {
      const bool hasFolder = !linkFolderOf(l.target).empty();
      const auto item = [&u](const char* label, bool enabled) {
        const bool hit = ImGui::MenuItem(label, nullptr, false, enabled);
        u.linkCtxItems.push_back({label, ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), enabled});
        return hit;
      };
      if (item("開く", true)) pick(Act::Open, k);
      if (item("場所（フォルダ）を開く", hasFolder)) pick(Act::OpenFolder, k);
      if (item("コピー", true)) pick(Act::Copy, k);
      ImGui::Separator();
      if (item("編集", true)) pick(Act::Edit, k);
      if (item("削除", true)) pick(Act::Delete, k);
      ImGui::Separator();
      if (item("上へ", k > 0)) pick(Act::Up, k);
      if (item("下へ", k + 1 < list.size())) pick(Act::Down, k);
      ImGui::EndPopup();
    }
    ImGui::SetCursorScreenPos(ImVec2(p.x + W - btns + u.m.s(4), p.y + (rowH - ImGui::GetFrameHeight()) / 2));
    if (ImGui::SmallButton("開く")) pick(Act::Open, k);
    if (risky) tip("実行ファイルやスクリプトです。開く前に確認します");
    ImGui::SameLine();
    if (iconButton(u, "del", "trash", "リンクを外す（場所のファイルや URL には何もしません）")) pick(Act::Delete, k);
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + rowH));
    ImGui::Separator();
    ImGui::PopID();
  }

  // 描画の途中でリンクの一覧を変えない（一覧への参照を使い終わってから）
  if (actIdx == kNone || actIdx >= list.size()) return;
  const std::size_t k = actIdx;
  switch (act) {
    case Act::Open: openLinkAt(f, id, k, false); break;
    case Act::OpenFolder: openLinkAt(f, id, k, true); break;
    case Act::Copy: {
      // 場所をクリップボードへ。通知にも場所は出さない
      if (f.p.setClipboardText(list[k].target)) c.toast("場所をクリップボードにコピーしました");
      else c.toast("クリップボードにコピーできませんでした");
      break;
    }
    case Act::Edit:
      u.linkEditing = true;
      u.linkEditIssue = id;
      u.linkEditIndex = k;
      u.linkEditOrig = list[k].target;
      u.linkTargetBuf = list[k].target;
      u.linkTitleBuf = list[k].title;
      u.linkError.clear();
      u.linkFocusTarget = true;
      break;
    case Act::Delete:
      deferAfterFrame(f, [&f, id, k] {
        f.c.removeLink(id, k);
        if (f.u.linkEditing && f.u.linkEditIssue == id) {  // 編集中のリンクの番号がずれる
          clearEdit(f.u);
          f.u.linkTargetBuf.clear();
          f.u.linkTitleBuf.clear();
        }
      });
      break;
    case Act::Up: deferAfterFrame(f, [&f, id, k] { f.c.reorderLink(id, k, k - 1); }); break;
    case Act::Down: deferAfterFrame(f, [&f, id, k] { f.c.reorderLink(id, k, k + 1); }); break;
    default: break;
  }
}

}  // namespace roadmap::ui
