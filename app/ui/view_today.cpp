// 「今日」ビュー（Issue #72）: 朝に開いて、今日と今週に何をするかを決める 1 画面。
// 今日 = 遅れ・期限・進行中・着手待ち・マイルストーンの節、今週 = 月〜金の日ごとの列と進行中の帯（集計は core/today）。
// 日付は IClock の今日（基準日ではない）。行のチェックで完了、「明日」「来週」でその場で日程をずらす（どちらも Undo 1 回）。
// 描画中に課題の数や状況を変えない（変更は deferAfterFrame で描画のあとに行う）。
#include <algorithm>
#include <string>
#include <unordered_set>
#include <vector>

#include "roadmap/text.hpp"
#include "roadmap/today.hpp"
#include "ui/icons.hpp"
#include "ui/internal.hpp"

namespace roadmap::ui {

namespace {

// 描画に使う計算結果（Session::viewIssues() / tree() / rows()。フレームの終わりまで同じ時点のものが残る）
struct Env {
  Frame& f;
  const Tree& T;
  const std::vector<Issue>& I;
  const std::vector<ScheduleRow>& R;
  Day today;
};

float rowHeight(const UiState& u) { return u.m.s(u.m.compact ? 44.f : 52.f); }
float cardHeight(const UiState& u) { return u.m.s(u.m.compact ? 56.f : 64.f); }

// 通知に入れる件名は長すぎないように
std::string clip(const std::string& s, std::size_t n) {
  const std::u32string u = text::toU32(s);
  if (u.size() <= n) return s;
  return text::fromU32(u.substr(0, n)) + "…";
}

bool isLate(const Env& e, int idx) {
  const auto i = static_cast<std::size_t>(idx);
  return e.R[i].floatDays < 0 || (e.T.dl[i] && *e.T.dl[i] < e.today);
}

// 余裕の文言（色だけに頼らず、遅れには「!」を付ける）
std::string riskText(const Env& e, int idx) {
  const ScheduleRow& r = e.R[static_cast<std::size_t>(idx)];
  if (isLate(e, idx)) return r.floatDays < 0 ? "! 遅れ " + floatText(r) : "! 期限超過";
  return "余裕 " + floatText(r);
}

RiskColor riskCol(const Env& e, int idx) {
  const ScheduleRow& r = e.R[static_cast<std::size_t>(idx)];
  return riskColor(e.f.u, isLate(e, idx) ? Risk::Late : riskOf(r.floatDays, false));
}

// 期限があれば期限、なければ最早の完了の日
std::string dateText(const Env& e, int idx) {
  const auto i = static_cast<std::size_t>(idx);
  if (e.T.dl[i]) return "期限 " + fmtMd(*e.T.dl[i]);
  return "終了予定 " + fmtMd(e.R[i].asapFinish);
}

bool overRect(const std::pair<ImVec2, ImVec2>& r) { return ImGui::IsMouseHoveringRect(r.first, r.second); }

// ---- 変更の依頼（描画が終わってから行う）----
void requestComplete(Frame& f, const std::string& sid) {
  deferAfterFrame(f, [&f, sid] {
    Session* s = f.c.session();
    if (!s || !s->tree() || !f.c.canEdit()) return;  // 読み取り専用では完了にしない（Issue #62）
    const int i = s->tree()->find(sid);
    if (i < 0) return;
    const std::string name = clip(s->tree()->name[static_cast<std::size_t>(i)], 24);
    f.c.setStatusFor({sid}, S::kStatusDone);  // 完了日は既存の動き（setStatus）
    f.c.toast("「" + name + "」を完了にしました", NoticeAction::Undo);
  });
}

void requestShift(Frame& f, const std::string& sid, ShiftTo to) {
  deferAfterFrame(f, [&f, sid, to] {
    Session* s = f.c.session();
    if (!s || !f.c.canEdit()) return;  // 読み取り専用では日程を動かさない（Issue #62）
    shiftIssue(*s, sid, to, f.c.clock().today());  // 通知（Undo つき）は Session が出す
    f.c.afterEdit();
  });
}

// ダブルクリック: リストへ移って、その課題の行まで進む（祖先が閉じていれば開く）
void requestReveal(Frame& f, const std::string& sid) {
  deferAfterFrame(f, [&f, sid] {
    Session* s = f.c.session();
    if (!s || !s->tree()) return;
    const Tree& T = *s->tree();
    const int i = T.find(sid);
    if (i < 0) return;
    for (int a = T.parentOf[static_cast<std::size_t>(i)], guard = 0; a >= 0 && guard < T.n; a = T.parentOf[static_cast<std::size_t>(a)], ++guard)
      if (s->isCollapsed(T.ids[static_cast<std::size_t>(a)])) s->setCollapsed(T.ids[static_cast<std::size_t>(a)], false);
    f.c.afterEdit();
    f.c.select(sid);
    f.u.revealId = sid;
    f.c.setView(app::View::List);
  });
}

// 完了にするチェック。押されたら true
bool checkBox(UiState& u, ImVec2 a, ImVec2 size, UiState::TodayRow& rec) {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImGui::SetCursorScreenPos(a);
  const bool pressed = ImGui::InvisibleButton("##chk", size);
  const bool hov = ImGui::IsItemHovered();
  const float bs = u.m.s(16);
  const ImVec2 c0(a.x + (size.x - bs) / 2, a.y + (size.y - bs) / 2);
  dl->AddRectFilled(c0, ImVec2(c0.x + bs, c0.y + bs), hov ? u.t->accentBg : u.t->surface, u.m.s(4));
  dl->AddRect(c0, ImVec2(c0.x + bs, c0.y + bs), hov ? u.t->accent : u.t->line2, u.m.s(4), 0, 1.5f);
  if (hov) drawIcon(dl, "check", ImVec2(c0.x + u.m.s(1), c0.y + u.m.s(1)), bs - u.m.s(2), u.t->accent);
  rec.chkA = a;
  rec.chkB = ImVec2(a.x + size.x, a.y + size.y);
  tip("完了にする（元に戻せます）");
  return pressed;
}

// 小さなボタン（「明日」「来週」）。押されたら true
bool smallButton(UiState& u, const char* id, const char* label, ImVec2 a, float w, float h, const char* tipText, ImVec2& ra,
                 ImVec2& rb) {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImGui::SetCursorScreenPos(a);
  const bool pressed = ImGui::InvisibleButton(id, ImVec2(w, h));
  const bool hov = ImGui::IsItemHovered();
  dl->AddRectFilled(a, ImVec2(a.x + w, a.y + h), hov ? u.t->surface3 : u.t->surface, u.m.s(4));
  dl->AddRect(a, ImVec2(a.x + w, a.y + h), hov ? u.t->accent : u.t->line2, u.m.s(4));
  const ImVec2 ts = ImGui::CalcTextSize(label);
  dl->AddText(ImVec2(a.x + (w - ts.x) / 2, a.y + (h - ts.y) / 2), hov ? u.t->accent : u.t->muted, label);
  ra = a;
  rb = ImVec2(a.x + w, a.y + h);
  tip(tipText);
  return pressed;
}

// 右端から「来週」「明日」を並べる。rx は左へ詰める
void shiftButtons(const Env& e, const std::string& sid, float& rx, float cy, float h, UiState::TodayRow& rec) {
  UiState& u = e.f.u;
  const float bh = u.m.s(22);
  const float w2 = ImGui::CalcTextSize("来週").x + u.m.s(14);
  rx -= w2;
  if (smallButton(u, "##nxt", "来週", ImVec2(rx, cy - bh / 2), w2, bh, "来週にずらす（開始・期限を5営業日あとへ。元に戻せます）", rec.nxtA, rec.nxtB))
    requestShift(e.f, sid, ShiftTo::NextWeek);
  rx -= u.m.s(4);
  const float w1 = ImGui::CalcTextSize("明日").x + u.m.s(14);
  rx -= w1;
  if (smallButton(u, "##tom", "明日", ImVec2(rx, cy - bh / 2), w1, bh, "明日にずらす（開始・期限を1営業日あとへ。元に戻せます）", rec.tomA, rec.tomB))
    requestShift(e.f, sid, ShiftTo::Tomorrow);
  rx -= u.m.s(6);
  (void)h;
}

// 件名の右に、コメント・添付・リンクの数を並べる。x を進める
void badges(const Env& e, const Issue& o, float& x, float cy) {
  UiState& u = e.f.u;
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const std::string& sid = o.id;
  if (!o.comments.empty()) {
    drawIcon(dl, "message", ImVec2(x, cy - u.m.s(6)), u.m.s(12), u.t->muted);
    const std::string n = std::to_string(o.comments.size());
    dl->AddText(ImVec2(x + u.m.s(14), cy - ImGui::GetTextLineHeight() / 2), u.t->muted, n.c_str());
    x += u.m.s(30);
  }
  if (const int na = static_cast<int>(e.f.c.attachmentCount(sid)); na > 0) {
    bool clicked = false;
    x += drawAttachBadge(e.f, sid, ImVec2(x, cy), cy, na, &clicked) + u.m.s(8);
    if (clicked) openDetail(e.f, sid, 3);
  }
  if (const int nl = static_cast<int>(e.f.c.linkCountOf(sid)); nl > 0) {
    bool clicked = false;
    x += drawLinkBadge(e.f, sid, ImVec2(x, cy), cy, nl, &clicked) + u.m.s(8);
    if (clicked) openDetail(e.f, sid, 3);
  }
}

float badgesWidth(const Env& e, const Issue& o) {
  UiState& u = e.f.u;
  float w = 0;
  if (!o.comments.empty()) w += u.m.s(30);
  if (e.f.c.attachmentCount(o.id) > 0) w += u.m.s(36);
  if (e.f.c.linkCountOf(o.id) > 0) w += u.m.s(36);
  return w;
}

// 行の最後に置く当たり判定（先に置いた部品が優先される）。押されたときの動作をまとめる
void rowButton(const Env& e, const std::string& sid, ImVec2 p, ImVec2 size) {
  UiState& u = e.f.u;
  ImGui::SetCursorScreenPos(p);
  ImGui::InvisibleButton("##row", size);
  if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) openDetail(e.f, sid, u.detailOpen ? u.detailTab : 0);
  if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) requestReveal(e.f, sid);
  if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) openContext(e.f, sid);
}

