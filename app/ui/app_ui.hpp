// 画面全体（SPEC 10章）。毎フレーム AppController の状態を読んで描く（即時モード）。
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <imgui.h>

#include "app_controller.hpp"
#include "ui/theme.hpp"

namespace roadmap::ui {

// プラットフォーム（Win32 など）が UI に提供する機能
class IPlatform {
 public:
  virtual ~IPlatform() = default;
  // ファイルを選んで中身（UTF-8 に直したもの）と名前を返す
  virtual std::optional<std::pair<std::string, std::string>> openTextFile() = 0;
  // 保存ダイアログ。成功したら true
  virtual bool saveTextFile(const std::string& suggestedName, const std::string& content) = 0;
  virtual void openFolder(const std::string& path) = 0;
  // フォルダを選ぶ（start = 最初に開く場所）。やめたら nullopt
  virtual std::optional<std::string> pickFolder(const std::string& start) = 0;
  virtual bool systemDark() = 0;
  virtual void requestQuit() = 0;
  // クリップボードのテキスト（UTF-8）。テキストがない・読めないときは nullopt。
  // 貼り付けた内容は個人情報を含みうるので、ログには出さない
  virtual std::optional<std::string> clipboardText() { return std::nullopt; }
  // 関連付けられたアプリでファイルを開く（添付の「開く」。実行ファイルの警告は UI が先に出す）。パスは UTF-8
  virtual void openFile(const std::string& path) { (void)path; }
  // 画像（RGBA 8bit）をテクスチャにする（サムネイル・プレビュー）。作れなければ nullptr。不要になったら releaseTexture
  virtual ImTextureID createTexture(const unsigned char* rgba, int w, int h) {
    (void)rgba, (void)w, (void)h;
    return nullptr;
  }
  virtual void releaseTexture(ImTextureID tex) { (void)tex; }
  // ドロップされた CSV / JSON を読んで UTF-8 にする（文字コードを判定する。読めなければ nullopt と理由）
  virtual std::optional<std::string> readTextFile(const std::string& path, std::string* error) {
    (void)path;
    if (error) *error = "このアプリでは読み込めません";
    return std::nullopt;
  }
};

enum class Dlg {
  None, NewProject, EditProject, ConfirmDelete, Import, Export, Folder, Settings, Workload, Warnings, Shortcuts,
  TplApply, TplSave, Conflict, ConfirmQuit, AppSettings, ConfirmIssueDelete, PasteChildren, AttachOpenWarn, AttachPreview
};

enum class EditField { None, Name, Assignee, Dur, Pred };

struct UiState {
  Metrics m;
  const Tokens* t = &lightTokens();
  ImFont* fontBody = nullptr;
  ImFont* fontMono = nullptr;
  ImFont* fontBold = nullptr;
  ImFont* fontLarge = nullptr;
  bool imeComposing = false;       // IME の変換中（プラットフォームが設定）
  int imeCooldown = 0;             // 変換を確定した直後のフレーム数（Enter を無視）

  // その場編集
  EditField editField = EditField::None;
  std::string editId;
  std::string editBuf;
  bool editFresh = false;          // 追加直後の行
  bool editFocus = false;          // 次のフレームでフォーカスを入れる
  bool editCursorEnd = false;      // 次に入力欄がアクティブになったとき、カーソルを末尾に置く（先行列）
  std::string editStartValue;
  // 編集を始めてから入力欄がアクティブになるまでに打った文字（落とさずに入れる。B13）
  bool editActive = false;
  int editArmFrame = 0;  // 編集を始めたフレーム
  std::vector<ImWchar> editPendingChars;

  // メニュー・ダイアログ
  Dlg dlg = Dlg::None;
  bool dlgOpening = false;
  std::string ctxId;               // 右クリックした課題
  bool ctxOpen = false;
  std::string statusMenuId;        // 状況ピルの小メニュー
  bool statusMenuOpen = false;
  bool projMenu = false, moreMenu = false, goalPop = false;
  bool paletteOpen = false;
  bool paletteFocus = false;
  std::string paletteQuery;
  int paletteSel = 0;
  bool focusSearch = false;

  // 詳細パネル
  bool detailOpen = false;
  int detailTab = 0;  // 0=詳細 1=依存関係 2=コメント 3=添付

