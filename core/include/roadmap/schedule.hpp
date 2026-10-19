// ツリー構築 + 計算をまとめて行う小道具と、計算結果を使った書き出し
#pragma once

#include <string>
#include <vector>

#include "roadmap/csv.hpp"
#include "roadmap/engine.hpp"

namespace roadmap {

struct Computed {
  Tree T;
  std::vector<ScheduleRow> R;
  std::vector<Issue> issues;  // 計算に使った課題の写し（Session が画面用に持つ。computeAll では空）
};

Result<Computed> computeAll(const std::vector<Issue>& issues, const ProjectMeta& meta, const Calendar& cal,
                            const ScheduleOptions& opt);

// アプリ用（入力した値のまま）
std::string exportAppCsv(const std::vector<Issue>& issues, const ProjectMeta& meta);
// JIRA用（計画日・階層の畳み込み）。c が null なら計画日なし
std::string exportJiraCsv(const std::vector<Issue>& issues, const ProjectMeta& meta, const Computed* c);

}  // namespace roadmap