// 今日の節の 1 行（左: チェック・件名・祖先のパス ／ 右: 担当・状況・期限と余裕・ずらす）
void drawWideRow(const Env& e, int idx, const char* sec, ImVec2 p, float w) {
  Frame& f = e.f;
  UiState& u = f.u;
  const auto i = static_cast<std::size_t>(idx);
  const Issue& o = e.I[i];
  const std::string& sid = o.id;
  const float h = rowHeight(u);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 end(p.x + w, p.y + h);
  const bool hov = ImGui::IsMouseHoveringRect(p, end) && ImGui::IsWindowHovered();
  registerDropTarget(f, sid, p, end);  // ここへファイルをドロップすると添付
  if (f.c.isSelected(sid)) dl->AddRectFilled(p, end, u.t->selbg);
  else if (hov) dl->AddRectFilled(p, end, u.t->hover);
  if (sid == f.c.cursor()) dl->AddRectFilled(p, ImVec2(p.x + u.m.s(3), end.y), u.t->accent);
  dl->AddLine(ImVec2(p.x, end.y - 1), ImVec2(end.x, end.y - 1), u.t->rowline);
  UiState::TodayRow rec;
  rec.id = sid;
  rec.sec = sec;
  rec.a = p;
  rec.b = end;

  ImGui::PushID(sid.c_str());
  const float cy = p.y + h / 2;
  // 右の部品（右端から）
  float rx = p.x + w - u.m.s(10);
  shiftButtons(e, sid, rx, cy, h, rec);
  {
    const RiskColor rc = riskCol(e, idx);
    const std::string t = riskText(e, idx);
    const ImVec2 ts = ImGui::CalcTextSize(t.c_str());
    const float cw = ts.x + u.m.s(12), ch = ts.y + u.m.s(4);
    rx -= cw;
    dl->AddRectFilled(ImVec2(rx, cy - ch / 2), ImVec2(rx + cw, cy + ch / 2), rc.bg, ch / 2);
    dl->AddText(ImVec2(rx + u.m.s(6), cy - ts.y / 2), rc.fg, t.c_str());
    rx -= u.m.s(8);
  }
  if (w >= u.m.s(620)) {
    const std::string t = dateText(e, idx);
    const ImVec2 ts = ImGui::CalcTextSize(t.c_str());
    rx -= ts.x;
    dl->AddText(ImVec2(rx, cy - ts.y / 2), u.t->muted, t.c_str());
    rx -= u.m.s(10);
  }
  if (w >= u.m.s(720)) {
    const std::string label = o.status.empty() ? "To Do" : o.status;
    const float cw = ImGui::CalcTextSize(label.c_str()).x + u.m.s(12);
    rx -= cw;
    ImGui::SetCursorScreenPos(ImVec2(rx, cy - (ImGui::GetTextLineHeight() + u.m.s(4)) / 2));
    statusPill(u, "##st", o.status, false);
    rx -= u.m.s(8);
  }
  if (!o.assignee.empty()) {
    rx -= u.m.s(20);
    ImGui::SetCursorScreenPos(ImVec2(rx, cy - u.m.s(10)));
    avatar(u, o.assignee);
    rx -= u.m.s(8);
  }
  // 左: チェック・件名・祖先のパス
  if (checkBox(u, ImVec2(p.x + u.m.s(4), p.y), ImVec2(u.m.s(32), h), rec)) requestComplete(f, sid);
  const float tx = p.x + u.m.s(42);
  const float lh = ImGui::GetTextLineHeight();
  const float y1 = p.y + (h - 2 * lh - u.m.s(2)) / 2;
  const float room = std::max(u.m.s(60), rx - tx);
  const std::string nm = ellipsize(e.T.name[i], std::max(u.m.s(40), room - badgesWidth(e, o)));
  dl->AddText(ImVec2(tx, y1), u.t->ink, nm.c_str());
  float bx = tx + ImGui::CalcTextSize(nm.c_str()).x + u.m.s(8);
  badges(e, o, bx, y1 + lh / 2);
  {
    ImGui::PushFont(u.fontMono);
    const std::string did = e.T.did[i];
    const float dw = ImGui::CalcTextSize(did.c_str()).x;
    dl->AddText(ImVec2(tx, y1 + lh + u.m.s(2)), u.t->muted, did.c_str());
    ImGui::PopFont();
    const float pw = room - dw - u.m.s(8);
    if (pw > u.m.s(60)) {
      const std::string path = ancestorLabel(e.T, idx, pw);
      if (!path.empty()) dl->AddText(ImVec2(tx + dw + u.m.s(8), y1 + lh + u.m.s(2)), withAlpha(u.t->muted, 0.85f), path.c_str());
    }
  }
  rowButton(e, sid, p, ImVec2(w, h));
  ImGui::PopID();
  u.today.rows.push_back(rec);
}

