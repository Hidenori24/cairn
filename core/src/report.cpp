// 報告の書き出し: 文字列の道具・ファイル名と保存先の検査・ページ全体の組み立て（ガント・統計・リストは report_*.cpp）
#include "roadmap/report.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "report_internal.hpp"
#include "roadmap/text.hpp"

namespace roadmap {

// ---------------------------------------------------------------- エスケープ
std::string cleanText(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (std::size_t i = 0; i < s.size();) {
    const auto b = static_cast<unsigned char>(s[i]);
    if (b < 0x80) {
      ++i;
      if (b == '\t' || b == '\n' || b == '\r') out += ' ';
      else if (b >= 0x20 && b != 0x7F) out += static_cast<char>(b);  // 他の制御文字（NUL など）は捨てる
      continue;
    }
    const std::size_t before = i;
    const char32_t cp = text::decodeAt(s, i);
    // 不正なバイト列は decodeAt が U+FFFD を返す（本物の U+FFFD も同じ。どちらも U+FFFD を書く）
    if (cp == 0xFFFD) {
      out += "\xEF\xBF\xBD";
      continue;
    }
    if (cp >= 0x80 && cp <= 0x9F) continue;                    // C1 制御文字
    if (cp == 0x2028 || cp == 0x2029) {                        // 行・段落の区切り
      out += ' ';
      continue;
    }
    if ((cp >= 0x202A && cp <= 0x202E) || (cp >= 0x2066 && cp <= 0x2069)) continue;  // 表示の向きを入れ替える制御文字
    if (cp == 0xFEFF || cp == 0xFFFE || cp == 0xFFFF) continue;
    out.append(s.data() + before, i - before);
  }
  return out;
}

std::string escapeXml(std::string_view s) {
  const std::string clean = cleanText(s);
  std::string out;
  out.reserve(clean.size() + clean.size() / 8 + 8);
  for (char ch : clean) {
    switch (ch) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      case '\'': out += "&#39;"; break;
      default: out += ch;
    }
  }
  return out;
}

// ---------------------------------------------------------------- ファイル名・保存先
namespace {

bool isReservedStem(const std::string& stem) {
  std::string u;
  for (char ch : stem) u += (ch >= 'a' && ch <= 'z') ? static_cast<char>(ch - 'a' + 'A') : ch;
  while (!u.empty() && u.back() == ' ') u.pop_back();
  if (u == "CON" || u == "PRN" || u == "AUX" || u == "NUL") return true;
  if (u.size() == 4 && (u.compare(0, 3, "COM") == 0 || u.compare(0, 3, "LPT") == 0) && u[3] >= '1' && u[3] <= '9') return true;
  return false;
}

}  // namespace

std::string reportFileName(const std::string& projectName, const std::string& isoDate) {
  std::string name;
  std::size_t count = 0;
  for (std::size_t i = 0; i < projectName.size() && count < 60;) {
    const char32_t cp = text::decodeAt(projectName, i);
    ++count;
    const bool bad = cp < 0x20 || cp == 0x7F || (cp >= 0x80 && cp <= 0x9F) || cp == '\\' || cp == '/' || cp == ':' || cp == '*' ||
                     cp == '?' || cp == '"' || cp == '<' || cp == '>' || cp == '|' || cp == 0xFFFD || cp == 0x2028 || cp == 0x2029 ||
                     (cp >= 0x202A && cp <= 0x202E) || (cp >= 0x2066 && cp <= 0x2069) || cp == 0xFEFF;
    text::appendUtf8(name, bad ? U'_' : cp);
  }
  while (!name.empty() && (name.back() == '.' || name.back() == ' ')) name.pop_back();
  while (!name.empty() && name.front() == ' ') name.erase(name.begin());
  if (name.empty()) name = "報告";
  // 最初の "." より前が予約名だと、拡張子が付いてもデバイス名として扱われる
  if (isReservedStem(name.substr(0, name.find('.')))) name.insert(name.begin(), '_');
  std::string date;
  for (char ch : isoDate)
    if ((ch >= '0' && ch <= '9') || ch == '-') date += ch;
  return name + (date.empty() ? "" : "_" + date) + ".html";
}

