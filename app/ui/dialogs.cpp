// ダイアログ（SPEC 10.11）
#include <algorithm>
#include <cfloat>
#include <set>

#include <imgui.h>
#include <imgui_stdlib.h>

#include "roadmap/links.hpp"
#include "roadmap/issue_name.hpp"
#include "roadmap/project_io.hpp"
#include "roadmap/text.hpp"
#include "ui/internal.hpp"

namespace roadmap::ui {

namespace {

const char* titleOf(Dlg d) {
  switch (d) {
    case Dlg::NewProject: return "新しいプロジェクト";
    case Dlg::EditProject: return "名前・キーの変更";
    case Dlg::ConfirmDelete: return "プロジェクトを削除";
    case Dlg::Import: return "CSVを取り込む";
    case Dlg::Export: return "書き出し";
    case Dlg::Folder: return "保存先フォルダ";
    case Dlg::Settings: return "プロジェクト設定";
    case Dlg::Workload: return "担当者の負荷";
    case Dlg::Warnings: return "読み込み時の注意";
    case Dlg::Shortcuts: return "ショートカット一覧";
    case Dlg::TplApply: return "テンプレから子タスクを追加";
    case Dlg::TplSave: return "子タスクをテンプレとして保存";
    case Dlg::Conflict: return "同じプロジェクトがあります";
    case Dlg::ConfirmQuit: return "保存されていない変更があります";
    case Dlg::AppSettings: return "アプリの設定";
    case Dlg::ConfirmIssueDelete: return "課題を削除";
    case Dlg::PasteChildren: return "貼り付けた内容を子課題として追加";
    case Dlg::AttachOpenWarn:
    case Dlg::LinkOpenWarn: return "実行できるファイルです";
    case Dlg::AttachPreview: return "画像のプレビュー";
    case Dlg::ExportHtml: return "HTML に書き出す";
    case Dlg::Weekly: return "週次レポート";
    case Dlg::BaselineSave: return "ベースラインを保存";
    case Dlg::BaselineManage: return "ベースラインの管理";
    case Dlg::ConfirmBaselineDelete: return "ベースラインを削除";
    default: return "";
  }
}

float widthOf(const UiState& u, Dlg d) {
  switch (d) {
    case Dlg::ConfirmDelete:
    case Dlg::ConfirmIssueDelete:
    case Dlg::AttachOpenWarn:
    case Dlg::LinkOpenWarn:
    case Dlg::Conflict:
    case Dlg::ConfirmBaselineDelete:
    case Dlg::ConfirmQuit: return u.m.s(460);
    case Dlg::BaselineSave: return u.m.s(560);
    case Dlg::BaselineManage: return u.m.s(720);
    case Dlg::Import:
    case Dlg::ExportHtml:
    case Dlg::Export: return u.m.s(640);
    case Dlg::Weekly: return u.m.s(820);
    case Dlg::Folder: return u.m.s(600);
    case Dlg::Settings: return u.m.s(620);
    case Dlg::AppSettings: return u.m.s(640);
    case Dlg::Workload: return u.m.s(720);
    case Dlg::AttachPreview: return u.m.s(780);
    case Dlg::Warnings: return u.m.s(560);
    case Dlg::Shortcuts: return u.m.s(620);
    default: return u.m.s(520);
  }
}

void close(Frame& f) {
  // 週次レポートの所感はプロジェクトに覚える（次の週の下書きに前回の所感を残す）
  if (f.u.dlg == Dlg::Weekly && f.c.currentId() == f.u.wkProject) f.c.setWeeklyNotes(f.u.wk.notes);
  f.u.dlg = Dlg::None;
  f.u.blRenameId.clear();
  f.u.paste = Outline{};  // 貼り付けた内容は残さない
  f.u.pasteParent.clear();
  ImGui::CloseCurrentPopup();
}

// 下端のボタン列（右寄せ。主ボタンは右端）。押されたボタンの番号（0 = 主ボタン）を返す。なければ -1
// primaryEnabled = false のときは、主ボタンだけ押せない（副ボタンは押せる）
int buttons(Frame& f, std::initializer_list<const char*> labels, bool danger = false, bool primaryEnabled = true) {
  UiState& u = f.u;
  ImGui::Spacing();
  ImGui::Separator();
  float total = 0;
  for (const char* l : labels) total += ImGui::CalcTextSize(l).x + u.m.s(40);
  ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowWidth() - total - u.m.s(12)));
  int pressed = -1;
  std::vector<const char*> v(labels);
  // 副ボタンを先に（左）、主ボタン（v[0]）を最後（右端）に
  for (std::size_t k = 1; k < v.size(); ++k) {
    if (ImGui::Button(v[k], ImVec2(ImGui::CalcTextSize(v[k]).x + u.m.s(28), 0))) pressed = static_cast<int>(k);
    ImGui::SameLine();
  }
  ImGui::PushStyleColor(ImGuiCol_Button, v4(danger ? u.t->late : u.t->accent));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, v4(withAlpha(danger ? u.t->late : u.t->accent, 0.85f)));
  ImGui::PushStyleColor(ImGuiCol_Text, v4(u.t->accentInk));
  if (!primaryEnabled) ImGui::BeginDisabled();
  if (ImGui::Button(v[0], ImVec2(ImGui::CalcTextSize(v[0]).x + u.m.s(28), 0)) && primaryEnabled) pressed = 0;
  if (!primaryEnabled) ImGui::EndDisabled();
  ImGui::PopStyleColor(3);
  return pressed;
}

void dlgNewProject(Frame& f) {
  UiState& u = f.u;
  if (ImGui::IsWindowAppearing()) {
    u.dName = "新しいプロジェクト";
    u.dKey.clear();
    u.dChoice = 0;
    ImGui::SetKeyboardFocusHere();
  }
  ImGui::TextUnformatted("名前");
  ImGui::SetNextItemWidth(-1);
  ImGui::InputText("##name", &u.dName);
  const bool enter = ImGui::IsItemFocused() && enterPressed(u);
  ImGui::TextUnformatted("キー（例: DEV）");
  ImGui::SetNextItemWidth(u.m.s(160));
  ImGui::InputText("##key", &u.dKey, ImGuiInputTextFlags_CharsUppercase);
  ImGui::TextUnformatted("雛形");
  ImGui::RadioButton("空", &u.dChoice, 0);
  ImGui::RadioButton("階層デモ（5階層の例）", &u.dChoice, 1);
  ImGui::RadioButton("組込みソフトウェア開発の雛形（要求〜量産移管）", &u.dChoice, 2);
  const int b = buttons(f, {"作成", "キャンセル"});
  if (b == 0 || enter) {
    const auto kind = u.dChoice == 1   ? app::SampleKind::Hierarchy
                      : u.dChoice == 2 ? app::SampleKind::EmbeddedSw
                                       : app::SampleKind::Empty;
    if (u.dChoice == 2 && text::trim(u.dName) == "新しいプロジェクト") u.dName = "組込みソフトウェア開発";
    if (u.dChoice == 2 && u.dKey.empty()) u.dKey = "DEV";
    f.c.createProject(kind, text::trim(u.dName), u.dKey);
    close(f);
  } else if (b == 1) {
    close(f);
  }
}

void dlgEditProject(Frame& f) {
  UiState& u = f.u;
  Session* s = f.c.session();
  if (!s) return close(f);
  if (ImGui::IsWindowAppearing()) {
    u.dName = s->settings().name;
    u.dKey = s->settings().key;
    ImGui::SetKeyboardFocusHere();
  }
  ImGui::TextUnformatted("名前");
  ImGui::SetNextItemWidth(-1);
  ImGui::InputText("##name", &u.dName);
  ImGui::TextUnformatted("キー");
  ImGui::SetNextItemWidth(u.m.s(160));
  ImGui::InputText("##key", &u.dKey, ImGuiInputTextFlags_CharsUppercase);
  ImGui::PushTextWrapPos(0);
  ImGui::TextDisabled("キーは、これから追加する課題の番号に使います。すでにある課題のキーは変わりません。");
  ImGui::PopTextWrapPos();
  if (ImGui::Button("複製")) {
    f.c.duplicateProject();
    return close(f);
  }
  ImGui::SameLine();
  ImGui::PushStyleColor(ImGuiCol_Text, v4(u.t->late));
  const bool del = ImGui::Button("削除");
  ImGui::PopStyleColor();
  const int b = buttons(f, {"保存", "キャンセル"});
  if (b == 0) {
    f.c.editProject(text::trim(u.dName), u.dKey);
    close(f);
  } else if (b == 1) {
    close(f);
  } else if (del) {
    close(f);
    openDialog(f, Dlg::ConfirmDelete);
  }
}

void dlgConfirmDelete(Frame& f) {
  Session* s = f.c.session();
  if (!s) return close(f);
  ImGui::PushTextWrapPos(0);
  ImGui::Text("「%s」を削除します。", s->settings().name.c_str());
  ImGui::TextDisabled("プロジェクトのフォルダ（CSV・設定・添付・バックアップ）ごと、データフォルダの「_削除済み」へ移します（そこから戻せます）。");
  ImGui::PopTextWrapPos();
  const int b = buttons(f, {"削除", "キャンセル"}, true);
  if (b == 0) {
    f.c.deleteCurrentProject();
    close(f);
  } else if (b == 1) {
    close(f);
  }
}

