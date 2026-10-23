// EVM（出来高管理。Issue #65）: BAC・PV・EV・SV・SPI・アーンドスケジュール・S カーブ・完了予測
#include <doctest/doctest.h>

#include <chrono>
#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "roadmap/baseline.hpp"
#include "roadmap/clock.hpp"
#include "roadmap/commands.hpp"
#include "roadmap/evm.hpp"
#include "roadmap/gen.hpp"
#include "roadmap/project_io.hpp"
#include "roadmap/schedule.hpp"

using namespace roadmap;
using doctest::Approx;

namespace {

Day D(const char* iso) { return *parseIso(iso); }

struct Spec {
  std::string id, parent, asg, status;
  std::optional<double> dur, est;
  std::string resolved, start;
  explicit Spec(std::string i, std::string p = "", std::string a = "", std::string st = "To Do", std::optional<double> d = 1.0)
      : id(std::move(i)), parent(std::move(p)), asg(std::move(a)), status(std::move(st)), dur(d) {}
};

Issue mk(const Spec& s) {
  Issue o;
  o.id = s.id;
  o.key = "P-" + s.id;
  o.summary = "課題" + s.id;
  o.parent = s.parent;
  o.assignee = s.asg;
  o.status = s.status;
  if (!s.resolved.empty()) o.resolved = parseIso(s.resolved);
  if (!s.start.empty()) o.start = parseIso(s.start);
  o.durW = s.dur;
  o.estW = s.est;
  return o;
}

BaselineItem bi(const std::string& id, char st, const char* s, const char* f) {
  BaselineItem it;
  it.id = id;
  it.st = st;
  if (s && *s) it.start = D(s);
  if (f && *f) it.finish = D(f);
  return it;
}

struct Proj {
  std::vector<Issue> issues;
  ProjectMeta meta;
  Computed comp;
  Calendar cal;  // 休業日なし
  Day base = D("2026-10-28");
  Baseline bl;

  static Proj make(const std::vector<Spec>& specs, const char* baseIso, const Calendar& cal = Calendar()) {
    Proj p;
    p.cal = cal;
    p.base = D(baseIso);
    p.meta.hasDeadlineCol = true;
    for (const Spec& s : specs) p.issues.push_back(mk(s));
    p.rebuild();
    p.bl.id = "b1";
    p.bl.name = "キックオフ";
    p.bl.at = "2026-10-19T09:00";
    return p;
  }
  void rebuild() {
    ScheduleOptions opt;
    opt.base = base;
    auto c = computeAll(issues, meta, cal, opt);
    REQUIRE(c.ok());
    comp = std::move(c).value();
    comp.issues = issues;
  }
  Result<EvmReport> run(const EvmSettings& s = {}, const TrendFilter& f = {}) const {
    return computeEvm(comp.T, issues, comp.R, cal, base, &bl, s, f);
  }
  EvmReport ok(const EvmSettings& s = {}, const TrendFilter& f = {}) const {
    auto r = run(s, f);
    REQUIRE(r.ok());
    return std::move(r).value();
  }
};

// 3 件（各 1 週 = 5 営業日）が 1 週ずつ連なる計画。A は完了、B は進行中、C は未着手。基準日 10/28（水）
//   起点 = 10/19。W(10/28) = 8。PV = 1 + 3/5 = 1.6、EV = 1、SPI = 0.625、ES = 5、遅れ = 3 営業日
Proj threeWeeks() {
  Spec a{"1"}, b{"2"}, c{"3"};
  a.status = "Done";
  a.resolved = "2026-10-23";
  b.status = "In Progress";
  Proj p = Proj::make({a, b, c}, "2026-10-28");
  p.bl.items = {bi("1", 't', "2026-10-19", "2026-10-23"), bi("2", 't', "2026-10-26", "2026-10-30"), bi("3", 't', "2026-11-02", "2026-11-06")};
  return p;
}

std::string iso(Day d) { return fmtIso(d); }

}  // namespace

// ---------------------------------------------------------------- ベースラインがない

TEST_CASE("evm: ベースラインがないときは指標を出さず、保存を案内する") {
  const Proj p = threeWeeks();
  auto r = computeEvm(p.comp.T, p.issues, p.comp.R, p.cal, p.base, nullptr, EvmSettings{});
  REQUIRE(r.ok());
  CHECK_FALSE(r->ok());
  CHECK(r->reason == EvmReason::NoBaseline);
  CHECK(r->curve.empty());
  CHECK(r->bac == 0);
  const std::string t = evmReasonText(*r);
  CHECK(t.rfind("! ", 0) == 0);
  CHECK(t.find("ベースラインを保存してください") != std::string::npos);
  CHECK(evmSpiText(*r) == "-");
  CHECK(evmEsDeltaText(*r) == "-");
  CHECK(evmNotes(*r).empty());
}

TEST_CASE("evm: ベースラインを選ぶ（指定 → 表示中 → 一番新しいもの）") {
  BaselineSet set;
  CHECK(evmPickBaseline(set, "") == nullptr);
  Baseline a, b, c;
  a.id = "b1";
  b.id = "b2";
  c.id = "b3";
  set.list = {a, b, c};
  CHECK(evmPickBaseline(set, "")->id == "b3");
  set.shown = "b2";
  CHECK(evmPickBaseline(set, "")->id == "b2");
  CHECK(evmPickBaseline(set, "b1")->id == "b1");
  CHECK(evmPickBaseline(set, "zzz")->id == "b2");  // 見つからない指定は無視する
}

// ---------------------------------------------------------------- 基本の指標

