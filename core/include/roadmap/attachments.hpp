// 課題への添付ファイル（SPEC 8.9）。ファイルシステムは IFileStore 経由で、GUI・Win32 には触れない。
//   本体: <プロジェクトのフォルダ>/添付/<Issue Id>/<ファイル名>（同名は -2、-3 …）
//   削除: <プロジェクトのフォルダ>/_削除済み/<Issue Id>/<ファイル名>へ移す（消さない）
//   メタ情報（名前・サイズ・日時・作成者）: settings.json の "attachments"（ProjectSettings::attachments）
// 個人情報を含みうるので、ファイル名・中身はログに出さない（呼び出し側は結果の理由だけを画面に出す）。
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "roadmap/clock.hpp"
#include "roadmap/filestore.hpp"
#include "roadmap/model.hpp"
#include "roadmap/result.hpp"

namespace roadmap {

inline constexpr std::uint64_t kMaxAttachFileBytes = 50ull * 1024 * 1024;        // 1ファイル
inline constexpr std::uint64_t kMaxAttachProjectBytes = 1024ull * 1024 * 1024;   // プロジェクト合計
inline constexpr std::size_t kMaxDropFiles = 500;                                  // 1回のドロップで付ける数
inline constexpr const char* kAttachDeleted = "_削除済み";                          // プロジェクトのフォルダの中

struct AttachLimits {
  std::uint64_t perFile = kMaxAttachFileBytes;
  std::uint64_t perProject = kMaxAttachProjectBytes;
  std::size_t maxFiles = kMaxDropFiles;
};

struct AttachRejected {
  std::string name;    // 断ったファイルの名前（フォルダが多すぎるときは空）
  std::string reason;  // 画面に出す理由（日本語）
};
struct AttachOutcome {
  std::vector<Attachment> added;
  std::vector<AttachRejected> rejected;
};

// ドロップされたパス（ファイルまたはフォルダ）を、ファイルの一覧にする。フォルダは中のファイルを全部（サブフォルダも、
// 名前順）。存在しないパスはそのまま残す（付けるときに理由つきで断る）。maxFiles を超えたら打ち切って truncated
struct ExpandedFiles {
  std::vector<std::string> files;
  bool truncated = false;
};
ExpandedFiles expandDropped(IFileStore& fs, const std::vector<std::string>& paths, std::size_t maxFiles = kMaxDropFiles);

// 添付の本体のパス
std::string attachmentPath(const std::string& projectDir, const std::string& issueId, const Attachment& a);

// ファイル（またはフォルダ）を付ける。断ったものは rejected に理由を入れ、ほかは続ける。
// 本体を書けたものだけ meta に足す（書けなければ増やさない）
AttachOutcome attachFiles(IFileStore& fs, const IClock& clock, const std::string& projectDir, AttachmentMap& meta,
                          const std::string& issueId, const std::vector<std::string>& files, const std::string& who,
                          const AttachLimits& lim = {});
// 中身を直接付ける（クリップボードの画像など）。断るときは理由をエラーで返す
Result<Attachment> attachData(IFileStore& fs, const IClock& clock, const std::string& projectDir, AttachmentMap& meta,
                              const std::string& issueId, const std::string& name, const std::string& data,
                              const std::string& who, const AttachLimits& lim = {});
// 削除: 本体を _削除済み へ移し、メタ情報から外す（本体が先に無くなっていてもメタ情報は外す）。移せなければ何も変えない
Status removeAttachment(IFileStore& fs, const std::string& projectDir, AttachmentMap& meta, const std::string& issueId,
                        std::size_t index);

// 別の課題へ移す（Issue #19）: 本体を 添付/<元>/ から 添付/<先>/ へ、メタ情報（名前・日時・作成者）と一緒に動かす。
// 同名は連番（-2、-3 …）で、返す Attachment の name が変わりうる。先の末尾に足す。合計は変わらないので上限は見ない。
// 本体を移せなければ（元がない・先に書けない）何も変えない。元と先が同じ課題ならエラー（並べ替えは reorderAttachment）。
// 先の課題が実在するかは呼び出し側が確かめる（core は課題の一覧を持たない）
Result<Attachment> moveAttachment(IFileStore& fs, const std::string& projectDir, AttachmentMap& meta, const std::string& fromIssue,
                                  std::size_t index, const std::string& toIssue);
// 別の課題へコピーする: 本体を複製して先に付ける（日時と作成者は「いま付けた」ものにする）。元は残る。
// コピーは新しいファイルなので、1ファイルとプロジェクト合計の上限を、実際に読めた大きさで検査する。失敗したら何も変えない
Result<Attachment> copyAttachment(IFileStore& fs, const IClock& clock, const std::string& projectDir, AttachmentMap& meta,
                                  const std::string& fromIssue, std::size_t index, const std::string& toIssue,
                                  const std::string& who, const AttachLimits& lim = {});
// 同じ課題の中で並べ替える: from の添付を、取り除いたあとの添字 to の位置へ入れる（本体は動かさない）
Status reorderAttachment(AttachmentMap& meta, const std::string& issueId, std::size_t from, std::size_t to);

// Windows のクリップボードの DIB（CF_DIB / CF_DIBV5。BITMAPINFOHEADER 以降）に、14 バイトのファイルヘッダを足して BMP の
// ファイルのバイト列にする。壊れたヘッダ（大きさ 0・極端な大きさ・色数・ピクセルの欠け）は断る。プラットフォームを問わず試せるよう core に置く
Result<std::string> bmpFromDib(const std::string& dib);

std::size_t attachmentCount(const AttachmentMap& m, const std::string& issueId);
std::uint64_t attachmentsTotalBytes(const AttachmentMap& m);

// JIRA の CSV の Attachment 列（`日時;作成者;ファイル名;URL`。同じ見出しが複数）をメタ情報として読む。本体はない。
// 読み取り専用の表示用で、CSV の中身（Issue::extra）は変えないので、取り込み・書き出しで壊れない。
// 形に合わないセルは、全体をファイル名として返す
struct JiraAttachment {
  std::string at, who, name, url;
};
std::vector<JiraAttachment> jiraAttachmentsOf(const Issue& o, const ProjectMeta& meta);

// 開く前に警告する種類（実行ファイル・スクリプト・ショートカットなど。拡張子で判断する）
bool isRiskyFile(const std::string& name);
// 画像（サムネイル・プレビューの対象。拡張子で判断する）
bool isImageFile(const std::string& name);
// "999 B" / "1.5 KB" / "5.0 MB" / "3.00 GB"
std::string formatBytes(std::uint64_t n);

}  // namespace roadmap
