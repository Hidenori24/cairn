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
  // 読んでいる間に大きくなった場合に備えて、実際に読めた大きさで、1ファイルとプロジェクト合計の両方を検査し直す
  if (data->size() > lim.perFile)
    return Error{"1ファイルの上限（" + formatBytes(lim.perFile) + "）を超えています"};
  if (total + data->size() > lim.perProject)
    return Error{"プロジェクト全体の合計の上限（" + formatBytes(lim.perProject) + "）を超えます（いま " + formatBytes(total) + "）"};
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

namespace {
// 移す・コピーする元の添付を調べる。元の課題と添付の番号、本体の有無、先の課題（空でなく、元と違う）
struct SourceAttach {
  Attachment a;
  std::string path;
};
Result<SourceAttach> findSource(IFileStore& fs, const std::string& projectDir, const AttachmentMap& meta, const std::string& fromIssue,
                                std::size_t index, const std::string& toIssue) {
  if (projectDir.empty()) return Error{"このプロジェクトには添付を置く場所がありません"};
  if (toIssue.empty()) return Error{"移す先の課題がありません"};
  if (fromIssue == toIssue) return Error{"同じ課題です"};
  const auto it = meta.find(fromIssue);
  if (it == meta.end() || index >= it->second.size()) return Error{"その添付はありません"};
  SourceAttach src;
  src.a = it->second[index];
  src.path = attachmentPath(projectDir, fromIssue, src.a);
  const auto st = fs.stat(src.path);
  if (!st || st->isDir) return Error{"元のファイルが見つかりません（フォルダから移動・削除された可能性があります）"};
  return src;
}
}  // namespace

Result<Attachment> moveAttachment(IFileStore& fs, const std::string& projectDir, AttachmentMap& meta, const std::string& fromIssue,
                                  std::size_t index, const std::string& toIssue) {
  auto src = findSource(fs, projectDir, meta, fromIssue, index, toIssue);
  if (!src) return Error{src.error()};
  const std::string dir = paths::attachmentDir(projectDir, toIssue);
  if (auto s = fs.makeDirs(dir); !s) return Error{"添付のフォルダを作れません: " + s.error()};
  std::set<std::string> used = lowerNamesIn(fs, dir);
  if (const auto it = meta.find(toIssue); it != meta.end())
    for (const auto& x : it->second) used.insert(text::toLower(x.name));
  std::string name = paths::fitFileName(dir, src->a.name, paths::kMaxPathUnits - kNameHeadroom);
  if (name.empty()) return Error{"移し先のパスが長すぎて、このファイル名では置けません"};
  name = paths::uniqueName(used, name, true);
  // 本体を先に移す。移せなければメタ情報には触れない（食い違わせない）。移せたあとのメタ情報の付け替えは失敗しない
  if (auto s = fs.move(src->path, joinPath(dir, name)); !s) return Error{"移せません: " + s.error()};
  Attachment moved = src->a;
  moved.name = name;
  meta[toIssue].push_back(moved);
  auto from = meta.find(fromIssue);
  from->second.erase(from->second.begin() + static_cast<std::ptrdiff_t>(index));
  if (from->second.empty()) meta.erase(from);
  return moved;
}

Result<Attachment> copyAttachment(IFileStore& fs, const IClock& clock, const std::string& projectDir, AttachmentMap& meta,
                                  const std::string& fromIssue, std::size_t index, const std::string& toIssue,
                                  const std::string& who, const AttachLimits& lim) {
  auto src = findSource(fs, projectDir, meta, fromIssue, index, toIssue);
  if (!src) return Error{src.error()};
  const auto st = fs.stat(src->path);
  const std::uint64_t size = st ? st->size : src->a.size;
  return storeOne(fs, clock, projectDir, meta, toIssue, src->a.name, size,
                  [&]() -> Result<std::string> {
                    auto d = fs.read(src->path);
                    if (!d) return Error{"元のファイルを読み込めません"};
                    return d;
                  },
                  who, lim);
}

Status reorderAttachment(AttachmentMap& meta, const std::string& issueId, std::size_t from, std::size_t to) {
  const auto it = meta.find(issueId);
  if (it == meta.end() || from >= it->second.size() || to >= it->second.size()) return Error{"その添付はありません"};
  if (from == to) return okStatus();
  auto& v = it->second;
  Attachment a = std::move(v[from]);
  v.erase(v.begin() + static_cast<std::ptrdiff_t>(from));
  v.insert(v.begin() + static_cast<std::ptrdiff_t>(to), std::move(a));
  return okStatus();
}

