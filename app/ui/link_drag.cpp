// 依存をつなぐドラッグ（Issue #8）の、ガント・一覧・ボードで共有する部分。
// 状態（UiState::linkDrag）・ハンドルとゴムひもの描画・つなぐ相手の判定と日程の影響・確定（描画のあと）を持つ。
// ガントは自分のバーの位置と日程のゴーストを描き、一覧とボードは行・カードの位置（LinkSlot）を渡して
// drawLinkOverlay にまかせる。
#include <algorithm>
#include <cmath>

#include <imgui_internal.h>

#include "ui/internal.hpp"

namespace roadmap::ui {

bool linkDragIdle(const UiState& u) {
  return !u.linkPick.active && !u.barDrag.active && !u.rowDrag.active && !u.colDrag.active && !u.linkDrag.active &&
         !u.arrowDrag.pending && !u.arrowDrag.active && u.dlg == Dlg::None && !u.paletteOpen &&
         u.editField == EditField::None;
}

void linkDragBegin(Frame& f, int issue, bool fromRight, ImVec2 mouse) {
  Session* s = f.c.session();
  if (!s || !s->tree() || issue < 0 || issue >= s->tree()->n) return;
  UiState& u = f.u;
  u.linkDrag = {};
  u.linkDrag.active = true;
  u.linkDrag.id = s->tree()->ids[static_cast<std::size_t>(issue)];
  u.linkDrag.fromRight = fromRight;
  u.linkDrag.project = f.c.currentId();
  u.linkDrag.view = f.c.view();
  u.linkDrag.start = mouse;
}

bool linkDragValid(Frame& f, app::View v) {
  UiState& u = f.u;
  if (!u.linkDrag.active) return false;
  const Session* s = f.c.session();
  if (!s || !s->tree() || s->tree()->find(u.linkDrag.id) < 0 || f.c.currentId() != u.linkDrag.project ||
      f.c.view() != v || u.linkDrag.view != v) {
    u.linkDrag = {};
    return false;
  }
  return true;
}

LinkEval linkDragEval(Frame& f, int tgt) {
  UiState& u = f.u;
  Session* s = f.c.session();
  const Tree& T = *s->tree();
  LinkEval e;
  e.src = T.find(u.linkDrag.id);
  e.fromRight = u.linkDrag.fromRight;
  e.tgt = (tgt >= 0 && tgt < T.n && tgt != e.src) ? tgt : -1;
  if (e.tgt >= 0) {
    const int pred = e.fromRight ? e.src : e.tgt, succ = e.fromRight ? e.tgt : e.src;
    e.blk = checkLink(T, s->viewIssues(), pred, succ);  // 循環・重複・親子は赤（つなげない）
    if (e.blk == LinkBlock::None) {
      // 相手が変わったとき（または課題が変わったとき）だけ日程の影響を計算し直す
      const std::string key = T.ids[static_cast<std::size_t>(pred)] + "\n" + T.ids[static_cast<std::size_t>(succ)];
      if (u.linkDrag.previewKey != key || u.linkDrag.previewVer != s->version()) {
        u.linkDrag.preview = s->previewLink(T.ids[static_cast<std::size_t>(pred)], T.ids[static_cast<std::size_t>(succ)]);
        u.linkDrag.previewKey = key;
        u.linkDrag.previewVer = s->version();
      }
      e.pv = &u.linkDrag.preview;
    }
  }
  e.ok = e.tgt >= 0 && e.blk == LinkBlock::None;
  e.bad = e.tgt >= 0 && !e.ok;
  e.lineCol = e.bad ? u.t->late : e.ok ? u.t->accent : u.t->muted;
  return e;
}

void linkDragFinish(Frame& f, const LinkEval& e) {
  if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) return;
  UiState& u = f.u;
  const Tree& T = *f.c.session()->tree();
  const std::string src = u.linkDrag.id;
  const bool fromRight = u.linkDrag.fromRight;
  u.linkDrag = {};
  if (e.tgt < 0 || e.tgt >= T.n) return;  // 相手がいなければ取り消し
  const std::string other = T.ids[static_cast<std::size_t>(e.tgt)];
  // 課題を変えるのは描画が終わってから（つなげない相手のときは理由の通知が出る）
  deferAfterFrame(f, [&f, src, other, fromRight] { linkIssues(f, src, fromRight, other, true); });
}

void drawLinkHandle(Frame& f, ImVec2 hc, bool hot, bool ok) {
  UiState& u = f.u;
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const float r = u.m.s(kLinkHandleR);
  const ImU32 col = ok ? u.t->accent : u.t->late;
  dl->AddCircleFilled(hc, r, hot ? col : u.t->surface);
  dl->AddCircle(hc, r, col, 0, 2.f);
  dl->AddCircleFilled(hc, r * 0.38f, hot ? IM_COL32_WHITE : col);
}

