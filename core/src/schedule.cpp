#include "roadmap/schedule.hpp"

namespace roadmap {

Result<Computed> computeAll(const std::vector<Issue>& issues, const ProjectMeta& meta, const Calendar& cal,
                            const ScheduleOptions& opt) {
  auto t = buildTree(issues, meta);
  if (!t) return Error{t.error()};
  Computed c{std::move(t).value(), {}, {}};
  c.R = computeSchedule(c.T, issues, cal, opt);
  return c;
}

std::string exportAppCsv(const std::vector<Issue>& issues, const ProjectMeta& meta) {
  return csv::exportCsv(issues, meta, {});
}

std::string exportJiraCsv(const std::vector<Issue>& issues, const ProjectMeta& meta, const Computed* c) {
  csv::ExportOptions o;
  o.planned = true;
  o.plan = [c](const std::string& id) -> std::optional<csv::PlanInfo> {
    if (!c) return std::nullopt;
    const int i = c->T.find(id);
    if (i < 0) return std::nullopt;
    const ScheduleRow& r = c->R[i];
    return csv::PlanInfo{r.asapStart, r.asapFinish, r.leaf, r.weeks, c->T.baseWeeks[i]};
  };
  o.ownDeadline = [c](const std::string& id) -> OptDay {
    if (!c) return std::nullopt;
    const int i = c->T.find(id);
    return i < 0 ? std::nullopt : c->T.ownDl[i];
  };
  return csv::exportCsv(issues, meta, o);
}

}  // namespace roadmap