// 今週の列の 1 枚のカード
void drawCard(const Env& e, int idx, ImVec2 p, float w) {
  Frame& f = e.f;
  UiState& u = f.u;
  const auto i = static_cast<std::size_t>(idx);
  const Issue& o = e.I[i];
  const std::string& sid = o.id;
  const float h = cardHeight(u);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 end(p.x + w, p.y + h);
  const bool hov = ImGui::IsMouseHoveringRect(p, end) && ImGui::IsWindowHovered();
  registerDropTarget(f, sid, p, end);
  const bool sel = f.c.isSelected(sid);
  dl->AddRectFilled(p, end, sel ? u.t->selbg : (hov ? u.t->hover : u.t->surface), u.m.s(6));
  dl->AddRect(p, end, sid == f.c.cursor() ? u.t->accent : u.t->line, u.m.s(6));
  const RiskColor rc = riskCol(e, idx);
  const bool late = isLate(e, idx);
  const bool tight = !late && e.R[i].floatDays <= kTodayTightDays;
  if (late || tight) dl->AddRectFilled(ImVec2(p.x, p.y + u.m.s(6)), ImVec2(p.x + u.m.s(3), end.y - u.m.s(6)), rc.fg, u.m.s(2));
  UiState::TodayRow rec;
  rec.id = sid;
  rec.sec = "week";
  rec.a = p;
  rec.b = end;

  ImGui::PushID(sid.c_str());
  // 下段: 余裕の文言と「明日」「来週」
  const float cy2 = p.y + h - u.m.s(17);
  float rx = p.x + w - u.m.s(6);
  shiftButtons(e, sid, rx, cy2, h, rec);
  {
    const std::string t = riskText(e, idx);
    const ImVec2 ts = ImGui::CalcTextSize(t.c_str());
    const float cw = ts.x + u.m.s(10), ch = ts.y + u.m.s(2);
    const float cx = p.x + u.m.s(10);
    if (cx + cw <= rx) {
      dl->AddRectFilled(ImVec2(cx, cy2 - ch / 2), ImVec2(cx + cw, cy2 + ch / 2), rc.bg, ch / 2);
      dl->AddText(ImVec2(cx + u.m.s(5), cy2 - ts.y / 2), rc.fg, t.c_str());
    }
  }
  // 上段: チェック・件名・担当
  const float cy1 = p.y + u.m.s(15);
  float right = p.x + w - u.m.s(6);
  if (!o.assignee.empty()) {
    right -= u.m.s(20);
    ImGui::SetCursorScreenPos(ImVec2(right, cy1 - u.m.s(10)));
    avatar(u, o.assignee);
    right -= u.m.s(4);
  }
  if (checkBox(u, ImVec2(p.x + u.m.s(2), p.y), ImVec2(u.m.s(28), u.m.s(30)), rec)) requestComplete(f, sid);
  const float tx = p.x + u.m.s(32);
  const std::string title = (e.T.kind[i] == Kind::Ms ? "◆ " : "") + e.T.name[i];
  const std::string nm = ellipsize(title, std::max(u.m.s(30), right - tx));
  dl->AddText(ImVec2(tx, cy1 - ImGui::GetTextLineHeight() / 2), u.t->ink, nm.c_str());
  rowButton(e, sid, p, ImVec2(w, h));
  if (ImGui::IsMouseHoveringRect(p, ImVec2(end.x, p.y + u.m.s(30))) && ImGui::IsWindowHovered() && !overRect({rec.chkA, rec.chkB}) &&
      nm != title) {
    ImGui::SetTooltip("%s", title.c_str());
  }
  ImGui::PopID();
  u.today.rows.push_back(rec);
}