TEST_CASE("evm: BAC・PV・EV・SV・SPI・アーンドスケジュール（0/100）") {
  const Proj p = threeWeeks();
  const EvmReport r = p.ok();
  REQUIRE(r.ok());
  CHECK(r.items == 3);
  CHECK(r.byDuration == 3);
  CHECK(r.byEstimate == 0);
  CHECK(r.doneCount == 1);
  CHECK(r.progCount == 1);
  CHECK(r.bac == Approx(3.0));
  CHECK(r.pv == Approx(1.6));
  CHECK(r.ev == Approx(1.0));
  CHECK(r.sv == Approx(-0.6));
  REQUIRE(r.hasSpi);
  CHECK(r.spi == Approx(0.625));
  CHECK(r.warn);  // しきい値 0.9 未満
  CHECK(evmWarnMark(r) == "!");
  CHECK(evmSpiText(r) == "0.63");
  CHECK(evmAmountText(r.bac) == "3 週");
  CHECK(evmSignedAmountText(r.sv) == "-0.6 週");
  REQUIRE(r.hasEs);
  CHECK(r.es == Approx(5.0));
  CHECK(r.at == Approx(8.0));
  CHECK(r.esDelta == Approx(3.0));
  CHECK(evmEsDeltaText(r) == "+3");
  CHECK(evmEsSentence(r).find("3 営業日 遅れています") != std::string::npos);
  CHECK(r.planStart == D("2026-10-19"));
  CHECK(r.planEnd == D("2026-11-06"));
  CHECK(r.hasFrom);
  CHECK(r.from == D("2026-10-19"));
  CHECK(r.baselineName == "キックオフ");
}

TEST_CASE("evm: SPI がしきい値以上なら「!」を付けない。しきい値は設定で変えられる") {
  const Proj p = threeWeeks();
  EvmSettings s;
  s.spiWarn = 0.6;
  const EvmReport r = p.ok(s);
  CHECK(r.spi == Approx(0.625));
  CHECK_FALSE(r.warn);
  CHECK(evmWarnMark(r).empty());
  s.spiWarn = 0.625;  // ちょうど（未満ではない）
  CHECK_FALSE(p.ok(s).warn);
  s.spiWarn = 0.63;
  CHECK(p.ok(s).warn);
}

TEST_CASE("evm: 計画より進んでいるとき（SV 正・SPI 1 超・ES 負の遅れ = 進み）") {
  // A（10/19〜23）と B（10/26〜30）が完了済み。基準日は 10/26（月）。PV = 1 + 1/5、EV = 2
  Spec a{"1"}, b{"2"}, c{"3"};
  a.status = "Done";
  a.resolved = "2026-10-22";
  b.status = "Done";
  b.resolved = "2026-10-26";
  Proj p = Proj::make({a, b, c}, "2026-10-26");
  p.bl.items = {bi("1", 't', "2026-10-19", "2026-10-23"), bi("2", 't', "2026-10-26", "2026-10-30"), bi("3", 't', "2026-11-02", "2026-11-06")};
  const EvmReport r = p.ok();
  CHECK(r.pv == Approx(1.2));
  CHECK(r.ev == Approx(2.0));
  CHECK(r.sv == Approx(0.8));
  CHECK(r.spi == Approx(2.0 / 1.2));
  CHECK_FALSE(r.warn);
  // EV 2 に PV が達するのは w = 10（10/30）。いまは w = 6 → 4 営業日の進み
  CHECK(r.es == Approx(10.0));
  CHECK(r.at == Approx(6.0));
  CHECK(r.esDelta == Approx(-4.0));
  CHECK(evmEsDeltaText(r) == "-4");
  CHECK(evmEsSentence(r).find("4 営業日 進んでいます") != std::string::npos);
}

TEST_CASE("evm: 計画どおり（SV 0）") {
  Spec a{"1"};
  a.status = "Done";
  a.resolved = "2026-10-23";
  Proj p = Proj::make({a}, "2026-10-23");
  p.bl.items = {bi("1", 't', "2026-10-19", "2026-10-23")};
  const EvmReport r = p.ok();
  CHECK(r.pv == Approx(1.0));
  CHECK(r.ev == Approx(1.0));
  CHECK(r.sv == Approx(0.0));
  CHECK(r.spi == Approx(1.0));
  CHECK(r.esDelta == Approx(0.0));
  CHECK(evmEsDeltaText(r) == "±0");
  CHECK(evmSignedAmountText(r.sv) == "±0 週");
  CHECK(evmEsSentence(r).find("計画どおり") != std::string::npos);
  CHECK(r.forecast.reason == EvmForecastReason::Finished);
}

// ---------------------------------------------------------------- 計画量（見積り／期間）

TEST_CASE("evm: 計画量は見積りがあれば見積り、なければ期間（ベースラインあり／なし × 見積りあり／なし）") {
  Spec a{"1"}, b{"2"}, c{"3"};
  a.est = 2.0;  // 見積りと期間の両方 → 見積り
  a.dur = 1.0;
  b.est = 3.0;  // 見積りだけ
  b.dur = std::nullopt;
  c.est = std::nullopt;  // 期間だけ
  c.dur = 0.5;
  Proj p = Proj::make({a, b, c}, "2026-10-28");
  p.bl.items = {bi("1", 't', "2026-10-19", "2026-10-23"), bi("2", 't', "2026-10-26", "2026-10-30"), bi("3", 't', "2026-11-02", "2026-11-06")};
  const EvmReport r = p.ok();
  CHECK(r.items == 3);
  CHECK(r.byEstimate == 2);
  CHECK(r.byDuration == 1);
  CHECK(r.bac == Approx(5.5));
  CHECK(evmAmountSourceText(r) == "計画量: 見積り 2 件・期間 1 件");
  // 見積りも期間もない課題は数えない
  Spec d{"4"};
  d.dur = std::nullopt;
  Proj q = Proj::make({a, d}, "2026-10-28");
  q.bl.items = {bi("1", 't', "2026-10-19", "2026-10-23"), bi("4", 't', "2026-10-19", "2026-10-23")};
  const EvmReport rq = q.ok();
  CHECK(rq.items == 1);
  CHECK(rq.noAmount == 1);
  CHECK(rq.bac == Approx(2.0));
  // 0 以下・非数・極端な値も数えない
  Spec e{"5"}, f{"6"}, g{"7"};
  e.dur = 0.0;
  f.dur = std::numeric_limits<double>::quiet_NaN();
  g.dur = 1e300;
  Proj w = Proj::make({a, e, f, g}, "2026-10-28");
  w.bl.items = {bi("1", 't', "2026-10-19", "2026-10-23"), bi("5", 't', "2026-10-19", "2026-10-23"), bi("6", 't', "2026-10-19", "2026-10-23"),
                bi("7", 't', "2026-10-19", "2026-10-23")};
  // dur = 0 は日程計算ではマイルストーン扱い（数えない）。NaN と 1e300 は計画量として数えない
  const EvmReport rw = w.ok();
  CHECK(rw.items == 1);
  CHECK(rw.noAmount == 2);
  CHECK(std::isfinite(rw.bac));
}

