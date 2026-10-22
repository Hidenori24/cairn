#include "roadmap/model.hpp"

#include <cstdlib>

#include "roadmap/sha256.hpp"
#include "roadmap/text.hpp"

namespace roadmap {

Json attachmentsToJson(const AttachmentMap& m) {
  Json o = Json::object();
  for (const auto& [id, list] : m) {
    if (list.empty()) continue;
    Json a = Json::array();
    for (const auto& x : list) {
      Json e = Json{{"name", x.name}, {"size", x.size}, {"at", x.at}, {"who", x.who}};
      if (isSha256Hex(x.sha256)) e["sha256"] = x.sha256;  // 空（古いメタ情報）なら出さない = 既存の設定ファイルは変わらない
      a.push_back(std::move(e));
    }
    o[id] = std::move(a);
  }
  return o;
}

// 設定 JSON から読んだ名前（課題の Issue Id のフォルダ名・ファイル名）が、添付フォルダの外を指さないか。
// 区切り（/ \）・「.」「..」・制御文字（NUL を含む）・Windows の予約文字（: * ? " < > |）・先頭や末尾の空白・末尾の「.」を拒否する
bool isSafePathComponent(std::string_view n) {
  if (n.empty() || n == "." || n == "..") return false;
  if (n.front() == ' ' || n.back() == ' ' || n.back() == '.') return false;
  for (const char ch : n) {
    const unsigned char c = static_cast<unsigned char>(ch);
    if (c < 0x20 || c == 0x7F) return false;
    if (std::string_view("/\\:*?\"<>|").find(ch) != std::string_view::npos) return false;
  }
  return true;
}

AttachmentMap attachmentsFromJson(const Json& j) {
  AttachmentMap m;
  if (!j.is_object()) return m;
  for (auto it = j.begin(); it != j.end(); ++it) {
    if (!it.value().is_array() || !isSafePathComponent(it.key())) continue;
    for (const auto& x : it.value()) {
      if (!x.is_object() || !x.contains("name") || !x["name"].is_string()) continue;
      Attachment a;
      a.name = x["name"].get<std::string>();
      if (!isSafePathComponent(a.name)) continue;
      // 非負の整数だけ読む（浮動小数点や範囲外の値は無視して既定のまま）
      if (x.contains("size") && x["size"].is_number_unsigned()) a.size = x["size"].get<std::uint64_t>();
      if (x.contains("at") && x["at"].is_string()) a.at = x["at"].get<std::string>();
      if (x.contains("who") && x["who"].is_string()) a.who = x["who"].get<std::string>();
      // 16進小文字 64 文字だけ読む（長さ・文字・型が壊れていれば空 = 不明のまま。項目は消さない）
      if (x.contains("sha256") && x["sha256"].is_string() && isSha256Hex(x["sha256"].get_ref<const std::string&>()))
        a.sha256 = x["sha256"].get<std::string>();
      m[it.key()].push_back(std::move(a));
    }
  }
  return m;
}

TagName tagOf(std::string_view s) {
  TagName none{"", std::string(s)};
  if (s.empty() || s[0] != '[') return none;
  const std::size_t close = s.find(']', 1);
  if (close == std::string_view::npos || close == 1) return none;
  std::size_t i = close + 1;
  while (i < s.size()) {
    std::size_t j = i;
    if (!text::isJsSpace(text::decodeAt(s, j))) break;
    i = j;
  }
  const std::string_view rest = s.substr(i);
  for (std::size_t k = 0; k < rest.size();)
    if (text::isLineTerminator(text::decodeAt(rest, k))) return none;
  return {std::string(s.substr(1, close - 1)), std::string(rest)};
}

bool isEpicType(std::string_view type) {
  return text::icontains(type, "epic") || type.find("エピック") != std::string_view::npos;
}

bool isMilestoneText(std::string_view s) {
  return text::icontains(s, "milestone") || s.find("マイルストーン") != std::string_view::npos;
}

bool isMilestone(const Issue& o) {
  if (isMilestoneText(o.type)) return true;
  for (const auto& l : o.labels)
    if (isMilestoneText(l)) return true;
  return false;
}

Cat statusCat(std::string_view status) {
  const std::string s = text::trim(status);
  static const char* const kDone[] = {"done", "closed", "resolved", "complete", "完了", "解決", "クローズ", "済"};
  for (const char* p : kDone)
    if (text::istartsWith(s, p)) return Cat::Done;
  static const char* const kProg[] = {"progress", "進行", "対応中", "doing", "working", "review", "レビュー"};
  for (const char* p : kProg)
    if (text::icontains(s, p)) return Cat::Prog;
  return Cat::Todo;
}

const char* catName(Cat c) {
  switch (c) {
    case Cat::Done: return "done";
    case Cat::Prog: return "prog";
    default: return "todo";
  }
}

int prioRank(std::string_view prio) {
  for (int i = 0; i < 5; ++i)
    if (text::iequals(prio, kPriorities[i])) return i;
  return 2;
}

std::string nextIssueId(const std::vector<Issue>& issues, const AttachmentMap& reserved, const LinkMap& reservedLinks) {
  double m = 0;
  const auto upto = [&m](const std::string& id) {
    if (!text::isAsciiDigits(id)) return;
    const double v = std::strtod(id.c_str(), nullptr);
    if (v > m) m = v;
  };
  for (const auto& o : issues) upto(o.id);
  // 削除した課題の添付が残っているとき（Undo で戻せる）、その id は再利用しない
  for (const auto& [id, list] : reserved)
    if (!list.empty()) upto(id);
  // 削除した課題のリンクも同じ（新しい課題が、前の課題のリンクを引き継がないように）
  for (const auto& [id, list] : reservedLinks)
    if (!list.empty()) upto(id);
  return text::jsNumber(m + 1);
}

}  // namespace roadmap
