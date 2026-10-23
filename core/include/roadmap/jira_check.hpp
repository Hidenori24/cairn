// JIRA の標準CSV取り込みで問題になる点の検査（付録E。JS版 tools/jira_check.py と同じ規則）
#pragma once

#include <string>
#include <vector>

namespace roadmap {

struct JiraCheckResult {
  bool ok = true;
  int rows = 0;
  int cols = 0;
  std::vector<std::string> errors;  // すべての問題点
  std::vector<std::string> warnings;  // JIRA が無視する可能性がある既知の追加列
  // 「名前: N件 / 列 M / OK」または「… / NG K件」と、問題点（最大12件）
  std::vector<std::string> report(const std::string& name) const;
};

JiraCheckResult checkJiraCsv(const std::string& csvText);

}  // namespace roadmap