void dlgImport(Frame& f) {
  UiState& u = f.u;
  if (ImGui::IsWindowAppearing()) {
    u.importError.clear();
    if (u.importName.empty()) u.importName = "取り込んだCSV";
  }
  if (ImGui::Button("ファイルを選ぶ…")) {
    if (auto r = f.p.openTextFile()) {
      u.dText = r->first;
      std::string nm = r->second;
      for (const char* ext : {".csv", ".CSV", ".json", ".JSON", ".tsv", ".txt"}) {
        const std::string e(ext);
        if (nm.size() > e.size() && nm.compare(nm.size() - e.size(), e.size(), e) == 0) nm = nm.substr(0, nm.size() - e.size());
      }
      u.importName = nm;
    }
  }
  ImGui::SameLine();
  ImGui::TextDisabled("JIRA互換のCSV（1行目に列名）、または共有・バックアップのJSON。UTF-8 / Shift_JIS を自動判定します");
  ImGui::InputTextMultiline("##paste", &u.dText, ImVec2(-1, ImGui::GetTextLineHeightWithSpacing() * 9));
  const bool isJson = !text::trimStart(u.dText).empty() && text::trimStart(u.dText)[0] == '{';
  if (!isJson) {
    ImGui::TextUnformatted("取り込み方法");
    int mode = u.importReplace ? 1 : 0;
    ImGui::RadioButton("新しいプロジェクトとして追加", &mode, 0);
    if (mode == 0) {
      ImGui::SameLine();
      ImGui::SetNextItemWidth(u.m.s(220));
      ImGui::InputText("##iname", &u.importName);
    }
    ImGui::BeginDisabled(f.c.session() == nullptr);
    ImGui::RadioButton("今のプロジェクトを置き換え（Undoで戻せる）", &mode, 1);
    ImGui::EndDisabled();
    u.importReplace = mode == 1;
  } else {
    ImGui::TextDisabled("JSON（共有・バックアップ）として読み込みます");
  }
  if (!u.importError.empty()) ImGui::TextColored(v4(u.t->late), "%s", u.importError.c_str());
  const int b = buttons(f, {"取り込む", "キャンセル"});
  if (b == 0) {
    auto r = f.c.importText(u.dText, text::trim(u.importName), u.importReplace ? app::ImportMode::Replace : app::ImportMode::New);
    if (!r.ok) {
      u.importError = r.error;
    } else {
      u.dText.clear();
      u.importName.clear();
      close(f);
      if (!r.conflictNames.empty()) {
        u.conflictNames = r.conflictNames;
        openDialog(f, Dlg::Conflict);
      }
    }
  } else if (b == 1) {
    close(f);
  }
}

void dlgConflict(Frame& f) {
  UiState& u = f.u;
  ImGui::PushTextWrapPos(0);
  ImGui::Text("同じプロジェクトがあります: %s", text::join(u.conflictNames, "、").c_str());
  ImGui::TextDisabled("上書きすると、手元の内容は取り込んだ内容で置き換わります。");
  ImGui::PopTextWrapPos();
  const int b = buttons(f, {"上書き", "キャンセル", "別のプロジェクトとして追加"});
  if (b == 0) f.c.confirmJsonImport(true), close(f);
  else if (b == 2) f.c.confirmJsonImport(false), close(f);
  else if (b == 1) close(f);
}

void dlgExport(Frame& f) {
  UiState& u = f.u;
  const bool backup = u.exportKind == "backup";
  if (!backup) {
    ImGui::TextUnformatted("形式");
    for (const auto& [k, l] : {std::pair<const char*, const char*>{"jira", "JIRAに取り込む用"},
                               {"app", "このアプリ用CSV（入力した値のまま）"}, {"share", "共有用JSON（このプロジェクトだけ）"}})
      if (ImGui::RadioButton(l, u.exportKind == k)) u.exportKind = k;
  } else {
    ImGui::TextUnformatted("全プロジェクトのバックアップ（JSON）");
  }
  std::string text = f.c.exportText(u.exportKind);
  ImGui::InputTextMultiline("##out", &text, ImVec2(-1, ImGui::GetTextLineHeightWithSpacing() * 12), ImGuiInputTextFlags_ReadOnly);
  int lines = 0;
  for (char ch : text) lines += ch == '\n';
  ImGui::TextDisabled("%d行 / %.1f KB", lines, static_cast<double>(text.size()) / 1024.0);
  const int b = buttons(f, {"ファイルに保存", "閉じる", "コピー"});
  if (b == 0) {
    if (f.p.saveTextFile(f.c.exportFileName(u.exportKind), text)) f.c.toast("保存しました");
  } else if (b == 2) {
    ImGui::SetClipboardText(text.c_str());
    f.c.toast("コピーしました");
  } else if (b == 1) {
    close(f);
  }
}

// 報告（HTML）の書き出し（Issue #42）。内容・範囲・見た目を選び、「書き出す…」で保存先を選んで書く
void dlgExportHtml(Frame& f) {
  UiState& u = f.u;
  app::AppController& c = f.c;
  Session* s = c.session();
  if (!s || !s->tree()) return close(f);
  ExportOptions& r = u.rep;
  if (ImGui::IsWindowAppearing()) {
    // 画面の今の状態から始める（拡大率・色分け）。テーマは印刷向けのライトが既定
    r = ExportOptions{};
    const std::string z = c.uiStr("zoom", "week");
    r.zoom = (z == "day" || z == "month" || z == "all") ? z : "week";
    const std::string cb = c.uiStr("colorBy", "root");
    r.colorBy = (cb == "float" || cb == "asg") ? cb : "root";
    u.repScope = 0;
  }
  ImGui::PushTextWrapPos(0);
  ImGui::TextUnformatted("ガント・統計・リストを、ブラウザで開ける1つのHTMLファイルに書き出します。外部のファイルは読まず、印刷（PDFとして保存）にも使えます。");
  ImGui::PopTextWrapPos();

  ImGui::SeparatorText("書き出す内容");
  ImGui::Checkbox("ガント", &r.gantt);
  ImGui::SameLine(0, u.m.s(20));
  ImGui::Checkbox("統計", &r.stats);
  ImGui::SameLine(0, u.m.s(20));
  ImGui::Checkbox("リスト", &r.list);

  const bool sel1 = c.selection().size() == 1 && s->issue(c.selection()[0]) != nullptr;
  if (r.gantt || r.list) {
    ImGui::SeparatorText("ガントとリストの範囲");
    ImGui::RadioButton("プロジェクト全体", &u.repScope, 0);
    ImGui::SameLine(0, u.m.s(16));
    ImGui::RadioButton("今の表示のまま（絞り込み・折りたたみ）", &u.repScope, 1);
    if (!sel1 && u.repScope == 2) u.repScope = 0;
    ImGui::BeginDisabled(!sel1);
    ImGui::RadioButton("選んだ課題とその配下", &u.repScope, 2);
    ImGui::EndDisabled();
    if (!sel1) {
      ImGui::SameLine();
      ImGui::TextDisabled("（課題を1つ選ぶと使えます）");
    }
  }
  if (r.gantt) {
    ImGui::SeparatorText("ガントの見た目");
    ImGui::TextUnformatted("拡大率");
    ImGui::SameLine();
    for (const auto& [k, l] : {std::pair<const char*, const char*>{"day", "日"}, {"week", "週"}, {"month", "月"}, {"all", "全体"}}) {
      if (ImGui::RadioButton(l, r.zoom == k)) r.zoom = k;
      ImGui::SameLine();
    }
    ImGui::NewLine();
    ImGui::TextUnformatted("色分け");
    ImGui::SameLine();
    for (const auto& [k, l] : {std::pair<const char*, const char*>{"root", "Epic"}, {"float", "余裕"}, {"asg", "担当者"}}) {
      if (ImGui::RadioButton(l, r.colorBy == k)) r.colorBy = k;
      ImGui::SameLine();
    }
    ImGui::NewLine();
    ImGui::Checkbox("依存の矢印を出す", &r.showDeps);
  }
  if (const Baseline* bl = s->shownBaseline(); bl && (r.gantt || r.stats)) {
    ImGui::SeparatorText("ベースライン");
    const std::string l = "ベースライン「" + bl->name + "」との差を入れる（ガントに計画のバー、統計に「計画との差」）";
    ImGui::Checkbox(l.c_str(), &r.baseline);
  }
  ImGui::SeparatorText("色");
  if (ImGui::RadioButton("ライト（印刷向け・既定）", !r.dark)) r.dark = false;
  ImGui::SameLine(0, u.m.s(16));
  if (ImGui::RadioButton("ダーク", r.dark)) r.dark = true;

  // 個人情報の注意
  ImGui::Spacing();
  ImGui::PushTextWrapPos(0);
  ImGui::PushStyleColor(ImGuiCol_Text, v4(u.t->tight));
  ImGui::TextUnformatted("担当者名・件名がファイルに入ります。メールや共有フォルダに置く前に、内容を確かめてください。");
  ImGui::PopStyleColor();
  ImGui::PopTextWrapPos();
  const std::size_t n = s->tree() ? static_cast<std::size_t>(s->tree()->n) : 0;
  if (r.gantt && n > kReportMaxGanttRows)
    ImGui::TextDisabled("課題が %zu 件あります。ガントは先頭の %zu 行までです（リストには全部出ます）。", n, kReportMaxGanttRows);
  const bool any = r.gantt || r.stats || r.list;
  if (!any) {
    ImGui::PushStyleColor(ImGuiCol_Text, v4(u.t->late));
    ImGui::TextUnformatted("書き出す内容を1つ以上選んでください。");
    ImGui::PopStyleColor();
  }

  const int b = buttons(f, {"書き出す…", "閉じる"}, false, any);
  if (b == 0) {
    ExportOptions opt = r;
    opt.scope = u.repScope == 1 ? ReportScope::Current : u.repScope == 2 ? ReportScope::Subtree : ReportScope::All;
    const std::string name = c.reportFileName();
    close(f);
    // ダイアログを閉じて、描画が終わってから保存先を選んで書く（OS の保存ダイアログが出る間、画面の参照を持たない）
    deferAfterFrame(f, [&f, opt, name] {
      const auto path = f.p.pickSaveFile(name, "html", "HTML ファイル");
      if (!path) return;  // やめた
      (void)f.c.exportReport(*path, opt);
    });
  } else if (b == 1) {
    close(f);
  }
}

