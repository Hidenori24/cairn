// ボード（SPEC 10.6）: 末端の課題をカードにし、状況ごとの列に並べる。カードを別の列へドラッグで状況を変える
#include <algorithm>
#include <map>

#include <imgui_internal.h>

#include "roadmap/text.hpp"
#include "ui/icons.hpp"
#include "ui/internal.hpp"

namespace roadmap::ui {

namespace {
struct DragCard {
  std::string id;
  bool active = false;
} gCardDrag;

void drawCard(Frame& f, int id, float w, const std::vector<int>& rootIdx, const LinkOverlayOpts& lo) {
  app::AppController& c = f.c;
  UiState& u = f.u;
  Session* s = c.session();
  const Tree& T = *s->tree();
  const ScheduleRow& r = (*s->rows())[id];
  const Issue& o = s->viewIssues()[id];
  const std::string& sid = T.ids[id];
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const float lh = ImGui::GetTextLineHeight();
  const float h = u.m.s(14) + lh * 3 + u.m.s(14);
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const ImVec2 q(p.x + w, p.y + h);
  const bool sel = c.isSelected(sid);
  // 依存をつなぐハンドル（カードの左右の端。右 → 後続、左 → 先行）。ハンドルを押したときはカードのクリックにもドラッグにもしない
  int handleSide = 0;
  {
    UiState::LinkSlot sl;
    sl.idx = id;
    sl.a = p;
    sl.b = q;
    sl.padX = u.m.s(10);
    const float mid = (p.y + q.y) / 2;
    sl.hl = ImVec2(p.x - u.m.s(2), mid);
    sl.hr = ImVec2(q.x + u.m.s(2), mid);
    sl.landR = ImVec2(p.x - u.m.s(2), mid);  // 右のハンドルで来た線はカードの左の端へ
    sl.landL = ImVec2(q.x + u.m.s(2), mid);
    sl.sameL = ImVec2(sl.hl.x - u.m.s(kLinkHandleR), mid);  // 同じ列のカードへは、同じ側の端へ回り込む
    sl.sameR = ImVec2(sl.hr.x + u.m.s(kLinkHandleR), mid);
    handleSide = linkSlotAdd(f, sl, lo);
  }
  const bool linking = handleSide != 0 || u.linkDrag.active;
  ImGui::PushID(sid.c_str());
  registerDropTarget(f, sid, p, q);  // カードへファイルをドロップすると添付
  ImGui::SetNextItemAllowOverlap();
  ImGui::InvisibleButton("##card", ImVec2(w, h));
  const bool hov = ImGui::IsItemHovered();
  const bool inCard = ImGui::IsMouseHoveringRect(p, q) && ImGui::IsWindowHovered() && !gCardDrag.active && !linking;
  if (!linking) {
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) rowClick(f, sid);
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) openContext(f, sid);
    if (hov && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) openDetail(f, sid, 0);
    if (!u.linkPick.active && ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 5.f))
      gCardDrag = {sid, true};
  }
  dl->AddRectFilled(p, q, sel ? u.t->accentBg : u.t->surface, u.m.s(6));
  dl->AddRect(p, q, sel ? u.t->accent : (hov ? u.t->line2 : u.t->line), u.m.s(6));
  dl->AddRectFilled(p, ImVec2(p.x + u.m.s(3), q.y), u.t->c[rootIdx[id]], u.m.s(6), ImDrawFlags_RoundCornersLeft);
  // 1行目: 種別・表示ID・余裕
  const float x0 = p.x + u.m.s(10);
  float y = p.y + u.m.s(7);
  ImGui::SetCursorScreenPos(ImVec2(x0, y));
  typeIcon(u, o.type);
  ImGui::PushFont(u.fontMono);
  dl->AddText(ImVec2(x0 + u.m.s(20), y), u.t->muted, T.did[id].c_str());
  ImGui::PopFont();
  {
    const std::string ft = floatText(r);
    const float fw = ImGui::CalcTextSize(ft.c_str()).x + u.m.s(12);
    ImGui::SetCursorScreenPos(ImVec2(q.x - fw - u.m.s(8), y - u.m.s(1)));
    floatChip(u, r);
  }
  y += lh + u.m.s(3);
  const bool editName = editing(u, sid, EditField::Name);
  if (editName) {
    // 作った直後の課題の件名をその場で決める（Enter で確定・Esc でやめる）
    ImGui::SetCursorScreenPos(ImVec2(x0, y - u.m.s(2)));
    drawInlineEdit(f, w - u.m.s(20));
  } else {
    dl->AddText(ImVec2(x0, y), u.t->ink, ellipsize(T.name[id], w - u.m.s(inCard ? 44.f : 20.f)).c_str());
  }
  bool del = false;
  if (inCard && !editName) {
    // 削除（通知の「元に戻す」で戻せる）
    const ImVec2 b(q.x - u.m.s(28), y - u.m.s(3));
    ImGui::SetCursorScreenPos(b);
    if (ImGui::InvisibleButton("##del", ImVec2(u.m.s(22), u.m.s(22)))) del = true;
    const bool delHov = ImGui::IsItemHovered();
    tip("削除（元に戻せます）");
    drawIcon(dl, "trash", ImVec2(b.x + u.m.s(3), b.y + u.m.s(3)), u.m.s(16), delHov ? u.t->late : u.t->muted);
  }
  y += lh;
  // 祖先の名前
  {
    std::vector<std::string> anc;
    for (int a = T.parentOf[id]; a >= 0; a = T.parentOf[a]) anc.insert(anc.begin(), T.name[a]);
    dl->AddText(ImVec2(x0, y), u.t->muted, ellipsize(text::join(anc, " › "), w - u.m.s(20)).c_str());
  }
  y += lh + u.m.s(2);
  // 下段: 優先度・コメント数・日付・担当
  {
    const std::string pr = o.prio.empty() ? "Medium" : o.prio;
    dl->AddText(ImVec2(x0, y), prioColor(u, pr), prioMark(pr));
    float x = x0 + u.m.s(30);
    if (!o.comments.empty()) {
      drawIcon(dl, "message", ImVec2(x, y + u.m.s(1)), u.m.s(12), u.t->muted);
      dl->AddText(ImVec2(x + u.m.s(14), y), u.t->muted, std::to_string(o.comments.size()).c_str());
      x += u.m.s(30);
    }
    if (const int na = static_cast<int>(c.attachmentCount(sid)); na > 0) {
      bool clicked = false;
      x += drawAttachBadge(f, sid, ImVec2(x, y + u.m.s(1)), y + lh / 2 + u.m.s(1), na, &clicked) + u.m.s(8);
      if (clicked) openDetail(f, sid, 3);
    }
    ImGui::PushFont(u.fontMono);
    const std::string dates = fmtShort(r.asapStart) + "〜" + fmtShort(r.asapFinish);
    dl->AddText(ImVec2(x, y), u.t->muted, dates.c_str());
    ImGui::PopFont();
    if (!o.assignee.empty()) {
      ImGui::SetCursorScreenPos(ImVec2(q.x - u.m.s(28), y - u.m.s(2)));
      avatar(u, o.assignee);
    }
  }
  ImGui::PopID();
  ImGui::SetCursorScreenPos(ImVec2(p.x, q.y + u.m.s(8)));
  ImGui::Dummy(ImVec2(w, 0));
  if (del) deferAfterFrame(f, [&f, sid] { requestDelete(f, {sid}); });  // 描画の途中で課題を減らさない
}
}  // namespace

