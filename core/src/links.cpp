#include "roadmap/links.hpp"

#include <algorithm>
#include <limits>
#include <optional>
#include <set>
#include <utility>

#include "roadmap/attachments.hpp"
#include "roadmap/model.hpp"
#include "roadmap/text.hpp"

namespace roadmap {

namespace {
using Json = nlohmann::ordered_json;

constexpr std::size_t kMaxIssueIdBytes = 64;

bool isAsciiAlpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
bool isAsciiAlnum(char c) { return isAsciiAlpha(c) || (c >= '0' && c <= '9'); }

LinkClass bad(LinkKind kind, std::string target, std::string reason) {
  LinkClass c;
  c.kind = kind;
  c.target = std::move(target);
  c.reason = std::move(reason);
  return c;
}
LinkClass unknown(std::string reason) { return bad(LinkKind::Unknown, {}, std::move(reason)); }
LinkClass rejected(std::string reason) { return bad(LinkKind::Rejected, {}, std::move(reason)); }

constexpr const char* kFormsHint = "URL（http:// か https://）か、ファイル・フォルダの場所（C:\\… ・ \\\\サーバー\\共有名\\… ・ file://…）を入れてください";

bool isBidiControl(char32_t cp) { return (cp >= 0x202A && cp <= 0x202E) || (cp >= 0x2066 && cp <= 0x2069); }
// 場所・表示名から取り除く文字（制御文字・C1 制御・ゼロ幅・行/段落区切り・BOM）
bool isStrippedChar(char32_t cp) {
  return cp < 0x20 || cp == 0x7F || (cp >= 0x80 && cp <= 0x9F) || cp == 0x200B || cp == 0x200C || cp == 0x200D || cp == 0x200E ||
         cp == 0x200F || cp == 0x2028 || cp == 0x2029 || cp == 0x2060 || cp == 0xFEFF;
}

// 取り除く文字を除いた文字列。方向を上書きする制御文字（見た目で別の場所に見せかけられる）があれば bidi を立てる
std::string cleanse(std::string_view s, bool& bidi) {
  // 印字できる ASCII だけなら、そのまま（ほとんどの URL・パスはこれ）
  if (std::all_of(s.begin(), s.end(), [](char ch) { return ch >= 0x20 && ch < 0x7F; })) return std::string(s);
  std::string out;
  out.reserve(s.size());
  for (std::size_t i = 0; i < s.size();) {
    const std::size_t at = i;
    const char32_t cp = text::decodeAt(s, i);
    if (isBidiControl(cp)) {
      bidi = true;
      continue;
    }
    if (isStrippedChar(cp)) continue;
    out.append(s.substr(at, i - at));
  }
  return out;
}

// 前後の空白と、囲み（" … "、“ … ”、< … >）を取る
std::string unwrap(std::string t) {
  for (int guard = 0; guard < 8; ++guard) {
    t = text::trim(t);
    bool again = false;
    const auto strip = [&t, &again](std::string_view open, std::string_view close) {
      if (again || t.size() < open.size() + close.size()) return;
      if (t.compare(0, open.size(), open) != 0 || t.compare(t.size() - close.size(), close.size(), close) != 0) return;
      t = t.substr(open.size(), t.size() - open.size() - close.size());
      again = true;
    };
    strip("\"", "\"");
    strip("\xE2\x80\x9C", "\xE2\x80\x9D");
    strip("<", ">");
    if (!again) break;
  }
  return text::trim(t);
}

// 先頭の「scheme:」（小文字。2 文字以上の英数字と + - .、先頭は英字）。なければ空
std::string schemeOf(const std::string& t) {
  if (t.empty() || !isAsciiAlpha(t[0])) return {};
  std::size_t i = 1;
  while (i < t.size() && (isAsciiAlnum(t[i]) || t[i] == '+' || t[i] == '-' || t[i] == '.')) ++i;
  if (i < 2 || i >= t.size() || t[i] != ':') return {};
  return text::asciiLower(std::string_view(t).substr(0, i));
}

// %XX を戻す。結果が UTF-8 として正しくなければ、戻さずそのまま返す
std::string pctDecode(std::string_view s) {
  const auto hex = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  std::string out;
  out.reserve(s.size());
  bool changed = false;
  for (std::size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '%' && i + 2 < s.size() && hex(s[i + 1]) >= 0 && hex(s[i + 2]) >= 0) {
      out.push_back(static_cast<char>(hex(s[i + 1]) * 16 + hex(s[i + 2])));
      i += 2;
      changed = true;
    } else {
      out.push_back(s[i]);
    }
  }
  if (!changed || !text::isValidUtf8(out)) return std::string(s);
  return out;
}

// パスに使えない文字（Windows の予約文字。ドライブの「:」は呼び出し側が先に取り除く）か、制御文字があれば理由を返す
const char* badPathReason(std::string_view s) {
  for (const char ch : s) {
    const unsigned char c = static_cast<unsigned char>(ch);
    if (c < 0x20 || c == 0x7F) return "場所に使えない文字（制御文字）が入っています";
    if (std::string_view("<>\"|*?:").find(ch) != std::string_view::npos)
      return "場所に使えない文字（< > \" | * ? : のどれか）が入っています";
  }
  return nullptr;
}

std::vector<std::string_view> splitPath(std::string_view s) {
  std::vector<std::string_view> out;
  std::size_t b = 0;
  for (std::size_t i = 0; i <= s.size(); ++i)
    if (i == s.size() || s[i] == '\\' || s[i] == '/') {
      if (i > b) out.push_back(s.substr(b, i - b));
      b = i + 1;
    }
  return out;
}

bool endsWithSep(std::string_view s) { return !s.empty() && (s.back() == '\\' || s.back() == '/'); }

LinkClass finish(LinkKind kind, std::string target) {
  if (target.size() > kMaxLinkPathBytes) return rejected("場所が長すぎます（パスは 1,024 バイトまで）");
  LinkClass c;
  c.kind = kind;
  c.target = std::move(target);
  return c;
}

// 「サーバー/共有名/…」（先頭の区切りは取ってある）。comps = 区切りで分けた部品
LinkClass classifyUncParts(std::string target, std::string_view parts) {
  if (const char* why = badPathReason(pctDecode(parts))) return rejected(why);
  const std::vector<std::string_view> comps = splitPath(parts);
  if (comps.size() < 2) return unknown("共有名がありません（\\\\サーバー\\共有名\\… の形で入れてください）");
  const bool folder = endsWithSep(parts) || comps.size() == 2;  // 共有のルートはフォルダ
  return finish(folder ? LinkKind::Folder : LinkKind::File, std::move(target));
}

LinkClass classifyUnc(const std::string& t) {
  // t は「\\」で始まる
  const std::string_view rest = std::string_view(t).substr(2);
  if (!rest.empty() && (rest[0] == '?' || rest[0] == '.')) return rejected("デバイスの名前を使った形式（\\\\?\\ \\\\.\\）は開けません");
  if (rest.empty() || rest[0] == '\\' || rest[0] == '/') return unknown("サーバー名がありません（\\\\サーバー\\共有名\\… の形で入れてください）");
  return classifyUncParts(t, rest);
}

LinkClass classifyDrive(const std::string& t) {
  // t は「C:\」か「C:/」で始まる
  if (const char* why = badPathReason(pctDecode(std::string_view(t).substr(2)))) return rejected(why);
  return finish(endsWithSep(t) ? LinkKind::Folder : LinkKind::File, t);
}

LinkClass classifyFileUrl(const std::string& t) {
  // t は小文字の「file:」で始まる
  if (t.compare(5, 2, "//") != 0) return unknown("file:// の形で入れてください");
  if (t.find('\\') != std::string::npos) return unknown("file:// の中は / で区切ってください");
  const std::string rest = t.substr(7);  // 「ホスト/パス」
  const std::size_t slash = rest.find('/');
  std::string host = slash == std::string::npos ? rest : rest.substr(0, slash);
  const std::string path = slash == std::string::npos ? std::string() : rest.substr(slash);  // 空か、「/」で始まる
  if (text::iequals(host, "localhost")) host.clear();
  if (host.empty()) {
    // file:///C:/… ・ file:////server/share/…
    if (path.size() >= 4 && isAsciiAlpha(path[1]) && path[2] == ':' && path[3] == '/') {
      if (const char* why = badPathReason(pctDecode(std::string_view(path).substr(3)))) return rejected(why);
      return finish(path.back() == '/' ? LinkKind::Folder : LinkKind::File, t);
    }
    if (path.size() >= 2 && path[0] == '/' && path[1] == '/') return classifyUncParts(t, std::string_view(path).substr(2));
    return unknown("file:// の後に、ドライブ（C:）か共有名のある場所を入れてください");
  }
  // file://server/share/…
  if (const char* why = badPathReason(pctDecode(host))) return rejected(why);
  if (path.empty()) return unknown("共有名がありません（file://サーバー/共有名/… の形で入れてください）");
  return classifyUncParts(t, host + path);
}

LinkClass classifyWeb(const std::string& t, const std::string& scheme) {
  if (t.compare(scheme.size(), 3, "://") != 0) return unknown(std::string(scheme) + "://… の形で入れてください");
  const std::string rest = t.substr(scheme.size() + 3);
  const std::size_t end = rest.find_first_of("/?#");
  const std::string authority = end == std::string::npos ? rest : rest.substr(0, end);
  std::string tail = end == std::string::npos ? std::string() : rest.substr(end);
  if (authority.empty()) return unknown("ホスト名がありません");
  if (authority.find('@') != std::string::npos) return rejected("ID・パスワードを含む URL は保存しません（設定ファイルに残るため）");
  if (authority.find_first_of(" \\\"<>") != std::string::npos) return rejected("ホスト名に使えない文字が入っています");
  if (tail.find_first_of("\"<>") != std::string::npos) return rejected("URL に使えない文字（\" < >）が入っています");
  tail = text::replaceAll(std::move(tail), " ", "%20");
  std::string out = scheme + "://" + authority + tail;
  if (out.size() > kMaxLinkUrlBytes) return rejected("URL が長すぎます（2,048 バイトまで）");
  LinkClass c;
  c.kind = LinkKind::Web;
  c.target = std::move(out);
  return c;
}

// ホストと、パスの区画（%XX を戻したもの）
struct WebParts {
  std::string host;
  std::vector<std::string> segs;
};
WebParts webPartsOf(const std::string& t) {
  WebParts w;
  const std::size_t at = t.find("://");
  if (at == std::string::npos) return w;
  const std::string rest = t.substr(at + 3);
  const std::size_t end = rest.find_first_of("/?#");
  w.host = end == std::string::npos ? rest : rest.substr(0, end);
  if (end == std::string::npos || rest[end] != '/') return w;
  const std::size_t pe = rest.find_first_of("?#", end);
  const std::string path = rest.substr(end, pe == std::string::npos ? std::string::npos : pe - end);
  for (const std::string_view s : splitPath(path)) w.segs.push_back(pctDecode(s));
  return w;
}

// 末尾の区切りを除いた、最後の部品
std::string lastComponent(std::string_view s) {
  while (endsWithSep(s)) s.remove_suffix(1);
  const std::size_t k = s.find_last_of("\\/");
  return std::string(k == std::string_view::npos ? s : s.substr(k + 1));
}

// 重複の判定に使う鍵（正規化済みの場所から作る。I/O も再判定もしない）。Web は完全一致、ファイル・フォルダは大文字小文字と区切りを無視
std::string linkKey(const std::string& normalized) {
  if (normalized.compare(0, 7, "http://") == 0 || normalized.compare(0, 8, "https://") == 0) return "w" + normalized;
  std::string k = text::asciiLower(normalized);
  std::replace(k.begin(), k.end(), '/', '\\');
  return "f" + k;
}
}  // namespace