  // ファイルのドロップ（エクスプローラーから）。プラットフォームが入れ、次のフレームの先頭で処理する。
  // ドロップ先が課題（行・カード・詳細パネル）なら添付、何もないところなら CSV / JSON の取り込み
  struct PendingDrop {
    bool active = false;
    std::vector<std::string> paths;  // UTF-8。ファイルまたはフォルダ
    ImVec2 pos{0, 0};                // ウィンドウの中の位置（ImGui の座標）
  } pendingDrop;
  // ドロップを受ける課題の範囲。前のフレームに描いたもの（dropTargets）で判定し、いまのフレームのものは dropBuild にためる
  struct DropTarget {
    ImVec2 a, b;
    std::string id;
  };
  std::vector<DropTarget> dropTargets, dropBuild;
  std::string attachWarnPath;  // 開く前に警告している添付の本体（Dlg::AttachOpenWarn）
  // 添付の画像（サムネイル・プレビュー）のテクスチャ。キー = パス・大きさ・日時・縮小の上限。
  // 使わなくなったものは解放し、プロジェクトを切り替えたら全部解放する（releaseImages / trimImages）
  struct ImageTex {
    ImTextureID tex = nullptr;
    int w = 0, h = 0;        // テクスチャの大きさ
    int origW = 0, origH = 0;  // 元の画像の大きさ
    bool failed = false;     // 壊れている・読めない・対応していない
    int lastFrame = 0;       // 最後に使ったフレーム
  };
  std::map<std::string, ImageTex> images;
  std::string imageProject;       // images を作ったプロジェクト
  int imageDecodedFrame = -1;     // 画像のデコードは1フレームに1枚まで
  std::string previewIssue;       // プレビュー中の添付（Dlg::AttachPreview）
  std::size_t previewIndex = 0;
  float previewZoom = 0;          // 0 = 全体に合わせる

  // 依存の相手を画面（一覧・ガント・ボード）のクリックで選ぶモード（Esc で中止）
  struct LinkPick {
    bool active = false;
    std::string anchor;          // 起点の課題
    bool anchorIsPred = false;   // true = 起点が先行（相手を後続に選ぶ）
    std::string project;         // 始めたプロジェクト（切り替わったら中止）
  } linkPick;
  // 最近つないだ課題（新しい順。プロジェクトごとに覚え、アプリを閉じると忘れる）
  std::string recentLinkProject;
  std::vector<std::string> recentLinks;

  // ダイアログの入力
  std::string dName, dKey, dText, dText2, dSearch;
  int dChoice = 0, dChoice2 = 0;
  std::vector<std::string> dTargets;  // テンプレの追加先など
  std::string exportKind = "jira";
  std::string importName;
  bool importReplace = false;
  std::string importError;
  std::vector<std::string> conflictNames;

  // ガント
  bool scrollToBase = true;
  std::string scrolledProject;
  std::string lastZoom;
  struct BarDrag {
    std::string id;
    bool resize = false;
    float startX = 0;
    Day origStart = 0, origFinish = 0;
    bool active = false;
  } barDrag;
  // 依存をつなぐドラッグ（バーの端のハンドルから別のバーへ。Esc・元の場所で離すと取り消し）
  struct LinkDrag {
    bool active = false;
    std::string id;           // つかんだバーの課題
    bool fromRight = true;    // 右のハンドル = つかんだバーが先行（相手が後続）。左なら逆
    std::string project;      // 始めたプロジェクト
    ImVec2 start{0, 0};       // 押した位置（つかんだバーが画面にないときの線の始点）
    // つなぐ前の日程の影響（相手が変わったときだけ計算し直す）
    std::string previewKey;
    std::uint64_t previewVer = 0;
    LinkPreview preview;
  } linkDrag;
  // 依存の矢印をつかんで付け替え・削除するドラッグ（矢印の上で押して動かす。Esc・元の場所で離す・タイムライン外で離すと取り消し）
  struct ArrowDrag {
    bool pending = false;     // 矢印の上で押した（まだ動いていない。付け替えできない矢印のときはそのまま）
    bool editable = false;    // つかんで付け替え・削除できる矢印か
    bool active = false;
    std::string from, to;     // つかんだ矢印の元のつなぎ（先行 → 後続の課題 id）
    bool grabHead = true;     // true = 矢じり側（後続）の端をつかんだ。false = 根元（先行）の端
    std::string project;
    ImVec2 start{0, 0};
    // つなぎ替えの日程の影響（相手が変わったときだけ計算し直す）
    std::string previewKey;
    std::uint64_t previewVer = 0;
    LinkPreview preview;
  } arrowDrag;
  // 前のフレームのガントの依存の矢印（画面の座標の折れ線）と、マウスの下の矢印。煙テストが当たり位置を知るために使う
  struct ArrowGeom {
    std::string from, to;     // 矢印の両端の行の課題 id
    std::vector<ImVec2> pts;
    bool conflict = false;    // 後続が先行より前の日付
    bool critical = false;    // 余裕 0 以下の連鎖
  };
  std::vector<ArrowGeom> arrowGeom;
  std::string arrowHoverFrom, arrowHoverTo;
  // 前のフレームのガントのバーの位置（画面の座標。見えている行だけ）。煙テストが当たり位置を知るために使う
  struct BarGeom {
    std::string id;
    float xl = 0, xr = 0, cy = 0;
  };
  std::vector<BarGeom> barGeom;
  // 前のフレームの統計ビューの負荷ヒートマップ（画面の座標。見えている行のセルだけ）と、集計の行数。
  // 煙テストが描画の有無と当たり位置を知るために使う
  struct StatsLoadCell {
    std::string who;
    int week = 0;
    ImVec2 a{0, 0}, b{0, 0};
    int percent = 0;
  };
  std::vector<StatsLoadCell> statsLoadCells;
  int statsLoadRows = 0, statsDoneRows = 0;
  struct RowDrag {
    std::string id;
    bool active = false;
  } rowDrag;
  struct ColDrag {
    std::string col;
    float startX = 0;
    float startW = 0;
    bool active = false;
  } colDrag;