// 週次レポート（Issue #43）。期間・項目・所感を選ぶと、Markdown のプレビューが出る。コピー・保存は明示的な操作
void dlgWeekly(Frame& f) {
  UiState& u = f.u;
  app::AppController& c = f.c;
  Session* s = c.session();
  if (!s || !s->tree()) return close(f);
  WeeklyOptions& o = u.wk;
  if (ImGui::IsWindowAppearing()) {
    u.wkProject = c.currentId();
    o = WeeklyOptions{};
    o.notes = c.weeklyNotes();  // 前回の所感を下書きに残す
    u.wkPeriod = 0;
    const WeeklyRange tw = weeklyRange(s->baseDay(), WeeklyPeriod::ThisWeek, 0, 0);
    u.wkFrom = fmtIso(tw.from);
    u.wkTo = fmtIso(tw.to);
    u.wkPreviewKey.clear();
  }
  if (c.currentId() != u.wkProject) return close(f);  // 開いたあとにプロジェクトが切り替わった
  ImGui::PushTextWrapPos(0);
  ImGui::TextUnformatted("今週の完了・来週の予定・遅れ・リスクを、数字と一覧は自動で、所感だけ自分で書く週報の下書きにします。");
  ImGui::PopTextWrapPos();

  ImGui::SeparatorText("期間（月曜始まり。基準日に合わせます）");
  ImGui::RadioButton("今週", &u.wkPeriod, 0);
  ImGui::SameLine(0, u.m.s(16));
  ImGui::RadioButton("先週", &u.wkPeriod, 1);
  ImGui::SameLine(0, u.m.s(16));
  ImGui::RadioButton("任意の範囲", &u.wkPeriod, 2);
  o.period = u.wkPeriod == 1 ? WeeklyPeriod::LastWeek : u.wkPeriod == 2 ? WeeklyPeriod::Custom : WeeklyPeriod::ThisWeek;
  if (o.period == WeeklyPeriod::Custom) {
    ImGui::SetNextItemWidth(u.m.s(120));
    ImGui::InputText("##wkfrom", &u.wkFrom);
    ImGui::SameLine();
    ImGui::TextUnformatted("〜");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(u.m.s(120));
    ImGui::InputText("##wkto", &u.wkTo);
    ImGui::SameLine();
    ImGui::TextDisabled("（例 2026-10-19）");
    const OptDay a = parseIso(text::trim(u.wkFrom)), b = parseIso(text::trim(u.wkTo));
    if (a && b) {
      o.from = *a;
      o.to = *b;
    } else {
      const WeeklyRange tw = weeklyRange(s->baseDay(), WeeklyPeriod::ThisWeek, 0, 0);
      o.from = tw.from;
      o.to = tw.to;
      ImGui::PushStyleColor(ImGuiCol_Text, v4(u.t->late));
      ImGui::TextUnformatted("日付を YYYY-MM-DD の形で入力してください（入力できるまで今週の範囲を使います）。");
      ImGui::PopStyleColor();
    }
  }
  {
    const WeeklyRange g = weeklyRange(s->baseDay(), o.period, o.from, o.to);
    int wd = 0;
    for (Day d = g.from; d <= g.to; ++d)
      if (s->calendar().isWork(d)) ++wd;
    ImGui::TextDisabled("%s 〜 %s（営業日 %d 日）／ 基準日 %s", fmtLong(g.from).c_str(), fmtLong(g.to).c_str(), wd, fmtLong(s->baseDay()).c_str());
  }

  ImGui::SeparatorText("入れる項目");
  WeeklySections& sec = o.sections;
  ImGui::Checkbox("サマリー", &sec.summary);
  ImGui::SameLine(0, u.m.s(16));
  ImGui::Checkbox("完了", &sec.done);
  ImGui::SameLine(0, u.m.s(16));
  ImGui::Checkbox("進行中", &sec.progress);
  ImGui::SameLine(0, u.m.s(16));
  ImGui::Checkbox("次の予定", &sec.next);
  ImGui::SameLine(0, u.m.s(16));
  ImGui::Checkbox("遅れ・余裕小", &sec.risks);
  ImGui::Checkbox("マイルストーン", &sec.milestones);
  ImGui::SameLine(0, u.m.s(16));
  ImGui::Checkbox("担当別", &sec.assignees);
  ImGui::SameLine(0, u.m.s(16));
  ImGui::Checkbox("先週から変わったこと", &sec.changes);
  ImGui::SameLine(0, u.m.s(16));
  ImGui::Checkbox("所感・課題・お願い", &sec.notes);
  {
    // 表示するベースラインがあるときだけ選べる（既定はオフ）
    const Baseline* bl = s->shownBaseline();
    ImGui::BeginDisabled(bl == nullptr);
    ImGui::Checkbox("ベースラインとの差", &sec.baseline);
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (bl) ImGui::TextDisabled("（「%s」と比べます）", bl->name.c_str());
    else ImGui::TextDisabled("（「ベースラインの管理…」で表示するものを選ぶと使えます）");
    if (!bl) sec.baseline = false;
  }
  const bool any = sec.any();

  if (sec.notes) {
    ImGui::SeparatorText("所感・課題・お願い（自由に書きます。プロジェクトに覚え、次回の下書きに残ります）");
    ImGui::InputTextMultiline("##wknotes", &o.notes, ImVec2(-1, u.m.s(72)));
    if (ImGui::IsItemDeactivatedAfterEdit()) c.setWeeklyNotes(o.notes);
    if (o.notes.size() > kWeeklyMaxNoteBytes) {
      ImGui::PushStyleColor(ImGuiCol_Text, v4(u.t->tight));
      ImGui::TextUnformatted("長すぎる分は切って出力します。");
      ImGui::PopStyleColor();
    }
  }

  // プレビュー（条件が変わったときだけ作り直す）
  std::string key = std::to_string(s->version()) + "|" + std::to_string(u.wkPeriod) + "|" + std::to_string(o.from) + "|" + std::to_string(o.to) + "|";
  for (bool b : {sec.summary, sec.done, sec.progress, sec.next, sec.risks, sec.milestones, sec.assignees, sec.changes, sec.notes, sec.baseline}) key += b ? '1' : '0';
  key += "|" + o.notes;
  if (key != u.wkPreviewKey) {
    const auto r = c.renderWeekly(o, false);
    u.wkPreview = r.ok() ? r.value() : std::string();
    u.wkError = r.ok() ? std::string() : r.error();
    u.wkPreviewKey = key;
  }
  ImGui::SeparatorText("プレビュー（Markdown）");
  if (!u.wkError.empty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, v4(u.t->late));
    ImGui::TextWrapped("作れません: %s", u.wkError.c_str());
    ImGui::PopStyleColor();
  } else if (!any) {
    ImGui::TextDisabled("入れる項目を1つ以上選んでください。");
  }
  ImGui::PushFont(u.fontMono);
  ImGui::InputTextMultiline("##wkprev", &u.wkPreview, ImVec2(-1, u.m.s(200)), ImGuiInputTextFlags_ReadOnly);
  ImGui::PopFont();

  ImGui::Spacing();
  ImGui::PushTextWrapPos(0);
  ImGui::PushStyleColor(ImGuiCol_Text, v4(u.t->tight));
  ImGui::TextUnformatted("担当者名・件名が出力に入ります。チャットや共有フォルダに置く前に、内容を確かめてください。");
  ImGui::PopStyleColor();
  ImGui::PopTextWrapPos();
  ImGui::TextDisabled("コピー・保存すると、そのときの課題の状況を覚えておき、次回の「先週から変わったこと」に使います。");

  const bool ok = any && u.wkError.empty();
  const int b = buttons(f, {"Markdown をコピー", "閉じる", "HTML を保存…", "Markdown を保存…"}, false, ok);
  if (b == 1) return close(f);
  if (b < 0) return;
  if (!ok) {
    c.toast("入れる項目を1つ以上選んでください");
    return;
  }
  WeeklyOptions opt = o;
  c.setWeeklyNotes(opt.notes);
  if (b == 0) {
    // クリップボードへ。描画が終わってから行う（設定への記録が描画中の参照に触れないように）
    deferAfterFrame(f, [&f, opt] {
      const auto md = f.c.renderWeekly(opt, false);
      if (!md.ok()) {
        f.c.toast("コピーできません: " + md.error());
      } else if (f.p.setClipboardText(md.value())) {
        f.c.recordWeekly(opt);
        f.c.toast("Markdown をクリップボードにコピーしました");
      } else {
        f.c.toast("コピーできませんでした（クリップボードを使えません）");
      }
    });
    return;
  }
  const bool html = b == 2;
  const std::string name = c.weeklyFileName(opt, html);
  // 保存先を選んで書く（OS の保存ダイアログが出る間、画面の参照を持たないよう描画のあとに行う）
  deferAfterFrame(f, [&f, opt, html, name] {
    const auto path = f.p.pickSaveFile(name, html ? "html" : "md", html ? "HTML ファイル" : "Markdown ファイル");
    if (!path) return;  // やめた
    (void)f.c.exportWeekly(*path, opt, html);
  });
}

// ベースラインを保存（Issue #53）。名前を付けて、いまの日程を残す
void dlgBaselineSave(Frame& f) {
  UiState& u = f.u;
  app::AppController& c = f.c;
  Session* s = c.session();
  if (!s || !s->tree()) return close(f);
  if (ImGui::IsWindowAppearing()) {
    u.dName = c.defaultBaselineName();
    ImGui::SetKeyboardFocusHere();
  }
  const std::size_t have = s->baselines().list.size();
  const bool full = have >= kBaselineMax;
  ImGui::PushTextWrapPos(0);
  ImGui::TextUnformatted("いまの各課題の、見込みの開始日・完了日・期限と状況を、名前をつけて保存します。あとで今の見込みと比べ、ガント・リスト・統計に「計画との差」を出します。");
  ImGui::PopTextWrapPos();
  ImGui::TextUnformatted("名前（例: キックオフ、v1.0 合意）");
  ImGui::SetNextItemWidth(-1);
  ImGui::InputText("##blname", &u.dName);
  const bool enter = ImGui::IsItemFocused() && enterPressed(u);
  const std::string clean = baselineSanitizeName(u.dName);
  if (text::codepointCount(u.dName) > kBaselineMaxNameChars)
    ImGui::TextDisabled("名前は %zu 文字までです（超えた分は切って保存します）。", kBaselineMaxNameChars);
  ImGui::TextDisabled("保存済み %zu / %zu", have, kBaselineMax);
  ImGui::Spacing();
  ImGui::PushTextWrapPos(0);
  ImGui::PushStyleColor(ImGuiCol_Text, v4(u.t->tight));
  ImGui::TextUnformatted("件名が設定ファイル（project.settings.json）に入ります（担当者名は入りません）。共有・バックアップ用のJSONには入りません。フォルダごと渡す前に、内容を確かめてください。");
  ImGui::PopStyleColor();
  if (full) {
    ImGui::PushStyleColor(ImGuiCol_Text, v4(u.t->late));
    ImGui::Text("ベースラインは %zu つまで保存できます。「ベースラインの管理…」で不要なものを削除してから保存してください。", kBaselineMax);
    ImGui::PopStyleColor();
  }
  ImGui::PopTextWrapPos();
  const bool ok = !full && !clean.empty();
  const int b = buttons(f, {"保存", "キャンセル", "ベースラインの管理…"}, false, ok);
  if (b == 2) {
    close(f);
    openDialog(f, Dlg::BaselineManage);
    return;
  }
  if (b == 1) return close(f);
  if (b == 0 || (enter && ok)) {
    (void)c.saveBaseline(u.dName);
    close(f);
  }
}

