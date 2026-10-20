// 添付ファイル（SPEC 8.9・10.9）: 詳細パネルの「添付」タブ、行・カードの添付バッジ、エクスプローラーからのドロップ
#include <algorithm>

#include <imgui.h>

#include "roadmap/attachments.hpp"
#include "roadmap/text.hpp"
#include "ui/icons.hpp"
#include "ui/image_decode.hpp"
#include "ui/internal.hpp"

namespace roadmap::ui {

namespace {
std::string baseNameOf(const std::string& path) {
  const auto k = path.find_last_of("/\\");
  return k == std::string::npos ? path : path.substr(k + 1);
}
constexpr int kThumbPx = 160;     // サムネイルの長辺（これより大きい画像は縮小して読む）
bool inside(const UiState::DropTarget& t, ImVec2 p) { return p.x >= t.a.x && p.x < t.b.x && p.y >= t.a.y && p.y < t.b.y; }
}  // namespace

void registerDropTarget(Frame& f, const std::string& issueId, ImVec2 a, ImVec2 b) {
  const ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 cmin = dl->GetClipRectMin(), cmax = dl->GetClipRectMax();
  a = ImVec2(std::max(a.x, cmin.x), std::max(a.y, cmin.y));
  b = ImVec2(std::min(b.x, cmax.x), std::min(b.y, cmax.y));
  if (b.x <= a.x || b.y <= a.y) return;
  f.u.dropBuild.push_back({a, b, issueId});
}

void processDrop(Frame& f) {
  UiState& u = f.u;
  if (!u.pendingDrop.active) return;
  UiState::PendingDrop d = std::move(u.pendingDrop);
  u.pendingDrop = {};
  if (d.paths.empty()) return;
  app::AppController& c = f.c;
  Session* s = c.session();
  // ドロップ先が課題（行・カード・詳細パネル）なら添付。あとから描いたもの（上に載っているもの）を優先する
  if (s && u.dlg == Dlg::None) {
    for (auto it = u.dropTargets.rbegin(); it != u.dropTargets.rend(); ++it)
      if (inside(*it, d.pos) && s->issue(it->id)) {
        c.attachFiles(it->id, d.paths);
        return;
      }
  }
  // 何もないところ: 今まで通り、CSV / JSON を取り込む（最初の1つ）
  if (d.paths.size() > 1) c.toast("複数のファイルがドロップされました。最初の1つだけを取り込みます");
  std::string err;
  const auto text = f.p.readTextFile(d.paths.front(), &err);
  if (!text) {
    c.toast(err);
    return;
  }
  std::string name = baseNameOf(d.paths.front());
  for (const char* ext : {".csv", ".json", ".CSV", ".JSON"}) {
    const std::string e(ext);
    if (name.size() > e.size() && name.compare(name.size() - e.size(), e.size(), e) == 0) name.resize(name.size() - e.size());
  }
  u.dText = *text;
  u.importName = name;
  openDialog(f, Dlg::Import);
}

const UiState::ImageTex* attachmentImage(Frame& f, const std::string& issueId, std::size_t index, int maxDim) {
  UiState& u = f.u;
  const auto& list = f.c.attachmentsOf(issueId);
  if (index >= list.size() || !isImageFile(list[index].name)) return nullptr;
  const Attachment& a = list[index];
  const std::string key = f.c.attachmentFile(issueId, index) + "|" + std::to_string(a.size) + "|" + a.at + "|" + std::to_string(maxDim);
  const int frame = ImGui::GetFrameCount();
  if (auto it = u.images.find(key); it != u.images.end()) {
    it->second.lastFrame = frame;
    return &it->second;
  }
  if (u.imageDecodedFrame == frame) return nullptr;  // 1フレームに1枚。重い画像で画面が止まらないように
  u.imageDecodedFrame = frame;
  UiState::ImageTex t;
  t.lastFrame = frame;
  if (auto bytes = f.c.attachmentBytes(issueId, index))
    if (auto px = decodeImage(*bytes, maxDim)) {
      t.tex = f.p.createTexture(px->rgba.data(), px->w, px->h);
      t.w = px->w;
      t.h = px->h;
      t.origW = px->origW;
      t.origH = px->origH;
    }
  t.failed = t.tex == nullptr;
  return &(u.images[key] = t);
}

void openImagePreview(Frame& f, const std::string& issueId, std::size_t index) {
  const auto& list = f.c.attachmentsOf(issueId);
  if (index >= list.size() || !isImageFile(list[index].name)) return;
  f.u.previewIssue = issueId;
  f.u.previewIndex = index;
  f.u.previewZoom = 0;
  openDialog(f, Dlg::AttachPreview);
}

void releaseImages(Frame& f) {
  for (auto& [k, t] : f.u.images)
    if (t.tex) f.p.releaseTexture(t.tex);
  f.u.images.clear();
}

void trimImages(Frame& f) {
  UiState& u = f.u;
  const int frame = ImGui::GetFrameCount();
  constexpr int kIdleFrames = 120;      // これだけ使われなければ解放する
  constexpr std::size_t kMaxImages = 96;  // 多すぎるときは古いものから
  for (auto it = u.images.begin(); it != u.images.end();) {
    if (frame - it->second.lastFrame > kIdleFrames) {
      if (it->second.tex) f.p.releaseTexture(it->second.tex);
      it = u.images.erase(it);
    } else {
      ++it;
    }
  }
  while (u.images.size() > kMaxImages) {
    // このフレームで使った画像は、まだ描画に渡す前なので解放しない（解放済みのテクスチャを描くと落ちる）。
    // それしか残っていなければ、上限を超えたまま次のフレームに回す
    auto oldest = u.images.end();
    for (auto it = u.images.begin(); it != u.images.end(); ++it)
      if (it->second.lastFrame < frame && (oldest == u.images.end() || it->second.lastFrame < oldest->second.lastFrame)) oldest = it;
    if (oldest == u.images.end()) break;
    if (oldest->second.tex) f.p.releaseTexture(oldest->second.tex);
    u.images.erase(oldest);
  }
}

void openAttachment(Frame& f, const std::string& issueId, std::size_t index) {
  app::AppController& c = f.c;
  const auto& list = c.attachmentsOf(issueId);
  const std::string path = c.attachmentFile(issueId, index);
  if (path.empty() || index >= list.size()) return;
  if (!c.attachmentExists(issueId, index)) {
    c.toast("ファイルが見つかりません（フォルダから移動・削除された可能性があります）");
    return;
  }
  if (isRiskyFile(list[index].name)) {
    f.u.attachWarnPath = path;
    openDialog(f, Dlg::AttachOpenWarn);
    return;
  }
  f.p.openFile(path);
}

float drawAttachBadge(Frame& f, ImVec2 pos, float cy, int count, bool* clicked) {
  UiState& u = f.u;
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const std::string n = std::to_string(count);
  const float w = u.m.s(16) + ImGui::CalcTextSize(n.c_str()).x + u.m.s(8);
  bool hov = false;
  if (clicked) {
    ImGui::SetCursorScreenPos(ImVec2(pos.x - u.m.s(2), cy - u.m.s(9)));
    if (ImGui::InvisibleButton("##att", ImVec2(w, u.m.s(18)))) *clicked = true;
    hov = ImGui::IsItemHovered();
    if (hov) tip("添付ファイル（クリックで一覧）");
  }
  const ImU32 col = hov ? u.t->accent : u.t->muted;
  drawIcon(dl, "paperclip", ImVec2(pos.x, cy - u.m.s(6)), u.m.s(12), col);
  dl->AddText(ImVec2(pos.x + u.m.s(14), cy - ImGui::GetTextLineHeight() / 2), col, n.c_str());
  return w;
}

namespace {
// JIRA の CSV から取り込んだ Attachment 列（メタ情報だけ。本体はない）
void drawJiraAttachments(Frame& f, const Issue& o) {
  const auto list = jiraAttachmentsOf(o, f.c.session()->meta());
  if (list.empty()) return;
  ImGui::Spacing();
  ImGui::SeparatorText("JIRA から取り込んだ添付（本体はありません）");
  ImGui::PushTextWrapPos(0);
  ImGui::TextDisabled("CSV の Attachment 列の内容です。そのまま書き出し時にも戻します。");
  ImGui::PopTextWrapPos();
  for (std::size_t k = 0; k < list.size(); ++k) {
    const JiraAttachment& a = list[k];
    ImGui::PushID(static_cast<int>(k));
    ImGui::PushTextWrapPos(0);
    ImGui::TextUnformatted(a.name.c_str());
    std::string meta;
    for (const std::string* part : {&a.at, &a.who})
      if (!part->empty()) meta += (meta.empty() ? "" : "  ") + *part;
    if (!meta.empty()) ImGui::TextDisabled("%s", meta.c_str());
    ImGui::PopTextWrapPos();
    ImGui::PopID();
  }
}
}  // namespace

void drawAttachTab(Frame& f, int i) {
  app::AppController& c = f.c;
  UiState& u = f.u;
  Session* s = c.session();
  const Issue& o = s->viewIssues()[i];
  const std::string id = o.id;
  if (!c.canAttach()) {
    ImGui::PushTextWrapPos(0);
    ImGui::TextColored(v4(u.t->tight), "%s", c.attachDisabledReason().c_str());
    ImGui::PopTextWrapPos();
    drawJiraAttachments(f, o);
    return;
  }
  const float W = ImGui::GetContentRegionAvail().x;
  // ドロップの案内（ここだけでなく、行・カード・この詳細パネルのどこへドロップしても付く）
  {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = u.m.s(58);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + W, p.y + h), u.t->surface2, u.m.s(6));
    dl->AddRect(p, ImVec2(p.x + W, p.y + h), u.t->line2, u.m.s(6));
    const char* l1 = "ここにファイルをドロップして添付";
    const char* l2 = "画像・PDF など何でも／フォルダは中のファイルを全部";
    const ImVec2 s1 = ImGui::CalcTextSize(l1), s2 = ImGui::CalcTextSize(l2);
    drawIcon(dl, "paperclip", ImVec2(p.x + (W - s1.x) / 2 - u.m.s(20), p.y + u.m.s(10)), u.m.s(14), u.t->muted);
    dl->AddText(ImVec2(p.x + (W - s1.x) / 2, p.y + u.m.s(9)), u.t->ink, l1);
    dl->AddText(ImVec2(p.x + (W - s2.x) / 2, p.y + u.m.s(9) + s1.y + u.m.s(5)), u.t->muted, l2);
    ImGui::Dummy(ImVec2(W, h));
  }
  const auto& list = c.attachmentsOf(id);
  const std::uint64_t total = attachmentsTotalBytes(s->attachments());
  ImGui::Spacing();
  ImGui::PushTextWrapPos(0);
  ImGui::TextDisabled("この課題 %d 件／プロジェクト全体 %s（上限 %s、1ファイル %s まで）", static_cast<int>(list.size()),
                      formatBytes(total).c_str(), formatBytes(kMaxAttachProjectBytes).c_str(),
                      formatBytes(kMaxAttachFileBytes).c_str());
  ImGui::PopTextWrapPos();
  if (list.empty()) {
    ImGui::Spacing();
    ImGui::TextDisabled("添付はまだありません。");
    drawJiraAttachments(f, o);
    return;
  }
  ImGui::Spacing();
  if (ImGui::SmallButton("添付のフォルダを開く")) f.p.openFolder(paths::attachmentDir(c.currentProjectDir(), id));
  ImGui::Separator();
  std::size_t del = static_cast<std::size_t>(-1), open = static_cast<std::size_t>(-1), preview = static_cast<std::size_t>(-1);
  for (std::size_t k = 0; k < list.size(); ++k) {
    const Attachment& a = list[k];
    ImGui::PushID(static_cast<int>(k));
    const bool risky = isRiskyFile(a.name);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float rowH = ImGui::GetTextLineHeight() * 2 + u.m.s(10);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float btns = u.m.s(86);
    // 行全体: ダブルクリックで開く
    ImGui::SetNextItemAllowOverlap();
    ImGui::InvisibleButton("##row", ImVec2(W - btns, rowH));
    const bool hov = ImGui::IsItemHovered();
    if (hov && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) open = k;
    if (hov) dl->AddRectFilled(p, ImVec2(p.x + W, p.y + rowH), u.t->hover, u.m.s(4));
    // 左: 画像はサムネイル（クリックでプレビュー）、そのほかは種類のアイコン
    const float box = rowH - u.m.s(8);
    const ImVec2 b0(p.x + u.m.s(2), p.y + u.m.s(4)), b1(b0.x + box, b0.y + box);
    const bool isImg = isImageFile(a.name);
    const UiState::ImageTex* img = isImg ? attachmentImage(f, id, k, kThumbPx) : nullptr;
    if (img && img->tex) {
      dl->AddRectFilled(b0, b1, u.t->surface3, u.m.s(3));
      const float sc = std::min(box / static_cast<float>(img->w), box / static_cast<float>(img->h));
      const ImVec2 sz(static_cast<float>(img->w) * sc, static_cast<float>(img->h) * sc);
      const ImVec2 q0(b0.x + (box - sz.x) / 2, b0.y + (box - sz.y) / 2);
      dl->AddImage(img->tex, q0, ImVec2(q0.x + sz.x, q0.y + sz.y));
      dl->AddRect(b0, b1, u.t->line, u.m.s(3));
      ImGui::SetCursorScreenPos(b0);
      ImGui::InvisibleButton("##thumb", ImVec2(box, box));
      if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) preview = k;
      if (ImGui::IsItemHovered()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        ImGui::BeginTooltip();
        const float tsc = std::min(1.f, std::min(u.m.s(220) / static_cast<float>(img->w), u.m.s(220) / static_cast<float>(img->h)));
        ImGui::Image(img->tex, ImVec2(static_cast<float>(img->w) * tsc, static_cast<float>(img->h) * tsc));
        ImGui::TextDisabled("クリックでプレビュー（%d × %d）", img->origW, img->origH);
        ImGui::EndTooltip();
      }
    } else {
      const char* icon = risky ? "warn" : (isImg ? "image" : "file");
      const ImU32 col = risky ? u.t->tight : (img && img->failed ? u.t->late : u.t->muted);
      drawIcon(dl, icon, ImVec2(b0.x + (box - u.m.s(18)) / 2, b0.y + (box - u.m.s(18)) / 2), u.m.s(18), col);
      if (img && img->failed && ImGui::IsMouseHoveringRect(b0, b1) && ImGui::IsWindowHovered())
        ImGui::SetTooltip("画像として読めません（壊れているか、対応していない形式です）");
    }
    const float tx = b1.x + u.m.s(8);
    dl->AddText(ImVec2(tx, p.y + u.m.s(4)), u.t->ink, ellipsize(a.name, W - btns - box - u.m.s(12)).c_str());
    std::string at = a.at;
    if (at.size() == 16) at[10] = ' ';
    std::string meta = formatBytes(a.size);
    if (!at.empty()) meta += "  " + at;
    if (!a.who.empty()) meta += "  " + a.who;
    dl->AddText(ImVec2(tx, p.y + u.m.s(4) + ImGui::GetTextLineHeight() + u.m.s(2)), u.t->muted,
                ellipsize(meta, W - btns - box - u.m.s(12)).c_str());
    ImGui::SetCursorScreenPos(ImVec2(p.x + W - btns + u.m.s(4), p.y + (rowH - ImGui::GetFrameHeight()) / 2));
    if (ImGui::SmallButton("開く")) open = k;
    if (risky) tip("実行ファイルやスクリプトです。開く前に確認します");
    ImGui::SameLine();
    if (iconButton(u, "del", "trash", "外す（ファイルは「_削除済み」フォルダへ移します）")) del = k;
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + rowH));
    ImGui::Separator();
    ImGui::PopID();
  }
  // 描画の途中で添付の一覧を変えない（一覧への参照を使い終わってから）
  if (preview != static_cast<std::size_t>(-1)) openImagePreview(f, id, preview);
  if (open != static_cast<std::size_t>(-1)) openAttachment(f, id, open);
  if (del != static_cast<std::size_t>(-1)) deferAfterFrame(f, [&f, id, del] { f.c.removeAttachment(id, del); });
  drawJiraAttachments(f, o);
}

}  // namespace roadmap::ui