const char* linkKindLabel(LinkKind k) {
  switch (k) {
    case LinkKind::Web: return "Web";
    case LinkKind::File: return "ファイル";
    case LinkKind::Folder: return "フォルダ";
    default: return "不明";
  }
}

LinkClass classifyLink(std::string_view raw) {
  if (!text::isValidUtf8(raw)) return rejected("文字コードが正しくありません");
  bool bidi = false;
  const std::string t = unwrap(cleanse(raw, bidi));
  if (bidi) return rejected("表示を紛らわしくする制御文字（右から左への上書きなど）が入っています");
  if (t.empty()) return unknown("場所が空です");
  // 1) UNC（\\server\share\…）
  if (t.size() >= 2 && t[0] == '\\' && t[1] == '\\') return classifyUnc(t);
  // 2) ドライブ付き（C:\… ・ C:/…）
  if (t.size() >= 2 && isAsciiAlpha(t[0]) && t[1] == ':') {
    if (t.size() >= 3 && (t[2] == '\\' || t[2] == '/')) return classifyDrive(t);
    return unknown("ドライブ名の後に \\ か / が必要です（C:\\… の形で入れてください）");
  }
  // 3) スキーム付き。許可リスト: http・https・file だけ
  const std::string scheme = schemeOf(t);
  if (!scheme.empty()) {
    const std::string normalized = scheme + t.substr(scheme.size());
    if (scheme == "http" || scheme == "https") return classifyWeb(normalized, scheme);
    if (scheme == "file") return classifyFileUrl(normalized);
    std::string shown = scheme.size() > 16 ? scheme.substr(0, 16) + "…" : scheme;
    return rejected("「" + shown + ":」で始まる場所は開けません（開けるのは http・https・file とファイル・フォルダの場所だけです）");
  }
  // 4) 判定できない
  return unknown(std::string("何の場所か分かりません。") + kFormsHint);
}