// ベースラインの管理（一覧・表示するものの選択・名前の変更・削除）
void dlgBaselineManage(Frame& f) {
  UiState& u = f.u;
  app::AppController& c = f.c;
  Session* s = c.session();
  if (!s) return close(f);
  const BaselineSet& set = s->baselines();
  ImGui::PushTextWrapPos(0);
  ImGui::TextUnformatted("表示するベースラインを選ぶと、ガントに計画のバーと差、リストの列「計画との差」、統計の節、HTML・週次レポートに比較が出ます。");
  ImGui::PopTextWrapPos();
  if (set.list.empty()) {
    ImGui::Spacing();
    ImGui::TextDisabled("ベースラインはまだありません。「…」メニューの「ベースラインを保存…」で、いまの日程を残せます。");
  } else {
    ImGui::Spacing();
    if (ImGui::RadioButton("表示しない", set.shown.empty())) c.showBaseline("");
    ImGui::TextDisabled("保存済み %zu / %zu", set.list.size(), kBaselineMax);
    if (ImGui::BeginTable("##bltable", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
      ImGui::TableSetupColumn("表示", ImGuiTableColumnFlags_WidthFixed, u.m.s(48));
      ImGui::TableSetupColumn("名前", ImGuiTableColumnFlags_WidthStretch);
      ImGui::TableSetupColumn("保存した日時", ImGuiTableColumnFlags_WidthFixed, u.m.s(130));
      ImGui::TableSetupColumn("課題", ImGuiTableColumnFlags_WidthFixed, u.m.s(64));
      ImGui::TableSetupColumn("操作", ImGuiTableColumnFlags_WidthFixed, u.m.s(170));
      ImGui::TableHeadersRow();
      std::string renameDone, renameTo, remove;
      for (const Baseline& b : set.list) {
        ImGui::TableNextRow();
        ImGui::PushID(b.id.c_str());
        ImGui::TableSetColumnIndex(0);
        if (ImGui::RadioButton("##show", set.shown == b.id)) c.showBaseline(b.id);
        ImGui::TableSetColumnIndex(1);
        if (u.blRenameId == b.id) {
          if (u.blRenameFocus) {
            ImGui::SetKeyboardFocusHere();
            u.blRenameFocus = false;
          }
          ImGui::SetNextItemWidth(-1);
          ImGui::InputText("##rn", &u.dText);
          if (ImGui::IsItemFocused() && enterPressed(u)) {
            renameDone = b.id;
            renameTo = u.dText;
          }
        } else {
          if (set.shown == b.id) ImGui::PushFont(u.fontBold);
          ImGui::TextUnformatted(b.name.c_str());
          if (set.shown == b.id) ImGui::PopFont();
        }
        ImGui::TableSetColumnIndex(2);
        ImGui::TextDisabled("%s", b.at.empty() ? "-" : (b.at.substr(0, 10) + " " + b.at.substr(11)).c_str());
        ImGui::TableSetColumnIndex(3);
        ImGui::TextDisabled("%zu 件%s", b.items.size(), b.truncated ? "+" : "");
        ImGui::TableSetColumnIndex(4);
        if (u.blRenameId == b.id) {
          if (ImGui::SmallButton("決定")) {
            renameDone = b.id;
            renameTo = u.dText;
          }
          ImGui::SameLine();
          if (ImGui::SmallButton("取消")) u.blRenameId.clear();
        } else {
          if (ImGui::SmallButton("名前の変更")) {
            u.blRenameId = b.id;
            u.blRenameFocus = true;
            u.dText = b.name;
          }
          ImGui::SameLine();
          if (ImGui::SmallButton("削除")) remove = b.id;
        }
        ImGui::PopID();
      }
      ImGui::EndTable();
      if (!renameDone.empty()) {
        if (c.renameBaseline(renameDone, renameTo)) u.blRenameId.clear();
      }
      if (!remove.empty()) {
        u.blDeleteId = remove;
        close(f);
        openDialog(f, Dlg::ConfirmBaselineDelete);
        return;
      }
    }
  }
  ImGui::Spacing();
  ImGui::PushTextWrapPos(0);
  ImGui::TextDisabled("ベースラインは project.settings.json に入ります（共有・バックアップ用のJSONには入りません）。");
  ImGui::PopTextWrapPos();
  const bool full = set.list.size() >= kBaselineMax;
  const int b = buttons(f, {"閉じる", "いまの日程を保存…"}, false, true);
  if (b == 0) return close(f);
  if (b == 1) {
    if (full) {
      c.toast("ベースラインは " + std::to_string(kBaselineMax) + " つまで保存できます。不要なものを削除してから保存してください");
      return;
    }
    close(f);
    openDialog(f, Dlg::BaselineSave);
  }
}

void dlgConfirmBaselineDelete(Frame& f) {
  UiState& u = f.u;
  app::AppController& c = f.c;
  Session* s = c.session();
  const Baseline* b = s ? baselineFind(s->baselines(), u.blDeleteId) : nullptr;
  if (!b) {
    close(f);
    return;
  }
  ImGui::PushTextWrapPos(0);
  ImGui::Text("ベースライン「%s」を削除します。", b->name.c_str());
  ImGui::TextDisabled("保存した計画の日程が消えます。元に戻せません。");
  ImGui::PopTextWrapPos();
  const int r = buttons(f, {"削除", "キャンセル"}, true);
  if (r < 0) return;
  if (r == 0) (void)c.deleteBaseline(u.blDeleteId);
  u.blDeleteId.clear();
  close(f);
  openDialog(f, Dlg::BaselineManage);
}

// 以前の平置きのプロジェクトがあるときだけ出す。自動では動かさず、ここで押したときに移す
void legacyMigrationBlock(Frame& f) {
  app::AppController& c = f.c;
  const int n = c.legacyCount();
  if (n == 0) return;
  ImGui::Separator();
  ImGui::TextColored(v4(f.u.t->tight), "以前の保存形式（平置き）のプロジェクトが %d 件あります。", n);
  ImGui::PushTextWrapPos(0);
  ImGui::TextDisabled("プロジェクトごとのフォルダ（CSV・設定・添付・バックアップ）に移せます。移す前に、データフォルダの「_バックアップ/移行前…」へ"
                      "全部の控えを作ります。移さなくても、そのまま読み書きできます。");
  ImGui::PopTextWrapPos();
  if (ImGui::Button("フォルダ構成に移す")) (void)c.migrateLegacy();
}

void dlgFolder(Frame& f) {
  UiState& u = f.u;
  app::AppController& c = f.c;
  ImGui::TextUnformatted("データフォルダ");
  ImGui::PushFont(u.fontMono);
  ImGui::TextWrapped("%s", c.dataDir().c_str());
  ImGui::PopFont();
  if (c.dataDirFallback) ImGui::TextColored(v4(u.t->tight), "既定の場所（実行ファイルの横）に書き込めないため、この場所を使っています。");
  switch (c.saveState()) {
    case app::SaveState::Saved: ImGui::TextColored(v4(u.t->ok), "保存済み %s", c.savedAt().c_str()); break;
    case app::SaveState::Saving: ImGui::TextColored(v4(u.t->accent), "保存中…"); break;
    case app::SaveState::Error:
      ImGui::TextColored(v4(u.t->late), "保存できません: %s", c.saveError().c_str());
      if (ImGui::Button("再試行")) c.retrySave();
      break;
  }
  if (ImGui::Button("フォルダを開く")) f.p.openFolder(c.dataDir());
  ImGui::SameLine();
  if (ImGui::Button("フォルダの変更を読み込む")) c.syncFolder(true);
  ImGui::SameLine();
  if (ImGui::Button("保存先を変える…")) {
    close(f);
    openDialog(f, Dlg::AppSettings);
    return;
  }
  ImGui::PushTextWrapPos(0);
  ImGui::TextDisabled("編集のたびに自動で保存します。プロジェクトごとのフォルダに CSV（project.csv）と設定（project.settings.json）を置き、添付は「添付」に入れます。"
                      "上書きする前の CSV は、起動してから最初の1回だけ、そのフォルダの「バックアップ」に残します（30個まで）。"
                      "削除したプロジェクトはフォルダごと「_削除済み」に移します。保存先は「アプリの設定」で変えられます（起動引数 --data・環境変数 ROADMAP_DATA が優先）。");
  ImGui::PopTextWrapPos();
  legacyMigrationBlock(f);
  if (buttons(f, {"閉じる"}) == 0) close(f);
}

// 保存先の由来の説明
const char* dataDirSourceText(const DataDirChoice& d) {
  switch (d.source) {
    case DataDirSource::Arg: return "起動引数 --data で指定されています";
    case DataDirSource::Env: return "環境変数 ROADMAP_DATA で指定されています";
    case DataDirSource::Prefs: return "この設定で選んだ場所です";
    case DataDirSource::Fallback: return "既定の場所（実行ファイルの横）に書き込めないため、%LOCALAPPDATA% を使っています";
    default: return "既定の場所（実行ファイルの横の data\\projects）です";
  }
}

std::string parentDir(const std::string& path) {
  const auto k = path.find_last_of("/\\");
  return k == std::string::npos ? std::string(".") : path.substr(0, k);
}

void dlgAppSettings(Frame& f) {
  UiState& u = f.u;
  app::AppController& c = f.c;
  Prefs& pr = c.prefs();
  if (ImGui::IsWindowAppearing()) {
    u.pendingDir.clear();
    u.pendingError.clear();
    u.dName = pr.me;
  }
  // ---- 保存先 ----
  ImGui::SeparatorText("保存先");
  ImGui::PushFont(u.fontMono);
  ImGui::TextWrapped("%s", c.dataDir().c_str());
  ImGui::PopFont();
  const DataDirChoice& info = c.dataDirInfo();
  ImGui::TextDisabled("%s", dataDirSourceText(info));
  if (info.prefsDirUnavailable)
    ImGui::TextColored(v4(u.t->tight), "設定した保存先（%s）に書き込めないため、ここを使っています。", pr.dataDir.c_str());
  const auto choose = [&](const std::string& dir) {
    u.pendingDir = dir;
    u.pendingError.clear();
    u.pendingCount = c.countProjectsIn(dir);
    u.pendingCopy = u.pendingCount == 0;  // 空の場所なら手元のものをコピー、あるならその場所のものを使う
  };
  if (c.dataDirLocked()) {
    ImGui::TextDisabled("起動のしかたで決まっているため、ここでは変えられません。");
  } else if (u.pendingDir.empty()) {
    if (ImGui::Button("変更…"))
      if (auto d = f.p.pickFolder(c.dataDir())) choose(*d);
    ImGui::SameLine();
    const bool isDefault = info.source == DataDirSource::Default;
    ImGui::BeginDisabled(isDefault);
    if (ImGui::Button("既定（実行ファイルの横）に戻す")) choose(c.defaultDir());
    ImGui::EndDisabled();
  }
  if (ImGui::Button("フォルダを開く")) f.p.openFolder(c.dataDir());
  legacyMigrationBlock(f);
  if (!u.pendingDir.empty()) {
    // 切り替えの確認
    ImGui::PushStyleColor(ImGuiCol_ChildBg, v4(u.t->surface2));
    ImGui::BeginChild("##pending", ImVec2(0, 0), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY);
    ImGui::TextUnformatted("新しい保存先");
    ImGui::PushFont(u.fontMono);
    ImGui::TextWrapped("%s", u.pendingDir.c_str());
    ImGui::PopFont();
    if (u.pendingCount > 0) ImGui::TextDisabled("この場所には %d 件のプロジェクトがあります。", u.pendingCount);
    else ImGui::TextDisabled("この場所にはまだプロジェクトがありません。");
    const std::string label = "今のプロジェクト（" + std::to_string(c.projects().size()) + "件）をこの場所にコピーする";
    ImGui::Checkbox(label.c_str(), &u.pendingCopy);
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("元の場所のファイルはそのまま残ります。同じプロジェクトが新しい場所にもあれば、新しい場所の方を使います。");
    ImGui::PopTextWrapPos();
    if (!u.pendingError.empty()) ImGui::TextColored(v4(u.t->late), "%s", u.pendingError.c_str());
    ImGui::PushStyleColor(ImGuiCol_Button, v4(u.t->accent));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, v4(withAlpha(u.t->accent, 0.85f)));
    ImGui::PushStyleColor(ImGuiCol_Text, v4(u.t->accentInk));
    const bool go = ImGui::Button("切り替える");
    ImGui::PopStyleColor(3);
    if (go) {
      if (auto r = c.switchDataDir(u.pendingDir, u.pendingCopy)) u.pendingDir.clear();
      else u.pendingError = r.error();
    }
    ImGui::SameLine();
    if (ImGui::Button("やめる")) u.pendingDir.clear();
    ImGui::EndChild();
    ImGui::PopStyleColor();
  }

  // ---- 表示 ----
  ImGui::SeparatorText("表示");
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted("テーマ");
  for (const auto& [k, l] : {std::pair<const char*, const char*>{"auto", "自動（Windows に合わせる）"}, {"light", "ライト"}, {"dark", "ダーク"}}) {
    ImGui::SameLine();
    if (ImGui::RadioButton(l, pr.theme == k)) c.setTheme(k);
  }
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted("密度");
  ImGui::SameLine();
  if (ImGui::RadioButton("標準", pr.density != "compact")) c.setDensity("normal");
  ImGui::SameLine();
  if (ImGui::RadioButton("コンパクト", pr.density == "compact")) c.setDensity("compact");
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted("画面の大きさ");
  ImGui::SameLine();
  if (ImGui::Button("-", ImVec2(u.m.s(28), 0))) c.stepUiScale(-1);
  ImGui::SameLine();
  ImGui::AlignTextToFramePadding();
  ImGui::Text("%3d%%", pr.uiScale);
  ImGui::SameLine();
  if (ImGui::Button("+", ImVec2(u.m.s(28), 0))) c.stepUiScale(1);
  ImGui::SameLine();
  ImGui::BeginDisabled(pr.uiScale == 100);
  if (ImGui::Button("100%に戻す")) c.setUiScale(100);
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::TextDisabled("Ctrl+ホイール・Ctrl+「+」「-」・Ctrl+0 でも変えられます");
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted("絞り込みで薄くする課題の濃さ");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(u.m.s(180));
  if (ImGui::SliderInt("##dim", &pr.dimPercent, 10, 80, "%d%%")) pr.dimPercent = std::clamp(pr.dimPercent, 10, 80);
  if (ImGui::IsItemDeactivatedAfterEdit()) c.savePrefs();
  ImGui::SameLine();
  {
    // 見本
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = ImGui::GetFrameHeight();
    dl->AddRectFilled(ImVec2(p.x, p.y + h * 0.25f), ImVec2(p.x + u.m.s(60), p.y + h * 0.75f), u.t->c[1], u.m.s(3));
    dl->AddRectFilled(ImVec2(p.x + u.m.s(68), p.y + h * 0.25f), ImVec2(p.x + u.m.s(128), p.y + h * 0.75f),
                      withAlpha(u.t->c[1], dimAlpha(c)), u.m.s(3));
    ImGui::Dummy(ImVec2(u.m.s(128), h));
  }

  // ---- 操作 ----
  ImGui::SeparatorText("操作");
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted("自分の名前（コメントの書き手）");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(u.m.s(180));
  ImGui::InputText("##me", &u.dName);
  if (ImGui::IsItemDeactivatedAfterEdit()) {
    const std::string me = text::trim(u.dName);
    pr.me = text::replaceAll(me.empty() ? "自分" : me, ";", "；");
    u.dName = pr.me;
    c.savePrefs();
  }
  if (ImGui::Checkbox("プロジェクトを開いたら基準日を今日に合わせる", &pr.autoBaseToday)) c.savePrefs();
  ImGui::SameLine();
  ImGui::TextDisabled("（起動中、プロジェクトごとに1回。前の日付は通知に出します）");
  if (ImGui::Checkbox("GitHub のリンクが閉じられたら自動で完了にする", &pr.autoCompleteGitHub)) c.savePrefs();
  ImGui::SameLine();
  ImGui::TextDisabled("（「GitHub の状態を確認」を押したときだけ。完了日は閉じた日。通知の「元に戻す」で戻せます）");
  if (ImGui::Checkbox("課題を削除する前に確かめる", &pr.confirmDelete)) c.savePrefs();
  ImGui::SameLine();
  ImGui::TextDisabled("（確かめなくても、通知の「元に戻す」か Ctrl+Z で戻せます）");
  {
    // 新しい課題の名前（追加した行の入力欄に最初から入る名前。入力欄は全選択なので、使わないときはそのまま打てば上書きできる）
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("新しい課題の名前");
    using NM = std::pair<const char*, const char*>;
    for (const auto& [k, l] : {NM{"keep", "今のまま"}, NM{"parentSeq", "親の名前＋連番"}, NM{"parentDefault", "親の名前／既定名"},
                               NM{"custom", "自由入力"}}) {
      ImGui::SameLine();
      if (ImGui::RadioButton(l, pr.newNameFormat == k)) {
        pr.newNameFormat = k;
        c.savePrefs();
      }
    }
    if (pr.newNameFormat == "custom") {
      ImGui::SetNextItemWidth(-FLT_MIN);
      ImGui::InputTextWithHint("##newname", "例: {親}の子 {n}", &pr.newNameCustom);
      if (ImGui::IsItemDeactivatedAfterEdit()) {
        pr.newNameCustom = clampNewNameCustom(pr.newNameCustom);
        c.savePrefs();
      }
      ImGui::PushTextWrapPos(0);
      ImGui::TextDisabled("{親} 親の名前　{親ID} 親の表示ID　{n} 連番（兄弟の名前と重ならない）　{日付} 今日の日付");
      ImGui::PopTextWrapPos();
    }
    if (pr.newNameFormat != "keep" && c.session()) {
      // 見本: 今選んでいる課題の下に作ると
      const Session* s = c.session();
      const std::string par = !c.cursor().empty() && s->issue(c.cursor()) ? c.cursor() : std::string();
      const std::string nm = c.newIssueName(par);
      ImGui::TextDisabled("%s", par.empty() ? "見本（最上位に作ると）:" : "見本（選んでいる課題の下に作ると）:");
      ImGui::SameLine();
      ImGui::TextUnformatted(nm.empty() ? kDefaultIssueName : nm.c_str());
    }
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("最上位に作るときは親がないので、連番だけ（新しい課題 n）か既定名になります。名前を空にして確定すると、最初に入っていた名前のままにします");
    ImGui::PopTextWrapPos();
  }
  if (ImGui::Checkbox("課題を追加したとき、親や直前の兄弟の値を引き継ぐ", &pr.inherit.on)) c.savePrefs();
  ImGui::TextDisabled("「子を追加」は親から、「同じ階層に追加」は直前の兄弟から引き継ぎます。追加直後の通知から元に戻せます");
  ImGui::BeginDisabled(!pr.inherit.on);
  ImGui::Indent();
  if (ImGui::Checkbox("担当者", &pr.inherit.assignee)) c.savePrefs();
  ImGui::SameLine();
  if (ImGui::Checkbox("優先度", &pr.inherit.prio)) c.savePrefs();
  ImGui::SameLine();
  if (ImGui::Checkbox("ラベル", &pr.inherit.labels)) c.savePrefs();
  ImGui::SameLine();
  if (ImGui::Checkbox("期限", &pr.inherit.deadline)) c.savePrefs();
  ImGui::SameLine();
  if (ImGui::Checkbox("種別（兄弟から）", &pr.inherit.type)) c.savePrefs();
  ImGui::SameLine();
  if (ImGui::Checkbox("期間（兄弟から）", &pr.inherit.duration)) c.savePrefs();
  ImGui::TextDisabled("種別は、オフでも階層で決まります（Epic の子は Task、それ以外は Sub-task）");
  ImGui::TextWrapped("注意: 担当者を引き継ぐと、同じ人に作業が集まります。同時に進める作業の制限（担当者ごと）や日程が変わるので、追加したあとに担当者を確かめてください");
  ImGui::Unindent();
  ImGui::EndDisabled();

  // ---- その他 ----
  ImGui::SeparatorText("その他");
  ImGui::BeginDisabled(!pr.hintDismissed);
  if (ImGui::Button("操作のヒントをもう一度表示")) {
    pr.hintDismissed = false;
    c.savePrefs();
  }
  ImGui::EndDisabled();
  ImGui::TextDisabled("設定ファイル");
  ImGui::PushFont(u.fontMono);
  ImGui::TextWrapped("%s", c.prefsPath().c_str());
  ImGui::PopFont();
  if (ImGui::Button("設定ファイルのフォルダを開く")) f.p.openFolder(parentDir(c.prefsPath()));
  ImGui::SameLine();
  ImGui::TextDisabled("バージョン %s", ROADMAP_VERSION);
  if (buttons(f, {"閉じる"}) == 0) close(f);
}

