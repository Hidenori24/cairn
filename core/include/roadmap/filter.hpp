// 絞り込みと可視行の列挙（SPEC 10.8。JS版 core.js の visSet / visibleRows / visibleLeaves）
#pragma once

#include <string>
#include <vector>

#include "roadmap/engine.hpp"
#include "roadmap/model.hpp"

namespace roadmap {

enum class AsgFilter { All, Assigned, Unassigned, Name };
enum class StFilter { All, Open, Todo, Prog, Done };
enum class FltFilter { None, Late, Tight, Done, Open };

struct Filter {
  std::string q;
  AsgFilter asg = AsgFilter::All;
  std::string asgName;
  StFilter st = StFilter::All;
  std::string pr;  // 優先度（小文字）。空 = すべて
  FltFilter flt = FltFilter::None;

  bool active() const {
    return !text_q_empty() || asg != AsgFilter::All || st != StFilter::All || !pr.empty() || flt != FltFilter::None;
  }
  bool text_q_empty() const;
};

struct VisibleRow {
  int id;
  int depth;
  bool isPar;
  bool open;
};

// 表示状態（settings.ui）から読むもの
struct ViewState {
  std::vector<char> collapsed;  // 添字ごと
  std::vector<char> hidden;     // ルート課題を出さない
  int level = 0;                // 0 = 制限なし
};
ViewState viewStateFromUi(const Tree& T, const Json& ui);

std::vector<char> visSet(const Tree& T, const std::vector<Issue>& issues, const std::vector<ScheduleRow>& R,
                         const Filter& f);
std::vector<VisibleRow> visibleRows(const Tree& T, const std::vector<Issue>& issues, const std::vector<ScheduleRow>& R,
                                    const ViewState& vs, const Filter& f);
std::vector<int> visibleLeaves(const Tree& T, const std::vector<Issue>& issues, const std::vector<ScheduleRow>& R,
                               const ViewState& vs, const Filter& f);

}  // namespace roadmap
