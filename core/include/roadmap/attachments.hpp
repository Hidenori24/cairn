// 課題への添付ファイル（SPEC 8.7）。ファイルシステムは IFileStore 経由で、GUI・Win32 には触れない。
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

std::size_t attachmentCount(const AttachmentMap& m, const std::string& issueId);
std::uint64_t attachmentsTotalBytes(const AttachmentMap& m);

// 開く前に警告する種類（実行ファイル・スクリプト・ショートカットなど。拡張子で判断する）
bool isRiskyFile(const std::string& name);
// 画像（サムネイル・プレビューの対象。拡張子で判断する）
bool isImageFile(const std::string& name);
// "999 B" / "1.5 KB" / "5.0 MB" / "3.00 GB"
std::string formatBytes(std::uint64_t n);

}  // namespace roadmap