std::string sanitizeLinkTitle(std::string_view title) {
  const std::string valid = text::sanitizeUtf8(title);
  std::string cleaned;
  cleaned.reserve(valid.size());
  for (std::size_t i = 0; i < valid.size();) {
    const std::size_t at = i;
    const char32_t cp = text::decodeAt(valid, i);
    if (isBidiControl(cp) || cp < 0x20 || cp == 0x7F || (cp >= 0x80 && cp <= 0x9F) || cp == 0x2028 || cp == 0x2029) continue;
    cleaned.append(valid, at, i - at);
  }
  const std::string t = text::trim(cleaned);
  if (t.size() <= kMaxLinkTitleBytes) return t;
  std::string out;
  for (std::size_t i = 0; i < t.size();) {
    const std::size_t at = i;
    text::decodeAt(t, i);
    if (out.size() + (i - at) > kMaxLinkTitleBytes) break;
    out.append(t, at, i - at);
  }
  return text::trim(out);
}

std::string defaultTitle(std::string_view target) {
  const LinkClass c = classifyLink(target);
  std::string name;
  switch (c.kind) {
    case LinkKind::Web: {
      const WebParts w = webPartsOf(c.target);
      name = w.host;
      const std::size_t n = w.segs.size();
      if (n == 1) name += "/" + w.segs[0];
      else if (n >= 2) name += "/" + w.segs[n - 2] + "/" + w.segs[n - 1];
      break;
    }
    case LinkKind::File:
    case LinkKind::Folder: {
      std::string p = c.target;
      if (p.compare(0, 5, "file:") == 0) p = pctDecode(p.substr(5));
      name = lastComponent(p);
      break;
    }
    default: break;
  }
  return sanitizeLinkTitle(name);
}