TEST_CASE("evm: ベースラインが空のとき（課題はあるが計画に入っていない）は対象なし") {
  Proj p = threeWeeks();
  p.bl.items.clear();
  const EvmReport r = p.ok();
  CHECK(r.reason == EvmReason::NoItems);
  CHECK(r.added == 3);
  CHECK(evmReasonText(r).rfind("! ", 0) == 0);
  CHECK(r.curve.empty());
}

TEST_CASE("evm: 課題が 0 件でも落ちない") {
  Proj p = Proj::make({}, "2026-10-28");
  p.bl.items.clear();
  const EvmReport r = p.ok();
  CHECK(r.reason == EvmReason::NoItems);
  CHECK(r.items == 0);
}

// ---------------------------------------------------------------- 0/100 と 50/50

TEST_CASE("evm: 0/100 は進行中を 0%、50/50 は着手で 50%") {
  Spec a{"1"}, b{"2"}, c{"3"};
  a.status = "Done";
  a.resolved = "2026-10-23";
  a.start = "2026-10-19";
  b.status = "In Progress";
  b.start = "2026-10-26";
  Proj p = Proj::make({a, b, c}, "2026-10-28");
  p.bl.items = {bi("1", 't', "2026-10-19", "2026-10-23"), bi("2", 't', "2026-10-26", "2026-10-30"), bi("3", 't', "2026-11-02", "2026-11-06")};
  EvmSettings half;
  half.progress = EvmProgress::FiftyFifty;
  const EvmReport r0 = p.ok();
  const EvmReport r5 = p.ok(half);
  CHECK(r0.ev == Approx(1.0));
  CHECK(r5.ev == Approx(1.5));  // A は完了で 1、B は着手（10/26）で 0.5
  CHECK(r5.pv == Approx(r0.pv));
  CHECK(r5.sv == Approx(-0.1));
  // 50/50 の途中: B の着手（10/26）より前の点には 0.5 は入らない
  for (const EvmPoint& pt : r5.curve) {
    if (pt.date >= D("2026-10-23") && pt.date < D("2026-10-26")) CHECK(pt.ev == Approx(1.0));
  }
  // 着手日が基準日より先の進行中は、基準日から 50%（進行中なら着手しているはず）
  p.base = D("2026-10-23");
  p.rebuild();
  CHECK(p.ok(half).ev == Approx(1.5));
  // 完了した課題の着手日が完了日より後（日付の矛盾）でも 1 を超えない。着手日がなければ完了日に 100%
  Spec d{"4"};
  d.status = "Done";
  d.resolved = "2026-10-21";
  d.start = "2026-10-25";
  Spec e{"5"};
  e.status = "Done";
  e.resolved = "2026-10-22";
  Proj q = Proj::make({d, e}, "2026-10-28");
  q.bl.items = {bi("4", 't', "2026-10-19", "2026-10-23"), bi("5", 't', "2026-10-19", "2026-10-23")};
  const EvmReport rq = q.ok(half);
  CHECK(rq.ev == Approx(2.0));
  // 完了日を基準日より後に置くと、その日までは入らない
  q.base = D("2026-10-20");
  q.rebuild();
  CHECK(q.ok(half).ev == Approx(0.0));
}

TEST_CASE("evm: 50/50 の進行中は、開始日がなければ基準日から 50%") {
  Spec b{"2"};
  b.status = "In Progress";
  Proj p = Proj::make({b}, "2026-10-28");
  p.bl.items = {bi("2", 't', "2026-10-26", "2026-10-30")};
  EvmSettings half;
  half.progress = EvmProgress::FiftyFifty;
  const EvmReport r = p.ok(half);
  CHECK(r.ev == Approx(0.5));
  CHECK(r.curve[static_cast<std::size_t>(r.nowIndex)].ev == Approx(0.5));
  // 基準日より前の点には入らない
  CHECK(r.curve.front().ev == Approx(0.0));
}

// ---------------------------------------------------------------- ベースラインを保存した日

TEST_CASE("evm: ベースラインを保存した日より前は計測しない") {
  Proj p = threeWeeks();
  p.bl.at = "2026-10-22T10:00";
  const EvmReport r = p.ok();
  REQUIRE(r.ok());
  CHECK(r.from == D("2026-10-22"));
  REQUIRE_FALSE(r.curve.empty());
  CHECK(r.curve.front().date == D("2026-10-22"));  // それより前の点はない
  for (const EvmPoint& pt : r.curve) CHECK(pt.date >= D("2026-10-22"));
  // 保存日の PV には、計画のそれまでの分が入る（A の 4/5）
  CHECK(r.curve.front().pv == Approx(4.0 / 5.0));
  // 基準日が保存日より前なら指標は出さない
  p.base = D("2026-10-21");
  p.rebuild();
  const EvmReport b = p.ok();
  CHECK(b.reason == EvmReason::BeforeBaseline);
  CHECK_FALSE(b.ok());
  CHECK(b.curve.empty());
  CHECK(evmReasonText(b).find("より前") != std::string::npos);
  // 保存日と同じ日の基準日は出す
  p.base = D("2026-10-22");
  p.rebuild();
  CHECK(p.ok().ok());
}