void dlgConfirmIssueDelete(Frame& f) {
  UiState& u = f.u;
  app::AppController& c = f.c;
  Session* s = c.session();
  if (!s || u.pendingDelete.empty()) return close(f);
  std::vector<std::string> ids;
  for (const auto& id : u.pendingDelete)
    if (s->issue(id)) ids.push_back(id);
  if (ids.empty()) return close(f);
  std::set<std::string> all(ids.begin(), ids.end());
  for (const auto& id : ids)
    for (const auto& k : s->descendantsOf(id)) all.insert(k);
  const int kids = static_cast<int>(all.size() - ids.size());
  ImGui::PushTextWrapPos(0);
  if (ids.size() == 1) ImGui::Text("「%s」を削除します。", s->issue(ids[0])->summary.c_str());
  else ImGui::Text("%d 件の課題を削除します。", static_cast<int>(ids.size()));
  if (kids > 0) ImGui::TextColored(v4(u.t->tight), "子の課題 %d 件も一緒に削除します。", kids);
  ImGui::TextDisabled("削除したあとも、通知の「元に戻す」か Ctrl+Z で戻せます。");
  ImGui::PopTextWrapPos();
  bool ask = c.prefs().confirmDelete;
  if (ImGui::Checkbox("削除の前に確かめる", &ask)) {
    c.prefs().confirmDelete = ask;
    c.savePrefs();
  }
  const int b = buttons(f, {"削除", "キャンセル"}, true);
  if (b == 0) {
    s->deleteIssues(ids);
    c.afterEdit();
    u.pendingDelete.clear();
    close(f);
  } else if (b == 1) {
    u.pendingDelete.clear();
    close(f);
  }
}