Result<std::string> bmpFromDib(const std::string& dib) {
  constexpr std::int64_t kMaxSide = 32767;
  constexpr std::int64_t kMaxPixels = 100'000'000;
  const auto u16 = [&](std::size_t at) { return static_cast<std::uint32_t>(static_cast<unsigned char>(dib[at])) | (static_cast<std::uint32_t>(static_cast<unsigned char>(dib[at + 1])) << 8); };
  const auto u32 = [&](std::size_t at) { return u16(at) | (u16(at + 2) << 16); };
  const auto i32 = [&](std::size_t at) { return static_cast<std::int64_t>(static_cast<std::int32_t>(u32(at))); };
  if (dib.size() < 40) return Error{"画像のヘッダが短すぎます"};
  const std::uint32_t hdr = u32(0);
  if (hdr != 40 && hdr != 56 && hdr != 108 && hdr != 124) return Error{"対応していない形式の画像です"};
  if (dib.size() < hdr) return Error{"画像のヘッダが途中で切れています"};
  const std::int64_t w = i32(4);
  const std::int64_t h = i32(8);
  const std::uint32_t planes = u16(12), bpp = u16(14), comp = u32(16), sizeImage = u32(20), clrUsed = u32(32);
  if (planes != 1) return Error{"画像のヘッダが壊れています"};
  if (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 16 && bpp != 24 && bpp != 32) return Error{"対応していない形式の画像です"};
  if (w <= 0 || h == 0) return Error{"画像の大きさが正しくありません"};
  const std::int64_t absH = h < 0 ? -h : h;
  if (w > kMaxSide || absH > kMaxSide || w * absH > kMaxPixels) return Error{"画像が大きすぎます"};
  const bool plain = comp == 0 || comp == 3 || comp == 6;  // BI_RGB / BI_BITFIELDS / BI_ALPHABITFIELDS
  if (!plain && !(comp == 1 && bpp == 8) && !(comp == 2 && bpp == 4)) return Error{"対応していない形式の画像です"};
  if ((comp == 3 || comp == 6) && bpp != 16 && bpp != 32) return Error{"画像のヘッダが壊れています"};
  std::uint64_t off = hdr;
  if (hdr == 40 && comp == 3) off += 12;  // BITMAPINFOHEADER の直後にマスク 3 つ（V3 以降はヘッダの中）
  if (hdr == 40 && comp == 6) off += 16;
  std::uint64_t colors = 0;
  if (bpp <= 8) {
    colors = clrUsed != 0 ? clrUsed : (std::uint64_t{1} << bpp);
    if (colors > (std::uint64_t{1} << bpp)) return Error{"画像の色の数が正しくありません"};
  } else if (clrUsed != 0) {
    if (clrUsed > 256) return Error{"画像の色の数が正しくありません"};
    colors = clrUsed;
  }
  off += colors * 4;
  if (off > dib.size()) return Error{"画像が途中で切れています"};
  const std::uint64_t avail = dib.size() - off;
  if (plain) {
    const std::uint64_t rowBytes = ((static_cast<std::uint64_t>(w) * bpp + 31) / 32) * 4;
    if (rowBytes * static_cast<std::uint64_t>(absH) > avail) return Error{"画像のデータが足りません（途中で切れています）"};
  } else if (sizeImage == 0 || sizeImage > avail) {
    return Error{"画像のデータが足りません（途中で切れています）"};
  }
  const std::uint64_t total = static_cast<std::uint64_t>(dib.size()) + 14;
  if (total > 0xffffffffull) return Error{"画像が大きすぎます"};
  std::string out;
  out.reserve(static_cast<std::size_t>(total));
  const auto put32 = [&](std::uint64_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<char>((v >> (8 * i)) & 0xff));
  };
  out += "BM";
  put32(total);
  put32(0);
  put32(14 + off);
  out += dib;
  return out;
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

std::vector<JiraAttachment> jiraAttachmentsOf(const Issue& o, const ProjectMeta& meta) {
  std::vector<JiraAttachment> out;
  for (std::size_t k = 0; k < meta.extraHeaders.size() && k < o.extra.size(); ++k) {
    if (text::toLower(text::trim(meta.extraHeaders[k])) != "attachment") continue;
    const std::string cell = text::trim(o.extra[k]);
    if (cell.empty()) continue;
    JiraAttachment a;
    const auto s1 = cell.find(';');
    const auto s2 = s1 == std::string::npos ? std::string::npos : cell.find(';', s1 + 1);
    const auto sl = cell.rfind(';');
    if (s2 == std::string::npos || sl == std::string::npos || sl <= s2) {
      a.name = cell;
    } else {
      a.at = text::trim(cell.substr(0, s1));
      a.who = text::trim(cell.substr(s1 + 1, s2 - s1 - 1));
      a.name = cell.substr(s2 + 1, sl - s2 - 1);
      a.url = text::trim(cell.substr(sl + 1));
    }
    out.push_back(std::move(a));
  }
  return out;
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