TEST_CASE("evm: 保存日時が分からないベースラインは、計画の最初の日から計測する") {
  Proj p = threeWeeks();
  p.bl.at = "";
  const EvmReport r = p.ok();
  REQUIRE(r.ok());
  CHECK_FALSE(r.hasFrom);
  CHECK(r.curve.front().date == D("2026-10-19"));
  bool found = false;
  for (const std::string& s : evmAssumptions(r)) found = found || s.find("保存日時が分からない") != std::string::npos;
  CHECK(found);
  // 壊れた保存日時も同じ
  p.bl.at = "not a date";
  CHECK_FALSE(p.ok().hasFrom);
}

TEST_CASE("evm: 保存した時点で完了していた課題は数えない（日程が潰れていても SPI を壊さない）") {
  Spec a{"1"}, b{"2"};
  a.status = "Done";
  a.resolved = "2026-10-05";  // 保存の前に完了。計画の日程は 1 日に潰れている
  Proj p = Proj::make({a, b}, "2026-10-21");
  p.bl.items = {bi("1", 'd', "2026-10-05", "2026-10-05"), bi("2", 't', "2026-10-19", "2026-10-23")};
  const EvmReport r = p.ok();
  CHECK(r.items == 1);
  CHECK(r.doneAtBaseline == 1);
  CHECK(r.bac == Approx(1.0));
  CHECK(r.ev == Approx(0.0));
  CHECK(r.pv == Approx(3.0 / 5.0));
}

// ---------------------------------------------------------------- 課題の追加・削除

TEST_CASE("evm: ベースラインのあとに増えた課題・消えた課題") {
  Proj p = threeWeeks();
  // 4 番はあとから追加、3 番は削除（ベースラインに残る）
  p.issues.erase(p.issues.begin() + 2);
  Spec d{"4"};
  d.status = "Done";
  d.resolved = "2026-10-27";
  p.issues.push_back(mk(d));
  p.rebuild();
  const EvmReport r = p.ok();
  CHECK(r.items == 2);
  CHECK(r.added == 1);  // 4 番: 計画がないので BAC・EV に入れない
  CHECK(r.gone == 1);   // 3 番
  CHECK(r.bac == Approx(2.0));
  CHECK(r.ev == Approx(1.0));
  const std::vector<std::string> notes = evmNotes(r);
  REQUIRE(notes.size() == 2);
  CHECK(notes[0].find("増えた 1 件") != std::string::npos);
  CHECK(notes[1].find("見つからない 1 件") != std::string::npos);
  // 絞り込み中は「消えた」を数えない（誰のものか分からない）
  TrendFilter f;
  f.who = TrendFilter::Who::Unassigned;
  CHECK(p.ok({}, f).gone == 0);
}

TEST_CASE("evm: 親に変わった課題・ベースライン時に親だった課題・マイルストーンは「消えた」にしない") {
  Spec parent{"1"}, kid{"2", "1"}, ms{"3"};
  ms.dur = 0.0;  // マイルストーン
  Proj p = Proj::make({parent, kid, ms}, "2026-10-28");
  BaselineItem pi = bi("1", 't', "2026-10-19", "2026-10-23");
  pi.parent = true;  // ベースラインでは親だった
  p.bl.items = {pi, bi("2", 't', "2026-10-19", "2026-10-23"), bi("3", 't', "2026-10-19", "2026-10-19")};
  const EvmReport r = p.ok();
  CHECK(r.items == 1);  // 2 番だけ（1 は親、3 はマイルストーン）
  CHECK(r.gone == 0);
  CHECK(r.added == 0);
}

TEST_CASE("evm: 葉だった課題があとで親になった（ベースラインでは葉）") {
  Spec parent{"1"}, kid{"2", "1"};
  Proj p = Proj::make({parent, kid}, "2026-10-28");
  p.bl.items = {bi("1", 't', "2026-10-19", "2026-10-23")};  // 1 番は葉だった。2 番はあとから
  const EvmReport r = p.ok();
  CHECK(r.reason == EvmReason::NoItems);
  CHECK(r.added == 1);  // 2 番
  CHECK(r.gone == 1);   // 1 番（もう作業ではない）
}

// ---------------------------------------------------------------- 日付なし・完了日なし

TEST_CASE("evm: 完了日のない完了済み・ベースラインに完了日のない課題は数えない") {
  Spec a{"1"}, b{"2"}, c{"3"};
  a.status = "Done";  // 完了日なし
  Proj p = Proj::make({a, b, c}, "2026-10-28");
  p.bl.items = {bi("1", 't', "2026-10-19", "2026-10-23"), bi("2", 't', "2026-10-26", "2026-10-30"), bi("3", 't', "2026-10-19", "")};
  const EvmReport r = p.ok();
  CHECK(r.items == 1);
  CHECK(r.noResolved == 1);
  CHECK(r.noPlanDate == 1);
  CHECK(r.bac == Approx(1.0));
  const std::vector<std::string> notes = evmNotes(r);
  bool resolved = false, plan = false;
  for (const std::string& s : notes) {
    resolved = resolved || s.find("完了日（Resolved）のない完了済みの 1 件") != std::string::npos;
    plan = plan || s.find("ベースラインに完了日のない 1 件") != std::string::npos;
  }
  CHECK(resolved);
  CHECK(plan);
}

TEST_CASE("evm: ベースラインに開始日がなければ完了日の 1 日に置く") {
  Spec a{"1"};
  Proj p = Proj::make({a}, "2026-10-28");
  p.bl.items = {bi("1", 't', "", "2026-10-30")};
  const EvmReport r = p.ok();
  REQUIRE(r.ok());
  CHECK(r.planStart == D("2026-10-30"));
  // 10/29 の終わりまでは 0、10/30 で全量
  for (const EvmPoint& pt : r.curve) {
    if (pt.date < D("2026-10-30")) CHECK(pt.pv == Approx(0.0));
    else CHECK(pt.pv == Approx(1.0));
  }
  CHECK(r.pv == Approx(0.0));
  CHECK_FALSE(r.hasSpi);  // PV が 0 → SPI は出さない（ゼロ割しない）
  CHECK(evmSpiText(r) == "-");
}

// ---------------------------------------------------------------- 年またぎ・休業日