bool isRiskyTarget(std::string_view target) {
  const LinkClass c = classifyLink(target);
  if (c.kind != LinkKind::File) return false;
  const bool url = c.target.compare(0, 5, "file:") == 0;
  const std::string path = url ? pctDecode(c.target.substr(5)) : c.target;
  // URL の # 以降は断片だが、シェルの解釈に依存させず、切った名前と切らない名前の両方を見る（疑わしければ警告）
  std::vector<std::string> candidates{path};
  if (url)
    if (const auto h = path.find('#'); h != std::string::npos) candidates.push_back(path.substr(0, h));
  for (const std::string& p : candidates) {
    std::string name = lastComponent(p);
    while (!name.empty() && (name.back() == ' ' || name.back() == '.')) name.pop_back();  // Windows は末尾の空白と「.」を無視する
    if (isRiskyFile(name)) return true;
  }
  return false;
}

std::string linkFolderOf(std::string_view target) {
  const LinkClass c = classifyLink(target);
  if (c.kind != LinkKind::File) return {};
  const std::size_t k = c.target.find_last_of("\\/");
  if (k == std::string::npos) return {};
  const LinkClass parent = classifyLink(std::string_view(c.target).substr(0, k + 1));
  return parent.kind == LinkKind::Folder ? parent.target : std::string();
}

bool sameLinkTarget(std::string_view a, std::string_view b) {
  const LinkClass ca = classifyLink(a), cb = classifyLink(b);
  if (!ca.ok() || !cb.ok()) return a == b;
  return linkKey(ca.target) == linkKey(cb.target);
}

std::size_t linkCount(const LinkMap& m, const std::string& issueId) {
  const auto it = m.find(issueId);
  return it == m.end() ? 0 : it->second.size();
}

std::size_t linkTotal(const LinkMap& m) {
  std::size_t n = 0;
  for (const auto& [id, list] : m) n += list.size();
  return n;
}

