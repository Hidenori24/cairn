// 状況の件数の履歴（Issue #64。#54 の B）: 記録・保存（settings.json の "history"）・累積フロー図の列
#include "roadmap/history.hpp"

#include <algorithm>
#include <cstdint>

#include "roadmap/schedule.hpp"

namespace roadmap {

namespace {

using Json = nlohmann::ordered_json;

Day mondayOf(Day d) { return d - (dow(d) + 6) % 7; }

// 点の検査: 月曜の ISO 日付・件数が 0〜上限・合計が 3 つの和と一致
bool validPoint(const HistoryPoint& p, Day* weekOut = nullptr) {
  const OptDay d = parseIso(p.weekStart);
  if (!d || fmtIso(*d) != p.weekStart || mondayOf(*d) != *d) return false;  // 正規の表記だけ（"2026-1-5" などは断る）
  const long long cs[3] = {p.todo, p.doing, p.done};
  long long sum = 0;
  for (long long c : cs) {
    if (c < 0 || c > kHistoryMaxCount) return false;
    sum += c;
  }
  if (sum != p.total) return false;
  if (weekOut) *weekOut = *d;
  return true;
}

// 昇順を保って入れる（同じ週は置き換える）。上限を超えたら古いものから落とす
void insertSorted(History& h, HistoryPoint p) {
  auto it = std::lower_bound(h.points.begin(), h.points.end(), p,
                             [](const HistoryPoint& a, const HistoryPoint& b) { return a.weekStart < b.weekStart; });
  if (it != h.points.end() && it->weekStart == p.weekStart) *it = std::move(p);
  else h.points.insert(it, std::move(p));
  if (h.points.size() > kHistoryMaxWeeks)
    h.points.erase(h.points.begin(), h.points.begin() + static_cast<std::ptrdiff_t>(h.points.size() - kHistoryMaxWeeks));
}

// 整数の欄を 0〜maxV で読む（欠けている・整数でない・範囲外は false）
bool readInt(const Json& j, const char* key, long long maxV, long long& out) {
  if (!j.contains(key)) return false;
  const Json& v = j[key];
  if (!v.is_number_integer()) return false;
  if (v.is_number_unsigned()) {
    const std::uint64_t u = v.get<std::uint64_t>();
    if (u > static_cast<std::uint64_t>(maxV)) return false;
    out = static_cast<long long>(u);
    return true;
  }
  const long long n = v.get<long long>();
  if (n < 0 || n > maxV) return false;
  out = n;
  return true;
}

bool pointFromJson(const Json& j, HistoryPoint& out) {
  if (!j.is_object()) return false;
  if (!j.contains("week") || !j["week"].is_string()) return false;
  HistoryPoint p;
  p.weekStart = j["week"].get<std::string>();
  long long todo = 0, doing = 0, done = 0, total = 0;
  if (!readInt(j, "todo", kHistoryMaxCount, todo) || !readInt(j, "doing", kHistoryMaxCount, doing) || !readInt(j, "done", kHistoryMaxCount, done) ||
      !readInt(j, "total", 3 * kHistoryMaxCount, total))
    return false;
  p.todo = static_cast<int>(todo);
  p.doing = static_cast<int>(doing);
  p.done = static_cast<int>(done);
  p.total = static_cast<int>(total);
  if (!validPoint(p)) return false;  // 日付・月曜・合計の一致
  for (auto it = j.begin(); it != j.end(); ++it)
    if (it.key() != "week" && it.key() != "todo" && it.key() != "doing" && it.key() != "done" && it.key() != "total")
      p.extra[it.key()] = it.value();
  out = std::move(p);
  return true;
}

}  // namespace

std::string historyWeekStart(Day d) { return fmtIso(mondayOf(d)); }

Result<HistoryPoint> captureHistoryPoint(const std::vector<Issue>& issues, const Computed& c, Day today) {
  if (c.T.n != static_cast<int>(issues.size()) || c.R.size() != issues.size()) return Error{"課題の数と日程の結果が合いません"};
  HistoryPoint p;
  p.weekStart = historyWeekStart(today);
  for (int id : c.T.leafOrder) {  // 統計（computeStats）と同じ: 葉だけ。マイルストーンは数えない
    if (c.T.kind[static_cast<std::size_t>(id)] == Kind::Ms) continue;
    const ScheduleRow& r = c.R[static_cast<std::size_t>(id)];
    if (r.done) ++p.done;
    else if (r.prog) ++p.doing;
    else ++p.todo;
  }
  p.total = p.todo + p.doing + p.done;
  return p;
}

Status recordHistory(History& h, HistoryPoint p) {
  if (!validPoint(p)) return Error{"この記録は使えません（月曜の日付・0 以上の件数・合計の一致が必要です）"};
  insertSorted(h, std::move(p));
  return okStatus();
}

bool historyHasWeek(const History& h, Day d) {
  const std::string w = historyWeekStart(d);
  return std::binary_search(h.points.begin(), h.points.end(), HistoryPoint{w, 0, 0, 0, 0},
                            [](const HistoryPoint& a, const HistoryPoint& b) { return a.weekStart < b.weekStart; });
}

Json historyToJson(const History& h) {
  Json pts = Json::array();
  for (const HistoryPoint& p : h.points) {
    Json o = p.extra.is_object() ? p.extra : Json::object();
    for (const char* k : {"week", "todo", "doing", "done", "total"}) o.erase(k);
    o["week"] = p.weekStart;
    o["todo"] = p.todo;
    o["doing"] = p.doing;
    o["done"] = p.done;
    o["total"] = p.total;
    pts.push_back(std::move(o));
  }
  Json j = h.extra.is_object() ? h.extra : Json::object();
  j.erase("points");
  j["points"] = std::move(pts);
  return j;
}

History historyFromJson(const Json& j) {
  History h;
  if (!j.is_object()) return h;
  for (auto it = j.begin(); it != j.end(); ++it)
    if (it.key() != "points") h.extra[it.key()] = it.value();
  if (!j.contains("points") || !j["points"].is_array()) return h;
  for (const Json& x : j["points"]) {
    HistoryPoint p;
    if (!pointFromJson(x, p)) continue;  // 壊れた点は読み飛ばす
    // 昇順で重ならないものだけ（重複・順序が逆のものは壊れた値として読み飛ばす）
    if (!h.points.empty() && !(h.points.back().weekStart < p.weekStart)) continue;
    h.points.push_back(std::move(p));
    if (h.points.size() > kHistoryMaxWeeks) h.points.erase(h.points.begin());  // 古いものから落とす
  }
  return h;
}

CfdSeries cfdSeries(const History& h, Day baseDay) {
  CfdSeries s;
  if (h.points.empty()) return s;
  const auto weekOf = [](const HistoryPoint& p) { return parseIso(p.weekStart).value_or(0); };
  const Day first = weekOf(h.points.front());
  const Day last = weekOf(h.points.back());
  const Day end = std::max(last, mondayOf(baseDay));
  // 先頭は最初の記録の週。ただし列は kHistoryMaxWeeks までで、超えるときは新しいほうを残す
  const long long span = (static_cast<long long>(end) - first) / 7 + 1;
  const long long skip = span > static_cast<long long>(kHistoryMaxWeeks) ? span - static_cast<long long>(kHistoryMaxWeeks) : 0;
  const long long n = span - skip;
  s.weeks.reserve(static_cast<std::size_t>(n));
  std::size_t pi = 0;
  for (long long k = 0; k < n; ++k) {
    CfdWeek w;
    w.start = static_cast<Day>(static_cast<long long>(end) - (n - 1 - k) * 7);
    w.gap = true;
    while (pi < h.points.size() && weekOf(h.points[pi]) < w.start) ++pi;
    if (pi < h.points.size() && weekOf(h.points[pi]) == w.start) {
      const HistoryPoint& p = h.points[pi];
      w.gap = false;
      w.todo = p.todo;
      w.doing = p.doing;
      w.done = p.done;
      w.total = p.total;
      ++s.recorded;
      s.maxTotal = std::max(s.maxTotal, p.total);
    }
    s.weeks.push_back(w);
  }
  return s;
}

std::string cfdPendingText(int recordedWeeks) { return "記録が溜まるとここに出ます（いま " + std::to_string(recordedWeeks) + " 週ぶん）"; }

std::string cfdWeekText(const CfdWeek& w) {
  std::string t = fmtMd(w.start) + " からの週\n";
  if (w.gap) return t + "記録なし";
  return t + "未着手 " + std::to_string(w.todo) + " 件\n進行中 " + std::to_string(w.doing) + " 件\n完了 " + std::to_string(w.done) + " 件\n合計 " +
         std::to_string(w.total) + " 件";
}

}  // namespace roadmap
