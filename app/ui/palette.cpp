// コマンドパレット（SPEC 10.10。Ctrl+K）
#include <algorithm>
#include <functional>
#include <imgui.h>
#include <imgui_stdlib.h>

#include "roadmap/text.hpp"
#include "ui/internal.hpp"

namespace roadmap::ui {

namespace {
struct Cmd {
  std::string group, title, key;
  std::function<void()> run;
};

std::vector<Cmd> buildCommands(Frame& f) {
  app::AppController& c = f.c;
  UiState& u = f.u;
  Session* s = c.session();
  std::vector<Cmd> v;
  const bool hasSel = !c.selection().empty();
  const std::string cur = c.cursor();
  const auto add = [&](const char* g, std::string t, const char* k, std::function<void()> fn) {
    v.push_back({g, std::move(t), k ? k : "", std::move(fn)});
  };
  if (s) {
    add("課題", "課題を追加（最上位）", nullptr, [&f] {
      const auto id = f.c.createIssue("", "");
      if (!id.empty()) beginEdit(f, id, EditField::Name, true);
    });
    if (hasSel) {
      add("課題", "選択した課題の子を追加", "Shift+N", [&f] {
        const auto id = f.c.addChild();
        if (!id.empty()) beginEdit(f, id, EditField::Name, true);
      });
      add("課題", "同じ階層に追加", "N", [&f] {
        const auto id = f.c.addSibling();
        if (!id.empty()) beginEdit(f, id, EditField::Name, true);
      });
      add("課題", "テンプレートから子タスクを追加", nullptr, [&f] { openTplApply(f); });
      if (!cur.empty() && s->tree() && s->tree()->find(cur) >= 0 && !s->tree()->kids[s->tree()->find(cur)].empty())
        add("課題", "子タスクをテンプレートとして保存", nullptr, [&f, cur] { openTplSave(f, cur); });
      if (!cur.empty()) add("課題", "名前を変更", "Enter / F2", [&f, cur] { beginEdit(f, cur, EditField::Name); });
      add("課題", "削除", "Del", [&f] { requestDelete(f, f.c.selection()); });
    }
    add("課題", "詳細パネルを開く・閉じる", "Space", [&u] { u.detailOpen = !u.detailOpen; });
    add("表示", "ガントを表示", "1", [&c] { c.setView(app::View::Gantt); });
    add("表示", "ボードを表示", "2", [&c] { c.setView(app::View::Board); });
    add("表示", "リストを表示", "3", [&c] { c.setView(app::View::List); });
    add("表示", "ギャップを表示", "4", [&c] { c.setView(app::View::Goals); });
    for (const auto& [k, l] : {std::pair<const char*, const char*>{"day", "日"}, {"week", "週"}, {"month", "月"}, {"all", "全体"}})
      add("表示", std::string("ガントの拡大率: ") + l, nullptr, [&c, &u, key = std::string(k)] {
        c.setUi("zoom", key);
        u.customPx = 0;
        u.scrollToBase = true;
      });
    for (const auto& [k, l] : {std::pair<const char*, const char*>{"root", "Epic"}, {"float", "余裕"}, {"asg", "担当者"}})
      add("表示", std::string("バーの色分け: ") + l, nullptr, [&c, key = std::string(k)] { c.setUi("colorBy", key); });
    add("表示", "すべて開く", nullptr, [&c] { c.setUi("collapsed", Json::object()); });
    add("表示", "すべて閉じる", nullptr, [&c] {
      const Tree* T = c.session()->tree();
      if (!T) return;
      Json col = Json::object();
      for (int id : T->dfs)
        if (!T->kids[id].empty()) col[T->ids[id]] = true;
      c.setUi("collapsed", col);
    });
    add("表示", "遅れを目立たせる（ほかを薄く）", nullptr, [&c] { c.filter().flt = FltFilter::Late; });
    add("表示", "余裕小を目立たせる（ほかを薄く）", nullptr, [&c] { c.filter().flt = FltFilter::Tight; });
    add("表示", "絞り込みを解除", nullptr, [&c] { c.clearFilter(); });
    add("表示", "未完了を目立たせる（ほかを薄く）", nullptr, [&c] { c.filter().flt = FltFilter::Open; });
    add("表示", "担当者が未割当の課題を目立たせる（ほかを薄く）", nullptr, [&c] { c.filter().asg = AsgFilter::Unassigned; });
  }
  add("表示", "密度の切替", nullptr, [&c] { c.setDensity(c.prefs().density == "compact" ? "normal" : "compact"); });
  add("表示", "テーマの切替（自動→ライト→ダーク）", nullptr, [&c] {
    const std::string t = c.prefs().theme;
    c.setTheme(t == "auto" ? "light" : t == "light" ? "dark" : "auto");
  });
  if (s) {
    add("計画", "同時作業の制限のオン/オフ", nullptr, [&c] {
      Session* ss = c.session();
      ss->setResource({!ss->settings().resource.on, ss->settings().resource.cap});
      c.afterEdit();
    });
    add("計画", "担当者の負荷を見る", nullptr, [&f] { openDialog(f, Dlg::Workload); });
    add("計画", "基準日を今日にする", nullptr, [&c] {
      c.session()->setBaseDate(c.clock().todayIso());
      c.afterEdit();
    });
    add("計画", "プロジェクト設定", nullptr, [&f] { openDialog(f, Dlg::Settings); });
    add("編集", "元に戻す", "Ctrl+Z", [&c] { c.undo(); });
    add("編集", "やり直す", "Ctrl+Shift+Z", [&c] { c.redo(); });
  }
  add("アプリ", "アプリの設定", "Ctrl+,", [&f] { openDialog(f, Dlg::AppSettings); });
  add("アプリ", "保存先を変える", nullptr, [&f] { openDialog(f, Dlg::AppSettings); });
  add("プロジェクト", "新しいプロジェクト", nullptr, [&f] { openDialog(f, Dlg::NewProject); });
  if (s) {
    add("プロジェクト", "名前・キーを変更", nullptr, [&f] { openDialog(f, Dlg::EditProject); });
    add("プロジェクト", "複製", nullptr, [&c] { c.duplicateProject(); });
  }
  add("プロジェクト", "CSVを取り込む", nullptr, [&f] { openDialog(f, Dlg::Import); });
  if (s) add("プロジェクト", "書き出し", nullptr, [&f] {
    f.u.exportKind = "jira";
    openDialog(f, Dlg::Export);
  });
  add("プロジェクト", "全プロジェクトをバックアップ", nullptr, [&f] {
    f.u.exportKind = "backup";
    openDialog(f, Dlg::Export);
  });
  add("プロジェクト", "ショートカット一覧", "?", [&f] { openDialog(f, Dlg::Shortcuts); });
  for (const auto& p : c.projects())
    if (p.id != c.currentId())
      add("プロジェクトを開く", p.name, nullptr, [&c, id = p.id] { c.openProject(id); });
  return v;
}
}  // namespace

void drawPalette(Frame& f) {
  app::AppController& c = f.c;
  UiState& u = f.u;
  if (!u.paletteOpen) {
    if (ImGui::BeginPopupModal("##palette")) {
      ImGui::CloseCurrentPopup();
      ImGui::EndPopup();
    }
    return;
  }
  if (!ImGui::IsPopupOpen("##palette")) ImGui::OpenPopup("##palette");
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  const float w = std::min(u.m.s(640), vp->WorkSize.x - u.m.s(40));
  ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + (vp->WorkSize.x - w) / 2, vp->WorkPos.y + vp->WorkSize.y * 0.12f));
  ImGui::SetNextWindowSize(ImVec2(w, 0));
  if (!ImGui::BeginPopupModal("##palette", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize)) {
    u.paletteOpen = false;
    return;
  }
  // 外側のクリックで閉じる
  if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)) {
    u.paletteOpen = false;
    ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
    return;
  }
  if (u.paletteFocus) {
    ImGui::SetKeyboardFocusHere();
    u.paletteFocus = false;
  }
  ImGui::SetNextItemWidth(-1);
  ImGui::InputTextWithHint("##pq", S::kPaletteHint, &u.paletteQuery);
  // 一覧
  struct Item {
    std::string group, title, key;
    std::function<void()> run;
  };
  std::vector<Item> items;
  const std::string q = text::toLower(text::trim(u.paletteQuery));
  const auto words = text::splitAny(q, U"", true);
  const auto match = [&](const std::string& hay) {
    const std::string h = text::toLower(hay);
    for (const auto& w2 : words)
      if (h.find(w2) == std::string::npos) return false;
    return true;
  };
  for (auto& cmd : buildCommands(f)) {
    if (!match(cmd.group + " " + cmd.title)) continue;
    items.push_back({cmd.group, cmd.title, cmd.key, cmd.run});
    if (q.empty() && items.size() >= 40) break;
  }
  if (!q.empty() && c.session() && c.session()->tree()) {
    const Tree& T = *c.session()->tree();
    int n = 0;
    for (int id : T.dfs) {
      if (n >= 8) break;
      if (!match("課題へ移動 " + T.name[id] + " " + T.did[id])) continue;
      const std::string sid = T.ids[id];
      items.push_back({"課題へ移動", T.did[id] + "  " + T.name[id], "", [&f, sid] {
                         f.c.select(sid);
                         f.u.detailOpen = true;
                       }});
      ++n;
    }
  }
  u.paletteSel = std::clamp(u.paletteSel, 0, std::max(0, static_cast<int>(items.size()) - 1));
  bool composing = u.imeComposing;
  if (!composing && ImGui::IsKeyPressed(ImGuiKey_DownArrow)) u.paletteSel = std::min(u.paletteSel + 1, static_cast<int>(items.size()) - 1);
  if (!composing && ImGui::IsKeyPressed(ImGuiKey_UpArrow)) u.paletteSel = std::max(u.paletteSel - 1, 0);
  std::function<void()> toRun;
  bool close = false;
  if (!composing && ImGui::IsKeyPressed(ImGuiKey_Escape)) close = true;
  if (enterPressed(u) && !items.empty()) {
    toRun = items[u.paletteSel].run;
    close = true;
  }
  ImGui::BeginChild("##plist", ImVec2(0, std::min(u.m.s(420), u.m.s(30) * static_cast<float>(items.size()) + u.m.s(30) * 6)));
  std::string lastGroup;
  for (int k = 0; k < static_cast<int>(items.size()); ++k) {
    const auto& it = items[k];
    if (it.group != lastGroup) {
      ImGui::TextDisabled("%s", it.group.c_str());
      lastGroup = it.group;
    }
    ImGui::PushID(k);
    const bool sel = k == u.paletteSel;
    if (ImGui::Selectable(it.title.c_str(), sel)) {
      toRun = it.run;
      close = true;
    }
    if (sel && ImGui::IsKeyPressed(ImGuiKey_DownArrow, true)) ImGui::SetScrollHereY();
    if (sel && ImGui::IsKeyPressed(ImGuiKey_UpArrow, true)) ImGui::SetScrollHereY();
    if (!it.key.empty()) {
      ImGui::SameLine(ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(it.key.c_str()).x);
      ImGui::TextDisabled("%s", it.key.c_str());
    }
    ImGui::PopID();
  }
  if (items.empty()) ImGui::TextDisabled("見つかりません");
  ImGui::EndChild();
  if (close) {
    u.paletteOpen = false;
    u.paletteQuery.clear();
    u.paletteSel = 0;
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
  // パレットを閉じてから実行
  if (toRun) toRun();
}

}  // namespace roadmap::ui
