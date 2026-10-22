// 週次レポートの HTML レンダラ（外部のファイル・スクリプトを使わない。印刷向けのライト。見た目は報告の書き出しと共有）
#include <string>
#include <vector>

#include "report_internal.hpp"
#include "roadmap/report.hpp"
#include "roadmap/theme_colors.hpp"
#include "weekly_internal.hpp"

namespace roadmap {

namespace {

using namespace weeklyfmt;

std::string E(std::string_view s) { return escapeXml(s); }

const char* const kExtraCss = R"(
.wk h2 small{font-weight:400;color:var(--muted);font-size:12px;margin-left:8px}
.wk h3{margin:12px 0 4px}
.wk ul{margin:4px 0 0;padding-left:1.4em}
.wk li{margin:2px 0}
.wk .sub{color:var(--muted);font-size:12px}
.wk .tiles{grid-template-columns:repeat(4,1fr)}
.wk .tile .d{font-size:11px;color:var(--muted)}
.wk .none{color:var(--muted)}
.wk .notes{white-space:normal;overflow-wrap:anywhere;border:1px solid var(--line);border-radius:6px;padding:8px 12px;min-height:3em}
.wk .pred{color:var(--muted);font-size:12px}
@media screen and (max-width:760px){.wk .tiles{grid-template-columns:repeat(2,1fr)}}
@media print{.part.wsec{break-before:auto;page-break-before:auto;margin-top:16px}}
@page{size:A4 portrait;margin:12mm}
)";

struct Tbl {
  std::string o;
  void head(const std::vector<std::pair<std::string, bool>>& cols) {
    o += "<table>\n<thead><tr>";
    for (const auto& [t, right] : cols) o += std::string("<th") + (right ? " class=\"n\"" : "") + ">" + E(t) + "</th>";
    o += "</tr></thead>\n<tbody>\n";
  }
  // 1 行。各セルは {文字列, 右寄せ, CSS クラス}。文字列はここでエスケープする
  struct Cell {
    std::string text;
    bool right = false;
    std::string cls;
  };
  void row(const std::vector<Cell>& cells) {
    o += "<tr>";
    for (const Cell& c : cells) {
      std::string cl = c.cls;
      if (c.right) cl += cl.empty() ? "n" : " n";
      o += "<td" + (cl.empty() ? std::string() : " class=\"" + cl + "\"") + ">" + E(c.text) + "</td>";
    }
    o += "</tr>\n";
  }
  std::string close() { return o + "</tbody>\n</table>\n"; }
};

std::string tile(const std::string& label, const std::string& value, const std::string& delta = std::string(), const char* cls = "") {
  return "<div class=\"tile\"><div class=\"l\">" + E(label) + "</div><div class=\"v" + (*cls ? std::string(" ") + cls : std::string()) +
         "\">" + E(value) + "</div>" + (delta.empty() ? std::string() : "<div class=\"d\">" + E(delta) + "</div>") + "</div>\n";
}

std::string section(const std::string& h2, const std::string& body, const std::string& small = std::string()) {
  return "<section class=\"part wsec\">\n<h2>" + E(h2) + (small.empty() ? std::string() : "<small>" + E(small) + "</small>") + "</h2>\n" + body +
         "</section>\n";
}

std::string moreP(int omitted) { return omitted > 0 ? "<p class=\"sub\">" + E(moreText(omitted)) + "</p>\n" : std::string(); }

std::string noneP() { return "<p class=\"none\">なし</p>\n"; }

std::string summaryHtml(const WeeklyReport& r) {
  const WeeklySummary& s = r.summary;
  std::string b = "<div class=\"tiles\">\n";
  b += tile("進み", percent(s.progress), s.hasPrev ? "先週から " + deltaPt(s.progressDelta) : std::string());
  b += tile("完了 / 全体", std::to_string(s.done) + " / " + std::to_string(s.tasks),
            "進行中 " + std::to_string(s.prog) + "・未着手 " + std::to_string(s.todo));
  b += tile("遅れ", std::to_string(s.late) + " 件", s.hasPrev ? "先週から " + deltaInt(s.lateDelta) : std::string(), s.late > 0 ? "rk-late" : "");
  b += tile("余裕小", std::to_string(s.tight) + " 件", s.hasPrev ? "先週から " + deltaInt(s.tightDelta) : std::string(),
            s.tight > 0 ? "rk-tight" : "");
  b += tile("完了の見込み日", s.hasFinish ? fmtLong(s.finish) : std::string("-"),
            s.hasFinishShift ? "先週から " + shiftText(s.finishShiftDays) : std::string());
  b += tile(doneTitle(r), std::to_string(s.doneInPeriod) + " 件");
  if (s.milestones > 0)
    b += tile("マイルストーン", std::to_string(s.msDone) + " / " + std::to_string(s.milestones), "遅れ " + std::to_string(s.msLate));
  b += "</div>\n";
  if (s.doneNoDate > 0)
    b += "<p class=\"warn\">完了日のない完了済みの作業が " + std::to_string(s.doneNoDate) + " 件あります（期間の集計には入りません）。</p>\n";
  if (s.hasPrev) b += "<p class=\"sub\">先週との比較: " + E(fmtLong(s.prevBase)) + " に書き出したときの状態と比べています。</p>\n";
  return section("サマリー", b);
}

std::string doneHtml(const WeeklyReport& r) {
  std::string b;
  if (r.done.items.empty()) {
    b += noneP();
  } else {
    Tbl t;
    t.head({{"課題", false}, {"担当", false}, {"完了日", false}});
    for (const WeeklyItem& it : r.done.items)
      t.row({{title(it.did, it.name), false, "nm"}, {who(it.assignee), false, ""}, {it.resolved ? fmtMd(*it.resolved) : std::string("-"), false, ""}});
    b += t.close();
  }
  b += moreP(r.done.omitted());
  return section(doneTitle(r), b, std::to_string(r.done.total) + " 件");
}

std::string progressHtml(const WeeklyReport& r) {
  std::string b;
  if (r.progress.items.empty()) {
    b += noneP();
  } else {
    Tbl t;
    t.head({{"課題", false}, {"担当", false}, {"残り", false}, {"余裕", false}});
    for (const WeeklyItem& it : r.progress.items)
      t.row({{title(it.did, it.name), false, "nm"},
             {who(it.assignee), false, ""},
             {remainText(it, r.base), false, ""},
             {floatPhrase(it.floatDays), false, it.floatDays < 0 ? "rk-late" : it.floatDays <= kWeeklyTightDays ? "rk-tight" : ""}});
    b += t.close();
  }
  b += moreP(r.progress.omitted());
  return section("進行中", b, std::to_string(r.progress.total) + " 件");
}

std::string nextHtml(const WeeklyReport& r) {
  std::string b = "<p class=\"sub\">" + E(rangeText(r.next)) + "（" + E(workdaysText(r.nextWorkdays)) + "）</p>\n";
  b += "<h3>開始（" + std::to_string(r.nextStart.total) + " 件）</h3>\n";
  if (r.nextStart.items.empty()) {
    b += noneP();
  } else {
    Tbl t;
    t.head({{"課題", false}, {"担当", false}, {"開始", false}});
    for (const WeeklyItem& it : r.nextStart.items) t.row({{title(it.did, it.name), false, "nm"}, {who(it.assignee), false, ""}, {fmtMd(it.start), false, ""}});
    b += t.close();
  }
  b += moreP(r.nextStart.omitted());
  b += "<h3>完了（" + std::to_string(r.nextFinish.total) + " 件）</h3>\n";
  if (r.nextFinish.items.empty()) {
    b += noneP();
  } else {
    Tbl t;
    t.head({{"課題", false}, {"担当", false}, {"完了見込み", false}});
    for (const WeeklyItem& it : r.nextFinish.items) t.row({{title(it.did, it.name), false, "nm"}, {who(it.assignee), false, ""}, {fmtMd(it.finish), false, ""}});
    b += t.close();
  }
  b += moreP(r.nextFinish.omitted());
  return section(nextTitle(r), b);
}

std::string risksHtml(const WeeklyReport& r) {
  std::string b = "<p class=\"sub\">余裕が負、または " + std::to_string(kWeeklyTightDays) + " 営業日以内の未完了の作業です。</p>\n";
  if (r.risks.items.empty()) {
    b += noneP();
  } else {
    Tbl t;
    t.head({{"課題", false}, {"担当", false}, {"余裕", false}, {"完了見込み", false}, {"先行", false}});
    for (const WeeklyItem& it : r.risks.items) {
      std::string pred = "-";
      if (it.predIndex >= 0)
        pred = title(it.predDid, it.predName) + "（完了見込み " + fmtMd(it.predFinish) + "・" + floatPhrase(it.predFloat) + "）";
      t.row({{(it.floatDays < 0 ? "! " : "") + title(it.did, it.name), false, "nm"},
             {who(it.assignee), false, ""},
             {floatPhrase(it.floatDays), false, it.floatDays < 0 ? "rk-late" : "rk-tight"},
             {fmtMd(it.finish), false, ""},
             {pred, false, "nm pred"}});
    }
    b += t.close();
  }
  b += moreP(r.risks.omitted());
  return section("遅れ・余裕小", b, std::to_string(r.risks.total) + " 件");
}

std::string milestonesHtml(const WeeklyReport& r) {
  std::string b;
  if (r.milestones.empty()) {
    b += noneP();
  } else {
    Tbl t;
    t.head({{"マイルストーン", false}, {"状況", false}, {"期限", false}, {"達成・見込み", false}, {"期限との差", false}});
    for (const WeeklyMilestone& m : r.milestones) {
      const bool late = !m.achieved && m.deadline && m.diffDays < 0;
      t.row({{title(m.did, m.name), false, "nm"},
             {m.achieved ? "達成" : late ? "! 遅れ" : "未達", false, m.achieved ? "rk-ok" : late ? "rk-late" : ""},
             {m.deadline ? fmtMd(*m.deadline) : std::string("-"), false, ""},
             {fmtMd(m.forecast), false, ""},
             {diffPhrase(m), false, late ? "rk-late" : ""}});
    }
    b += t.close();
    if (r.milestonesTotal > static_cast<int>(r.milestones.size()))
      b += moreP(r.milestonesTotal - static_cast<int>(r.milestones.size()));
  }
  return section("マイルストーン", b);
}

std::string assigneesHtml(const WeeklyReport& r) {
  std::string b;
  if (r.assignees.empty()) {
    b += noneP();
  } else {
    Tbl t;
    t.head({{"担当", false}, {doneTitle(r), true}, {"進行中", true}, {"未完了", true}, {"うち遅れ", true}, {loadTitle(r), true}});
    for (const WeeklyAssigneeRow& a : r.assignees)
      t.row({{who(a.name), false, ""},
             {std::to_string(a.doneInPeriod), true, ""},
             {std::to_string(a.inProgress), true, ""},
             {std::to_string(a.open), true, ""},
             {std::to_string(a.late), true, a.late > 0 ? "rk-late" : ""},
             {loadText(a.loadNextPercent), true, ""}});
    b += t.close();
    if (r.assigneesTotal > static_cast<int>(r.assignees.size()))
      b += "<p class=\"sub\">ほか " + std::to_string(r.assigneesTotal - static_cast<int>(r.assignees.size())) + " 人</p>\n";
  }
  b += "<p class=\"sub\">名前順です（順位ではありません）。負荷は、その週の営業日数に対する作業量の割合です。</p>\n";
  return section("担当別", b);
}

std::string changesHtml(const WeeklyReport& r) {
  const WeeklyChanges& c = r.changes;
  std::string b = "<p class=\"sub\">" + E(fmtLong(c.since)) + " に書き出したときの状態との比較です。";
  if (c.truncated) b += "（比べた相手は課題が多かったため、一部の課題だけです）";
  b += "</p>\n";
  b += "<h3>完了の見込み日が動いた課題（" + std::to_string(c.finishMovedTotal) + " 件）</h3>\n";
  if (c.finishMoved.empty()) {
    b += noneP();
  } else {
    Tbl t;
    t.head({{"課題", false}, {"担当", false}, {"先週の見込み", false}, {"今の見込み", false}, {"変化", false}});
    for (const WeeklyFinishMove& mv : c.finishMoved)
      t.row({{title(mv.did, mv.name), false, "nm"},
             {who(mv.assignee), false, ""},
             {fmtMd(mv.from), false, ""},
             {fmtMd(mv.to), false, ""},
             {shiftText(mv.shiftDays), false, mv.shiftDays > 0 ? "rk-late" : "rk-ok"}});
    b += t.close();
    if (c.finishMovedTotal > static_cast<int>(c.finishMoved.size()))
      b += moreP(c.finishMovedTotal - static_cast<int>(c.finishMoved.size()));
  }
  b += "<h3>新たに遅れた課題（" + std::to_string(c.newlyLate.total) + " 件）</h3>\n";
  if (c.newlyLate.items.empty()) {
    b += noneP();
  } else {
    Tbl t;
    t.head({{"課題", false}, {"担当", false}, {"余裕", false}});
    for (const WeeklyItem& it : c.newlyLate.items) t.row({{title(it.did, it.name), false, "nm"}, {who(it.assignee), false, ""}, {floatPhrase(it.floatDays), false, "rk-late"}});
    b += t.close();
  }
  b += moreP(c.newlyLate.omitted());
  b += "<h3>新しく完了した課題（" + std::to_string(c.newlyDone.total) + " 件）</h3>\n";
  if (c.newlyDone.items.empty()) {
    b += noneP();
  } else {
    Tbl t;
    t.head({{"課題", false}, {"担当", false}, {"完了日", false}});
    for (const WeeklyItem& it : c.newlyDone.items)
      t.row({{title(it.did, it.name), false, "nm"}, {who(it.assignee), false, ""}, {it.resolved ? fmtMd(*it.resolved) : std::string("-"), false, ""}});
    b += t.close();
  }
  b += moreP(c.newlyDone.omitted());
  b += "<p class=\"sub\">増えた作業: " + std::to_string(c.added) + " 件 ／ 無くなった作業: " + std::to_string(c.removed) + " 件</p>\n";
  return section("先週から変わったこと", b);
}

std::string baselineHtml(const WeeklyReport& r) {
  const WeeklyBaseline& bl = r.baseline;
  const std::string at = baselineAtText(bl.at);
  std::string b = "<p class=\"sub\">ベースライン「" + E(bl.name) + "」" + (at.empty() ? std::string() : "（保存 " + E(at) + "）") +
                  "との比較です。営業日の差で、+ は計画より遅れ、- は早まりです。";
  if (bl.truncated) b += "（ベースラインは課題が多かったため、一部の課題だけです）";
  b += "</p>\n";
  b += "<div class=\"tiles\">\n";
  b += tile("遅れ", std::to_string(bl.sum.late) + " 件", std::string(), bl.sum.late > 0 ? "rk-late" : "");
  b += tile("早まり", std::to_string(bl.sum.early) + " 件");
  b += tile("計画どおり", std::to_string(bl.sum.onPlan) + " 件");
  b += tile("計画になかった", std::to_string(bl.sum.added) + " 件");
  b += tile("消えた", std::to_string(bl.sum.removed) + " 件");
  b += "</div>\n";
  const std::string note = baselineSameDayNote(bl.sum);
  if (!note.empty()) b += "<p class=\"sub\">" + E(note) + "</p>\n";
  const auto moves = [&](const std::string& head, const WeeklyBaselineList& l, bool late) {
    b += "<h3>" + E(head) + "（" + std::to_string(l.total) + " 件）</h3>\n";
    if (l.items.empty()) {
      b += noneP();
    } else {
      Tbl t;
      t.head({{"課題", false}, {"担当", false}, {"計画の完了", false}, {"今の見込み", false}, {"差", false}});
      for (const WeeklyBaselineRow& it : l.items) {
        const std::string d = it.diffDays > 0 ? "! +" + std::to_string(it.diffDays) + "日" : it.diffDays < 0 ? "-" + std::to_string(-it.diffDays) + "日" : "±0日";
        t.row({{title(it.did, it.name), false, "nm"},
               {who(it.assignee), false, ""},
               {it.planFinish ? fmtMd(*it.planFinish) : std::string("-"), false, ""},
               {fmtMd(it.nowFinish), false, ""},
               {d, false, late ? "rk-late" : "rk-ok"}});
      }
      b += t.close();
    }
    b += moreP(l.omitted());
  };
  moves("遅れた課題", bl.late, true);
  moves("早まった課題", bl.early, false);
  b += "<h3>計画になかった課題（" + std::to_string(bl.added.total) + " 件）</h3>\n";
  if (bl.added.items.empty()) {
    b += noneP();
  } else {
    Tbl t;
    t.head({{"課題", false}, {"担当", false}, {"完了見込み", false}});
    for (const WeeklyBaselineRow& it : bl.added.items) t.row({{title(it.did, it.name), false, "nm"}, {who(it.assignee), false, ""}, {fmtMd(it.nowFinish), false, ""}});
    b += t.close();
  }
  b += moreP(bl.added.omitted());
  b += "<h3>消えた課題（" + std::to_string(bl.removed.total) + " 件）</h3>\n";
  if (bl.removed.items.empty()) {
    b += noneP();
  } else {
    Tbl t;
    t.head({{"課題", false}, {"Issue Id", false}, {"計画の完了", false}});
    for (const WeeklyBaselineRow& it : bl.removed.items)
      t.row({{it.name.empty() ? it.id : it.name, false, "nm"}, {it.id, false, ""}, {it.planFinish ? fmtMd(*it.planFinish) : std::string("-"), false, ""}});
    b += t.close();
  }
  b += moreP(bl.removed.omitted());
  return section("ベースラインとの差", b);
}

std::string notesHtml(const WeeklyReport& r) {
  std::string b = "<div class=\"notes\">";
  if (r.notes.empty()) {
    b += "<span class=\"none\">（未記入）</span>";
  } else {
    std::size_t from = 0;
    bool first = true;
    for (;;) {
      const std::size_t p = r.notes.find('\n', from);
      const std::string ln = r.notes.substr(from, p == std::string::npos ? std::string::npos : p - from);
      if (!first) b += "<br>\n";
      b += E(ln);
      first = false;
      if (p == std::string::npos) break;
      from = p + 1;
    }
  }
  b += "</div>\n";
  return section("所感・課題・お願い", b);
}

}  // namespace

