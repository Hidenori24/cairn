// 週次レポートの Markdown レンダラ
#include <algorithm>
#include <functional>

#include "roadmap/report.hpp"
#include "roadmap/text.hpp"
#include "weekly_internal.hpp"

namespace roadmap {

namespace {

// 1 文字目が行頭だと Markdown の構文になる文字（見出し・箇条書き・引用）
bool isBlockLead(char c) { return c == '#' || c == '-' || c == '+' || c == '>'; }

// 文字数で切る（コードポイント単位）
std::string clipChars(const std::string& s, std::size_t maxChars) {
  std::size_t count = 0;
  for (std::size_t i = 0; i < s.size();) {
    if (count == maxChars) return s.substr(0, i) + "…";
    text::decodeAt(s, i);
    ++count;
  }
  return s;
}

}  // namespace

std::string escapeMarkdown(std::string_view in) {
  std::string s = text::trim(cleanText(in));
  s = clipChars(s, kWeeklyMaxNameChars);
  // 行頭が「数字 + . か )」なら、その記号を打ち消す（番号つきリスト）
  std::size_t digitsEnd = 0;
  while (digitsEnd < s.size() && s[digitsEnd] >= '0' && s[digitsEnd] <= '9') ++digitsEnd;
  // （"1.2.3 件名" のように記号のあとが空白でなければ番号つきリストではない）
  const bool numbered = digitsEnd > 0 && digitsEnd < s.size() && (s[digitsEnd] == '.' || s[digitsEnd] == ')') &&
                        (digitsEnd + 1 == s.size() || s[digitsEnd + 1] == ' ');
  std::string out;
  out.reserve(s.size() + s.size() / 4 + 4);
  for (std::size_t i = 0; i < s.size(); ++i) {
    const char c = s[i];
    bool esc = false;
    switch (c) {
      case '\\':
      case '`':
      case '*':
      case '_':
      case '~':
      case '[':
      case ']':
      case '<':
      case '>':
      case '&':
      case '|': esc = true; break;
      default: break;
    }
    if (!esc && isBlockLead(c) && (i == 0 || (c == '#' && i + 1 == s.size()))) esc = true;
    if (!esc && numbered && i == digitsEnd) esc = true;
    if (esc) out += '\\';
    out += c;
  }
  return out;
}

namespace {

using namespace weeklyfmt;

// 所感: Markdown のまま入れる。"<"（生の HTML・自動リンク）だけ打ち消す。制御文字は weeklySanitizeNotes で除去済み
std::string notesText(const std::string& notes) {
  std::string out;
  std::size_t run = 0;  // 直前のバックスラッシュの連続の長さ（入力のまま）
  for (char c : weeklySanitizeNotes(notes)) {
    // 奇数個の直前のバックスラッシュは、この "<" をすでに打ち消している。偶数個（0 を含む）のときだけ足す
    if (c == '<' && run % 2 == 0) out += '\\';
    out += c;
    run = c == '\\' ? run + 1 : 0;
  }
  return out;
}

struct Md {
  std::string o;
  void line(const std::string& s = std::string()) { o += s + "\n"; }
  void heading(int level, const std::string& escapedText) {
    if (!o.empty()) blank();
    o += std::string(static_cast<std::size_t>(level), '#') + " " + escapedText + "\n\n";
  }
  void blank() {
    if (o.empty()) return;
    if (o.size() >= 2 && o[o.size() - 1] == '\n' && o[o.size() - 2] == '\n') return;
    o += "\n";
  }
};

std::string who2(const std::string& name) { return escapeMarkdown(who(name)); }

void listMore(Md& m, const WeeklyList& l) {
  if (l.omitted() > 0) m.line("- " + moreText(l.omitted()));
}

void renderSummary(Md& m, const WeeklyReport& r) {
  const WeeklySummary& s = r.summary;
  m.heading(2, "サマリー");
  std::string pg = "- 進み: " + percent(s.progress);
  if (s.hasPrev) pg += "（先週から " + deltaPt(s.progressDelta) + "）";
  m.line(pg);
  m.line("- 作業: 全体 " + std::to_string(s.tasks) + " 件（完了 " + std::to_string(s.done) + "・進行中 " + std::to_string(s.prog) +
         "・未着手 " + std::to_string(s.todo) + "）");
  std::string late = "- 遅れ: " + std::to_string(s.late) + " 件";
  if (s.hasPrev) late += "（先週から " + deltaInt(s.lateDelta) + "）";
  m.line(late);
  std::string tight = "- 余裕小（" + std::to_string(kWeeklyTightDays) + " 営業日以内）: " + std::to_string(s.tight) + " 件";
  if (s.hasPrev) tight += "（先週から " + deltaInt(s.tightDelta) + "）";
  m.line(tight);
  if (s.hasFinish) {
    std::string f = "- 完了の見込み日: " + fmtLong(s.finish);
    if (s.hasFinishShift) f += "（先週から " + shiftText(s.finishShiftDays) + "）";
    m.line(f);
  } else {
    m.line("- 完了の見込み日: 未完了の作業はありません");
  }
  m.line("- " + doneTitle(r) + ": " + std::to_string(s.doneInPeriod) + " 件");
  if (s.milestones > 0)
    m.line("- マイルストーン: 達成 " + std::to_string(s.msDone) + " / 全体 " + std::to_string(s.milestones) + "（遅れ " +
           std::to_string(s.msLate) + "）");
  if (s.doneNoDate > 0)
    m.line("- 注意: 完了日のない完了済みの作業が " + std::to_string(s.doneNoDate) + " 件あります（期間の集計には入りません）");
  if (s.hasPrev) m.line("- 先週との比較: " + snapshotBasisText(s.prevAuto, s.prevBase) + "と比べています");
}

void renderDone(Md& m, const WeeklyReport& r) {
  m.heading(2, doneTitle(r) + "（" + std::to_string(r.done.total) + " 件）");
  if (r.done.items.empty()) m.line("- なし");
  for (const WeeklyItem& it : r.done.items) {
    std::string s = "- " + escapeMarkdown(title(it.did, it.name)) + "（担当: " + who2(it.assignee);
    if (it.resolved) s += "・完了 " + fmtMd(*it.resolved);
    m.line(s + "）");
  }
  listMore(m, r.done);
}

void renderTouched(Md& m, const WeeklyReport& r) {
  const WeeklyTouched& t = r.touched;
  if (!t.hasColumn) {
    m.heading(2, touchedTitle(r));
    m.line(touchedNoColumnText());
    return;
  }
  m.heading(2, touchedTitle(r) + "（" + std::to_string(t.total) + " 件）");
  m.line("期間内に更新日（Updated）か作成日（Created）が入る、完了していない作業です。完了した作業は「" + doneTitle(r) + "」に出ます。");
  m.blank();
  if (t.groups.empty()) m.line("- なし");
  for (const WeeklyTouchedGroup& g : t.groups) {
    m.heading(3, g.path.empty() ? touchedNoParentText() : escapeMarkdown(g.path));
    for (const WeeklyTouchedItem& it : g.items)
      m.line("- " + escapeMarkdown(title(it.item.did, it.item.name)) + "（" + statusName(it.item) + "・担当: " + who2(it.item.assignee) + "・" +
             touchedOnText(it) + "・" + touchedChangeText(it) + "）");
  }
  if (t.omitted() > 0) m.line("- " + moreText(t.omitted()));
  if (t.total > 0) {
    m.blank();
    m.line(touchedBasisText(t));
  }
  if (t.noStamp > 0) {
    m.blank();
    m.line(touchedNoStampText(t.noStamp));
  }
}

void renderProgress(Md& m, const WeeklyReport& r) {
  m.heading(2, "進行中（" + std::to_string(r.progress.total) + " 件）");
  if (r.progress.items.empty()) m.line("- なし");
  for (const WeeklyItem& it : r.progress.items)
    m.line("- " + escapeMarkdown(title(it.did, it.name)) + "（担当: " + who2(it.assignee) + "・" + remainPhrase(it, r.base) + "・" +
           floatPhrase(it.floatDays) + "）");
  listMore(m, r.progress);
}

void renderNext(Md& m, const WeeklyReport& r) {
  m.heading(2, nextTitle(r));
  m.line(rangeText(r.next) + "（" + workdaysText(r.nextWorkdays) + "）");
  m.blank();
  m.heading(3, "開始（" + std::to_string(r.nextStart.total) + " 件）");
  if (r.nextStart.items.empty()) m.line("- なし");
  for (const WeeklyItem& it : r.nextStart.items)
    m.line("- " + escapeMarkdown(title(it.did, it.name)) + "（担当: " + who2(it.assignee) + "・開始 " + fmtMd(it.start) + "）");
  listMore(m, r.nextStart);
  m.heading(3, "完了（" + std::to_string(r.nextFinish.total) + " 件）");
  if (r.nextFinish.items.empty()) m.line("- なし");
  for (const WeeklyItem& it : r.nextFinish.items)
    m.line("- " + escapeMarkdown(title(it.did, it.name)) + "（担当: " + who2(it.assignee) + "・完了見込み " + fmtMd(it.finish) + "）");
  listMore(m, r.nextFinish);
}

void renderRisks(Md& m, const WeeklyReport& r) {
  m.heading(2, "遅れ・余裕小（" + std::to_string(r.risks.total) + " 件）");
  m.line("余裕が負、または " + std::to_string(kWeeklyTightDays) + " 営業日以内の未完了の作業です。");
  m.blank();
  if (r.risks.items.empty()) m.line("- なし");
  for (const WeeklyItem& it : r.risks.items) {
    m.line("- " + std::string(it.floatDays < 0 ? "**遅れ** " : "") + escapeMarkdown(title(it.did, it.name)) + "（担当: " +
           who2(it.assignee) + "・" + floatPhrase(it.floatDays) + "・完了見込み " + fmtMd(it.finish) + "）");
    if (it.predIndex >= 0)
      m.line("  - 先行: " + escapeMarkdown(title(it.predDid, it.predName)) + "（完了見込み " + fmtMd(it.predFinish) + "・" +
             floatPhrase(it.predFloat) + "）");
  }
  listMore(m, r.risks);
}

void renderMilestones(Md& m, const WeeklyReport& r) {
  m.heading(2, "マイルストーン");
  if (r.milestones.empty()) {
    m.line("- なし");
    return;
  }
  m.line("| マイルストーン | 状況 | 期限 | 達成・見込み | 期限との差 |");
  m.line("| --- | --- | --- | --- | --- |");
  for (const WeeklyMilestone& ms : r.milestones) {
    const char* st = ms.achieved ? "達成" : (ms.deadline && ms.diffDays < 0 ? "遅れ" : "未達");
    m.line("| " + escapeMarkdown(title(ms.did, ms.name)) + " | " + st + " | " + (ms.deadline ? fmtMd(*ms.deadline) : std::string("-")) +
           " | " + fmtMd(ms.forecast) + " | " + diffPhrase(ms) + " |");
  }
  if (r.milestonesTotal > static_cast<int>(r.milestones.size()))
    m.line("\nほか " + std::to_string(r.milestonesTotal - static_cast<int>(r.milestones.size())) + " 件");
}

void renderAssignees(Md& m, const WeeklyReport& r) {
  m.heading(2, "担当別");
  if (r.assignees.empty()) {
    m.line("- なし");
    return;
  }
  m.line("| 担当 | " + doneTitle(r) + " | 進行中 | 未完了 | うち遅れ | " + loadTitle(r) + " |");
  m.line("| --- | ---: | ---: | ---: | ---: | ---: |");
  for (const WeeklyAssigneeRow& a : r.assignees)
    m.line("| " + who2(a.name) + " | " + std::to_string(a.doneInPeriod) + " | " + std::to_string(a.inProgress) + " | " +
           std::to_string(a.open) + " | " + std::to_string(a.late) + " | " + loadText(a.loadNextPercent) + " |");
  if (r.assigneesTotal > static_cast<int>(r.assignees.size()))
    m.line("\nほか " + std::to_string(r.assigneesTotal - static_cast<int>(r.assignees.size())) + " 人");
  m.line("\n名前順です（順位ではありません）。負荷は、その週の営業日数に対する作業量の割合です。");
}

void renderChanges(Md& m, const WeeklyReport& r) {
  const WeeklyChanges& c = r.changes;
  m.heading(2, "先週から変わったこと");
  m.line(changesLeadText(c));
  if (c.truncated) m.line("（比べた相手は課題が多かったため、一部の課題だけです）");
  m.blank();
  m.line("### 完了の見込み日が動いた課題（" + std::to_string(c.finishMovedTotal) + " 件）");
  m.blank();
  if (c.finishMoved.empty()) m.line("- なし");
  for (const WeeklyFinishMove& mv : c.finishMoved)
    m.line("- " + escapeMarkdown(title(mv.did, mv.name)) + "（担当: " + who2(mv.assignee) + "・" + fmtMd(mv.from) + " → " + fmtMd(mv.to) +
           "・" + shiftText(mv.shiftDays) + "）");
  if (c.finishMovedTotal > static_cast<int>(c.finishMoved.size()))
    m.line("- " + moreText(c.finishMovedTotal - static_cast<int>(c.finishMoved.size())));
  m.blank();
  m.line("### 新たに遅れた課題（" + std::to_string(c.newlyLate.total) + " 件）");
  m.blank();
  if (c.newlyLate.items.empty()) m.line("- なし");
  for (const WeeklyItem& it : c.newlyLate.items)
    m.line("- " + escapeMarkdown(title(it.did, it.name)) + "（担当: " + who2(it.assignee) + "・" + floatPhrase(it.floatDays) + "）");
  listMore(m, c.newlyLate);
  m.blank();
  m.line("### 新しく完了した課題（" + std::to_string(c.newlyDone.total) + " 件）");
  m.blank();
  if (c.newlyDone.items.empty()) m.line("- なし");
  for (const WeeklyItem& it : c.newlyDone.items) {
    std::string s = "- " + escapeMarkdown(title(it.did, it.name)) + "（担当: " + who2(it.assignee);
    if (it.resolved) s += "・完了 " + fmtMd(*it.resolved);
    m.line(s + "）");
  }
  listMore(m, c.newlyDone);
  m.blank();
  m.line("- 増えた作業: " + std::to_string(c.added) + " 件 ／ 無くなった作業: " + std::to_string(c.removed) + " 件");
}

void renderBaseline(Md& m, const WeeklyReport& r) {
  const WeeklyBaseline& b = r.baseline;
  m.heading(2, "ベースラインとの差");
  const std::string at = baselineAtText(b.at);
  m.line("ベースライン「" + escapeMarkdown(b.name) + "」" + (at.empty() ? std::string() : "（保存 " + at + "）") +
         "との比較です。営業日の差で、+ は計画より遅れ、- は早まりです。");
  if (b.truncated) m.line("（ベースラインは課題が多かったため、一部の課題だけです）");
  m.blank();
  m.line(baselineCountsText(b.sum));
  const std::string note = baselineSameDayNote(b.sum);
  if (!note.empty()) m.line(note);
  m.blank();
  const auto rows = [&](const std::string& head, const WeeklyBaselineList& l, const std::function<std::string(const WeeklyBaselineRow&)>& detail) {
    m.line("### " + head + "（" + std::to_string(l.total) + " 件）");
    m.blank();
    if (l.items.empty()) m.line("- なし");
    for (const WeeklyBaselineRow& it : l.items) {
      const std::string nm = it.did.empty() ? escapeMarkdown(it.name.empty() ? it.id : it.name) : escapeMarkdown(title(it.did, it.name));
      m.line("- " + nm + "（" + (it.assignee.empty() && it.did.empty() ? std::string() : "担当: " + who2(it.assignee) + "・") + detail(it) + "）");
    }
    if (l.omitted() > 0) m.line("- " + moreText(l.omitted()));
    m.blank();
  };
  rows("遅れた課題", b.late, baselineMoveText);
  rows("早まった課題", b.early, baselineMoveText);
  rows("計画になかった課題", b.added, baselineAddedText);
  rows("消えた課題", b.removed, baselineRemovedText);
}

void renderEvm(Md& m, const WeeklyReport& r) {
  const EvmReport& e = r.evm.report;
  m.heading(2, "EVM（出来高管理）");
  if (!e.ok()) {
    m.line(escapeMarkdown(evmReasonText(e)));
    m.blank();
    for (const std::string& s : evmNotes(e)) m.line("- " + escapeMarkdown(s));
    m.blank();
    return;
  }
  const std::string at = baselineAtText(e.baselineAt);
  m.line("ベースライン「" + escapeMarkdown(e.baselineName) + "」" + (at.empty() ? std::string() : "（保存 " + at + "）") + "の日程と比べた、基準日（" +
         fmtShort(e.base) + "）の終わりの時点の指標です。SV・ES は + が進み／遅れの向きに注意（SV は + が進み、ES の遅れは + が遅れ）。");
  m.blank();
  for (const weeklyfmt::EvmRow& row : weeklyfmt::evmRows(e)) m.line("- " + escapeMarkdown(row.label + ": " + row.value));
  m.blank();
  m.line(escapeMarkdown(evmEsSentence(e)));
  m.line(escapeMarkdown(evmForecastText(e)));
  for (const std::string& s : evmNotes(e)) m.line("- " + escapeMarkdown(s));
  m.blank();
}

}  // namespace

std::string weeklyMarkdown(const WeeklyReport& r) {
  Md m;
  const std::string name = escapeMarkdown(r.projectName);
  m.heading(1, (name.empty() ? std::string() : name + " ") + "週次レポート");
  m.line("期間: " + rangeText(r.range) + "（" + workdaysText(r.rangeWorkdays) + "） ／ 基準日: " + fmtLong(r.base));
  if (r.rangeCapped) m.line("（範囲が長すぎたため、先頭の " + std::to_string(kWeeklyMaxRangeDays) + " 日までにしました）");
  m.blank();
  const WeeklySections& s = r.sections;
  const auto notes = [&] {
    m.heading(2, "所感・課題・お願い");
    if (!r.notes.empty()) m.line(notesText(r.notes));
  };
  if (r.empty) {
    m.line("課題がありません。");
    if (s.baseline && r.baseline.available) renderBaseline(m, r);  // 全部削除したあとの「消えた課題」
    if (s.notes) notes();  // 課題がなくても、所感は書ける
    while (m.o.size() >= 2 && m.o[m.o.size() - 1] == '\n' && m.o[m.o.size() - 2] == '\n') m.o.pop_back();
    return m.o;
  }
  if (s.summary) renderSummary(m, r);
  if (s.done) renderDone(m, r);
  if (s.touched && r.touched.available) renderTouched(m, r);
  if (s.progress) renderProgress(m, r);
  if (s.next) renderNext(m, r);
  if (s.risks) renderRisks(m, r);
  if (s.milestones) renderMilestones(m, r);
  if (s.assignees) renderAssignees(m, r);
  if (s.changes && r.changes.available) renderChanges(m, r);
  if (s.baseline && r.baseline.available) renderBaseline(m, r);
  if (s.evm && r.evm.available) renderEvm(m, r);
  if (s.notes) notes();
  // 末尾は改行 1 つ
  while (m.o.size() >= 2 && m.o[m.o.size() - 1] == '\n' && m.o[m.o.size() - 2] == '\n') m.o.pop_back();
  return m.o;
}

}  // namespace roadmap
