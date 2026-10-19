#include "roadmap/jira_check.hpp"

#include <algorithm>
#include <map>
#include <set>

#include "roadmap/csv.hpp"
#include "roadmap/text.hpp"

namespace roadmap {

namespace {
const std::set<std::string> kStdTypes{"Epic", "Task", "Sub-task", "Story", "Bug"};
const std::set<std::string> kStdCols{"Issue Id", "Issue key", "Issue Type", "Summary", "Parent", "Epic Name",
                                     "Epic Link", "Priority", "Status", "Assignee", "Start date", "Due date",
                                     "Resolved", "Original estimate", "Labels", "Description",
                                     "Outward issue link (Blocks)", "Comment"};
const std::set<std::string> kAppCols{"Earliest start", "Deadline", "Remaining (weeks)", "Duration (weeks)"};
const std::set<std::string> kPrio{"Highest", "High", "Medium", "Low", "Lowest"};

bool isAppCol(const std::string& c) { return kAppCols.count(c) || c.rfind("Duration (weeks) @", 0) == 0; }
bool dig(char c) { return c >= '0' && c <= '9'; }

// ^(\d+w)?( ?\d+d)?( ?\d+h)?( ?\d+m)?$
bool estOk(const std::string& e) {
  std::size_t i = 0;
  const char units[] = {'w', 'd', 'h', 'm'};
  for (int g = 0; g < 4; ++g) {
    std::size_t j = i;
    if (g > 0 && j < e.size() && e[j] == ' ') ++j;
    const std::size_t d0 = j;
    while (j < e.size() && dig(e[j])) ++j;
    if (j > d0 && j < e.size() && e[j] == units[g]) i = j + 1;
  }
  return i == e.size();
}
bool isoOk(const std::string& v) {
  if (v.size() != 10 || v[4] != '-' || v[7] != '-') return false;
  for (int k : {0, 1, 2, 3, 5, 6, 8, 9})
    if (!dig(v[k])) return false;
  return true;
}
// ^\d{2}/[A-Z][a-z]{2}/\d{2} \d{2}:\d{2} [AP]M;[^;]*;
bool commentOk(const std::string& c) {
  if (c.size() < 22) return false;
  const auto up = [](char x) { return x >= 'A' && x <= 'Z'; };
  const auto lo = [](char x) { return x >= 'a' && x <= 'z'; };
  if (!(dig(c[0]) && dig(c[1]) && c[2] == '/' && up(c[3]) && lo(c[4]) && lo(c[5]) && c[6] == '/' && dig(c[7]) &&
        dig(c[8]) && c[9] == ' ' && dig(c[10]) && dig(c[11]) && c[12] == ':' && dig(c[13]) && dig(c[14]) &&
        c[15] == ' ' && (c[16] == 'A' || c[16] == 'P') && c[17] == 'M' && c[18] == ';'))
    return false;
  return c.find(';', 19) != std::string::npos;
}
bool hasSpace(const std::string& s) {
  for (std::size_t i = 0; i < s.size();)
    if (text::isJsSpace(text::decodeAt(s, i))) return true;
  return false;
}
}  // namespace

std::vector<std::string> JiraCheckResult::report(const std::string& name) const {
  std::vector<std::string> out;
  out.push_back(name + ": " + std::to_string(rows) + "件 / 列 " + std::to_string(cols) + " / " +
                (ok ? std::string("OK") : "NG " + std::to_string(errors.size()) + "件"));
  for (std::size_t i = 0; i < errors.size() && i < 12; ++i) out.push_back("   - " + errors[i]);
  return out;
}

JiraCheckResult checkJiraCsv(const std::string& csvText) {
  JiraCheckResult res;
  const csv::Table t = csv::parse(csvText);
  const auto& h = t.header;
  const auto& body = t.rows;
  auto& errs = res.errors;
  const auto idx = [&](const std::string& c) -> int {
    auto it = std::find(h.begin(), h.end(), c);
    return it == h.end() ? -1 : static_cast<int>(it - h.begin());
  };
  const auto col = [&](const std::vector<std::string>& r, const std::string& c) -> std::string {
    const int i = idx(c);
    return (i >= 0 && i < static_cast<int>(r.size())) ? r[i] : "";
  };
  const auto cols = [&](const std::vector<std::string>& r, const std::string& c) {
    std::vector<std::string> out;
    for (std::size_t i = 0; i < h.size(); ++i)
      if (h[i] == c && i < r.size() && !r[i].empty()) out.push_back(r[i]);
    return out;
  };
  std::size_t firstApp = h.size();
  for (std::size_t i = 0; i < h.size(); ++i)
    if (isAppCol(h[i])) {
      firstApp = i;
      break;
    }
  for (std::size_t i = 0; i < h.size(); ++i) {
    const std::string& c = h[i];
    if (kStdCols.count(c) && i > firstApp) errs.push_back("標準列 " + c + " がアプリ独自列より後ろ");
    if (!kStdCols.count(c) && !isAppCol(c)) errs.push_back("不明な列 " + c);
  }
  std::vector<std::string> ids;
  std::set<std::string> idset;
  std::map<std::string, std::string> T;
  for (const auto& r : body) {
    ids.push_back(col(r, "Issue Id"));
    idset.insert(ids.back());
    T[col(r, "Issue Id")] = col(r, "Issue Type");
  }
  if (ids.size() != idset.size()) errs.push_back("Issue Id が重複");
  {
    std::vector<std::string> en;
    for (const auto& r : body)
      if (col(r, "Issue Type") == "Epic") en.push_back(col(r, "Epic Name"));
    std::set<std::string> u(en.begin(), en.end());
    if (en.size() != u.size()) errs.push_back("Epic Name が重複");
  }
  for (const auto& r : body) {
    const std::string i = col(r, "Issue Id"), ty = col(r, "Issue Type"), p = col(r, "Parent");
    if (!kStdTypes.count(ty)) errs.push_back(i + ": 標準にない種別 " + ty);
    const std::string sum = col(r, "Summary");
    if (text::trim(sum).empty() || text::codepointCount(sum) > 255) errs.push_back(i + ": Summary が空か255文字超");
    if (ty == "Epic" && col(r, "Epic Name").empty()) errs.push_back(i + ": Epic に Epic Name がない");
    if (ty == "Sub-task" && p.empty()) errs.push_back(i + ": Sub-task に Parent がない");
    if (!p.empty()) {
      if (!idset.count(p)) errs.push_back(i + ": Parent " + p + " が存在しない");
      else if (T[p] == "Sub-task") errs.push_back(i + ": Sub-task の下に子（Parent=" + p + "）");
      else if (ty == "Sub-task" && T[p] == "Epic") errs.push_back(i + ": Sub-task の親が Epic");
      else if (ty != "Sub-task" && T[p] != "Epic") errs.push_back(i + ": Task の親が Epic 以外（" + T[p] + "）");
    }
    const std::string pr = col(r, "Priority");
    if (!pr.empty() && !kPrio.count(pr)) errs.push_back(i + ": 優先度 " + pr);
    for (const auto& l : cols(r, "Labels"))
      if (hasSpace(l)) errs.push_back(i + ": ラベルに空白 '" + l + "'");
    const std::string e = col(r, "Original estimate");
    if (!e.empty() && !estOk(e)) errs.push_back(i + ": 見積の書式 " + e);
    for (const char* d : {"Start date", "Due date", "Resolved"}) {
      const std::string v = col(r, d);
      if (!v.empty() && !isoOk(v)) errs.push_back(i + ": " + d + " の書式 " + v);
    }
    for (const auto& b : cols(r, "Outward issue link (Blocks)"))
      if (!idset.count(b) || b == i) errs.push_back(i + ": Blocks 先 " + b + " が不正");
    for (const auto& c : cols(r, "Comment"))
      if (!commentOk(c)) errs.push_back(i + ": コメントの書式");
    const std::string s = col(r, "Start date"), du = col(r, "Due date");
    if (!s.empty() && !du.empty() && s > du) errs.push_back(i + ": Start > Due");
  }
  res.rows = static_cast<int>(body.size());
  res.cols = static_cast<int>(h.size());
  res.ok = errs.empty();
  return res;
}

}  // namespace roadmap