TEST_CASE("evm: 年またぎ・休業日は営業日に数えない") {
  const Calendar cal = Calendar::fromText(defaultHolidaysText());  // 2026-12-29〜2027-01-03 が休業
  Spec a{"1", "", "", "To Do", 2.0};
  Proj p = Proj::make({a}, "2027-01-04", cal);
  // 12/28（月）〜 1/8（金）。営業日は 12/28、1/4〜1/8 の 6 日
  p.bl.items = {bi("1", 't', "2026-12-28", "2027-01-08")};
  p.bl.at = "2026-12-28T09:00";
  const EvmReport r = p.ok();
  REQUIRE(r.ok());
  CHECK(r.bac == Approx(2.0));
  CHECK(r.pv == Approx(2.0 * 2.0 / 6.0));  // 1/4 の終わりで 12/28 と 1/4 の 2 日ぶん
  for (const EvmPoint& pt : r.curve) {
    if (pt.date == D("2026-12-28")) CHECK(pt.pv == Approx(2.0 / 6.0));
    if (pt.date == D("2027-01-03")) CHECK(pt.pv == Approx(2.0 / 6.0));  // 休業日の間は増えない
    if (pt.date == D("2027-01-10")) CHECK(pt.pv == Approx(2.0));
  }
  // 起点から基準日までの経過も営業日（12/28 と 1/4 の 2 日）
  CHECK(r.at == Approx(2.0));
}

TEST_CASE("evm: 休業日・土日だけの区間は次の営業日の 1 日に置く") {
  const Calendar cal = Calendar::fromText(defaultHolidaysText());
  Spec a{"1"};
  Proj p = Proj::make({a}, "2027-01-05", cal);
  p.bl.items = {bi("1", 't', "2026-12-30", "2026-12-31")};  // 両方休業日
  p.bl.at = "2026-12-28T09:00";
  const EvmReport r = p.ok();
  REQUIRE(r.ok());
  CHECK(r.items == 1);
  CHECK(r.planStart == D("2027-01-04"));
  CHECK(r.planEnd == D("2027-01-04"));
  for (const EvmPoint& pt : r.curve) {
    if (pt.date < D("2027-01-04")) CHECK(pt.pv == Approx(0.0));
    else CHECK(pt.pv == Approx(1.0));
  }
}

// ---------------------------------------------------------------- S カーブ

TEST_CASE("evm: S カーブは日付の昇順で、PV は単調に BAC まで増え、EV は基準日までしか持たない") {
  const Proj p = threeWeeks();
  const EvmReport r = p.ok();
  REQUIRE(r.nowIndex >= 0);
  REQUIRE(static_cast<std::size_t>(r.nowIndex) < r.curve.size());
  CHECK(r.curve[static_cast<std::size_t>(r.nowIndex)].date == p.base);
  CHECK(r.curve[static_cast<std::size_t>(r.nowIndex)].pv == Approx(r.pv));
  CHECK(r.curve[static_cast<std::size_t>(r.nowIndex)].ev == Approx(r.ev));
  for (std::size_t k = 1; k < r.curve.size(); ++k) {
    CHECK(r.curve[k].date > r.curve[k - 1].date);
    CHECK(r.curve[k].pv >= r.curve[k - 1].pv - 1e-12);
    if (r.curve[k].hasEv && r.curve[k - 1].hasEv) CHECK(r.curve[k].ev >= r.curve[k - 1].ev - 1e-12);
    CHECK(r.curve[k].hasEv == (r.curve[k].date <= p.base));
  }
  CHECK(r.curve.back().pv == Approx(r.bac));
  CHECK(r.curve.back().date >= r.planEnd);
  CHECK(r.curve.front().date == r.from);
  // 週の終わり（日曜）の点
  bool sundays = true;
  for (std::size_t k = 1; k < r.curve.size(); ++k)
    if (r.curve[k].date != p.base && dow(r.curve[k].date) != 0) sundays = false;
  CHECK(sundays);
}

TEST_CASE("evm: 完了予測（ES ÷ 経過のペースが続いた場合）") {
  const Proj p = threeWeeks();
  const EvmReport r = p.ok();
  // 計画の期間 15 営業日 × 経過 8 ÷ ES 5 = 24 営業日目 = 11/19
  REQUIRE(r.forecast.ok);
  CHECK(r.forecast.finish == D("2026-11-19"));
  CHECK(r.forecast.finishWeek == D("2026-11-16"));
  CHECK(r.curve.back().date >= r.forecast.finish);  // 予測の終わりまで軸を伸ばす
  const std::string t = evmForecastText(r);
  CHECK(t.rfind("!", 0) == std::string::npos);
  CHECK(t.find("11/16(月) の週") != std::string::npos);
}

TEST_CASE("evm: 完了予測を出せない理由（完了済み・出来高なし・遠すぎる）") {
  {  // 出来高なし
    Spec a{"1"}, b{"2"};
    Proj p = Proj::make({a, b}, "2026-10-21");
    p.bl.items = {bi("1", 't', "2026-10-19", "2026-10-23"), bi("2", 't', "2026-10-26", "2026-10-30")};
    const EvmReport r = p.ok();
    CHECK_FALSE(r.forecast.ok);
    CHECK(r.forecast.reason == EvmForecastReason::NoProgress);
    CHECK(r.es == Approx(0.0));
    CHECK(r.esDelta == Approx(3.0));  // 0 から数えた経過のぶん遅れ
    CHECK(evmForecastText(r).rfind("! ", 0) == 0);
  }
  {  // 遠すぎる: 100 週の計画の 1 日目に、1 日ぶんだけ終わった
    Spec x{"1", "", "", "Done", 0.2}, y{"2", "", "", "To Do", 100.0};
    x.resolved = "2026-10-19";
    Proj p = Proj::make({x, y}, "2026-10-23");
    p.bl.items = {bi("1", 't', "2026-10-19", "2026-10-19"), bi("2", 't', "2026-10-19", "2028-09-01")};
    const EvmReport r = p.ok();
    CHECK(r.forecast.reason == EvmForecastReason::TooFar);
    CHECK_FALSE(r.forecast.ok);
    CHECK(evmForecastText(r).rfind("! ", 0) == 0);
    CHECK(evmForecastText(r).find("104 週") != std::string::npos);
  }
  {  // 完了済み
    Spec a{"1"};
    a.status = "Done";
    a.resolved = "2026-10-23";
    Proj p = Proj::make({a}, "2026-10-29");
    p.bl.items = {bi("1", 't', "2026-10-19", "2026-10-23")};
    const EvmReport r = p.ok();
    CHECK(r.forecast.reason == EvmForecastReason::Finished);
    CHECK(evmForecastText(r).rfind("!", 0) == std::string::npos);
    // 完了後の ES は計画の終わり。基準日が先に進むほど遅れが増える（完了が計画より早ければ進み）
    CHECK(r.es == Approx(5.0));
    CHECK(r.at == Approx(9.0));
  }
}

