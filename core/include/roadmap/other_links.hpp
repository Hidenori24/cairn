// Blocks 以外の JIRA リンク（Relates・Duplicate など）の解釈と編集（Issue #12）。UI から独立した純粋関数。
// これらのリンクは日程の計算には使わない。値は「未知の列」(meta.extraHeaders / Issue::extra) にそのまま残り、
// 書き出しでは取り込んだときの列名・列の位置・値のまま戻る。
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "roadmap/model.hpp"

namespace roadmap {

// リンク列の見出しの解釈
struct LinkColumn {
  std::string kind;  // リンクの種類（見出しの括弧の中。例 "Relates"）。括弧がなければ接頭辞より後ろ
  bool outward = true;
};

// 見出しが Blocks 以外のリンク列なら種類と向きを返す。
// 対象: "Outward issue link (…)" / "Inward issue link (…)" / "外向き …" / "内向き …"。
// Blocks を含む列はここでは扱わない（日程に使う列）
std::optional<LinkColumn> linkColumnOf(std::string_view header);

// 課題 1 件の関連リンク 1 つ
struct OtherLink {
  std::size_t col = 0;  // extra / extraHeaders の添字
  std::string kind;
  bool outward = true;
  std::string ref;  // CSV のセルの値（課題キーか Issue Id）
  int target = -1;  // 相手の課題の添字（見つからなければ -1。ref は文字のまま出す）
};

// ref に当たる課題の添字（Issue Id → 課題キーの順に完全一致。なければ -1）
int findLinkTarget(const std::vector<Issue>& issues, std::string_view ref);

// issues[idx] の関連リンク（列の並び順）。空のセルは含めない
std::vector<OtherLink> otherLinksOf(const ProjectMeta& meta, const std::vector<Issue>& issues, std::size_t idx);

// 追加できる種類の名前か。空・括弧・改行・Blocks など、書き出して読み直すと別の列になる名前は不可で、理由（日本語）を返す
std::optional<std::string> checkLinkKind(std::string_view kind);

// プロジェクトにあるリンクの種類（出現順・大文字小文字を区別せず重複なし）
std::vector<std::string> otherLinkKinds(const ProjectMeta& meta);

// 足せるかの検査。足せないときは理由（日本語）を返す（空 = 足せる）
std::string checkAddOtherLink(const ProjectMeta& meta, const std::vector<Issue>& issues, std::size_t idx,
                              std::string_view kind, bool outward, std::string_view ref);

// issues[idx] に関連リンクを足す。同じ種類・向きの空のセルがあればそこへ、なければ列を末尾に足す（全課題の extra を伸ばす）。
// 失敗したとき（checkAddOtherLink が理由を返すとき）は何も変えず、その理由を返す（空 = 成功）
std::string addOtherLink(ProjectMeta& meta, std::vector<Issue>& issues, std::size_t idx, std::string_view kind,
                         bool outward, std::string_view ref);

// 列 col のセルを空にする（列は残す。往復で列の並びが変わらないように）。変えたら true
bool removeOtherLink(const ProjectMeta& meta, std::vector<Issue>& issues, std::size_t idx, std::size_t col);

}  // namespace roadmap
