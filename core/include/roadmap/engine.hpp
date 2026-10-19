// 課題ツリーの構築とスケジュール計算（SPEC 7章。JS版 engine.js と同じ結果になること）
#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "roadmap/calendar.hpp"
#include "roadmap/model.hpp"
#include "roadmap/result.hpp"

namespace roadmap {

enum class Kind { Task, Ms };

// ツリー。課題は issues の添字（int）で参照する。
struct Tree {
  int n = 0;
  std::vector<std::string> ids;                 // 添字 → id
  std::unordered_map<std::string, int> byId;    // id → 添字（同じ id は最後のもの）
  std::vector<std::vector<int>> kids;
  std::vector<int> parentOf;                    // -1 = なし
  std::vector<int> roots;
  std::vector<int> depth, rootOf;
  std::vector<std::vector<int>> leaves;         // 配下の葉（葉なら自分だけ）
  std::vector<int> dfs;                         // 深さ優先の順
  std::vector<int> dfsPos;                      // 添字 → dfs での位置
  std::vector<std::string> did, name;           // 表示ID・表示名
  std::vector<Cat> cat;
  std::vector<double> baseWeeks;                // 葉のみ有効
  std::vector<Kind> kind;                       // 葉のみ有効
  std::vector<std::vector<int>> preds, succs;   // 葉どうし
  std::vector<int> leafOrder;                   // トポロジカル順
  std::vector<OptDay> ownDl, dl;                // 自身の期限 / 葉の実効の期限
  std::vector<std::string> warns;
  std::vector<std::string> scenarios;           // 「標準」+ …

  bool isLeaf(int i) const { return kids[i].empty(); }
  int find(const std::string& id) const {
    auto it = byId.find(id);
    return it == byId.end() ? -1 : it->second;
  }
};

Result<Tree> buildTree(const std::vector<Issue>& issues, const ProjectMeta& meta);

struct ScheduleRow {
  bool leaf = false;
  bool done = false;
  bool prog = false;
  double weeks = 0;  // 葉のみ
  int days = 0;      // 葉のみ
  Day asapStart = 0, asapFinish = 0, alapStart = 0, alapFinish = 0;
  int floatDays = 0;
  std::string status;  // 遅れ / 余裕小 / OK / 完了
  int n = 1, nDone = 0;
  Day freeStart = 0;
  bool waited = false;
  bool pinned = false;
};

struct ScheduleOptions {
  Day base = 0;
  std::string scenario = kStdScenario;
  ResourceLimit resource;
};

// issues は buildTree に渡したものと同じであること
std::vector<ScheduleRow> computeSchedule(const Tree& T, const std::vector<Issue>& issues, const Calendar& cal,
                                         const ScheduleOptions& opt);

struct Workload {
  std::string name;
  int n = 0;
  double weeks = 0;
  Day last = 0;
  std::vector<int> ids;
  int peak = 0;
  int wait = 0;
};
std::vector<Workload> workload(const Tree& T, const std::vector<Issue>& issues, const std::vector<ScheduleRow>& R,
                               const Calendar& cal);

struct Summary {
  int late = 0, tight = 0, done = 0, total = 0;
};
Summary summarize(const Tree& T, const std::vector<ScheduleRow>& R);

// 表示用: 遅れ=「−N週」、それ以外=「N日」、完了=「完了」（SPEC 10.12）
std::string floatText(const ScheduleRow& r);
enum class Risk { Done, Late, Tight, Ok };
Risk riskOf(int floatDays, bool done);

}  // namespace roadmap
