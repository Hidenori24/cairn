// 報告: リスト（表。件名・状況・担当・期間・余裕・期限）
#include <algorithm>

#include "report_internal.hpp"
#include "roadmap/csv.hpp"

namespace roadmap::report {

std::string renderList(const Ctx& cx) {
  const ExportInput& in = cx.in;
  const Tree& T = in.tree;
  std::string out = "<h2>リスト</h2>\n";
  if (cx.rows.empty()) {
    out += "<p class=\"empty\">表示する課題がありません。</p>\n";
    return out;
  }
  const std::size_t n = std::min(cx.rows.size(), kReportMaxListRows);
  out += "<p class=\"note\">" + std::to_string(cx.rows.size()) + " 件。期間は計画（最早の開始〜完了）、余裕は営業日です。</p>\n";
  if (cx.rows.size() > n)
    out += "<p class=\"warn\">課題が多いため、先頭の " + std::to_string(n) + " 件だけを出しています。</p>\n";
  out += "<table>\n<thead><tr><th>件名</th><th>状況</th><th>担当</th><th>期間</th><th class=\"n\">余裕</th><th>期限</th></tr></thead>\n<tbody>\n";
  for (std::size_t i = 0; i < n; ++i) {
    const VisibleRow& vr = cx.rows[i];
    const std::size_t u = static_cast<std::size_t>(vr.id);
    const ScheduleRow& r = in.rows[u];
    const Issue& o = in.issues[u];
    const Risk risk = riskOf(r.floatDays, r.done);
    const bool ms = T.isLeaf(vr.id) && T.kind[u] == Kind::Ms;
    out += vr.isPar ? "<tr class=\"par\">" : "<tr>";
    // 件名: 階層はインデント（1 階層 14px。深すぎるものは止める）
    const int pad = std::min(std::max(vr.depth, 0), 12) * 14;
    out += "<td class=\"nm\" style=\"padding-left:" + std::to_string(8 + pad) + "px\"><span class=\"id\">" + std::string(ms ? "◆ " : "") + esc(T.did[u]) +
           "</span> " + esc(T.name[u]) + "</td>";
    // 状況
    if (vr.isPar) {
      out += "<td class=\"st-todo\">" + std::to_string(r.nDone) + "/" + std::to_string(r.n) + "</td>";
    } else {
      const Cat c = statusCat(o.status);
      out += std::string("<td class=\"st-") + catName(c) + "\">" + esc(o.status.empty() ? std::string("To Do") : o.status) + "</td>";
    }
    out += "<td>" + (vr.isPar ? std::string() : esc(o.assignee)) + "</td>";
    // 期間
    std::string term = fmtMd(r.asapStart) + " 〜 " + fmtMd(r.asapFinish);
    if (!vr.isPar && !ms) term += "　" + csv::fmtNum(r.weeks) + "週";
    out += "<td>" + esc(term) + "</td>";
    // 余裕（遅れは「!」を付ける）
    const char* rk = risk == Risk::Late ? "rk-late" : risk == Risk::Tight ? "rk-tight" : risk == Risk::Done ? "rk-done" : "rk-ok";
    out += std::string("<td class=\"n ") + rk + "\">" + esc(floatLabel(r)) + "</td>";
    // 期限
    const OptDay d = T.isLeaf(vr.id) ? T.dl[u] : T.ownDl[u];
    out += "<td>" + (d ? esc(fmtMd(*d)) : std::string()) + "</td></tr>\n";
  }
  out += "</tbody>\n</table>\n";
  return out;
}

}  // namespace roadmap::report
