// 絞り込みと可視行の列挙（SPEC 10.8。JS版 core.js の visSet / visibleRows / visibleLeaves）
#pragma once

#include <map>
#include <set>
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

// 列見出しの絞り込み。一致しない課題は隠す（上の絞り込みは薄くするだけ）
struct ColumnFilter {
  std::string name;                                   // 件名・表示ID・キーに含む文字（祖先が一致すれば配下も一致）
  std::map<std::string, std::set<std::string>> hide;  // 列のキー → 隠す値（columnValue の文字列）

  bool active() const;
  bool hidden(const std::string& col, const std::string& value) const;
  void toggle(const std::string& col, const std::string& value);
  void clear(const std::string& col);
};

// 列の絞り込みで使う、末端の課題の値。列のキーは type / status / asg / prio / float
inline constexpr const char* kNoneValue = "（なし）";
inline constexpr const char* kUnassignedValue = "（未割当）";
std::string columnValue(const std::string& col, const Issue& o, const ScheduleRow& r);
// 末端の課題に現れる値（重複なし・出現順）
std::vector<std::string> columnValues(const std::string& col, const Tree& T, const std::vector<Issue>& issues,
                                      const std::vector<ScheduleRow>& R);

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

// 条件に一致する課題（親は、一致する子孫があれば一致）。cf があれば両方を満たすもの
std::vector<char> visSet(const Tree& T, const std::vector<Issue>& issues, const std::vector<ScheduleRow>& R,
                         const Filter& f, const ColumnFilter* cf = nullptr);
std::vector<VisibleRow> visibleRows(const Tree& T, const std::vector<Issue>& issues, const std::vector<ScheduleRow>& R,
                                    const ViewState& vs, const Filter& f, const ColumnFilter* cf = nullptr);
std::vector<int> visibleLeaves(const Tree& T, const std::vector<Issue>& issues, const std::vector<ScheduleRow>& R,
                               const ViewState& vs, const Filter& f, const ColumnFilter* cf = nullptr);

}  // namespace roadmap
