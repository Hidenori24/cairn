// ガントの座標計算（日付 → x、行 → y、依存の矢印の経路、バーの色の判断）。
// 画面（app/ui/view_gantt.cpp）と HTML への書き出し（report）が同じ関数を使う。ImGui には依存しない。
// 長さの単位は呼ぶ側の座標（画面は画素。scale は画面の倍率で、定数 6 や 7 に掛ける。書き出しは 1）。
#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "roadmap/baseline.hpp"
#include "roadmap/engine.hpp"
#include "roadmap/filter.hpp"

namespace roadmap {

struct GanttPoint {
  float x = 0, y = 0;
};

// ---------------------------------------------------------------- 時間軸
// 日付 → x。rs = 左端の日、hi = 右端の日、left = 日 rs の x
struct GanttAxis {
  Day rs = 0, hi = 0;
  float px = 6;  // 1日の幅
  float left = 0;
  float x(Day d) const { return left + static_cast<float>(d - rs) * px; }
};

// 時間軸が覆う日の範囲: lo = 基準日と最早開始の最小、hi = max(基準日+120, 最早完了+21, 最遅完了+21, 期限+21)
struct GanttDayRange {
  Day lo = 0, hi = 0;
};
// plan = 表示するベースラインとの比較（なければ nullptr）。あれば、その計画の開始日・完了日 + 21 も範囲に含める
// （日程が大きく動いても、計画のバーが軸の外に出ない）。課題の数と合わない比較は無視する。nullptr のときは従来と同じ
GanttDayRange ganttDayRange(const Tree& T, const std::vector<ScheduleRow>& R, Day base, const BaselineCompare* plan = nullptr);
// 左端 rs = lo を含む週の月曜日の 1 週前
Day ganttAxisStart(Day lo);

// 拡大率の名前（day / week / month / all）→ 1 日の幅。all は fitAvail（左の表を除いた表示幅 − 余白）÷ 日数
// （0.9 以上 12 以下。倍率は掛けない）。それ以外の名前は week
float ganttZoomPx(std::string_view zoom, float scale, float fitAvail, int days);
// 見出しの書き方。customPx（Ctrl+ホイールで変えた倍率 1 のときの 1 日の幅。0 = 段階のまま）が正ならその幅で決める
std::string ganttStyleZoom(std::string_view zoom, float customPx);
// 見出しの高さ（日・週は 2 段、月・全体は 1 段）
float ganttHeaderH(std::string_view styleZoom, float scale);
// 週の見出し（月曜の M/D）を何週おきに出すか（1 週の幅 weekW が狭いほど間引く）
int ganttWeekLabelEvery(float weekW, float scale);

// ---------------------------------------------------------------- 行
inline float ganttRowY(float bodyTop, float rowH, int i) { return bodyTop + rowH * static_cast<float>(i); }

// 行のバーが占める左右（依存のつなぎ方の当たり判定・矢印の根元に使う）。マイルストーンは菱形の幅（scale × 7 の両側）
struct GanttSpan {
  float xl = 0, xr = 0;
  float w = 0;  // 幅（xr = xl + w。最小幅 3 を満たした値。画面が丸め誤差なく描くために持つ）
};
GanttSpan ganttSpan(const GanttAxis& ax, const Tree& T, const ScheduleRow& r, int id, bool isPar, float scale);

// 通常の作業のバーの左右（開始日 s0 〜 完了日 f0。最小幅 3）
GanttSpan ganttTaskBar(const GanttAxis& ax, Day s0, Day f0);

struct GanttRowShape {
  enum class Kind { Task, Summary, Milestone } kind = Kind::Task;
  float x0 = 0, x1 = 0;   // Task・Summary のバー（最小幅 3）。x1 = x0 + w
  float w = 0;
  float msX = 0;          // Milestone の菱形の中心
  bool hasSlack = false;  // 余裕の影（バーの右。Task は斜線、Summary は細い帯）
  float slackX0 = 0, slackX1 = 0;
  bool hasOver = false;   // 遅れの赤い帯（Task のみ。最遅完了 < 最早完了）
  float overX0 = 0, overX1 = 0;
  bool hasDeadline = false;
  float dlX = 0;          // 期限の菱形の中心
};
GanttRowShape ganttRowShape(const GanttAxis& ax, const Tree& T, const ScheduleRow& r, int id, bool isPar);

// ---------------------------------------------------------------- ベースライン（計画のバー。Issue #53）
// 現在のバーの下に出す薄い「計画のバー」と、ずれた分（営業日）の文字の位置。縦は行の上端からの位置（rowH は呼ぶ側の行の高さ）
struct GanttPlanShape {
  enum class Kind { None, Bar, Milestone } kind = Kind::None;
  float x0 = 0, x1 = 0, w = 0;  // Bar: 計画の開始〜完了（最小幅 3）。x1 = x0 + w
  float msX = 0;                // Milestone: 計画の菱形の中心
  float labelX = 0;             // 差の文字を置く左端（現在のバー・余裕の影・計画のバーの右端のうち一番右の外）
  float y0 = 0, y1 = 0;         // 行の上端からの位置（rowH から求める）
};
// 現在の形 cur と、課題の比較 e から計画の形を作る。isMs = マイルストーン（末端で Ms）。計画に日付がなければ kind = None
GanttPlanShape ganttPlanShape(const GanttAxis& ax, const GanttRowShape& cur, const BaselineEntry& e, bool isMs, float scale,
                              float rowH = 0);

// ---------------------------------------------------------------- 依存の矢印
// 表示している行の番号（Tree の添字 → 行。表示していなければ -1）
std::vector<int> ganttRowIndex(const Tree& T, const std::vector<VisibleRow>& rows);
// 葉 leaf を表す表示行（折りたたまれていれば最も近い祖先の行）。なければ -1
int ganttVisibleRow(const Tree& T, const std::vector<int>& rowOf, int leaf);

// 表示している行どうしをつなぐ先行 → 後続の対。行の範囲 [first, last] にかかるものだけ
struct GanttPair {
  int leaf = 0, succ = 0;  // 葉どうし（Tree の添字）
  int ra = 0, rb = 0;      // 表示行
  bool conflict = false;   // 後続が先行より前の日付
  bool critical = false;   // 余裕 0 以下の連鎖
};
void ganttForEachPair(const Tree& T, const std::vector<ScheduleRow>& R, const std::vector<int>& rowOf, int first, int last,
                      const std::function<void(const GanttPair&)>& fn);

// 行どうしにまとめた矢印（同じ行 → 行の重複は 1 本）。行の昇順
struct GanttLink {
  int a = 0, b = 0;   // 表示行
  int succLeaf = 0;   // 色の判断に使う後続の葉（最初に見つかったもの）
  bool conflict = false, critical = false;
};
std::vector<GanttLink> ganttLinks(const Tree& T, const std::vector<ScheduleRow>& R, const std::vector<int>& rowOf, int first,
                                  int last);

// 矢印の両端: y0 / y1 は先行・後続の行の中心の y。マイルストーンは菱形の外側に付ける
struct GanttArrowEnds {
  float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
};
GanttArrowEnds ganttArrowEnds(const GanttAxis& ax, const Tree& T, const std::vector<ScheduleRow>& R, int ia, int ib, float cy0,
                              float cy1, float scale);
// 折れ線の経路。終点が始点の右 12 以上なら「右へ → 縦 → 右へ」、そうでなければ半行ぶんの迂回
std::vector<GanttPoint> ganttArrowRoute(const GanttArrowEnds& e, float rowH, float scale);

// ---------------------------------------------------------------- 色
// ルート課題の色番号（子を持つか Epic のルートだけ 1〜7、ほかは 0。JS版 rootIndexMap）
std::vector<int> rootIndexMap(const Tree& T, const std::vector<Issue>& issues);
// 担当者名のハッシュ（h = h*31 + ch を 32bit 符号なしで）
unsigned nameHash(const std::string& s);

enum class GanttColorBy { Root, Float, Assignee };
GanttColorBy ganttColorByFromKey(std::string_view key);  // root / float / asg。不明は Root
const char* ganttColorByKey(GanttColorBy by);

// バーの色の判断。Palette は index（0〜7）のルート色
struct GanttBarColor {
  enum class Kind { Palette, Accent, Late, TightEdge } kind = Kind::Palette;
  int index = 0;
};
GanttBarColor ganttBarColor(GanttColorBy by, Risk risk, int rootIdx, const std::string& assignee);

}  // namespace roadmap