Status checkSavePath(const std::string& path, const std::vector<std::string>& exts, const std::string& extHint) {
  if (path.empty()) return Error{"保存先が空です"};
  if (path.size() > 32000) return Error{"保存先のパスが長すぎます"};
  for (char ch : path)
    if (static_cast<unsigned char>(ch) < 0x20 || ch == 0x7F) return Error{"保存先に使えない文字が含まれています"};
  if (!text::isValidUtf8(path)) return Error{"保存先の文字コードが正しくありません"};
  const std::size_t slash = path.find_last_of("/\\");
  const std::string file = slash == std::string::npos ? path : path.substr(slash + 1);
  if (file.empty()) return Error{"保存先のファイル名がありません"};
  const std::string lower = text::asciiLower(file);
  for (const std::string& ext : exts)
    if (lower.size() > ext.size() && lower.compare(lower.size() - ext.size(), ext.size(), ext) == 0) return okStatus();
  return Error{"保存先のファイル名は " + extHint + " にしてください"};
}

Status checkReportPath(const std::string& path) { return checkSavePath(path, {".html", ".htm"}, ".html"); }

Status writeReport(IFileStore& fs, const std::string& path, const std::string& html) {
  if (Status s = checkReportPath(path); !s.ok()) return s;
  if (const auto st = fs.stat(path); st && st->isDir) return Error{"同じ名前のフォルダがあります"};
  return fs.writeAtomic(path, html);
}

// ---------------------------------------------------------------- 書き出す行
std::vector<VisibleRow> reportRows(const ExportInput& in, ReportScope scope) {
  const Tree& T = in.tree;
  std::vector<VisibleRow> out;
  if (T.n <= 0 || in.issues.size() != static_cast<std::size_t>(T.n) || in.rows.size() != static_cast<std::size_t>(T.n)) return out;
  const auto inRange = [&](int id) { return id >= 0 && id < T.n; };
  switch (scope) {
    case ReportScope::All:
      for (int id : T.dfs)
        if (inRange(id)) out.push_back({id, T.depth[static_cast<std::size_t>(id)], !T.kids[static_cast<std::size_t>(id)].empty(), true});
      break;
    case ReportScope::Current:
      for (const VisibleRow& r : in.currentRows)
        if (inRange(r.id)) out.push_back(r);
      break;
    case ReportScope::Subtree: {
      if (!inRange(in.subtreeRoot)) break;
      const std::size_t root = static_cast<std::size_t>(in.subtreeRoot);
      const int d0 = T.depth[root];
      for (std::size_t p = static_cast<std::size_t>(T.dfsPos[root]); p < T.dfs.size(); ++p) {
        const int id = T.dfs[p];
        if (!inRange(id)) break;
        const std::size_t k = static_cast<std::size_t>(id);
        if (p > static_cast<std::size_t>(T.dfsPos[root]) && T.depth[k] <= d0) break;
        out.push_back({id, T.depth[k] - d0, !T.kids[k].empty(), true});
      }
      break;
    }
  }
  return out;
}