void dlgAttachOpenWarn(Frame& f) {
  UiState& u = f.u;
  if (u.attachWarnPath.empty()) return close(f);
  ImGui::PushTextWrapPos(0);
  const std::string name = u.attachWarnPath.substr(u.attachWarnPath.find_last_of("/\\") == std::string::npos
                                                       ? 0
                                                       : u.attachWarnPath.find_last_of("/\\") + 1);
  ImGui::Text("「%s」を開こうとしています。", name.c_str());
  ImGui::TextColored(v4(u.t->tight), "実行ファイルやスクリプトは、開くとこのパソコンで実行されることがあります。");
  ImGui::TextDisabled("送り主や入手元が確かなときだけ開いてください。");
  ImGui::PopTextWrapPos();
  const int b = buttons(f, {"それでも開く", "やめる"}, true);
  if (b == 0) {
    f.p.openFile(u.attachWarnPath);
    u.attachWarnPath.clear();
    close(f);
  } else if (b == 1) {
    u.attachWarnPath.clear();
    close(f);
  }
}

// 関連リンクが実行ファイルなどを指しているとき、開く前の確認（添付と同じ警告。確認したら開く）。場所はログに出さない
void dlgLinkOpenWarn(Frame& f) {
  UiState& u = f.u;
  if (u.linkWarnTarget.empty()) return close(f);
  ImGui::PushTextWrapPos(0);
  ImGui::Text("「%s」を開こうとしています。", defaultTitle(u.linkWarnTarget).c_str());
  ImGui::TextColored(v4(u.t->tight), "実行ファイルやスクリプトは、開くとこのパソコンで実行されることがあります。");
  ImGui::TextDisabled("送り主や入手元が確かなときだけ開いてください。");
  ImGui::PopTextWrapPos();
  const int b = buttons(f, {"それでも開く", "やめる"}, true);
  if (b == 0) {
    openLinkNow(f, u.linkWarnTarget);
    u.linkWarnTarget.clear();
    close(f);
  } else if (b == 1) {
    u.linkWarnTarget.clear();
    close(f);
  }
}

void dlgAttachPreview(Frame& f) {
  UiState& u = f.u;
  app::AppController& c = f.c;
  const auto& list = c.attachmentsOf(u.previewIssue);
  if (u.previewIndex >= list.size()) return close(f);
  const Attachment& a = list[u.previewIndex];
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  const float maxW = std::min(u.m.s(740), vp->WorkSize.x - u.m.s(80));
  const float maxH = std::max(u.m.s(200), vp->WorkSize.y - u.m.s(280));
  const UiState::ImageTex* img = attachmentImage(f, u.previewIssue, u.previewIndex, kPreviewMaxPx);
  ImGui::TextUnformatted(a.name.c_str());
  if (img && img->tex) {
    const float fit = std::min(1.f, std::min(maxW / static_cast<float>(img->w), maxH / static_cast<float>(img->h)));
    float zoom = u.previewZoom > 0 ? u.previewZoom : fit;
    ImGui::SameLine();
    ImGui::TextDisabled("%d × %d px（%s）／ 表示 %d%%", img->origW, img->origH, formatBytes(a.size).c_str(),
                        static_cast<int>(zoom * 100.f * static_cast<float>(img->w) / static_cast<float>(std::max(1, img->origW)) + 0.5f));
    // 画像の領域（拡大したらスクロール。Ctrl+ホイールで拡大縮小）
    ImGui::BeginChild("##preview", ImVec2(maxW, std::min(maxH, std::max(u.m.s(120), static_cast<float>(img->h) * std::max(zoom, fit) + u.m.s(24)))),
                      ImGuiChildFlags_Border, ImGuiWindowFlags_HorizontalScrollbar);
    if (ImGui::IsWindowHovered() && ImGui::GetIO().KeyCtrl && ImGui::GetIO().MouseWheel != 0) {
      zoom = std::clamp(zoom * (ImGui::GetIO().MouseWheel > 0 ? 1.15f : 1.f / 1.15f), 0.1f, 8.f);
      u.previewZoom = zoom;
    }
    const ImVec2 sz(static_cast<float>(img->w) * zoom, static_cast<float>(img->h) * zoom);
    const float padX = std::max(0.f, (ImGui::GetContentRegionAvail().x - sz.x) / 2);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + padX);
    ImGui::Image(img->tex, sz);
    ImGui::EndChild();
    if (ImGui::Button("全体に合わせる")) u.previewZoom = 0;
    ImGui::SameLine();
    // 表示の倍率は縮小したテクスチャが基準なので、元の画像の100%は「元の幅 ÷ テクスチャの幅」
    if (ImGui::Button("等倍")) u.previewZoom = static_cast<float>(std::max(1, img->origW)) / static_cast<float>(std::max(1, img->w));
    ImGui::SameLine();
    if (ImGui::Button("拡大")) u.previewZoom = std::clamp(zoom * 1.25f, 0.1f, 8.f);
    ImGui::SameLine();
    if (ImGui::Button("縮小")) u.previewZoom = std::clamp(zoom / 1.25f, 0.1f, 8.f);
    ImGui::SameLine();
    ImGui::TextDisabled("Ctrl+ホイールでも拡大縮小");
    if (img->origW > img->w || img->origH > img->h) {
      ImGui::SameLine();
      ImGui::TextDisabled("（大きな画像は縮小して表示しています）");
    }
  } else if (img && img->failed) {
    ImGui::Spacing();
    ImGui::PushTextWrapPos(0);
    ImGui::TextColored(v4(u.t->late), "この画像は表示できません。");
    ImGui::TextDisabled("ファイルが壊れているか、対応していない形式です（PNG・JPEG・GIF・BMP に対応）。「アプリで開く」で、関連付けられたアプリで開けることがあります。");
    ImGui::PopTextWrapPos();
  } else {
    ImGui::Spacing();
    ImGui::TextDisabled("読み込み中…");
  }
  const int b = buttons(f, {"閉じる", "アプリで開く"});
  if (b == 1) {
    const std::string issue = u.previewIssue;
    const std::size_t idx = u.previewIndex;
    close(f);
    openAttachment(f, issue, idx);
  } else if (b == 0) {
    close(f);
  }
}

