#include "roadmap/stamps.hpp"

#include <algorithm>
#include <cstddef>
#include <initializer_list>
#include <string_view>
#include <unordered_map>

#include "roadmap/text.hpp"

namespace roadmap {

namespace {

const std::initializer_list<const char*>& namesOf(StampKind kind) {
  static const std::initializer_list<const char*> kCreated = {"created", "作成日", "作成", "作成日時"};
  static const std::initializer_list<const char*> kUpdated = {"updated", "更新日", "更新", "更新日時"};
  return kind == StampKind::Created ? kCreated : kUpdated;
}

// 中身が同じか（Created・Updated の欄は見ない。extra は短いほうを空文字で伸ばして比べる）
bool sameContent(const Issue& a, const Issue& b, int c1, int c2) {
  if (a == b) return true;
  Issue x = a, y = b;
  const std::size_t n = std::max(x.extra.size(), y.extra.size());
  x.extra.resize(n);
  y.extra.resize(n);
  for (const int c : {c1, c2})
    if (c >= 0 && static_cast<std::size_t>(c) < n) {
      x.extra[static_cast<std::size_t>(c)].clear();
      y.extra[static_cast<std::size_t>(c)].clear();
    }
  return x == y;
}

int ensureColumn(ProjectMeta& meta, std::vector<Issue>& issues, StampKind kind) {
  int c = findStampColumn(meta, kind);
  if (c < 0) {
    meta.extraHeaders.push_back(stampHeader(kind));
    c = static_cast<int>(meta.extraHeaders.size()) - 1;
  }
  for (auto& o : issues)
    if (o.extra.size() < meta.extraHeaders.size()) o.extra.resize(meta.extraHeaders.size());
  return c;
}

const char* const kMon[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

bool isDigit(char c) { return c >= '0' && c <= '9'; }

// JIRA の日時の書式（19/Oct/26 9:00 AM。年は 2 桁か 4 桁、AM/PM は省略可）か。年の桁数と AM/PM の有無も返す
struct JiraDt {
  bool ok = false;
  int yearDigits = 2;
  bool ampm = true;
};
JiraDt parseJiraDt(std::string_view s) {
  JiraDt r;
  std::size_t i = 0;
  const auto digits = [&](std::size_t lo, std::size_t hi) {
    std::size_t n = 0;
    while (i < s.size() && isDigit(s[i])) {
      ++i;
      ++n;
    }
    return n >= lo && n <= hi ? n : std::size_t{0};
  };
  if (!digits(1, 2)) return r;
  if (i >= s.size() || s[i] != '/') return r;
  ++i;
  const std::size_t m0 = i;
  while (i < s.size() && ((s[i] >= 'A' && s[i] <= 'Z') || (s[i] >= 'a' && s[i] <= 'z'))) ++i;
  if (i - m0 != 3) return r;
  const std::string mon = text::asciiLower(std::string(s.substr(m0, 3)));
  bool known = false;
  for (const char* m : kMon) known |= text::asciiLower(m) == mon;
  if (!known) return r;
  if (i >= s.size() || s[i] != '/') return r;
  ++i;
  const std::size_t yn = digits(2, 4);
  if (yn != 2 && yn != 4) return r;
  r.yearDigits = static_cast<int>(yn);
  const std::size_t sp = i;
  while (i < s.size() && s[i] == ' ') ++i;
  if (i == sp) return r;
  if (!digits(1, 2)) return r;
  if (i >= s.size() || s[i] != ':') return r;
  ++i;
  if (digits(2, 2) != 2) return r;
  while (i < s.size() && s[i] == ' ') ++i;
  if (i == s.size()) {
    r.ampm = false;
    r.ok = true;
    return r;
  }
  if (s.size() - i == 2 && (s[i] == 'A' || s[i] == 'a' || s[i] == 'P' || s[i] == 'p') && (s[i + 1] == 'M' || s[i + 1] == 'm')) {
    r.ampm = true;
    r.ok = true;
  }
  return r;
}

std::string pad2(int v) { return (v < 10 ? "0" : "") + std::to_string(v); }

// Updated に書く文字。列の最初の値が JIRA の日時ならその書式（時刻は when）、それ以外は YYYY-MM-DD
std::string updatedText(const JiraDt& fmt, const StampWhen& when) {
  const std::string iso = fmtIso(when.day);  // YYYY-MM-DD
  if (!fmt.ok || iso.size() != 10 || iso[4] != '-' || iso[7] != '-') return iso;
  const int mo = (iso[5] - '0') * 10 + (iso[6] - '0');
  if (mo < 1 || mo > 12) return iso;
  const std::string out =
      iso.substr(8, 2) + "/" + kMon[mo - 1] + "/" + (fmt.yearDigits == 4 ? iso.substr(0, 4) : iso.substr(2, 2)) + " ";
  const int h = std::clamp(when.hour, 0, 23), mi = std::clamp(when.minute, 0, 59);
  if (fmt.ampm) {
    const int h12 = h % 12 == 0 ? 12 : h % 12;
    return out + std::to_string(h12) + ":" + pad2(mi) + (h >= 12 ? " PM" : " AM");
  }
  return out + std::to_string(h) + ":" + pad2(mi);
}

}  // namespace

const char* stampHeader(StampKind kind) { return kind == StampKind::Created ? "Created" : "Updated"; }

int findStampColumn(const ProjectMeta& meta, StampKind kind) {
  for (std::size_t i = 0; i < meta.extraHeaders.size(); ++i) {
    const std::string h = text::asciiLower(text::trim(meta.extraHeaders[i]));
    for (const char* n : namesOf(kind))
      if (h == n) return static_cast<int>(i);
  }
  return -1;
}

StampResult stampChanges(const std::vector<Issue>& before, ProjectMeta& meta, std::vector<Issue>& after, const StampWhen& when,
                         const std::unordered_set<std::string>* only) {
  StampResult res;
  const int c1 = findStampColumn(meta, StampKind::Created);
  const int c2 = findStampColumn(meta, StampKind::Updated);

  std::unordered_map<std::string_view, std::size_t> inBefore, inAfter;
  inBefore.reserve(before.size());
  inAfter.reserve(after.size());
  for (std::size_t i = 0; i < before.size(); ++i) inBefore.emplace(before[i].id, i);
  for (std::size_t i = 0; i < after.size(); ++i) inAfter.emplace(after[i].id, i);

  std::vector<std::size_t> fresh;             // 新しい課題（after の添字）
  std::vector<char> edited(after.size(), 0);  // 中身が変わった課題
  const auto markPartner = [&](const std::string& id) {
    const auto a = inAfter.find(id);
    if (a != inAfter.end() && inBefore.count(id)) edited[a->second] = 1;
  };
  for (std::size_t i = 0; i < after.size(); ++i) {
    const auto b = inBefore.find(after[i].id);
    if (b == inBefore.end()) {
      fresh.push_back(i);
      continue;
    }
    const Issue& was = before[b->second];
    const Issue& now = after[i];
    if (sameContent(was, now, c1, c2)) continue;
    edited[i] = 1;
    // つなぎの増減は、つないだ相手の課題（先行・後続）の編集でもある
    if (was.blocks != now.blocks) {
      for (const auto& t : now.blocks)
        if (std::find(was.blocks.begin(), was.blocks.end(), t) == was.blocks.end()) markPartner(t);
      for (const auto& t : was.blocks)
        if (std::find(now.blocks.begin(), now.blocks.end(), t) == now.blocks.end()) markPartner(t);
    }
  }
  std::vector<std::size_t> touched;
  for (std::size_t i = 0; i < after.size(); ++i)
    if (edited[i] && (!only || only->count(after[i].id))) touched.push_back(i);
  if (fresh.empty() && touched.empty()) return res;

  if (!fresh.empty()) {
    const int c = ensureColumn(meta, after, StampKind::Created);
    const std::string day = fmtIso(when.day);
    for (const std::size_t i : fresh) after[i].extra[static_cast<std::size_t>(c)] = day;
    res.created = static_cast<int>(fresh.size());
  }
  if (!touched.empty()) {
    // 書式は、列にすでにある最初の値で決める（なければ日付）
    JiraDt fmt;
    if (const int cu = findStampColumn(meta, StampKind::Updated); cu >= 0) {
      for (const auto& o : after) {
        if (static_cast<std::size_t>(cu) >= o.extra.size()) continue;
        const std::string v = text::trim(o.extra[static_cast<std::size_t>(cu)]);
        if (v.empty()) continue;
        fmt = parseJiraDt(v);
        break;
      }
    }
    const int c = ensureColumn(meta, after, StampKind::Updated);
    const std::string stamp = updatedText(fmt, when);
    for (const std::size_t i : touched) after[i].extra[static_cast<std::size_t>(c)] = stamp;
    res.updated = static_cast<int>(touched.size());
  }
  return res;
}

}  // namespace roadmap