// ---------------------------------------------------------------- 内部の道具
namespace report {

std::string n1(double v) {
  if (!std::isfinite(v)) return "0";
  const double lim = 1e9;
  v = std::max(-lim, std::min(lim, v));
  long long t = std::llround(v * 10.0);
  const bool neg = t < 0;
  if (neg) t = -t;
  std::string s = std::to_string(t / 10);
  if (t % 10 != 0) s += "." + std::to_string(t % 10);
  return (neg && t != 0) ? "-" + s : s;
}

std::string n0(double v) { return n1(std::round(v)); }

std::string alphaStr(double a) {
  const long p = std::max(0L, std::min(100L, std::lround(a * 100.0)));
  if (p >= 100) return "1";
  if (p <= 0) return "0";
  std::string s = "0.";
  s += static_cast<char>('0' + p / 10);
  if (p % 10 != 0) s += static_cast<char>('0' + p % 10);
  return s;
}

std::string hex(unsigned rgb) {
  static const char* const d = "0123456789abcdef";
  std::string s = "#";
  for (int sh = 20; sh >= 0; sh -= 4) s += d[(rgb >> sh) & 0xF];
  return s;
}

double textW(std::string_view s, double px) {
  double w = 0;
  for (std::size_t i = 0; i < s.size();) {
    const char32_t cp = text::decodeAt(s, i);
    w += cp < 0x250 ? 0.58 : 1.0;
  }
  return w * px;
}

std::string clipText(std::string_view s, double maxW, double px) {
  if (textW(s, px) <= maxW) return std::string(s);
  const double ell = px;  // 「…」
  std::string out;
  double w = 0;
  for (std::size_t i = 0; i < s.size();) {
    const std::size_t before = i;
    const char32_t cp = text::decodeAt(s, i);
    const double cw = (cp < 0x250 ? 0.58 : 1.0) * px;
    if (w + cw + ell > maxW) break;
    out.append(s.data() + before, i - before);
    w += cw;
  }
  return out + "…";
}

void addRect(std::string& o, double x, double y, double w, double h, const std::string& fill, double rx, double opacity,
             std::string_view extra) {
  o += "<rect x=\"" + n1(x) + "\" y=\"" + n1(y) + "\" width=\"" + n1(w) + "\" height=\"" + n1(h) + "\"";
  if (rx > 0) o += " rx=\"" + n1(rx) + "\"";
  if (!fill.empty()) o += " fill=\"" + fill + "\"";
  if (opacity < 0.999) o += " fill-opacity=\"" + alphaStr(opacity) + "\"";
  if (!extra.empty()) {
    o += ' ';
    o.append(extra);
  }
  o += "/>";
}

namespace {
void textAttrs(std::string& o, double x, double y, const TextStyle& st) {
  o += "<text x=\"" + n1(x) + "\" y=\"" + n1(y) + "\"";
  if (!st.fill.empty()) o += " fill=\"" + st.fill + "\"";
  if (st.size > 0) o += " font-size=\"" + n1(st.size) + "\"";
  if (st.bold) o += " font-weight=\"700\"";
  if (st.anchor && *st.anchor) o += std::string(" text-anchor=\"") + st.anchor + "\"";
  if (st.opacity < 0.999) o += " fill-opacity=\"" + alphaStr(st.opacity) + "\"";
  o += ">";
}
}  // namespace

void addText(std::string& o, double x, double y, std::string_view s, const TextStyle& st) {
  textAttrs(o, x, y, st);
  o += esc(s);
  o += "</text>";
}

void openText(std::string& o, double x, double y, const TextStyle& st) { textAttrs(o, x, y, st); }

unsigned catInk(const ThemeColors& k, Cat c) {
  if (c == Cat::Done) return k.ok;
  if (c == Cat::Prog) return k.accent;
  return k.muted;
}

std::string floatLabel(const ScheduleRow& r) {
  const std::string t = floatText(r);
  if (!r.done && r.floatDays < 0) return "! " + t;
  return t;
}

unsigned riskInk(const ThemeColors& k, Risk r) {
  switch (r) {
    case Risk::Late: return k.late;
    case Risk::Tight: return k.tight;
    case Risk::Done: return k.muted;
    default: return k.ok;
  }
}

}  // namespace report