bool isLinkIssueId(std::string_view id) { return id.size() <= kMaxIssueIdBytes && isSafePathComponent(id); }

namespace {
// list の中に（except 番目を除いて）同じ場所があるか。target は正規化済み（list の場所も正規化済み）
bool hasTarget(const std::vector<Link>& list, const std::string& target, std::size_t except = static_cast<std::size_t>(-1)) {
  const std::string key = linkKey(target);
  for (std::size_t i = 0; i < list.size(); ++i)
    if (i != except && linkKey(list[i].target) == key) return true;
  return false;
}
Error cannotLink() { return Error{"この課題にはリンクを付けられません（課題 ID が設定ファイルに保存できない形です）"}; }
Error perIssueFull() { return Error{"1 つの課題に付けられるリンクは 50 件までです"}; }
Error totalFull() { return Error{"プロジェクト全体で付けられるリンクは 5,000 件までです"}; }
Error duplicate() { return Error{"この場所はすでにこの課題のリンクに入っています"}; }
Error noSuchLink() { return Error{"リンクがありません"}; }

// to の末尾へ link を足せるか（足せなければ理由）。totalCounts = 全体の件数に数えるか（移動は合計が変わらない）
std::optional<Error> checkRoom(const LinkMap& m, const std::string& to, const std::string& target, bool totalCounts) {
  if (!isLinkIssueId(to)) return cannotLink();
  const auto it = m.find(to);
  const std::vector<Link> none;
  const std::vector<Link>& list = it == m.end() ? none : it->second;
  if (hasTarget(list, target)) return duplicate();
  if (list.size() >= kMaxLinksPerIssue) return perIssueFull();
  if (totalCounts && linkTotal(m) >= kMaxLinksTotal) return totalFull();
  return std::nullopt;
}
}  // namespace

Result<Link> linkAdd(LinkMap& m, const IClock& clock, const std::string& issueId, std::string_view title, std::string_view target) {
  if (!isLinkIssueId(issueId)) return cannotLink();
  const LinkClass c = classifyLink(target);
  if (!c.ok()) return Error{c.reason};
  if (auto e = checkRoom(m, issueId, c.target, true)) return *e;
  Link l;
  l.title = sanitizeLinkTitle(title);
  if (l.title.empty()) l.title = defaultTitle(c.target);
  l.target = c.target;
  l.addedAt = clock.epochMs();
  m[issueId].push_back(l);
  return l;
}

Status linkEdit(LinkMap& m, const std::string& issueId, std::size_t index, std::string_view title, std::string_view target) {
  const auto it = m.find(issueId);
  if (it == m.end() || index >= it->second.size()) return noSuchLink();
  const LinkClass c = classifyLink(target);
  if (!c.ok()) return Error{c.reason};
  if (hasTarget(it->second, c.target, index)) return duplicate();
  Link& l = it->second[index];
  l.title = sanitizeLinkTitle(title);
  if (l.title.empty()) l.title = defaultTitle(c.target);
  l.target = c.target;
  return okStatus();
}

Status linkRemove(LinkMap& m, const std::string& issueId, std::size_t index) {
  const auto it = m.find(issueId);
  if (it == m.end() || index >= it->second.size()) return noSuchLink();
  it->second.erase(it->second.begin() + static_cast<std::ptrdiff_t>(index));
  if (it->second.empty()) m.erase(it);
  return okStatus();
}

Status linkReorder(LinkMap& m, const std::string& issueId, std::size_t from, std::size_t to) {
  const auto it = m.find(issueId);
  if (it == m.end() || from >= it->second.size() || to >= it->second.size()) return noSuchLink();
  if (from == to) return okStatus();
  auto& v = it->second;
  Link l = std::move(v[from]);
  v.erase(v.begin() + static_cast<std::ptrdiff_t>(from));
  v.insert(v.begin() + static_cast<std::ptrdiff_t>(to), std::move(l));
  return okStatus();
}

Result<Link> linkMove(LinkMap& m, const std::string& fromIssue, std::size_t index, const std::string& toIssue) {
  const auto it = m.find(fromIssue);
  if (it == m.end() || index >= it->second.size()) return noSuchLink();
  if (fromIssue == toIssue) return Error{"移す先が同じ課題です（並べ替えは linkReorder）"};
  if (auto e = checkRoom(m, toIssue, it->second[index].target, false)) return *e;
  Link l = std::move(it->second[index]);
  it->second.erase(it->second.begin() + static_cast<std::ptrdiff_t>(index));
  if (it->second.empty()) m.erase(it);
  m[toIssue].push_back(l);
  return l;
}

