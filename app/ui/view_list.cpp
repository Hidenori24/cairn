// リストビュー（SPEC 10.7.1）と、表の行（ガントの左の表と共有）
#include <algorithm>
#include <map>
#include <set>

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include "roadmap/text.hpp"
#include "ui/icons.hpp"
#include "ui/internal.hpp"

namespace roadmap::ui {

namespace {
constexpr float kListGutter = 20.f;  // 表の左右の余白（依存のハンドルを出す。拡大率をかける前）

const char* colTitle(Col c) {
  switch (c) {
    case Col::Name: return "課題";
    case Col::Type: return "種別";
    case Col::Status: return "状況";
    case Col::Asg: return "担当";
    case Col::Prio: return "優先度";
    case Col::Start: return "開始（計画）";
    case Col::Finish: return "完了（計画）";
    case Col::Dur: return "期間";
    case Col::Float: return "余裕";
    case Col::Deadline: return "期限";
    case Col::Pred: return "先行";
    case Col::Plan: return "計画との差";
    default: return "";
  }
}
const char* colKey(Col c) {
  switch (c) {
    case Col::Name: return "name";
    case Col::Type: return "type";
    case Col::Status: return "status";
    case Col::Asg: return "asg";
    case Col::Prio: return "prio";
    case Col::Start: return "start";
    case Col::Finish: return "finish";
    case Col::Dur: return "dur";
    case Col::Float: return "float";
    case Col::Deadline: return "deadline";
    case Col::Pred: return "pred";
    case Col::Plan: return "plan";
    default: return "";
  }
}

// 列の絞り込みのキー（絞り込めない列は空）
const char* filterKey(Col c) {
  switch (c) {
    case Col::Name: return "name";
    case Col::Type: return "type";
    case Col::Status: return "status";
    case Col::Asg: return "asg";
    case Col::Prio: return "prio";
    case Col::Float: return "float";
    case Col::Pred: return "pred";
    case Col::Plan: return "plan";
    default: return "";
  }
}

// 列「計画との差」の絞り込みの値の並び（遅れを先頭に）
const char* const kPlanValueOrder[] = {kBaselineValueLate, kBaselineValueOnPlan, kBaselineValueEarly, kBaselineValueAdded, kBaselineValueNone};

bool columnFiltered(const ColumnFilter& cf, const std::string& col) {
  if (col == "name") return !text::trim(cf.name).empty();
  const auto it = cf.hide.find(col);
  return it != cf.hide.end() && !it->second.empty();
}

// 絞り込みの一覧に出す値（いまある値 + 隠したまま消えた値）を、列ごとの順に
std::vector<std::string> filterValues(const Session& s, const ColumnFilter& cf, const std::string& col) {
  const Tree& T = *s.tree();
  std::vector<std::string> v;
  if (col == "plan") {
    // 表示しているベースラインとの比較に現れる値（末端の課題）
    if (const BaselineCompare* bc = s.baselineCompare()) {
      std::set<std::string> seen;
      for (int id : T.leafOrder)
        if (static_cast<std::size_t>(id) < bc->entries.size()) seen.insert(baselineColumnValue(bc->entries[static_cast<std::size_t>(id)]));
      for (const char* x : kPlanValueOrder)
        if (seen.count(x)) v.push_back(x);
    }
  } else {
    v = columnValues(col, T, s.viewIssues(), *s.rows());
  }
  if (const auto it = cf.hide.find(col); it != cf.hide.end())
    for (const auto& h : it->second)
      if (std::find(v.begin(), v.end(), h) == v.end()) v.push_back(h);
  const auto rank = [&](const std::string& x) -> int {
    if (col == "status") return static_cast<int>(statusCat(x));
    if (col == "prio") return prioRank(x);
    if (col == "plan") {
      for (int k = 0; k < 5; ++k)
        if (x == kPlanValueOrder[k]) return k;
      return 5;
    }
    if (col == "float") {
      static const char* const order[] = {"遅れ", "余裕小", "余裕あり", "完了"};
      for (int k = 0; k < 4; ++k)
        if (x == order[k]) return k;
      return 4;
    }
    return x == kUnassignedValue || x == kNoneValue ? 1 : 0;
  };
  const bool byName = col == "asg" || col == "type";
  std::stable_sort(v.begin(), v.end(), [&](const std::string& a, const std::string& b) {
    if (rank(a) != rank(b)) return rank(a) < rank(b);
    return byName && jaCompare(a, b) < 0;
  });
  return v;
}

// 列の絞り込みのポップアップ（一致しないものは隠す）
void drawColumnFilterPopup(Frame& f, const std::string& col, const char* title, ImVec2 anchor) {
  UiState& u = f.u;
  Session* s = f.c.session();
  ColumnFilter& cf = f.c.columnFilter();
  ImGui::SetNextWindowPos(anchor, ImGuiCond_Appearing);  // 漏斗の下に出す
  ImGui::SetNextWindowSizeConstraints(ImVec2(u.m.s(220), 0), ImVec2(u.m.s(360), u.m.s(460)));
  if (!ImGui::BeginPopup("##colf")) return;
  ImGui::PushFont(u.fontBold);
  ImGui::Text("「%s」で絞り込む", title);
  ImGui::PopFont();
  if (col == "name") {
    ImGui::TextDisabled("件名・ID を含むものだけを表示");
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    ImGui::SetNextItemWidth(u.m.s(240));
    ImGui::InputTextWithHint("##nf", "含む文字", &cf.name);
    if (ImGui::IsItemDeactivated() && enterPressed(u)) ImGui::CloseCurrentPopup();
  } else if (s && s->tree()) {
    ImGui::TextDisabled("チェックを外した値は隠します");
    const auto values = filterValues(*s, cf, col);
    std::map<std::string, int> count;
    const Tree& T = *s->tree();
    const BaselineCompare* bc = col == "plan" ? s->baselineCompare() : nullptr;
    for (int id : T.leafOrder) {
      if (col == "plan") {
        if (bc && static_cast<std::size_t>(id) < bc->entries.size()) ++count[baselineColumnValue(bc->entries[static_cast<std::size_t>(id)])];
        continue;
      }
      ++count[col == "pred" ? std::string(predColumnValue(!s->computed()->declPreds[id].empty()))
                            : columnValue(col, s->viewIssues()[id], (*s->rows())[id])];
    }
    bool all = !columnFiltered(cf, col);
    if (ImGui::Checkbox("すべて", &all)) {
      cf.clear(col);
      if (!all)
        for (const auto& v : values) cf.hide[col].insert(v);
    }
    ImGui::Separator();
    const float listH = std::min(u.m.s(320), ImGui::GetFrameHeightWithSpacing() * static_cast<float>(values.size()) + u.m.s(4));
    ImGui::BeginChild("##vals", ImVec2(0, listH));
    for (const auto& v : values) {
      ImGui::PushID(v.c_str());
      bool on = !cf.hidden(col, v);
      const std::string label = v + "  (" + std::to_string(count[v]) + ")";
      if (ImGui::Checkbox(label.c_str(), &on)) cf.toggle(col, v);
      ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::CalcTextSize("だけ").x - u.m.s(10));
      if (ImGui::SmallButton("だけ")) {
        cf.clear(col);
        for (const auto& w : values)
          if (w != v) cf.hide[col].insert(w);
      }
      ImGui::PopID();
    }
    ImGui::EndChild();
  }
  if (columnFiltered(cf, col)) {
    ImGui::Separator();
    if (ImGui::Selectable("この列の絞り込みを解除")) cf.clear(col);
  }
  ImGui::EndPopup();
}

ImU32 planColor(const UiState& u, const BaselineEntry& e) {
  return e.state == BaselineState::Late ? u.t->late : e.state == BaselineState::Early ? u.t->ok : u.t->muted;
}

void textIn(ImDrawList* dl, const UiState& u, ImVec2 p, ImVec2 sz, const std::string& s, ImU32 col, ImFont* font = nullptr) {
  if (font) ImGui::PushFont(font);
  const std::string t = ellipsize(s, sz.x - u.m.s(8));
  const ImVec2 ts = ImGui::CalcTextSize(t.c_str());
  dl->AddText(ImVec2(p.x + u.m.s(4), p.y + (sz.y - ts.y) / 2), col, t.c_str());
  if (font) ImGui::PopFont();
}
}  // namespace