void drawLinkRubber(Frame& f, ImVec2 p0, ImVec2 p1, bool fromRight, bool sameSide, float minBend, ImU32 col) {
  UiState& u = f.u;
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const float dir = fromRight ? 1.f : -1.f;
  // 同じ側の端どうし（一覧）は、端の余白の中で小さく膨らむ。そうでなければ距離の半分まで大きく曲げる
  const float dx = sameSide ? minBend : std::max(minBend, std::abs(p1.x - p0.x) * 0.5f);
  const ImVec2 c1(p0.x + dir * dx, p0.y), c2(sameSide ? p1.x + dir * dx : p1.x - dir * dx, p1.y);
  dl->AddBezierCubic(p0, c1, c2, p1, withAlpha(col, 0.25f), u.m.s(6));  // 太い薄い線 + 本線
  dl->AddBezierCubic(p0, c1, c2, p1, col, u.m.s(2.2f));
  if (fromRight) {
    const float bx = sameSide ? p1.x + u.m.s(8) : p1.x - u.m.s(8);  // 矢じりの向き（進む向き）
    dl->AddTriangleFilled(p1, ImVec2(bx, p1.y - u.m.s(5)), ImVec2(bx, p1.y + u.m.s(5)), col);
  } else {
    dl->AddCircleFilled(p1, u.m.s(4), col);
  }
}

void drawLinkTip(Frame& f, const LinkEval& e, const char* noun) {
  UiState& u = f.u;
  const Tree& T = *f.c.session()->tree();
  const std::string a = T.did[static_cast<std::size_t>(e.fromRight ? e.src : std::max(e.tgt, 0))];
  const std::string b = T.did[static_cast<std::size_t>(e.fromRight ? std::max(e.tgt, 0) : e.src)];
  ImGui::BeginTooltip();
  if (e.tgt < 0) {
    ImGui::TextUnformatted((std::string("つなぐ相手の") + noun + "までドラッグ（Esc で取り消し）").c_str());
  } else if (e.bad) {
    ImGui::PushStyleColor(ImGuiCol_Text, v4(u.t->late));
    ImGui::TextUnformatted((a + " → " + b + " はつなげません").c_str());
    ImGui::TextUnformatted(linkBlockReason(e.blk));
    ImGui::PopStyleColor();
  } else {
    const LinkPreview* pv = e.pv;
    ImGui::TextUnformatted((a + " → " + b + " をつなぐ").c_str());
    const std::string who = T.did[static_cast<std::size_t>(e.fromRight ? e.tgt : e.src)];  // 後続
    if (pv && pv->delayDays > 0) {
      ImGui::PushStyleColor(ImGuiCol_Text, v4(u.t->late));
      std::string t = who + " が " + std::to_string(pv->delayDays) + "営業日 遅れます";
      if (pv->shifts.size() > 1) t += "（影響 " + std::to_string(pv->shifts.size()) + "件）";
      ImGui::TextUnformatted(t.c_str());
      ImGui::PopStyleColor();
    } else if (pv && !pv->shifts.empty()) {
      ImGui::TextDisabled("%s", ("日程が変わる課題 " + std::to_string(pv->shifts.size()) + "件").c_str());
    } else {
      ImGui::TextDisabled("日程は変わりません");
    }
    ImGui::TextDisabled("離すとつなぎます（Esc で取り消し）");
  }
  ImGui::EndTooltip();
}

void linkDragAutoScroll(Frame& f, ImVec2 mp, ImVec2 clipMin, ImVec2 clipMax, bool horizontal) {
  const UiState& u = f.u;
  const float zone = u.m.s(36);
  const float step = u.m.s(900.f) * ImGui::GetIO().DeltaTime;  // 端にいるときの速さ（1秒に 900px 相当）
  if (clipMax.x - clipMin.x < zone * 2 || clipMax.y - clipMin.y < zone * 2) return;
  // 端からの近さ -1..1（負 = 手前へ。外に出たら最大）
  const auto push = [&](float v, float lo, float hi) {
    if (v < lo + zone) return -std::min(1.f, (lo + zone - v) / zone);
    if (v > hi - zone) return std::min(1.f, (v - (hi - zone)) / zone);
    return 0.f;
  };
  if (mp.x >= clipMin.x && mp.x < clipMax.x) {
    const float dy = push(mp.y, clipMin.y, clipMax.y);
    if (dy != 0.f) ImGui::SetScrollY(std::max(0.f, ImGui::GetScrollY() + dy * step));
  }
  if (horizontal && mp.y >= clipMin.y && mp.y < clipMax.y) {
    const float dx = push(mp.x, clipMin.x, clipMax.x);
    if (dx != 0.f) ImGui::SetScrollX(std::max(0.f, ImGui::GetScrollX() + dx * step));
  }
}

