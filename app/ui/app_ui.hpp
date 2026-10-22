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
#include "roadmap/report.hpp"
#include "roadmap/trends.hpp"
#include "roadmap/weekly.hpp"
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
  // 保存先を選ぶ（名前を付けて保存）。suggestedName = 最初に入れる名前、ext = 既定の拡張子（"html"）、typeLabel = 種類の名前。
  // 選んだパス（UTF-8）を返す。やめたら nullopt。既存のファイルの上書きは、このダイアログが確認する。書き込みは呼び出し側が
  // IFileStore で行う
  virtual std::optional<std::string> pickSaveFile(const std::string& suggestedName, const std::string& ext,
                                                  const std::string& typeLabel) = 0;
  // フォルダを選ぶ（start = 最初に開く場所）。やめたら nullopt
  virtual std::optional<std::string> pickFolder(const std::string& start) = 0;
  virtual bool systemDark() = 0;
  virtual void requestQuit() = 0;
  // クリップボードのテキスト（UTF-8）。テキストがない・読めないときは nullopt。
  // 貼り付けた内容は個人情報を含みうるので、ログには出さない
  virtual std::optional<std::string> clipboardText() { return std::nullopt; }
  // クリップボードにテキスト（UTF-8）を書く。成功したら true。書けない・大きすぎるときは false
  // （週次レポートの「Markdown をコピー」。内容は個人情報を含みうるので、ログには出さない）
  virtual bool setClipboardText(const std::string& text) {
    (void)text;
    return false;
  }
  // 関連付けられたアプリでファイルを開く（添付の「開く」。実行ファイルの警告は UI が先に出す）。パスは UTF-8
  virtual void openFile(const std::string& path) { (void)path; }
  // 関連リンク（URL・ファイル・フォルダの場所）を既定のブラウザ／アプリ／エクスプローラーで開く（Issue #60）。target は UI が
  // classifyLink で検査し、実行ファイルの警告も済ませた場所。プラットフォームも許可リストを確かめる。存在確認はしない。
  // 開けなければ false と理由（日本語）を error に入れる。場所は個人情報を含みうるので、ログには出さない
  virtual bool openLink(const std::string& target, std::string* error) {
    (void)target;
    if (error) *error = "このアプリでは開けません";
    return false;
  }
  // 画像（RGBA 8bit）をテクスチャにする（サムネイル・プレビュー）。作れなければ nullptr。不要になったら releaseTexture
  virtual ImTextureID createTexture(const unsigned char* rgba, int w, int h) {
    (void)rgba, (void)w, (void)h;
    return nullptr;
  }
  virtual void releaseTexture(ImTextureID tex) { (void)tex; }
  // ファイルを選ぶダイアログ（複数選択。添付の「ファイルを選んで追加」）。選んだパス（UTF-8）を返す。やめた・開けないときは空。
  // ファイル名は個人情報を含みうるので、ログには出さない
  virtual std::vector<std::string> pickOpenFiles(const std::string& title) {
    (void)title;
    return {};
  }
  // クリップボードの画像を、BMP ファイルのバイト列（14 バイトのヘッダつき）で返す。画像がない・読めない・大きすぎる・
  // ヘッダが壊れているときは nullopt（Win32 は CF_DIBV5 / CF_DIB を読んで core の bmpFromDib で包む）。内容はログに出さない
  virtual std::optional<std::string> clipboardImage() { return std::nullopt; }
  // ドロップされた CSV / JSON を読んで UTF-8 にする（文字コードを判定する。読めなければ nullopt と理由）
  virtual std::optional<std::string> readTextFile(const std::string& path, std::string* error) {
    (void)path;
    if (error) *error = "このアプリでは読み込めません";
    return std::nullopt;
  }
};