// 節の見出し（件名の見出しと件数）
void drawSectionHead(const Env& e, const char* key, const std::string& title, const std::string& note, int n, ImVec2 p, float w) {
  UiState& u = e.f.u;
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const bool late = std::string(key) == "late";
  const float cy = p.y + u.m.s(17);
  float x = p.x + u.m.s(12);
  ImGui::PushFont(u.fontBold);
  const std::string t = late ? "! " + title : title;
  const ImVec2 ts = ImGui::CalcTextSize(t.c_str());
  dl->AddText(ImVec2(x, cy - ts.y / 2), late ? u.t->late : u.t->ink, t.c_str());
  ImGui::PopFont();
  x += ts.x + u.m.s(8);
  {
    const std::string cnt = std::to_string(n) + "件";
    const ImVec2 cs = ImGui::CalcTextSize(cnt.c_str());
    const float cw = cs.x + u.m.s(12), ch = cs.y + u.m.s(4);
    dl->AddRectFilled(ImVec2(x, cy - ch / 2), ImVec2(x + cw, cy + ch / 2), late ? u.t->lateBg : u.t->surface3, ch / 2);
    dl->AddText(ImVec2(x + u.m.s(6), cy - cs.y / 2), late ? u.t->late : u.t->muted, cnt.c_str());
    x += cw + u.m.s(10);
  }
  if (!note.empty() && x < p.x + w - u.m.s(40)) dl->AddText(ImVec2(x, cy - ImGui::GetTextLineHeight() / 2), u.t->muted, ellipsize(note, p.x + w - x - u.m.s(12)).c_str());
  dl->AddLine(ImVec2(p.x + u.m.s(8), p.y + u.m.s(32)), ImVec2(p.x + w - u.m.s(8), p.y + u.m.s(32)), u.t->line);
}

// ---- 見出し部（タブ・担当・基準日の案内）----
struct HeaderOut {
  float h = 0;
  bool week = false;
  bool mine = true;
};

