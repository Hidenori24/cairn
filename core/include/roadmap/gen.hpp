// テスト用の課題ツリーを乱数で作る（rmcli gen と性質のテストで使う）。シードが同じなら同じものを作る
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "roadmap/model.hpp"

namespace roadmap {

struct GenOptions {
  int count = 100;
  std::uint64_t seed = 1;
  double depRate = 0.6;      // 末端ごとの依存の数の目安
  bool withStatus = true;    // 完了・進行中を混ぜる
  bool withComments = true;
  bool withScenario = true;
};

struct Generated {
  std::vector<Issue> issues;
  ProjectMeta meta;
};

Generated generateProject(const GenOptions& opt);

}  // namespace roadmap