std::vector<std::string> planTipLines(const BaselineCompare& bc, const BaselineEntry& e) {
  std::vector<std::string> L;
  L.push_back("ベースライン「" + bc.baselineName + "」との差");
  switch (e.state) {
    case BaselineState::Added: L.push_back("計画になかった課題です"); return L;
    case BaselineState::NoDate: L.push_back("計画に日付がないため、比べられません"); return L;
    default: break;
  }
  if (e.planFinish)
    L.push_back("計画 " + (e.planStart ? fmtLong(*e.planStart) + " 〜 " : std::string()) + fmtLong(*e.planFinish));
  L.push_back("いま " + fmtLong(e.nowStart) + " 〜 " + fmtLong(e.nowFinish));
  if (e.hasFinishDiff) L.push_back("完了日の差 " + baselineDiffText(e) + "（営業日。+ は遅れ）");
  if (e.hasStartDiff && e.startDiff != 0) L.push_back("開始日の差 " + std::string(e.startDiff > 0 ? "+" : "") + std::to_string(e.startDiff) + "日");
  const std::string note = baselineDiffNote(e);
  if (!note.empty()) L.push_back(note);
  return L;
}

void drawTableHeader(Frame& f, ImVec2 pos, float width, float h, const std::vector<ColDef>& cols, bool sortable) {
  UiState& u = f.u;
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + h), u.t->surface2);
  dl->AddLine(ImVec2(pos.x, pos.y + h - 1), ImVec2(pos.x + width, pos.y + h - 1), u.t->line);
  const ColumnFilter& cf = f.c.columnFilter();
  float x = pos.x;
  for (const auto& cd : cols) {
    const char* title = colTitle(cd.col);
    const std::string fk = filterKey(cd.col);
    const float fw = fk.empty() ? 0.f : u.m.s(22);  // 漏斗のボタンの幅
    if (*title) {
      const ImVec2 ts = ImGui::CalcTextSize(title);
      std::string label = title;
      if (sortable && u.sortKey == colKey(cd.col) && u.sortDir) label += u.sortDir > 0 ? " ▲" : " ▼";
      const bool filtered = !fk.empty() && columnFiltered(cf, fk);
      dl->AddText(ImVec2(x + u.m.s(6), pos.y + (h - ts.y) / 2), filtered ? u.t->accent : u.t->muted, label.c_str());
      ImGui::PushID(title);
      if (!fk.empty()) {
        // 列の絞り込み（見出しにマウスを載せたとき・絞り込み中に出す）
        const float lx = x + u.m.s(6) + ImGui::CalcTextSize(label.c_str()).x + u.m.s(2);  // 列名のすぐ右
        const ImVec2 bp(std::min(lx, x + cd.w - fw), pos.y);
        const bool cellHov = ImGui::IsMouseHoveringRect(ImVec2(x, pos.y), ImVec2(x + cd.w, pos.y + h)) && ImGui::IsWindowHovered();
        ImGui::SetCursorScreenPos(bp);
        if (ImGui::InvisibleButton("##cf", ImVec2(fw, h)) || u.openColFilter == fk) {
          ImGui::OpenPopup("##colf");
          u.openColFilter.clear();
        }
        const bool btnHov = ImGui::IsItemHovered();
        if (btnHov) tip("この列で絞り込む（一致しないものは隠す）");
        const ImU32 ic = filtered ? u.t->accent : withAlpha(u.t->muted, btnHov ? 1.f : cellHov ? 0.7f : 0.25f);
        drawIcon(dl, "filter", ImVec2(bp.x + (fw - u.m.s(13)) / 2, pos.y + (h - u.m.s(13)) / 2), u.m.s(13), ic);
        drawColumnFilterPopup(f, fk, title, ImVec2(bp.x, pos.y + h));
      }
      if (sortable) {
        ImGui::SetCursorScreenPos(ImVec2(x, pos.y));
        if (ImGui::InvisibleButton("##sort", ImVec2(cd.w, h))) {
          // 昇順 → 降順 → 解除
          if (u.sortKey != colKey(cd.col)) {
            u.sortKey = colKey(cd.col);
            u.sortDir = 1;
          } else if (u.sortDir == 1) {
            u.sortDir = -1;
          } else {
            u.sortDir = 0;
            u.sortKey.clear();
          }
        }
      }
      ImGui::PopID();
    }
    x += cd.w;
  }
}