void drawBoardView(Frame& f, ImVec2 size) {
  app::AppController& c = f.c;
  UiState& u = f.u;
  Session* s = c.session();
  if (!s->tree()) return;
  const Tree& T = *s->tree();
  const auto& I = s->viewIssues();
  const auto leaves = c.visibleLeaves();
  const std::vector<char> dim = c.dimmedSet();
  u.order.clear();
  for (int id : leaves) u.order.push_back(T.ids[id]);
  const std::vector<int> rootIdx = rootIndexMap(T, I);

  // 列: To Do / In Progress / Done + それ以外の状況（出現順。大文字小文字を無視して同一視）
  std::vector<std::string> colNames = {S::kStatusTodo, S::kStatusProg, S::kStatusDone};
  for (const auto& o : I) {
    if (o.status.empty()) continue;
    bool found = false;
    for (const auto& cn : colNames) found |= text::toLower(cn) == text::toLower(o.status);
    if (!found) colNames.push_back(o.status);
  }
  const auto colOf = [&](int id) -> int {
    const std::string st = I[id].status;
    if (st.empty()) return 0;
    for (std::size_t k = 0; k < colNames.size(); ++k)
      if (text::toLower(colNames[k]) == text::toLower(st)) return static_cast<int>(k);
    return 0;
  };
  // スイムレーン
  const std::string lane = c.uiStr("lane", "none");
  std::vector<std::pair<std::string, std::vector<int>>> lanes;
  if (lane == "asg") {
    std::map<std::string, std::vector<int>> by;
    std::vector<int> none;
    for (int id : leaves) {
      const std::string a = text::trim(I[id].assignee);
      if (a.empty()) none.push_back(id);
      else by[a].push_back(id);
    }
    std::vector<std::string> names;
    for (const auto& [k, v] : by) names.push_back(k);
    std::stable_sort(names.begin(), names.end(), [](const std::string& a, const std::string& b) { return jaCompare(a, b) < 0; });
    for (const auto& n : names) lanes.emplace_back(n, by[n]);
    if (!none.empty()) lanes.emplace_back(S::kUnassigned, none);
  } else if (lane == "root") {
    for (int r : T.roots) {
      std::vector<int> v;
      for (int id : leaves)
        if (T.rootOf[id] == r) v.push_back(id);
      if (!v.empty()) lanes.emplace_back(T.name[r], v);
    }
  } else {
    lanes.emplace_back("", leaves);
  }

  ImGui::BeginChild("##board", size, ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
  // 依存をつなぐドラッグ（カードの左右の端のハンドル。ゴムひもは右 → 相手の左の端）
  LinkOverlayOpts linkOpts;
  linkOpts.noun = "カード";
  linkOpts.sameColumn = true;
  linkOpts.hitR = 8.f;  // 右下の削除ボタンと重ならない大きさ
  linkOpts.bend = 24.f;
  linkOpts.round = 6.f;
  linkOpts.horizontalScroll = true;
  linkOpts.busy = gCardDrag.active;
  {
    const ImRect inner = ImGui::GetCurrentWindow()->InnerRect;  // スクロールバーを除いた、見えている範囲
    linkOpts.clipMin = inner.Min;
    linkOpts.clipMax = inner.Max;
  }
  u.linkSlots.clear();
  if (leaves.empty()) {
    ImGui::SetCursorPos(ImVec2(u.m.s(24), u.m.s(24)));
    ImGui::TextDisabled("%s", S::kNoMatch);
  }
  const float pad = u.m.s(12);
  const int nc = static_cast<int>(colNames.size());
  const float colW = std::max(u.m.s(200), (size.x - pad * static_cast<float>(nc + 1) - u.m.s(14)) / static_cast<float>(nc));
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  // 列の見出し
  float y = origin.y + pad;
  for (int k = 0; k < nc; ++k) {
    const float x = origin.x + pad + (colW + pad) * static_cast<float>(k);
    int n = 0;
    for (int id : leaves) n += colOf(id) == k;
    const std::string h = colNames[k] + "  " + std::to_string(n);
    dl->AddText(ImVec2(x + u.m.s(4), y), u.t->muted, h.c_str());
  }
  y += ImGui::GetTextLineHeight() + u.m.s(8);
  int dropCol = -1;
  for (const auto& [laneName, ids] : lanes) {
    if (!laneName.empty()) {
      const std::string lh = laneName + "  " + std::to_string(ids.size()) + "件";
      ImGui::PushFont(u.fontBold);
      dl->AddText(ImVec2(origin.x + pad, y), u.t->ink, lh.c_str());
      ImGui::PopFont();
      y += ImGui::GetTextLineHeight() + u.m.s(6);
    }
    float maxY = y;
    for (int k = 0; k < nc; ++k) {
      const float x = origin.x + pad + (colW + pad) * static_cast<float>(k);
      float cy = y + u.m.s(8);
      ImGui::SetCursorScreenPos(ImVec2(x + u.m.s(6), cy));
      for (int id : ids)
        if (colOf(id) == k) {
          {
            FadeScope fade(dl, isDimmed(u, dim, id, T.ids[id]), dimAlpha(c));
            drawCard(f, id, colW - u.m.s(12), rootIdx, linkOpts);
          }
          cy = ImGui::GetCursorScreenPos().y;
          ImGui::SetCursorScreenPos(ImVec2(x + u.m.s(6), cy));
        }
      const float colBottom = std::max(cy, y + u.m.s(60));
      const ImVec2 a(x, y), b(x + colW, colBottom + u.m.s(4));
      dl->AddRect(a, b, u.t->line, u.m.s(8));
      if (gCardDrag.active && ImGui::IsMouseHoveringRect(a, b)) {
        dl->AddRect(a, b, u.t->accent, u.m.s(8), 0, 2.f);
        dropCol = k;
      }
      maxY = std::max(maxY, colBottom);
    }
    y = maxY + u.m.s(16);
  }
  ImGui::SetCursorScreenPos(ImVec2(origin.x, y));
  ImGui::Dummy(ImVec2(pad + (colW + pad) * static_cast<float>(nc), u.m.s(20)));
  linkOpts.busy = gCardDrag.active;
  drawLinkOverlay(f, app::View::Board, linkOpts);  // ハンドル・ゴムひも・相手のカードの強調・確定
  if (gCardDrag.active && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
    const std::string id = gCardDrag.id;
    gCardDrag = {};
    const int i = T.find(id);
    if (dropCol >= 0 && i >= 0 && colOf(i) != dropCol) c.setStatusFor({id}, colNames[dropCol]);
  }
  ImGui::EndChild();
}

}  // namespace roadmap::ui