enum class Dlg {
  None, NewProject, EditProject, ConfirmDelete, Import, Export, Folder, Settings, Workload, Warnings, Shortcuts,
  TplApply, TplSave, Conflict, ConfirmQuit, AppSettings, ConfirmIssueDelete, PasteChildren, AttachOpenWarn, AttachPreview,
  ExportHtml, Weekly, BaselineSave, BaselineManage, ConfirmBaselineDelete, LinkOpenWarn, ConfirmTakeOver
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
  // 行・カードの添付バッジの範囲（煙テストが、マウスを載せる位置を知るため。前のフレームのものが attachBadges）
  std::vector<DropTarget> attachBadges, attachBadgeBuild;
  std::string attachWarnPath;  // 開く前に警告している添付の本体（Dlg::AttachOpenWarn）
  // 添付のサムネイル（添付タブの行）をつかんで別の課題へ動かすドラッグ（離した位置のドロップ先は dropTargets で調べる）。
  // 別の課題の上で離すと移動（Ctrl を押していればコピー）、同じ課題の添付の一覧の上なら並べ替え、何もないところなら何もしない。
  // Esc・プロジェクトの切り替え・ダイアログを開いたときは取り消す
  struct AttachDrag {
    bool active = false;
    bool link = false;     // true = つかんだのは添付でなく関連リンク（リンクの行。Issue #60）。移動先の判定・Esc などは同じ仕組み
    std::string issueId;   // つかんだ添付（リンク）の課題
    std::size_t index = 0; // つかんだ添付（リンク）の番号
    std::string project;   // 始めたプロジェクト
  } attachDrag;
  int attachDragEscFrame = -10;      // 添付のドラッグを Esc で取り消したフレーム（そのフレームの Esc は、ほかの Esc の動作に使わない）
  bool attachDragCancelled = false;  // 取り消したドラッグ。マウスのボタンを離すまで、つかみ直したことにしない
  // 前のフレームの添付タブの行の範囲（画面の座標）。並べ替えの位置の判定と、煙テストの当たり位置に使う
  std::vector<std::pair<ImVec2, ImVec2>> attachRows;
  std::string attachRowsIssue;
  int attachRowsFrame = -10;
  // 添付タブの一覧（スクロールする子ウィンドウ）の見える範囲。この外では並べ替えの位置を出さない（隠れた行へ並べ替えない）
  std::pair<ImVec2, ImVec2> attachRowsClip{{0, 0}, {0, 0}};
  // 前のフレームの添付タブの「ファイルを選んで追加」ボタンとサムネイルの大きさのスライダーの範囲（画面の座標）。煙テストの当たり位置
  std::pair<ImVec2, ImVec2> attachPickBtn{{0, 0}, {0, 0}}, attachSlider{{0, 0}, {0, 0}};
  // ---- 関連リンク（Issue #60。添付タブの中の「リンク」の節）----
  std::string linkTargetBuf, linkTitleBuf;  // 追加・編集の入力欄（場所・表示名）
  std::string linkError;                    // 直前の追加・編集で断られた理由（入力欄の下に出す。入力しなおすと消える）
  bool linkEditing = false;                 // 編集中（入力欄に既存のリンクを入れている）
  std::string linkEditIssue, linkEditOrig;  // 編集中のリンクの課題・もとの場所（一覧が変わって別のリンクを指したら編集をやめる）
  std::size_t linkEditIndex = 0;
  bool linkFocusTarget = false;             // 次のフレームで場所の入力欄にフォーカスを入れる
  std::string linkWarnTarget;               // 開く前に警告しているリンクの場所（Dlg::LinkOpenWarn）
  // 前のフレームのリンクの行の範囲（画面の座標）。並べ替えの位置の判定と、煙テストの当たり位置に使う（attachRows と同じ）
  std::vector<std::pair<ImVec2, ImVec2>> linkRows;
  std::string linkRowsIssue;
  int linkRowsFrame = -10;
  std::pair<ImVec2, ImVec2> linkRowsClip{{0, 0}, {0, 0}};
  // 前のフレームのリンクの節の部品の範囲（画面の座標。煙テストが当たり位置を知るため）
  std::pair<ImVec2, ImVec2> linkTargetBox{{0, 0}, {0, 0}}, linkTitleBox{{0, 0}, {0, 0}}, linkAddBtn{{0, 0}, {0, 0}},
      linkPasteBtn{{0, 0}, {0, 0}}, linkCancelBtn{{0, 0}, {0, 0}};
  // リンクの右クリックのメニュー: 開いたリンク（課題・番号）と、前のフレームの項目の範囲（煙テストが押す位置を知るため）
  std::string linkCtxIssue;
  std::size_t linkCtxIndex = 0;
  bool linkCtxRequest = false;
  struct MenuItemGeom {
    std::string label;
    ImVec2 a{0, 0}, b{0, 0};
    bool enabled = true;
  };
  std::vector<MenuItemGeom> linkCtxItems;
  // 行・カードのリンクのバッジの範囲（前のフレームのものが linkBadges）と、最後に出したバッジのツールチップの行（煙テスト用）
  std::vector<DropTarget> linkBadges, linkBadgeBuild;
  // ---- GitHub の状態の確認（Issue #61）----
  // 前のフレームの節の部品の範囲（画面の座標。煙テストが当たり位置を知るため）と、出した状態の表示（行ごと。出さない行は空）
  std::pair<ImVec2, ImVec2> linkCheckBtn{{0, 0}, {0, 0}}, toastActionBtn{{0, 0}, {0, 0}};
  std::vector<std::string> linkStatusShown;
  std::vector<std::string> linkTipLines;
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

