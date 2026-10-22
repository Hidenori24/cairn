// ベースライン比較（Issue #53）: 保存する日程の写し、settings.json の "baselines" との変換、いまの見込みとの比較
#include "roadmap/baseline.hpp"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

#include "roadmap/schedule.hpp"
#include "roadmap/text.hpp"

namespace roadmap {

namespace {

using Json = nlohmann::ordered_json;

constexpr std::size_t kMaxBaselineIdBytes = 32;
constexpr const char* kDefaultName = "ベースライン";

// 向きを変える文字・行区切り・制御文字（件名や名前に混ざると表示や 1 行 1 課題の保存形式を壊す）
bool isDroppedCp(char32_t cp) {
  if (cp < 0x20 || cp == 0x7F || (cp >= 0x80 && cp <= 0x9F)) return true;
  if (cp == 0x2028 || cp == 0x2029 || (cp >= 0x202A && cp <= 0x202E) || (cp >= 0x2066 && cp <= 0x2069)) return true;
  if (cp == 0x200E || cp == 0x200F || cp == 0x061C || cp == 0xFEFF || cp == 0xFFFE || cp == 0xFFFF) return true;
  return false;
}

// 壊れた UTF-8 は U+FFFD、除く文字は捨て、前後の空白を落とし、maxChars 文字までにする
std::string cleanLabel(const std::string& in, std::size_t maxChars) {
  const std::string s = text::sanitizeUtf8(in);
  std::string out;
  out.reserve(std::min<std::size_t>(s.size(), maxChars * 4));
  for (std::size_t i = 0; i < s.size();) {
    const char32_t cp = text::decodeAt(s, i);
    if (isDroppedCp(cp)) continue;
    text::appendUtf8(out, cp);
  }
  out = text::trim(out);
  if (text::codepointCount(out) > maxChars) {
    std::size_t i = 0, n = 0;
    while (i < out.size() && n < maxChars) {
      (void)text::decodeAt(out, i);
      ++n;
    }
    out.resize(i);
    out = text::trimEnd(out);
  }
  return out;
}

bool validIssueId(const std::string& id) {
  if (id.empty() || id.size() > kBaselineMaxIdBytes) return false;
  for (char ch : id) {
    const auto b = static_cast<unsigned char>(ch);
    if (b < 0x20 || b == 0x7F) return false;
  }
  return text::isValidUtf8(id);
}

bool validBaselineId(const std::string& id) {
  if (id.empty() || id.size() > kMaxBaselineIdBytes) return false;
  for (char ch : id) {
    const bool ok = (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_' || ch == '-';
    if (!ok) return false;
  }
  return true;
}

bool validState(char c) { return c == 't' || c == 'p' || c == 'd'; }

// "YYYY-MM-DDTHH:MM"（空は「分からない」）
bool validAt(const std::string& s) {
  if (s.empty()) return true;
  if (s.size() != 16 || s[10] != 'T' || s[13] != ':') return false;
  if (!parseIso(s.substr(0, 10))) return false;
  const auto dig = [&](std::size_t i) { return s[i] >= '0' && s[i] <= '9'; };
  if (!dig(11) || !dig(12) || !dig(14) || !dig(15)) return false;
  const int h = (s[11] - '0') * 10 + (s[12] - '0'), m = (s[14] - '0') * 10 + (s[15] - '0');
  return h < 24 && m < 60;
}

// 課題 1 件を 1 つの文字列にする: "<Issue Id>\t<状況>\t<親 0/1>\t<開始>\t<完了>\t<期限>\t<件名>"
std::string itemToString(const BaselineItem& it) {
  std::string s = it.id;
  s += '\t';
  s += it.st;
  s += '\t';
  s += it.parent ? '1' : '0';
  s += '\t';
  s += it.start ? fmtIso(*it.start) : std::string();
  s += '\t';
  s += it.finish ? fmtIso(*it.finish) : std::string();
  s += '\t';
  s += it.deadline ? fmtIso(*it.deadline) : std::string();
  s += '\t';
  s += it.title;
  return s;
}

// 空は「なし」。入っていて読めない日付は壊れた値（false）
bool readOptDay(const std::string& s, OptDay& out) {
  if (s.empty()) {
    out.reset();
    return true;
  }
  const OptDay d = parseIso(s);
  if (!d) return false;
  out = d;
  return true;
}

bool itemFromString(const std::string& s, BaselineItem& out) {
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
    if (parts.size() > 7) return false;
  }
  if (parts.size() != 7) return false;
  if (!validIssueId(parts[0]) || parts[1].size() != 1 || !validState(parts[1][0])) return false;
  if (parts[2] != "0" && parts[2] != "1") return false;
  BaselineItem it;
  it.id = parts[0];
  it.st = parts[1][0];
  it.parent = parts[2] == "1";
  if (!readOptDay(parts[3], it.start) || !readOptDay(parts[4], it.finish) || !readOptDay(parts[5], it.deadline)) return false;
  it.title = cleanLabel(parts[6], kBaselineMaxTitleChars);
  out = std::move(it);
  return true;
}

// 名前・日時・課題を検査して整える
void normalize(Baseline& b) {
  b.name = baselineSanitizeName(b.name);
  if (b.name.empty()) b.name = kDefaultName;
  if (!validAt(b.at)) b.at.clear();
  std::vector<BaselineItem> kept;
  kept.reserve(std::min(b.items.size(), kBaselineMaxItems));
  std::unordered_set<std::string> seen;
  for (BaselineItem& it : b.items) {
    if (!validIssueId(it.id) || !validState(it.st)) continue;
    if (!seen.insert(it.id).second) continue;
    if (kept.size() >= kBaselineMaxItems) {
      b.truncated = true;
      break;
    }
    it.title = cleanLabel(it.title, kBaselineMaxTitleChars);
    kept.push_back(std::move(it));
  }
  b.items = std::move(kept);
}

bool baselineFromJson(const Json& j, Baseline& out) {
  if (!j.is_object()) return false;
  Baseline b;
  if (!j.contains("id") || !j["id"].is_string()) return false;
  b.id = j["id"].get<std::string>();
  if (!validBaselineId(b.id)) return false;
  if (!j.contains("name") || !j["name"].is_string()) return false;
  b.name = j["name"].get<std::string>();
  if (j.contains("at")) {
    if (!j["at"].is_string()) return false;
    b.at = j["at"].get<std::string>();
    if (!validAt(b.at)) return false;
  }
  if (j.contains("truncated")) {
    if (!j["truncated"].is_boolean()) return false;
    b.truncated = j["truncated"].get<bool>();
  }
  if (!j.contains("items") || !j["items"].is_array()) return false;
  std::unordered_set<std::string> seen;
  for (const Json& x : j["items"]) {
    if (!x.is_string()) continue;  // 壊れた項目だけ読み飛ばす
    BaselineItem it;
    if (!itemFromString(x.get<std::string>(), it)) continue;
    if (!seen.insert(it.id).second) continue;
    if (b.items.size() >= kBaselineMaxItems) {
      b.truncated = true;
      break;
    }
    b.items.push_back(std::move(it));
  }
  b.name = baselineSanitizeName(b.name);
  if (b.name.empty()) b.name = kDefaultName;
  out = std::move(b);
  return true;
}

std::string newBaselineId(const BaselineSet& set) {
  for (int n = 1; n < 1000; ++n) {
    const std::string id = "b" + std::to_string(n);
    if (!baselineFind(set, id)) return id;
  }
  return "b" + std::to_string(set.list.size() + 1000);
}

// from から to までの営業日の数（両端を含む。from > to なら 0）
long workdaysInclusive(const Calendar& cal, long a, long b) {
  if (a > b) return 0;
  // 7 の倍数だけずらしても曜日は変わらない（負の日付も正にそろえる）
  const long shift = a < 0 ? ((-a + 6) / 7) * 7 : 0;
  const auto weekdaysUpTo = [](long n) -> long {  // 日 0〜n の平日の数
    if (n < 0) return 0;
    const long full = (n + 1) / 7;
    long c = full * 5;
    for (long d = full * 7; d <= n; ++d) {
      const int w = dow(static_cast<Day>(d));
      if (w >= 1 && w <= 5) ++c;
    }
    return c;
  };
  long wd = weekdaysUpTo(b + shift) - weekdaysUpTo(a + shift - 1);
  for (Day h : cal.holidays()) {
    if (h < a || h > b) continue;
    const int w = dow(h);
    if (w >= 1 && w <= 5) --wd;
  }
  return wd;
}

}  // namespace

// ---------------------------------------------------------------- 名前・保存・管理
std::string baselineSanitizeName(const std::string& s) { return cleanLabel(s, kBaselineMaxNameChars); }

Result<Baseline> captureBaseline(const std::vector<Issue>& issues, const Computed& computed, const std::string& name,
                                 const std::string& nowDT) {
  const Tree& T = computed.T;
  const std::size_t n = T.n > 0 ? static_cast<std::size_t>(T.n) : 0;
  if (issues.size() != n || computed.R.size() != n || T.ids.size() != n || T.kids.size() != n)
    return Error{"課題と日程が合っていないため、ベースラインを作れません"};
  Baseline b;
  b.name = baselineSanitizeName(name);
  if (b.name.empty()) b.name = kDefaultName;
  b.at = validAt(nowDT) ? nowDT : std::string();
  b.items.reserve(std::min(n, kBaselineMaxItems));
  std::unordered_set<std::string> seen;
  const bool byDfs = T.dfs.size() == n;
  const auto add = [&](std::size_t k) {
    if (!validIssueId(T.ids[k]) || !seen.insert(T.ids[k]).second) {
      b.truncated = true;  // 入らなかった課題がある: 「計画になかった」と数え違えないよう、不完全の印を付ける
      return true;
    }
    if (b.items.size() >= kBaselineMaxItems) {
      b.truncated = true;
      return false;
    }
    const ScheduleRow& row = computed.R[k];
    BaselineItem it;
    it.id = T.ids[k];
    it.st = row.done ? 'd' : row.prog ? 'p' : 't';
    it.parent = !T.kids[k].empty();
    it.start = row.asapStart;
    it.finish = row.asapFinish;
    it.deadline = it.parent ? T.ownDl[k] : T.dl[k];
    it.title = cleanLabel(T.name[k], kBaselineMaxTitleChars);
    b.items.push_back(std::move(it));
    return true;
  };
  if (byDfs) {
    for (int i : T.dfs)
      if (i >= 0 && static_cast<std::size_t>(i) < n && !add(static_cast<std::size_t>(i))) break;
  } else {
    for (std::size_t k = 0; k < n; ++k)
      if (!add(k)) break;
  }
  return b;
}

Result<std::string> baselineAdd(BaselineSet& set, Baseline b) {
  if (set.list.size() >= kBaselineMax)
    return Error{"ベースラインは " + std::to_string(kBaselineMax) + " つまで保存できます。不要なものを削除してから保存してください"};
  normalize(b);
  b.id = newBaselineId(set);
  const std::string id = b.id;
  set.list.push_back(std::move(b));
  return id;
}

Status baselineRename(BaselineSet& set, const std::string& id, const std::string& name) {
  for (Baseline& b : set.list) {
    if (b.id != id) continue;
    const std::string clean = baselineSanitizeName(name);
    if (clean.empty()) return Error{"名前を入力してください"};
    b.name = clean;
    return okStatus();
  }
  return Error{"ベースラインが見つかりません"};
}

bool baselineRemove(BaselineSet& set, const std::string& idArg) {
  // idArg が set.list の要素（set.list[0].id など）を指していても、erase のあとで別の値に変わらないよう値で持つ
  const std::string id = idArg;
  const auto it = std::find_if(set.list.begin(), set.list.end(), [&](const Baseline& b) { return b.id == id; });
  if (it == set.list.end()) return false;
  const bool wasShown = set.shown == id;
  set.list.erase(it);
  if (wasShown) set.shown.clear();
  return true;
}

const Baseline* baselineFind(const BaselineSet& set, const std::string& id) {
  for (const Baseline& b : set.list)
    if (b.id == id) return &b;
  return nullptr;
}

const Baseline* baselineShown(const BaselineSet& set) { return set.shown.empty() ? nullptr : baselineFind(set, set.shown); }

bool baselineSetShown(BaselineSet& set, const std::string& id) {
  if (id.empty()) {
    set.shown.clear();
    return true;
  }
  if (!baselineFind(set, id)) return false;
  set.shown = id;
  return true;
}

// ---------------------------------------------------------------- JSON
Json baselinesToJson(const BaselineSet& set) {
  Json j = Json::object();
  j["shown"] = set.shown;
  Json list = Json::array();
  for (const Baseline& b : set.list) {
    Json o = Json::object();
    o["id"] = b.id;
    o["name"] = b.name;
    o["at"] = b.at;
    o["truncated"] = b.truncated;
    Json items = Json::array();
    for (const BaselineItem& it : b.items) items.push_back(itemToString(it));
    o["items"] = std::move(items);
    list.push_back(std::move(o));
  }
  j["list"] = std::move(list);
  return j;
}

BaselineSet baselinesFromJson(const Json& j) {
  BaselineSet set;
  if (!j.is_object()) return set;
  if (j.contains("list") && j["list"].is_array()) {
    for (const Json& x : j["list"]) {
      if (set.list.size() >= kBaselineMax) break;
      Baseline b;
      if (!baselineFromJson(x, b)) continue;  // 壊れたベースラインは読み飛ばす
      if (baselineFind(set, b.id)) continue;  // id の重複は先のものを残す
      set.list.push_back(std::move(b));
    }
  }
  if (j.contains("shown") && j["shown"].is_string()) {
    const std::string id = j["shown"].get<std::string>();
    if (baselineFind(set, id)) set.shown = id;
  }
  return set;
}

// ---------------------------------------------------------------- 比較
const char* baselineStateText(BaselineState s) {
  switch (s) {
    case BaselineState::OnPlan: return "計画どおり";
    case BaselineState::Late: return "遅れ";
    case BaselineState::Early: return "早まり";
    case BaselineState::Added: return "計画になかった";
    case BaselineState::Removed: return "消えた";
    case BaselineState::NoDate: return "比較できない";
  }
  return "";
}

int baselineWorkdayDiff(const Calendar& cal, Day from, Day to) {
  if (to == from) return 0;
  if (to > from) return static_cast<int>(workdaysInclusive(cal, static_cast<long>(from) + 1, to));
  return -static_cast<int>(workdaysInclusive(cal, static_cast<long>(to) + 1, from));
}

Result<BaselineCompare> compareBaseline(const Baseline& baseline, const std::vector<Issue>& issues, const Computed& computed,
                                        const Calendar& cal) {
  const Tree& T = computed.T;
  const std::size_t n = T.n > 0 ? static_cast<std::size_t>(T.n) : 0;
  if (issues.size() != n || computed.R.size() != n || T.ids.size() != n || T.kids.size() != n)
    return Error{"課題と日程が合っていないため、比較できません"};
  BaselineCompare c;
  c.baselineId = baseline.id;
  c.baselineName = baseline.name;
  c.baselineAt = baseline.at;
  c.truncated = baseline.truncated;
  c.entries.resize(n);
  std::unordered_map<std::string, std::size_t> byId;
  byId.reserve(baseline.items.size() * 2);
  for (std::size_t q = 0; q < baseline.items.size(); ++q) byId.emplace(baseline.items[q].id, q);
  std::vector<char> used(baseline.items.size(), 0);

  const auto judge = [](int diff) { return diff > 0 ? BaselineState::Late : diff < 0 ? BaselineState::Early : BaselineState::OnPlan; };

  for (std::size_t k = 0; k < n; ++k) {
    const ScheduleRow& row = computed.R[k];
    BaselineEntry& e = c.entries[k];
    e.index = static_cast<int>(k);
    e.id = T.ids[k];
    e.done = row.done;
    e.parent = !T.kids[k].empty();
    e.nowStart = row.asapStart;
    e.nowFinish = row.asapFinish;
    const auto f = byId.find(e.id);
    if (f == byId.end()) {
      // 比べた相手が一部の課題だけなら、ないことは「計画になかった」とは言えない
      e.state = baseline.truncated ? BaselineState::NoDate : BaselineState::Added;
      continue;
    }
    const BaselineItem& it = baseline.items[f->second];
    used[f->second] = 1;
    e.planSt = it.st;
    e.planStart = it.start;
    e.planFinish = it.finish;
    e.planDeadline = it.deadline;
    if (it.start) {
      e.hasStartDiff = true;
      e.startDiff = baselineWorkdayDiff(cal, *it.start, row.asapStart);
      e.startDiffers = *it.start != row.asapStart;
    }
    if (!it.finish) {
      e.state = BaselineState::NoDate;
      continue;
    }
    e.hasFinishDiff = true;
    e.finishDiff = baselineWorkdayDiff(cal, *it.finish, row.asapFinish);
    e.finishDiffers = *it.finish != row.asapFinish;
    e.state = judge(e.finishDiff);
  }

  for (std::size_t q = 0; q < baseline.items.size(); ++q) {
    if (used[q]) continue;
    const BaselineItem& it = baseline.items[q];
    BaselineEntry e;
    e.id = it.id;
    e.title = it.title;
    e.state = BaselineState::Removed;
    e.parent = it.parent;
    e.planSt = it.st;
    e.planStart = it.start;
    e.planFinish = it.finish;
    e.planDeadline = it.deadline;
    c.removed.push_back(std::move(e));
  }

  // 集計は末端の課題だけ（親のまとめは子の差を言い換えたものなので数えない）
  std::vector<int> late;
  for (std::size_t k = 0; k < n; ++k) {
    if (!T.kids[k].empty()) continue;
    const BaselineEntry& e = c.entries[k];
    if (e.done) ++c.sum.doneNow;
    switch (e.state) {
      case BaselineState::OnPlan:
        ++c.sum.onPlan;
        if (e.finishDiffers) ++c.sum.sameDayDiffers;
        break;
      case BaselineState::Early: ++c.sum.early; break;
      case BaselineState::Late:
        ++c.sum.late;
        late.push_back(static_cast<int>(k));
        break;
      case BaselineState::Added: ++c.sum.added; break;
      case BaselineState::NoDate: ++c.sum.noDate; break;
      case BaselineState::Removed: break;
    }
  }
  for (const BaselineEntry& e : c.removed)
    if (!e.parent) ++c.sum.removed;
  c.sum.compared = c.sum.early + c.sum.onPlan + c.sum.late;
  c.lateTotal = static_cast<int>(late.size());
  const auto pos = [&](int i) { return T.dfsPos.size() == n ? T.dfsPos[static_cast<std::size_t>(i)] : i; };
  std::stable_sort(late.begin(), late.end(), [&](int a, int b) {
    const int da = c.entries[static_cast<std::size_t>(a)].finishDiff, db = c.entries[static_cast<std::size_t>(b)].finishDiff;
    if (da != db) return da > db;
    return pos(a) < pos(b);
  });
  if (late.size() > kBaselineTopN) late.resize(kBaselineTopN);
  c.topLate = std::move(late);
  return c;
}

// ---------------------------------------------------------------- 表示の文言
std::string baselineDiffText(const BaselineEntry& e) {
  switch (e.state) {
    case BaselineState::Added: return "新規";
    case BaselineState::Removed: return "消えた";
    case BaselineState::NoDate: return "-";
    default: break;
  }
  if (!e.hasFinishDiff) return "-";
  if (e.finishDiff > 0) return "+" + std::to_string(e.finishDiff) + "日";
  if (e.finishDiff < 0) return "-" + std::to_string(-static_cast<long long>(e.finishDiff)) + "日";
  return "±0日";
}

std::string baselineDiffMark(const BaselineEntry& e) { return e.state == BaselineState::Late ? "!" : std::string(); }

std::string baselineDiffNote(const BaselineEntry& e) {
  if (e.hasFinishDiff && e.finishDiff == 0 && e.finishDiffers) return "営業日の差は 0（休業日・土日をまたいだ日付の違い）";
  return {};
}

bool baselineShowLabel(const BaselineEntry& e) {
  switch (e.state) {
    case BaselineState::Late:
    case BaselineState::Early:
    case BaselineState::Added: return true;
    case BaselineState::OnPlan: return e.hasFinishDiff && e.finishDiffers;
    default: return false;
  }
}

std::string baselineColumnValue(const BaselineEntry& e) {
  switch (e.state) {
    case BaselineState::Late: return kBaselineValueLate;
    case BaselineState::Early: return kBaselineValueEarly;
    case BaselineState::OnPlan: return kBaselineValueOnPlan;
    case BaselineState::Added: return kBaselineValueAdded;
    default: return kBaselineValueNone;
  }
}

}  // namespace roadmap
