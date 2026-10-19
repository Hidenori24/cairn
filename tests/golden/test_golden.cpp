// golden/ の正解データとの照合（SPEC 12.2）
#include <doctest/doctest.h>

#include <cmath>
#include <nlohmann/json.hpp>

#include "../test_util.hpp"
#include "roadmap/csv.hpp"
#include "roadmap/schedule.hpp"

using namespace roadmap;
using testutil::golden;
using testutil::readBytes;
using testutil::readText;

namespace {

const char* const kSamples[] = {"regulation_roadmap", "hierarchy_demo", "leveling_demo"};

Calendar defaultCal() { return Calendar::fromText(readText(golden("holidays.txt"))); }

csv::ImportResult load(const std::string& sample) {
  auto imp = csv::importCsv(readText(golden("samples/" + sample + ".csv")));
  if (!imp.ok()) FAIL(imp.error());
  return std::move(imp).value();
}

nlohmann::json optDate(const OptDay& d) { return d ? nlohmann::json(fmtIso(*d)) : nlohmann::json(nullptr); }

// 1件の照合。最初に食い違った行の did・フィールド・期待値・実際の値を出す
void checkSchedule(const std::string& sample, const std::string& file) {
  const auto expect = nlohmann::json::parse(readBytes(golden(file)));
  auto imp = load(sample);
  ScheduleOptions opt;
  opt.base = *parseIso(expect["baseDate"].get<std::string>());
  opt.scenario = expect["scenario"].get<std::string>();
  opt.resource.on = expect["levelResources"]["on"].get<bool>();
  opt.resource.cap = expect["levelResources"]["cap"].get<int>();
  auto c = computeAll(imp.issues, imp.meta, defaultCal(), opt);
  if (!c.ok()) FAIL(c.error());
  const Tree& T = c->T;
  CHECK(nlohmann::json(T.warns) == expect["warns"]);
  CHECK(nlohmann::json(T.scenarios) == expect["scenarios"]);
  const auto& rows = expect["rows"];
  REQUIRE(rows.size() == T.dfs.size());
  for (std::size_t k = 0; k < rows.size(); ++k) {
    const int id = T.dfs[k];
    const ScheduleRow& r = c->R[id];
    const bool leaf = T.isLeaf(id);
    nlohmann::json got = {
        {"id", T.ids[id]}, {"did", T.did[id]}, {"name", T.name[id]}, {"depth", T.depth[id]}, {"leaf", r.leaf},
        {"cat", catName(T.cat[id])},
        {"kind", leaf ? nlohmann::json(T.kind[id] == Kind::Ms ? "ms" : "task") : nlohmann::json(nullptr)},
        {"asapStart", fmtIso(r.asapStart)}, {"asapFinish", fmtIso(r.asapFinish)},
        {"alapStart", fmtIso(r.alapStart)}, {"alapFinish", fmtIso(r.alapFinish)}, {"float", r.floatDays},
        {"status", r.status}, {"weeks", leaf ? nlohmann::json(r.weeks) : nlohmann::json(nullptr)},
        {"days", leaf ? nlohmann::json(r.days) : nlohmann::json(nullptr)}, {"waited", r.waited},
        {"pinned", r.pinned}, {"effectiveDeadline", leaf ? optDate(T.dl[id]) : nlohmann::json(nullptr)},
        {"ownDeadline", optDate(T.ownDl[id])}, {"n", r.n}, {"nDone", r.nDone}};
    const auto& e = rows[k];
    for (auto it = e.begin(); it != e.end(); ++it) {
      const auto& gv = got[it.key()];
      bool same = gv == it.value();
      if (!same && it.key() == "weeks" && gv.is_number() && it.value().is_number())
        same = std::abs(gv.get<double>() - it.value().get<double>()) < 1e-9;
      if (!same) {
        FAIL_CHECK(file << ": 行 " << k << " did=" << e["did"].get<std::string>() << " の " << it.key() << ": 期待 "
                        << it.value().dump() << " / 実際 " << gv.dump());
        return;
      }
    }
  }
}

}  // namespace

TEST_CASE("golden: スケジュール計算") {
  for (const char* s : kSamples) {
    const std::string sample = s;
    SUBCASE((sample + ".base").c_str()) { checkSchedule(sample, sample + ".base.json"); }
    SUBCASE((sample + ".level1").c_str()) { checkSchedule(sample, sample + ".level1.json"); }
    SUBCASE((sample + ".level2").c_str()) { checkSchedule(sample, sample + ".level2.json"); }
    SUBCASE((sample + ".base_1102").c_str()) { checkSchedule(sample, sample + ".base_1102.json"); }
  }
  SUBCASE("regulation_roadmap.scn_nb") { checkSchedule("regulation_roadmap", "regulation_roadmap.scn_nb.json"); }
}

TEST_CASE("golden: 書き出しCSVがバイト単位で一致") {
  for (const char* s : kSamples) {
    const std::string sample = s;
    auto imp = load(sample);
    SUBCASE((sample + ".roundtrip").c_str()) {
      CHECK(exportAppCsv(imp.issues, imp.meta) == readBytes(golden(sample + ".roundtrip.csv")));
    }
    SUBCASE((sample + ".jira").c_str()) {
      ScheduleOptions opt;
      opt.base = *parseIso("2026-10-19");
      auto c = computeAll(imp.issues, imp.meta, defaultCal(), opt);
      REQUIRE(c.ok());
      CHECK(exportJiraCsv(imp.issues, imp.meta, &c.value()) == readBytes(golden(sample + ".jira.csv")));
    }
  }
}
