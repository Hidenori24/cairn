#include "roadmap/link_candidates.hpp"

#include <algorithm>
#include <limits>

#include "roadmap/text.hpp"

namespace roadmap {

namespace {

// seeds から依存の矢印（forward = 後続の向き、そうでなければ先行の向き）にたどれる葉の集合（seeds を含む）
std::vector<char> reach(const Tree& T, const std::vector<int>& seeds, bool forward) {
  std::vector<char> seen(static_cast<std::size_t>(T.n), 0);
  std::vector<int> stack;
  for (int s : seeds)
    if (!seen[s]) {
      seen[s] = 1;
      stack.push_back(s);
    }
  while (!stack.empty()) {
    const int x = stack.back();
    stack.pop_back();
    for (int y : forward ? T.succs[x] : T.preds[x])
      if (!seen[y]) {
        seen[y] = 1;
        stack.push_back(y);
      }
  }
  return seen;
}

bool overlaps(const std::vector<int>& a, const std::vector<int>& b) {
  for (int x : a)
    if (std::find(b.begin(), b.end(), x) != b.end()) return true;
  return false;
}

bool hits(const std::vector<char>& set, const std::vector<int>& leaves) {
  for (int x : leaves)
    if (set[x]) return true;
  return false;
}

bool hasBlock(const Issue& from, const std::string& toId) {
  return std::find(from.blocks.begin(), from.blocks.end(), toId) != from.blocks.end();
}

// self が pred か succ のどちらか。もう一方の相手 other について判定する（reachSet は self 側から事前に求めたもの）
LinkBlock judge(const Tree& T, const std::vector<Issue>& issues, int self, int other, LinkDir dir,
                const std::vector<char>& reachSet) {
  if (self == other) return LinkBlock::Self;
  const int pred = dir == LinkDir::AddPred ? other : self;
  const int succ = dir == LinkDir::AddPred ? self : other;
  if (hasBlock(issues[pred], issues[succ].id)) return LinkBlock::Already;
  if (overlaps(T.leaves[self], T.leaves[other])) return LinkBlock::Family;
  if (hits(reachSet, T.leaves[other])) return LinkBlock::Cycle;
  return LinkBlock::None;
}

}  // namespace

const char* linkBlockReason(LinkBlock b) {
  switch (b) {
    case LinkBlock::None: return "";
    case LinkBlock::Self: return "自分自身にはつなげません";
    case LinkBlock::Already: return "すでにつながっています";
    case LinkBlock::Cycle: return "つなぐと先行・後続が循環します";
    case LinkBlock::Family: return "親子の間にはつなげません";
  }
  return "";
}

LinkBlock checkLink(const Tree& T, const std::vector<Issue>& issues, int pred, int succ) {
  if (pred < 0 || succ < 0 || pred >= T.n || succ >= T.n) return LinkBlock::Self;
  // succ から後続の向きにたどった先に pred の葉があれば、pred → succ で循環する
  return judge(T, issues, succ, pred, LinkDir::AddPred, reach(T, T.leaves[succ], true));
}

std::vector<LinkCandidate> linkCandidates(const Tree& T, const std::vector<Issue>& issues, int self, LinkDir dir,
                                          std::string_view query, const std::vector<std::string>& recentIds,
                                          std::size_t limit) {
  std::vector<LinkCandidate> out;
  if (self < 0 || self >= T.n) return out;
  // 先行を足す（相手 → 自分）なら、自分から後続へたどれる葉が相手にあると循環。後続を足すなら逆
  const std::vector<char> rs = reach(T, T.leaves[self], dir == LinkDir::AddPred);

  const std::vector<std::string> terms = text::splitAny(text::toLower(text::trim(query)), U"", true);
  const std::string exact = text::toLower(text::trim(query));

  struct Item {
    LinkCandidate c;
    int tier;       // 0 = 完全一致 1 = 最近 2 = 兄弟 3 = その他
    std::size_t recentPos;
    int pos;        // ツリーの順
  };
  std::vector<Item> items;
  const int myParent = T.parentOf[self];
  for (int p = 0; p < T.n; ++p) {
    const int i = T.dfs[static_cast<std::size_t>(p)];
    const LinkBlock b = judge(T, issues, self, i, dir, rs);
    if (b == LinkBlock::Self || b == LinkBlock::Already) continue;
    if (!terms.empty()) {
      const std::string hay = text::toLower(T.did[i]) + "\n" + text::toLower(issues[i].key) + "\n" +
                              text::toLower(issues[i].id) + "\n" + text::toLower(T.name[i]);
      bool all = true;
      for (const auto& t : terms)
        if (hay.find(t) == std::string::npos) {
          all = false;
          break;
        }
      if (!all) continue;
    }
    Item it;
    it.c.idx = i;
    it.c.block = b;
    it.c.sibling = T.parentOf[i] == myParent;
    const auto rp = std::find(recentIds.begin(), recentIds.end(), issues[i].id);
    it.c.recent = rp != recentIds.end();
    it.recentPos = it.c.recent ? static_cast<std::size_t>(rp - recentIds.begin()) : 0;
    const bool isExact = !exact.empty() && (text::toLower(T.did[i]) == exact || text::toLower(issues[i].key) == exact ||
                                            text::toLower(issues[i].id) == exact);
    it.tier = isExact ? 0 : it.c.recent ? 1 : it.c.sibling ? 2 : 3;
    it.pos = p;
    items.push_back(std::move(it));
  }
  std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
    const bool ao = a.c.block == LinkBlock::None, bo = b.c.block == LinkBlock::None;
    if (ao != bo) return ao;  // つなげられるものが先
    if (a.tier != b.tier) return a.tier < b.tier;
    if (a.tier == 1 && a.recentPos != b.recentPos) return a.recentPos < b.recentPos;
    return a.pos < b.pos;
  });
  for (const auto& it : items) {
    if (out.size() >= limit) break;
    out.push_back(it.c);
  }
  return out;
}

}  // namespace roadmap
