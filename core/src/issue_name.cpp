#include "roadmap/issue_name.hpp"

#include <cstddef>

#include "roadmap/text.hpp"

namespace roadmap {

const char* newNameModeKey(NewNameMode m) {
  switch (m) {
    case NewNameMode::ParentSeq: return "parentSeq";
    case NewNameMode::ParentDefault: return "parentDefault";
    case NewNameMode::Custom: return "custom";
    case NewNameMode::Keep: break;
  }
  return "keep";
}

NewNameMode newNameModeFromKey(const std::string& k) {
  for (NewNameMode m : {NewNameMode::ParentSeq, NewNameMode::ParentDefault, NewNameMode::Custom})
    if (k == newNameModeKey(m)) return m;
  return NewNameMode::Keep;
}

std::string clampNewNameCustom(std::string s) {
  if (s.size() <= kNewNameCustomMax) return s;
  std::size_t n = kNewNameCustomMax;
  while (n > 0 && (static_cast<unsigned char>(s[n]) & 0xC0) == 0x80) --n;
  s.resize(n);
  return s;
}

namespace {
bool taken(const std::vector<std::string>& sib, const std::string& name) {
  for (const auto& s : sib)
    if (text::trim(s) == name) return true;
  return false;
}

// {n} を兄弟の数+1 から数え、兄弟の名前と重ならない最初の名前にする。{n} がなければ1回だけ作る
template <class F>
std::string withSeq(const NewNameInput& in, bool usesSeq, F make) {
  std::size_t n = in.siblings.size() + 1;
  std::string name = text::trim(make(n));
  if (!usesSeq) return name;
  // 兄弟は高々 siblings.size() 件なので、そのうち必ず空きが見つかる
  for (std::size_t k = 0; k <= in.siblings.size() && taken(in.siblings, name); ++k) name = text::trim(make(++n));
  return name;
}

bool has(const std::string& s, const char* part) { return s.find(part) != std::string::npos; }

// 形の {親ID} {親} {日付} {n} を1回の走査で置き換える（親の名前の中の { } は展開しない）
std::string expand(const std::string& form, const NewNameInput& in, std::size_t n) {
  struct Tok {
    const char* key;
    std::string val;
  };
  const Tok toks[] = {{"{親ID}", in.parentDid}, {"{親}", in.parentName}, {"{日付}", in.today}, {"{n}", std::to_string(n)}};
  std::string out;
  for (std::size_t i = 0; i < form.size();) {
    bool hit = false;
    if (form[i] == '{')
      for (const Tok& t : toks) {
        const std::string k = t.key;
        if (form.compare(i, k.size(), k) == 0) {
          out += t.val;
          i += k.size();
          hit = true;
          break;
        }
      }
    if (!hit) out += form[i++];
  }
  return out;
}
}  // namespace

std::string makeNewIssueName(const NewNameInput& in) {
  const auto seqName = [&](const std::string& head) {
    return withSeq(in, true, [&](std::size_t n) { return head + " " + std::to_string(n); });
  };
  switch (in.mode) {
    case NewNameMode::Keep: return "";
    case NewNameMode::ParentSeq:
      if (!in.hasParent) return seqName(kDefaultIssueName);
      return seqName(in.parentName + "の子");
    case NewNameMode::ParentDefault:
      if (!in.hasParent) return "";
      return in.parentName + "／" + kDefaultIssueName;
    case NewNameMode::Custom: break;
  }
  std::string form = text::replaceAll(in.custom, "\r", " ");
  form = text::replaceAll(form, "\n", " ");
  if (text::trim(form).empty()) return "";
  // 最上位に親はないので、{親}・{親ID} を含む形は使わず「新しい課題 n」にする
  if (!in.hasParent && (has(form, "{親}") || has(form, "{親ID}"))) return seqName(kDefaultIssueName);
  return withSeq(in, has(form, "{n}"), [&](std::size_t n) { return expand(form, in, n); });
}

}  // namespace roadmap
