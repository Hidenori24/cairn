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
constexpr int kThumbDefault = 36, kThumbMin = 24, kThumbMax = 160;  // 添付タブのサムネイルの大きさ（スライダー。論理 px）
int thumbSize(const app::AppController& c) { return std::clamp(c.uiInt("attachThumb", kThumbDefault), kThumbMin, kThumbMax); }
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

int attachReorderSlot(const UiState& u, const std::string& issueId, ImVec2 pos) {
  if (u.attachRowsIssue != issueId || u.attachRows.empty() || ImGui::GetFrameCount() - u.attachRowsFrame > 1) return -1;
  const auto& first = u.attachRows.front();
  const auto& last = u.attachRows.back();
  const float pad = (first.second.y - first.first.y) / 2;
  if (pos.x < first.first.x || pos.x >= first.second.x || pos.y < first.first.y - pad || pos.y >= last.second.y + pad) return -1;
  int slot = 0;
  for (const auto& r : u.attachRows)
    if ((r.first.y + r.second.y) / 2 < pos.y) ++slot;
  return slot;
}

namespace {
// 位置 pos にあるドロップ先（あとから描いたものを優先。実在する課題だけ）
const UiState::DropTarget* targetAt(Frame& f, ImVec2 pos) {
  Session* s = f.c.session();
  if (!s) return nullptr;
  for (auto it = f.u.dropTargets.rbegin(); it != f.u.dropTargets.rend(); ++it)
    if (inside(*it, pos) && s->issue(it->id)) return &*it;
  return nullptr;
}
}  // namespace

void processAttachDrag(Frame& f) {
  UiState& u = f.u;
  const ImGuiIO& io = ImGui::GetIO();
  if (!io.MouseDown[0]) u.attachDragCancelled = false;
  if (!u.attachDrag.active) return;
  app::AppController& c = f.c;
  const UiState::AttachDrag d = u.attachDrag;
  // 続けられないとき（プロジェクトを切り替えた・ダイアログが開いた・つかんだ添付がなくなった）は取り消す
  if (!c.session() || u.dlg != Dlg::None || c.currentId() != d.project || d.index >= c.attachmentsOf(d.issueId).size()) {
    u.attachDrag = {};
    u.attachDragCancelled = true;
    return;
  }
  if (io.MouseDown[0]) return;  // まだつかんでいる
  u.attachDrag = {};
  if (!ImGui::IsMouseReleased(ImGuiMouseButton_Left)) return;
  // 離した位置のドロップ先。何もないところでは何もしない
  const ImVec2 pos = io.MousePos;
  const UiState::DropTarget* t = targetAt(f, pos);
  if (!t) return;
  if (t->id == d.issueId) {  // 同じ課題 = 添付の一覧の上なら並べ替え
    const int slot = attachReorderSlot(u, d.issueId, pos);
    if (slot < 0) return;
    const std::size_t at = static_cast<std::size_t>(slot);
    const std::size_t to = at > d.index ? at - 1 : at;
    if (to != d.index) c.reorderAttachment(d.issueId, d.index, to);
    return;
  }
  if (io.KeyCtrl) c.copyAttachment(d.issueId, d.index, t->id);
  else c.moveAttachment(d.issueId, d.index, t->id);
}

