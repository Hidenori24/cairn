// UI の各ファイルで共有する関数（app/ui の中だけで使う）
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "roadmap/commands.hpp"
#include "roadmap/gantt_geom.hpp"
#include "roadmap/link_candidates.hpp"
#include "ui/app_ui.hpp"
#include "ui/strings.hpp"
#include "ui/widgets.hpp"

namespace roadmap::ui {

// ---- ビュー ----
void drawListView(Frame& f, ImVec2 size);
void drawGanttView(Frame& f, ImVec2 size);
void drawBoardView(Frame& f, ImVec2 size);
void drawGoalsView(Frame& f, ImVec2 size);
void drawStatsView(Frame& f, ImVec2 size);
void drawDetailPanel(Frame& f, ImVec2 size);
// 詳細パネルの「添付」タブ（i = 課題の添字）
void drawAttachTab(Frame& f, int i);
void drawPalette(Frame& f);
void drawDialogs(Frame& f);
void drawGoalPopover(Frame& f);

// ---- 共通の操作 ----
void openDialog(Frame& f, Dlg d);
void openContext(Frame& f, const std::string& id);
void openStatusMenu(Frame& f, const std::string& id);
void openDetail(Frame& f, const std::string& id, int tab = 0);
// 行のクリック（Ctrl・Shift を見て選択を変える）
void rowClick(Frame& f, const std::string& id);
// 依存の相手を画面のクリックで選ぶモードに入る / 抜ける。起点が先行なら相手は後続、起点が後続なら相手は先行
void beginLinkPick(Frame& f, const std::string& anchor, bool anchorIsPred);
void cancelLinkPick(Frame& f);
// 起点と相手をつなぐ。つなげない相手（循環など）は理由を通知して false。成功すると Undo 1回で戻せる。
// announce = 成功を通知する（元に戻すボタンつき）
bool linkIssues(Frame& f, const std::string& anchor, bool anchorIsPred, const std::string& other, bool announce);
// ---- 依存をつなぐドラッグ（ガント・一覧・ボードで共有。app/ui/link_drag.cpp）----
inline constexpr float kLinkHandleR = 6.f;    // ハンドルの半径（拡大率をかける前）
inline constexpr float kLinkHandleGap = 4.f;  // ガントのバーの端からハンドルまでのすきま
// ほかのドラッグ・選択モード・ダイアログ・入力がなく、ハンドルを出して押してよいか
bool linkDragIdle(const UiState& u);
// issue（tree() の添字）のハンドルからドラッグを始める。fromRight = 右のハンドル（つかんだ課題が先行）
void linkDragBegin(Frame& f, int issue, bool fromRight, ImVec2 mouse);
// ビュー v を描いているときのドラッグとして有効か。課題が消えた・プロジェクトやビューが変わったなら捨てて false
bool linkDragValid(Frame& f, app::View v);
// つなぐ相手 tgt（なければ -1。つかんだ課題自身も相手なし）を調べた結果
struct LinkEval {
  int src = -1, tgt = -1;
  bool fromRight = true;
  LinkBlock blk = LinkBlock::None;       // 循環・重複・親子など
  const LinkPreview* pv = nullptr;       // 日程の影響（つなげるときだけ。u.linkDrag の中を指す）
  bool ok = false, bad = false;          // つなげる / 相手がいるのにつなげない（赤）
  ImU32 lineCol = 0;                     // 線・強調の色
  bool open = false;                     // 相手のいない空き領域の上（離すと新しい課題を作るメニューを出す。呼び出し側が決める）
};
LinkEval linkDragEval(Frame& f, int tgt);
// 離したら確定する。相手がいなければ、空き領域（e.open）ならメニュー（drawLinkMenu）を出し、そうでなければ取り消し。
// 課題を変えるのは描画が終わってから
void linkDragFinish(Frame& f, const LinkEval& e);
// 空き領域で離したときのメニュー「新しい後続（先行）の課題を作ってつなぐ」を描く（オーバーレイで毎フレーム呼ぶ）
void drawLinkMenu(Frame& f);
// 起点 anchor の兄弟として新しい課題を作り、succ なら 起点 → 新しい課題、そうでなければ 新しい課題 → 起点 とつなぐ（全体で Undo 1回）。
// sameStatus = 状況を起点に合わせる（ボード）。つなげなければ作った課題も戻す。成功すると件名のその場編集を始める
bool createLinkedIssue(Frame& f, const std::string& anchor, bool succ, bool sameStatus);
void drawLinkHandle(Frame& f, ImVec2 hc, bool hot, bool ok = true);
// ゴムひも（p0 から p1 まで）。sameSide = p1 にも p0 と同じ側から入る（一覧の左右の余白の中を通す）。minBend = 曲がりの最小
void drawLinkRubber(Frame& f, ImVec2 p0, ImVec2 p1, bool fromRight, bool sameSide, float minBend, ImU32 col);
// カーソルの横に出す理由・日程の影響（noun = 「バー」「行」「カード」）
void drawLinkTip(Frame& f, const LinkEval& e, const char* noun);
// ドラッグ中、マウスが見えている範囲の端に近いと描いているウィンドウをスクロールする
void linkDragAutoScroll(Frame& f, ImVec2 mouse, ImVec2 clipMin, ImVec2 clipMax, bool horizontal);
// 一覧とボードの設定
struct LinkOverlayOpts {
  ImVec2 clipMin{0, 0}, clipMax{0, 0};  // 見えている範囲（この外ではハンドルを出さず、当たり判定もしない）
  const char* noun = "行";               // 「つなぐ相手の○までドラッグ」
  bool sameSide = false;                 // 相手の同じ側の端に付ける（一覧）
  bool sameColumn = false;               // 同じ列の相手には同じ側の端に付ける（ボード。遠くの列へは反対側の端）
  float hitR = 10.f;                     // ハンドルの当たり半径（拡大率をかける前）
  float bend = 24.f;                     // ゴムひもの曲がりの最小（拡大率をかける前）
  float round = 4.f;                     // 強調の角の丸み（拡大率をかける前）
  bool busy = false;                     // そのビュー固有のドラッグ中（ボードのカードの移動）はハンドルを出さない
  bool horizontalScroll = false;         // 横にもスクロールできる
};
// 行・カードを登録する。ハンドルの上にマウスがあれば 1（左）か 2（右）を返し、押されたらドラッグを始める
// （返り値が 0 でないときは、その行・カードのクリックやドラッグにしない）。描く前にこのフレームの先頭で linkSlots を clear しておく
int linkSlotAdd(Frame& f, const UiState::LinkSlot& slot, const LinkOverlayOpts& o);
// 登録した行・カードの上のハンドル・ドラッグ中のゴムひも・相手の強調・確定をまとめて描く（行・カードを描いたあとに1回）
void drawLinkOverlay(Frame& f, app::View view, const LinkOverlayOpts& o);
// 最近つないだ課題（いまのプロジェクトのもの。新しい順）
const std::vector<std::string>& recentLinks(const Frame& f);
// その場編集を始める（fresh = 追加直後の行）
void beginEdit(Frame& f, const std::string& id, EditField field, bool fresh = false);
// その場編集の入力欄を描く（カーソル位置に、幅 w で）。終わったら false
bool drawInlineEdit(Frame& f, float w);
bool editing(const UiState& u, const std::string& id, EditField field);
// 描画が終わってから実行する（行・カードの描画の途中で課題の数を変える操作に使う）
inline void deferAfterFrame(Frame& f, std::function<void()> fn) { f.u.deferred.push_back(std::move(fn)); }
// 課題を削除する（アプリの設定で「確かめる」なら確認ダイアログを出す）
void requestDelete(Frame& f, std::vector<std::string> ids);
// 上の絞り込みで薄くするときの不透明度（アプリの設定）
inline float dimAlpha(const app::AppController& c) { return static_cast<float>(c.prefs().dimPercent) / 100.f; }
// 選択をテンプレの追加先にしてダイアログを開く
void openTplApply(Frame& f);
void openTplSave(Frame& f, const std::string& id);
// クリップボードの複数行を、選んだ課題の子としてまとめて作る（プレビューのダイアログを開く）
void startPasteChildren(Frame& f);
// 入力欄の外の Ctrl+V: 課題が1つ選ばれていて、クリップボードにテキストがなく画像があれば、その画像を添付する。
// テキストがあれば今までどおり（複数行を子課題として貼り付ける）
void pasteFromClipboard(Frame& f);

// ---- 添付とファイルのドロップ ----
// この範囲（画面の座標。描画中のクリップ範囲に切り詰める）を、課題 issueId のドロップ先にする
void registerDropTarget(Frame& f, const std::string& issueId, ImVec2 a, ImVec2 b);
// 前のフレームに描いたドロップ先と照らして、pendingDrop を処理する（drawApp の先頭で呼ぶ）
void processDrop(Frame& f);
// 添付のサムネイルのドラッグ（別の課題へ移動・コピー、同じ課題の中の並べ替え）。processAttachDrag は drawApp の先頭
// （processDrop の次）で、離した位置を前のフレームのドロップ先と照らして実行する。drawAttachDrag は最後にカーソルへ付く表示を描く
void processAttachDrag(Frame& f);
void drawAttachDrag(Frame& f);
// 並べ替えのドラッグで、位置 pos に離したときの挿入位置（0〜添付の数）。添付の一覧の外なら -1
int attachReorderSlot(const UiState& u, const std::string& issueId, ImVec2 pos);
// 添付の本体を開く。実行ファイルなどは先に警告のダイアログを出す（確認したら開く）
void openAttachment(Frame& f, const std::string& issueId, std::size_t index);
// 添付の画像のテクスチャ（maxDim = 縮小の上限）。まだ作っていなければ nullptr（デコードは1フレームに1枚まで。
// 次のフレーム以降に出る）。壊れている・対応していないものは failed が true の項目を返す。画像でなければ nullptr
inline constexpr int kPreviewMaxPx = 1280;  // プレビューするときの画像の長辺（これより大きい画像は縮小して読む）
const UiState::ImageTex* attachmentImage(Frame& f, const std::string& issueId, std::size_t index, int maxDim);
// 画像をクリックしたときのプレビュー（画像でなければ何もしない）
void openImagePreview(Frame& f, const std::string& issueId, std::size_t index);
// テクスチャを解放する（releaseImages = 全部。trimImages = しばらく使っていないものだけ）
void releaseImages(Frame& f);
void trimImages(Frame& f);
// 添付の数のバッジ（クリップのアイコンと数）を描く。幅を返す。クリックされたら clicked を true にする。
// マウスを載せる（少し待つ）と、課題 issueId の添付のサムネイルのツールチップを出す（SPEC 10.9。載せていないときは画像を読まない）
float drawAttachBadge(Frame& f, const std::string& issueId, ImVec2 pos, float cy, int count, bool* clicked);

// 件名セル（開閉・種別・表示ID・件名・コメント数・ホバー時の小ボタン）
struct NameCellOpts {
  bool showHoverButtons = true;
  bool indent = true;
  // 件名の右に祖先のパスを薄く出す（階層が見えない平らな一覧用。行の高さは変えない）
  bool showPath = false;
};
void drawNameCell(Frame& f, const VisibleRow& r, ImVec2 pos, ImVec2 size, const NameCellOpts& o = {});

// 表の列（リストとガントの左の表で共有）
enum class Col { Grip, Name, Type, Status, Asg, Prio, Start, Finish, Dur, Float, Deadline, Pred };
struct ColDef {
  Col col;
  float w;
};
// 1行を描き、クリック・ダブルクリック・右クリックを扱う。flat = 並べ替え中（インデントなし）
void drawTableRow(Frame& f, const VisibleRow& r, ImVec2 pos, float width, const std::vector<ColDef>& cols, bool flat,
                  bool highlight = false);
// 見出しの行（列名の右の漏斗で列の絞り込み）
void drawTableHeader(Frame& f, ImVec2 pos, float width, float h, const std::vector<ColDef>& cols, bool sortable);

// 上の絞り込みに一致しない課題を薄く描く。作ってから壊すまでに描いた頂点の不透明度に a を掛ける
class FadeScope {
 public:
  FadeScope(ImDrawList* dl, bool on, float a = 0.3f) : dl_(on ? dl : nullptr), start_(dl->VtxBuffer.Size), a_(a) {}
  ~FadeScope() {
    if (!dl_) return;
    for (int i = start_; i < dl_->VtxBuffer.Size; ++i) {
      ImU32& c = dl_->VtxBuffer[i].col;
      const float al = static_cast<float>((c & IM_COL32_A_MASK) >> IM_COL32_A_SHIFT) * a_;
      c = (c & ~IM_COL32_A_MASK) | (static_cast<ImU32>(al) << IM_COL32_A_SHIFT);
    }
  }
  FadeScope(const FadeScope&) = delete;
  FadeScope& operator=(const FadeScope&) = delete;

 private:
  ImDrawList* dl_;
  int start_;
  float a_;
};
// 薄くする行か（その場編集中の行は薄くしない）
inline bool isDimmed(const UiState& u, const std::vector<char>& dim, int id, const std::string& sid) {
  return !dim.empty() && dim[id] && u.editId != sid;
}

// ルート課題の色番号 rootIndexMap と担当者名のハッシュ nameHash は core/gantt_geom（書き出しと共有）
// ゴール（子を持たず、種別かラベルが Milestone）
bool isGoal(const Tree& T, const std::vector<Issue>& issues, int id);
// 世代名（最上位・子・孫・ひ孫・玄孫・第N世代）
std::string generationName(int depth);
// 状況の分類での表示色
ImU32 catColor(const UiState& u, Cat c);

}  // namespace roadmap::ui