void drawTableRow(Frame& f, const VisibleRow& r, ImVec2 pos, float width, const std::vector<ColDef>& cols, bool flat,
                  bool highlight) {
  app::AppController& c = f.c;
  UiState& u = f.u;
  Session* s = c.session();
  const Tree& T = *s->tree();
  const auto& R = *s->rows();
  const Issue& o = s->viewIssues()[r.id];
  const ScheduleRow& row = R[r.id];
  const std::string id = o.id;
  const float h = u.m.rowH();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 end(pos.x + width, pos.y + h);
  const bool sel = c.isSelected(id);
  const bool hov = ImGui::IsMouseHoveringRect(pos, end) && ImGui::IsWindowHovered();
  registerDropTarget(f, id, pos, end);  // ここへファイルをドロップすると添付
  if (sel) dl->AddRectFilled(pos, end, u.t->selbg);
  else if (hov) dl->AddRectFilled(pos, end, u.t->hover);
  if (highlight) dl->AddRectFilled(pos, ImVec2(pos.x + u.m.s(3), end.y), u.t->accent);
  if (r.depth == 0 && !flat) dl->AddLine(pos, ImVec2(end.x, pos.y), u.t->line);
  dl->AddLine(ImVec2(pos.x, end.y - 1), ImVec2(end.x, end.y - 1), u.t->rowline);

  ImGui::PushID(id.c_str());
  // 行全体の当たり判定（上に載る部品を優先させる）
  ImGui::SetCursorScreenPos(pos);
  ImGui::SetNextItemAllowOverlap();
  ImGui::InvisibleButton("##row", ImVec2(width, h));
  const bool rowClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
  const bool rowDbl = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
  const bool rowRight = ImGui::IsItemClicked(ImGuiMouseButton_Right);
  Col dblCol = Col::Name;

  float x = pos.x;
  for (const auto& cd : cols) {
    const ImVec2 cp(x, pos.y), cs(cd.w, h);
    const bool inCell = ImGui::IsMouseHoveringRect(cp, ImVec2(x + cd.w, pos.y + h));
    if (inCell) dblCol = cd.col;
    switch (cd.col) {
      case Col::Grip: {
        drawIcon(dl, "grip", ImVec2(x + u.m.s(1), pos.y + (h - u.m.s(16)) / 2), u.m.s(16), withAlpha(u.t->muted, hov ? 1.f : 0.4f));
        ImGui::SetCursorScreenPos(cp);
        ImGui::InvisibleButton("##grip", cs);
        if (ImGui::IsItemHovered()) {
          ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
          tip("ドラッグして並べ替え・階層を変更");
        }
        if (!u.ro && ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 4.f)) u.rowDrag = {id, true};
        break;
      }
      case Col::Name: drawNameCell(f, r, cp, cs, NameCellOpts{true, !flat, flat}); break;
      case Col::Type: textIn(dl, u, cp, cs, o.type, u.t->muted); break;
      case Col::Status: {
        ImGui::SetCursorScreenPos(ImVec2(x + u.m.s(4), pos.y + (h - u.m.s(24)) / 2));
        if (r.isPar) {
          textIn(dl, u, cp, cs, std::to_string(row.nDone) + "/" + std::to_string(row.n), u.t->muted, u.fontMono);
        } else if (statusPill(u, "st", o.status, true)) {
          openStatusMenu(f, id);
        }
        break;
      }
      case Col::Asg:
        if (r.isPar) break;
        if (editing(u, id, EditField::Assignee)) {
          ImGui::SetCursorScreenPos(ImVec2(x, pos.y + (h - ImGui::GetFrameHeight()) / 2));
          drawInlineEdit(f, cd.w - u.m.s(2));
        } else {
          textIn(dl, u, cp, cs, o.assignee, u.t->ink);
        }
        break;
      case Col::Prio:
        if (!r.isPar) {
          const std::string pr = o.prio.empty() ? "Medium" : o.prio;
          textIn(dl, u, cp, cs, std::string(prioMark(pr)) + " " + pr, prioColor(u, pr));
        }
        break;
      case Col::Start: textIn(dl, u, cp, cs, fmtMd(row.asapStart), u.t->ink, u.fontMono); break;
      case Col::Finish: textIn(dl, u, cp, cs, fmtMd(row.asapFinish), u.t->ink, u.fontMono); break;
      case Col::Dur:
        if (r.isPar) textIn(dl, u, cp, cs, std::to_string(row.n) + "件", u.t->muted);
        else if (T.kind[r.id] == Kind::Ms) textIn(dl, u, cp, cs, "◆", u.t->muted);
        else if (editing(u, id, EditField::Dur)) {
          ImGui::SetCursorScreenPos(ImVec2(x, pos.y + (h - ImGui::GetFrameHeight()) / 2));
          drawInlineEdit(f, cd.w - u.m.s(2));
        } else {
          textIn(dl, u, cp, cs, csv::fmtNum(row.weeks) + "w", row.done ? u.t->muted : u.t->ink, u.fontMono);
        }
        break;
      case Col::Float: {
        ImGui::SetCursorScreenPos(ImVec2(x + u.m.s(4), pos.y + (h - ImGui::GetTextLineHeight() - u.m.s(4)) / 2));
        floatChip(u, row);
        break;
      }
      case Col::Deadline: {
        const OptDay d = T.isLeaf(r.id) ? T.dl[r.id] : T.ownDl[r.id];
        if (d) textIn(dl, u, cp, cs, fmtMd(*d), u.t->ink, u.fontMono);
        break;
      }
      case Col::Plan: {
        const BaselineCompare* bc = s->baselineCompare();
        if (!bc || static_cast<std::size_t>(r.id) >= bc->entries.size()) break;
        const BaselineEntry& e = bc->entries[static_cast<std::size_t>(r.id)];
        const std::string mark = baselineDiffMark(e);
        textIn(dl, u, cp, cs, (mark.empty() ? std::string() : mark + " ") + baselineDiffText(e), planColor(u, e), u.fontMono);
        if (inCell && ImGui::IsWindowHovered() && !u.linkDrag.active) {
          const auto lines = planTipLines(*bc, e);
          ImGui::BeginTooltip();
          for (std::size_t q = 0; q < lines.size(); ++q) {
            if (q == 0) ImGui::TextUnformatted(lines[q].c_str());
            else ImGui::TextDisabled("%s", lines[q].c_str());
          }
          ImGui::EndTooltip();
        }
        break;
      }
      case Col::Pred:
        if (editing(u, id, EditField::Pred)) {
          ImGui::SetCursorScreenPos(ImVec2(x, pos.y + (h - ImGui::GetFrameHeight()) / 2));
          drawInlineEdit(f, cd.w - u.m.s(2));
        } else if (const auto& dp = s->computed()->declPreds[static_cast<std::size_t>(r.id)]; !dp.empty()) {
          const std::string pt = formatPreds(T, dp);
          textIn(dl, u, cp, cs, pt, u.t->ink);
          // 幅に収まらないときは、全部をマウスの横に出す
          if (inCell && ImGui::IsWindowHovered() && ImGui::CalcTextSize(pt.c_str()).x > cd.w - u.m.s(8))
            ImGui::SetTooltip("先行: %s", pt.c_str());
        }
        break;
    }
    x += cd.w;
  }
  ImGui::PopID();

  if (rowClicked) rowClick(f, id);
  if (rowRight) openContext(f, id);
  if (rowDbl) {
    if (dblCol == Col::Asg && !r.isPar) beginEdit(f, id, EditField::Assignee);
    else if (dblCol == Col::Dur && !r.isPar && !row.done && T.kind[r.id] != Kind::Ms) beginEdit(f, id, EditField::Dur);
    else if (dblCol == Col::Pred) beginEdit(f, id, EditField::Pred);
    else if (dblCol == Col::Name || dblCol == Col::Type) beginEdit(f, id, EditField::Name);
    else openDetail(f, id, 0);
  }
}