TEST_CASE("evm: 計画の終わりが極端に先でも、S カーブは週の数の上限で切る") {
  Spec a{"1"};
  Proj p = Proj::make({a}, "2026-10-28");
  p.bl.items = {bi("1", 't', "2026-10-19", "9999-12-31")};
  const EvmReport r = p.ok();
  REQUIRE(r.ok());
  CHECK(r.curve.size() <= static_cast<std::size_t>(kEvmMaxWeeks + 2));
  CHECK(r.curveClipped);
  REQUIRE(r.nowIndex >= 0);
  CHECK(r.curve[static_cast<std::size_t>(r.nowIndex)].date == p.base);
  CHECK(std::isfinite(r.pv));
  CHECK(r.pv > 0);
  bool clipNote = false;
  for (const std::string& s : evmNotes(r)) clipNote = clipNote || s.find("156 週") != std::string::npos;
  CHECK(clipNote);
}

TEST_CASE("evm: 基準日が計画よりずっと先でも、基準日を含む S カーブにする") {
  Spec a{"1"};
  Proj p = Proj::make({a}, "2030-01-07");
  p.bl.items = {bi("1", 't', "2026-10-19", "2026-10-23")};
  const EvmReport r = p.ok();
  REQUIRE(r.ok());
  CHECK(r.curveClipped);  // 開始側を切った
  CHECK(r.curve.size() <= static_cast<std::size_t>(kEvmMaxWeeks + 2));
  REQUIRE(r.nowIndex >= 0);
  CHECK(r.curve[static_cast<std::size_t>(r.nowIndex)].date == p.base);
  CHECK(r.pv == Approx(1.0));
  CHECK(r.ev == Approx(0.0));
  CHECK(r.esDelta > 0);
}

// ---------------------------------------------------------------- 絞り込み

TEST_CASE("evm: 担当者・ルート課題で絞る") {
  Spec root{"1"}, a{"2", "1", "田中"}, b{"3", "1", "佐藤"}, c{"4", "", "田中"};
  a.status = "Done";
  a.resolved = "2026-10-23";
  Proj p = Proj::make({root, a, b, c}, "2026-10-28");
  p.bl.items = {bi("2", 't', "2026-10-19", "2026-10-23"), bi("3", 't', "2026-10-26", "2026-10-30"), bi("4", 't', "2026-10-26", "2026-10-30")};
  const EvmReport all = p.ok();
  CHECK(all.items == 3);
  CHECK_FALSE(all.filtered);
  TrendFilter f;
  f.who = TrendFilter::Who::Name;
  f.name = "田中";
  const EvmReport tanaka = p.ok({}, f);
  CHECK(tanaka.filtered);
  CHECK(tanaka.items == 2);
  CHECK(tanaka.bac == Approx(2.0));
  CHECK(tanaka.ev == Approx(1.0));
  f.name = "  佐藤 ";  // 前後の空白は無視する
  CHECK(p.ok({}, f).items == 1);
  f.who = TrendFilter::Who::Unassigned;
  CHECK(p.ok({}, f).reason == EvmReason::NoItems);
  // ルート課題
  TrendFilter r1;
  r1.root = p.comp.T.find("1");
  const EvmReport rr = p.ok({}, r1);
  CHECK(rr.items == 2);
  TrendFilter alone;
  alone.root = TrendFilter::kStandalone;
  const EvmReport ra = p.ok({}, alone);
  CHECK(ra.items == 1);
  // 絞り込み後の「増えた」は、その範囲だけ数える
  Spec d{"5", "1", "鈴木"};
  p.issues.push_back(mk(d));
  p.rebuild();
  CHECK(p.ok({}, r1).added == 1);
  CHECK(p.ok({}, alone).added == 0);
}

TEST_CASE("evm: 絞り込みのルート課題が範囲外・ルートでないときはエラー") {
  const Proj p = threeWeeks();
  TrendFilter f;
  f.root = 99;
  CHECK_FALSE(p.run({}, f).ok());
  f.root = -7;
  CHECK_FALSE(p.run({}, f).ok());
  Spec root{"1"}, kid{"2", "1"};
  Proj q = Proj::make({root, kid}, "2026-10-28");
  q.bl.items = {bi("2", 't', "2026-10-19", "2026-10-23")};
  f.root = q.comp.T.find("2");  // 子はルートではない
  CHECK_FALSE(q.run({}, f).ok());
}

TEST_CASE("evm: 課題の数と計算結果が合わないときはエラー（例外にしない）") {
  Proj p = threeWeeks();
  p.issues.pop_back();
  auto r = computeEvm(p.comp.T, p.issues, p.comp.R, p.cal, p.base, &p.bl, EvmSettings{});
  CHECK_FALSE(r.ok());
}

// ---------------------------------------------------------------- 総当たりとの照合