HeaderOut drawHeader(Frame& f, ImVec2 size, Day today, const TodayReport& rep, Day base, bool week, bool mine) {
  app::AppController& c = f.c;
  UiState& u = f.u;
  UiState::TodayView& tv = u.today;
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 p0 = ImGui::GetCursorScreenPos();
  const float w = size.x;
  const float h1 = u.m.s(46);
  const bool mismatch = base != today;
  const float hb = mismatch ? u.m.s(34) : 0;
  dl->AddRectFilled(p0, ImVec2(p0.x + w, p0.y + h1 + hb), u.t->surface2);
  dl->AddLine(ImVec2(p0.x, p0.y + h1 + hb - 1), ImVec2(p0.x + w, p0.y + h1 + hb - 1), u.t->line);
  const float by = p0.y + (h1 - u.m.s(28)) / 2;
  ImGui::SetCursorScreenPos(ImVec2(p0.x + u.m.s(12), by));
  // 今日／今週
  if (iconTextButton(u, "tabToday", "sun", "今日", false, !week)) c.setUi("todayTab", "today");
  tv.tabToday = {ImGui::GetItemRectMin(), ImGui::GetItemRectMax()};
  ImGui::SameLine(0, u.m.s(4));
  if (iconTextButton(u, "tabWeek", "calendar", "今週", false, week)) c.setUi("todayTab", "week");
  tv.tabWeek = {ImGui::GetItemRectMin(), ImGui::GetItemRectMax()};
  ImGui::SameLine(0, u.m.s(16));
  // 担当（自分／全員）
  const std::string meLabel = "自分（" + ellipsize(c.prefs().me, u.m.s(110)) + "）";
  if (iconTextButton(u, "whoMe", "user", meLabel.c_str(), false, mine)) c.setUi("todayWho", "me");
  tv.whoMe = {ImGui::GetItemRectMin(), ImGui::GetItemRectMax()};
  tip("担当が、アプリの設定の「自分の名前」と同じ課題だけを出します");
  ImGui::SameLine(0, u.m.s(4));
  if (iconTextButton(u, "whoAll", nullptr, S::kAll, false, !mine)) c.setUi("todayWho", "all");
  tv.whoAll = {ImGui::GetItemRectMin(), ImGui::GetItemRectMax()};
  tip("担当に関わらず、すべての課題を出します");
  // 右: 今日の日付
  {
    const std::string d = fmtLong(today);
    const ImVec2 ts = ImGui::CalcTextSize(d.c_str());
    const float chipW = rep.todayIsWork ? 0 : ImGui::CalcTextSize("休業日").x + u.m.s(20);
    const float rx = p0.x + w - u.m.s(14) - ts.x - chipW;
    if (rx > ImGui::GetItemRectMax().x + u.m.s(20)) {
      ImGui::PushFont(u.fontBold);
      dl->AddText(ImVec2(rx, p0.y + (h1 - ts.y) / 2), u.t->ink, d.c_str());
      ImGui::PopFont();
      if (!rep.todayIsWork) {
        ImGui::SetCursorScreenPos(ImVec2(rx + ts.x + u.m.s(8), p0.y + (h1 - ts.y - u.m.s(4)) / 2));
        chip(u, "休業日", u.t->tight, u.t->tightBg);
      }
    }
  }
  tv.baseNote = mismatch;
  if (mismatch) {
    const ImVec2 q(p0.x, p0.y + h1);
    dl->AddRectFilled(q, ImVec2(q.x + w, q.y + hb - 1), u.t->tightBg);
    drawIcon(dl, "warn", ImVec2(q.x + u.m.s(12), q.y + u.m.s(9)), u.m.s(16), u.t->tight);
    const std::string msg = "基準日（" + fmtLong(base) + "）が今日と違います。日程は基準日から計算するので、今日の見込みとずれます。";
    const float btnW = ImGui::CalcTextSize("基準日を今日にする").x + u.m.s(24);
    dl->AddText(ImVec2(q.x + u.m.s(36), q.y + (hb - ImGui::GetTextLineHeight()) / 2), u.t->tight,
                ellipsize(msg, w - u.m.s(36) - btnW - u.m.s(24)).c_str());
    ImGui::SetCursorScreenPos(ImVec2(q.x + w - btnW - u.m.s(12), q.y + u.m.s(3)));
    if (iconTextButton(u, "baseToday", nullptr, "基準日を今日にする")) {
      deferAfterFrame(f, [&f] {
        if (!f.c.session() || !f.c.canEdit()) return;
        f.c.session()->setBaseDate(f.c.clock().todayIso());
        f.c.afterEdit();
      });
    }
    tv.baseBtn = {ImGui::GetItemRectMin(), ImGui::GetItemRectMax()};
  }
  ImGui::SetCursorScreenPos(ImVec2(p0.x, p0.y + h1 + hb));
  ImGui::Dummy(ImVec2(0, 0));
  HeaderOut out;
  out.h = h1 + hb;
  out.week = week;
  out.mine = mine;
  return out;
}

// ---- 今日 ----
struct Item {
  bool head = false;
  int idx = -1;
  const char* key = "";
  std::string title, note;
  int count = 0;
  float y = 0, h = 0;
};

// 何も出ないときの案内
void drawEmpty(Frame& f, ImVec2 origin, float w, bool mine, bool noMine, int openAll) {
  UiState& u = f.u;
  app::AppController& c = f.c;
  UiState::TodayView& tv = u.today;
  tv.empty = true;
  tv.noMine = noMine;
  ImDrawList* dl = ImGui::GetWindowDrawList();
  float y = origin.y + u.m.s(48);
  const float x = origin.x + std::max(u.m.s(24), (w - u.m.s(560)) / 2);
  const float room = std::min(u.m.s(560), w - u.m.s(48));
  ImGui::PushFont(u.fontLarge);
  const char* head = noMine ? "担当が「自分」の課題がありません" : "今日やることはありません";
  dl->AddText(ImVec2(x, y), u.t->ink, head);
  y += ImGui::GetTextLineHeight() + u.m.s(10);
  ImGui::PopFont();
  const auto para = [&](const std::string& t, ImU32 col) {
    const ImVec2 ts = ImGui::CalcTextSize(t.c_str(), nullptr, false, room);
    dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(x, y), col, t.c_str(), nullptr, room);
    y += ts.y + u.m.s(8);
  };
  if (noMine) {
    para("担当者が「" + c.prefs().me + "」の課題が1件もありません。", u.t->muted);
    para("アプリの設定の「自分の名前」を担当者名に合わせるか、「全員」に切り替えてください。", u.t->muted);
  } else if (mine && openAll > 0) {
    para("「自分」の課題には、今日の対象がありません。", u.t->muted);
    para("ほかの人の分は「全員」で見られます。", u.t->muted);
  } else if (openAll == 0) {
    para("未完了の課題がありません。", u.t->muted);
  }
  para("期限・開始日・先行などの日付がない未着手の課題は、ここには出ません。", withAlpha(u.t->muted, 0.85f));
  para("進行中にすると出ます。", withAlpha(u.t->muted, 0.85f));
  y += u.m.s(4);
  if (mine) {
    ImGui::SetCursorScreenPos(ImVec2(x, y));
    if (iconTextButton(u, "emptyAll", nullptr, "全員に切り替える")) c.setUi("todayWho", "all");
    ImGui::SameLine(0, u.m.s(8));
    if (noMine && iconTextButton(u, "emptyMe", nullptr, "アプリの設定…")) openDialog(f, Dlg::AppSettings);
  }
}