namespace {
// 並べ替えのキー（SPEC 10.7.1）
std::vector<int> sortedLeaves(const Session& s, const std::vector<int>& leaves, const std::string& key, int dir) {
  const Tree& T = *s.tree();
  const BaselineCompare* bc = key == "plan" ? s.baselineCompare() : nullptr;
  // 列「計画との差」の並べ替え: 完了日の差（営業日）。差のないもの（計画になかった・比べられない）は最後
  const auto planDiff = [&](int id, bool& none) {
    none = true;
    if (!bc || static_cast<std::size_t>(id) >= bc->entries.size()) return 0;
    const BaselineEntry& e = bc->entries[static_cast<std::size_t>(id)];
    none = !e.hasFinishDiff;
    return e.finishDiff;
  };
  const auto& R = *s.rows();
  const auto& I = s.viewIssues();
  std::vector<int> v = leaves;
  // 先行の並べ替え用の文字（先行なしは最後）
  std::vector<std::string> predText;
  if (key == "pred") {
    predText.resize(static_cast<std::size_t>(T.n));
    for (int id : leaves) predText[static_cast<std::size_t>(id)] = formatPreds(T, s.computed()->declPreds[static_cast<std::size_t>(id)]);
  }
  const auto catRank = [&](int id) { return T.cat[id] == Cat::Todo ? 0 : T.cat[id] == Cat::Prog ? 1 : 2; };
  // 「最後」に置く値は向きに関係なく最後
  std::stable_sort(v.begin(), v.end(), [&](int a, int b) {
    int cmp = 0;
    bool lastA = false, lastB = false;
    if (key == "name") cmp = jaCompare(T.name[a], T.name[b]);
    else if (key == "type") cmp = jaCompare(I[a].type, I[b].type);
    else if (key == "status") cmp = catRank(a) - catRank(b);
    else if (key == "asg") {
      lastA = I[a].assignee.empty();
      lastB = I[b].assignee.empty();
      cmp = jaCompare(I[a].assignee, I[b].assignee);
    } else if (key == "prio") cmp = prioRank(I[a].prio) - prioRank(I[b].prio);
    else if (key == "start") cmp = R[a].asapStart - R[b].asapStart;
    else if (key == "finish") cmp = R[a].asapFinish - R[b].asapFinish;
    else if (key == "dur") cmp = R[a].weeks < R[b].weeks ? -1 : R[a].weeks > R[b].weeks ? 1 : 0;
    else if (key == "float") {
      lastA = R[a].done;
      lastB = R[b].done;
      cmp = R[a].floatDays - R[b].floatDays;
    } else if (key == "pred") {
      lastA = predText[a].empty();
      lastB = predText[b].empty();
      cmp = jaCompare(predText[a], predText[b]);
    } else if (key == "plan") {
      cmp = planDiff(a, lastA) - planDiff(b, lastB);
    } else if (key == "deadline") {
      lastA = !T.dl[a];
      lastB = !T.dl[b];
      cmp = (T.dl[a] && T.dl[b]) ? *T.dl[a] - *T.dl[b] : 0;
    }
    if (lastA != lastB) return lastB;
    if (lastA && lastB) return false;
    return dir > 0 ? cmp < 0 : cmp > 0;
  });
  return v;
}
}  // namespace