  // 読み取り専用（他の人が編集中など。Issue #62）。drawApp が毎フレームの先頭で設定する。ドラッグを始めない判断に使う
  bool ro = false;
  // ダイアログの下端のボタンの範囲（最後に描いたもの。煙テストが押す位置を知る）
  std::vector<std::pair<std::string, std::pair<ImVec2, ImVec2>>> dlgButtons;
  // 読み取り専用の帯（Issue #62）: 前のフレームに帯を出したか、出した文言、ボタンの範囲（画面の座標。煙テストが押す位置を知る）
  bool roBandShown = false;
  std::string roBandText;
  std::pair<ImVec2, ImVec2> roRecheckBtn{{0, 0}, {0, 0}}, roTakeoverBtn{{0, 0}, {0, 0}};

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
  // 「HTML に書き出す」ダイアログの選択（開くたびに画面の状態から初期化する）
  ExportOptions rep;
  int repScope = 0;  // 0 = 全体 1 = 今の表示のまま 2 = 選んだ課題の配下
  // 「週次レポート」ダイアログ（開くたびに初期化する。Issue #43）
  WeeklyOptions wk;
  int wkPeriod = 0;               // 0 = 今週 1 = 先週 2 = 任意の範囲
  std::string wkFrom, wkTo;       // 任意の範囲（YYYY-MM-DD の入力欄）
  std::string wkPreview;          // Markdown のプレビュー（wkPreviewKey が変わったときだけ作り直す）
  std::string wkPreviewKey, wkError;
  std::string wkProject;          // ダイアログを開いたときのプロジェクト（切り替わっていたら所感を書き込まない）
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
    app::View view = app::View::Gantt;  // 始めたビュー（ガント・一覧・ボード。切り替わったら終わる）
    ImVec2 start{0, 0};       // 押した位置（つかんだバー・行・カードが画面にないときの線の始点）
    // つなぐ前の日程の影響（相手が変わったときだけ計算し直す）
    std::string previewKey;
    std::uint64_t previewVer = 0;
    LinkPreview preview;
  } linkDrag;
  // 依存のドラッグを空いた場所で離したときのメニュー「新しい後続（先行）の課題を作ってつなぐ」（Issue #8）。
  // 離したフレームに request を立て、オーバーレイで開く。Esc・メニューの外のクリックで閉じる（何も変えない）
  struct LinkMenu {
    bool request = false;     // 開く要求（次に描くときに OpenPopup する）
    bool active = false;      // 開いている（ほかのドラッグ・ハンドルを出さない）
    std::string id;           // 起点の課題
    bool fromRight = true;    // つかんだハンドル（右 = 主になる項目は「後続」。左なら「先行」）
    std::string project;      // 開いたプロジェクト（切り替わったら閉じる）
    app::View view = app::View::Gantt;  // 開いたビュー（ボードは作る課題の状況を起点に合わせる）
    int escFrame = -1;        // Esc で閉じたフレーム（同じ Esc で選択を外さない）
    // 前のフレームの項目の位置（画面の座標。煙テストが押す位置を知るために使う）
    std::pair<ImVec2, ImVec2> succItem{{0, 0}, {0, 0}}, predItem{{0, 0}, {0, 0}}, cancelItem{{0, 0}, {0, 0}};
  } linkMenu;
  // 一覧の行・ボードのカードの、依存のドラッグ用の位置（画面の座標。このフレームに描いた見えているものだけ。
  // 描き始めに clear し、linkSlotAdd で足す。煙テストが当たり位置を知るためにも使う）
  struct LinkSlot {
    int idx = -1;                 // 課題の添字（tree()）
    std::string id;               // 描いた時点の tree() での課題 id（煙テストが、同じフレームの描画が1つの計算結果に揃っているかを見る）
    ImVec2 a{0, 0}, b{0, 0};      // 行・カードの枠（相手に選んだとき強調する範囲）
    float padX = 0;               // 相手に選べる範囲を、左右にここまで広げる（ハンドルのある余白）
    ImVec2 hl{0, 0}, hr{0, 0};    // 左右のハンドルの中心
    // 相手にしたとき、ゴムひもの先が付く点。左のハンドルで来た線 / 右のハンドルで来た線（landL, landR = 反対側の端へ）と、
    // 同じ側の端へ回り込む線（sameL, sameR = 一覧、ボードの同じ列）
    ImVec2 landL{0, 0}, landR{0, 0};
    ImVec2 sameL{0, 0}, sameR{0, 0};
  };
  std::vector<LinkSlot> linkSlots;
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
  // ベースライン（Issue #53）: 管理ダイアログで名前を変えている行の id、削除の確認中の id
  std::string blRenameId, blDeleteId;
  bool blRenameFocus = false;
  // 前のフレームのガントの計画のバー（画面の座標。見えている行だけ）。煙テストが描画の有無と位置を知るために使う
  struct PlanGeom {
    std::string id;
    float xl = 0, xr = 0, y0 = 0, y1 = 0;  // 計画のバー（マイルストーンは菱形を囲む四角）
    bool milestone = false;
    std::string label;                      // 差の文字（"! +3日" など。出さないときは空）
  };
  std::vector<PlanGeom> planGeom;
  // 前のフレームの統計ビューの「計画との差」の節（煙テストが描画の有無と当たり位置を知るために使う）
  struct StatsPlan {
    bool drawn = false;
    int tiles = 0;                          // 件数のタイルの数
    std::vector<DropTarget> lateRows;       // 遅れの大きい順の行（id = 課題の Issue Id）
  } statsPlan;
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
  // 統計ビューの「推移」（Issue #54 の A）: 絞り込みの選択（プロジェクトごと。切り替えたら初期化）と、
  // 前のフレームの描画の結果（煙テストが当たり位置と件数を知るために使う）
  struct StatsTrend {
    std::string projectId;       // この選択がどのプロジェクトのものか
    TrendFilter::Who who = TrendFilter::Who::All;
    std::string name;            // who == Name のときの担当者名
    bool standalone = false;     // ルート課題: 親のない単独の課題
    std::string rootId;          // ルート課題の Issue Id（空 = すべて）
    // ---- 前のフレームの描画 ----
    bool drawn = false;
    int items = 0, weeks = 0, leadN = 0, agingN = 0;
    bool forecastOk = false;
    struct Col {
      ImVec2 a{0, 0}, b{0, 0};
    };
    std::vector<Col> weekCols;   // 週ごとの当たり（過去の週だけ）
    Col lastCol;                 // 予測の週も含めた最後の列
    float chartX1 = 0;           // 図の右端（画面の座標）。列がここに収まっていることを煙テストが確かめる
    struct AgingRow {
      std::string id;
      ImVec2 a{0, 0}, b{0, 0};
    };
    std::vector<AgingRow> agingRows;
    ImVec2 whoA{0, 0}, whoB{0, 0}, rootA{0, 0}, rootB{0, 0};  // 担当者・ルート課題の選択欄
  } statsTrend;
  // 統計ビューの「状況の件数の推移（累積フロー）」（Issue #64）: 前のフレームの描画の結果（煙テストが当たり位置と件数を知るために使う）
  struct StatsCfd {
    bool drawn = false;         // 節を描いた
    bool chart = false;         // 図を描いた（記録が 2 点以上）
    bool pending = false;       // 「記録が溜まるとここに出ます」の案内を出した
    int recorded = 0;           // 記録のある週の数
    int weeks = 0;              // 図の列の数
    int gaps = 0;               // 記録のない週の数
    float chartX1 = 0;          // 図の右端（画面の座標）
    std::vector<StatsTrend::Col> cols;  // 週ごとの当たり
    ImVec2 recA{0, 0}, recB{0, 0};      // 「いま記録する」ボタン
  } statsCfd;
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
