#include "roadmap/github_ref.hpp"

#include <algorithm>
#include <set>
#include <utility>

#include <nlohmann/json.hpp>

#include "roadmap/text.hpp"

namespace roadmap {

namespace {
using GhJson = nlohmann::json;

constexpr const char* kNotGitHub =
    "GitHub の PR・Issue の URL（https://github.com/<owner>/<repo>/pull/<番号> か /issues/<番号>）として読めません";

bool isNameChar(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
}
// owner・repo として使える名前か（英数字と - _ .、1〜100 バイト。"." と ".." は不可）
bool validName(std::string_view s) {
  if (s.empty() || s.size() > kMaxGitHubNameBytes) return false;
  if (s == "." || s.find("..") != std::string_view::npos) return false;
  return std::all_of(s.begin(), s.end(), isNameChar);
}
// 1〜9 桁の数字で、先頭が 0 でなく、0 でもない
bool parseNumber(std::string_view s, std::uint32_t& out) {
  if (s.empty() || s.size() > 9 || s[0] == '0') return false;
  std::uint32_t v = 0;
  for (const char c : s) {
    if (c < '0' || c > '9') return false;
    v = v * 10 + static_cast<std::uint32_t>(c - '0');
  }
  if (v == 0 || v > kMaxGitHubNumber) return false;
  out = v;
  return true;
}
// 下位パスの区画に使える文字
bool isSubSegment(std::string_view s) {
  if (s.empty() || s == "." || s == "..") return false;
  return std::all_of(s.begin(), s.end(), [](char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~';
  });
}

bool digits(std::string_view s, std::size_t at, std::size_t n) {
  if (at + n > s.size()) return false;
  for (std::size_t i = at; i < at + n; ++i)
    if (s[i] < '0' || s[i] > '9') return false;
  return true;
}
int num(std::string_view s, std::size_t at, std::size_t n) {
  int v = 0;
  for (std::size_t i = at; i < at + n; ++i) v = v * 10 + (s[i] - '0');
  return v;
}
constexpr std::string_view kZeroTime = "0001-01-01T00:00:00Z";

// 日時の項目の読み取り結果
struct TimeField {
  enum class Kind { Absent, Valid, Bad } kind = Kind::Absent;
  std::string value;
};
TimeField readTime(const GhJson& o, const char* key) {
  TimeField f;
  const auto it = o.find(key);
  if (it == o.end() || it->is_null()) return f;
  if (!it->is_string()) {
    f.kind = TimeField::Kind::Bad;
    return f;
  }
  const std::string s = it->get<std::string>();
  if (s == kZeroTime || s.empty()) return f;  // gh は未設定の日時をゼロ値で出すことがある
  if (!isGhTimestamp(s)) {
    f.kind = TimeField::Kind::Bad;
    return f;
  }
  f.kind = TimeField::Kind::Valid;
  f.value = s;
  return f;
}

RemoteInfo unknown(const char* why) {
  RemoteInfo r;
  r.state = RemoteState::Unknown;
  r.reason = why;
  return r;
}
RemoteInfo known(RemoteState s, std::string closedAt = {}) {
  RemoteInfo r;
  r.state = s;
  r.closedAt = std::move(closedAt);
  return r;
}

// JSON のオブジェクトとして読む。読めなければ err に理由を入れて false
bool readObject(std::string_view text, GhJson& out, RemoteInfo& err) {
  if (text.size() > kMaxGhOutputBytes) {
    err = unknown("gh の出力が大きすぎます");
    return false;
  }
  out = GhJson::parse(text.begin(), text.end(), nullptr, /*allow_exceptions=*/false);
  if (out.is_discarded() || !out.is_object()) {
    err = unknown("gh の出力を読めませんでした");
    return false;
  }
  return true;
}
// 文字列の項目。なければ present = false。null は「なし」。文字列でない値は ok = false
struct StrField {
  bool ok = true;
  bool present = false;
  std::string value;
};
StrField readStr(const GhJson& o, const char* key) {
  StrField f;
  const auto it = o.find(key);
  if (it == o.end() || it->is_null()) return f;
  if (!it->is_string()) {
    f.ok = false;
    return f;
  }
  f.present = true;
  f.value = it->get<std::string>();
  return f;
}
// 日時の項目の型が正しいか（先に確かめる）
bool timesOk(const GhJson& o, TimeField& a, TimeField& b, const char* ka, const char* kb) {
  a = readTime(o, ka);
  b = readTime(o, kb);
  return a.kind != TimeField::Kind::Bad && b.kind != TimeField::Kind::Bad;
}

std::string quoteWinArg(std::string_view a) {
  if (!a.empty() && a.find_first_of(" \t\n\v\"") == std::string_view::npos) return std::string(a);
  std::string out = "\"";
  std::size_t backslashes = 0;
  for (const char ch : a) {
    if (ch == '\\') {
      ++backslashes;
    } else if (ch == '"') {
      out.append(backslashes * 2 + 1, '\\');
      out.push_back('"');
      backslashes = 0;
    } else {
      out.append(backslashes, '\\');
      backslashes = 0;
      out.push_back(ch);
    }
  }
  out.append(backslashes * 2, '\\');
  out.push_back('"');
  return out;
}
}  // namespace

// ---------------------------------------------------------------- URL
std::string GitHubRef::canonicalUrl() const {
  return "https://github.com/" + owner + "/" + repo + (kind == GitHubKind::Pull ? "/pull/" : "/issues/") + std::to_string(number);
}

Result<GitHubRef> parseGitHubUrl(std::string_view t) {
  const Error bad{kNotGitHub};
  if (t.empty() || t.size() > kMaxLinkUrlBytes) return bad;
  for (const char ch : t) {
    const unsigned char c = static_cast<unsigned char>(ch);
    if (c <= 0x20 || c >= 0x7F || c == '\\') return bad;  // 空白・制御文字・ASCII 以外・バックスラッシュ
  }
  constexpr std::string_view kScheme = "https://";
  if (t.size() <= kScheme.size() || !text::iequals(t.substr(0, kScheme.size()), kScheme)) return bad;
  const std::string_view rest = t.substr(kScheme.size());
  const std::size_t end = rest.find_first_of("/?#");
  if (end == std::string_view::npos || rest[end] != '/') return bad;
  // ホストは github.com だけ（www・ユーザー情報・ポート・末尾の「.」・別ホストはすべてここで断る）
  if (!text::iequals(rest.substr(0, end), "github.com")) return bad;
  const std::string_view afterHost = rest.substr(end);
  const std::size_t pe = afterHost.find_first_of("?#");
  const std::string_view path = afterHost.substr(0, pe);  // 「/」で始まる。?・# 以降は読まない
  if (path.find('%') != std::string_view::npos || path.find("..") != std::string_view::npos) return bad;
  std::vector<std::string_view> segs;
  for (std::string_view p = path.substr(1);;) {
    const std::size_t k = p.find('/');
    if (k == std::string_view::npos) {
      segs.push_back(p);
      break;
    }
    segs.push_back(p.substr(0, k));
    p = p.substr(k + 1);
  }
  if (!segs.empty() && segs.back().empty()) segs.pop_back();  // 末尾の「/」は 1 つだけ許す
  if (segs.size() < 4) return bad;
  GitHubRef ref;
  if (!validName(segs[0]) || !validName(segs[1])) return bad;
  ref.owner = std::string(segs[0]);
  ref.repo = std::string(segs[1]);
  if (segs[2] == "pull") ref.kind = GitHubKind::Pull;
  else if (segs[2] == "issues") ref.kind = GitHubKind::Issue;
  else return bad;
  if (!parseNumber(segs[3], ref.number)) return bad;
  for (std::size_t i = 4; i < segs.size(); ++i)
    if (!isSubSegment(segs[i])) return bad;  // /files /commits /checks などの下位パス
  return ref;
}

bool isGitHubTarget(std::string_view target) { return parseGitHubUrl(target).ok(); }

Status validateGitHubRef(const GitHubRef& ref) {
  if (!validName(ref.owner) || !validName(ref.repo) || ref.number == 0 || ref.number > kMaxGitHubNumber)
    return Error{"GitHub の PR・Issue の指定が正しくありません"};
  return okStatus();
}

// ---------------------------------------------------------------- 状態
const char* remoteStateLabel(RemoteState s) {
  switch (s) {
    case RemoteState::Open: return "open";
    case RemoteState::Draft: return "draft";
    case RemoteState::Merged: return "merged";
    case RemoteState::ClosedUnmerged: return "closed（マージなし）";
    case RemoteState::ClosedCompleted: return "closed（完了）";
    case RemoteState::ClosedNotPlanned: return "closed（計画外）";
    default: return "不明";
  }
}

bool isGhTimestamp(std::string_view s) {
  if (s.size() != 20 || s == kZeroTime) return false;
  if (!digits(s, 0, 4) || s[4] != '-' || !digits(s, 5, 2) || s[7] != '-' || !digits(s, 8, 2) || s[10] != 'T' || !digits(s, 11, 2) ||
      s[13] != ':' || !digits(s, 14, 2) || s[16] != ':' || !digits(s, 17, 2) || s[19] != 'Z')
    return false;
  const int y = num(s, 0, 4), mo = num(s, 5, 2), d = num(s, 8, 2), h = num(s, 11, 2), mi = num(s, 14, 2), se = num(s, 17, 2);
  if (y < 1970 || h > 23 || mi > 59 || se > 59) return false;
  return makeDay(y, mo, d).has_value();
}

std::optional<std::int64_t> epochSecondsOfUtc(std::string_view isoUtc) {
  if (!isGhTimestamp(isoUtc)) return std::nullopt;
  const OptDay day = makeDay(num(isoUtc, 0, 4), num(isoUtc, 5, 2), num(isoUtc, 8, 2));
  if (!day) return std::nullopt;
  return static_cast<std::int64_t>(*day) * 86400 + num(isoUtc, 11, 2) * 3600 + num(isoUtc, 14, 2) * 60 + num(isoUtc, 17, 2);
}

OptDay localDayOfUtc(std::string_view isoUtc, const UtcOffsetFn& offsetAt) {
  const auto epoch = epochSecondsOfUtc(isoUtc);
  if (!epoch) return std::nullopt;
  const std::int64_t offsetSec = static_cast<std::int64_t>(offsetAt ? offsetAt(*epoch) : 0) * 60;
  std::int64_t local = *epoch + offsetSec;
  std::int64_t days = local / 86400;
  if (local % 86400 < 0) --days;  // 床除算
  return static_cast<Day>(days);
}

OptDay localDayOfUtc(std::string_view isoUtc, int utcOffsetMinutes) {
  return localDayOfUtc(isoUtc, UtcOffsetFn([utcOffsetMinutes](std::int64_t) { return utcOffsetMinutes; }));
}

RemoteInfo parseGhPrJson(std::string_view json) {
  GhJson o;
  RemoteInfo err;
  if (!readObject(json, o, err)) return err;
  const StrField st = readStr(o, "state");
  if (!st.ok || !st.present) return unknown("gh の出力に状態がありません");
  TimeField merged, closed;
  if (!timesOk(o, merged, closed, "mergedAt", "closedAt")) return unknown("gh の出力の日時の形式が正しくありません");
  const auto dr = o.find("isDraft");
  const bool hasDraft = dr != o.end() && !dr->is_null();
  if (hasDraft && !dr->is_boolean()) return unknown("gh の出力の isDraft が正しくありません");
  const bool draft = hasDraft && dr->get<bool>();
  const bool hasMerged = merged.kind == TimeField::Kind::Valid, hasClosed = closed.kind == TimeField::Kind::Valid;
  if (st.value == "OPEN") {
    if (!hasDraft) return unknown("gh の出力に isDraft がありません");
    return known(draft ? RemoteState::Draft : RemoteState::Open);
  }
  if (st.value == "MERGED") {
    if (hasMerged) return known(RemoteState::Merged, merged.value);
    if (hasClosed) return known(RemoteState::Merged, closed.value);
    return unknown("マージした日が取得できませんでした");
  }
  if (st.value == "CLOSED") {
    if (hasMerged) return known(RemoteState::Merged, merged.value);  // 閉じていてもマージ済みならマージ済み
    if (hasClosed) return known(RemoteState::ClosedUnmerged, closed.value);
    return unknown("閉じた日が取得できませんでした");
  }
  return unknown("未対応の状態です");
}

RemoteInfo parseGhIssueJson(std::string_view json) {
  GhJson o;
  RemoteInfo err;
  if (!readObject(json, o, err)) return err;
  const StrField st = readStr(o, "state");
  if (!st.ok || !st.present) return unknown("gh の出力に状態がありません");
  const StrField reason = readStr(o, "stateReason");
  if (!reason.ok) return unknown("gh の出力の stateReason が正しくありません");
  const TimeField closed = readTime(o, "closedAt");
  if (closed.kind == TimeField::Kind::Bad) return unknown("gh の出力の日時の形式が正しくありません");
  if (st.value == "OPEN") {
    if (reason.present && !reason.value.empty() && reason.value != "REOPENED") return unknown("未対応の状態の理由です");
    return known(RemoteState::Open);
  }
  if (st.value == "CLOSED") {
    if (closed.kind != TimeField::Kind::Valid) return unknown("閉じた日が取得できませんでした");
    if (!reason.present || reason.value.empty()) return unknown("閉じた理由が取得できませんでした");
    if (reason.value == "COMPLETED") return known(RemoteState::ClosedCompleted, closed.value);
    if (reason.value == "NOT_PLANNED") return known(RemoteState::ClosedNotPlanned, closed.value);
    return unknown("未対応の閉じた理由です");
  }
  return unknown("未対応の状態です");
}

// ---------------------------------------------------------------- gh の起動
Result<std::vector<std::string>> ghArguments(const GitHubRef& ref) {
  if (auto v = validateGitHubRef(ref); !v) return Error{v.error()};
  const std::string url = ref.canonicalUrl();
  if (ref.kind == GitHubKind::Pull)
    return std::vector<std::string>{"pr", "view", url, "--json", "state,isDraft,mergedAt,closedAt"};
  return std::vector<std::string>{"issue", "view", url, "--json", "state,stateReason,closedAt"};
}

std::string windowsCommandLine(std::string_view exe, const std::vector<std::string>& args) {
  std::string out = quoteWinArg(exe);
  for (const std::string& a : args) {
    out.push_back(' ');
    out += quoteWinArg(a);
  }
  return out;
}

// ---------------------------------------------------------------- テスト用の窓口
void FakeRemoteStatus::set(const std::string& url, RemoteInfo info) {
  std::lock_guard<std::mutex> lk(mu_);
  answers_.insert_or_assign(url, Result<RemoteInfo>(std::move(info)));
}
void FakeRemoteStatus::fail(const std::string& url, std::string reason) {
  std::lock_guard<std::mutex> lk(mu_);
  answers_.insert_or_assign(url, Result<RemoteInfo>(Error{std::move(reason)}));
}
void FakeRemoteStatus::setDefaultFailure(std::string reason) {
  std::lock_guard<std::mutex> lk(mu_);
  defaultFailure_ = std::move(reason);
}
void FakeRemoteStatus::hold(bool on) {
  std::lock_guard<std::mutex> lk(mu_);
  hold_ = on;
  cv_.notify_all();
}
void FakeRemoteStatus::release() { hold(false); }
void FakeRemoteStatus::holdUrl(const std::string& url, bool on) {
  std::lock_guard<std::mutex> lk(mu_);
  if (on) heldUrls_.insert(url);
  else heldUrls_.erase(url);
  cv_.notify_all();
}
void FakeRemoteStatus::cancel() {
  std::lock_guard<std::mutex> lk(mu_);  // 待っている fetch に、token が中断されたことを見せる
  cv_.notify_all();
}
Result<RemoteInfo> FakeRemoteStatus::fetch(const GitHubRef& ref, const CancelToken& token) {
  const std::string url = ref.canonicalUrl();
  std::unique_lock<std::mutex> lk(mu_);
  ++calls_;
  ++running_;
  called_.push_back(url);
  // 中断は token で決める（fetch に入る前に中断されていても、すぐ戻る）
  cv_.wait(lk, [&] { return token.cancelled() || (!hold_ && heldUrls_.count(url) == 0); });
  --running_;
  if (token.cancelled()) return Error{"確認を中断しました"};
  const auto it = answers_.find(url);
  if (it == answers_.end()) return Error{defaultFailure_};
  return it->second;
}
int FakeRemoteStatus::calls() const {
  std::lock_guard<std::mutex> lk(mu_);
  return calls_;
}
std::vector<std::string> FakeRemoteStatus::calledUrls() const {
  std::lock_guard<std::mutex> lk(mu_);
  return called_;
}
int FakeRemoteStatus::running() const {
  std::lock_guard<std::mutex> lk(mu_);
  return running_;
}

// ---------------------------------------------------------------- 完了の提案
std::optional<CompletionProposal> proposeCompletion(const Issue& issue, const LinkMap& links, const RemoteStates& states,
                                                    int utcOffsetMinutes) {
  return proposeCompletion(issue, links, states, UtcOffsetFn([utcOffsetMinutes](std::int64_t) { return utcOffsetMinutes; }));
}

std::optional<CompletionProposal> proposeCompletion(const Issue& issue, const LinkMap& links, const RemoteStates& states,
                                                    const UtcOffsetFn& offsetAt) {
  if (issue.resolved || statusCat(issue.status) == Cat::Done) return std::nullopt;
  const auto it = links.find(issue.id);
  if (it == links.end()) return std::nullopt;
  CompletionProposal p;
  std::set<std::string> seen;
  OptDay latest;
  for (const Link& l : it->second) {
    const LinkClass k = classifyLink(l.target);
    if (!linkKindGitHub(k.kind)) continue;  // GitHub 以外のリンクは無視する
    const auto ref = parseGitHubUrl(k.target);
    if (!ref) continue;
    const std::string key = ref->canonicalUrl();
    if (!seen.insert(key).second) continue;
    const auto st = states.find(key);
    if (st == states.end() || !remoteStateClosed(st->second.state)) return std::nullopt;  // 未確認・open・draft・Unknown
    const OptDay day = localDayOfUtc(st->second.closedAt, offsetAt);
    if (!day) return std::nullopt;
    if (!latest || *day > *latest) latest = day;
    if (st->second.state == RemoteState::ClosedUnmerged) p.hasUnmerged = true;
    if (st->second.state == RemoteState::ClosedNotPlanned) p.hasNotPlanned = true;
    ++p.linkCount;
  }
  if (p.linkCount == 0 || !latest) return std::nullopt;
  p.resolved = *latest;
  p.resolvedIso = fmtIso(*latest);
  if (p.hasUnmerged && p.hasNotPlanned) p.caution = "マージなしで閉じられたもの・計画外で閉じられたものを含みます";
  else if (p.hasUnmerged) p.caution = "マージなしで閉じられたものを含みます";
  else if (p.hasNotPlanned) p.caution = "計画外で閉じられたものを含みます";
  return p;
}

}  // namespace roadmap
