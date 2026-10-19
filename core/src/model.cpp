#include "roadmap/model.hpp"

#include <cstdlib>

#include "roadmap/text.hpp"

namespace roadmap {

TagName tagOf(std::string_view s) {
  TagName none{"", std::string(s)};
  if (s.empty() || s[0] != '[') return none;
  const std::size_t close = s.find(']', 1);
  if (close == std::string_view::npos || close == 1) return none;
  std::size_t i = close + 1;
  while (i < s.size()) {
    std::size_t j = i;
    if (!text::isJsSpace(text::decodeAt(s, j))) break;
    i = j;
  }
  const std::string_view rest = s.substr(i);
  for (std::size_t k = 0; k < rest.size();)
    if (text::isLineTerminator(text::decodeAt(rest, k))) return none;
  return {std::string(s.substr(1, close - 1)), std::string(rest)};
}

bool isEpicType(std::string_view type) {
  return text::icontains(type, "epic") || type.find("エピック") != std::string_view::npos;
}

bool isMilestoneText(std::string_view s) {
  return text::icontains(s, "milestone") || s.find("マイルストーン") != std::string_view::npos;
}

bool isMilestone(const Issue& o) {
  if (isMilestoneText(o.type)) return true;
  for (const auto& l : o.labels)
    if (isMilestoneText(l)) return true;
  return false;
}

Cat statusCat(std::string_view status) {
  const std::string s = text::trim(status);
  static const char* const kDone[] = {"done", "closed", "resolved", "complete", "完了", "解決", "クローズ", "済"};
  for (const char* p : kDone)
    if (text::istartsWith(s, p)) return Cat::Done;
  static const char* const kProg[] = {"progress", "進行", "対応中", "doing", "working", "review", "レビュー"};
  for (const char* p : kProg)
    if (text::icontains(s, p)) return Cat::Prog;
  return Cat::Todo;
}

const char* catName(Cat c) {
  switch (c) {
    case Cat::Done: return "done";
    case Cat::Prog: return "prog";
    default: return "todo";
  }
}

int prioRank(std::string_view prio) {
  for (int i = 0; i < 5; ++i)
    if (text::iequals(prio, kPriorities[i])) return i;
  return 2;
}

std::string nextIssueId(const std::vector<Issue>& issues) {
  double m = 0;
  for (const auto& o : issues)
    if (text::isAsciiDigits(o.id)) {
      const double v = std::strtod(o.id.c_str(), nullptr);
      if (v > m) m = v;
    }
  return text::jsNumber(m + 1);
}

}  // namespace roadmap