namespace {
bool inRect(ImVec2 p, ImVec2 a, ImVec2 b) { return p.x >= a.x && p.x < b.x && p.y >= a.y && p.y < b.y; }

// スロット sl のハンドルのうちマウスが当たっているもの（0 = なし 1 = 左 2 = 右）
int hotSide(const UiState::LinkSlot& sl, ImVec2 mp, float hitR) {
  const ImVec2 hs[2] = {sl.hl, sl.hr};
  for (int side = 2; side >= 1; --side) {
    const ImVec2 hc = hs[side - 1];
    if ((mp.x - hc.x) * (mp.x - hc.x) + (mp.y - hc.y) * (mp.y - hc.y) <= hitR * hitR) return side;
  }
  return 0;
}

// マウスが行・カードの上（左右に padX 広げた範囲）にあるか
bool overSlot(const UiState::LinkSlot& sl, ImVec2 mp) {
  return inRect(mp, ImVec2(sl.a.x - sl.padX, sl.a.y), ImVec2(sl.b.x + sl.padX, sl.b.y));
}
}  // namespace

int linkSlotAdd(Frame& f, const UiState::LinkSlot& slot, const LinkOverlayOpts& o) {
  UiState& u = f.u;
  u.linkSlots.push_back(slot);
  if (o.busy || !linkDragIdle(u) || !ImGui::IsWindowHovered()) return 0;
  const ImVec2 mp = ImGui::GetMousePos();
  if (!inRect(mp, o.clipMin, o.clipMax)) return 0;
  const int side = hotSide(slot, mp, u.m.s(o.hitR));
  // ハンドルを押したらドラッグを始める（この行・カードのクリックにはしない）
  if (side != 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) linkDragBegin(f, slot.idx, side == 2, mp);
  return side;
}

void drawLinkOverlay(Frame& f, app::View view, const LinkOverlayOpts& o) {
  UiState& u = f.u;
  Session* s = f.c.session();
  if (!s || !s->tree()) return;
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 mp = ImGui::GetMousePos();
  const float hitR = u.m.s(o.hitR);
  dl->PushClipRect(o.clipMin, o.clipMax, true);

  if (linkDragValid(f, view)) {
    const Tree& T = *s->tree();
    // マウスの下の行・カード（つかんだ課題自身は「元の場所」で、相手なし）
    const UiState::LinkSlot* tgtSlot = nullptr;
    const UiState::LinkSlot* srcSlot = nullptr;
    const int si = T.find(u.linkDrag.id);
    for (const auto& sl : u.linkSlots) {
      if (sl.idx == si) srcSlot = &sl;
      else if (!tgtSlot && inRect(mp, o.clipMin, o.clipMax) && overSlot(sl, mp)) tgtSlot = &sl;
    }
    const LinkEval e = linkDragEval(f, tgtSlot ? tgtSlot->idx : -1);
    const bool fromRight = e.fromRight;
    // 同じ側の端どうしをつなぐか（一覧は常に。ボードは同じ列のとき）
    const bool same = tgtSlot && (o.sameSide || (o.sameColumn && srcSlot && std::abs(srcSlot->a.x - tgtSlot->a.x) < 1.f));
    // 相手の行・カードを強調（つなげないときは赤）
    if (tgtSlot) {
      dl->AddRectFilled(tgtSlot->a, tgtSlot->b, withAlpha(e.lineCol, 0.10f), u.m.s(o.round));
      dl->AddRect(tgtSlot->a, tgtSlot->b, e.lineCol, u.m.s(o.round), 0, 2.5f);
      if (same) drawLinkHandle(f, fromRight ? tgtSlot->hr : tgtSlot->hl, true, !e.bad);
    }
    // ゴムひも: つかんだ側のハンドルから、相手の端に吸着（なければカーソル）まで
    ImVec2 p0 = u.linkDrag.start;
    if (srcSlot) {
      p0 = fromRight ? srcSlot->hr : srcSlot->hl;
      drawLinkHandle(f, p0, true, !e.bad);
    }
    ImVec2 p1 = mp;
    if (tgtSlot) p1 = same ? (fromRight ? tgtSlot->sameR : tgtSlot->sameL) : (fromRight ? tgtSlot->landR : tgtSlot->landL);
    drawLinkRubber(f, p0, p1, fromRight, same, u.m.s(o.bend), e.lineCol);
    dl->PopClipRect();
    drawLinkTip(f, e, o.noun);
    ImGui::SetMouseCursor(e.bad ? ImGuiMouseCursor_NotAllowed : ImGuiMouseCursor_Hand);
    linkDragAutoScroll(f, mp, o.clipMin, o.clipMax, o.horizontalScroll);
    linkDragFinish(f, e);
    return;
  }

  // ドラッグ中でなければ、マウスを載せた行・カードの左右にハンドルを出す
  if (!o.busy && linkDragIdle(u) && ImGui::IsWindowHovered() && inRect(mp, o.clipMin, o.clipMax)) {
    for (const auto& sl : u.linkSlots) {
      if (!overSlot(sl, mp)) continue;
      const int side = hotSide(sl, mp, hitR);
      drawLinkHandle(f, sl.hl, side == 1);
      drawLinkHandle(f, sl.hr, side == 2);
      if (side != 0) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        ImGui::SetTooltip("%s", side == 2 ? "ドラッグして後続につなぐ" : "ドラッグして先行につなぐ");
      }
      break;
    }
  }
  dl->PopClipRect();
}

}  // namespace roadmap::ui