TEST_CASE("evm: PV の S カーブと ES を、日ごとの総当たりと照合する") {
  const Calendar cal = Calendar::fromText(defaultHolidaysText());
  std::uint64_t rng = 12345;
  const auto next = [&](std::uint64_t m) {
    rng = rng * 6364136223846793005ULL + 1442695040888963407ULL;
    return (rng >> 33) % m;
  };
  std::vector<Spec> specs;
  std::vector<BaselineItem> items;
  struct Plan {
    Day s, f;
    double a;
  };
  std::vector<Plan> plans;
  const Day origin = D("2026-12-14");
  for (int i = 0; i < 40; ++i) {
    Spec s{std::to_string(i + 1)};
    s.dur = static_cast<double>(1 + next(8)) / 2.0;
    if (next(3) == 0) {
      s.status = "Done";
      s.resolved = iso(origin + static_cast<Day>(next(40)));
    } else if (next(4) == 0) {
      s.status = "In Progress";
    }
    specs.push_back(s);
    Day st = origin + static_cast<Day>(next(40));
    while (!cal.isWork(st)) ++st;
    Day fi = st + static_cast<Day>(next(30));
    while (!cal.isWork(fi)) ++fi;
    items.push_back(bi(s.id, 't', iso(st).c_str(), iso(fi).c_str()));
    plans.push_back({st, fi, *s.dur});
  }
  Proj p = Proj::make(specs, "2027-01-12", cal);
  p.bl.items = items;
  p.bl.at = "2026-12-14T09:00";
  const EvmReport r = p.ok();
  REQUIRE(r.ok());
  REQUIRE(r.items == 40);
  const auto brutePv = [&](Day d) {
    double pv = 0;
    for (const Plan& q : plans) {
      int n = 0, upto = 0;
      for (Day x = q.s; x <= q.f; ++x)
        if (cal.isWork(x)) {
          ++n;
          if (x <= d) ++upto;
        }
      pv += q.a * upto / n;
    }
    return pv;
  };
  for (const EvmPoint& pt : r.curve) CHECK(pt.pv == Approx(brutePv(pt.date)).epsilon(1e-9));
  CHECK(r.pv == Approx(brutePv(p.base)).epsilon(1e-9));
  double bac = 0;
  for (const Plan& q : plans) bac += q.a;
  CHECK(r.bac == Approx(bac));
  // ES: 日ごとに PV を並べ、EV に初めて達する営業日を線形補間で探す
  double ev = 0;
  for (std::size_t i = 0; i < specs.size(); ++i)
    if (specs[i].status == "Done" && parseIso(specs[i].resolved).value() <= p.base) ev += plans[i].a;
  CHECK(r.ev == Approx(ev));
  double prev = 0, es = 0;
  int w = 0;
  for (Day x = D("2026-12-14");; ++x) {
    if (!cal.isWork(x)) continue;
    ++w;
    const double cur = brutePv(x);
    if (cur >= ev - 1e-9) {
      es = (w - 1) + (cur - prev > 1e-12 ? (ev - prev) / (cur - prev) : 0.0);
      break;
    }
    prev = cur;
  }
  // 起点はベースラインの最初の営業日（plans の最小の開始日）。brute 側の起点が違うときは差で比べる
  Day minS = plans[0].s;
  for (const Plan& q : plans) minS = std::min(minS, q.s);
  int shift = 0;
  for (Day x = D("2026-12-14"); x < minS; ++x)
    if (cal.isWork(x)) ++shift;
  CHECK(r.es == Approx(es - shift).epsilon(1e-9));
}

// ---------------------------------------------------------------- 5,000 件

TEST_CASE("evm: 5,000 件を短い時間で集計する") {
  auto g = generateProject({5000, 7});
  const Day base = D("2026-10-19");
  Proj p;
  p.issues = std::move(g.issues);
  p.meta = std::move(g.meta);
  p.base = base;
  p.cal = Calendar::fromText(defaultHolidaysText());
  for (std::size_t i = 0; i < p.issues.size(); ++i) {
    Issue& s = p.issues[i];
    if (i % 3 == 0) s.estW = 0.5 + static_cast<double>(i % 7);
    if (statusCat(s.status) == Cat::Done) s.resolved = base - 40 + static_cast<Day>(i % 60);
  }
  p.rebuild();
  REQUIRE(p.comp.T.n == 5000);
  // ベースライン: 日程を保存して、進行中・完了を少し動かす
  auto b = captureBaseline(p.issues, p.comp, "計画", "2026-09-01T09:00");
  REQUIRE(b.ok());
  p.bl = b.value();
  p.bl.id = "b1";
  const auto t0 = std::chrono::steady_clock::now();
  const auto res = p.run();
  const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  MESSAGE("evm 5000 件: " << ms << " ms");
  REQUIRE(res.ok());
  CHECK(res->ok());
  CHECK(res->items > 1000);
  CHECK(res->curve.size() <= static_cast<std::size_t>(kEvmMaxWeeks + 2));
  CHECK(res->bac > 0);
  CHECK(res->pv <= res->bac + 1e-6);
  CHECK(ms < 1000);  // 目標は Release で 50 ms 以内（Debug・サニタイザ付きでも 1 秒は超えない）
}

// ---------------------------------------------------------------- 文言

TEST_CASE("evm: 前提と注記の文") {
  const Proj p = threeWeeks();
  const EvmReport r = p.ok();
  const std::vector<std::string> as = evmAssumptions(r);
  REQUIRE(as.size() >= 6);
  CHECK(as[0].find("ベースライン「キックオフ」（保存 2026/10/19 09:00）") != std::string::npos);
  bool zero = false, unit = false;
  for (const std::string& s : as) {
    zero = zero || s.find("0/100") != std::string::npos;
    unit = unit || s.find("見積りがあれば見積り、なければ期間") != std::string::npos;
  }
  CHECK(zero);
  CHECK(unit);
  EvmSettings half;
  half.progress = EvmProgress::FiftyFifty;
  bool fifty = false;
  for (const std::string& s : evmAssumptions(p.ok(half))) fifty = fifty || s.find("50/50") != std::string::npos;
  CHECK(fifty);
  CHECK(evmNotes(r).empty());
  CHECK(evmAmountText(12.25) == "12.3 週");  // 小数 1 桁
  CHECK(evmAmountText(12.0) == "12 週");
  CHECK(evmAmountText(0.04) == "0 週");
  CHECK(evmSignedAmountText(2.5) == "+2.5 週");
  CHECK(evmSignedAmountText(-0.04) == "±0 週");
}