void drawAttachDrag(Frame& f) {
  UiState& u = f.u;
  if (!u.attachDrag.active) return;
  app::AppController& c = f.c;
  Session* s = c.session();
  const UiState::AttachDrag d = u.attachDrag;
  if (!s || c.currentId() != d.project) return;
  const auto& list = c.attachmentsOf(d.issueId);
  if (d.index >= list.size()) return;
  const ImGuiIO& io = ImGui::GetIO();
  const ImVec2 pos = io.MousePos;
  if (pos.x < -1e6f) return;  // マウスがウィンドウの外
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  const bool copy = io.KeyCtrl;
  const UiState::DropTarget* t = targetAt(f, pos);
  const Issue* to = t && t->id != d.issueId ? s->issue(t->id) : nullptr;
  std::string note;
  if (to) {
    // 受ける課題の範囲を縁取る
    dl->AddRectFilled(t->a, t->b, withAlpha(u.t->accent, 0.14f));
    dl->AddRect(t->a, t->b, u.t->accent, 0, 0, u.m.s(2));
    note = std::string(copy ? "コピー: " : "移動: ") + ellipsize(to->summary, u.m.s(190));
  } else if (t) {
    const int slot = attachReorderSlot(u, d.issueId, pos);
    if (slot >= 0) {
      const std::size_t at = static_cast<std::size_t>(slot);
      const std::size_t dest = at > d.index ? at - 1 : at;
      if (dest != d.index && at < u.attachRows.size() + 1) {
        const float y = at == 0 ? u.attachRows.front().first.y : u.attachRows[at - 1].second.y;
        dl->AddLine(ImVec2(u.attachRows.front().first.x, y), ImVec2(u.attachRows.front().second.x, y), u.t->accent, u.m.s(3));
        note = "ここへ並べ替え";
      }
    }
  }
  if (note.empty()) note = to || t ? "" : "課題の行・カードへドロップで移動（Ctrl でコピー）";
  // カーソルに付くもの: サムネイル（画像のとき）と名前
  const Attachment& a = list[d.index];
  const float th = u.m.s(34);
  const UiState::ImageTex* img = isImageFile(a.name) ? attachmentImage(f, d.issueId, d.index, kThumbPx) : nullptr;
  const std::string name = ellipsize(a.name, u.m.s(190));
  const float tw = std::max({ImGui::CalcTextSize(name.c_str()).x, ImGui::CalcTextSize(note.c_str()).x, u.m.s(60)});
  const float w = th + u.m.s(8) + tw + u.m.s(14);
  const float h = th + u.m.s(10);
  ImVec2 p0(pos.x + u.m.s(14), pos.y + u.m.s(12));
  p0.x = std::min(p0.x, std::max(0.f, io.DisplaySize.x - w - u.m.s(4)));
  p0.y = std::min(p0.y, std::max(0.f, io.DisplaySize.y - h - u.m.s(4)));
  const ImVec2 p1(p0.x + w, p0.y + h);
  dl->AddRectFilled(p0, p1, withAlpha(u.t->surface, 0.94f), u.m.s(6));
  dl->AddRect(p0, p1, u.t->accent, u.m.s(6));
  const ImVec2 b0(p0.x + u.m.s(5), p0.y + u.m.s(5)), b1(b0.x + th, b0.y + th);
  if (img && img->tex) {
    const float sc = std::min(th / static_cast<float>(img->w), th / static_cast<float>(img->h));
    const ImVec2 sz(static_cast<float>(img->w) * sc, static_cast<float>(img->h) * sc);
    const ImVec2 q0(b0.x + (th - sz.x) / 2, b0.y + (th - sz.y) / 2);
    dl->AddImage(img->tex, q0, ImVec2(q0.x + sz.x, q0.y + sz.y));
  } else {
    drawIcon(dl, isRiskyFile(a.name) ? "warn" : (isImageFile(a.name) ? "image" : "file"), ImVec2(b0.x + (th - u.m.s(18)) / 2, b0.y + (th - u.m.s(18)) / 2),
             u.m.s(18), u.t->muted);
  }
  const float tx = b1.x + u.m.s(8);
  const float lh = ImGui::GetTextLineHeight();
  const float ty = p0.y + (h - (note.empty() ? lh : lh * 2 + u.m.s(2))) / 2;
  dl->AddText(ImVec2(tx, ty), u.t->ink, name.c_str());
  if (!note.empty()) dl->AddText(ImVec2(tx, ty + lh + u.m.s(2)), to ? u.t->accent : u.t->muted, note.c_str());
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
    const float h = u.m.s(76);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + W, p.y + h), u.t->surface2, u.m.s(6));
    dl->AddRect(p, ImVec2(p.x + W, p.y + h), u.t->line2, u.m.s(6));
    const char* l1 = "ここにファイルをドロップして添付";
    const char* l2 = "画像・PDF など何でも／フォルダは中のファイルを全部";
    const char* l3 = "コピーした画像は、課題を選んで Ctrl+V でも付きます";
    const ImVec2 s1 = ImGui::CalcTextSize(l1), s2 = ImGui::CalcTextSize(l2), s3 = ImGui::CalcTextSize(l3);
    drawIcon(dl, "paperclip", ImVec2(p.x + (W - s1.x) / 2 - u.m.s(20), p.y + u.m.s(10)), u.m.s(14), u.t->muted);
    dl->AddText(ImVec2(p.x + (W - s1.x) / 2, p.y + u.m.s(9)), u.t->ink, l1);
    dl->AddText(ImVec2(p.x + (W - s2.x) / 2, p.y + u.m.s(9) + s1.y + u.m.s(5)), u.t->muted, l2);
    dl->AddText(ImVec2(p.x + (W - s3.x) / 2, p.y + u.m.s(9) + s1.y + s2.y + u.m.s(8)), u.t->muted, l3);
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
  ImGui::Spacing();
  // 「ファイルを選んで追加」: ダイアログを開く間に描画中の参照が壊れないよう、フレームの終わりに行う
  if (ImGui::Button("ファイルを選んで追加")) {
    deferAfterFrame(f, [&f, id] {
      const std::vector<std::string> picked = f.p.pickOpenFiles("添付するファイルを選ぶ");
      if (!picked.empty() && f.c.session() && f.c.session()->issue(id)) f.c.attachFiles(id, picked);
    });
  }
  u.attachPickBtn = {ImGui::GetItemRectMin(), ImGui::GetItemRectMax()};
  if (list.empty()) {
    ImGui::Spacing();
    ImGui::TextDisabled("添付はまだありません。");
    drawJiraAttachments(f, o);
    return;
  }
  ImGui::SameLine();
  if (ImGui::SmallButton("添付のフォルダを開く")) f.p.openFolder(paths::attachmentDir(c.currentProjectDir(), id));
  {
    int px = thumbSize(c);
    ImGui::SetNextItemWidth(u.m.s(110));
    if (ImGui::SliderInt("サムネイルの大きさ##thumbsz", &px, kThumbMin, kThumbMax, "%d")) c.setUi("attachThumb", px);
    u.attachSlider = {ImGui::GetItemRectMin(), ImGui::GetItemRectMax()};
  }
  ImGui::PushTextWrapPos(0);
  ImGui::TextDisabled("サムネイルをつかんで、別の課題の行・カードへ離すと移動（Ctrl でコピー）。この一覧の中では並べ替え。");
  ImGui::PopTextWrapPos();
  ImGui::Separator();
  const float thumbBox = u.m.s(static_cast<float>(thumbSize(c)));
  u.attachRows.clear();
  u.attachRowsIssue = id;
  u.attachRowsFrame = ImGui::GetFrameCount();
  std::size_t del = static_cast<std::size_t>(-1), open = static_cast<std::size_t>(-1), preview = static_cast<std::size_t>(-1);
  for (std::size_t k = 0; k < list.size(); ++k) {
    const Attachment& a = list[k];
    ImGui::PushID(static_cast<int>(k));
    const bool risky = isRiskyFile(a.name);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float textH = ImGui::GetTextLineHeight() * 2 + u.m.s(2);
    const float rowH = std::max(textH + u.m.s(10), thumbBox + u.m.s(8));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float btns = u.m.s(86);
    u.attachRows.emplace_back(p, ImVec2(p.x + W, p.y + rowH));
    const float dragPx = u.m.s(5);
    const bool beingDragged = u.attachDrag.active && u.attachDrag.issueId == id && u.attachDrag.index == k;
    // 行全体: ダブルクリックで開く。つかんで動かすと、別の課題へ移す・コピーする（離したときに processAttachDrag が実行する）
    ImGui::SetNextItemAllowOverlap();
    ImGui::InvisibleButton("##row", ImVec2(W - btns, rowH));
    const bool hov = ImGui::IsItemHovered();
    bool grabbed = ImGui::IsItemActive();
    if (hov && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) open = k;
    if (hov && !u.attachDrag.active) dl->AddRectFilled(p, ImVec2(p.x + W, p.y + rowH), u.t->hover, u.m.s(4));
    // 左: 画像はサムネイル（クリックでプレビュー）、そのほかは種類のアイコン
    const float box = thumbBox;
    const ImVec2 b0(p.x + u.m.s(2), p.y + (rowH - box) / 2), b1(b0.x + box, b0.y + box);
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
      // プレビューは「押して離した」ときだけ（つかんで動かしたときは開かない）
      const bool released = ImGui::InvisibleButton("##thumb", ImVec2(box, box));
      grabbed = grabbed || ImGui::IsItemActive();
      if (released && ImGui::GetIO().MouseDragMaxDistanceSqr[0] < dragPx * dragPx) preview = k;
      if (ImGui::IsItemHovered() && !u.attachDrag.active) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        ImGui::BeginTooltip();
        const float tsc = std::min(1.f, std::min(u.m.s(220) / static_cast<float>(img->w), u.m.s(220) / static_cast<float>(img->h)));
        ImGui::Image(img->tex, ImVec2(static_cast<float>(img->w) * tsc, static_cast<float>(img->h) * tsc));
        ImGui::TextDisabled("クリックでプレビュー（%d × %d）／つかんで別の課題へ", img->origW, img->origH);
        ImGui::EndTooltip();
      }
    } else {
      const char* icon = risky ? "warn" : (isImg ? "image" : "file");
      const ImU32 col = risky ? u.t->tight : (img && img->failed ? u.t->late : u.t->muted);
      drawIcon(dl, icon, ImVec2(b0.x + (box - u.m.s(18)) / 2, b0.y + (box - u.m.s(18)) / 2), u.m.s(18), col);
      if (img && img->failed && ImGui::IsMouseHoveringRect(b0, b1) && ImGui::IsWindowHovered())
        ImGui::SetTooltip("画像として読めません（壊れているか、対応していない形式です）");
    }
    if (grabbed && !u.attachDrag.active && !u.attachDragCancelled && ImGui::IsMouseDragging(ImGuiMouseButton_Left, dragPx)) {
      u.attachDrag.active = true;
      u.attachDrag.issueId = id;
      u.attachDrag.index = k;
      u.attachDrag.project = c.currentId();
    }
    const float tx = b1.x + u.m.s(8);
    const float ty = p.y + (rowH - textH) / 2;
    dl->AddText(ImVec2(tx, ty), u.t->ink, ellipsize(a.name, W - btns - box - u.m.s(12)).c_str());
    std::string at = a.at;
    if (at.size() == 16) at[10] = ' ';
    std::string meta = formatBytes(a.size);
    if (!at.empty()) meta += "  " + at;
    if (!a.who.empty()) meta += "  " + a.who;
    dl->AddText(ImVec2(tx, ty + ImGui::GetTextLineHeight() + u.m.s(2)), u.t->muted,
                ellipsize(meta, W - btns - box - u.m.s(12)).c_str());
    if (beingDragged) dl->AddRectFilled(p, ImVec2(p.x + W, p.y + rowH), withAlpha(u.t->surface, 0.55f));  // つかんでいる行は薄く
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