  // リスト
  std::string sortKey;
  std::string openColFilter;  // 次のフレームで開く列の絞り込み（列のキー）
  // 行・カードの途中で起きた「課題の数が変わる操作」（削除・子の追加）は、描画が終わってから行う。
  // 描画中に課題が減ると、残りの行が古い行番号で新しい計算結果を読んで範囲外になるため
  std::vector<std::function<void()>> deferred;
  // 拡大縮小
  float zoomAcc = 0;                // Ctrl+ホイールの細かい回転（タッチパッドのピンチ）をためる
  std::int64_t zoomLastStepMs = -1000;  // 画面全体の拡大縮小を最後に動かした時刻（作り直しが続かないように間引く）
  std::int64_t zoomHintUntilMs = 0;     // 倍率の表示を出す期限
  std::string zoomHint;
  bool wheelConsumed = false;       // このフレームの Ctrl+ホイールはガントが使った
  float customPx = 0;               // ガントの時間軸を Ctrl+ホイールで変えた1日の幅（倍率1のとき。0 = 日・週・月・全体の段階）
  std::string customProject;        // customPx を設定したプロジェクト
  float ganttSx = 0;                // ガントの横スクロール（前のフレームの値）
  float ganttLeft = 0;              // 時間軸の左端（ガントの左の表の右端。画面の座標）
  float ganttPx = 1;                // 1日の幅（画面の座標。前のフレームの値）
  float pendingScrollX = 0;
  bool hasPendingScroll = false;
  // アプリの設定: 選んだ新しい保存先（切り替えの確認中）
  std::string pendingDir, pendingError;
  bool pendingCopy = true;
  int pendingCount = 0;
  std::vector<std::string> pendingDelete;  // 削除の確認ダイアログで消す課題
  // 貼り付けで子課題をまとめて作る（確認ダイアログ）
  std::string pasteParent;  // 追加先の親
  Outline paste;            // 貼り付けた内容を解析したもの（ダイアログを閉じたら捨てる）
  int sortDir = 0;  // 0=解除 1=昇順 -1=降順

  // 画面に並んでいる順（Shift 選択・矢印用）
  std::vector<std::string> order;
  // フォントに足りない文字（プラットフォームがフォントを作り直す）
  std::set<ImWchar> missingGlyphs;
  bool fontsDirty = false;
};

struct Frame {
  app::AppController& c;
  UiState& u;
  IPlatform& p;
};

// 1フレーム分を描く（ImGui::NewFrame と Render の間で呼ぶ）
void drawApp(app::AppController& c, UiState& u, IPlatform& p);
// 画面の文言に使っている文字（tools/gen_ui_glyphs.py が作る一覧）。フォントの作成時に必ず入れる
const std::vector<ImWchar>& uiGlyphs();
// 画面の文字のうち、フォントにないものを集める（プラットフォームがフォントを作り直す判断に使う）
void collectGlyphs(const app::AppController& c, std::set<ImWchar>& out);

}  // namespace roadmap::ui