void drawTodayBody(const Env& e, const TodayReport& rep, ImVec2 size, bool mine, bool noMine) {
  Frame& f = e.f;
  UiState& u = f.u;
  UiState::TodayView& tv = u.today;
  const float width = std::max(size.x - u.m.s(14), u.m.s(360));
  struct Sec {
    const char* key;
    const char* title;
    const char* note;
    const std::vector<std::string>* ids;
  };
  const Sec secs[] = {{"late", "遅れている", "見込みが期限を過ぎる・期限を過ぎて未完了", &rep.late},
                      {"due", "今日が期限・終了予定", "", &rep.dueToday},
                      {"prog", "進行中", "余裕の小さい順", &rep.inProgress},
                      {"start", "今日から始められる", "先行がすべて完了・未着手", &rep.canStart},
                      {"ms", "今日のマイルストーン", "", &rep.milestones}};
  std::vector<Item> items;
  float y = u.m.s(8);
  u.order.clear();
  for (const Sec& sc : secs) {
    if (sc.ids->empty()) continue;
    Item hd;
    hd.head = true;
    hd.key = sc.key;
    hd.title = sc.title;
    hd.note = sc.note;
    hd.count = static_cast<int>(sc.ids->size());
    hd.y = y;
    hd.h = u.m.s(36);
    y += hd.h;
    items.push_back(hd);
    tv.sections.emplace_back(sc.key, hd.count);
    for (const std::string& id : *sc.ids) {
      const int idx = e.T.find(id);
      if (idx < 0) continue;
      Item it;
      it.idx = idx;
      it.key = sc.key;
      it.y = y;
      it.h = rowHeight(u);
      y += it.h;
      items.push_back(it);
      u.order.push_back(id);
    }
    y += u.m.s(10);
  }
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  if (items.empty()) {
    ImGui::Dummy(ImVec2(width, size.y - u.m.s(4)));
    drawEmpty(f, origin, width, mine, noMine, rep.openAll);
    return;
  }
  ImGui::Dummy(ImVec2(width, y + u.m.s(16)));
  const float scrollY = ImGui::GetScrollY(), viewH = ImGui::GetWindowHeight();
  for (const Item& it : items) {
    if (it.y + it.h < scrollY - u.m.s(2) || it.y > scrollY + viewH + u.m.s(2)) continue;  // 見える範囲だけ描く
    const ImVec2 p(origin.x, origin.y + it.y);
    if (it.head) drawSectionHead(e, it.key, it.title, it.note, it.count, p, width);
    else drawWideRow(e, it.idx, it.key, p, width);
  }
}