void dlgSettings(Frame& f) {
  UiState& u = f.u;
  app::AppController& c = f.c;
  Session* s = c.session();
  if (!s) return close(f);
  if (ImGui::IsWindowAppearing()) {
    u.dText = s->settings().holidays;
    u.dText2.clear();
    u.dName = s->settings().baseDate;
  }
  ImGui::SeparatorText("日程の計算");
  ImGui::TextUnformatted("基準日");
  ImGui::SetNextItemWidth(u.m.s(140));
  ImGui::InputText("##base", &u.dName);
  if (ImGui::IsItemDeactivatedAfterEdit())
    if (auto d = csv::parseDate(u.dName)) s->setBaseDate(fmtIso(*d)), c.afterEdit();
  ImGui::SameLine();
  if (ImGui::Button("今日")) {
    s->setBaseDate(c.clock().todayIso());
    u.dName = s->settings().baseDate;
    c.afterEdit();
  }
  ImGui::TextUnformatted("シナリオ");
  if (const Tree* T = s->tree()) {
    for (const auto& sc : T->scenarios) {
      const bool cur = s->settings().scenario == sc;
      if (cur) ImGui::PushStyleColor(ImGuiCol_Button, v4(u.t->accentBg));
      if (ImGui::Button(sc.c_str())) s->setScenario(sc), c.afterEdit();
      if (cur) ImGui::PopStyleColor();
      ImGui::SameLine();
    }
  }
  ImGui::SetNextItemWidth(u.m.s(160));
  ImGui::InputTextWithHint("##newscn", "新しいシナリオ名", &u.dText2);
  ImGui::SameLine();
  if (ImGui::Button("追加") && !text::trim(u.dText2).empty()) {
    s->addScenario(u.dText2);
    u.dText2.clear();
    c.afterEdit();
  }
  {
    ResourceLimit r = s->settings().resource;
    bool on = r.on;
    if (ImGui::Checkbox("担当者ごとの同時作業の制限", &on)) s->setResource({on, r.cap}), c.afterEdit();
    ImGui::SameLine();
    int cap = r.cap;
    ImGui::SetNextItemWidth(u.m.s(100));
    if (ImGui::SliderInt("件まで", &cap, 1, 9)) s->setResource({r.on, cap}), c.afterEdit();
  }
  if (ImGui::CollapsingHeader("休業日（1行に1日。空白・カンマ・セミコロン・読点でも区切れます）")) {
    ImGui::InputTextMultiline("##hol", &u.dText, ImVec2(-1, u.m.s(140)));
    if (ImGui::Button("反映")) s->setHolidays(u.dText), c.afterEdit();
    ImGui::SameLine();
    if (ImGui::Button("既定に戻す")) {
      u.dText = defaultHolidaysText();
      s->setHolidays(u.dText);
      c.afterEdit();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%d日", static_cast<int>(parseHolidays(u.dText).size()));
  }
  ImGui::SeparatorText("表示");
  ImGui::TextUnformatted("ガントの列");
  for (const auto& [k, l] : {std::pair<const char*, const char*>{"asg", "担当"}, {"status", "状況"}, {"dur", "期間"}, {"float", "余裕"}}) {
    bool v = c.uiBool("cols", k, true);
    if (ImGui::Checkbox(l, &v)) c.setUiSub("cols", k, v);
    ImGui::SameLine();
  }
  ImGui::NewLine();
  {
    bool v = c.uiBool("cols", "pred", false);  // 既定はオフ
    if (ImGui::Checkbox("先行の列（リストとガントの表に出す）", &v)) c.setUiSub("cols", "pred", v);
  }
  {
    bool v = c.uiBool("cols", "plan", false);  // 既定はオフ。ベースラインを表示しているときだけ出る
    if (ImGui::Checkbox("計画との差の列（ベースラインを表示しているとき、リストとガントの表に出す）", &v)) c.setUiSub("cols", "plan", v);
  }
  if (ImGui::Button("テーマ・密度・文字の大きさ・保存先（アプリの設定）…")) {
    close(f);
    openDialog(f, Dlg::AppSettings);
    return;
  }
  ImGui::SeparatorText("データ");
  if (ImGui::Button("保存先フォルダ…")) {
    close(f);
    openDialog(f, Dlg::Folder);
    return;
  }
  ImGui::SameLine();
  if (ImGui::Button("全プロジェクトのバックアップ…")) {
    close(f);
    u.exportKind = "backup";
    openDialog(f, Dlg::Export);
    return;
  }
  if (ImGui::CollapsingHeader("CSVの列の説明")) {
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled(
        "Issue Id / Issue key / Issue Type / Summary（必須）/ Parent・Epic Link（親）/ Priority / Status / Assignee / "
        "Start date / Due date / Resolved / Original estimate / Labels / Description / Outward・Inward issue link (Blocks) / "
        "Comment に加えて、Earliest start（着手可能日）、Deadline（目標期限）、Remaining (weeks)、Duration (weeks)、"
        "Duration (weeks) @シナリオ名 を読み書きします。日本語の列名（件名・担当者・期限 など）も使えます。知らない列はそのまま残します。");
    ImGui::PopTextWrapPos();
  }
  if (buttons(f, {"閉じる"}) == 0) close(f);
}

void dlgWorkload(Frame& f) {
  UiState& u = f.u;
  Session* s = f.c.session();
  if (!s || !s->tree()) return close(f);
  const auto wl = workload(*s->tree(), s->viewIssues(), *s->rows(), s->calendar());
  const int cap = s->settings().resource.cap;
  if (wl.empty()) {
    ImGui::TextDisabled("担当者が入った未完了の課題がありません");
  } else if (ImGui::BeginTable("##wl", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
    for (const char* h : {"担当者", "未完了", "合計(週)", "計画上の同時作業の最大", "最後の完了予定", "待たされる作業"}) ImGui::TableSetupColumn(h);
    ImGui::TableHeadersRow();
    for (const auto& w : wl) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(w.name.c_str());
      ImGui::TableNextColumn();
      ImGui::Text("%d", w.n);
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(csv::fmtNum(w.weeks).c_str());
      ImGui::TableNextColumn();
      if (w.peak > cap) ImGui::TextColored(v4(u.t->late), "%d", w.peak);
      else ImGui::Text("%d", w.peak);
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(fmtLong(w.last).c_str());
      ImGui::TableNextColumn();
      ImGui::Text("%d", w.wait);
    }
    ImGui::EndTable();
  }
  ImGui::PushTextWrapPos(0);
  ImGui::TextDisabled("同時作業の最大は、制限なしで先行だけから決まる開始日で数えています（制限値 %d を超えると赤）。"
                      "待たされる作業は、同時作業の制限がオンのとき、同じ担当者の別作業が終わるのを待つ件数です。", cap);
  ImGui::PopTextWrapPos();
  if (buttons(f, {"閉じる"}) == 0) close(f);
}

void dlgWarnings(Frame& f) {
  Session* s = f.c.session();
  const auto w = s ? s->allWarns() : std::vector<std::string>{};
  if (w.empty()) ImGui::TextDisabled("%s", S::kNoWarns);
  ImGui::PushTextWrapPos(0);
  for (const auto& x : w) ImGui::BulletText("%s", x.c_str());
  ImGui::PopTextWrapPos();
  if (buttons(f, {"閉じる"}) == 0) close(f);
}

void dlgShortcuts(Frame& f) {
  const std::pair<const char*, const char*> global[] = {
      {"Ctrl+K", "コマンドパレット"}, {"Ctrl+Z / Ctrl+Shift+Z / Ctrl+Y", "元に戻す / やり直す"}, {"1 2 3 4 5", "ガント / ボード / リスト / ギャップ / 統計"},
      {"/", "絞り込み欄へ"},        {"?", "この一覧"},  {"Ctrl+,", "アプリの設定"},
      {"Ctrl+ホイール / Ctrl+「+」「-」 / Ctrl+0", "画面全体の拡大縮小 / 100%に戻す"},
      {"Ctrl+ホイール（ガントの時間軸の上）", "ガントの時間軸の拡大縮小（マウスの位置の日付を動かさない）"},
      {"Esc", "メニュー → 依存の相手の選択 → 選択 → 詳細パネルの順に閉じる"}};
  const std::pair<const char*, const char*> rows[] = {
      {"↑ ↓", "カーソル移動"},           {"Shift+↑↓", "範囲を広げる"},          {"Alt+↑↓", "兄弟の中で上下に移動"},
      {"← →", "閉じる / 開く"},          {"Enter / F2", "件名を編集"},           {"N / Insert", "同じ階層に追加"},
      {"Shift+N", "子を追加"},           {"Tab / Shift+Tab", "階層を下げる / 上げる"}, {"Delete / Backspace", "選択を削除"},
      {"Space", "詳細パネルを開閉"},
      {"Ctrl+V", "クリップボードの複数行を、選んだ課題の子としてまとめて追加（テキストがなく画像があれば、その画像を添付）"}};
  const std::pair<const char*, const char*> edit[] = {
      {"Enter", "確定（追加直後の行は次の行を追加）"}, {"Ctrl+Enter", "確定して同じ階層に追加"}, {"Shift+Enter", "確定して子を追加"},
      {"Tab / Shift+Tab", "編集したまま階層を変える"}, {"↑ ↓", "隣の行の編集へ"}, {"Esc", "取り消し"}};
  const auto group = [](const char* title, auto& list) {
    ImGui::SeparatorText(title);
    if (ImGui::BeginTable(title, 2)) {
      for (const auto& [k, d] : list) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(k);
        ImGui::TableNextColumn();
        ImGui::TextDisabled("%s", d);
      }
      ImGui::EndTable();
    }
  };
  group("全体", global);
  group("行の操作（入力欄の外）", rows);
  group("件名の編集中", edit);
  if (buttons(f, {"閉じる"}) == 0) close(f);
}

void dlgTplApply(Frame& f) {
  UiState& u = f.u;
  app::AppController& c = f.c;
  Session* s = c.session();
  if (!s || !s->tree()) return close(f);
  const Tree& T = *s->tree();
  const auto tpls = c.templates();
  if (u.dTargets.size() == 1 && T.find(u.dTargets[0]) >= 0) ImGui::Text("追加先: %s", T.name[T.find(u.dTargets[0])].c_str());
  else ImGui::Text("追加先: %d件", static_cast<int>(u.dTargets.size()));
  u.dChoice = std::clamp(u.dChoice, 0, std::max(0, static_cast<int>(tpls.size()) - 1));
  ImGui::BeginChild("##tlist", ImVec2(u.m.s(220), u.m.s(260)), ImGuiChildFlags_Border);
  std::string del;
  for (int k = 0; k < static_cast<int>(tpls.size()); ++k) {
    ImGui::PushID(k);
    if (ImGui::Selectable(tpls[k].name.c_str(), k == u.dChoice)) u.dChoice = k;
    if (!tpls[k].builtin && ImGui::BeginPopupContextItem()) {
      if (ImGui::MenuItem("削除")) del = tpls[k].id;
      ImGui::EndPopup();
    }
    ImGui::PopID();
  }
  ImGui::EndChild();
  ImGui::SameLine();
  ImGui::BeginChild("##tprev", ImVec2(0, u.m.s(260)), ImGuiChildFlags_Border);
  if (!tpls.empty()) {
    const Template& t = tpls[u.dChoice];
    ImGui::TextDisabled("%s", t.desc.c_str());
    if (!t.builtin) {
      ImGui::SameLine();
      if (ImGui::SmallButton("このテンプレートを削除")) del = t.id;
    }
    for (std::size_t k = 0; k < t.items.size(); ++k) {
      const auto& it = t.items[k];
      std::string pre;
      for (int a : it.after) pre += (pre.empty() ? "" : ",") + std::to_string(a + 1);
      ImGui::Text("%s%d. %s", std::string(static_cast<std::size_t>(std::max(0, it.lv)) * 2, ' ').c_str(), static_cast<int>(k + 1), it.n.c_str());
      ImGui::SameLine();
      ImGui::TextDisabled("%s週%s", csv::fmtNum(it.w ? *it.w : 1).c_str(), pre.empty() ? "" : ("・先行 " + pre).c_str());
    }
  }
  ImGui::EndChild();
  if (!del.empty()) c.deleteTemplate(del);
  const int b = buttons(f, {"追加", "キャンセル"});
  if (b == 0 && !tpls.empty()) {
    s->applyTemplate(u.dTargets, tpls[u.dChoice]);
    c.afterEdit();
    close(f);
  } else if (b == 1) {
    close(f);
  }
}

