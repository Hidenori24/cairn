#include "roadmap/gen.hpp"

#include <algorithm>
#include <random>
#include <unordered_map>

#include "roadmap/engine.hpp"

namespace roadmap {

Generated generateProject(const GenOptions& opt) {
  std::mt19937_64 rng(opt.seed);
  const auto rnd = [&](int n) { return static_cast<int>(rng() % static_cast<std::uint64_t>(n)); };
  const auto chance = [&](double p) { return std::uniform_real_distribution<double>(0, 1)(rng) < p; };
  static const char* const kWho[] = {"田中", "佐藤", "鈴木", "高橋", "伊藤", "渡辺"};
  static const char* const kStatus[] = {"To Do", "In Progress", "Done"};
  static const char* const kWords[] = {"設計", "実装", "レビュー", "試験", "文書", "調査", "評価", "移行"};
  const Day base = daysFromCivil(2026, 10, 19);

  Generated g;
  g.meta.hasDeadlineCol = true;
  if (opt.withScenario) g.meta.scenarios = {"案B"};
  auto& v = g.issues;
  v.reserve(static_cast<std::size_t>(opt.count));
  std::vector<int> kidsCount;
  for (int i = 0; i < opt.count; ++i) {
    Issue o;
    o.id = std::to_string(i + 1);
    // 親: 先頭や一部は最上位。それ以外は前に作った課題（深さはほどほど）
    if (i > 0 && !chance(0.08)) {
      const int p = std::max(0, i - 1 - rnd(std::min(i, 12)));
      o.parent = v[p].id;
    }
    const bool top = o.parent.empty();
    o.type = top ? "Epic" : (chance(0.05) ? "Milestone" : (chance(0.5) ? "Task" : "Sub-task"));
    o.summary = std::string(kWords[rnd(8)]) + " " + std::to_string(i + 1);
    if (chance(0.2)) o.summary = "[G-" + std::to_string(i + 1) + "] " + o.summary;
    o.prio = kPriorities[rnd(5)];
    o.assignee = chance(0.8) ? kWho[rnd(6)] : "";
    o.durW = o.type == "Milestone" ? 0.0 : (rnd(8) + 1) * 0.5;
    if (opt.withScenario && chance(0.2)) o.alt.set("案B", (rnd(6) + 1) * 0.5);
    if (chance(0.1)) o.deadline = base + 20 + rnd(300);
    if (chance(0.05)) o.notBefore = base + rnd(60);
    if (opt.withStatus) {
      const int s = rnd(10);
      if (s < 2) {
        o.status = kStatus[2];
        o.start = base - 20 - rnd(20);
        o.resolved = base - rnd(15);
      } else if (s < 3) {
        o.status = kStatus[1];
        o.start = base - rnd(10);
        if (chance(0.5)) o.remW = (rnd(4) + 1) * 0.5;
      } else {
        o.status = kStatus[0];
      }
    }
    if (chance(0.3)) o.labels.push_back(chance(0.5) ? "重要" : "label" + std::to_string(rnd(5)));
    if (opt.withComments && chance(0.1))
      o.comments.push_back({"2026-10-0" + std::to_string(1 + rnd(9)) + "T1" + std::to_string(rnd(10)) + ":05",
                            kWho[rnd(6)], "メモ, \"引用\"\n改行"});
    o.desc = chance(0.3) ? "説明 " + std::to_string(i) : "";
    v.push_back(std::move(o));
  }
  // 依存: 末端どうし、配列の前から後ろへだけ（循環しない）
  std::vector<char> hasKid(v.size(), 0);
  std::unordered_map<std::string, int> idx;
  for (std::size_t i = 0; i < v.size(); ++i) idx[v[i].id] = static_cast<int>(i);
  for (const auto& o : v)
    if (!o.parent.empty()) hasKid[idx[o.parent]] = 1;
  std::vector<int> leaves;
  for (std::size_t i = 0; i < v.size(); ++i)
    if (!hasKid[i]) leaves.push_back(static_cast<int>(i));
  for (std::size_t k = 1; k < leaves.size(); ++k) {
    const int to = leaves[k];
    int n = 0;
    while (n < 2 && chance(opt.depRate / (n + 1))) {
      const int from = leaves[k - 1 - static_cast<std::size_t>(rnd(static_cast<int>(std::min<std::size_t>(k, 30))))];
      auto& b = v[from].blocks;
      if (std::find(b.begin(), b.end(), v[to].id) == b.end()) b.push_back(v[to].id);
      ++n;
    }
  }
  // 時々、親への依存も入れる（循環するなら外す）
  for (int t = 0; t < opt.count / 50; ++t) {
    const int a = rnd(opt.count), b = rnd(opt.count);
    if (a == b) continue;
    v[a].blocks.push_back(v[b].id);
    if (!buildTree(v, g.meta)) v[a].blocks.pop_back();
  }
  return g;
}

}  // namespace roadmap
