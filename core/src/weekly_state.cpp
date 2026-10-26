// 週次レポートの設定（所感・スナップショット）の保持と、settings.json の "weekly" との変換
#include "roadmap/weekly_state.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_set>

#include "roadmap/text.hpp"

namespace roadmap {

namespace {

using Json = nlohmann::ordered_json;

constexpr long long kMaxCount = 10000000;  // 件数として読む値の上限

bool validId(const std::string& id) {
  if (id.empty() || id.size() > kWeeklyMaxIdBytes) return false;
  for (char ch : id) {
    const auto b = static_cast<unsigned char>(ch);
    if (b < 0x20 || b == 0x7F) return false;
  }
  return text::isValidUtf8(id);
}

bool validState(char c) { return c == 't' || c == 'p' || c == 'd'; }

// 課題 1 件を 1 つの文字列にする: "<Issue Id>\t<状況>\t<完了日>\t<遅れ>"（settings.json で 1 課題 1 行にするため）
std::string itemToString(const WeeklySnapItem& it) {
  std::string s = it.id;
  s += '\t';
  s += it.st;
  s += '\t';
  s += it.finish ? fmtIso(*it.finish) : std::string();
  s += '\t';
  s += (it.late && it.st != 'd') ? '1' : '0';
  return s;
}

bool itemFromString(const std::string& s, WeeklySnapItem& out) {
  std::vector<std::string> parts;
  std::size_t from = 0;
  for (;;) {
    const std::size_t p = s.find('\t', from);
    if (p == std::string::npos) {
      parts.push_back(s.substr(from));
      break;
    }
    parts.push_back(s.substr(from, p - from));
    from = p + 1;
    if (parts.size() > 4) return false;
  }
  if (parts.size() != 4) return false;
  if (!validId(parts[0]) || parts[1].size() != 1 || !validState(parts[1][0])) return false;
  WeeklySnapItem it;
  it.id = parts[0];
  it.st = parts[1][0];
  if (!parts[2].empty()) {
    const OptDay d = parseIso(parts[2]);
    if (!d) return false;
    it.finish = d;
  }
  if (parts[3] != "0" && parts[3] != "1") return false;
  it.late = parts[3] == "1" && it.st != 'd';
  out = std::move(it);
  return true;
}

// 項目の検査（id・状況）と重複の除去、上限への切り詰め
void normalizeItems(WeeklySnapshot& s) {
  std::vector<WeeklySnapItem> kept;
  kept.reserve(std::min(s.items.size(), kWeeklyMaxSnapshotItems));
  std::unordered_set<std::string> seen;
  for (WeeklySnapItem& it : s.items) {
    if (!validId(it.id) || !validState(it.st)) continue;
    if (!seen.insert(it.id).second) continue;
    if (kept.size() >= kWeeklyMaxSnapshotItems) {
      s.truncated = true;
      break;
    }
    if (it.st == 'd') it.late = false;
    kept.push_back(std::move(it));
  }
  s.items = std::move(kept);
  // マイルストーン（Issue #75）: 同じ検査。上限を超えたぶんは持たない
  std::vector<WeeklySnapItem> ms;
  ms.reserve(std::min(s.milestones.size(), kWeeklyMaxSnapshotMilestones));
  std::unordered_set<std::string> seenMs;
  for (WeeklySnapItem& it : s.milestones) {
    if (!validId(it.id) || !validState(it.st)) continue;
    if (!seenMs.insert(it.id).second) continue;
    if (ms.size() >= kWeeklyMaxSnapshotMilestones) break;
    if (it.st == 'd') it.late = false;
    ms.push_back(std::move(it));
  }
  s.milestones = std::move(ms);
}

void clampCounts(WeeklySnapshot& s) {
  const auto cl = [](int v) { return std::max(0, std::min(v, static_cast<int>(kMaxCount))); };
  s.tasks = cl(s.tasks);
  s.done = cl(s.done);
  s.late = cl(s.late);
  s.tight = cl(s.tight);
  if (!std::isfinite(s.progress)) s.progress = 0;
  s.progress = std::max(0.0, std::min(1.0, s.progress));
}

bool readCount(const Json& j, const char* key, int& out) {
  if (!j.contains(key)) return true;  // 欠けていれば 0
  const Json& v = j[key];
  if (!v.is_number_integer()) return false;
  const long long n = v.get<long long>();
  if (n < 0 || n > kMaxCount) return false;
  out = static_cast<int>(n);
  return true;
}

bool isKnownSnapshotKey(const std::string& k) {
  static const char* const kKnown[] = {"base", "progress", "tasks", "done", "late", "tight", "finish", "truncated", "items", "ms"};
  for (const char* x : kKnown)
    if (k == x) return true;
  return false;
}

bool snapshotFromJson(const Json& j, WeeklySnapshot& out) {
  if (!j.is_object()) return false;
  WeeklySnapshot s;
  if (!j.contains("base") || !j["base"].is_string()) return false;
  const OptDay base = parseIso(j["base"].get<std::string>());
  if (!base) return false;
  s.base = *base;
  if (j.contains("progress")) {
    if (!j["progress"].is_number()) return false;
    const double p = j["progress"].get<double>();
    if (!std::isfinite(p)) return false;
    s.progress = std::max(0.0, std::min(1.0, p));
  }
  if (!readCount(j, "tasks", s.tasks) || !readCount(j, "done", s.done) || !readCount(j, "late", s.late) ||
      !readCount(j, "tight", s.tight))
    return false;
  if (j.contains("finish") && !j["finish"].is_null()) {
    if (!j["finish"].is_string()) return false;
    const std::string f = j["finish"].get<std::string>();
    if (!f.empty()) {
      const OptDay d = parseIso(f);
      if (!d) return false;
      s.finish = d;
    }
  }
  if (j.contains("truncated")) {
    if (!j["truncated"].is_boolean()) return false;
    s.truncated = j["truncated"].get<bool>();
  }
  if (j.contains("items")) {
    if (!j["items"].is_array()) return false;
    std::unordered_set<std::string> seen;
    for (const Json& x : j["items"]) {
      if (!x.is_string()) continue;  // 壊れた項目だけ読み飛ばす
      WeeklySnapItem it;
      if (!itemFromString(x.get<std::string>(), it)) continue;
      if (!seen.insert(it.id).second) continue;
      if (s.items.size() >= kWeeklyMaxSnapshotItems) {
        s.truncated = true;
        break;
      }
      s.items.push_back(std::move(it));
    }
  }
  if (j.contains("ms") && j["ms"].is_array()) {  // マイルストーン（Issue #75）。配列でなければ無視（スナップショットは読める）
    std::unordered_set<std::string> seenMs;
    for (const Json& x : j["ms"]) {
      if (!x.is_string()) continue;
      WeeklySnapItem it;
      if (!itemFromString(x.get<std::string>(), it)) continue;
      if (!seenMs.insert(it.id).second) continue;
      if (s.milestones.size() >= kWeeklyMaxSnapshotMilestones) break;
      s.milestones.push_back(std::move(it));
    }
  }
  // 未知のキーは捨てずに持つ（書き戻す）
  for (auto it = j.begin(); it != j.end(); ++it)
    if (!isKnownSnapshotKey(it.key())) s.extra[it.key()] = it.value();
  out = std::move(s);
  return true;
}

// 同じ週のものを置き換えて入れ、基準日の昇順にそろえる（手動・自動で共通）。上限を超えたぶんは古いものから落とす
void putSnapshot(std::vector<WeeklySnapshot>& snaps, WeeklySnapshot s, std::size_t maxSnaps) {
  clampCounts(s);
  normalizeItems(s);
  const Day week = weeklyMondayOf(s.base);
  snaps.erase(std::remove_if(snaps.begin(), snaps.end(), [&](const WeeklySnapshot& x) { return weeklyMondayOf(x.base) == week; }), snaps.end());
  snaps.push_back(std::move(s));
  std::stable_sort(snaps.begin(), snaps.end(), [](const WeeklySnapshot& a, const WeeklySnapshot& b) { return a.base < b.base; });
  while (snaps.size() > maxSnaps) snaps.erase(snaps.begin());
}

Json snapshotToJson(const WeeklySnapshot& s) {
  Json o = Json::object();
  o["base"] = fmtIso(s.base);
  o["progress"] = std::round(s.progress * 10000.0) / 10000.0;
  o["tasks"] = s.tasks;
  o["done"] = s.done;
  o["late"] = s.late;
  o["tight"] = s.tight;
  o["finish"] = s.finish ? Json(fmtIso(*s.finish)) : Json(nullptr);
  o["truncated"] = s.truncated;
  Json items = Json::array();
  for (const WeeklySnapItem& it : s.items) items.push_back(itemToString(it));
  o["items"] = std::move(items);
  if (!s.milestones.empty()) {  // 空なら書かない（マイルストーンのないプロジェクトの設定は今までと同じバイト列）
    Json ms = Json::array();
    for (const WeeklySnapItem& it : s.milestones) ms.push_back(itemToString(it));
    o["ms"] = std::move(ms);
  }
  if (s.extra.is_object())
    for (auto it = s.extra.begin(); it != s.extra.end(); ++it)
      if (!isKnownSnapshotKey(it.key())) o[it.key()] = it.value();  // 未知のキーはそのまま書き戻す
  return o;
}

}  // namespace

Day weeklyMondayOf(Day d) { return d - (dow(d) + 6) % 7; }

std::string weeklySanitizeNotes(const std::string& in) {
  const std::string s = text::sanitizeUtf8(in);
  std::string out;
  out.reserve(std::min(s.size(), kWeeklyMaxNoteBytes));
  for (std::size_t i = 0; i < s.size();) {
    const std::size_t before = i;
    const char32_t cp = text::decodeAt(s, i);
    if (cp == U'\r') continue;  // CRLF は LF にそろえる
    if (cp < 0x20 && cp != U'\n' && cp != U'\t') continue;
    if (cp == 0x7F || (cp >= 0x80 && cp <= 0x9F)) continue;
    if (cp == 0x2028 || cp == 0x2029 || (cp >= 0x202A && cp <= 0x202E) || (cp >= 0x2066 && cp <= 0x2069) || cp == 0xFEFF ||
        cp == 0xFFFE || cp == 0xFFFF)
      continue;
    if (out.size() + (i - before) > kWeeklyMaxNoteBytes) break;  // 文字の途中では切らない
    out.append(s, before, i - before);
  }
  return out;
}

void weeklyPutSnapshot(WeeklyMemo& m, WeeklySnapshot s) { putSnapshot(m.snaps, std::move(s), kWeeklyMaxSnapshots); }

const WeeklySnapshot* weeklyPreviousSnapshot(const WeeklyMemo& m, Day base) {
  const Day week = weeklyMondayOf(base);
  const WeeklySnapshot* best = nullptr;
  for (const WeeklySnapshot& s : m.snaps)
    if (weeklyMondayOf(s.base) < week && (!best || s.base > best->base)) best = &s;
  return best;
}

Json weeklyToJson(const WeeklyMemo& m) {
  Json j = m.extra.is_object() ? m.extra : Json::object();
  j.erase("notes");
  j.erase("memberNotes");
  j.erase("snaps");
  j["notes"] = m.notes;
  if (!m.memberNotes.empty()) {
    Json memberNotes = Json::object();
    std::size_t count = 0;
    for (const auto& [name, notes] : m.memberNotes) {
      if (name.size() > kWeeklyMaxAssigneeNameBytes || !text::isValidUtf8(name) || count >= kWeeklyMaxMemberNotes) continue;
      memberNotes[name] = weeklySanitizeNotes(notes);
      ++count;
    }
    if (!memberNotes.empty()) j["memberNotes"] = std::move(memberNotes);
  }
  Json snaps = Json::array();
  for (const WeeklySnapshot& s : m.snaps) snaps.push_back(snapshotToJson(s));
  j["snaps"] = std::move(snaps);
  return j;
}

WeeklyMemo weeklyFromJson(const Json& j) {
  WeeklyMemo m;
  if (!j.is_object()) return m;
  for (auto it = j.begin(); it != j.end(); ++it)
    if (it.key() != "notes" && it.key() != "memberNotes" && it.key() != "snaps") m.extra[it.key()] = it.value();
  if (j.contains("notes") && j["notes"].is_string()) m.notes = weeklySanitizeNotes(j["notes"].get<std::string>());
  if (j.contains("memberNotes") && j["memberNotes"].is_object()) {
    for (auto it = j["memberNotes"].begin(); it != j["memberNotes"].end() && m.memberNotes.size() < kWeeklyMaxMemberNotes; ++it) {
      const std::string name = text::trim(text::sanitizeUtf8(it.key()));
      if (name.size() > kWeeklyMaxAssigneeNameBytes || !text::isValidUtf8(name) || !it.value().is_string()) continue;
      m.memberNotes.emplace(name, weeklySanitizeNotes(it.value().get<std::string>()));
    }
  }
  if (j.contains("snaps") && j["snaps"].is_array()) {
    for (const Json& x : j["snaps"]) {
      WeeklySnapshot s;
      if (!snapshotFromJson(x, s)) continue;  // 壊れたスナップショットは読み飛ばす
      weeklyPutSnapshot(m, std::move(s));
    }
  }
  return m;
}

// ---------------------------------------------------------------- 自動のスナップショット（Issue #70）
bool weeklyAutoHasWeek(const WeeklyAuto& a, Day d) { return weeklyAutoOfWeek(a, weeklyMondayOf(d)) != nullptr; }

void weeklyAutoPut(WeeklyAuto& a, WeeklySnapshot s) { putSnapshot(a.snaps, std::move(s), kWeeklyAutoMaxSnapshots); }

const WeeklySnapshot* weeklyAutoOfWeek(const WeeklyAuto& a, Day monday) {
  const Day week = weeklyMondayOf(monday);
  for (const WeeklySnapshot& s : a.snaps)
    if (weeklyMondayOf(s.base) == week) return &s;
  return nullptr;
}

const WeeklySnapshot* weeklyAutoAtOrBefore(const WeeklyAuto& a, Day d) {
  const WeeklySnapshot* best = nullptr;
  for (const WeeklySnapshot& s : a.snaps)
    if (weeklyMondayOf(s.base) <= d && (!best || s.base > best->base)) best = &s;
  return best;
}

const WeeklySnapshot* weeklyAutoBefore(const WeeklyAuto& a, Day d) {
  const Day week = weeklyMondayOf(d);
  const WeeklySnapshot* best = nullptr;
  for (const WeeklySnapshot& s : a.snaps)
    if (weeklyMondayOf(s.base) < week && (!best || s.base > best->base)) best = &s;
  return best;
}

Json weeklyAutoToJson(const WeeklyAuto& a) {
  Json j = Json::object();
  Json snaps = Json::array();
  for (const WeeklySnapshot& s : a.snaps) snaps.push_back(snapshotToJson(s));
  j["snaps"] = std::move(snaps);
  if (a.extra.is_object())
    for (auto it = a.extra.begin(); it != a.extra.end(); ++it)
      if (it.key() != "snaps") j[it.key()] = it.value();  // 未知のキーはそのまま書き戻す
  return j;
}

WeeklyAuto weeklyAutoFromJson(const Json& j) {
  WeeklyAuto a;
  if (!j.is_object()) return a;
  for (auto it = j.begin(); it != j.end(); ++it)
    if (it.key() != "snaps") a.extra[it.key()] = it.value();
  if (!j.contains("snaps") || !j["snaps"].is_array()) return a;
  for (const Json& x : j["snaps"]) {
    WeeklySnapshot s;
    if (!snapshotFromJson(x, s)) continue;  // 壊れたスナップショットは読み飛ばす
    weeklyAutoPut(a, std::move(s));
  }
  return a;
}

}  // namespace roadmap
