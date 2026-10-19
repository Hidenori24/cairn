#include "roadmap/csv.hpp"

#include <algorithm>
#include <cstdlib>
#include <unordered_map>
#include <unordered_set>

#include "roadmap/text.hpp"

namespace roadmap::csv {

using text::isJsSpace;

// ---------------------------------------------------------------- 字句解析
Table parse(std::string_view s) {
  if (s.size() >= 3 && static_cast<unsigned char>(s[0]) == 0xEF && static_cast<unsigned char>(s[1]) == 0xBB &&
      static_cast<unsigned char>(s[2]) == 0xBF)
    s.remove_prefix(3);
  std::string_view first = s.substr(0, s.find('\n'));
  if (!first.empty() && first.size() < s.size() && first.back() == '\r') first.remove_suffix(1);
  const auto cnt = [&](char ch) { return std::count(first.begin(), first.end(), ch); };
  char delim = ',';
  if (cnt('\t') > cnt(',')) delim = '\t';
  else if (cnt(';') > cnt(',')) delim = ';';

  std::vector<std::vector<std::string>> rows;
  std::vector<std::string> row;
  std::string cur;
  bool q = false;
  for (std::size_t i = 0; i < s.size(); ++i) {
    const char c = s[i];
    if (q) {
      if (c == '"') {
        if (i + 1 < s.size() && s[i + 1] == '"') {
          cur += '"';
          ++i;
        } else {
          q = false;
        }
      } else {
        cur += c;
      }
    } else if (c == '"') {
      q = true;
    } else if (c == delim) {
      row.push_back(std::move(cur));
      cur.clear();
    } else if (c == '\n' || c == '\r') {
      if (c == '\r' && i + 1 < s.size() && s[i + 1] == '\n') ++i;
      row.push_back(std::move(cur));
      cur.clear();
      rows.push_back(std::move(row));
      row.clear();
    } else {
      cur += c;
    }
  }
  if (!cur.empty() || !row.empty()) {
    row.push_back(std::move(cur));
    rows.push_back(std::move(row));
  }
  Table t;
  bool haveHeader = false;
  for (auto& r : rows) {
    const bool any = std::any_of(r.begin(), r.end(), [](const std::string& x) { return !text::trim(x).empty(); });
    if (!any) continue;
    if (!haveHeader) {
      t.header = std::move(r);
      haveHeader = true;
    } else {
      t.rows.push_back(std::move(r));
    }
  }
  return t;
}

std::string stringify(const std::vector<std::vector<std::string>>& rows) {
  std::string out;
  for (const auto& r : rows) {
    for (std::size_t i = 0; i < r.size(); ++i) {
      if (i) out += ',';
      const std::string& v = r[i];
      if (v.find_first_of("\",\r\n") != std::string::npos) {
        out += '"';
        out += text::replaceAll(v, "\"", "\"\"");
        out += '"';
      } else {
        out += v;
      }
    }
    out += "\r\n";
  }
  if (rows.empty()) out = "\r\n";
  return out;
}

std::string norm(std::string_view s) {
  const std::string low = text::toLower(s);
  std::string out;
  bool inSp = false;
  for (std::size_t i = 0; i < low.size();) {
    const std::size_t st = i;
    const char32_t c = text::decodeAt(low, i);
    if (isJsSpace(c) || c == '_') {
      if (!inSp) out += ' ';
      inSp = true;
    } else {
      out.append(low, st, i - st);
      inSp = false;
    }
  }
  return text::trim(out);
}

// ---------------------------------------------------------------- 値の読み取り
namespace {

bool isDigit(char c) { return c >= '0' && c <= '9'; }
bool isAlpha(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }
int toInt(std::string_view s) {
  int v = 0;
  for (char c : s) v = v * 10 + (c - '0');
  return v;
}
std::size_t digitRun(std::string_view s, std::size_t i) {
  std::size_t j = i;
  while (j < s.size() && isDigit(s[j])) ++j;
  return j - i;
}
std::size_t alphaRun(std::string_view s, std::size_t i) {
  std::size_t j = i;
  while (j < s.size() && isAlpha(s[j])) ++j;
  return j - i;
}

// JS の Date.UTC と同じく、0〜99年は 1900 年代として扱う
OptDay mk(int y, int m, int d) {
  if (y >= 0 && y <= 99) y += 1900;
  return makeDay(y, m, d);
}

int monthOf(std::string_view letters) {
  static const char* const kMon[] = {"jan", "feb", "mar", "apr", "may", "jun", "jul", "aug", "sep", "oct", "nov", "dec"};
  const std::string l = text::asciiLower(letters.substr(0, 3));
  for (int i = 0; i < 12; ++i)
    if (l == kMon[i]) return i + 1;
  return 0;
}

// 区切り [-/.年] / [-/.月] を読み、読めたらバイト数を返す（0 = 不一致）
std::size_t sepAt(std::string_view s, std::size_t i, const char* kanji) {
  if (i >= s.size()) return 0;
  if (s[i] == '-' || s[i] == '/' || s[i] == '.') return 1;
  const std::string_view k(kanji);
  if (s.substr(i, k.size()) == k) return k.size();
  return 0;
}

}  // namespace

OptDay parseDate(std::string_view in) {
  const std::string str = text::trim(in);
  const std::string_view s = str;
  if (s.empty()) return std::nullopt;
  // 1) YYYY[-/.年]M[-/.月]D
  if (digitRun(s, 0) >= 4) {
    const std::size_t s1 = sepAt(s, 4, "年");
    if (s1) {
      const std::size_t mStart = 4 + s1;
      const std::size_t mr = digitRun(s, mStart);
      for (std::size_t ml = std::min<std::size_t>(mr, 2); ml >= 1; --ml) {
        const std::size_t s2 = sepAt(s, mStart + ml, "月");
        if (!s2) continue;
        const std::size_t dStart = mStart + ml + s2;
        const std::size_t dr = std::min<std::size_t>(digitRun(s, dStart), 2);
        if (dr == 0) continue;
        return mk(toInt(s.substr(0, 4)), toInt(s.substr(mStart, ml)), toInt(s.substr(dStart, dr)));
      }
    }
  }
  // 2) D[/- ]Mon[/- ,.]*YY(YY)
  {
    const std::size_t dr = digitRun(s, 0);
    if (dr >= 1 && dr <= 2 && dr < s.size() && (s[dr] == '/' || s[dr] == '-' || s[dr] == ' ')) {
      const std::size_t a = dr + 1;
      const std::size_t L = alphaRun(s, a);
      if (L >= 3 && L <= 9) {
        std::size_t j = a + L;
        while (j < s.size() && (s[j] == '/' || s[j] == '-' || s[j] == ' ' || s[j] == ',' || s[j] == '.')) ++j;
        const std::size_t yr = std::min<std::size_t>(digitRun(s, j), 4);
        if (yr >= 2) {
          const int mo = monthOf(s.substr(a, L));
          int y = toInt(s.substr(j, yr));
          if (y < 100) y += 2000;
          return mo ? mk(y, mo, toInt(s.substr(0, dr))) : std::nullopt;
        }
      }
    }
  }
  // 3) Mon[.] D[,] YYYY
  {
    const std::size_t L = alphaRun(s, 0);
    if (L >= 3 && L <= 9) {
      std::size_t j = L;
      if (j < s.size() && s[j] == '.') ++j;
      if (j < s.size() && s[j] == ' ') {
        ++j;
        const std::size_t dr = digitRun(s, j);
        if (dr >= 1 && dr <= 2) {
          std::size_t k = j + dr;
          if (k < s.size() && s[k] == ',') ++k;
          if (k < s.size() && s[k] == ' ' && digitRun(s, k + 1) >= 4) {
            const int mo = monthOf(s.substr(0, L));
            return mo ? mk(toInt(s.substr(k + 1, 4)), mo, toInt(s.substr(j, dr))) : std::nullopt;
          }
        }
      }
    }
  }
  return std::nullopt;
}

std::optional<double> parseDur(std::string_view in, bool plainIsWeeks) {
  const std::string str = text::trim(in);
  const std::string_view s = str;
  if (s.empty()) return std::nullopt;
  // /^\d+(\.\d+)?$/
  {
    const std::size_t a = digitRun(s, 0);
    bool plain = a > 0 && a == s.size();
    if (a > 0 && a < s.size() && s[a] == '.') {
      const std::size_t b = digitRun(s, a + 1);
      plain = b > 0 && a + 1 + b == s.size();
    }
    if (plain) {
      const double n = std::strtod(str.c_str(), nullptr);
      if (plainIsWeeks) return n;
      return n >= 600 ? n / (8.0 * 3600 * 5) : n;
    }
  }
  // /(\d+(?:\.\d+)?)\s*(w|d|h|m|週間?|日|時間|分)/gi を左から順に
  double tot = 0;
  bool hit = false;
  std::size_t i = 0;
  while (i < s.size()) {
    if (!isDigit(s[i])) {
      ++i;
      continue;
    }
    const std::size_t a = digitRun(s, i);
    std::size_t numEnds[2];
    int nEnds = 0;
    if (i + a < s.size() && s[i + a] == '.') {
      const std::size_t b = digitRun(s, i + a + 1);
      if (b) numEnds[nEnds++] = i + a + 1 + b;
    }
    numEnds[nEnds++] = i + a;
    bool matched = false;
    for (int e = 0; e < nEnds && !matched; ++e) {
      const std::size_t ne = numEnds[e];
      std::size_t j = ne;
      while (j < s.size()) {
        std::size_t k = j;
        if (!isJsSpace(text::decodeAt(s, k))) break;
        j = k;
      }
      double mult = 0;
      std::size_t ulen = 0;
      if (j < s.size()) {
        const char c = static_cast<char>(s[j] | 0x20);
        if (c == 'w') { mult = 1; ulen = 1; }
        else if (c == 'd') { mult = 1.0 / 5; ulen = 1; }
        else if (c == 'h') { mult = 1.0 / 40; ulen = 1; }
        else if (c == 'm') { mult = 1.0 / 2400; ulen = 1; }
        else if (s.substr(j, 6) == "週間") { mult = 1; ulen = 6; }
        else if (s.substr(j, 3) == "週") { mult = 1; ulen = 3; }
        else if (s.substr(j, 3) == "日") { mult = 1.0 / 5; ulen = 3; }
        else if (s.substr(j, 6) == "時間") { mult = 1.0 / 40; ulen = 6; }
        else if (s.substr(j, 3) == "分") { mult = 1.0 / 2400; ulen = 3; }
      }
      if (ulen) {
        const double v = std::strtod(std::string(s.substr(i, ne - i)).c_str(), nullptr);
        // JS と同じ順で計算する（v / 5 など）
        if (mult == 1) tot += v;
        else if (mult == 1.0 / 5) tot += v / 5;
        else if (mult == 1.0 / 40) tot += v / 40;
        else tot += v / 2400;
        hit = true;
        matched = true;
        i = j + ulen;
      }
    }
    if (!matched) ++i;
  }
  if (!hit) return std::nullopt;
  return tot;
}

namespace {
std::string pad2(int n) { return (n < 10 && n >= 0 ? "0" : "") + std::to_string(n); }
const char* const kMonName[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
}  // namespace

std::string parseDT(std::string_view in) {
  const std::string str = text::trim(in);
  const std::string_view s = str;
  const OptDay d = parseDate(s);
  if (!d) return "";
  int hh = 0, mm = 0;
  for (std::size_t i = 0; i < s.size(); ++i) {
    if (!isDigit(s[i])) continue;
    const std::size_t run = digitRun(s, i);
    bool found = false;
    for (std::size_t hl = std::min<std::size_t>(run, 2); hl >= 1 && !found; --hl) {
      const std::size_t c = i + hl;
      if (c < s.size() && s[c] == ':' && c + 2 < s.size() + 0 && isDigit(s[c + 1]) && c + 2 < s.size() && isDigit(s[c + 2])) {
        hh = toInt(s.substr(i, hl));
        mm = toInt(s.substr(c + 1, 2));
        std::size_t j = c + 3;
        while (j < s.size()) {
          std::size_t k = j;
          if (!isJsSpace(text::decodeAt(s, k))) break;
          j = k;
        }
        if (j + 1 < s.size() && (s[j + 1] == 'M' || s[j + 1] == 'm')) {
          const char a = static_cast<char>(s[j] | 0x20);
          if (a == 'a' || a == 'p') {
            const bool pm = a == 'p';
            if (pm && hh < 12) hh += 12;
            if (!pm && hh == 12) hh = 0;
          }
        }
        found = true;
      }
    }
    if (found) break;
  }
  return fmtIso(*d) + "T" + pad2(hh) + ":" + pad2(mm);
}

std::string fmtDT(std::string_view s) {
  // ^(\d{4})-(\d{2})-(\d{2})T(\d{2}):(\d{2})$
  if (s.size() != 16 || s[4] != '-' || s[7] != '-' || s[10] != 'T' || s[13] != ':') return "";
  for (int i : {0, 1, 2, 3, 5, 6, 8, 9, 11, 12, 14, 15})
    if (!isDigit(s[i])) return "";
  const int mon = toInt(s.substr(5, 2));
  const int hh = toInt(s.substr(11, 2));
  const char* ap = hh >= 12 ? "PM" : "AM";
  const int h12 = hh % 12 == 0 ? 12 : hh % 12;
  const std::string monName = (mon >= 1 && mon <= 12) ? kMonName[mon - 1] : "undefined";
  return std::string(s.substr(8, 2)) + "/" + monName + "/" + std::string(s.substr(2, 2)) + " " + pad2(h12) + ":" +
         std::string(s.substr(14, 2)) + " " + ap;
}

Comment parseComment(std::string_view cell) {
  const std::size_t a = cell.find(';');
  if (a != std::string_view::npos) {
    const std::size_t b = cell.find(';', a + 1);
    if (b != std::string_view::npos) {
      const std::string_view dt = cell.substr(0, a);
      const OptDay d = parseDate(dt);
      if (d && *d != 0) {  // JS では 0（1970-01-01）は偽になる
        return {parseDT(dt), text::trim(cell.substr(a + 1, b - a - 1)), std::string(cell.substr(b + 1))};
      }
    }
  }
  return {"", "", std::string(cell)};
}

std::string fmtComment(const Comment& c) { return fmtDT(c.at) + ";" + c.who + ";" + c.text; }

std::string fmtDate(const OptDay& n) { return n ? fmtIso(*n) : std::string(); }

std::string fmtNum(double w) { return text::jsNumber(text::jsRound(w * 100) / 100); }

std::string fmtEst(const std::optional<double>& wo) {
  if (!wo || *wo <= 0) return "";
  const double w = *wo;
  const double d = text::jsRound(w * 5 * 100) / 100;
  if (std::abs(w - text::jsRound(w)) < 1e-9) return text::jsNumber(text::jsRound(w)) + "w";
  if (std::abs(d - text::jsRound(d)) < 1e-9) return text::jsNumber(text::jsRound(d)) + "d";
  return text::jsNumber(std::max(1.0, text::jsRound(w * 40))) + "h";
}

std::string emptyCsv() {
  return "Issue Id,Issue Type,Summary,Parent,Priority,Status,Start date,Due date,Labels,Description,"
         "Outward issue link (Blocks),Deadline,Duration (weeks)\r\n";
}

// ---------------------------------------------------------------- 取り込み
namespace {

using Pred = bool (*)(const std::string&);

bool eqAny(const std::string& h, std::initializer_list<const char*> syn) {
  for (const char* s : syn)
    if (h == norm(s)) return true;
  return false;
}

struct Cols {
  std::vector<std::size_t> id, key, type, sum, parent, epicLink, epicName, prio, status, assignee, earliest, comment,
      start, due, deadline, resolved, est, remW, remE, labels, desc, bOut, bIn;
};

bool startsWithAfter(const std::string& h, std::string_view prefix, std::string_view needle) {
  return h.compare(0, prefix.size(), prefix) == 0 && h.find(needle, prefix.size()) != std::string::npos;
}

struct DurCol {
  std::size_t i;
  bool hasScn;
  std::string scn;
};

std::optional<DurCol> durColOf(std::size_t i, const std::string& raw) {
  const std::string t = text::trim(raw);
  std::size_t plen = 0;
  if (text::istartsWith(t, "duration (weeks)")) plen = 16;
  else if (t.rfind("期間(週)", 0) == 0) plen = std::string_view("期間(週)").size();
  else if (t.rfind("期間（週）", 0) == 0) plen = std::string_view("期間（週）").size();
  else return std::nullopt;
  const std::string rest = text::trimStart(std::string_view(t).substr(plen));
  if (rest.empty()) return DurCol{i, false, ""};
  if (rest[0] != '@') return std::nullopt;
  const std::string scn = text::trimStart(std::string_view(rest).substr(1));
  if (scn.empty()) return std::nullopt;
  for (std::size_t k = 0; k < scn.size();)
    if (text::isLineTerminator(text::decodeAt(scn, k))) return std::nullopt;
  return DurCol{i, true, text::trim(scn)};
}

struct Raw {
  Issue o;
  std::string parentRef, epicLinkRef, origId;
  std::vector<std::string> out, in;
};

}  // namespace

Result<ImportResult> importCsv(std::string_view textIn) {
  const Table parsed = parse(textIn);
  const auto& H = parsed.header;
  const auto& R = parsed.rows;
  std::vector<std::string> nH;
  nH.reserve(H.size());
  for (const auto& h : H) nH.push_back(norm(h));
  std::vector<std::string> warns;

  Cols col;
  for (std::size_t i = 0; i < nH.size(); ++i) {
    const std::string& h = nH[i];
    if (eqAny(h, {"issue id", "id", "課題id"})) col.id.push_back(i);
    if (eqAny(h, {"issue key", "key", "課題キー", "キー"})) col.key.push_back(i);
    if (eqAny(h, {"issue type", "issuetype", "type", "課題タイプ"})) col.type.push_back(i);
    if (eqAny(h, {"summary", "件名", "要約"})) col.sum.push_back(i);
    if (eqAny(h, {"parent", "parent id", "親", "親課題"})) col.parent.push_back(i);
    if (h == "epic link" || h == "custom field (epic link)" || h == "custom field (epic link" || h == "epic link)" ||
        eqAny(h, {"エピックリンク"}))
      col.epicLink.push_back(i);
    if (h.find("epic name") != std::string::npos || eqAny(h, {"エピック名"})) col.epicName.push_back(i);
    if (eqAny(h, {"priority", "優先度"})) col.prio.push_back(i);
    if (eqAny(h, {"status", "ステータス"})) col.status.push_back(i);
    if (eqAny(h, {"assignee", "担当者", "担当"})) col.assignee.push_back(i);
    if (eqAny(h, {"earliest start", "着手可能日", "start no earlier than"})) col.earliest.push_back(i);
    if (eqAny(h, {"comment", "コメント"})) col.comment.push_back(i);
    if (h.find("start date") != std::string::npos || eqAny(h, {"開始日"})) col.start.push_back(i);
    if (eqAny(h, {"due date", "due", "期限日", "期限"})) col.due.push_back(i);
    if (eqAny(h, {"deadline", "目標期限", "逆算期限"})) col.deadline.push_back(i);
    if (eqAny(h, {"resolved", "解決日", "resolution date"})) col.resolved.push_back(i);
    if (eqAny(h, {"original estimate", "original time estimate", "元の見積り", "初期見積り"})) col.est.push_back(i);
    if (eqAny(h, {"remaining (weeks)", "残り(週)", "残り（週）"})) col.remW.push_back(i);
    if (eqAny(h, {"remaining estimate", "残りの見積り"})) col.remE.push_back(i);
    if (eqAny(h, {"labels", "ラベル"})) col.labels.push_back(i);
    if (eqAny(h, {"description", "説明"})) col.desc.push_back(i);
    if (startsWithAfter(h, "outward issue link", "block") || startsWithAfter(h, "外向き", "block")) col.bOut.push_back(i);
    if (startsWithAfter(h, "inward issue link", "block") || startsWithAfter(h, "内向き", "block")) col.bIn.push_back(i);
  }
  if (col.sum.empty()) return Error{"「Summary」列が見つかりません。1行目に列名があるCSVを読み込んでください。"};

  std::vector<DurCol> durCols;
  for (std::size_t i = 0; i < H.size(); ++i)
    if (auto d = durColOf(i, H[i])) durCols.push_back(*d);
  std::vector<std::string> scenarios;
  for (const auto& d : durCols)
    if (d.hasScn && std::find(scenarios.begin(), scenarios.end(), d.scn) == scenarios.end()) scenarios.push_back(d.scn);

  std::vector<bool> known(H.size(), false);
  for (const auto* v : {&col.id, &col.key, &col.type, &col.sum, &col.parent, &col.epicLink, &col.epicName, &col.prio,
                        &col.status, &col.assignee, &col.earliest, &col.comment, &col.start, &col.due, &col.deadline,
                        &col.resolved, &col.est, &col.remW, &col.remE, &col.labels, &col.desc, &col.bOut, &col.bIn})
    for (std::size_t i : *v) known[i] = true;
  for (const auto& d : durCols) known[d.i] = true;
  std::vector<std::size_t> extraIdx;
  for (std::size_t i = 0; i < H.size(); ++i)
    if (!known[i]) extraIdx.push_back(i);

  const auto at = [](const std::vector<std::string>& r, std::size_t i) -> std::string_view {
    return i < r.size() ? std::string_view(r[i]) : std::string_view();
  };
  const auto cell = [&](const std::vector<std::string>& r, const std::vector<std::size_t>& idx) {
    for (std::size_t i : idx) {
      std::string v = text::trim(at(r, i));
      if (!v.empty()) return v;
    }
    return std::string();
  };
  const auto cells = [&](const std::vector<std::string>& r, const std::vector<std::size_t>& idx) {
    std::vector<std::string> out;
    for (std::size_t i : idx) {
      std::string v = text::trim(at(r, i));
      if (!v.empty()) out.push_back(std::move(v));
    }
    return out;
  };

  std::vector<Raw> raw;
  int skipped = 0;
  for (const auto& r : R) {
    std::string summary = cell(r, col.sum);
    if (summary.empty()) {
      ++skipped;
      continue;
    }
    Raw x;
    Issue& o = x.o;
    o.id = cell(r, col.id);
    o.key = cell(r, col.key);
    o.type = cell(r, col.type);
    if (o.type.empty()) o.type = "Task";
    o.summary = std::move(summary);
    x.parentRef = cell(r, col.parent);
    x.epicLinkRef = cell(r, col.epicLink);
    o.epicName = cell(r, col.epicName);
    o.prio = cell(r, col.prio);
    o.status = cell(r, col.status);
    o.assignee = cell(r, col.assignee);
    o.start = parseDate(cell(r, col.start));
    o.due = parseDate(cell(r, col.due));
    o.deadline = parseDate(cell(r, col.deadline));
    o.resolved = parseDate(cell(r, col.resolved));
    o.labels = cells(r, col.labels);
    o.desc = col.desc.empty() ? std::string() : text::trimEnd(at(r, col.desc[0]));
    x.out = cells(r, col.bOut);
    x.in = cells(r, col.bIn);
    o.notBefore = parseDate(cell(r, col.earliest));
    for (const auto& c : cells(r, col.comment)) o.comments.push_back(parseComment(c));
    for (std::size_t i : extraIdx) o.extra.emplace_back(at(r, i));
    auto rem = parseDur(cell(r, col.remW), true);
    if (!rem) rem = parseDur(cell(r, col.remE), false);
    o.remW = rem;
    o.estW = parseDur(cell(r, col.est), false);
    std::optional<double> dd;
    for (const auto& d : durCols)
      if (!d.hasScn && !dd) dd = parseDur(at(r, d.i), true);
    o.durW = dd;
    for (const auto& d : durCols)
      if (d.hasScn)
        if (auto v = parseDur(at(r, d.i), true)) o.alt.set(d.scn, *v);
    raw.push_back(std::move(x));
  }
  if (raw.empty()) return Error{"課題の行が見つかりません。"};

  // id の確定（空・重複は採番）
  double maxN = 0;
  for (const auto& x : raw)
    if (text::isAsciiDigits(x.o.id)) maxN = std::max(maxN, std::strtod(x.o.id.c_str(), nullptr));
  int dupIds = 0;
  std::unordered_set<std::string> seen;
  for (auto& x : raw) {
    x.origId = x.o.id;
    if (x.o.id.empty() || seen.count(x.o.id)) {
      if (!x.o.id.empty()) ++dupIds;
      maxN += 1;
      x.o.id = text::jsNumber(maxN);
    }
    seen.insert(x.o.id);
  }
  if (dupIds) warns.push_back("Issue Id が重複している行が" + std::to_string(dupIds) + "件あり、別のIDを振りました");

  std::unordered_map<std::string, std::size_t> byRef, epicRef;
  for (std::size_t i = 0; i < raw.size(); ++i)
    for (const std::string* k : {&raw[i].origId, &raw[i].o.key, &raw[i].o.id})
      if (!k->empty() && !byRef.count(*k)) byRef[*k] = i;
  for (std::size_t i = 0; i < raw.size(); ++i)
    if (isEpicType(raw[i].o.type))
      for (const std::string* k : {&raw[i].o.summary, &raw[i].o.epicName})
        if (!k->empty() && !epicRef.count(*k)) epicRef[*k] = i;

  std::vector<std::string> unresolved;
  const auto s20 = [](const std::string& s) { return text::utf16Prefix(s, 20); };
  for (std::size_t i = 0; i < raw.size(); ++i) {
    auto& x = raw[i];
    x.o.parent.clear();
    for (const std::string* v : {&x.parentRef, &x.epicLinkRef}) {
      if (v->empty() || !x.o.parent.empty()) continue;
      std::optional<std::size_t> p;
      if (auto it = byRef.find(*v); it != byRef.end()) p = it->second;
      else if (auto it2 = epicRef.find(*v); it2 != epicRef.end()) p = it2->second;
      if (p && *p != i) x.o.parent = raw[*p].o.id;
      else unresolved.push_back(s20(x.o.summary) + " → 親 " + *v);
    }
  }
  // 親の循環を切る
  std::unordered_map<std::string, std::size_t> byId;
  for (std::size_t i = 0; i < raw.size(); ++i) byId[raw[i].o.id] = i;
  for (auto& x : raw) {
    std::unordered_set<std::string> seenP;
    const Raw* p = &x;
    while (p && !p->o.parent.empty()) {
      if (seenP.count(p->o.id)) {
        warns.push_back("親子関係がループしていたため「" + s20(x.o.summary) + "」の親を外しました");
        x.o.parent.clear();
        break;
      }
      seenP.insert(p->o.id);
      auto it = byId.find(p->o.parent);
      p = it == byId.end() ? nullptr : &raw[it->second];
    }
  }
  // 依存関係（A が B をブロック = A が B の先行）
  const auto addBlock = [&](std::size_t from, std::size_t to) {
    if (from == to) return;
    auto& b = raw[from].o.blocks;
    if (std::find(b.begin(), b.end(), raw[to].o.id) == b.end()) b.push_back(raw[to].o.id);
  };
  for (std::size_t i = 0; i < raw.size(); ++i) {
    for (const auto& v : raw[i].out) {
      if (auto it = byRef.find(v); it != byRef.end()) addBlock(i, it->second);
      else unresolved.push_back(s20(raw[i].o.summary) + " → " + v);
    }
    for (const auto& v : raw[i].in) {
      if (auto it = byRef.find(v); it != byRef.end()) addBlock(it->second, i);
      else unresolved.push_back(v + " → " + s20(raw[i].o.summary));
    }
  }
  if (!unresolved.empty()) {
    std::vector<std::string> head(unresolved.begin(), unresolved.begin() + std::min<std::size_t>(5, unresolved.size()));
    std::string w = "参照先が見つからず無視: " + text::join(head, ", ");
    if (unresolved.size() > 5) w += " ほか" + std::to_string(unresolved.size() - 5) + "件";
    warns.push_back(std::move(w));
  }
  if (skipped) warns.push_back("Summary が空の行を" + std::to_string(skipped) + "件、読み飛ばしました");
  const std::pair<const std::vector<std::size_t>*, const char*> dateCols[] = {
      {&col.start, "Start date"}, {&col.due, "Due date"}, {&col.deadline, "Deadline"}};
  for (const auto& [idx, label] : dateCols) {
    if (idx->empty()) continue;
    int bad = 0;
    for (const auto& r : R) {
      const std::string v = cell(r, *idx);
      if (!v.empty() && !parseDate(v)) ++bad;
    }
    if (bad) warns.push_back("日付として読めない値が" + std::to_string(bad) + "件（" + label + "列）");
  }

  ImportResult res;
  res.issues.reserve(raw.size());
  for (auto& x : raw) res.issues.push_back(std::move(x.o));
  res.meta.hasDeadlineCol = !col.deadline.empty();
  res.meta.scenarios = std::move(scenarios);
  for (std::size_t i : extraIdx) res.meta.extraHeaders.push_back(H[i]);
  res.warns = std::move(warns);
  return res;
}

// ---------------------------------------------------------------- 書き出し
std::string exportCsv(const std::vector<Issue>& issues, const ProjectMeta& meta, const ExportOptions& opts) {
  std::unordered_map<std::string, std::size_t> byId;
  for (std::size_t i = 0; i < issues.size(); ++i) byId[issues[i].id] = i;
  std::vector<std::vector<std::size_t>> kids(issues.size());
  std::vector<std::size_t> roots;
  // kids は id ごと（同じ id が複数あれば最後のもの）
  for (std::size_t i = 0; i < issues.size(); ++i) {
    const Issue& o = issues[i];
    auto it = o.parent.empty() ? byId.end() : byId.find(o.parent);
    if (it != byId.end()) kids[it->second].push_back(i);
    else roots.push_back(i);
  }
  std::vector<std::size_t> order;
  order.reserve(issues.size());
  {
    // 深さ優先（親の直後に子）
    std::vector<std::pair<const std::vector<std::size_t>*, std::size_t>> st;
    st.emplace_back(&roots, 0);
    std::vector<bool> visited(issues.size(), false);
    while (!st.empty()) {
      auto& [list, pos] = st.back();
      if (pos >= list->size()) {
        st.pop_back();
        continue;
      }
      const std::size_t i = (*list)[pos++];
      order.push_back(i);
      if (!visited[i]) {
        visited[i] = true;
        st.emplace_back(&kids[i], 0);
      }
    }
  }
  const auto parentOf = [&](const Issue& o) -> const Issue* {
    if (o.parent.empty()) return nullptr;
    auto it = byId.find(o.parent);
    return it == byId.end() ? nullptr : &issues[it->second];
  };

  std::vector<std::string> scnNames = meta.scenarios;
  for (const auto& o : issues)
    for (const auto& [s, v] : o.alt.items())
      if (std::find(scnNames.begin(), scnNames.end(), s) == scnNames.end()) scnNames.push_back(s);
  std::size_t maxLab0 = 1, maxBl = 1, maxCm = 0;
  bool anyKey = false, anyAssignee = false, anyRes = false, anyRem = false, anyNB = false, anyDl = false;
  for (const auto& o : issues) {
    maxLab0 = std::max(maxLab0, o.labels.size());
    maxBl = std::max(maxBl, o.blocks.size());
    maxCm = std::max(maxCm, o.comments.size());
    anyKey |= !o.key.empty();
    anyAssignee |= !o.assignee.empty();
    anyRes |= o.resolved.has_value();
    anyRem |= o.remW.has_value();
    anyNB |= o.notBefore.has_value();
    anyDl |= o.deadline.has_value();
  }
  const bool withDeadline = meta.hasDeadlineCol || opts.planned || anyDl;
  const auto epicNameOf = [](const Issue& o) { return o.epicName.empty() ? o.summary : o.epicName; };

  // JIRA用: 階層を「Epic > タスク > サブタスク」の3段に畳む（SPEC 6.7）
  std::vector<std::string> effParent(issues.size()), trail(issues.size()), retype(issues.size());
  std::vector<int> msLab(issues.size(), 0);
  bool anyMsLab = false;
  if (opts.planned) {
    for (std::size_t i = 0; i < issues.size(); ++i) {
      const Issue& o = issues[i];
      if (isMilestoneText(o.type)) {
        const bool has = std::any_of(o.labels.begin(), o.labels.end(), [](const std::string& l) { return text::iequals(l, "milestone"); });
        msLab[i] = has ? 0 : 1;
        anyMsLab |= msLab[i] != 0;
      }
      if (isEpic(o)) continue;
      const Issue* a = &o;
      std::vector<const Issue*> chain;
      for (const Issue* p = parentOf(*a); p && !isEpic(*p); p = parentOf(*a)) {
        chain.insert(chain.begin(), a);
        a = p;
        if (chain.size() > issues.size()) break;  // 念のため（循環）
      }
      if (a == &o) {
        const Issue* pe = parentOf(o);
        effParent[i] = pe && isEpic(*pe) ? pe->id : "";
        const std::string lt = text::asciiLower(o.type);
        if (lt != "task" && lt != "story" && lt != "bug") retype[i] = "Task";
      } else {
        effParent[i] = a->id;
        retype[i] = "Sub-task";
        if (chain.size() > 1) {
          std::vector<std::string> names;
          for (std::size_t k = 0; k + 1 < chain.size(); ++k) names.push_back(chain[k]->summary);
          trail[i] = text::join(names, " > ");
        }
      }
    }
  }
  const std::size_t maxLab = maxLab0 + (anyMsLab ? 1 : 0);

  using Fn = std::function<std::string(std::size_t)>;
  std::vector<std::pair<std::string, Fn>> cols;
  const auto C = [&](std::string h, Fn f) { cols.emplace_back(std::move(h), std::move(f)); };
  const auto plan = [&](std::size_t i) -> std::optional<PlanInfo> {
    return opts.plan ? opts.plan(issues[i].id) : std::nullopt;
  };
  C("Issue Id", [&](std::size_t i) { return issues[i].id; });
  if (anyKey) C("Issue key", [&](std::size_t i) { return issues[i].key; });
  C("Issue Type", [&](std::size_t i) { return retype[i].empty() ? issues[i].type : retype[i]; });
  C("Summary", [&](std::size_t i) { return issues[i].summary; });
  C("Parent", [&](std::size_t i) -> std::string {
    if (opts.planned) return effParent[i];
    return parentOf(issues[i]) ? issues[i].parent : "";
  });
  C("Epic Name", [&](std::size_t i) { return isEpic(issues[i]) ? epicNameOf(issues[i]) : std::string(); });
  C("Epic Link", [&](std::size_t i) {
    const Issue* p = parentOf(issues[i]);
    return p && isEpic(*p) && !isEpic(issues[i]) ? epicNameOf(*p) : std::string();
  });
  C("Priority", [&](std::size_t i) { return issues[i].prio; });
  C("Status", [&](std::size_t i) { return issues[i].status; });
  if (anyAssignee) C("Assignee", [&](std::size_t i) { return issues[i].assignee; });
  C("Start date", [&](std::size_t i) {
    if (opts.planned)
      if (auto p = plan(i)) return fmtIso(p->start);
    return fmtDate(issues[i].start);
  });
  C("Due date", [&](std::size_t i) {
    if (opts.planned)
      if (auto p = plan(i)) return fmtIso(p->finish);
    return fmtDate(issues[i].due);
  });
  if (anyRes) C("Resolved", [&](std::size_t i) { return fmtDate(issues[i].resolved); });
  C("Original estimate", [&](std::size_t i) -> std::string {
    if (opts.planned) {
      auto p = plan(i);
      return p && p->leaf ? fmtEst(p->weeks) : "";
    }
    return fmtEst(issues[i].estW);
  });
  for (std::size_t k = 0; k < maxLab; ++k)
    C("Labels", [&, k](std::size_t i) -> std::string {
      const auto& l = issues[i].labels;
      if (k < l.size()) return l[k];
      if (msLab[i] && k == l.size()) return "Milestone";
      return "";
    });
  C("Description", [&](std::size_t i) {
    const Issue& o = issues[i];
    if (!trail[i].empty()) return "[元の階層] " + trail[i] + (o.desc.empty() ? "" : "\n" + o.desc);
    return o.desc;
  });
  for (std::size_t k = 0; k < maxBl; ++k)
    C("Outward issue link (Blocks)", [&, k](std::size_t i) { return k < issues[i].blocks.size() ? issues[i].blocks[k] : ""; });
  for (std::size_t k = 0; k < maxCm; ++k)
    C("Comment", [&, k](std::size_t i) { return k < issues[i].comments.size() ? fmtComment(issues[i].comments[k]) : ""; });
  if (anyNB) C("Earliest start", [&](std::size_t i) { return fmtDate(issues[i].notBefore); });
  if (withDeadline)
    C("Deadline", [&](std::size_t i) {
      if (opts.planned && opts.ownDeadline) return fmtDate(opts.ownDeadline(issues[i].id));
      return fmtDate(issues[i].deadline);
    });
  if (anyRem) C("Remaining (weeks)", [&](std::size_t i) { return issues[i].remW ? fmtNum(*issues[i].remW) : ""; });
  C("Duration (weeks)", [&](std::size_t i) -> std::string {
    if (opts.planned) {
      auto p = plan(i);
      return p && p->leaf ? fmtNum(p->baseWeeks) : "";
    }
    return issues[i].durW ? fmtNum(*issues[i].durW) : "";
  });
  for (const auto& s : scnNames)
    C("Duration (weeks) @" + s, [&, s](std::size_t i) {
      const double* v = issues[i].alt.find(s);
      return v ? fmtNum(*v) : std::string();
    });
  for (std::size_t k = 0; k < meta.extraHeaders.size(); ++k)
    C(meta.extraHeaders[k], [&, k](std::size_t i) { return k < issues[i].extra.size() ? issues[i].extra[k] : ""; });

  std::vector<std::vector<std::string>> rows;
  rows.reserve(order.size() + 1);
  {
    std::vector<std::string> h;
    for (const auto& c : cols) h.push_back(c.first);
    rows.push_back(std::move(h));
  }
  for (std::size_t i : order) {
    std::vector<std::string> r;
    r.reserve(cols.size());
    for (const auto& c : cols) r.push_back(c.second(i));
    rows.push_back(std::move(r));
  }
  return stringify(rows);
}

}  // namespace roadmap::csv
