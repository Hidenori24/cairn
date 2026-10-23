// EVM（出来高管理。Issue #65）の集計。定義は evm.hpp と SPEC 10.7.5
#include "roadmap/evm.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <optional>
#include <unordered_map>

#include "roadmap/text.hpp"
#include "workdays_internal.hpp"

namespace roadmap {

namespace {

using detail::WorkDays;
using Real = long double;

constexpr double kEps = 1e-9;

Day weekStart(Day d) { return d - (dow(d) + 6) % 7; }  // 月曜（統計・推移と同じ切り方）

// 計画量（週）。見積りがあればそれ、なければ期間。どちらもない・0 以下・極端な値は nullopt
struct Amount {
  double v = 0;
  bool byEstimate = false;
};
std::optional<Amount> amountOf(const Issue& s) {
  const auto ok = [](const std::optional<double>& x) { return x && std::isfinite(*x) && *x > 0 && *x <= kEvmMaxAmount; };
  if (ok(s.estW)) return Amount{*s.estW, true};
  if (ok(s.durW)) return Amount{*s.durW, false};
  return std::nullopt;
}

// 計測の対象 1 件
struct Item {
  double a = 0;      // 計画量
  Day s = 0, f = 0;  // 計画の開始日・完了日（営業日に寄せたあと）
  long long ws = 0;  // 起点から数えて、開始日の前までの営業日の数
  long long we = 0;  // 起点から数えて、完了日までの営業日の数（we > ws）
  double r = 0;      // 1 営業日あたりの計画量
};

// 計画価値 PV を、営業日の通し番号 w の関数として持つ。1 件の PV は w が ws〜we の間で直線に増えて a になる。
// 全件の合計を、開始・完了の並べ替えと累積和で O(log n) で引く（件数 × 週の数の掛け算にしない）
class PvCurve {
 public:
  void build(const std::vector<Item>& items) {
    const std::size_t n = items.size();
    std::vector<std::size_t> byS(n), byE(n);
    std::iota(byS.begin(), byS.end(), std::size_t{0});
    std::iota(byE.begin(), byE.end(), std::size_t{0});
    std::sort(byS.begin(), byS.end(), [&](std::size_t x, std::size_t y) { return items[x].ws < items[y].ws; });
    std::sort(byE.begin(), byE.end(), [&](std::size_t x, std::size_t y) { return items[x].we < items[y].we; });
    sKey_.resize(n);
    sR_.assign(n + 1, 0);
    sRS_.assign(n + 1, 0);
    for (std::size_t k = 0; k < n; ++k) {
      const Item& it = items[byS[k]];
      sKey_[k] = it.ws;
      sR_[k + 1] = sR_[k] + static_cast<Real>(it.r);
      sRS_[k + 1] = sRS_[k] + static_cast<Real>(it.r) * static_cast<Real>(it.ws);
    }
    eKey_.resize(n);
    eA_.assign(n + 1, 0);
    eR_.assign(n + 1, 0);
    eRS_.assign(n + 1, 0);
    for (std::size_t k = 0; k < n; ++k) {
      const Item& it = items[byE[k]];
      eKey_[k] = it.we;
      eA_[k + 1] = eA_[k] + static_cast<Real>(it.a);
      eR_[k + 1] = eR_[k] + static_cast<Real>(it.r);
      eRS_[k + 1] = eRS_[k] + static_cast<Real>(it.r) * static_cast<Real>(it.ws);
    }
    bac_ = eA_[n];
  }
  double bac() const { return static_cast<double>(bac_); }
  // 営業日の通し番号 w（その日の終わり）までの計画価値
  double at(long long w) const {
    if (w <= 0) return 0;
    const std::size_t ks = static_cast<std::size_t>(std::lower_bound(sKey_.begin(), sKey_.end(), w) - sKey_.begin());  // ws < w
    const std::size_t ke = static_cast<std::size_t>(std::upper_bound(eKey_.begin(), eKey_.end(), w) - eKey_.begin());  // we <= w
    const Real v = eA_[ke] + (sR_[ks] - eR_[ke]) * static_cast<Real>(w) - (sRS_[ks] - eRS_[ke]);
    if (v <= 0) return 0;
    return std::min(static_cast<double>(v), bac());
  }