std::string weeklyHtml(const WeeklyReport& r, const IClock* clock) {
  const ThemeColors& k = lightColors();
  const std::string name = r.projectName.empty() ? std::string("週次レポート") : r.projectName;
  std::string h;
  h.reserve(1 << 14);
  h += "<!doctype html>\n<html lang=\"ja\">\n<head>\n<meta charset=\"utf-8\">\n";
  h += "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">\n";
  h += "<title>" + E(name) + " - 週次レポート</title>\n<style>\n" + report::pageCss(k) + kExtraCss + "</style>\n</head>\n<body>\n<div class=\"page wk\">\n";
  h += "<header>\n<h1>" + E(name) + " 週次レポート</h1>\n<p class=\"meta\">期間 <b>" + E(rangeText(r.range)) + "</b>（" +
       E(workdaysText(r.rangeWorkdays)) + "）　／　基準日 <b>" + E(fmtLong(r.base)) + "</b>";
  if (clock) h += "　／　出力 <b>" + E(report::dateTimeText(*clock)) + "</b>";
  h += "</p>\n";
  if (r.rangeCapped) h += "<p class=\"warn\">範囲が長すぎたため、先頭の " + std::to_string(kWeeklyMaxRangeDays) + " 日までにしました。</p>\n";
  h += "</header>\n<main>\n";
  if (r.empty) {
    h += "<p class=\"empty\">課題がありません。</p>\n";
    if (r.sections.notes) h += notesHtml(r);  // 課題がなくても、所感は書ける
  } else {
    const WeeklySections& s = r.sections;
    if (!s.any()) h += "<p class=\"empty\">出す項目が選ばれていません。</p>\n";
    if (s.summary) h += summaryHtml(r);
    if (s.done) h += doneHtml(r);
    if (s.progress) h += progressHtml(r);
    if (s.next) h += nextHtml(r);
    if (s.risks) h += risksHtml(r);
    if (s.milestones) h += milestonesHtml(r);
    if (s.assignees) h += assigneesHtml(r);
    if (s.changes && r.changes.available) h += changesHtml(r);
    if (s.baseline && r.baseline.available) h += baselineHtml(r);
    if (s.notes) h += notesHtml(r);
  }
  h += "</main>\n<footer>このアプリで出力（プロジェクト管理）</footer>\n</div>\n</body>\n</html>\n";
  return h;
}

}  // namespace roadmap