void drawListView(Frame& f, ImVec2 size) {
  app::AppController& c = f.c;
  UiState& u = f.u;
  Session* s = c.session();
  if (!s->tree()) return;
  const Tree& T = *s->tree();
  const float h = u.m.rowH(), headH = u.m.s(30);
  std::vector<ColDef> cols = {{Col::Name, 0},        {Col::Type, u.m.s(86)},  {Col::Status, u.m.s(96)},
                              {Col::Asg, u.m.s(110)},  {Col::Prio, u.m.s(80)},  {Col::Start, u.m.s(104)},
                              {Col::Finish, u.m.s(104)}, {Col::Dur, u.m.s(64)}, {Col::Float, u.m.s(72)},
                              {Col::Deadline, u.m.s(104)}};
  if (c.uiBool("cols", "pred", false)) cols.insert(cols.begin() + 1, {Col::Pred, u.m.s(140)});  // 件名の次
  if (s->baselineCompare() && c.uiBool("cols", "plan", false)) cols.push_back({Col::Plan, u.m.s(104)});  // 表示するベースラインがあるときだけ
  float fixed = 0;
  for (const auto& cd : cols) fixed += cd.w;
  // 表の左右には、依存をつなぐハンドルを出す余白を取る（行のボタンや開閉と重ならない）
  const float gut = u.m.s(kListGutter);
  const float width = std::max(size.x - u.m.s(14), fixed + u.m.s(260) + gut * 2);
  const float rowW = width - gut * 2;  // 表そのものの幅
  cols[0].w = rowW - fixed;

  // 行
  std::vector<VisibleRow> rows;
  const bool sorting = u.sortDir != 0 && !u.sortKey.empty();
  if (sorting) {
    for (int id : sortedLeaves(*s, c.visibleLeaves(), u.sortKey, u.sortDir))
      rows.push_back({id, T.depth[id], false, false});
  } else {
    rows = c.visibleRows();
  }
  const std::vector<char> dim = c.dimmedSet();
  u.order.clear();
  for (const auto& r : rows) u.order.push_back(T.ids[r.id]);

  // 依存をつなぐドラッグ（ハンドルは行の左右の余白。ゴムひもは同じ側の余白の中を通す）
  LinkOverlayOpts linkOpts;
  linkOpts.noun = "行";
  linkOpts.sameSide = true;
  linkOpts.hitR = kListGutter / 2;  // 余白の幅にちょうど収まる
  linkOpts.bend = kListGutter / 2;
  linkOpts.round = 0;
  linkOpts.horizontalScroll = true;
  ImGui::BeginChild("##list", size, ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  const float noteH = sorting ? u.m.s(26) : 0;
  // 見出しは縦スクロールで上に固定
  const float scrollY = ImGui::GetScrollY();
  ImGui::Dummy(ImVec2(width, headH + noteH + h * static_cast<float>(rows.size())));
  if (rows.empty()) {
    ImGui::SetCursorScreenPos(ImVec2(origin.x + gut + u.m.s(24), origin.y + headH + noteH + u.m.s(24)));
    ImGui::TextDisabled("%s", s->viewIssues().empty() ? S::kEmptyProject : S::kNoMatch);
  }
  // 見える範囲の行だけを描く（仮想化）
  const float top = headH + noteH;
  const float viewH = ImGui::GetWindowHeight();
  if (!u.revealId.empty()) {  // 「今日」ビューの行のダブルクリック: その課題の行が見える位置までスクロールする
    for (std::size_t k = 0; k < rows.size(); ++k)
      if (T.ids[rows[k].id] == u.revealId) {
        ImGui::SetScrollY(std::max(0.f, top + h * static_cast<float>(k) - viewH / 3));
        break;
      }
    u.revealId.clear();
  }
  const int first = std::max(0, static_cast<int>((scrollY - top) / h) - 1);
  const int last = std::min(static_cast<int>(rows.size()), static_cast<int>((scrollY + viewH - top) / h) + 2);
  {
    // 見出しの下だけに描く（見出しに隠れた行がクリックを取らないように）
    const ImVec2 wp = ImGui::GetWindowPos();
    ImGui::PushClipRect(ImVec2(wp.x, wp.y + top), ImVec2(wp.x + ImGui::GetWindowWidth(), wp.y + viewH), true);
    u.linkSlots.clear();
    const ImRect inner = ImGui::GetCurrentWindow()->InnerRect;  // スクロールバーを除いた、見えている範囲
    linkOpts.clipMin = ImVec2(inner.Min.x, std::max(inner.Min.y, origin.y + scrollY + top));
    linkOpts.clipMax = inner.Max;
    const float hr = u.m.s(kLinkHandleR);
    for (int i = first; i < last; ++i) {
      const ImVec2 p(origin.x + gut, origin.y + top + h * static_cast<float>(i));
      const std::string& sid = T.ids[rows[i].id];
      FadeScope fade(ImGui::GetWindowDrawList(), isDimmed(u, dim, rows[i].id, sid), dimAlpha(c));
      drawTableRow(f, rows[i], p, rowW, cols, sorting, sid == c.cursor());
      // 依存をつなぐ: 行の左右の余白にハンドル（右 → 後続、左 → 先行）
      UiState::LinkSlot sl;
      sl.idx = rows[i].id;
      sl.a = p;
      sl.b = ImVec2(p.x + rowW, p.y + h);
      sl.padX = gut;
      sl.hl = ImVec2(origin.x + gut / 2, p.y + h / 2);
      sl.hr = ImVec2(origin.x + width - gut / 2, p.y + h / 2);
      sl.sameL = ImVec2(sl.hl.x - hr, sl.hl.y);  // ゴムひもは左右の余白の中を通り、相手のハンドルの外側に付く
      sl.sameR = ImVec2(sl.hr.x + hr, sl.hr.y);
      linkSlotAdd(f, sl, linkOpts);
    }
    ImGui::PopClipRect();
  }
  drawLinkOverlay(f, app::View::List, linkOpts);
  // 見出し（最後に描いて前面に）
  const ImVec2 hp(origin.x + gut, origin.y + scrollY);
  drawTableHeader(f, hp, rowW, headH, cols, true);
  if (sorting) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(ImVec2(hp.x, hp.y + headH), ImVec2(hp.x + rowW, hp.y + headH + noteH), u.t->accentBg);
    dl->AddText(ImVec2(hp.x + u.m.s(10), hp.y + headH + u.m.s(5)), u.t->accent, S::kSortNote);
  }
  // 行のドロップ（並べ替え）はガントと同じ
  ImGui::EndChild();
}

}  // namespace roadmap::ui