// ---- 今週 ----
void drawWeekBody(const Env& e, const TodayReport& rep, ImVec2 size, bool noMine) {
  Frame& f = e.f;
  UiState& u = f.u;
  app::AppController& c = f.c;
  UiState::TodayView& tv = u.today;
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const float pad = u.m.s(12), gap = u.m.s(8);
  const float availW = std::max(size.x - u.m.s(14), u.m.s(360));
  const float colW = std::max(u.m.s(176), (availW - pad * 2 - gap * 4) / 5.f);
  const float width = std::max(availW, pad * 2 + colW * 5.f + gap * 4);
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  const float scrollY = ImGui::GetScrollY(), viewH = ImGui::GetWindowHeight();
  const auto visible = [&](float y0, float y1) { return y1 >= scrollY - u.m.s(2) && y0 <= scrollY + viewH + u.m.s(2); };

  // 先に全体の高さを決める（Dummy を先に置く）
  const float cardH = cardHeight(u), cardGap = u.m.s(6);
  std::size_t maxN = 0;
  for (const auto& d : rep.week) maxN = std::max(maxN, d.ids.size());
  const std::size_t shownBands = tv.bandsAll ? rep.bands.size() : std::min<std::size_t>(rep.bands.size(), 5);
  const float bandH = u.m.s(28);
  float y = u.m.s(10);
  const float yChips = y;
  y += u.m.s(34);
  const float yBands = y;
  if (!rep.bands.empty()) y += u.m.s(26) + bandH * static_cast<float>(shownBands) + (rep.bands.size() > 5 ? u.m.s(30) : u.m.s(6)) + u.m.s(8);
  const float yCols = y;
  const float colHead = u.m.s(44);
  const float colBody = std::max(u.m.s(70), static_cast<float>(maxN) * (cardH + cardGap) + u.m.s(8));
  y += colHead + colBody + u.m.s(24);
  ImGui::Dummy(ImVec2(width, y));

  tv.completing = rep.summary.completing;
  tv.late = rep.summary.late;
  tv.tight = rep.summary.tight;
  tv.elsewhere = rep.summary.lateElsewhere;
  tv.bands = static_cast<int>(rep.bands.size());
  tv.bandsShown = static_cast<int>(shownBands);

  // サマリー（今週の完了予定・遅れ・余裕小）
  if (visible(yChips, yChips + u.m.s(34))) {
    ImGui::SetCursorScreenPos(ImVec2(origin.x + pad, origin.y + yChips + u.m.s(4)));
    chip(u, "今週の完了予定 " + std::to_string(rep.summary.completing) + " 件", u.t->ink, u.t->surface3);
    ImGui::SameLine(0, u.m.s(6));
    chip(u, "遅れ " + std::to_string(rep.summary.late) + " 件", rep.summary.late > 0 ? u.t->late : u.t->muted,
         rep.summary.late > 0 ? u.t->lateBg : u.t->surface3);
    ImGui::SameLine(0, u.m.s(6));
    chip(u, "余裕小 " + std::to_string(rep.summary.tight) + " 件", rep.summary.tight > 0 ? u.t->tight : u.t->muted,
         rep.summary.tight > 0 ? u.t->tightBg : u.t->surface3);
    if (rep.summary.lateElsewhere > 0) {
      ImGui::SameLine(0, u.m.s(12));
      const std::string l = "! 今週の外にも遅れが " + std::to_string(rep.summary.lateElsewhere) + " 件（「今日」で見る）";
      if (chipButton(u, "elsewhere", l, u.t->late, u.t->lateBg)) c.setUi("todayTab", "today");
      tv.elsewhereBtn = {ImGui::GetItemRectMin(), ImGui::GetItemRectMax()};
      tip("今週の列に載らない遅れ（今週より前から遅れている課題など）は、「今日」の「遅れている」に出ます");
    }
    if (noMine) {
      ImGui::SameLine(0, u.m.s(12));
      chip(u, "担当が「自分」の課題がありません（「全員」に切り替えられます）", u.t->tight, u.t->tightBg);
    }
  }

  // 進行中の帯
  u.order.clear();
  if (!rep.bands.empty() && visible(yBands, yCols)) {
    const float by0 = origin.y + yBands;
    ImGui::PushFont(u.fontBold);
    dl->AddText(ImVec2(origin.x + pad, by0 + u.m.s(2)), u.t->ink, "進行中");
    ImGui::PopFont();
    dl->AddText(ImVec2(origin.x + pad + ImGui::CalcTextSize("進行中").x + u.m.s(16), by0 + u.m.s(2)), u.t->muted,
                (std::to_string(rep.bands.size()) + " 件（帯は今週にかかる期間）").c_str());
    for (std::size_t k = 0; k < shownBands; ++k) {
      const TodayBand& b = rep.bands[k];
      const int idx = e.T.find(b.id);
      if (idx < 0) continue;
      const auto i = static_cast<std::size_t>(idx);
      const float x0 = origin.x + pad + static_cast<float>(b.first) * (colW + gap);
      const float x1 = origin.x + pad + static_cast<float>(b.last) * (colW + gap) + colW;
      const ImVec2 a(x0, by0 + u.m.s(26) + bandH * static_cast<float>(k)), z(x1, a.y + bandH - u.m.s(4));
      const bool hov = ImGui::IsMouseHoveringRect(a, z) && ImGui::IsWindowHovered();
      const bool sel = c.isSelected(b.id);
      const bool late = isLate(e, idx);
      dl->AddRectFilled(a, z, sel || hov ? u.t->accentBg : withAlpha(u.t->accentBg, 0.7f), u.m.s(6));
      dl->AddRect(a, z, late ? u.t->late : u.t->accent, u.m.s(6));
      std::string label = std::string(b.cutLeft ? "◀ " : "") + e.T.name[i] + (b.cutRight ? " ▶" : "");
      if (late) label = "! " + label;
      dl->PushClipRect(a, z, true);
      dl->AddText(ImVec2(a.x + u.m.s(8), a.y + (z.y - a.y - ImGui::GetTextLineHeight()) / 2), late ? u.t->late : u.t->accent,
                  ellipsize(label, z.x - a.x - u.m.s(16)).c_str());
      dl->PopClipRect();
      ImGui::PushID(b.id.c_str());
      ImGui::SetCursorScreenPos(a);
      ImGui::InvisibleButton("##band", ImVec2(z.x - a.x, z.y - a.y));
      if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) openDetail(f, b.id, u.detailOpen ? u.detailTab : 0);
      if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) requestReveal(f, b.id);
      if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) openContext(f, b.id);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("%s\n%s〜%s", e.T.name[i].c_str(), fmtMd(e.R[i].asapStart).c_str(), fmtMd(e.R[i].asapFinish).c_str());
      ImGui::PopID();
      UiState::TodayRow rec;
      rec.id = b.id;
      rec.sec = "band";
      rec.a = a;
      rec.b = z;
      tv.rows.push_back(rec);
    }
    if (rep.bands.size() > 5) {
      ImGui::SetCursorScreenPos(ImVec2(origin.x + pad, by0 + u.m.s(26) + bandH * static_cast<float>(shownBands) + u.m.s(2)));
      const std::string l = tv.bandsAll ? "上位 5 件だけにする" : "ほか " + std::to_string(rep.bands.size() - 5) + " 件を表示";
      if (iconTextButton(u, "bandsAll", nullptr, l.c_str())) tv.bandsAll = !tv.bandsAll;
      tv.bandsBtn = {ImGui::GetItemRectMin(), ImGui::GetItemRectMax()};
    }
  }
  // 矢印キーの順: 出している帯 → 列。同じ課題（帯とカードの両方に出る）は 1 回だけ入れる
  std::unordered_set<std::string> inOrder;
  for (std::size_t k = 0; k < shownBands; ++k)
    if (inOrder.insert(rep.bands[k].id).second) u.order.push_back(rep.bands[k].id);

  // 日ごとの列
  tv.colRects.clear();
  for (int k = 0; k < kTodayWeekDays; ++k) {
    const TodayDay& d = rep.week[static_cast<std::size_t>(k)];
    const float x0 = origin.x + pad + static_cast<float>(k) * (colW + gap);
    const ImVec2 a(x0, origin.y + yCols), z(x0 + colW, origin.y + yCols + colHead + colBody);
    tv.colRects.emplace_back(a, z);
    tv.colCounts[static_cast<std::size_t>(k)] = static_cast<int>(d.ids.size());
    for (const std::string& id : d.ids)
      if (inOrder.insert(id).second) u.order.push_back(id);
    if (!visible(yCols, yCols + colHead + colBody)) continue;
    dl->AddRectFilled(a, z, d.holiday ? withAlpha(u.t->holbg, 0.9f) : u.t->surface2, u.m.s(8));
    dl->AddRect(a, z, d.isToday ? u.t->accent : u.t->line, u.m.s(8), 0, d.isToday ? 2.f : 1.f);
    // 見出し: 曜日と日付（今日は強調。休業日は薄く）
    const std::string head = std::string(dowName(d.day)) + "  " + fmtShort(d.day);
    ImGui::PushFont(u.fontBold);
    dl->AddText(ImVec2(a.x + u.m.s(10), a.y + u.m.s(8)), d.holiday ? u.t->muted : (d.isToday ? u.t->accent : u.t->ink), head.c_str());
    ImGui::PopFont();
    std::string sub;
    if (d.isToday) sub = "今日";
    if (d.holiday) sub += std::string(sub.empty() ? "" : "・") + "休業日";
    if (!sub.empty()) dl->AddText(ImVec2(a.x + u.m.s(10), a.y + u.m.s(8) + ImGui::GetTextLineHeight() + u.m.s(1)), d.holiday ? u.t->muted : u.t->accent, sub.c_str());
    const std::string cnt = std::to_string(d.ids.size()) + "件";
    const ImVec2 cs = ImGui::CalcTextSize(cnt.c_str());
    dl->AddText(ImVec2(z.x - u.m.s(10) - cs.x, a.y + u.m.s(10)), u.t->muted, cnt.c_str());
    if (d.ids.empty()) {
      dl->AddText(ImVec2(a.x + u.m.s(10), a.y + colHead + u.m.s(8)), withAlpha(u.t->muted, 0.7f), "なし");
      continue;
    }
    float cy = a.y + colHead;
    for (const std::string& id : d.ids) {
      const int idx = e.T.find(id);
      if (idx < 0) continue;
      if (visible(cy - origin.y, cy - origin.y + cardH)) drawCard(e, idx, ImVec2(a.x + u.m.s(6), cy), colW - u.m.s(12));
      cy += cardH + cardGap;
    }
  }
}

}  // namespace