void dlgTplSave(Frame& f) {
  UiState& u = f.u;
  app::AppController& c = f.c;
  Session* s = c.session();
  if (!s || !s->tree() || u.dTargets.empty() || s->find(u.dTargets[0]) < 0) return close(f);
  const auto items = templateItemsFrom(*s->tree(), s->viewIssues(), s->find(u.dTargets[0]), true);
  ImGui::TextUnformatted("名前（必須）");
  ImGui::SetNextItemWidth(-1);
  ImGui::InputText("##tn", &u.dName);
  ImGui::TextUnformatted("説明");
  ImGui::SetNextItemWidth(-1);
  ImGui::InputText("##td", &u.dText);
  ImGui::BeginChild("##tsp", ImVec2(0, u.m.s(200)), ImGuiChildFlags_Border);
  for (std::size_t k = 0; k < items.size(); ++k)
    ImGui::Text("%s%d. %s", std::string(static_cast<std::size_t>(std::max(0, items[k].lv)) * 2, ' ').c_str(),
                static_cast<int>(k + 1), items[k].n.c_str());
  ImGui::EndChild();
  const int b = buttons(f, {"保存", "キャンセル"});
  if (b == 0 && !text::trim(u.dName).empty()) {
    c.saveTemplate(u.dName, u.dText, items);
    close(f);
  } else if (b == 1) {
    close(f);
  }
}

void dlgPasteChildren(Frame& f) {
  UiState& u = f.u;
  app::AppController& c = f.c;
  Session* s = c.session();
  if (!s || !s->tree() || s->find(u.pasteParent) < 0 || u.paste.items.empty()) return close(f);
  const Tree& T = *s->tree();
  const Outline& o = u.paste;
  ImGui::PushTextWrapPos(0);
  ImGui::Text("追加先: %s", T.name[T.find(u.pasteParent)].c_str());
  ImGui::Text("%d件を子課題として追加します（%d段）", static_cast<int>(o.items.size()), o.maxLevel + 1);
  if (!o.warns.empty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, v4(u.t->tight));
    for (const auto& w : o.warns) ImGui::BulletText("%s", w.c_str());
    ImGui::PopStyleColor();
  }
  ImGui::PopTextWrapPos();
  constexpr std::size_t kPreviewRows = 12;
  ImGui::BeginChild("##pastePreview", ImVec2(0, u.m.s(240)), ImGuiChildFlags_Border);
  for (std::size_t k = 0; k < o.items.size() && k < kPreviewRows; ++k)
    ImGui::Text("%s%s", std::string(static_cast<std::size_t>(o.items[k].lv) * 2, ' ').c_str(), o.items[k].name.c_str());
  if (o.items.size() > kPreviewRows) ImGui::TextDisabled("ほか%d件", static_cast<int>(o.items.size() - kPreviewRows));
  ImGui::EndChild();
  ImGui::TextDisabled("追加したあとも、通知の「元に戻す」（Ctrl+Z）で1回で戻せます。");
  const int b = buttons(f, {"追加", "キャンセル"});
  if (b == 0) {
    s->addOutline(u.pasteParent, o.items);
    c.afterEdit();
    close(f);
  } else if (b == 1) {
    close(f);
  }
}

void dlgConfirmQuit(Frame& f) {
  app::AppController& c = f.c;
  ImGui::PushTextWrapPos(0);
  ImGui::TextUnformatted("保存してから終了しますか？");
  if (c.saveState() == app::SaveState::Error) ImGui::TextColored(v4(f.u.t->late), "保存できません: %s", c.saveError().c_str());
  ImGui::PopTextWrapPos();
  const int b = buttons(f, {"保存して終了", "キャンセル", "保存せずに終了"}, false);
  if (b == 0) {
    if (c.flushNow()) f.p.requestQuit();
    close(f);
  } else if (b == 2) {
    f.p.requestQuit();
    close(f);
  } else if (b == 1) {
    close(f);
  }
}

}  // namespace

void drawDialogs(Frame& f) {
  UiState& u = f.u;
  if (u.dlg == Dlg::None) {
    // 状態と食い違って残ったモーダルがあれば閉じる（入力を塞がないように）
    if (ImGui::BeginPopupModal("###dlg")) {
      ImGui::CloseCurrentPopup();
      ImGui::EndPopup();
    }
    return;
  }
  const char* title = titleOf(u.dlg);
  const std::string id = std::string(title) + "###dlg";
  if (u.dlgOpening) {
    ImGui::OpenPopup(id.c_str());
    u.dlgOpening = false;
  }
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x / 2, vp->WorkPos.y + vp->WorkSize.y / 2), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(widthOf(u, u.dlg), 0));
  ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(vp->WorkSize.x - u.m.s(40), vp->WorkSize.y - u.m.s(40)));
  bool open = true;
  if (!ImGui::BeginPopupModal(id.c_str(), &open, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize)) {
    u.dlg = Dlg::None;
    return;
  }
  // Esc と外側クリックで閉じる（確認ダイアログは外側クリックでは閉じない）
  const bool confirm = u.dlg == Dlg::ConfirmDelete || u.dlg == Dlg::ConfirmQuit || u.dlg == Dlg::Conflict ||
                       u.dlg == Dlg::ConfirmIssueDelete || u.dlg == Dlg::AttachOpenWarn || u.dlg == Dlg::LinkOpenWarn || u.dlg == Dlg::ConfirmBaselineDelete || (u.dlg == Dlg::AppSettings && !u.pendingDir.empty());
  const bool outside = ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
                       !ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByPopup |
                                               ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
                       !ImGui::IsAnyItemActive();
  if ((ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !u.imeComposing) || (outside && !confirm)) {
    close(f);
    ImGui::EndPopup();
    return;
  }
  switch (u.dlg) {
    case Dlg::NewProject: dlgNewProject(f); break;
    case Dlg::EditProject: dlgEditProject(f); break;
    case Dlg::ConfirmDelete: dlgConfirmDelete(f); break;
    case Dlg::Import: dlgImport(f); break;
    case Dlg::Export: dlgExport(f); break;
    case Dlg::Folder: dlgFolder(f); break;
    case Dlg::Settings: dlgSettings(f); break;
    case Dlg::Workload: dlgWorkload(f); break;
    case Dlg::Warnings: dlgWarnings(f); break;
    case Dlg::Shortcuts: dlgShortcuts(f); break;
    case Dlg::TplApply: dlgTplApply(f); break;
    case Dlg::TplSave: dlgTplSave(f); break;
    case Dlg::Conflict: dlgConflict(f); break;
    case Dlg::ConfirmQuit: dlgConfirmQuit(f); break;
    case Dlg::AppSettings: dlgAppSettings(f); break;
    case Dlg::ConfirmIssueDelete: dlgConfirmIssueDelete(f); break;
    case Dlg::PasteChildren: dlgPasteChildren(f); break;
    case Dlg::AttachOpenWarn: dlgAttachOpenWarn(f); break;
    case Dlg::LinkOpenWarn: dlgLinkOpenWarn(f); break;
    case Dlg::AttachPreview: dlgAttachPreview(f); break;
    case Dlg::ExportHtml: dlgExportHtml(f); break;
    case Dlg::Weekly: dlgWeekly(f); break;
    case Dlg::BaselineSave: dlgBaselineSave(f); break;
    case Dlg::BaselineManage: dlgBaselineManage(f); break;
    case Dlg::ConfirmBaselineDelete: dlgConfirmBaselineDelete(f); break;
    default: close(f); break;
  }
  if (!open) close(f);
  ImGui::EndPopup();
}

void startPasteChildren(Frame& f) {
  app::AppController& c = f.c;
  UiState& u = f.u;
  Session* s = c.session();
  if (!s) return;
  if (c.selection().size() != 1 || !s->issue(c.selection()[0])) {
    c.toast("子課題を貼り付ける親の課題を1つ選んでください");
    return;
  }
  const auto clip = f.p.clipboardText();
  if (!clip || text::trim(*clip).empty()) {
    c.toast("クリップボードにテキストがありません");
    return;
  }
  Outline o = parseOutline(*clip);
  if (o.items.empty()) {
    c.toast("貼り付けた内容に、課題にできる行がありません");
    return;
  }
  u.pasteParent = c.selection()[0];
  u.paste = std::move(o);
  openDialog(f, Dlg::PasteChildren);
}

void pasteFromClipboard(Frame& f) {
  app::AppController& c = f.c;
  Session* s = c.session();
  if (!s) return;
  if (c.selection().size() == 1 && s->issue(c.selection()[0])) {
    const auto clip = f.p.clipboardText();
    if (!clip || text::trim(*clip).empty()) {  // テキストがあれば、画像より先に子課題の貼り付け（Excel などは両方を載せる）
      if (const auto img = f.p.clipboardImage()) {
        c.attachClipboardImage(c.selection()[0], *img);
        return;
      }
    }
  }
  startPasteChildren(f);
}

}  // namespace roadmap::ui
