// 新しい課題の名前の既定値（アプリの設定「新しい課題の名前」）を作る純粋関数
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace roadmap {

// 名前を付けなかったときの課題名
inline constexpr const char* kDefaultIssueName = "新しい課題";

enum class NewNameMode {
  Keep,           // 今のまま（既定名）
  ParentSeq,      // 親の名前＋連番   例: 要件定義の子 3
  ParentDefault,  // 親の名前／既定名 例: 要件定義／新しい課題
  Custom,         // 自由入力。{親} {親ID} {n} {日付}
};
const char* newNameModeKey(NewNameMode m);  // "keep" / "parentSeq" / "parentDefault" / "custom"
NewNameMode newNameModeFromKey(const std::string& k);  // 知らないキーは Keep

constexpr std::size_t kNewNameCustomMax = 200;  // 自由入力の長さの上限（バイト）
std::string clampNewNameCustom(std::string s);  // 上限に切る（UTF-8 の文字の途中では切らない）

struct NewNameInput {
  NewNameMode mode = NewNameMode::Keep;
  std::string custom;               // Custom のときの形
  bool hasParent = false;           // false = 最上位に作る
  std::string parentName;           // 親の表示名
  std::string parentDid;            // 親の表示ID
  std::vector<std::string> siblings;  // 作る場所にすでにある兄弟の名前（連番が重ならないように見る）
  std::string today;                // {日付} に入れる "YYYY-MM-DD"
};

// 名前を返す。空 = 既定名（kDefaultIssueName）のまま
std::string makeNewIssueName(const NewNameInput& in);

}  // namespace roadmap