// ---------------------------------------------------------------- 設定（project.settings.json の "evm"）

TEST_CASE("evm 設定: 既定のときは書かない。往復できる") {
  EvmSettings s;
  CHECK(s.isDefault());
  CHECK(evmSettingsToJson(s).empty());
  s.progress = EvmProgress::FiftyFifty;
  s.spiWarn = 0.8;
  const auto j = evmSettingsToJson(s);
  CHECK(j["progress"] == "half");
  CHECK(j["spiWarn"] == 0.8);
  const EvmSettings back = evmSettingsFromJson(j);
  CHECK(back == s);
}

TEST_CASE("evm 設定: 壊れた値は項目ごとに読み飛ばし、未知のキーは残す") {
  using Json = nlohmann::ordered_json;
  CHECK(evmSettingsFromJson(Json::parse(R"(5)")).isDefault());
  CHECK(evmSettingsFromJson(Json::parse(R"([1,2])")).isDefault());
  CHECK(evmSettingsFromJson(Json::parse(R"(null)")).isDefault());
  // 型が違う・範囲外・知らない値 → 既定。ほかの項目は読む
  EvmSettings s = evmSettingsFromJson(Json::parse(R"({"progress":5,"spiWarn":"x"})"));
  CHECK(s.isDefault());
  s = evmSettingsFromJson(Json::parse(R"({"progress":"half","spiWarn":0})"));
  CHECK(s.progress == EvmProgress::FiftyFifty);
  CHECK(s.spiWarn == kEvmSpiWarnDefault);
  s = evmSettingsFromJson(Json::parse(R"({"progress":"nine","spiWarn":2.5})"));
  CHECK(s.isDefault());
  s = evmSettingsFromJson(Json::parse(R"({"spiWarn":-1})"));
  CHECK(s.spiWarn == kEvmSpiWarnDefault);
  s = evmSettingsFromJson(Json::parse(R"({"spiWarn":0.5})"));
  CHECK(s.spiWarn == 0.5);
  s = evmSettingsFromJson(Json::parse(R"({"spiWarn":1e300})"));  // 範囲外の大きな値
  CHECK(s.spiWarn == kEvmSpiWarnDefault);
  // 未知のキーは消さずに書き戻す
  s = evmSettingsFromJson(Json::parse(R"({"progress":"half","futureKey":{"n":1},"other":[1]})"));
  CHECK(s.progress == EvmProgress::FiftyFifty);
  CHECK_FALSE(s.isDefault());
  const auto j = evmSettingsToJson(s);
  CHECK(j["futureKey"]["n"] == 1);
  CHECK(j["other"][0] == 1);
  // 既定の値でも未知のキーがあれば書く（消さない）
  s = evmSettingsFromJson(Json::parse(R"({"futureKey":1})"));
  CHECK_FALSE(s.isDefault());
  CHECK(evmSettingsToJson(s)["futureKey"] == 1);
}

TEST_CASE("evm 設定: project.settings.json の往復と、既定のときは書かない") {
  FixedClock clk(LocalTime{2026, 10, 19, 9, 30, 0});
  ProjectRecord rec;
  rec.settings.id = "p1";
  rec.settings.name = "テスト";
  rec.settings.baseDate = "2026-10-19";
  rec.settings.holidays = "";
  const auto j0 = settingsToJson(rec);
  CHECK_FALSE(j0.contains("evm"));
  rec.settings.evm.progress = EvmProgress::FiftyFifty;
  rec.settings.evm.spiWarn = 0.75;
  const auto j1 = settingsToJson(rec);
  REQUIRE(j1.contains("evm"));
  const ProjectRecord back = recordFromSettingsJson(j1, "", "テスト", clk);
  CHECK(back.settings.evm == rec.settings.evm);
  // 未知のトップレベルのキーとは別に、"evm" は既知のキーとして unknown に入らない
  CHECK_FALSE(back.settings.unknown.contains("evm"));
  // 壊れた "evm" は既定に戻る（ほかの設定は読む）
  auto j2 = j1;
  j2["evm"] = "壊れた値";
  const ProjectRecord bad = recordFromSettingsJson(j2, "", "テスト", clk);
  CHECK(bad.settings.evm.isDefault());
  CHECK(bad.settings.id == "p1");
}

TEST_CASE("evm 設定: Session で変える（範囲に丸める・同じ値は保存しない・読み取り専用は断る）") {
  ProjectData d;
  d.meta.hasDeadlineCol = true;
  d.settings.id = "p1";
  d.settings.name = "t";
  d.settings.baseDate = "2026-10-19";
  Session s(std::move(d));
  CHECK(s.evmSettings().isDefault());
  EvmSettings e;
  e.progress = EvmProgress::FiftyFifty;
  e.spiWarn = 5.0;  // 範囲外は丸める
  s.setEvmSettings(e);
  CHECK(s.evmSettings().progress == EvmProgress::FiftyFifty);
  CHECK(s.evmSettings().spiWarn == kEvmSpiWarnMax);
  e.spiWarn = std::numeric_limits<double>::quiet_NaN();
  s.setEvmSettings(e);
  CHECK(s.evmSettings().spiWarn == kEvmSpiWarnDefault);
  e.spiWarn = 0.0;
  s.setEvmSettings(e);
  CHECK(s.evmSettings().spiWarn == kEvmSpiWarnMin);
  // 未知のキーは画面からの設定で消えない
  s.data().settings.evm.extra["futureKey"] = 1;
  e.spiWarn = 0.7;
  s.setEvmSettings(e);
  CHECK(s.evmSettings().extra["futureKey"] == 1);
  // 読み取り専用
  s.setReadOnly(true);
  e.progress = EvmProgress::ZeroHundred;
  s.setEvmSettings(e);
  CHECK(s.evmSettings().progress == EvmProgress::FiftyFifty);
}