// ---------------------------------------------------------------- ページ全体
namespace {

std::string css(const ThemeColors& k) {
  const std::string page = report::hex(k.dark ? k.bg : k.surface);
  std::string c;
  c += ":root{color-scheme:" + std::string(k.dark ? "dark" : "light") + ";--bg:" + page + ";--card:" + report::hex(k.surface) +
       ";--soft:" + report::hex(k.surface2) + ";--soft2:" + report::hex(k.surface3) + ";--ink:" + report::hex(k.ink) +
       ";--muted:" + report::hex(k.muted) + ";--line:" + report::hex(k.line) + ";--line2:" + report::hex(k.line2) +
       ";--accent:" + report::hex(k.accent) + ";--late:" + report::hex(k.late) + ";--tight:" + report::hex(k.tight) +
       ";--ok:" + report::hex(k.ok) + "}\n";
  c += R"(*{box-sizing:border-box}
html{-webkit-print-color-adjust:exact;print-color-adjust:exact}
body{margin:0;background:var(--bg);color:var(--ink);font:13px/1.6 "Yu Gothic UI","Meiryo","Hiragino Sans","Noto Sans CJK JP","Noto Sans JP",system-ui,sans-serif}
.page{max-width:1240px;margin:0 auto;padding:24px 20px 32px}
h1{font-size:22px;line-height:1.3;margin:0 0 4px;overflow-wrap:anywhere}
h2{font-size:17px;margin:0 0 8px;padding-bottom:6px;border-bottom:2px solid var(--line2)}
h3{font-size:14px;margin:0 0 6px}
p{margin:0}
.meta{color:var(--muted)}
.meta b{color:var(--ink);font-weight:600}
.part{margin-top:30px}
.note{color:var(--muted);font-size:12px;margin:2px 0 8px}
.warn{margin:6px 0;padding:6px 10px;border:1px solid var(--tight);border-radius:6px;color:var(--tight);font-size:12px}
.empty{margin-top:24px;padding:16px;border:1px dashed var(--line2);border-radius:8px;color:var(--muted)}
.legend{display:flex;flex-wrap:wrap;gap:4px 16px;margin:6px 0 10px;font-size:12px;color:var(--muted)}
.legend span{display:inline-flex;align-items:center;gap:5px}
.sw{display:inline-block;width:10px;height:10px;border-radius:2px;border:1px solid var(--line2)}
.scroll{overflow-x:auto}
svg{display:block;font-family:inherit;font-size:12px}
.card{margin:0 0 18px}
.gseg{break-inside:avoid;page-break-inside:avoid}
.gantt-panel-start{break-before:page;page-break-before:always}
.gantt-first{page:gantt}
.part.gantt{page:gantt}
.tiles{display:grid;grid-template-columns:repeat(6,1fr);gap:8px;margin:6px 0 10px}
.tile{border:1px solid var(--line);border-radius:6px;padding:8px 10px;background:var(--card)}
.tile .l{font-size:11px;color:var(--muted)}
.tile .v{font-size:20px;font-weight:700;line-height:1.3}
table{border-collapse:collapse;width:100%;font-size:12px}
th{background:var(--soft);color:var(--muted);font-weight:600;text-align:left;border-bottom:1px solid var(--line2);padding:5px 8px;white-space:nowrap}
td{border-bottom:1px solid var(--line);padding:4px 8px;vertical-align:top}
thead{display:table-header-group}
tr{break-inside:avoid;page-break-inside:avoid}
td.n,th.n{text-align:right;font-variant-numeric:tabular-nums}
td.nm{overflow-wrap:anywhere}
.id{color:var(--muted);font-variant-numeric:tabular-nums}
.par td{font-weight:700;background:var(--soft)}
.st-done{color:var(--ok)}.st-prog{color:var(--accent)}.st-todo{color:var(--muted)}
.rk-late{color:var(--late);font-weight:700}.rk-tight{color:var(--tight);font-weight:600}.rk-ok{color:var(--ok)}.rk-done{color:var(--muted)}
footer{margin-top:36px;padding-top:8px;border-top:1px solid var(--line);color:var(--muted);font-size:11px}
@page{size:A4 portrait;margin:10mm;@bottom-right{content:counter(page) " / " counter(pages);font-size:9px;color:#667085}}
@page gantt{size:A4 landscape;margin:10mm;@bottom-right{content:counter(page) " / " counter(pages);font-size:9px;color:#667085}}
@media screen{
.part.gantt{width:100vw;margin-left:calc(50% - 50vw);padding-left:max(20px,calc((100vw - 1240px)/2 + 20px));padding-right:max(20px,calc((100vw - 1240px)/2 + 20px))}
}
@media screen and (max-width:760px){.tiles{grid-template-columns:repeat(2,1fr)}.page{padding:12px 10px}}
@media print{
body{font-size:11px;line-height:1.5}
.page{max-width:none;padding:0}
.scroll{overflow:visible}
svg{max-width:100%;height:auto}
.part{margin-top:0;break-before:page;page-break-before:always}
.part.first{break-before:auto;page-break-before:auto;margin-top:14px}
.card{break-inside:avoid;page-break-inside:avoid}
h2,h3{break-after:avoid;page-break-after:avoid}
footer{margin-top:12px;break-inside:avoid;page-break-inside:avoid}
}
)";
  return c;
}

std::string fmtDateTime(const IClock& clk) {
  const LocalTime t = clk.now();
  char b[48];
  std::snprintf(b, sizeof b, "%04d/%02d/%02d %02d:%02d", t.y, t.mo, t.d, t.h, t.mi);
  return b;
}

}  // namespace

namespace report {
std::string pageCss(const ThemeColors& k) { return css(k); }
std::string dateTimeText(const IClock& clk) { return fmtDateTime(clk); }
}  // namespace report

std::string exportHtml(const ExportInput& in, const ExportOptions& opt) {
  const ThemeColors& k = opt.dark ? darkColors() : lightColors();
  const Tree& T = in.tree;
  const bool consistent = T.n > 0 && in.issues.size() == static_cast<std::size_t>(T.n) && in.rows.size() == static_cast<std::size_t>(T.n);
  report::Ctx cx{in, opt, k, opt.dark, {}};
  if (consistent) cx.rows = reportRows(in, opt.scope);
  // 課題が 0 件（全部削除した）のときは、入力の食い違いではない。有効な比較（消えた課題）は出す
  const bool noIssues = T.n == 0 && in.issues.empty() && in.rows.empty();
  if ((consistent || noIssues) && opt.baseline && in.baseline && in.baseline->entries.size() == static_cast<std::size_t>(std::max(T.n, 0)))
    cx.bl = in.baseline;

  const std::string title = in.projectName.empty() ? std::string("報告") : in.projectName;
  std::string h;
  h.reserve(1 << 16);
  h += "<!doctype html>\n<html lang=\"ja\">\n<head>\n<meta charset=\"utf-8\">\n";
  h += "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">\n";
  h += "<title>" + escapeXml(title) + " - 報告</title>\n<style>\n" + css(k) + "</style>\n</head>\n<body>\n<div class=\"page\">\n";

  // ヘッダ: プロジェクト名・基準日・出力した日時
  h += std::string("<header") + (consistent && opt.gantt ? " class=\"gantt-first\"" : std::string()) + ">\n<h1>" + escapeXml(title) + "</h1>\n<p class=\"meta\">基準日 <b>" + escapeXml(fmtLong(in.base)) + "</b>";
  if (in.clock) h += "　／　出力 <b>" + escapeXml(fmtDateTime(*in.clock)) + "</b>";
  if (consistent) {
    const Summary sm = summarize(T, in.rows);
    h += "　／　完了 " + std::to_string(sm.done) + " / 全体 " + std::to_string(sm.total);
    if (sm.late > 0) h += "　<span class=\"rk-late\">! 遅れ " + std::to_string(sm.late) + "</span>";
    if (sm.tight > 0) h += "　<span class=\"rk-tight\">余裕小 " + std::to_string(sm.tight) + "</span>";
  }
  h += "</p>\n";
  if (consistent && (opt.gantt || opt.list)) {
    if (opt.scope == ReportScope::Current) {
      h += "<p class=\"meta\">表示範囲: 画面の表示のまま";
      if (!in.filterLabel.empty()) h += "（絞り込み: " + escapeXml(in.filterLabel) + "）";
      h += "</p>\n";
    } else if (opt.scope == ReportScope::Subtree && in.subtreeRoot >= 0 && in.subtreeRoot < T.n) {
      h += "<p class=\"meta\">表示範囲: 「" + escapeXml(T.name[static_cast<std::size_t>(in.subtreeRoot)]) + "」とその配下</p>\n";
    }
  }
  h += "</header>\n<main>\n";

  if (!consistent) {
    h += "<p class=\"empty\">課題がありません。課題を追加してから書き出してください。</p>\n";
    if (noIssues && cx.bl && opt.stats) h += "<section class=\"part first\">\n" + report::renderStats(cx) + "</section>\n";  // 消えた課題の比較
  } else if (!opt.gantt && !opt.stats && !opt.list) {
    h += "<p class=\"empty\">書き出す内容が選ばれていません。</p>\n";
  } else {
    bool first = true;
    const auto part = [&](const std::string& body, bool gantt = false) {
      h += std::string("<section class=\"part") + (first ? " first" : "") + (gantt ? " gantt" : "") + "\">\n" + body + "</section>\n";
      first = false;
    };
    if (opt.gantt) part(report::renderGantt(cx), true);
    if (opt.stats) part(report::renderStats(cx));
    if (opt.list) part(report::renderList(cx));
  }
  const bool ganttOnly = consistent && opt.gantt && !opt.stats && !opt.list;
  h += "</main>\n<footer" + std::string(ganttOnly ? " class=\"gantt-first\"" : "") + ">このアプリで出力（Cairn）</footer>\n</div>\n</body>\n</html>\n";
  return h;
}

}  // namespace roadmap
