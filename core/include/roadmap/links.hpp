// 課題の関連リンク（URL・社内ファイル・フォルダ。SPEC 8.10・10.10。Issue #60）。
// 添付（attachments.hpp）はファイルの本体をコピーするが、リンクは「場所を指すだけ」で何もコピーしない。
//  - 保存先は project.settings.json の "links"（Issue Id → [{title, target, at}]）。CSV は変えない。
//    パスは環境依存なので、共有・バックアップの JSON には入れない（recordToShareJson は項目を選んで書く）
//  - 種類の判定は文字列だけで行う（存在確認や I/O をしない。ネットワークドライブで固まらないため）
//  - 開けるのは許可リストだけ: http://・https://・file://・UNC（\\server\share\…）・ドライブ付き（C:\…）。
//    javascript:・ms-msdt:・data:・ftp: など、それ以外のスキームは Rejected（理由つき）
//  - URL・パス・表示名は社内の機微な情報を含みうるので、ログにも、理由の文言にも入れない
// UI・Win32 に依存しない。例外は投げず、失敗は戻り値で返す。
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "roadmap/clock.hpp"
#include "roadmap/result.hpp"

namespace roadmap {

inline constexpr std::size_t kMaxLinkUrlBytes = 2048;   // URL（http・https）の長さの上限
inline constexpr std::size_t kMaxLinkPathBytes = 1024;  // パス（file://・UNC・ドライブ付き）の長さの上限
inline constexpr std::size_t kMaxLinkTitleBytes = 200;  // 表示名の長さの上限
inline constexpr std::size_t kMaxLinksPerIssue = 50;    // 1 課題のリンクの数
inline constexpr std::size_t kMaxLinksTotal = 5000;     // プロジェクト全体のリンクの数
inline constexpr std::size_t kMaxPasteLinkLines = 20;   // 「貼り付けて追加」で一度に読む行の数

// 場所の種類。判定の結果を増やしやすいよう enum class にする（新しい種類は末尾に足す）
enum class LinkKind {
  Web,       // http:// https://
  File,      // ファイル（file://・UNC・ドライブ付き。末尾が \ / でないもの）
  Folder,    // フォルダ（末尾が \ / のもの。\\server\share だけの共有のルートも）
  Unknown,   // 何の場所か判定できない（空・スキームのない相対パス・ふつうの文章など）
  Rejected,  // 許可リストにない（開かない）。デバイスのパス・上限を超える長さ・使えない文字なども
};
// 開ける（追加してよい）種類か
inline constexpr bool linkKindOpenable(LinkKind k) { return k == LinkKind::Web || k == LinkKind::File || k == LinkKind::Folder; }
// 画面に出す種類の名前（"Web" "ファイル" "フォルダ"。開けない種類は "不明"）
const char* linkKindLabel(LinkKind k);

struct LinkClass {
  LinkKind kind = LinkKind::Unknown;
  std::string target;  // 正規化した場所（制御文字・前後の空白・囲みの引用符を取り、Web はスキームを小文字に、URL の空白を %20 にする）
  std::string reason;  // Unknown・Rejected のときの理由（日本語。入力の中身は入れない）
  bool ok() const { return linkKindOpenable(kind); }
};
// 場所の文字列を正規化して種類を判定する。文字列だけで決める（存在確認・I/O をしない）。
// 前後の空白・" や “” の囲み・< > の囲み・制御文字（改行を含む）・ゼロ幅の文字を取り除く。
// 長さの上限: Web は kMaxLinkUrlBytes、そのほかは kMaxLinkPathBytes（正規化したあとの長さ）
LinkClass classifyLink(std::string_view raw);

// 表示名を整える: 制御文字を除き、前後の空白を取り、kMaxLinkTitleBytes に切り詰める（文字の途中では切らない）。不正な UTF-8 は U+FFFD にする
std::string sanitizeLinkTitle(std::string_view title);
// 表示名を省略したときの名前。Web = ホスト + 末尾の 2 区画（github.com/issues/60）、ファイル = ファイル名、
// フォルダ = 最後のフォルダ名。URL の %XX は（UTF-8 として正しければ）戻す
std::string defaultTitle(std::string_view target);
// 開く前に警告する場所か（実行ファイル・スクリプト・ショートカットなど）。ファイルの場所だけが対象
// （Web はブラウザが扱う。フォルダは実行しない）。判定は添付と同じ isRiskyFile（attachments.hpp）。
// file:// の %XX・末尾の空白や「.」（Windows は無視する）・# 以降を見て、すり抜けさせない
bool isRiskyTarget(std::string_view target);
// ファイルの場所の親フォルダ（末尾に \ か / が付いた、Folder として判定できる場所）。ファイル以外・親がないものは空
std::string linkFolderOf(std::string_view target);
// 同じ場所か（ファイルとフォルダは大文字小文字と \ / の違いを無視。Web は完全一致）。重複の判定に使う
bool sameLinkTarget(std::string_view a, std::string_view b);

struct Link {
  std::string title;         // 表示名
  std::string target;        // 場所（classifyLink で正規化したもの）
  std::int64_t addedAt = 0;  // 追加した時刻（UTC のミリ秒。IClock::epochMs）
  // 読み込んだ未知のキー（書き戻す）。あとから項目を足した版で保存した設定を、この版で開いて保存しても消えないように
  nlohmann::ordered_json extra;
  bool operator==(const Link&) const = default;
};
// Issue Id → リンク（追加した順）。CSV の Issue Id で引く（表示 ID は階層で変わるため使わない）
using LinkMap = std::map<std::string, std::vector<Link>>;

std::size_t linkCount(const LinkMap& m, const std::string& issueId);
std::size_t linkTotal(const LinkMap& m);
// リンクを置いてよい Issue Id か（設定ファイルに保存して読み直せる形: isSafePathComponent を通り、64 バイトまで）
bool isLinkIssueId(std::string_view id);

// 追加する。title が空なら defaultTitle。断る理由（開けない種類・上限・重複・Issue Id）をエラーで返し、何も変えない
Result<Link> linkAdd(LinkMap& m, const IClock& clock, const std::string& issueId, std::string_view title,
                     std::string_view target);
// 直す（追加した時刻は変えない）。重複（自分以外に同じ場所）・開けない種類は断る
Status linkEdit(LinkMap& m, const std::string& issueId, std::size_t index, std::string_view title, std::string_view target);
Status linkRemove(LinkMap& m, const std::string& issueId, std::size_t index);
// 同じ課題の中で並べ替える: from のリンクを、取り除いたあとの添字 to の位置へ入れる（添付の reorderAttachment と同じ）
Status linkReorder(LinkMap& m, const std::string& issueId, std::size_t from, std::size_t to);
// 別の課題へ移す（追加した時刻は引き継ぐ。先の末尾に足す）。先に同じ場所があれば断る。元と先が同じならエラー。
// 先の課題が実在するかは呼び出し側が確かめる（core は課題の一覧を持たない）
Result<Link> linkMove(LinkMap& m, const std::string& fromIssue, std::size_t index, const std::string& toIssue);
// 別の課題へコピーする（元は残る。追加した時刻は「いま」にする）
Result<Link> linkCopy(LinkMap& m, const IClock& clock, const std::string& fromIssue, std::size_t index,
                      const std::string& toIssue);

struct LinkLineRejected {
  std::size_t line = 0;  // 何行目か（1 から。空行も数える）
  std::string reason;
};
struct LinkPasteOutcome {
  std::vector<Link> added;
  std::vector<LinkLineRejected> rejected;
  std::size_t skipped = 0;  // kMaxPasteLinkLines を超えて読まなかった行（空でない行）の数
};
// 貼り付けたテキストの各行を 1 行 1 リンク（表示名なし）として追加する。空行は読み飛ばし、読むのは先頭の kMaxPasteLinkLines 行まで。
// 断った行は理由つき（行の中身は入れない）で rejected に入れ、ほかは続ける
LinkPasteOutcome linkAddLines(LinkMap& m, const IClock& clock, const std::string& issueId, std::string_view text);

// settings.json の "links" との変換。読み込みでは壊れた値を読み飛ばす:
// 型が違う・Issue Id が isLinkIssueId を通らない・target が開けない種類（Rejected・Unknown・長さ超過）・同じ課題の中の重複・件数の超過は
// その項目を捨てる。表示名が壊れていれば defaultTitle、長ければ切り詰め、at が壊れていれば 0 にする（項目は残す）。未知のキーは extra に残す
nlohmann::ordered_json linksToJson(const LinkMap& m);
LinkMap linksFromJson(const nlohmann::ordered_json& j);

}  // namespace roadmap
