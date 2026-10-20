#include "roadmap/dependency_view.hpp"

namespace roadmap {

namespace {
// seeds から forward（後続）か逆（先行）の向きにだけたどれる葉を mark に足す（seeds を含む）
void walk(const Tree& T, const std::vector<int>& seeds, bool forward, std::vector<char>& mark) {
  std::vector<char> seen(static_cast<std::size_t>(T.n), 0);
  std::vector<int> stack;
  for (const int s : seeds)
    if (!seen[static_cast<std::size_t>(s)]) {
      seen[static_cast<std::size_t>(s)] = 1;
      stack.push_back(s);
    }
  while (!stack.empty()) {
    const int x = stack.back();
    stack.pop_back();
    mark[static_cast<std::size_t>(x)] = 1;
    for (const int y : forward ? T.succs[static_cast<std::size_t>(x)] : T.preds[static_cast<std::size_t>(x)])
      if (!seen[static_cast<std::size_t>(y)]) {
        seen[static_cast<std::size_t>(y)] = 1;
        stack.push_back(y);
      }
  }
}
}  // namespace

std::vector<char> traceDependencies(const Tree& T, int self) {
  std::vector<char> mark(static_cast<std::size_t>(T.n), 0);
  if (self < 0 || self >= T.n) return mark;
  const std::vector<int>& seeds = T.leaves[static_cast<std::size_t>(self)];
  walk(T, seeds, true, mark);
  walk(T, seeds, false, mark);
  // 葉の祖先（親の行）も含める。葉だけが対象のとき親が薄くなってしまわないように
  std::vector<char> leafMark = mark;
  for (int i = 0; i < T.n; ++i)
    if (leafMark[static_cast<std::size_t>(i)])
      for (int p = T.parentOf[static_cast<std::size_t>(i)]; p >= 0; p = T.parentOf[static_cast<std::size_t>(p)])
        mark[static_cast<std::size_t>(p)] = 1;
  return mark;
}

bool isDateConflict(const ScheduleRow& pred, const ScheduleRow& succ, bool succZeroWidth) {
  if (succ.done) return false;
  return succZeroWidth ? succ.asapStart < pred.asapFinish : succ.asapStart <= pred.asapFinish;
}

bool isCriticalLink(const ScheduleRow& pred, const ScheduleRow& succ) {
  return !pred.done && !succ.done && pred.floatDays <= 0 && succ.floatDays <= 0;
}

}  // namespace roadmap
