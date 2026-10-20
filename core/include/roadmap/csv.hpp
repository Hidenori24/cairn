// CSV入出力（SPEC 6章。JS版 csvio.js と同じ結果になること）
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "roadmap/calendar.hpp"
#include "roadmap/model.hpp"
#include "roadmap/result.hpp"

namespace roadmap::csv {

struct Table {
  std::vector<std::string> header;
  std::vector<std::vector<std::string>> rows;
};

// 字句解析（区切りは先頭行で自動判定。BOMは除く）
Table parse(std::string_view text);
std::string stringify(const std::vector<std::vector<std::string>>& rows);
// 見出しの正規化（小文字化・空白と _ の連続を1つの空白に・前後を削る）
std::string norm(std::string_view s);

// ---- 値の読み取り・書式 ----
OptDay parseDate(std::string_view s);
std::optional<double> parseDur(std::string_view s, bool plainIsWeeks);
std::string parseDT(std::string_view s);   // → "YYYY-MM-DDTHH:MM"（読めなければ空）
std::string fmtDT(std::string_view iso);   // "2026-10-19T10:30" → "19/Oct/26 10:30 AM"
Comment parseComment(std::string_view cell);
std::string fmtComment(const Comment& c);
std::string fmtDate(const OptDay& n);
std::string fmtNum(double w);
std::string fmtEst(const std::optional<double>& w);

// ---- 取り込み ----
struct ImportResult {
  std::vector<Issue> issues;
  ProjectMeta meta;
  std::vector<std::string> warns;
};
Result<ImportResult> importCsv(std::string_view text);

// ---- 書き出し ----
struct PlanInfo {
  Day start = 0;
  Day finish = 0;
  bool leaf = false;
  double weeks = 0;      // 現在のシナリオの週
  double baseWeeks = 0;  // シナリオに関係しない基本の週（葉のみ）
};
struct ExportOptions {
  bool planned = false;  // JIRA用
  std::function<std::optional<PlanInfo>(const std::string& id)> plan;
  std::function<OptDay(const std::string& id)> ownDeadline;
};
std::string exportCsv(const std::vector<Issue>& issues, const ProjectMeta& meta, const ExportOptions& opts = {});

// 空のプロジェクトの CSV（JS版の EMPTY_CSV）
std::string emptyCsv();

}  // namespace roadmap::csv
