// マイルストーンの予測の推移（Issue #75）
#include "roadmap/mta.hpp"

#include <algorithm>
#include <unordered_map>

namespace roadmap {

int mtaClampLateDays(int v) { return std::max(kMtaMinLateDays, std::min(v, kMtaMaxLateDays)); }

Result<MtaReport> computeMta(const Tree& T, const std::vector<Issue>& issues, const std::vector<ScheduleRow>& rows, const Calendar& cal,
                             const WeeklyAuto& autos, Day base, const BaselineCompare* baseline, const MtaOptions& opt) {
  const std::size_t n = T.n > 0 ? static_cast<std::size_t>(T.n) : 0;
  if (issues.size() != n || rows.size() != n || T.dfs.size() != n || T.kids.size() != n || T.kind.size() != n || T.ids.size() != n ||
      T.did.size() != n || T.name.size() != n || T.dl.size() != n)
    return Error{"課題と日程の数が合わないため、マイルストーンの予測の推移を出せません"};

  MtaReport r;
  r.base = base;
  r.lateDays = mtaClampLateDays(opt.lateDays);
  const Day baseMon = weeklyMondayOf(base);

  // ---- 横軸: マイルストーンの記録がある最初の週から、最後の記録か基準日の週のうち後のほうまで + 「いま」----
  std::vector<const WeeklySnapshot*> bySlot;  // 列ごとのスナップショット（記録のない週は nullptr）
  bool anySnap = false;
  Day first = 0, last = 0;
  for (const WeeklySnapshot& s : autos.snaps) {
    if (s.milestones.empty()) continue;
    const Day w = weeklyMondayOf(s.base);
    if (!anySnap || w < first) first = w;
    if (!anySnap || w > last) last = w;
    anySnap = true;
  }
  if (anySnap) {
    const Day end = std::max(last, baseMon);
    const long long span = (static_cast<long long>(end) - first) / 7 + 1;
    const long long cols = std::min<long long>(span, kMtaMaxWeeks);
    r.truncatedWeeks = span > cols;
    for (long long k = 0; k < cols; ++k) {
      const Day w = static_cast<Day>(static_cast<long long>(end) - (cols - 1 - k) * 7);
      const WeeklySnapshot* hit = nullptr;
      for (const WeeklySnapshot& s : autos.snaps)
        if (!s.milestones.empty() && weeklyMondayOf(s.base) == w) hit = &s;
      MtaColumn c;
      c.week = w;
      c.recorded = hit != nullptr;
      if (hit) ++r.recordedWeeks;
      r.columns.push_back(c);
      bySlot.push_back(hit);
    }
    if (r.recordedWeeks == 0) {  // 記録がすべて窓の外
      r.columns.clear();
      bySlot.clear();
      r.truncatedWeeks = false;
    } else {
      MtaColumn now;
      now.week = base;
      now.recorded = true;
      now.now = true;
      r.columns.push_back(now);
      bySlot.push_back(nullptr);
    }
  }
  const std::size_t nc = r.columns.size();

  // ---- 系列: いまのマイルストーン（深さ優先）→ 記録にだけある（削除された）----
  std::unordered_map<std::string, std::size_t> at;
  for (int i : T.dfs) {
    if (i < 0 || static_cast<std::size_t>(i) >= n) continue;
    const std::size_t k = static_cast<std::size_t>(i);
    if (!T.kids[k].empty() || T.kind[k] != Kind::Ms) continue;
    if (r.series.size() >= kMtaMaxSeries) break;
    if (!at.emplace(T.ids[k], r.series.size()).second) continue;
    MtaSeries s;
    s.id = T.ids[k];
    s.did = T.did[k];
    s.name = T.name[k];
    s.index = i;
    s.achievedNow = rows[k].done;
    s.deadline = T.dl[k];
    if (baseline && baseline->entries.size() == n) s.plan = baseline->entries[k].planFinish;
    r.series.push_back(std::move(s));
  }
  for (const WeeklySnapshot* snap : bySlot) {
    if (!snap) continue;
    for (const WeeklySnapItem& it : snap->milestones) {
      if (r.series.size() >= kMtaMaxSeries) break;
      if (!at.emplace(it.id, r.series.size()).second) continue;
      MtaSeries s;
      s.id = it.id;
      s.removed = true;
      r.series.push_back(std::move(s));
    }
  }

  // ---- 点 ----
  // 列ごとの Issue Id → 項目（マイルストーンが多くても、描画のたびの集計が重くならないように）
  std::vector<std::unordered_map<std::string, const WeeklySnapItem*>> byId(nc);
  for (std::size_t c = 0; c < nc; ++c) {
    if (!bySlot[c]) continue;
    byId[c].reserve(bySlot[c]->milestones.size());
    for (const WeeklySnapItem& it : bySlot[c]->milestones) byId[c].emplace(it.id, &it);
  }
  for (MtaSeries& s : r.series) {
    if (nc == 0) break;
    s.points.assign(nc, MtaPoint{});
    for (std::size_t c = 0; c < nc; ++c) {
      MtaPoint& p = s.points[c];
      if (r.columns[c].now) {
        if (s.index < 0) continue;
        const std::size_t k = static_cast<std::size_t>(s.index);
        const ScheduleRow& row = rows[k];
        p.has = true;
        p.achieved = row.done;
        p.date = row.done && issues[k].resolved ? *issues[k].resolved : row.asapFinish;
        continue;
      }
      if (!bySlot[c]) continue;
      const auto found = byId[c].find(s.id);
      if (found == byId[c].end() || !found->second->finish) continue;
      const WeeklySnapItem* it = found->second;
      ++s.recorded;
      p.has = true;
      p.achieved = it->st == 'd';
      p.date = *it->finish;
    }
    // 前の点からの動き。達成のあとは線を止める（達成が続くあいだは点にしない）
    int prev = -1;
    for (std::size_t c = 0; c < nc; ++c) {
      MtaPoint& p = s.points[c];
      if (!p.has) continue;
      if (prev >= 0 && s.points[static_cast<std::size_t>(prev)].achieved && p.achieved) {
        p.has = false;
        p.ended = true;
        continue;
      }
      if (prev >= 0) {
        const std::size_t pv = static_cast<std::size_t>(prev);
        p.hasMove = true;
        p.move = baselineWorkdayDiff(cal, s.points[pv].date, p.date);
        const Day from = weeklyMondayOf(r.columns[pv].week);
        const Day to = r.columns[c].now ? baseMon : r.columns[c].week;
        p.sinceWeeks = static_cast<int>((static_cast<long long>(to) - from) / 7);
        p.late = p.move >= r.lateDays;
      }
      prev = static_cast<int>(c);
    }
    // 遅れの警告: 「いま」と、基準日の週より前で最新の記録
    if (s.index < 0 || s.achievedNow || !s.points.back().has) continue;
    int ref = -1;
    for (std::size_t c = 0; c + 1 < nc; ++c)
      if (r.columns[c].recorded && r.columns[c].week < baseMon && s.points[c].has) ref = static_cast<int>(c);
    if (ref < 0) continue;
    const std::size_t rf = static_cast<std::size_t>(ref);
    const int move = baselineWorkdayDiff(cal, s.points[rf].date, s.points.back().date);
    if (move < r.lateDays) continue;
    s.warn = true;
    s.warnMove = move;
    s.warnFromWeek = r.columns[rf].week;
    s.warnFrom = s.points[rf].date;
    s.warnTo = s.points.back().date;
    s.warnWeeks = static_cast<int>((static_cast<long long>(baseMon) - s.warnFromWeek) / 7);
    ++r.warnings;
  }
  return r;
}

// ---------------------------------------------------------------- 文言
std::string mtaPendingText(int recordedWeeks) { return "記録が溜まるとここに出ます（いま " + std::to_string(recordedWeeks) + " 週ぶん）"; }

std::string mtaColumnText(const MtaColumn& c) {
  if (c.now) return "いま（" + fmtShort(c.week) + "）";
  return fmtShort(c.week) + " の週";
}

std::string mtaSeriesName(const MtaSeries& s) {
  if (s.removed) return "(削除された) " + s.id;
  if (s.did.empty()) return s.name;
  return s.name.empty() ? s.did : s.did + " " + s.name;
}

std::string mtaDateText(const MtaPoint& p) {
  if (p.ended) return "達成済み";
  if (!p.has) return "-";
  return fmtShort(p.date) + (p.achieved ? "（達成）" : "");
}

std::string mtaMoveText(const MtaPoint& p) {
  if (!p.has || !p.hasMove) return "-";
  std::string t;
  if (p.move == 0) t = "変わらず";
  else if (p.move > 0) t = "+" + std::to_string(p.move) + " 営業日（遅れ）";
  else t = "-" + std::to_string(-static_cast<long long>(p.move)) + " 営業日（前倒し）";
  if (p.late) t = "! " + t;
  if (p.sinceWeeks == 0) t += "・週の頭から";
  else if (p.sinceWeeks >= 2) t += "・" + std::to_string(p.sinceWeeks) + " 週前の記録から";
  return t;
}

std::string mtaWarnTail(const MtaSeries& s) {
  std::string t = "完了の見込みが " + fmtShort(s.warnFromWeek) + " の週の記録";
  if (s.warnWeeks >= 2) t += "（" + std::to_string(s.warnWeeks) + " 週前）";
  t += "から " + std::to_string(s.warnMove) + " 営業日遅れました（" + fmtShort(s.warnFrom) + " → " + fmtShort(s.warnTo) + "）";
  return t;
}

std::string mtaWarnText(const MtaSeries& s) { return "! " + mtaSeriesName(s) + ": " + mtaWarnTail(s); }

std::string mtaRefLineText(const MtaSeries& s) {
  if (s.deadline && s.plan && *s.deadline == *s.plan) return "期限・計画 " + fmtShort(*s.deadline);
  std::string t;
  if (s.deadline) t = "期限 " + fmtShort(*s.deadline);
  if (s.plan) t += std::string(t.empty() ? "" : "・") + "計画 " + fmtShort(*s.plan);
  return t;
}

std::string mtaPointText(const MtaSeries& s, const MtaColumn& c, std::size_t col) {
  if (col >= s.points.size()) return {};
  const MtaPoint& p = s.points[col];
  std::string t = mtaSeriesName(s) + "\n" + mtaColumnText(c) + "\n";
  if (p.ended) return t + "達成済み";
  if (!p.has) return t + "記録なし";
  t += "見込み " + mtaDateText(p);
  if (p.hasMove) t += "\n前の点から " + mtaMoveText(p);
  return t;
}

std::string mtaLegendText(const MtaSeries& s) {
  std::string t = mtaSeriesName(s);
  std::string extra = mtaRefLineText(s);
  if (s.achievedNow) extra += std::string(extra.empty() ? "" : "・") + "達成済み";
  if (!extra.empty()) t += "（" + extra + "）";
  return t;
}

// ---------------------------------------------------------------- 図の共通部分
MtaStyle mtaStyleOf(std::size_t k) {
  // 印と線種の組み合わせが 6 つめまで全部違う（色が見分けられなくても区別できる）
  static const struct {
    MtaMarker marker;
    int dash, gap;
    const char* name;
  } kTable[kMtaStyles] = {
      {MtaMarker::Circle, 0, 0, "実線・丸"},          {MtaMarker::Square, 8, 4, "破線・四角"},      {MtaMarker::Triangle, 2, 3, "点線・三角"},
      {MtaMarker::Diamond, 14, 4, "長い破線・ひし形"}, {MtaMarker::Cross, 4, 2, "短い破線・バツ"}, {MtaMarker::Plus, 1, 4, "細かい点線・十字"}};
  const auto& t = kTable[k % static_cast<std::size_t>(kMtaStyles)];
  MtaStyle s;
  s.color = static_cast<int>(k % 8);
  s.marker = t.marker;
  s.dash = t.dash;
  s.gap = t.gap;
  s.name = t.name;
  return s;
}

MtaYAxis mtaYAxis(const MtaReport& r, const std::vector<std::size_t>& shown) {
  MtaYAxis ax;
  bool any = false;
  Day lo = 0, hi = 0;
  const auto take = [&](Day d) {
    if (!any) lo = hi = d;
    lo = std::min(lo, d);
    hi = std::max(hi, d);
    any = true;
  };
  bool pointed = false;  // 点が 1 つでもある（期限・計画だけでは図にならない）
  for (std::size_t i : shown) {
    if (i >= r.series.size()) continue;
    const MtaSeries& s = r.series[i];
    bool has = false;
    for (const MtaPoint& p : s.points)
      if (p.has) {
        take(p.date);
        has = true;
      }
    if (!has) continue;
    pointed = true;
    if (s.deadline) take(*s.deadline);
    if (s.plan) take(*s.plan);
  }
  if (!any || !pointed) return ax;
  long long span = static_cast<long long>(hi) - lo;
  if (span < 7) {  // 幅を持たせる（点が 1 日に集まっていても、線が見えるように）
    const long long grow = 7 - span;
    lo -= static_cast<Day>(grow / 2);
    hi += static_cast<Day>(grow - grow / 2);
    span = 7;
  }
  const Day pad = static_cast<Day>(std::max<long long>(2, (span + 7) / 8));
  ax.lo = lo - pad;
  ax.hi = hi + pad;
  static const int kSteps[] = {1, 2, 3, 5, 7, 10, 14, 21, 28, 42, 56, 91, 182, 364, 728};
  const auto countFor = [&](int step) {
    const long long a = (static_cast<long long>(ax.lo) + step - 1) / step;  // 日は正（エポックより後）なので切り上げの割り算で足りる
    const long long b = static_cast<long long>(ax.hi) / step;
    return b - a + 1;
  };
  int step = kSteps[std::size(kSteps) - 1];
  for (int s : kSteps)
    if (countFor(s) <= 5) {
      step = s;
      break;
    }
  for (long long d = ((static_cast<long long>(ax.lo) + step - 1) / step) * step; d <= ax.hi; d += step) ax.ticks.push_back(static_cast<Day>(d));
  ax.ok = true;
  return ax;
}

std::vector<std::size_t> mtaDefaultShown(const MtaReport& r) {
  std::vector<std::size_t> out;
  const auto add = [&](auto pred) {
    for (std::size_t i = 0; i < r.series.size() && out.size() < static_cast<std::size_t>(kMtaStyles); ++i)
      if (pred(r.series[i]) && std::find(out.begin(), out.end(), i) == out.end()) out.push_back(i);
  };
  add([](const MtaSeries& s) { return s.warn; });
  add([](const MtaSeries& s) { return s.recorded > 0; });
  add([](const MtaSeries&) { return true; });
  return out;
}

}  // namespace roadmap
