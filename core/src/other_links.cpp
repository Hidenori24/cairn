#include "roadmap/other_links.hpp"

#include <algorithm>

#include "roadmap/csv.hpp"
#include "roadmap/text.hpp"

namespace roadmap {

namespace {

constexpr std::string_view kOutPrefix = "outward issue link";
constexpr std::string_view kInPrefix = "inward issue link";
constexpr std::string_view kOutJa = "外向き";
constexpr std::string_view kInJa = "内向き";

// 全角の括弧（UTF-8）
constexpr std::string_view kOpenFw = "（";
constexpr std::string_view kCloseFw = "）";

bool startsWith(std::string_view s, std::string_view p) { return s.compare(0, p.size(), p) == 0; }

// 見出し（元の大文字小文字のまま）の最後の括弧の中身。括弧がなければ nullopt
std::optional<std::string> lastParen(std::string_view h) {
  struct Open {
    std::size_t pos, len;
  };
  Open best{std::string_view::npos, 0};
  const auto consider = [&](std::size_t p, std::size_t len) {
    if (p != std::string_view::npos && (best.pos == std::string_view::npos || p > best.pos)) best = {p, len};
  };
  consider(h.rfind('('), 1);
  consider(h.rfind(kOpenFw), kOpenFw.size());
  if (best.pos == std::string_view::npos) return std::nullopt;
  const std::size_t from = best.pos + best.len;
  std::size_t end = std::string_view::npos;
  for (std::size_t i = from; i < h.size(); ++i) {
    if (h[i] == ')') {
      end = i;
      break;
    }
    if (h.compare(i, kCloseFw.size(), kCloseFw) == 0) {
      end = i;
      break;
    }
  }
  return text::trim(h.substr(from, end == std::string_view::npos ? std::string_view::npos : end - from));
}

bool sameKind(std::string_view a, std::string_view b) { return text::toLower(text::trim(a)) == text::toLower(text::trim(b)); }

std::string headerFor(std::string_view kind, bool outward) {
  return std::string(outward ? "Outward issue link (" : "Inward issue link (") + std::string(kind) + ")";
}

bool cellEmpty(const Issue& o, std::size_t col) { return col >= o.extra.size() || text::trim(o.extra[col]).empty(); }

}  // namespace

std::optional<LinkColumn> linkColumnOf(std::string_view header) {
  const std::string h = csv::norm(header);
  bool outward = true;
  std::size_t plen = 0;
  if (startsWith(h, kOutPrefix)) plen = kOutPrefix.size();
  else if (startsWith(h, kInPrefix)) (outward = false, plen = kInPrefix.size());
  else if (startsWith(h, kOutJa)) plen = kOutJa.size();
  else if (startsWith(h, kInJa)) (outward = false, plen = kInJa.size());
  else return std::nullopt;
  // 取り込み側（csv.cpp）が Blocks の列として日程に使うもの
  if (h.find("block", plen) != std::string::npos) return std::nullopt;
  LinkColumn c;
  c.outward = outward;
  if (auto p = lastParen(header)) c.kind = *p;
  else c.kind = text::trim(std::string_view(h).substr(plen));
  return c;
}

int findLinkTarget(const std::vector<Issue>& issues, std::string_view ref) {
  const std::string r = text::trim(ref);
  if (r.empty()) return -1;
  for (std::size_t i = 0; i < issues.size(); ++i)
    if (issues[i].id == r) return static_cast<int>(i);
  for (std::size_t i = 0; i < issues.size(); ++i)
    if (!issues[i].key.empty() && issues[i].key == r) return static_cast<int>(i);
  return -1;
}

std::vector<OtherLink> otherLinksOf(const ProjectMeta& meta, const std::vector<Issue>& issues, std::size_t idx) {
  std::vector<OtherLink> out;
  if (idx >= issues.size()) return out;
  const Issue& o = issues[idx];
  for (std::size_t c = 0; c < meta.extraHeaders.size() && c < o.extra.size(); ++c) {
    const std::string ref = text::trim(o.extra[c]);
    if (ref.empty()) continue;
    const auto col = linkColumnOf(meta.extraHeaders[c]);
    if (!col) continue;
    out.push_back({c, col->kind, col->outward, ref, findLinkTarget(issues, ref)});
  }
  return out;
}

std::optional<std::string> checkLinkKind(std::string_view kindIn) {
  const std::string kind = text::trim(kindIn);
  if (kind.empty()) return std::string("リンクの種類を入力してください");
  for (const char ch : kind)
    if (static_cast<unsigned char>(ch) < 0x20 || ch == '(' || ch == ')')
      return std::string("リンクの種類に括弧・改行は使えません");
  if (kind.find(kOpenFw) != std::string::npos || kind.find(kCloseFw) != std::string::npos)
    return std::string("リンクの種類に括弧・改行は使えません");
  const std::string n = csv::norm(kind);
  if (n.find("block") != std::string::npos)
    return std::string("Blocks は日程に使う依存関係です。先行・後続から設定してください");
  // 取り込みは見出しに "start date" / "epic name" を含む列を別の列として読む
  if (n.find("start date") != std::string::npos || n.find("epic name") != std::string::npos)
    return std::string("この名前は別の列として読まれるため使えません");
  // 書き出して読み直したとき、同じ種類のリンク列として戻ること
  const auto col = linkColumnOf(headerFor(kind, true));
  if (!col || col->kind != kind) return std::string("この名前は JIRA の列名として使えません");
  return std::nullopt;
}

std::vector<std::string> otherLinkKinds(const ProjectMeta& meta) {
  std::vector<std::string> out;
  for (const auto& h : meta.extraHeaders) {
    const auto col = linkColumnOf(h);
    if (!col || col->kind.empty()) continue;
    if (std::none_of(out.begin(), out.end(), [&](const std::string& k) { return sameKind(k, col->kind); }))
      out.push_back(col->kind);
  }
  return out;
}

std::string checkAddOtherLink(const ProjectMeta& meta, const std::vector<Issue>& issues, std::size_t idx,
                              std::string_view kindIn, bool outward, std::string_view refIn) {
  if (idx >= issues.size()) return "課題が見つかりません";
  if (auto why = checkLinkKind(kindIn)) return *why;
  const std::string ref = text::trim(refIn);
  if (ref.empty()) return "相手の課題の ID かキーを入力してください";
  const int target = findLinkTarget(issues, ref);
  if (target == static_cast<int>(idx)) return "自分自身にはつなげません";
  for (const auto& l : otherLinksOf(meta, issues, idx))
    if (l.outward == outward && sameKind(l.kind, kindIn) && (l.ref == ref || (target >= 0 && l.target == target)))
      return "すでに設定されています";
  return {};
}

std::string addOtherLink(ProjectMeta& meta, std::vector<Issue>& issues, std::size_t idx, std::string_view kindIn,
                         bool outward, std::string_view refIn) {
  if (auto why = checkAddOtherLink(meta, issues, idx, kindIn, outward, refIn); !why.empty()) return why;
  const std::string kind = text::trim(kindIn);
  const std::string ref = text::trim(refIn);
  Issue& o = issues[idx];
  // 同じ種類・向きで、この課題のセルが空の列
  std::size_t col = meta.extraHeaders.size();
  for (std::size_t c = 0; c < meta.extraHeaders.size(); ++c) {
    const auto lc = linkColumnOf(meta.extraHeaders[c]);
    if (lc && lc->outward == outward && sameKind(lc->kind, kind) && cellEmpty(o, c)) {
      col = c;
      break;
    }
  }
  if (col == meta.extraHeaders.size()) {
    meta.extraHeaders.push_back(headerFor(kind, outward));
    for (auto& x : issues) x.extra.resize(meta.extraHeaders.size());
  }
  if (o.extra.size() < meta.extraHeaders.size()) o.extra.resize(meta.extraHeaders.size());
  o.extra[col] = ref;
  return {};
}

bool removeOtherLink(const ProjectMeta& meta, std::vector<Issue>& issues, std::size_t idx, std::size_t col) {
  if (idx >= issues.size() || col >= meta.extraHeaders.size() || !linkColumnOf(meta.extraHeaders[col])) return false;
  Issue& o = issues[idx];
  if (cellEmpty(o, col)) return false;
  o.extra[col].clear();
  return true;
}

}  // namespace roadmap