Result<Link> linkCopy(LinkMap& m, const IClock& clock, const std::string& fromIssue, std::size_t index, const std::string& toIssue) {
  const auto it = m.find(fromIssue);
  if (it == m.end() || index >= it->second.size()) return noSuchLink();
  if (fromIssue == toIssue) return Error{"コピー先が同じ課題です"};
  if (auto e = checkRoom(m, toIssue, it->second[index].target, true)) return *e;
  Link l = it->second[index];
  l.addedAt = clock.epochMs();
  m[toIssue].push_back(l);
  return l;
}

LinkPasteOutcome linkAddLines(LinkMap& m, const IClock& clock, const std::string& issueId, std::string_view text) {
  LinkPasteOutcome out;
  std::size_t lineNo = 0, read = 0;
  std::size_t i = 0;
  while (i <= text.size()) {
    std::size_t e = text.find_first_of("\r\n", i);
    if (e == std::string_view::npos) e = text.size();
    const std::string_view line = text.substr(i, e - i);
    ++lineNo;
    // 改行は \n・\r\n・\r のどれでも 1 つ
    if (e < text.size() && text[e] == '\r' && e + 1 < text.size() && text[e + 1] == '\n') ++e;
    i = e + 1;
    if (text::trim(line).empty()) continue;
    if (read >= kMaxPasteLinkLines) {
      ++out.skipped;
      continue;
    }
    ++read;
    auto r = linkAdd(m, clock, issueId, "", line);
    if (r) out.added.push_back(std::move(r).value());
    else out.rejected.push_back({lineNo, r.error()});
  }
  return out;
}

Json linksToJson(const LinkMap& m) {
  Json o = Json::object();
  for (const auto& [id, list] : m) {
    if (list.empty()) continue;
    Json arr = Json::array();
    for (const Link& l : list) {
      Json e = Json{{"title", l.title}, {"target", l.target}, {"at", l.addedAt}};
      if (l.extra.is_object())
        for (auto it = l.extra.begin(); it != l.extra.end(); ++it)
          if (it.key() != "title" && it.key() != "target" && it.key() != "at") e[it.key()] = it.value();
      arr.push_back(std::move(e));
    }
    o[id] = std::move(arr);
  }
  return o;
}

LinkMap linksFromJson(const Json& j) {
  LinkMap m;
  if (!j.is_object()) return m;
  std::size_t total = 0;
  for (auto it = j.begin(); it != j.end(); ++it) {
    if (!it.value().is_array() || !isLinkIssueId(it.key())) continue;
    std::vector<Link> list;
    std::set<std::string> keys;  // 重複の判定（件数が多くても遅くならないよう、鍵で引く）
    for (const Json& x : it.value()) {
      if (total >= kMaxLinksTotal || list.size() >= kMaxLinksPerIssue) break;
      if (!x.is_object() || !x.contains("target") || !x["target"].is_string()) continue;
      const LinkClass c = classifyLink(x["target"].get_ref<const std::string&>());
      if (!c.ok() || !keys.insert(linkKey(c.target)).second) continue;
      Link l;
      l.target = c.target;
      if (x.contains("title") && x["title"].is_string()) l.title = sanitizeLinkTitle(x["title"].get_ref<const std::string&>());
      if (l.title.empty()) l.title = defaultTitle(l.target);
      if (x.contains("at")) {
        const Json& at = x["at"];
        if (at.is_number_unsigned()) {
          const std::uint64_t v = at.get<std::uint64_t>();
          if (v <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) l.addedAt = static_cast<std::int64_t>(v);
        } else if (at.is_number_integer()) {
          l.addedAt = std::max<std::int64_t>(0, at.get<std::int64_t>());
        }
      }
      for (auto k = x.begin(); k != x.end(); ++k) {
        if (k.key() == "title" || k.key() == "target" || k.key() == "at") continue;
        if (!l.extra.is_object()) l.extra = Json::object();
        l.extra[k.key()] = k.value();
      }
      list.push_back(std::move(l));
      ++total;
    }
    if (!list.empty()) m[it.key()] = std::move(list);
  }
  return m;
}

}  // namespace roadmap