void drawTodayView(Frame& f, ImVec2 size) {
  app::AppController& c = f.c;
  UiState& u = f.u;
  Session* s = c.session();
  UiState::TodayView& tv = u.today;
  // 前のフレームの描画の結果を捨てる（スモークテストが「いま描いたもの」を見るため）
  const bool bandsAll = tv.bandsAll;
  tv = UiState::TodayView{};
  tv.bandsAll = bandsAll;
  if (!s || !s->tree() || !s->rows()) {
    ImGui::TextDisabled("%s", S::kEmptyProject);
    return;
  }
  // 名前の編集（F2・Enter・右クリックの「名前を変更」）は、この表示に入力欄がないので、編集できるリストへ移す
  // （入力欄が出ないまま編集の状態だけが残り、キー操作を塞がないように）
  if (u.editField != EditField::None && !u.editId.empty()) {
    const std::string id = u.editId;
    const Tree& T0 = *s->tree();
    for (int a = T0.parentOf[static_cast<std::size_t>(T0.find(id) < 0 ? 0 : T0.find(id))], guard = 0; T0.find(id) >= 0 && a >= 0 && guard < T0.n;
         a = T0.parentOf[static_cast<std::size_t>(a)], ++guard)
      if (s->isCollapsed(T0.ids[static_cast<std::size_t>(a)])) s->setCollapsed(T0.ids[static_cast<std::size_t>(a)], false);
    c.afterEdit();
    c.setView(app::View::List);
    u.revealId = id;
    return;
  }
  const Tree& T = *s->tree();
  const auto& I = s->viewIssues();
  const auto& R = *s->rows();
  const Day today = c.clock().today();
  const bool week = c.uiStr("todayTab", "today") == "week";
  const bool mine = c.uiStr("todayWho", "me") != "all";
  TodayFilter flt;
  flt.byAssignee = mine;
  flt.assignee = c.prefs().me;
  const auto res = computeToday(T, I, R, s->calendar(), today, flt);
  if (!res.ok()) {
    ImGui::TextDisabled("%s", res.error().c_str());
    return;
  }
  const TodayReport& rep = *res;
  bool noMine = false;
  if (mine) {  // 担当が「自分」の課題が 1 件もない（名前の食い違いに気づけるように）
    noMine = true;
    const std::string me = text::trim(c.prefs().me);
    for (const Issue& o : I)
      if (text::trim(o.assignee) == me) {
        noMine = false;
        break;
      }
  }
  tv.week = week;
  tv.total = rep.total();

  ImGui::BeginChild("##todayhead", ImVec2(size.x, u.m.s(46) + (s->baseDay() != today ? u.m.s(34) : 0.f)), ImGuiChildFlags_None,
                    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
  const HeaderOut ho = drawHeader(f, size, today, rep, s->baseDay(), week, mine);
  ImGui::EndChild();
  ImGui::BeginChild("##todaybody", ImVec2(size.x, size.y - ho.h), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
  const Env env{f, T, I, R, today};
  if (week) drawWeekBody(env, rep, ImVec2(size.x, size.y - ho.h), noMine);
  else drawTodayBody(env, rep, ImVec2(size.x, size.y - ho.h), mine, noMine);
  ImGui::EndChild();
  tv.drawn = true;
}

}  // namespace roadmap::ui
