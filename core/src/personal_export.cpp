#include "roadmap/personal.hpp"
#include "roadmap/report.hpp"
#include "roadmap/csv.hpp"
#include <cmath>

namespace roadmap {
namespace {
std::string field(const Json& j, const char* key) {
  return j.is_object() && j.contains(key) && j[key].is_string() ? j[key].get<std::string>() : "";
}
std::optional<double> value(const Json& j, const char* key) {
  if (!j.is_object() || !j.contains(key) || !j[key].is_number()) return {};
  const double n = j[key].get<double>();
  return std::isfinite(n) && n >= 0 && n <= 100 ? std::optional<double>(n) : std::nullopt;
}
std::string pct(const Json& j, const char* key) { const auto n = value(j, key); return n ? csv::fmtNum(*n) + "%" : "—"; }
std::string md(const std::string& in) {
  std::string out;
  for (char c : in) {
    if (c == '\r') continue;
    if (c == '\n') { out += "<br>"; continue; }
    if (c == '<') { out += "&lt;"; continue; }
    if (c == '>') { out += "&gt;"; continue; }
    if (c == '&') { out += "&amp;"; continue; }
    if (std::string("\\`*_[]|#!~").find(c) != std::string::npos) out += '\\';
    out += c;
  }
  return out;
}
const char* css = R"(<style>
.personal-report{color:#182230;background:#fff;line-height:1.5;padding:16px;box-sizing:border-box;overflow-wrap:anywhere}
.personal-report h2{font-size:20px;margin:0 0 8px}.personal-report h3{margin:16px 0 6px}
.personal-report .meta{color:#475467;font-size:12px}.personal-report table{border-collapse:collapse;width:100%;table-layout:fixed;font-size:12px}
.personal-report th,.personal-report td{border-bottom:1px solid #d0d5dd;padding:7px;text-align:left;vertical-align:top;overflow-wrap:anywhere}
.personal-report th:first-child{width:40%}.personal-report th{background:#f2f4f7}.personal-report .behind{background:#fff0f0;color:#b42318}
.personal-report .ahead{background:#eff8ff;color:#175cd3}.personal-report .comment,.personal-report .notes{white-space:pre-wrap;overflow-wrap:anywhere}
.personal-report .notes{border-left:3px solid #d0d5dd;padding:8px}.personal-report thead{display:table-header-group}
@page personal{size:A4 portrait;margin:12mm}
@media print{.personal-report{page:personal;break-before:page;margin:0;padding:0;max-width:none}.personal-document>.personal-report:first-child{break-before:auto}
.personal-report tr{break-inside:avoid}.personal-report tr.comment-row{break-inside:auto}.personal-report tr.has-comment{break-after:avoid}
.personal-report h2,.personal-report h3{break-after:avoid}.personal-report .notes{orphans:3;widows:3}}
</style>)";
}
Result<std::string> renderPersonalReports(const Json& data, const std::vector<std::string>& ids, bool html, bool fragment) {
  if (ids.empty()) return Error{"保存済みの報告を選んでください"};
  if (!data.is_object() || !data.contains("reports") || !data["reports"].is_object()) return Error{"保存済みの報告がありません"};
  std::string out;
  if (html) {
    if (!fragment) out = "<!doctype html><html lang=\"ja\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>個人報告</title>";
    out += css;
    if (!fragment) out += "</head><body class=\"personal-document\">";
  }
  for (const auto& id : ids) {
    if (!data["reports"].contains(id) || !data["reports"][id].is_object()) return Error{"選んだ保存済み報告が見つかりません: " + id};
    const auto& report = data["reports"][id];
    const std::string heading = field(report, "memberName") + " — " + field(report, "date");
    const std::string period = "対象期間 " + field(report, "from") + " 〜 " + field(report, "to") + " / 保存 " + field(report, "savedAt");
    const std::string baseline = field(report, "baselineName").empty() ? "ベースラインなし（予定との比較なし）" : "ベースライン: " + field(report, "baselineName");
    if (html) out += "<section class=\"personal-report\"><h2>" + escapeXml(heading) + "</h2><p class=\"meta\">" + escapeXml(period) + "<br>" + escapeXml(baseline) + "</p>";
    else out += "# " + md(heading) + "\n\n" + md(period) + "\n\n" + md(baseline) + "\n\n";
    for (const char* section : {"tasks", "extra"}) {
      const std::string title = std::string(section) == "tasks" ? "タスク" : "タスク外の仕事";
      if (html) out += "<h3>" + title + "</h3><table><thead><tr><th>件名</th><th>状態</th><th>予定</th><th>実績</th><th>前回</th></tr></thead><tbody>";
      else out += "## " + title + "\n\n| 件名 | 状態 | 予定 | 実績 | 前回 |\n|---|---|---|---|---|\n";
      if (report.contains(section) && report[section].is_array()) for (const auto& task : report[section]) {
        const auto comment = field(task, "comment");
        const int tone = progressTone(value(task, "actual"), value(task, "planned"));
        const std::string cls = std::string(tone < 0 ? "behind" : tone > 0 ? "ahead" : "") + (comment.empty() ? "" : " has-comment");
        if (html) out += "<tr class=\"" + cls + "\">"; else out += "|";
        for (const auto& text : {field(task, "name"), field(task, "status"), pct(task, "planned"), pct(task, "actual"), pct(task, "previous")})
          out += html ? "<td>" + escapeXml(text) + "</td>" : " " + md(text) + " |";
        out += html ? "</tr>" : "\n";
        if (!comment.empty()) out += html ? "<tr class=\"comment-row\"><td colspan=\"5\" class=\"comment\">" + escapeXml(comment) + "</td></tr>" : "| " + md(comment) + " | | | | |\n";
      }
      out += html ? "</tbody></table>" : "\n";
    }
    for (const char* key : {"blockers", "next"}) {
      const std::string title = std::string(key) == "blockers" ? "困りごと" : "次の期間の予定";
      if (field(report, key).empty()) continue;
      if (html) out += "<h3>" + title + "</h3><div class=\"notes\">" + escapeXml(field(report, key)) + "</div>";
      else out += "## " + title + "\n\n" + md(field(report, key)) + "\n\n";
    }
    out += html ? "</section>" : "---\n\n";
  }
  if (html && !fragment) out += "</body></html>";
  return out;
}
}
