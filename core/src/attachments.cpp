#include "roadmap/attachments.hpp"

#include <algorithm>
#include <cstdio>
#include <functional>
#include <set>

#include "roadmap/store.hpp"
#include "roadmap/text.hpp"

namespace roadmap {

namespace {
constexpr int kMaxDepth = 12;           // フォルダのドロップで降りる深さ
constexpr std::size_t kNameHeadroom = 6;  // 連番（-2 … -999）を付けても収まる余裕

std::string baseName(const std::string& path) {
  const auto k = path.find_last_of("/\\");
  return k == std::string::npos ? path : path.substr(k + 1);
}

std::string extOf(const std::string& name) {
  const auto k = name.rfind('.');
  if (k == std::string::npos || k == 0) return "";
  return text::toLower(name.substr(k + 1));
}

void walk(IFileStore& fs, const std::string& path, int depth, std::size_t maxFiles, ExpandedFiles& out) {
  if (out.truncated) return;
  const auto st = fs.stat(path);
  if (!st || !st->isDir) {
    if (out.files.size() >= maxFiles) {
      out.truncated = true;
      return;
    }
    out.files.push_back(path);
    return;
  }
  if (depth >= kMaxDepth) return;
  auto listed = fs.list(path);
  if (!listed) return;
  std::vector<FileInfo> items = std::move(*listed);
  std::sort(items.begin(), items.end(), [](const FileInfo& a, const FileInfo& b) { return a.name < b.name; });
  for (const auto& f : items) walk(fs, joinPath(path, f.name), depth + 1, maxFiles, out);
}

std::set<std::string> lowerNamesIn(IFileStore& fs, const std::string& dir) {
  std::set<std::string> used;
  if (auto l = fs.list(dir))
    for (const auto& f : *l) used.insert(text::toLower(f.name));
  return used;
}

// 上限の検査 → 本体を書く → メタ情報に足す
Result<Attachment> storeOne(IFileStore& fs, const IClock& clock, const std::string& projectDir, AttachmentMap& meta,
                            const std::string& issueId, const std::string& rawName, std::uint64_t size,
                            const std::function<Result<std::string>()>& getData, const std::string& who,
                            const AttachLimits& lim) {
  if (projectDir.empty()) return Error{"このプロジェクトには添付を置く場所がありません"};
  if (issueId.empty()) return Error{"添付先の課題がありません"};
  if (size > lim.perFile)
    return Error{"1ファイルの上限（" + formatBytes(lim.perFile) + "）を超えています（" + formatBytes(size) + "）"};
  const std::uint64_t total = attachmentsTotalBytes(meta);
  if (total + size > lim.perProject)
    return Error{"プロジェクト全体の合計の上限（" + formatBytes(lim.perProject) + "）を超えます（いま " + formatBytes(total) + "）"};
  const std::string dir = paths::attachmentDir(projectDir, issueId);
  if (auto s = fs.makeDirs(dir); !s) return Error{"添付のフォルダを作れません: " + s.error()};
  std::set<std::string> used = lowerNamesIn(fs, dir);
  if (const auto it = meta.find(issueId); it != meta.end())
    for (const auto& a : it->second) used.insert(text::toLower(a.name));
  std::string name = paths::fitFileName(dir, paths::safeFileName(rawName), paths::kMaxPathUnits - kNameHeadroom);
  if (name.empty()) return Error{"保存先のパスが長すぎて、このファイル名では置けません"};
  name = paths::uniqueName(used, name, true);
  auto data = getData();
  if (!data) return Error{data.error()};
  if (data->size() > lim.perFile)  // 読んでいる間に大きくなった
    return Error{"1ファイルの上限（" + formatBytes(lim.perFile) + "）を超えています"};
  if (auto s = fs.writeAtomic(joinPath(dir, name), *data); !s) return Error{"書き込めません: " + s.error()};
  Attachment a;
  a.name = name;
  a.size = data->size();
  a.at = clock.nowDT();
  a.who = who;
  meta[issueId].push_back(a);
  return a;
}
}  // namespace

ExpandedFiles expandDropped(IFileStore& fs, const std::vector<std::string>& pathsIn, std::size_t maxFiles) {
  ExpandedFiles out;
  for (const auto& p : pathsIn) walk(fs, p, 0, maxFiles, out);
  return out;
}

std::string attachmentPath(const std::string& projectDir, const std::string& issueId, const Attachment& a) {
  return joinPath(paths::attachmentDir(projectDir, issueId), a.name);
}

AttachOutcome attachFiles(IFileStore& fs, const IClock& clock, const std::string& projectDir, AttachmentMap& meta,
                          const std::string& issueId, const std::vector<std::string>& files, const std::string& who,
                          const AttachLimits& lim) {
  AttachOutcome out;
  const ExpandedFiles ex = expandDropped(fs, files, lim.maxFiles);
  for (const auto& src : ex.files) {
    const std::string shown = baseName(src);
    const auto st = fs.stat(src);
    if (!st) {
      out.rejected.push_back({shown, "ファイルが見つかりません"});
      continue;
    }
    auto r = storeOne(fs, clock, projectDir, meta, issueId, shown, st->size,
                      [&]() -> Result<std::string> {
                        auto d = fs.read(src);
                        if (!d) return Error{"読み込めません"};
                        return d;
                      },
                      who, lim);
    if (r) out.added.push_back(*r);
    else out.rejected.push_back({shown, r.error()});
  }
  if (ex.truncated)
    out.rejected.push_back({"", "ファイルが多すぎます（1回に " + std::to_string(lim.maxFiles) + " 個まで）。残りは付けていません"});
  return out;
}

Result<Attachment> attachData(IFileStore& fs, const IClock& clock, const std::string& projectDir, AttachmentMap& meta,
                              const std::string& issueId, const std::string& name, const std::string& data,
                              const std::string& who, const AttachLimits& lim) {
  return storeOne(fs, clock, projectDir, meta, issueId, name, data.size(),
                  [&]() -> Result<std::string> { return data; }, who, lim);
}

Status removeAttachment(IFileStore& fs, const std::string& projectDir, AttachmentMap& meta, const std::string& issueId,
                        std::size_t index) {
  auto it = meta.find(issueId);
  if (it == meta.end() || index >= it->second.size()) return Error{"その添付はありません"};
  const Attachment a = it->second[index];
  const std::string src = attachmentPath(projectDir, issueId, a);
  if (!projectDir.empty() && fs.stat(src)) {
    const std::string ddir = joinPath(joinPath(projectDir, kAttachDeleted), paths::safeName(issueId, paths::kMaxFolderNameUnits, "issue"));
    if (auto s = fs.makeDirs(ddir); !s) return Error{"削除済みのフォルダを作れません: " + s.error()};
    const std::string name = paths::uniqueName(lowerNamesIn(fs, ddir), a.name, true);
    if (auto s = fs.move(src, joinPath(ddir, name)); !s) return Error{"削除済みへ移せません: " + s.error()};
  }
  it->second.erase(it->second.begin() + static_cast<std::ptrdiff_t>(index));
  if (it->second.empty()) meta.erase(it);
  return okStatus();
}

std::size_t attachmentCount(const AttachmentMap& m, const std::string& issueId) {
  const auto it = m.find(issueId);
  return it == m.end() ? 0 : it->second.size();
}

std::uint64_t attachmentsTotalBytes(const AttachmentMap& m) {
  std::uint64_t n = 0;
  for (const auto& [id, list] : m)
    for (const auto& a : list) n += a.size;
  return n;
}

bool isRiskyFile(const std::string& name) {
  static const char* const kExt[] = {"exe", "bat", "cmd", "com", "msi", "msp", "ps1", "psm1", "vbs", "vbe", "js", "jse",
                                     "wsf", "wsh", "scr", "lnk", "jar", "hta", "reg", "dll", "cpl", "msc", "pif", "url",
                                     "application", "gadget"};
  const std::string e = extOf(name);
  if (e.empty()) return false;
  return std::any_of(std::begin(kExt), std::end(kExt), [&](const char* x) { return e == x; });
}

bool isImageFile(const std::string& name) {
  const std::string e = extOf(name);
  return e == "png" || e == "jpg" || e == "jpeg" || e == "gif" || e == "bmp";
}

std::string formatBytes(std::uint64_t n) {
  char buf[32];
  if (n < 1024) {
    std::snprintf(buf, sizeof buf, "%llu B", static_cast<unsigned long long>(n));
  } else if (n < 1024ull * 1024) {
    std::snprintf(buf, sizeof buf, "%.1f KB", static_cast<double>(n) / 1024.0);
  } else if (n < 1024ull * 1024 * 1024) {
    std::snprintf(buf, sizeof buf, "%.1f MB", static_cast<double>(n) / (1024.0 * 1024.0));
  } else {
    std::snprintf(buf, sizeof buf, "%.2f GB", static_cast<double>(n) / (1024.0 * 1024.0 * 1024.0));
  }
  return buf;
}

}  // namespace roadmap