 private:
  std::vector<long long> sKey_, eKey_;
  std::vector<Real> sR_, sRS_, eA_, eR_, eRS_;
  Real bac_ = 0;
};

// 出来高 EV: (日, 量) の累積。基準日までを引く
class EvCurve {
 public:
  void add(Day d, double v) { ev_.emplace_back(d, v); }
  void build() {
    std::sort(ev_.begin(), ev_.end(), [](const auto& x, const auto& y) { return x.first < y.first; });
    sum_.assign(ev_.size() + 1, 0);
    for (std::size_t k = 0; k < ev_.size(); ++k) sum_[k + 1] = sum_[k] + static_cast<Real>(ev_[k].second);
  }
  double upTo(Day d) const {
    const std::size_t k = static_cast<std::size_t>(
        std::upper_bound(ev_.begin(), ev_.end(), d, [](Day x, const std::pair<Day, double>& e) { return x < e.first; }) - ev_.begin());
    return static_cast<double>(sum_[k]);
  }

 private:
  std::vector<std::pair<Day, double>> ev_;
  std::vector<Real> sum_;
};

std::string numText(double v) {
  const long long t = std::llround(v * 10.0);
  const bool neg = t < 0;
  const long long a = neg ? -t : t;
  std::string s = std::to_string(a / 10) + "." + std::to_string(a % 10);
  if (s.size() > 2 && s.compare(s.size() - 2, 2, ".0") == 0) s.resize(s.size() - 2);
  return (neg && a != 0 ? "-" : "") + s;
}

std::string signedNum(double v) {
  const long long t = std::llround(v * 10.0);
  if (t == 0) return "±0";
  return (t > 0 ? "+" : "") + numText(v);
}

std::string dateText(const std::string& at) {  // "2026-10-19T09:30" → "2026/10/19 09:30"
  if (at.size() != 16) return {};
  return at.substr(0, 4) + "/" + at.substr(5, 2) + "/" + at.substr(8, 2) + " " + at.substr(11, 5);
}

}  // namespace

const Baseline* evmPickBaseline(const BaselineSet& set, const std::string& wantedId) {
  if (!wantedId.empty())
    if (const Baseline* b = baselineFind(set, wantedId)) return b;
  if (const Baseline* b = baselineShown(set)) return b;
  return set.list.empty() ? nullptr : &set.list.back();
}

Result<EvmReport> computeEvm(const Tree& T, const std::vector<Issue>& issues, const std::vector<ScheduleRow>& R, const Calendar& cal, Day base,
                             const Baseline* baseline, const EvmSettings& settings, const TrendFilter& filter) {
  const std::size_t n = T.n > 0 ? static_cast<std::size_t>(T.n) : 0;
  if (issues.size() != n || R.size() != n) return Error{"課題と計算結果の数が合いません。"};
  if (filter.root != TrendFilter::kAllRoots && filter.root != TrendFilter::kStandalone) {
    if (filter.root < 0 || filter.root >= T.n || T.parentOf[static_cast<std::size_t>(filter.root)] >= 0)
      return Error{"絞り込みのルート課題が見つかりません。"};
  }

  EvmReport out;
  out.settings = settings;
  out.base = base;
  out.filtered = filter.active();
  if (!baseline) {
    out.reason = EvmReason::NoBaseline;
    return out;
  }
  out.baselineId = baseline->id;
  out.baselineName = baseline->name;
  out.baselineAt = baseline->at;
  out.baselineTruncated = baseline->truncated;
  if (baseline->at.size() >= 10) {
    if (const OptDay d = parseIso(baseline->at.substr(0, 10))) {
      out.hasFrom = true;
      out.from = *d;
    }
  }

  const WorkDays wd(cal);
  const std::string wantName = text::trim(filter.name);

  std::unordered_map<std::string, std::size_t> byId;
  byId.reserve(baseline->items.size() * 2);
  for (std::size_t q = 0; q < baseline->items.size(); ++q) byId.emplace(baseline->items[q].id, q);
  std::vector<char> used(baseline->items.size(), 0);

  struct Pending {
    Amount amt;
    Day s, f;
    std::size_t idx;
  };
  std::vector<Pending> scope;

  for (int id : T.leafOrder) {
    const std::size_t i = static_cast<std::size_t>(id);
    if (T.kind[i] == Kind::Ms) {
      // マイルストーンは数えないが、ベースラインの対応づけには使う（「消えた」と数え違えない）
      if (const auto it = byId.find(T.ids[i]); it != byId.end()) used[it->second] = 1;
      continue;
    }
    const auto found = byId.find(T.ids[i]);
    const BaselineItem* bi = nullptr;
    if (found != byId.end() && !baseline->items[found->second].parent) {
      bi = &baseline->items[found->second];
      used[found->second] = 1;
    }
    const Issue& s = issues[i];
    const std::string who = text::trim(s.assignee);
    if (filter.who == TrendFilter::Who::Unassigned && !who.empty()) continue;
    if (filter.who == TrendFilter::Who::Name && who != wantName) continue;
    if (filter.root == TrendFilter::kStandalone) {
      if (!T.kids[static_cast<std::size_t>(T.rootOf[i])].empty()) continue;
    } else if (filter.root >= 0 && T.rootOf[i] != filter.root) {
      continue;
    }

    if (!bi) {
      ++out.added;
      continue;
    }
    if (bi->st == 'd') {
      ++out.doneAtBaseline;
      continue;
    }
    if (!bi->finish) {
      ++out.noPlanDate;
      continue;
    }
    const std::optional<Amount> amt = amountOf(s);
    if (!amt) {
      ++out.noAmount;
      continue;
    }
    if (R[i].done && !s.resolved) {
      ++out.noResolved;
      continue;
    }
    Day ps = bi->start ? *bi->start : *bi->finish, pf = *bi->finish;
    if (ps > pf) ps = pf;
    if (wd.count(ps, pf) == 0) {  // 営業日がない区間（休業日・土日だけ）は、次の営業日の 1 日に置く
      Day d = pf;
      int guard = 0;
      while (!wd.isWork(d) && guard < 4000) {
        ++d;
        ++guard;
      }
      if (!wd.isWork(d)) {
        ++out.noPlanDate;
        continue;
      }
      ps = pf = d;
    }
    scope.push_back({*amt, ps, pf, i});
  }

  if (!out.filtered) {
    for (std::size_t q = 0; q < baseline->items.size(); ++q)
      if (!used[q] && !baseline->items[q].parent && baseline->items[q].st != 'd') ++out.gone;
  }

  if (scope.empty()) {
    out.reason = EvmReason::NoItems;
    return out;
  }
  if (out.hasFrom && base < out.from) {
    out.reason = EvmReason::BeforeBaseline;
    return out;
  }
  out.reason = EvmReason::None;

  // ---- 起点（計画の最初の日の前日）から数えた営業日の通し番号 ----
  Day minStart = scope[0].s, maxFinish = scope[0].f;
  for (const Pending& p : scope) {
    minStart = std::min(minStart, p.s);
    maxFinish = std::max(maxFinish, p.f);
  }
  const Day origin = minStart - 1;
  const auto W = [&](Day d) -> long long { return d <= origin ? 0 : static_cast<long long>(wd.count(origin + 1, d)); };

  std::vector<Item> items;
  items.reserve(scope.size());
  EvCurve evc;
  for (const Pending& p : scope) {
    Item it;
    it.a = p.amt.v;
    it.s = p.s;
    it.f = p.f;
    it.ws = W(p.s - 1);
    it.we = W(p.f);
    it.r = it.a / static_cast<double>(it.we - it.ws);
    items.push_back(it);

    const Issue& s = issues[p.idx];
    const ScheduleRow& row = R[p.idx];
    ++out.items;
    (p.amt.byEstimate ? out.byEstimate : out.byDuration) += 1;
    const bool half = settings.progress == EvmProgress::FiftyFifty;
    if (row.done) {
      ++out.doneCount;
      const Day res = *s.resolved;
      if (half) {
        const Day startDay = s.start ? std::min(*s.start, res) : res;
        evc.add(startDay, p.amt.v * 0.5);
        evc.add(res, p.amt.v * 0.5);
      } else {
        evc.add(res, p.amt.v);
      }
    } else if (row.prog) {
      ++out.progCount;
      if (half) evc.add(s.start ? std::min(*s.start, base) : base, p.amt.v * 0.5);
    }
  }
  evc.build();
  PvCurve pvc;
  pvc.build(items);
  out.planStart = minStart;
  out.planEnd = maxFinish;
  long long wMax = 0;
  for (const Item& it : items) wMax = std::max(wMax, it.we);

  // ---- 基準日の時点 ----
  out.bac = pvc.bac();
  const long long wNow = W(base);
  out.pv = pvc.at(wNow);
  out.ev = std::min(evc.upTo(base), out.bac);
  out.sv = out.ev - out.pv;
  const double tol = kEps * std::max(1.0, out.bac);
  if (out.pv > tol) {
    out.hasSpi = true;
    out.spi = out.ev / out.pv;
    out.warn = out.spi < settings.spiWarn;
  }

  // アーンドスケジュール: EV に等しい PV に初めて達する営業日（PV は営業日の間で直線なので、整数の間は線形補間で正確）
  {
    double es = 0;
    if (out.ev > tol) {
      long long lo = 1, hi = wMax;
      while (lo < hi) {
        const long long mid = lo + (hi - lo) / 2;
        if (pvc.at(mid) >= out.ev - tol) hi = mid;
        else lo = mid + 1;
      }
      const double prev = pvc.at(lo - 1), cur = pvc.at(lo);
      es = static_cast<double>(lo - 1);
      if (cur - prev > 1e-12) es += std::clamp((out.ev - prev) / (cur - prev), 0.0, 1.0);
    }
    out.hasEs = true;
    out.es = es;
    out.at = static_cast<double>(wNow);
    out.esDelta = out.at - out.es;
  }

  // 営業日の通し番号 w に着く最初の日
  const auto dateOfWorkday = [&](long long w) -> Day {
    if (w <= 0) return origin;
    long long hi = static_cast<long long>(origin) + 1 + w * 2 + 400;
    while (W(static_cast<Day>(std::min<long long>(hi, 2'000'000'000))) < w && hi < 2'000'000'000) hi = hi * 2;
    long long lo = static_cast<long long>(origin) + 1;
    hi = std::min<long long>(hi, 2'000'000'000);
    while (lo < hi) {
      const long long mid = lo + (hi - lo) / 2;
      if (W(static_cast<Day>(mid)) >= w) hi = mid;
      else lo = mid + 1;
    }
    return static_cast<Day>(lo);
  };

  // ---- 完了予測: ES ÷ 経過 のペースが続くとして、計画の期間（wMax）をそのペースで割る ----
  {
    EvmForecast& fc = out.forecast;
    if (out.ev >= out.bac - tol) {
      fc.reason = EvmForecastReason::Finished;
    } else if (out.es <= kEps || out.at <= 0) {
      fc.reason = EvmForecastReason::NoProgress;
    } else {
      const double wf = static_cast<double>(wMax) * out.at / out.es;
      if (wf - out.at > static_cast<double>(kEvmMaxForecastWeeks) * 5.0) {
        fc.reason = EvmForecastReason::TooFar;
      } else {
        const long long w = std::max(static_cast<long long>(std::ceil(wf - 1e-9)), wNow);
        fc.ok = true;
        fc.reason = EvmForecastReason::None;
        fc.finish = std::max(dateOfWorkday(w), base);
        fc.finishWeek = weekStart(fc.finish);
      }
    }
  }

  // ---- S カーブ: 計測を始める日（保存日。分からなければ計画の最初の日）から、週の終わりごと。基準日の点を必ず入れる ----
  {
    Day first = out.hasFrom ? out.from : minStart;
    if (base < first) first = base;
    const Day firstAllowed = weekStart(base) - 7 * (kEvmMaxWeeks - 1);
    if (first < firstAllowed) {
      first = firstAllowed;
      out.curveClipped = true;
    }
    Day last = std::max(out.planEnd, base);
    if (out.forecast.ok) last = std::max(last, out.forecast.finish);
    const Day lastAllowed = weekStart(first) + 7 * kEvmMaxWeeks - 1;
    if (last > lastAllowed) {
      last = lastAllowed;
      out.curveClipped = true;
    }
    last = std::max(last, base);
    std::vector<Day> dates;
    dates.push_back(first);
    for (Day d = weekStart(first) + 6;; d += 7) {
      if (d > first) dates.push_back(d);
      if (d >= last) break;
    }
    // 基準日の点を入れる
    const auto it = std::lower_bound(dates.begin(), dates.end(), base);
    if (it == dates.end()) {
      dates.push_back(base);
    } else if (*it != base) {
      dates.insert(it, base);
    }
    for (Day d : dates) {
      EvmPoint p;
      p.date = d;
      p.pv = pvc.at(W(d));
      if (d <= base) {
        p.hasEv = true;
        p.ev = std::min(evc.upTo(d), out.bac);
      }
      if (d == base) out.nowIndex = static_cast<int>(out.curve.size());
      out.curve.push_back(p);
    }
  }
  return out;
}

// ---------------------------------------------------------------- 文言
std::string evmAmountText(double weeks) { return numText(weeks) + " 週"; }

std::string evmSignedAmountText(double weeks) { return signedNum(weeks) + " 週"; }

std::string evmSpiText(const EvmReport& r) {
  if (!r.ok() || !r.hasSpi) return "-";
  const long long t = std::llround(r.spi * 100.0);
  return std::to_string(t / 100) + "." + (t % 100 < 10 ? "0" : "") + std::to_string(t % 100);
}

std::string evmEsDeltaText(const EvmReport& r) {
  if (!r.ok() || !r.hasEs) return "-";
  return signedNum(r.esDelta);
}

std::string evmEsSentence(const EvmReport& r) {
  if (!r.ok() || !r.hasEs) return {};
  const long long t = std::llround(r.esDelta * 10.0);
  if (t == 0) return "アーンドスケジュールでは、計画どおりです。";
  if (t > 0) return "アーンドスケジュールでは、計画より " + numText(r.esDelta) + " 営業日 遅れています。";
  return "アーンドスケジュールでは、計画より " + numText(-r.esDelta) + " 営業日 進んでいます。";
}

std::string evmWarnMark(const EvmReport& r) { return r.ok() && r.warn ? "!" : ""; }

std::string evmReasonText(const EvmReport& r) {
  switch (r.reason) {
    case EvmReason::None:
      return {};
    case EvmReason::NoBaseline:
      return "! ベースラインを保存してください。EVM は、保存した計画（ベースライン）の日程と、いまの完了を比べて出します。"
             "ベースラインを保存した日から計測します。";
    case EvmReason::BeforeBaseline:
      return "! 基準日（" + fmtShort(r.base) + "）が、ベースライン「" + r.baselineName + "」を保存した日（" + fmtShort(r.from) +
             "）より前なので、指標は出せません。ベースラインを保存した日から計測します。";
    case EvmReason::NoItems:
      return "! 計測の対象になる課題がありません。ベースラインを保存した時点で未完了だった作業のうち、見積りか期間があり、"
             "完了なら完了日（Resolved）のあるものが対象です。";
  }
  return {};
}

std::string evmAmountSourceText(const EvmReport& r) {
  return "計画量: 見積り " + std::to_string(r.byEstimate) + " 件・期間 " + std::to_string(r.byDuration) + " 件";
}

std::vector<std::string> evmAssumptions(const EvmReport& r) {
  std::vector<std::string> out;
  if (r.baselineId.empty() && r.baselineName.empty()) {
    out.push_back("計画: ベースラインの日程（まだ保存されていません）");
  } else {
    const std::string at = dateText(r.baselineAt);
    out.push_back("計画: ベースライン「" + r.baselineName + "」" + (at.empty() ? std::string() : "（保存 " + at + "）") +
                  " の開始〜完了の営業日へ、計画量を均等に配ります。");
  }
  if (r.hasFrom) out.push_back("計測: ベースラインを保存した日（" + fmtShort(r.from) + "）から。それより前の PV は出しません。");
  else out.push_back("計測: ベースラインの保存日時が分からないので、計画の最初の日から。");
  out.push_back("計画量: 見積りがあれば見積り、なければ期間（週）。ベースラインには保存されていないので、いまの値です。");
  out.push_back(r.settings.progress == EvmProgress::FiftyFifty
                    ? "出来高 EV: 50/50（着手で 50%、完了で 100%。着手は開始日、なければ基準日から。完了日は Resolved）。"
                    : "出来高 EV: 0/100（完了するまで 0%、完了で 100%。進行中は 0%。完了日は Resolved）。");
  out.push_back("対象: ベースラインを保存した時点で未完了だった末端の作業（マイルストーン・親のまとめを除く）。");
  out.push_back("基準日（" + fmtShort(r.base) + "）の終わりの時点。営業日は休業日・土日を除きます。SPI が " +
                [&] {
                  const long long t = std::llround(r.settings.spiWarn * 100.0);
                  return std::to_string(t / 100) + "." + (t % 100 < 10 ? "0" : "") + std::to_string(t % 100);
                }() +
                " 未満なら「!」を付けます。");
  out.push_back("アーンドスケジュール: EV に等しい PV に計画が達する時点（営業日）と、実際の経過の差。SPI の弱点（終盤で 1 に近づく）を補います。");
  return out;
}

std::vector<std::string> evmNotes(const EvmReport& r) {
  std::vector<std::string> out;
  if (r.baselineTruncated) out.push_back("ベースラインは課題が多かったため、一部の課題だけです。");
  if (r.added > 0) out.push_back("ベースラインを保存したあとに増えた " + std::to_string(r.added) + " 件は、計画がないので数えていません。");
  if (r.doneAtBaseline > 0)
    out.push_back("ベースラインを保存した時点で完了していた " + std::to_string(r.doneAtBaseline) + " 件は、日程が計画を表さないので数えていません。");
  if (r.noPlanDate > 0) out.push_back("ベースラインに完了日のない " + std::to_string(r.noPlanDate) + " 件は数えていません。");
  if (r.noAmount > 0) out.push_back("見積りも期間もない（か、極端な値の）" + std::to_string(r.noAmount) + " 件は数えていません。");
  if (r.noResolved > 0) out.push_back("完了日（Resolved）のない完了済みの " + std::to_string(r.noResolved) + " 件は数えていません。");
  if (r.gone > 0)
    out.push_back("ベースラインにあって、いまは作業として見つからない " + std::to_string(r.gone) + " 件（削除・親に変わった課題）は数えていません。");
  if (r.curveClipped) out.push_back("S カーブは " + std::to_string(kEvmMaxWeeks) + " 週までに切っています。");
  return out;
}

std::string evmForecastText(const EvmReport& r) {
  if (!r.ok()) return {};
  const EvmForecast& f = r.forecast;
  switch (f.reason) {
    case EvmForecastReason::None:
      return "いまの進み具合（ES ÷ 経過）が続くと、すべて完了するのは " + fmtMd(f.finishWeek) + " の週の見込みです（点線。計画の終わりは " +
             fmtMd(r.planEnd) + "）。";
    case EvmForecastReason::Finished:
      return "計測の対象はすべて完了しているので、完了の予測は要りません。";
    case EvmForecastReason::NoProgress:
      return "! まだ出来高（完了）がないので、完了の予測は出していません。";
    case EvmForecastReason::TooFar:
      return "! いまの進み具合だと " + std::to_string(kEvmMaxForecastWeeks) + " 週より先になるので、完了の予測は出していません。";
  }
  return {};
}

}  // namespace roadmap
