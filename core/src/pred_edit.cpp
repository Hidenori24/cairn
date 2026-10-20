#include "roadmap/pred_edit.hpp"

#include <algorithm>

#include "roadmap/link_candidates.hpp"
#include "roadmap/text.hpp"

namespace roadmap {

namespace {

constexpr std::u32string_view kSeps = U",，、;；\n\r";

// 課題の参照を解く。完全一致（表示ID・キー・id）→ 大文字小文字を無視した一致（表示ID・キー）の順。
// 見つからなければ -1、同じ段階に複数あれば -2
int resolve(const Tree& T, const std::vector<Issue>& issues, const std::string& ref) {
  int hit = -1, count = 0;
  for (int i = 0; i < T.n; ++i) {
    const auto ix = static_cast<std::size_t>(i);
    if (T.did[ix] == ref || issues[ix].key == ref || issues[ix].id == ref) {
      hit = i;
      ++count;
    }
  }
  if (count == 1) return hit;
  if (count > 1) return -2;
  const std::string low = text::toLower(ref);
  for (int i = 0; i < T.n; ++i) {
    const auto ix = static_cast<std::size_t>(i);
    if (text::toLower(T.did[ix]) == low || text::toLower(issues[ix].key) == low) {
      hit = i;
      ++count;
    }
  }
  return count == 1 ? hit : count > 1 ? -2 : -1;
}

std::string quoted(const std::string& s) { return "「" + s + "」"; }

bool hasSpace(const std::string& s) { return s.find(' ') != std::string::npos || s.find("　") != std::string::npos; }

}  // namespace

std::vector<PredToken> parsePredText(std::string_view in) {
  std::string s = text::trim(in);
  if (s.size() >= 2 && s.front() == '"' && s.back() == '"') s = text::replaceAll(s.substr(1, s.size() - 2), "\"\"", "\"");
  std::vector<PredToken> out;
  for (const auto& part : text::splitAny(s, kSeps, false)) {
    const std::string t = text::trim(part);
    if (!t.empty()) out.push_back({t, t});
  }
  return out;
}

std::vector<std::vector<int>> declaredPreds(const Tree& T, const std::vector<Issue>& issues) {
  std::vector<std::vector<int>> out(static_cast<std::size_t>(T.n));
  for (const int p : T.dfs)
    for (const auto& b : issues[static_cast<std::size_t>(p)].blocks) {
      const int s = T.find(b);
      if (s < 0 || s == p) continue;
      auto& v = out[static_cast<std::size_t>(s)];
      if (std::find(v.begin(), v.end(), p) == v.end()) v.push_back(p);
    }
  return out;
}

std::string formatPreds(const Tree& T, const std::vector<int>& preds) {
  std::string out;
  for (const int p : preds) {
    if (!out.empty()) out += ", ";
    out += T.did[static_cast<std::size_t>(p)];
  }
  return out;
}

std::string formatPredsForEdit(const Tree& T, const std::vector<Issue>& issues, const std::vector<int>& preds) {
  std::string out;
  for (const int p : preds) {
    const auto i = static_cast<std::size_t>(p);
    std::string ref = T.did[i];
    if (resolve(T, issues, ref) != p) ref = !issues[i].key.empty() ? issues[i].key : issues[i].id;
    if (resolve(T, issues, ref) != p) ref = issues[i].id;
    if (!out.empty()) out += ", ";
    out += ref;
  }
  return out;
}

PredPlan planPredEdit(const Tree& T, const std::vector<Issue>& issues, int self, std::string_view textIn) {
  PredPlan plan;
  if (self < 0 || self >= T.n) {
    plan.ok = false;
    plan.problems.push_back({"", "課題が見つかりません"});
    return plan;
  }
  const std::string& selfId = issues[static_cast<std::size_t>(self)].id;
  // いまの先行
  std::vector<int> cur;
  for (int i = 0; i < T.n; ++i) {
    const auto& b = issues[static_cast<std::size_t>(i)].blocks;
    if (i != self && std::find(b.begin(), b.end(), selfId) != b.end()) cur.push_back(i);
  }
  // 入力を解く
  std::vector<int> want;
  for (const auto& tk : parsePredText(textIn)) {
    const int i = resolve(T, issues, tk.ref);
    if (i == -1) {
      plan.problems.push_back({tk.raw, quoted(tk.raw) + "という課題が見つかりません" +
                                           (hasSpace(tk.raw) ? "（複数はカンマで区切ります）" : "")});
      continue;
    }
    if (i == -2) {
      plan.problems.push_back({tk.raw, quoted(tk.raw) + "に当てはまる課題が複数あります（キーか id で書いてください）"});
      continue;
    }
    if (i == self) {
      plan.problems.push_back({tk.raw, "自分自身は先行にできません"});
      continue;
    }
    if (std::find(want.begin(), want.end(), i) != want.end()) {
      plan.problems.push_back({tk.raw, quoted(tk.raw) + "が重複しています"});
      continue;
    }
    want.push_back(i);
    if (std::find(cur.begin(), cur.end(), i) != cur.end()) continue;  // すでに先行
    const LinkBlock b = checkLink(T, issues, i, self);
    if (b != LinkBlock::None) plan.problems.push_back({tk.raw, quoted(tk.raw) + "は先行にできません: " + linkBlockReason(b)});
  }
  if (!plan.problems.empty()) {
    plan.ok = false;
    return plan;
  }
  for (const int i : want)
    if (std::find(cur.begin(), cur.end(), i) == cur.end()) plan.add.push_back(issues[static_cast<std::size_t>(i)].id);
  for (const int i : cur)
    if (std::find(want.begin(), want.end(), i) == want.end()) plan.remove.push_back(issues[static_cast<std::size_t>(i)].id);
  return plan;
}

std::vector<int> predSuggestions(const Tree& T, const std::vector<Issue>& issues, int self, std::string_view textIn,
                                 std::size_t limit) {
  std::vector<int> out;
  if (self < 0 || self >= T.n) return out;
  // 入力中の項目 = 最後の区切りより後ろ
  const std::u32string u = text::toU32(textIn);
  std::size_t cut = u.size();
  while (cut > 0 && kSeps.find(u[cut - 1]) == std::u32string_view::npos) --cut;
  const std::string tail = text::toLower(text::trim(text::fromU32(u.substr(cut))));
  // すでに入力した項目
  std::vector<int> used;
  for (const auto& tk : parsePredText(text::fromU32(u.substr(0, cut))))
    if (const int i = resolve(T, issues, tk.ref); i >= 0) used.push_back(i);
  for (const int i : T.dfs) {
    if (out.size() >= limit) break;
    if (i == self || std::find(used.begin(), used.end(), i) != used.end()) continue;
    const auto ix = static_cast<std::size_t>(i);
    if (!tail.empty()) {
      const std::string hay = text::toLower(T.did[ix]) + "\n" + text::toLower(issues[ix].key) + "\n" +
                              text::toLower(issues[ix].id) + "\n" + text::toLower(T.name[ix]);
      if (hay.find(tail) == std::string::npos) continue;
    }
    const LinkBlock b = checkLink(T, issues, i, self);
    if (b == LinkBlock::Cycle || b == LinkBlock::Family || b == LinkBlock::Self) continue;
    out.push_back(i);
  }
  return out;
}

std::string completePredText(const Tree& T, const std::vector<Issue>& issues, int self, std::string_view textIn) {
  const auto sg = predSuggestions(T, issues, self, textIn, 1);
  if (sg.empty()) return std::string(textIn);
  const std::u32string u = text::toU32(textIn);
  std::size_t cut = u.size();
  while (cut > 0 && kSeps.find(u[cut - 1]) == std::u32string_view::npos) --cut;
  std::string head = text::fromU32(u.substr(0, cut));
  if (!head.empty()) {
    // 区切りの後ろには空白を 1 つ置く（"DEV-1," → "DEV-1, "）
    head = text::trimEnd(head);
    head += " ";
  }
  return head + formatPredsForEdit(T, issues, sg);
}

}  // namespace roadmap
