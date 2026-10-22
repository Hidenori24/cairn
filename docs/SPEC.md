# プロジェクト管理ツール（C++版）仕様書

- 版: 1.0（2026-10-19）
- 対象読者: この仕様をもとにゼロから実装する開発者（Claude Code を含む）
- 位置づけ: 既存のブラウザ版（HTML/JavaScript、以下「JS版」）を、**C++だけで作り直す**ための仕様。画面・計算・CSV入出力・保存の挙動はJS版と同じにする。

---

## 0. この文書の使い方

### 0.1 一緒に渡すもの

| 場所 | 中身 | 使い方 |
|---|---|---|
| `SPEC.md`（この文書） | 仕様 | 実装の根拠 |
| `golden/` | JS版が出した計算結果（JSON）・書き出しCSV・サンプルCSV・休業日 | **テストの正解データ**。C++版の出力と完全一致させる（第12章） |
| ~~`reference-js/`~~ | （0.3.1 で削除。JS版のソースはこのリポジトリに含まない） | — |

### 0.2 優先順位（食い違ったとき）
1. `golden/` の正解データ
2. この仕様書
3. （削除済み）JS版のソース。以降の本文にある `reference-js/…` への参照は、当時の確認先を示す記録で、今は参照できない

仕様書とJS版が食い違い、JS版が明らかな不具合に見える場合は、**黙って直さず**、第14章の「疑問・差分メモ」に書いてから判断を仰ぐこと。

### 0.3 用語
- **課題（Issue）**: 1行のタスク。Epic・Story・Task・Sub-task・Bug・Milestone などの種別を持つ。
- **葉（leaf）**: 子を持たない課題。**日程を持つのは葉だけ**。親は配下の葉から集計した「まとめ行」。
- **営業日**: 土日と休業日を除く日。**1週 = 5営業日**。
- **最早（ASAP）**: 先行関係だけで決まる、最も早い開始・完了。
- **最遅（ALAP）**: 期限から逆算した、最も遅い開始・完了。
- **余裕（float）**: 最早完了から最遅完了までの営業日数。負なら遅れ。
- **シナリオ**: 期間（週）の別案。名前を付けて切り替える。標準は「標準」。
- **ゴール**: 種別が Milestone の葉。
- **プロジェクト**: 課題の集合1つ = CSV 1ファイル + 設定ファイル 1つ。

---

## 1. 目的・範囲

### 1.1 目的
JIRA互換のCSVを原本に、課題の階層・依存関係・期間から、**営業日ベースの日程（最早・最遅・余裕）を自動計算**し、ガント・ボード・リスト・ゴール（ギャップ）で見ながら編集する、デスクトップアプリ。

### 1.2 必須（MVP）
1. 課題の階層編集（追加・削除・インデント・並べ替え・ドラッグ移動）
2. 依存関係（Blocks）と、親への依存の配下への展開
3. スケジュール計算（最早・最遅・余裕、親のまとめ、期限の継承、シナリオ、担当者ごとの同時作業制限）
4. 5つの表示: ガント / ボード / リスト / ゴール（ギャップ） / 統計（0.4.0 で追加）
5. 詳細パネル（編集）、コマンドパレット、元に戻す/やり直す
6. CSV入出力（JIRA互換）。JIRA用の書き出しは階層を3段に畳む
7. データフォルダへの自動保存（CSV + 設定JSON）、外部変更の取り込み、バックアップ
8. テンプレート（子タスクの雛形）
9. 絞り込み（検索語・担当・状況・優先度・遅れ/余裕小）
10. 日本語UI（IME入力を含む）、ライト/ダークテーマ、密度（標準/コンパクト）

### 1.3 非ゴール
- ネットワーク機能・複数人の同時編集・クラウド同期
- JIRAへの直接接続（CSVのみ）
- Windows以外のGUI対応（**コアとテストはLinuxでもビルド・実行できること**。GUIは Windows 10/11 x64 のみ必須）
- スマートフォン・Web対応

### 1.4 JS版からの意図的な違い
| JS版 | C++版 |
|---|---|
| データはブラウザの localStorage にも保存 | **持たない**。保存先はデータフォルダのみ |
| 初回はサンプル2件を自動投入 | 自動投入しない。初回画面でサンプルを選べる（8.7） |
| フォルダ選択ダイアログと許可の再確認 | 不要。データフォルダは起動引数・既定の場所で決める（8.1） |
| ブラウザのダウンロードで書き出し | ファイル保存ダイアログで書き出し |
| 設定の `hint`（初回ヒント） | 初回だけ操作ヒントのバーを出す（任意） |
| バックアップの数に上限なし | プロジェクトごとに30個まで（超えたら古いものから削除。8.4） |
| `data/projects` に `プロジェクト名.csv` と `.settings.json` を平置き | **プロジェクトごとのフォルダ**（`<プロジェクト>/project.csv` ほか。8.2）。JS版は使わなくなったため、JS版との相互読み書きは求めない。以前の平置きは読み続ける（互換読み。8.2・8.8） |

**それ以外は同じ挙動にする。**

---

## 2. 技術要件

### 2.1 言語・ツール
- **C++20**（MSVC 2022 17.8+、GCC 12+、Clang 16+）。例外は使ってよいが、`core` の公開APIの失敗は `std::expected` 風の結果型（C++20では自前の `Result<T>`）で返し、UIに例外を漏らさない。
- ビルド: **CMake 3.24+ / Ninja**。依存は `vcpkg`（マニフェストモード）か `FetchContent` でバージョンを固定する。
- 文字コード: ソース・内部は**UTF-8**（`std::string`）。MSVC は `/utf-8`。Windows APIとの境界でだけUTF-16に変換する。
- 警告: MSVC `/W4`、GCC/Clang `-Wall -Wextra -Wpedantic`。CIでは警告をエラーにする（サードパーティは除く）。
- 静的解析: `clang-tidy`（`bugprone-*`、`performance-*`、`modernize-*` の一部）をCIで実行。
- フォーマット: `clang-format`（Google系ベース、幅100）。

### 2.2 構成要素
```
project-roadmap/
├─ CMakeLists.txt
├─ vcpkg.json
├─ CLAUDE.md                 （付録C）
├─ docs/SPEC.md              （この文書）
├─ golden/                   （正解データ。第12章）
├─ core/                     ← UI非依存の静的ライブラリ（roadmap_core）
│  ├─ include/roadmap/…      公開ヘッダ
│  └─ src/…
├─ cli/                      ← コマンドライン（rmcli）。計算・変換・検査を画面なしで実行
├─ app/                      ← GUI（roadmap_app）
│  ├─ platform/              Win32・DX11・フォント・IME・ファイルダイアログ
│  ├─ ui/                    画面（ガント・ボード・リスト・ゴール・パネル）
│  └─ resources/             サンプルCSV・組み込みテンプレート・アイコン
├─ tests/
│  ├─ unit/                  Catch2（または doctest）
│  └─ golden/                正解データとの照合
└─ .github/workflows/
```

### 2.3 依存の方針（最小限）
| 用途 | ライブラリ | 条件 |
|---|---|---|
| テスト | Catch2 v3（または doctest） | core・cli のテストのみで使う |
| JSON | nlohmann/json | settings・prefs・golden読み込み。coreのみ |
| GUI | **Dear ImGui**（Win32 + DirectX 11 バックエンド） | appのみ。第3.4節の判断ゲートを通ること |
| ファイルダイアログ | Win32 `IFileOpenDialog` / `IFileSaveDialog` | appのみ |

- 正規表現は `std::regex` を**使わない**（遅く、UTF-8に弱い）。必要な判定は第5〜7章に書いた規則を手書きで実装する。
- それ以外のサードパーティを足すときは、理由をPRに書く。

### 2.4 配布形態
- **単一の `.exe`**（ポータブル）。MSVC は静的CRT（`/MT`）、ImGui は静的リンク。システムDLL以外に依存しない。目標サイズ 10MB 以下。
- レジストリに書かない。インストーラ不要。
- 同じフォルダ（またはその下）に `data/projects/` を置けば、フォルダごと持ち運べる（8.1）。プロジェクトも、フォルダごと持ち運べる・zip で共有できる（8.2）。

---

## 3. アーキテクチャ

### 3.1 層と依存の向き
```
app/ui  ──▶  app/controller（AppController）──▶  core（roadmap_core）
app/platform ─┘                                     ▲
cli ───────────────────────────────────────────────┘
```
- **`core` は GUI・Win32・ImGui を一切 include しない。**
- ファイルシステムと時計は、`core` の小さなインターフェース（`IFileStore`、`IClock`）で受ける。テストでは偽物（メモリ上）を差し込む。
- `AppController` は「現在のプロジェクト・選択・編集状態・Undo・フィルタ」を持ち、UIからの操作を `core` のコマンドに変換する。ImGui の型は持たない。
- UI（`app/ui`）は毎フレーム `AppController` の状態を読んで描くだけにする（即時モード）。状態の複製を作らない。

### 3.2 `core` の主なモジュール
| モジュール | 責務 | 該当章 |
|---|---|---|
| `calendar` | 日付型、営業日計算 | 5 |
| `model` | `Issue`、`ProjectData`、`ProjectSettings` | 4 |
| `csv` | CSVの字句解析・取り込み・書き出し | 6 |
| `tree` | 課題ツリー構築（`Tree`）、警告 | 7.2 |
| `engine` | スケジュール計算、担当者負荷 | 7.3〜7.6 |
| `commands` | 編集操作、`mutate`、Undo/Redo | 9 |
| `filter` | 絞り込み、可視行の列挙 | 10.8 |
| `templates` | 子タスク雛形 | 9.4 |
| `store` | データフォルダの読み書き・競合解決 | 8 |

### 3.3 スレッド
- UIスレッド 1本を基本とする。保存（ディスク書き込み）はバックグラウンドのワーカーで行ってよいが、**`core` のデータ構造へのアクセスはUIスレッドのみ**。書き込み用には、UIスレッドで直列化した文字列（CSV・JSON）を渡す。
- スケジュール計算は 5,000 件でも 50ms 以内（11章）なので、UIスレッドで同期実行する。

### 3.4 UIフレームワークの選択と判断ゲート（フェーズ0）
**第一候補: Dear ImGui + Win32/DX11。** 理由: ガントなどの自前描画が書きやすく、単一exeにしやすい。

ただし、この種のアプリは**日本語IME入力**と**表の中での編集**が品質を左右する。実装の最初に**スパイク（使い捨ての試作）**で次を確認し、**1つでも満たせなければ Qt 6 Widgets（LGPL・動的リンク）に切り替える**。`core` と `AppController` は共通なので、切り替えのコストはUI層だけで済むようにする。

| 確認項目 | 合格基準 |
|---|---|
| 日本語フォント | 「Yu Gothic UI」→「Meiryo UI」→「MS UI Gothic」の順にシステムフォントを読み込み、常用漢字・かな・全角記号が欠けずに表示できる。起動時間が +300ms 以内 |
| IME | テキスト欄で変換中の文字が確認でき、候補ウィンドウが入力位置の近くに出て、確定できる。Enter で確定した直後に別の操作が走らない |
| 表の編集 | 行をダブルクリックして件名をその場で編集できる |
| ツリー表 | 5,000行で、スクロールが 60fps 近く保たれる（可視行だけ描画） |
| ドラッグ | 行のドラッグ（並べ替え）、ガントのバーのドラッグができる |
| 高DPI | 150% / 200% 表示で文字がにじまず、レイアウトが崩れない |

---

## 4. データモデル

### 4.1 型
```cpp
using Day = std::int32_t;            // 1970-01-01 からの日数（UTC）。日付なしは std::optional<Day>

struct Comment { std::string at;     // "YYYY-MM-DDTHH:MM"（空なら日時なし）
                 std::string who, text; };

struct Issue {
  std::string id;                    // プロジェクト内で一意。通常は数字の文字列
  std::string key;                   // JIRAのキー（"DEV-12"）。空でもよい
  std::string type;                  // Epic / Story / Task / Sub-task / Bug / Milestone / その他の文字列
  std::string summary;               // 件名。先頭が "[B-1] " なら番号タグ（4.3）
  std::string parent;                // 親の id。空なら最上位
  std::string prio;                  // Highest/High/Medium/Low/Lowest（その他の文字列も保持）
  std::string status;                // To Do / In Progress / Done / その他
  std::string assignee;
  std::optional<Day> start, due, deadline, resolved, notBefore;
  //   start=実績開始、due=Due date、deadline=目標期限、resolved=完了日、notBefore=着手可能日（Earliest start）
  std::optional<double> durW, estW, remW;     // 期間(週)、元の見積(週)、残り(週)
  std::map<std::string, double> alt;          // シナリオ名 → 期間(週)
  std::vector<std::string> labels;
  std::string desc;
  std::vector<std::string> blocks;            // この課題が「ブロックする」課題の id（= 後続）
  std::vector<Comment> comments;
  std::string epicName;                       // Epic Name 列
  std::vector<std::string> extra;             // 未知の列の値（meta.extraHeaders と同じ並び）
};

struct ProjectMeta {
  bool hasDeadlineCol = false;                // CSVに Deadline 列があったか
  std::vector<std::string> scenarios;         // 「標準」以外のシナリオ名（出現順）
  std::vector<std::string> extraHeaders;      // 未知の列の見出し（出力時に末尾へ戻す）
};

struct ProjectSettings {                      // CSVに入らない設定。8.3 のJSONと対応
  std::string id, name, key;                  // key は新規課題の採番用（"DEV"）
  std::string baseDate;                       // 基準日 "YYYY-MM-DD"
  std::string holidays;                       // 休業日。1行に1日。区切りは空白・カンマ・セミコロン・読点
  std::string scenario = "標準";
  struct { bool on=false; int cap=1; } resource;
  nlohmann::json ui;                          // 表示状態（折りたたみ等）。未知のキーも保持
};

struct ProjectData { std::vector<Issue> issues; ProjectMeta meta; ProjectSettings settings; };
```
- **`issues` の並び順が、そのまま表示順**（親の直後に子が並ぶとは限らないが、同じ親の子どうしは配列順 = 表示順）。
- 並び順は編集操作が意味を持つので、`std::vector` の順序を保つ。`id` から課題を引く索引は別に持つ（`Tree`）。

### 4.2 ID・キー
- 新規課題の `id` = 既存の**数字だけの id の最大値 + 1**（なければ 1）。
- `key` = プロジェクトのキーが空でなければ `"{key}-{id}"`、空なら空文字。
- 既存課題の `key` はキーを変更しても変えない。

### 4.3 番号タグと表示名
- `summary` が `^\[([^\]]+)\]\s*(.*)$` に一致する（正規表現を使わず手書きで実装）とき、`tag` = 括弧内、`name` = 残り。一致しなければ `tag` = 空、`name` = `summary` 全体。
- **表示ID（`did`）** = `tag`（空でなければ）→ `key`（空でなければ）→ `id` の順。
- **表示名（`name`）** = `name`（空なら `summary`）。
- 表示IDは課題の参照に使う（親の指定・依存の追加・コマンドパレット）。

### 4.4 種別・状況の判定（大文字小文字は区別しない、ASCIIのみ）
- **Epic**: `type` に `epic` または `エピック` を含む。
- **Milestone**: `type` に `milestone` または `マイルストーン` を含む（ラベルにも同じ語があれば Milestone 扱い。7.2）。
- **状況の分類 `cat`**（`status` の前後の空白を除いて判定）:
  1. `done`: **先頭が** `done` / `closed` / `resolved` / `complete` / `completed` / `完了` / `解決` / `クローズ` / `済` のいずれか。
  2. そうでなく、**どこかに** `progress` / `進行` / `対応中` / `doing` / `working` / `review` / `レビュー` を含む → `prog`。
  3. それ以外（空も含む）→ `todo`。

### 4.5 優先度
`Highest, High, Medium, Low, Lowest`。順位は 0〜4。比較は大文字小文字を区別しない。不明な値は順位 2（Medium 相当）で扱う。

---

## 5. 日付と営業日カレンダー

### 5.1 日付
- 内部は `Day`（1970-01-01 からの日数）。曜日 `dow(n) = (n + 4) % 7`（0=日曜 … 6=土曜。**負の値でも正しく動くこと**）。
- 文字列は `YYYY-MM-DD`（書き出し・設定）。年月日からの変換は検証つき（存在しない日付は失敗。例: 2月30日）。
- 画面表示: `2026/10/19(月)`（長）、`10/19`（短）。曜日は「日月火水木金土」。

### 5.2 休業日
- 設定の `holidays`（テキスト）を、区切り（空白・`,`・`;`・`、`）で分け、各トークンを 6.4 の `parseDate` で読んで集合にする。読めないトークンは無視する。
- 既定値は `golden/holidays.txt`（33日。2026-11-03 〜 2028-03-20 の祝日・年末年始）。新規プロジェクトの `holidays` に入れる。

### 5.3 カレンダー関数（`Calendar` クラス。休業日集合を持つ）
| 関数 | 意味 |
|---|---|
| `isWork(n)` | 月〜金で、休業日でない |
| `workday(n, k)` | `n` から営業日を `k` 日進める（`k<0` は戻る）。`k==0` は `n` をそのまま返す（営業日でなくてもよい）。進める日は営業日だけを数える |
| `lastWork(n)` | `n` 以前で最後の営業日 = `workday(n+1, -1)` |
| `nd(a, b)` | `a`〜`b`（両端含む）の営業日数 |
| `floatDays(a, b)` | `b >= a` なら `nd(a, b) - 1`、そうでなければ `-(nd(b, a) - 1)` |
| `weekdaysBetween(a, b)` | 休業日を**考えずに**月〜金だけを数える（7.2 の期間の読み取りで使う） |

`workday(n, k)` の擬似コード:
```
step = k > 0 ? +1 : -1; c = |k|
while c > 0: n += step; if isWork(n): c -= 1
return n
```
（`k == 0` は何もせず `n`。`workday(n,1)` は「n の次の営業日」、`n` が週末でも動く）

---

## 6. CSV入出力

JS版 `csvio.js` と同じ結果になること。**`golden/*.roundtrip.csv`、`golden/*.jira.csv` と1バイトも違わないこと**（第12章）。

### 6.1 文字コード
- 読み込み: UTF-8（BOMがあれば取り除く）。UTF-8として不正なら **Shift_JIS（CP932）**として読み直す。それも不可なら不正バイトを `U+FFFD` にして読む。
- 書き出し: **BOMなしUTF-8**、改行は `\r\n`、末尾にも `\r\n`。
- 区切り文字: 読み込み時は先頭行で自動判定。`\t` の数が `,` より多ければタブ、`;` の数が `,` より多ければセミコロン、そうでなければ `,`。書き出しは常に `,`。

### 6.2 字句解析（`parse`）
- 引用符 `"` で囲まれたセルの中では、区切り文字・改行をそのまま含められる。`""` は `"` 1文字。
- 改行は `\n`、`\r`、`\r\n` のいずれも行末。
- すべてのセルが空白だけの行は**捨てる**。最初に残った行が見出し、以降がデータ行。
- 書き出し（`stringify`）: セルに `"`・`,`・`\r`・`\n` のいずれかが含まれたら、全体を `"…"` で囲み、中の `"` を `""` にする。

### 6.3 見出しの対応づけ
見出しは**正規化**して比較する: 小文字化し、空白（全角空白 U+3000 を含む）と `_` の連続を1つの半角空白にし、前後を削る。

| 項目 | 一致する見出し（正規化後）。複数列あれば左から最初に値のある列 |
|---|---|
| id | `issue id`, `id`, `課題id` |
| key | `issue key`, `key`, `課題キー`, `キー` |
| type | `issue type`, `issuetype`, `type`, `課題タイプ` |
| summary（必須） | `summary`, `件名`, `要約` |
| parent | `parent`, `parent id`, `親`, `親課題` |
| epicLink | `epic link`（`custom field (epic link)` も可）, `エピックリンク` |
| epicName | 「`epic name`」を**含む**列, `エピック名` |
| prio | `priority`, `優先度` |
| status | `status`, `ステータス` |
| assignee | `assignee`, `担当者`, `担当` |
| notBefore | `earliest start`, `着手可能日`, `start no earlier than` |
| comment（複数列可） | `comment`, `コメント` |
| start | 「`start date`」を**含む**列, `開始日` |
| due | `due date`, `due`, `期限日`, `期限` |
| deadline | `deadline`, `目標期限`, `逆算期限` |
| resolved | `resolved`, `解決日`, `resolution date` |
| est | `original estimate`, `original time estimate`, `元の見積り`, `初期見積り` |
| remW | `remaining (weeks)`, `残り(週)`, `残り（週）` |
| remE | `remaining estimate`, `残りの見積り` |
| labels（複数列可） | `labels`, `ラベル` |
| desc | `description`, `説明` |
| blocks（送出・複数列可） | `outward issue link` で始まり `block` を含む列、`外向き` で始まり `block` を含む列 |
| blocks（受入・複数列可） | `inward issue link` で始まり `block` を含む列、`内向き` で始まり `block` を含む列 |
| 期間（週） | **正規化前の見出し**が `Duration (weeks)` / `期間(週)` / `期間（週）`（大文字小文字不問）のあとに、任意で `@シナリオ名` が付く形。`@` なしが標準、`@名前` がシナリオ |

- 上のどれにも当たらない列は**未知の列**。見出しと各行の値を `extraHeaders` / `extra` に保持し、書き出しで末尾に戻す。
  - 未知の列のうち、`outward issue link` / `inward issue link` / `外向き` / `内向き` で始まり `block` を含まない列（`Outward issue link (Relates)` など）は**関連リンク**として詳細パネルの「依存関係」タブに種類（見出しの括弧の中）と向きごとに出す（`core/include/roadmap/other_links.hpp`）。セルの値は課題キーか Issue Id で、見つかれば押してその課題へ移動し、見つからなければ文字のまま出す。追加・削除は未知の列のセルを書き換えるだけで、列の名前・並びは変えない（追加で新しい列が要るときだけ末尾に足す）。**日程の計算・依存の検査には使わない。**
- `Summary` 列がなければ取り込みを失敗させる（「`Summary` 列が見つかりません。1行目に列名があるCSVを読み込んでください。」）。

### 6.4 値の読み取り
**`parseDate(s)`**（前後の空白を除く。空なら「なし」）。次のいずれかで**先頭から**一致したら採用（後ろに続く時刻などは無視）:
1. `YYYY[-/.年]M[-/.月]D`（月・日は1〜2桁）
2. `D[/- ]Mon[/- ,.]*YY(YY)`（月名は英語3〜9文字、先頭3文字で判定。年が2桁なら +2000）
3. `Mon[.] D[,] YYYY`

存在しない日付（例 2026-02-30）は「なし」。

**`parseDur(s, plainIsWeeks)` → 週**
- 数字だけ（小数可）: `plainIsWeeks` なら週そのまま。そうでなければ、600 以上は**秒**とみなして `n / (8 * 3600 * 5)` 週、600 未満は週そのまま。
- 単位つき（複数連結可。`1w 2d 4h` 等）: `w`/`週`/`週間` = 1週、`d`/`日` = 1/5 週、`h`/`時間` = 1/40 週、`m`/`分` = 1/2400 週。合計する。単位が一つも見つからなければ「なし」。

**`parseDT(s)` → `YYYY-MM-DDTHH:MM`**: 日付部を `parseDate`、時刻部を `H:MM` + 任意の `AM`/`PM`。時刻がなければ `00:00`。日付が読めなければ空文字。

**コメント**: 1セル = `日時;投稿者;本文`（本文に `;` を含んでよい）。日時が `parseDate` で読めるときだけこの形と解釈し、そうでなければ全体を本文とする。書き出しの日時は `19/Oct/26 10:30 AM` 形式（`DD/Mon/YY hh:mm AM|PM`）。

### 6.5 取り込み（`importCsv`）の手順
1. 各データ行から `Issue` を作る。`Summary` が空の行は読み飛ばす（件数を警告に）。
   - `type` の既定は `Task`。`desc` は最初の `desc` 列の値の末尾空白を削る（`desc` 列がなければ空）。
   - `remW` = `remaining (weeks)` 列（plainIsWeeks=true）→ なければ `remaining estimate` 列（plainIsWeeks=false）。`estW` = `est` 列（false）。
   - `durW` = 標準の期間列のうち最初に読めた値。`alt[シナリオ名]` = そのシナリオ列の値。
2. **id の確定**: 数字だけの id の最大値を求め、**空または重複**の id には `最大値+1` から順に新しい数字を振る（重複は件数を警告に）。元の id は参照解決のために覚える。
3. **参照の索引**: 元の id・key・新しい id → 課題（先に出たものを優先）。Epic 種別の課題は、`summary` と `epicName` でも引けるようにする。
4. **親の解決**: `Parent` 列 → なければ `Epic Link` 列の順に引く。見つかり、かつ自分自身でなければ採用。見つからなければ警告に「`{件名の先頭20文字} → 親 {値}`」を追加。
5. **親の循環を切る**: 各課題から親をたどり、同じ課題に戻ったら、その課題の親を外し警告する。
6. **依存**: 送出列「A が B をブロック」→ `A.blocks` に B。受入列「A が B にブロックされる」→ `B.blocks` に A。自分自身・重複は無視。解決できない参照は警告。
7. 日付列に読めない値があれば、列ごとに件数を警告する。
8. 戻り値: `issues`、`meta`（`hasDeadlineCol` = Deadline列があるか、`scenarios`、`extraHeaders`）、`warns`。

警告の文言は `reference-js/web/js/lib/csvio.js` の文字列と一致させる（画面にそのまま出る）。

### 6.6 書き出し（`exportCsv`）
オプション: `planned`（JIRA用か）、`plan(id)`（計画日と期間）、`ownDeadline(id)`。

**行の順序**: 親の直後に子（深さ優先）。最上位は `issues` 配列の順。

**列の順序と内容**（上から。条件つきの列は、条件を満たすときだけ出力）:

| 列 | 出力する条件 | 値 |
|---|---|---|
| `Issue Id` | 常に | `id` |
| `Issue key` | どれかの課題に `key` がある | `key` |
| `Issue Type` | 常に | JIRA用で変換されたとき（6.7）はその種別、そうでなければ `type` |
| `Summary` | 常に | `summary` |
| `Parent` | 常に | JIRA用: 6.7 の実効の親。それ以外: 親の `id`（存在すれば） |
| `Epic Name` | 常に | Epic なら `epicName`（空なら `summary`）、そうでなければ空 |
| `Epic Link` | 常に | 親が Epic で自分が Epic でなければ、その Epic の Epic Name |
| `Priority` | 常に | `prio` |
| `Status` | 常に | `status` |
| `Assignee` | どれかに担当者がある | `assignee` |
| `Start date` | 常に | JIRA用: `plan(id).start`（計画の開始。親は配下の最早）。なければ `start`。形式 `YYYY-MM-DD` |
| `Due date` | 常に | JIRA用: `plan(id).finish`。なければ `due` |
| `Resolved` | どれかに `resolved` がある | `resolved` |
| `Original estimate` | 常に | JIRA用: 葉なら `plan.weeks` を `fmtEst`、親は空。それ以外: `estW` を `fmtEst` |
| `Labels` ×N | N = max(1, 課題ごとのラベル数の最大)。JIRA用でMilestoneにラベル `Milestone` を足す場合は +1 | i番目のラベル |
| `Description` | 常に | JIRA用で元の階層があれば `[元の階層] 上位 > 上位\n` + `desc`。なければ `desc` |
| `Outward issue link (Blocks)` ×M | M = max(1, `blocks` 数の最大) | i番目の `blocks` |
| `Comment` ×K | K = コメント数の最大（0なら列なし） | `fmtComment` |
| `Earliest start` | どれかに `notBefore` がある | `notBefore` |
| `Deadline` | `meta.hasDeadlineCol` または JIRA用 または どれかに `deadline` がある | JIRA用: `ownDeadline(id)`。それ以外: `deadline` |
| `Remaining (weeks)` | どれかに `remW` がある | `fmtNum(remW)` |
| `Duration (weeks)` | 常に | JIRA用: 葉なら `plan.baseWeeks`（シナリオに関係しない基本の週）、親は空。それ以外: `durW` を `fmtNum` |
| `Duration (weeks) @{シナリオ名}` ×シナリオ数 | シナリオごと | `alt[名前]` を `fmtNum` |
| 未知の列 | `extraHeaders` ごと | `extra[i]` |

シナリオ名の並びは `meta.scenarios` の順、続いて課題の `alt` に現れた未登録の名前。

**数値・日付の書式**
- `fmtDate(n)`: `YYYY-MM-DD`、なしは空。
- `fmtNum(w)`: `round(w*100)/100` を、**末尾の0を付けず**に10進表記（`1`、`0.2`、`2.5`）。指数表記にしない。
- `fmtEst(w)`: `w <= 0` または なし → 空。`w` が整数 → `{w}w`。`w*5`（小数2桁に丸め）が整数 → `{日数}d`。それ以外 → `{max(1, round(w*40))}h`。
- `round` は **四捨五入（0.5は上へ）**。`std::round` ではなく `floor(x + 0.5)` を使う（負の値は現れない前提）。

### 6.7 JIRA用の階層の畳み込み（`planned = true` のときだけ）
JIRAの階層は「Epic > タスク（Story・Bug）> サブタスク」の3段まで。書き出し時だけ次の規則で変換する。**元の課題データは変えない。**

各課題 `o`（Epic 以外）について:
1. `o` から親をたどり、**Epic でない親が続く間**の鎖を作り、鎖の先頭（Epic の直下にいるか、親なしの課題）を `a` とする。
2. `a == o`（自分がタスク階層）: 実効の親 = 親が Epic ならその Epic、そうでなければ空。種別が Task/Story/Bug 以外なら `Task` に変える。
3. `a != o`（それより深い）: 実効の親 = `a`、種別 = `Sub-task`。`o` と `a` の**間にある課題**が1つ以上あれば、その名前を `>` でつないだ文字列を「元の階層」として説明の先頭に `[元の階層] …` で残す。
4. Epic の実効の親は空。
5. Milestone: JIRAに標準の種別がないため、種別を Task 系に変え（上の規則による）、ラベルに `Milestone` を足す（すでにあれば足さない）。取り込み直すとMilestoneに戻る（ラベルによる判定。4.4）。
- 見積は整数の書式、日付は `yyyy-MM-dd`、ラベルに空白を入れない（空白があれば取り込み側で問題になるため、`tools/jira_check.py` 相当の検査を `rmcli check-jira` として実装する。付録Eの規則）。

---

## 7. スケジュール計算エンジン

JS版 `engine.js` と**同じ結果**になること。`golden/*.json` が正解。ここは擬似コードで示すが、細部は `reference-js/web/js/lib/engine.js` を読むこと。

### 7.1 考え方
- 日程を持つのは**葉だけ**。親は配下の葉から集計した「まとめ行」。
- 親への依存・期限は、その配下のすべての葉に展開して扱う。
- すべて営業日で計算する。1週 = 5営業日。
- 入力: `issues`、`meta`、`Calendar`、基準日、シナリオ名、`resource{on, cap}`。出力: 課題ごとの結果 `Result`。

### 7.2 ツリー構築（`buildTree`）
1. 索引、親子、`roots`（親がない、または親が存在しない課題）、`kids`、`parentOf`。親が自分自身なら親なしとして扱う。
2. **親ループ検出**: 各課題から親をたどって同じ課題に戻ったら、全体をエラーにする（「親子関係がループしています: {表示ID}」）。
3. 深さ優先で `dfs` 順、`depth`、`rootOf`、`leaves[id]`（葉なら自分だけ）を作る。
4. **期間（週）** `baseWeeks[葉]`: `durW` → `estW` → （`start`と`due`が両方あり `due >= start`）なら `max(1, weekdaysBetween(start, due)) / 5` → どれもなければ **1週**（警告: 「期間が読み取れず1週と仮置き: {表示IDを最大10件}（ほか N件）」）。
5. `kind[葉]` = `"ms"`（期間が0、または 種別かラベルが Milestone）、それ以外 `"task"`。
6. **依存の展開**: 課題 `a` の `blocks` の各 `b` について、`leaves[a]` の各 `la` と `leaves[b]` の各 `lb` の組に `la → lb`（`preds[lb]` に `la`、`succs[la]` に `lb`）。`la == lb`（親子・同じ配下の依存）は無視し、警告「親子の間（または同じ配下）の依存関係は無視しました」。
7. **トポロジカル順** `leafOrder`: 葉を `dfs` 順で並べ、入次数0から開始。**同じ段階で取り出せる葉が複数あるときは、`dfs` 順の位置が小さいものを先に**取る（毎回ソート）。取り切れなければエラー（「先行・後続の関係がループしています: {表示ID最大6件}（ほか）」）。
8. **期限**:
   - `ownDl[id]`: `meta.hasDeadlineCol` なら `deadline`。なければ、葉で後続がないときだけ `due`。
   - `dl[葉]` = 自分の `ownDl` と、**祖先の `ownDl`** のうち最小（祖先の継承は `hasDeadlineCol` のときだけ）。
   - 警告: 期限が1件もない → 「期限（Deadline）が1件も無いため、余裕は0として表示します。期限を入れると逆算できます」。ある場合に、後続がなく期限もない未完了の葉があれば「期限が無い末端の課題{N}件は、最も遅い期限に合わせています」。
9. `scenarios` = `["標準"]` + `meta.scenarios` + 課題の `alt` に現れた名前（重複なし）。

### 7.3 最早（順方向）
定数: `base` = 基準日の `Day`、`first = workday(base - 1, 1)`（基準日以降で最初の営業日）。

```
weeks(id)  = (scn != "標準" && alt[scn] あり) ? alt[scn] : baseWeeks[id]
days(id)   = max(round(weeks(id) * 5), 1)
zw(id)     = kind[id]=="ms" && weeks(id)==0          // 期間0のマイルストーン: 営業日を消費しない

startOf(id, lookup):
  s = first
  if notBefore あり: s = max(s, isWork(notBefore) ? notBefore : workday(notBefore, 1))
  if preds あり:   pm = max(lookup[p].finish for p in preds)
                   s = max(s, zw(id) ? pm : workday(pm, 1))
  return s

for id in leafOrder:
  if cat==done:  finish = resolved ?? due ?? workday(first, -1);  start = start ?? finish;  if start > finish: start = finish;  pinned
  elif cat==prog && start あり && start <= base:
       start = o.start
       finish = remW あり ? workday(first, max(round(remW*5),1) - 1)
                          : max(workday(start, days-1), first)
       pinned
  else: start = startOf(id, asap);  finish = zw ? start : workday(start, days-1)
  asap[id] = free[id] = [start, finish]
```
`pinned` は「実績で固定された」印（ドラッグで動かせない）。

### 7.4 最遅（逆方向）
```
projEnd = max(dl[id] for id in leafOrder if dl あり && cat != done)   // なければ なし
for id in reverse(leafOrder):
  if cat==done: alap[id] = asap[id]; continue
  c = []
  if dl[id] あり: c.push(lastWork(dl[id]))
  for s in succs[id] if cat[s] != done: c.push(zw(s) ? alap[s].start : workday(alap[s].start, -1))
  f = c が空でなければ min(c)
      そうでなければ projEnd あり ? lastWork(projEnd) : asap[id].finish
  alap[id] = [ zw(id) ? f : workday(f, -(days-1)), f ]
```

### 7.5 担当者ごとの同時作業の制限（`resource.on` のとき）
最早（7.3）の結果を、担当者ごとの**同時に持てる件数 `cap`**を超えないよう、後ろへずらす。
- `who(id)` = `assignee` の前後空白を除いたもの。`useRes(id)` = `who` が空でなく、`kind != ms` かつ `cat != done`。
- `pinned` の葉は確定済みとして、その期間を担当者の占有（`busy[who]`）に入れる。
- 未確定の葉のうち、**先行がすべて確定済み**のもの（eligible）から、**最遅開始が早い順**（同じなら `leafOrder` の位置が小さい順）に1つずつ選ぶ:
  - `est = startOf(pick, asap)`（確定済みの先行の最早完了から）。
  - `useRes` なら `st = fit(who, est, days)`、占有に `[st, workday(st, days-1)]` を追加。そうでなければ `st = est`。
  - `asap[pick] = [st, zw ? st : workday(st, days-1)]`、確定にする。
- `fit(person, est, d)`: `t = est` から、`[t, workday(t,d-1)]` と重なる占有が `cap` 件未満になる最初の `t` を返す。重なりが `cap` 件以上なら、その時点で同時に `cap` 件以上になる占有群のうち**最も早く終わるもの**の翌営業日へ `t` を進めて繰り返す（最大5000回で打ち切り）。細部は `engine.js` の `fit` を正とする。
- 全体が確定しないまま eligible が空になったら、残りは最早のまま打ち切る（循環は 7.2 で弾かれているので通常起きない）。
- `waited = !done && asap.start > free.start`（制限で待たされた）。

### 7.6 余裕と親のまとめ
```
葉: float = done ? 0 : floatDays(asap.finish, alap.finish)
    status = done ? "完了" : float<0 ? "遅れ" : float<=10 ? "余裕小" : "OK"
親（dfs の逆順）:
    L = leaves[id], act = L のうち未完了
    done = act が空;  nDone = |L| - |act|
    prog = act が空でなく (nDone>0 || act に prog がある)
    asapStart = min(L.asapStart);  asapFinish = max(L.asapFinish)
    alapStart = min(L.alapStart);  alapFinish = max(L.alapFinish)
    float = act が空でなければ min(act.float)、空なら 0
    status = act が空でなければ （上と同じ判定）、空なら "完了"
    n = |L|, nDone
```
`Result`（葉・親共通）: `leaf, done, prog, weeks, days, asapStart, asapFinish, alapStart, alapFinish, float, status, n, nDone, freeStart, waited, pinned`。

### 7.7 担当者の負荷（`workload`）
未完了・Milestone でない・担当者ありの葉を担当者ごとに集計: `n`（件数）、`weeks`（合計）、`last`（最後の完了予定 = max asapFinish）、`wait`（待たされた件数）、`peak`（**制限なしの開始日 `freeStart`** を基準に、同じ担当者の作業が同時に重なる最大件数）。`weeks` の降順で並べる。細部は `engine.js` の `workload`。

### 7.8 画面で使う集計（`summary`）
葉（`leafOrder`）について、完了 `done`、未完了のうち `float < 0` を `late`、`0 <= float <= 10` を `tight`。総数 `total`。

### 7.9 エラー・警告の扱い
- ツリー構築のエラー（親ループ、依存ループ）は、画面上部の赤いバナーに出し、計算結果を持たない。直前の有効な状態へ戻す編集は自動で行う（9.1 の `mutate`）。
- 警告は「読み込み時の注意」ダイアログとツールバーのボタン（件数つき）に出す。

---

## 8. 保存とデータフォルダ

### 8.1 場所
- 既定: **実行ファイルと同じフォルダの `data/projects/`**。なければ作る。書き込めない場所（`Program Files` 等）なら `%LOCALAPPDATA%\ProjectRoadmap\projects\` を使い、画面に保存先を明示する。
- 上書き: 起動引数 `--data <dir>` > 環境変数 `ROADMAP_DATA` > 既定。
- 設定ファイル（8.6）は、`--data` の1つ上のフォルダではなく、**実行ファイルと同じフォルダの `roadmap_prefs.json`**（書き込めなければ `%LOCALAPPDATA%\ProjectRoadmap\`）。

### 8.2 ファイル構成
**フォルダ ＝ プロジェクト**。1つのプロジェクトの CSV・設定・添付・バックアップが1つのフォルダに収まる（持ち運び・zip での共有・バックアップ・削除がフォルダ単位でできる）。
```
<data>/
├─ プロジェクトA/                    プロジェクトのフォルダ（名前 = stem。プロジェクト名を変えても変えない）
│    ├─ project.csv                  原本（6章の「app形式」の書き出し。固定名）
│    ├─ project.settings.json        設定（8.3）
│    ├─ 添付/                        課題の添付（#19 で実装。置き場所の規約は下）
│    │    └─ <Issue Id>/ファイル…
│    └─ バックアップ/                このプロジェクトの分だけ（30個まで）
│         └─ プロジェクトA.20261019-181500.csv
└─ _削除済み/                        削除したプロジェクト（フォルダごと）
     └─ プロジェクトA.20261019-181500/    （中身は上と同じ。戻すときもフォルダごと）
```
- **フォルダ名（stem）**: プロジェクト名から、`\ / : * ? " < > |` と制御文字を `_` に置き換え、前後の `.` と空白を削り、80文字（UTF-16 の単位）以内にする。空なら `project`。同名（大文字小文字を無視）があれば `-2`、`-3` … を付ける。`_削除済み`・`_バックアップ` は予約された名前で、プロジェクトには使わない（`-2` を付ける）。**プロジェクト名を変えてもフォルダ名は変えない**（設定に stem を持たず、対応は 8.5 の規則で行う）。
- **CSV の名前は固定**（`project.csv`）。フォルダ名を見れば何のプロジェクトか分かり、名前の変更に強い。
- **添付の置き場所（規約）**: `<プロジェクトのフォルダ>/添付/<Issue Id>/<ファイル>`。フォルダ名は CSV の `Issue Id`（表示 ID は階層の移動で、件名は編集で変わるので使わない）。`Issue Id` にフォルダ名に使えない文字があれば同じ規則で `_` にし、80文字以内にする。**パスの長さ**: Windows の既定（260文字）を超えないよう、ファイルを置くパス全体（フォルダ + `/` + ファイル名）を **240文字（UTF-16 の単位）以内**にする（一時ファイル `.tmpNNNNNN` の分の余裕）。長ければファイル名を、拡張子を残して切る。フォルダだけで収まらなければ置けないとして知らせる。実装は `core` の `paths::attachmentDir` / `paths::fitFileName`（添付そのものの実装は #19）。
- **互換読み（以前の平置き）**: `<data>/プロジェクト名.csv` と `<data>/プロジェクト名.settings.json`（`_バックアップ/` は全プロジェクト共通）も、今まで通り読み書きできる。**自動では動かさない**（8.8）。
- **入口として読めるもの**: ①`.csv` だけあって `.settings.json` がないファイル（名前 = stem、設定は既定値）、②`project.csv` がなく、別名の CSV だけを置いたフォルダ（名前順で先頭の CSV を読む。名前 = フォルダ名。取り込んだ時点で、そのフォルダに `project.csv` と設定を作る。元の CSV には触れない）。どちらも1つのプロジェクトとして読む。
- 走査の対象は `<data>` 直下の `*.csv` と、1段下のフォルダ。`_削除済み`・`_バックアップ`・名前が `.` で始まるフォルダ、CSV のないフォルダは見ない。プロジェクトのフォルダの中の `添付/` などは見ない。

### 8.3 `settings.json`
キーは付録Bを参照。**未知のキーは捨てずに保持して書き戻す。** JS版は使わなくなったため、JS版の `data/projects` との相互読み書きは求めない（形式は JS版の設定と同じまま。互換が必要になるまで変えない）。

### 8.4 書き込み
- 編集のたびに **400ms 待って**（連続編集をまとめる）、変更があったプロジェクトだけ書く。内容（CSV + 設定JSON）が前回書いた内容と同じなら書かない。
- **原子的に書く**: 同じフォルダの一時ファイルに書いて閉じ、`rename`（Windowsでは `MoveFileExW` + `MOVEFILE_REPLACE_EXISTING`）で置き換える。書き込み途中のクラッシュで壊れたCSVを残さない。
- **バックアップ**: そのプロセスで**最初に上書きするとき**だけ、既存のCSVをそのプロジェクトのフォルダの `バックアップ/` にコピーする（古い順に残し、容量の上限は設けない。ただし**プロジェクトごとに**30個を超えたら古いものから消す）。平置きのプロジェクトは、従来通り `<data>/_バックアップ/` に（全プロジェクト合わせて30個）。
- 失敗したら（他のアプリがファイルを開いているなど）、画面上部に「保存できません: {理由}」を出し、**2秒→5秒→10秒の間隔で再試行**する。成功したらバナーを消す。ユーザーが「再試行」も押せる。終了時に未保存があれば確認する。
- 保存状態を常にツールバーに出す: 「保存済み 18:36」「保存中…」「保存できません」。

### 8.5 起動時と外部変更の取り込み
データフォルダの直下の `*.csv`（平置き）と、1段下のフォルダの `project.csv`（8.2）を一覧し、更新時刻（csv と settings の新しい方）を `mtime` とする。アプリはプロジェクトごとに「**最後に自分が書いた/読んだ時点の `mtime`**」をメモリに持つ。

起動時と、**ウィンドウがアクティブに戻ったとき**（3秒以上経っていれば）、次を行う:
1. 既知のプロジェクトは stem（フォルダ名。平置きではファイル名の stem。平置きとフォルダは別のものとして扱う）で対応づける。新しいものは `settings.json` の `id` で既知のプロジェクトと対応づけ（フォルダ名を手で変えても同じプロジェクトとして追う）、どちらにも当たらなければ**新しいプロジェクト**として追加する（`id` が既存と衝突したら新しい id を振る）。
2. 対応づいたプロジェクトについて:
   - ファイルの `mtime` が記録より新しく、**未保存の編集がない** → ファイルから読み直す（Undo履歴は消す）。
   - 未保存の編集があり、ファイルは変わっていない → 書く。
   - 両方変わっている → ファイル側の内容を**別プロジェクト「{名前}（フォルダの版）」**として追加し、手元の内容をファイルへ書く（どちらも失わない）。
3. 読めない（不正な）CSVは**取り込まず、上書きもせず**、通知に「読めないCSV: {名前}」を出す。
4. 削除: アプリで削除したプロジェクトは、プロジェクトのフォルダを**フォルダごと**（添付・バックアップも）`_削除済み/<stem>.<日時>/` へ**移す**（同じ名前があれば `-2`）。平置きのプロジェクトは、従来通り CSV と設定のファイルを `_削除済み/` へ移す。戻すときもフォルダごと（`_削除済み/` のフォルダをデータフォルダへ移す。名前が使われていれば `-2`。`Workspace::restoreDeleted`）。

任意（推奨）: `ReadDirectoryChangesW`（または `FindFirstChangeNotificationW`）でフォルダを監視し、アクティブ時でなくても外部変更を拾う。**サブフォルダも監視する**（`bWatchSubtree = TRUE`。Excel で `<プロジェクト>/project.csv` を直したら反映される）。

### 8.6 設定ファイル `roadmap_prefs.json`
```json
{ "theme": "auto", "density": "normal", "me": "自分", "currentProject": "プロジェクト名",
  "templates": [ /* 利用者のテンプレート。形式は付録A */ ], "window": { "x": 100, "y": 100, "w": 1440, "h": 900, "maximized": false },
  "hintDismissed": false }
```
- `theme`: `auto`（OSの設定に従う）/ `light` / `dark`。`density`: `normal` / `compact`。
- 書き込めないときは黙って失敗してよい（次回は既定値）。

### 8.7 初回起動（データフォルダが空のとき）
「はじめに」画面を出す: 「空のプロジェクトを作る」「サンプル（階層デモ）」「CSVを取り込む」。サンプルは実行ファイルに**埋め込む**（`golden/samples/` の2ファイルを資源として同梱）。選んだものだけを `data/projects/` に書く（プロジェクトのフォルダとして。8.2）。何もしなければ何も作らない。

### 8.8 平置きからフォルダ構成への移行
- **自動では動かさない**。平置きのプロジェクトが1つでもあれば、起動時に通知で知らせ、「データフォルダ」ダイアログと「アプリの設定」の保存先に、件数と「フォルダ構成に移す」ボタンを出す。移さなくても、平置きのまま読み書きできる（互換読み）。
- 移すときは、**先に**平置きの全ファイル（CSV と設定）を `<data>/_バックアップ/移行前.<日時>/` へ控える（1つでも控えに失敗したら何も動かさない）。そのあと1プロジェクトずつ、`<stem>/project.csv` と `project.settings.json` を**バイト列を変えずに**写し、読み戻して一致を確かめてから、平置きのファイルを消す。フォルダ名が使われていれば `-2`。設定ファイルがなかったものは、`id` を固定するため設定を作る。写せなかったものは平置きのまま残し、通知に出す。
- 未保存の編集は、移す前に平置きへ書く（失われない）。実装は `Workspace::migrateLegacy`。

### 8.9 添付ファイル（#19）
- **本体**: `<プロジェクトのフォルダ>/添付/<Issue Id>/<ファイル名>`（8.2 の規則。同名は `-2`、`-3` …）。**フォルダ構成のプロジェクトだけ**（平置きには置き場がないので、理由を出して断る）。実装は `core` の `attachments.hpp`（`IFileStore` 経由）。
- **メタ情報**: `project.settings.json` の `"attachments"`（`{ "<Issue Id>": [ { "name", "size", "at", "who", "sha256" } ] }`。空なら書かない）。`sha256` は本体の SHA-256（16進小文字 64 文字）で、追加（ドロップ・ファイル選択・貼り付け・コピー）のときに、書いた本体と同じバイト列（`IFileStore::read` で読んだもの）から `core` の自前実装（`sha256.hpp`。RFC 6234）で計算して持つ。移動は引き継ぐ（本体を読み直さない）。古いメタ情報（`sha256` のないもの）は壊さず空（不明）として扱い、書くときも出さない（既存の設定ファイルは変わらない）。長さ・文字・型の壊れた値は読み飛ばして空にする（項目は消さない）。共有・バックアップ JSON（B.2）には入れない（本体を含まないため）。課題を削除・Undo しても添付は残る（Issue Id が同じなら、戻した課題に付いたまま）。
- **上限**: 1ファイル 50MB・プロジェクト合計 1GB。超えたら理由を出してそのファイルだけ断る。1回のドロップは 500 ファイルまで。パス全体は 240 文字（UTF-16）以内（8.2）。
- **削除**: 本体を `<プロジェクトのフォルダ>/_削除済み/<Issue Id>/<ファイル名>`（同名は `-2`）へ**移す**（消さない）。移せなければメタ情報も残す。添付の追加・削除は元に戻す（Undo）の対象外で、すぐ保存する。
- **ドロップ**（Windows）: エクスプローラーから複数のファイル・フォルダ（中のファイルを全部）。ドロップ先が課題（リストの行・ガントの行・ボードのカード・詳細パネル）なら添付、何もないところなら今まで通り CSV / JSON の取り込み（最初の1つ）。
- **画面**: 詳細パネルの「添付」タブ（一覧: 名前・サイズ・日時・付けた人、開く、削除、フォルダを開く。項目にマウスを載せると、SHA-256 の先頭 12 文字を短縮表示する。「ハッシュをコピー」はない）。行・カードに、添付の数（クリップのアイコン。押すと添付タブ）。そのバッジにマウスを載せる（少し待つ）と、先頭 4 件ぶんのツールチップを出す: 画像は小さなサムネイルを横に並べ、画像でないものは名前だけ、5 件目以降は「ほか N 件」。画像はサムネイルと同じテクスチャ（デコードは 1 フレームに 1 枚まで）を使い、マウスを載せていないときは読まない。開くときは関連付けられたアプリ（`IPlatform::openFile`）。実行ファイル・スクリプト（`.exe` `.bat` `.js` `.lnk` など）は、開く前に警告する。
- **JIRA の Attachment 列**: 取り込みでは未知の列として保持し（同じ見出しが複数でも）、書き出しでそのまま戻す（golden のバイト一致は変えない）。形が `日時;作成者;ファイル名;URL` なら、添付タブに「本体はありません」として表示する（読み取り専用）。
- **移動・コピー・並べ替え**: 添付タブの行をつかんで、別の課題（リストの行・ガントの行・ボードのカード・詳細パネル）の上で離すと**移動**（Ctrl を押していれば**コピー**）。本体を `添付/<元の Issue Id>/` から `添付/<先の Issue Id>/` へ、メタ情報と一緒に動かす（同名は連番。先の末尾に足す）。移動は本体を先に移し、移せなければ何も変えない。コピーは新しいファイルとして上限（1ファイル・合計）を検査し、日時と作成者は付けた時点のもの。自分自身・存在しない課題・本体のない添付・設定 JSON の読み込みを通らない課題 ID（区切り・`..`・予約文字を含むもの）は、ファイルを動かす前に理由を出して断る。同じ課題の添付の一覧の中（スクロールで見える範囲）で離すと並べ替え。何もないところで離す・Esc・ダイアログ／パレット／ポップアップが開いたときは何もしない。実装は `core` の `moveAttachment` / `copyAttachment` / `reorderAttachment`。
- **追加の手段**: 添付タブの「ファイルを選んで追加」（`IPlatform::pickOpenFiles`。複数選択）。課題を 1 つ選んで、テキストがなく画像があるクリップボードから `Ctrl+V` で `貼り付け-<日時>.bmp`（`IPlatform::clipboardImage`。Windows の DIB に BMP のヘッダを足す `bmpFromDib`。BI_RGB と BI_BITFIELDS だけ。RLE などは断る）。テキストがあれば子課題の貼り付け（複数行を選んだ課題の子としてまとめて追加）が先。
- 個人情報を含みうるので、ファイル名・中身はログに出さない。

### 8.10 関連リンク（#60）
添付（8.9。本体をコピーして持つ）と違い、**場所を指すだけ**で何もコピーしない参照。実装は `core` の `links.hpp`（UI・Win32 に依存せず、例外を投げず、`Result` で返す）。
- **持つもの**: `Link { title, target, addedAt }`（表示名・場所・追加した時刻。時刻は UTC のミリ秒で、`IClock` から取る）と、Issue Id → リンクの並び（`LinkMap`）。項目の未知のキーは `extra` に持って書き戻す。
- **種類の判定** `classifyLink(target)`: 文字列だけで決める（存在確認・I/O をしない。ネットワークドライブで固まらないため）。前後の空白・`"` `“”` `<>` の囲み・制御文字（改行を含む）・ゼロ幅の文字を取り除いて正規化する。
  - **Web**: `http://` `https://`（スキームの大文字小文字は問わず小文字にそろえる。URL の空白は `%20`）。ID・パスワード入り（`user:pass@`）は断る。
  - **File / Folder**: `file://`、UNC（`\\server\share\…`）、ドライブ付き（`C:\…` `C:/…`）。末尾が `\` か `/` ならフォルダ、それ以外はファイル（共有のルート `\\server\share` はフォルダ）。
  - **Rejected**（理由つき）: 許可リストにないスキーム（`javascript:` `ms-msdt:` `data:` `vbscript:` `ftp:` `mailto:` など）、デバイスのパス（`\\?\` `\\.\`）、パスに使えない文字（`< > " | * ? :`）、方向を上書きする制御文字、不正な UTF-8、長さの超過（URL 2,048 バイト・パス 1,024 バイト）。
  - **Unknown**: 何の場所か分からない（空・相対パス・ふつうの文章）。Rejected と Unknown は追加も開くこともしない。
- **実行ファイルの判定** `isRiskyTarget(target)`: ファイルの場所だけ。拡張子の表は添付の `isRiskyFile`（8.9）と共通。`file://` は判定と同じ解析（`#` 以降の断片を先に分け、`%XX` を戻して、区切りで成分に分ける。生のドライブ・UNC のパスの `%` はそのままの文字）で名前を取り、末尾の空白と「.」も取る。**親のフォルダ** `linkFolderOf(target)`（「場所（フォルダ）を開く」。`file://` は開ける形のパスに直して返す）、**重複の判定** `sameLinkTarget`（ファイル・フォルダは大文字小文字（アクセント付きの文字も）と区切りを無視、Web は完全一致）、**表示名の既定** `defaultTitle`（Web = ホスト + 末尾の 2 区画、ファイル = ファイル名、フォルダ = 最後のフォルダ名。`%XX` は UTF-8 として正しければ戻す）。
- **操作**: 追加・編集・削除・並べ替え（`linkReorder`。`reorderAttachment` と同じ位置の決め方）・別の課題への移動（時刻を引き継ぐ）とコピー（時刻は「いま」）・貼り付けた各行の追加（`linkAddLines`。20 行まで）。上限は 1 課題 50 件・全体 5,000 件・表示名 200 バイト。同じ課題に同じ場所は追加しない（理由を返す）。Issue Id は設定 JSON に保存して読み直せる名前（`isSafePathComponent`・64 バイトまで）だけ。
- **保存**: `project.settings.json` の `"links"`（`{ "<Issue Id>": [ { "title", "target", "at" } ] }`。空なら書かない）。**CSV は変えない**（JIRA 互換・golden のバイト一致を維持）。**共有・バックアップ JSON（B.2）には入れない**（パスは環境依存）。読み込みでは壊れた値（型・Issue Id・開けない場所・重複・件数超過）を読み飛ばし、表示名・時刻の壊れは直して項目を残し、未知のキーは壊さない。課題を削除・Undo してもリンクは残り、その Issue Id は新しい課題に使い回さない。リンクの追加・削除は元に戻す（Undo）の対象外で、保存はする。
- **開く**: `IPlatform::openLink(target, &error)`（Win32 は `ShellExecuteW`）。UI が `classifyLink` で検査し、Rejected・Unknown は開かず理由を通知する。実行ファイル・スクリプトは先に警告のダイアログ（添付と同じ。確認したら開く）。ファイル・UNC は存在確認をせず、開けなければ理由を通知する。
- **ログ・通知**: URL・パス・表示名は個人情報・社内の機微を含みうるので、ログに出さず、通知にも出さない（件数・行番号・理由だけ。理由にも入力の中身は入れない）。

### 8.11 GitHub の PR・Issue のリンクと、閉じたら完了の提案（#61）
8.10 のリンクのうち、GitHub の PR・Issue を指すものを見分け、状態（open・merged など）を取ってリンクの横に出し、**すべて閉じたら課題を「完了」にする提案**を出す。実装は `core` の `github_ref.hpp`（UI・Win32 に依存せず、例外を投げず、`Result` で返す）。
- **許可する URL** `parseGitHubUrl(target)`: `https://github.com/<owner>/<repo>/pull/<n>` と `/issues/<n>` だけ。スキームとホストの大文字小文字は問わず、パスの語（`pull` `issues`）は小文字だけ。末尾の `/`・`?…`・`#…`・下位パス（`/files` `/commits` `/checks` など）は許容して**正規化**する（`https://github.com/<owner>/<repo>/pull|issues/<n>`。これが状態の鍵と `gh` に渡す引数になる）。断るもの: `http://`・`www.github.com`・`github.com.evil.example` などの別ホスト・ユーザー情報（`user@`）・ポート・GitHub Enterprise のホスト・空白／制御文字／ASCII 以外／`\`・パスの中の `%XX`（エンコードされた区切りや `..`）・`..` を含む区画・空の区画・owner／repo が英数字と `-` `_` `.` 以外または 100 バイト超・番号が 0・先頭 0・10 桁以上・数字以外・URL が 2,048 バイト超。理由に入力の中身は入れない。
- **種類** `LinkKind::GitHubPull` / `GitHubIssue`（`classifyLink` が `https://` の Web を `parseGitHubUrl` に通して決める。`linkKindOpenable` は真で、Web として開けることは今までどおり。保存・重複の判定・表示名も Web と同じ）。
- **状態** `RemoteState`: `Open` / `Draft` / `Merged` / `ClosedUnmerged`（マージせずに閉じた PR）/ `ClosedCompleted` / `ClosedNotPlanned`（Issue）/ `Unknown`。画面の表示は `open` `draft` `merged` `closed（マージなし）` `closed（完了）` `closed（計画外）`（色だけに頼らず文字で出す）。`RemoteInfo` は状態・閉じた日時（UTC の `YYYY-MM-DDTHH:MM:SSZ`。PR は `mergedAt` があればそれ）・Unknown の理由を持つ。
- **`gh` の JSON** `parseGhPrJson` / `parseGhIssueJson`: PR は `state`（OPEN / CLOSED / MERGED）・`isDraft`・`mergedAt`・`closedAt`、Issue は `state`（OPEN / CLOSED）・`stateReason`（COMPLETED / NOT_PLANNED / REOPENED）・`closedAt`。`gh` が未設定の日時として出すゼロ値（`0001-01-01T00:00:00Z`）は「なし」。壊れた JSON・オブジェクトでない・欠け・未知の値・日時の形式違い・64KB 超は、**Unknown と理由**を返す（閉じた状態で日付が取れない／理由が空・`DUPLICATE` などの未対応の値も Unknown）。
- **取得の窓口** `IRemoteStatus::fetch(GitHubRef) → Result<RemoteInfo>`（時計・ファイルと同じ流儀。実装は差し替えられる。`FakeRemoteStatus` はテスト用の固定の応答）。コントローラが別のスレッドで**1 件ずつ**実行し、結果を `tick()`（毎フレーム）で拾う。同じ PR・Issue は 1 回の確認で 1 回だけ取る。確認した結果は**メモリ上のみで保存しない**（起動のたびに「未確認」。起動時の自動確認はしない。押したときだけ）。
- **Windows の実装**（`app/platform/win32_gh.*`）: `PATH` の絶対パスのフォルダだけから `gh.exe` を `SearchPathW` で探し（カレントフォルダは見ない）、`CreateProcessW` で**直接起動**する（`cmd /c` などのシェルを通さない）。引数は**検証済みの URL を 1 つの引数として**渡す（`gh pr view <URL> --json state,isDraft,mergedAt,closedAt` / `gh issue view <URL> --json state,stateReason,closedAt`。`ghArguments` が `validateGitHubRef` を通らない参照を断り、`windowsCommandLine` が引用符で包む）。標準入力・標準エラーは NUL、標準出力は 64KB まで読み、**15 秒**で打ち切ってプロセス（ジョブ）を終了する。中断（アプリの終了・窓口の差し替え）は、取得 1 回ごとの `CancelToken`（**スレッドを起こす前に作って** `fetch` に渡す）で伝える。`PATH` の探索の前後と待ち時間の間に token を見るので、`fetch` に入る前の中断も取りこぼさない。`gh` がない・終了コードが 0 でない（未ログインなど）は理由つきの失敗。URL・リポジトリ名・タイトルは理由にもログにも入れない。トークン方式（REST API）は後回し。
- **完了の提案** `proposeCompletion(issue, links, states, utcOffsetMinutes)`: その課題の **GitHub のリンクがすべて閉じている**ときだけ提案する（GitHub 以外のリンクは無視。GitHub のリンクが 1 つもない・1 件でも open／draft／Unknown／未確認（取得に失敗したもの、**確認中・順番待ちのもの**を含む）があれば提案しない。確認中のリンクは、以前の結果が残っていても、新しい結果が届くまで未確認として扱う（提案の生成・「完了にする」を押したときの再検証・自動の完了のすべて）。確認を始めたら、その課題の古い提案の通知は取り下げる。すでに完了（`Resolved` が入っている・状況が完了）の課題にも提案しない）。**完了日**は各リンクの閉じた日時（UTC）を、**その時刻に適用される時間帯の規則**で換算した暦の日のうち最も遅い日（今日より先なら今日）。時間帯の規則は OS が持つので、core は `UtcOffsetFn`（その時刻の UTC からの差・分）を受け取るだけで、プラットフォームが注入する（Windows は `SystemTimeToTzSpecificLocalTime`。夏時間のある時間帯で、過去の日時を「いま」の差で一律に換算して日付がずれることを避ける）。マージなしで閉じた PR・計画外で閉じた Issue を含むときは、提案の文に**注意**（「マージなしで閉じられたものを含みます」「計画外で閉じられたものを含みます」）を添える。
- **通知とコマンド**: 確認が終わって提案が成立したら、通知「#表示ID 『件名の先頭 30 文字』のリンクがすべて閉じています。完了にしますか？（注意）」と「**完了にする**」ボタン（確認ダイアログにはしない。押すまで課題は変わらない）。押すと既存の編集（`Session::completeIssue`。状況を `Done` にして `Resolved` に完了日を入れる。`beginBatch` で **Undo 1 回**）で完了にし、「元に戻す」つきの通知を出す。押す前にプロジェクトが切り替わった・課題がない・リンクの状態が変わって提案が成立しなくなったときは、何も変えず理由を通知する。失敗した確認は理由（件数と理由。URL なし）を通知し、課題は変えない。アプリの設定「GitHub のリンクが閉じられたら自動で完了にする」（`roadmap_prefs.json` の `autoCompleteGitHub`。既定オフ）がオンなら、確認が終わった時点で自動で完了にし、「元に戻す」つきの通知を出す。
- **CSV は変えない**（JIRA 互換・golden のバイト一致を維持）。`project.settings.json` にも状態は入らない（リンクの `title`・`target`・`at` のまま）。

---

## 9. 編集操作と元に戻す

JS版 `core.js` の動作と同じにする。`reference-js/src/core/core.js` を読むこと。

### 9.1 `mutate` と Undo/Redo
- すべての編集は `mutate(fn, post?)` を通す: ①スナップショット（`issues` + `meta` + シナリオ名）を取る → ②`fn` で変更 → ③ツリー構築と計算 → エラーなら**スナップショットに戻し**、通知「その編集はできません: {理由}」を出して false を返す → ④（`post` があれば実行して再計算）→ ⑤スナップショットと変わっていれば Undo に積む（**最大100件**、Redoを消す）→ ⑥保存を予約。
- `undo` / `redo` は、スナップショットを入れ替えて再計算し、保存を予約する。**プロジェクトを切り替える・ファイルから読み直す**ときは履歴を消す。
- 設定の変更（ズーム、折りたたみ、基準日、休業日、リソース設定、シナリオの切替・追加）は **Undoの対象外**。ただし保存はする。

### 9.2 課題の操作（すべて `mutate`）
| 操作 | 動作 |
|---|---|
| 追加 `addIssue(parent, after, name)` | 新しい課題を作る（4.2）。`type` は、親なし → `Epic`、親が Epic → `Task`、それ以外 → `Sub-task`。既定: `prio=Medium, status=To Do, durW=1`、件名の既定は「新しい課題」。`after` があればその兄弟の**配下の最後の後ろ**へ、なければ配列末尾へ。親があれば親を展開する |
| 削除 `deleteIssues(ids)` | 子孫も含めて消し、残った課題の `blocks` から消えた id を取り除く。通知「N件を削除しました」＋「元に戻す」 |
| 上へ/下へ `moveIssue(id, ±1)` | **同じ親の兄弟**と配列上の位置を入れ替える。端なら何もしない |
| 親の付け替え `reparent(id, newParent)` | 配列から抜いて**末尾**に追加し、`parent` を変える。新しい親を展開する |
| インデント `indent` | 直前の兄弟の子にする（直前の兄弟がなければ何もしない） |
| アウトデント `outdent` | 親の兄弟にし、**元の親の直後**へ置く |
| 移動 `relocate(id, target, where)` | `before` / `after` / `into`。自分や子孫は対象にできない。`after` は対象の**配下の最後の後ろ**。`into` は末尾に追加して対象を展開 |
| 項目の変更 `setField` | 単純な代入 |
| 状況 `setStatus(id, st)` | `status` を変え、分類が `done` で `resolved` がなければ `resolved = min(今日, 基準日)`、`prog` で `start` がなければ `start = min(今日, 基準日)` |
| 番号 `setTag(id, tag)` | `[` `]` を除いて前後の空白を削る。同じ表示IDが他の課題にあれば（大文字小文字不問）拒否して通知。空にするとタグを外す。件名は `[tag] ` + 名前 |
| 件名 `setName` | 番号タグを保ったまま名前だけ変える。空なら「（無題）」 |
| 期間 `setDuration(id, w)` | シナリオが「標準」なら `durW`、そうでなければ `alt[シナリオ]` に書く |
| シナリオ値の削除 | `alt[現在のシナリオ]` を消す |
| 着手可能日 `setEarliest(id, day)` | `notBefore` に設定。**計算後に最早開始が `day` と一致しなければ元に戻し**、通知「その日には動かせません（先行や、同じ担当者の作業が終わるまで待つため）」 |
| 期限 `setDeadline(id, day)` | `meta.hasDeadlineCol` が false なら、先に今の `ownDl`（Due date由来）を各課題の `deadline` へ移して `hasDeadlineCol = true` にしてから設定する |
| 依存の追加 `addLink(from, to)` | 自分自身は拒否（通知）。重複は拒否（通知）。`from.blocks` に `to` を追加 |
| 依存の削除 | `from.blocks` から外す |
| コメント追加 | 空は無視。`who` の `;` は `；` に置き換える。日時は現在時刻（`YYYY-MM-DDTHH:MM`）。`me` を設定に覚える |
| コメント削除 | 添字で削除 |
| 一括 `bulk(ids, fn)` | 複数の課題へ同じ変更。**Undo 1回で戻る** |

### 9.3 参照の解決 `resolveRef(text)`
詳細パネルの「親」やコマンドパレットで使う。前後の空白を除いて、①表示ID・`key`・`id` のいずれかと**完全一致** → ②表示ID・`key` と大文字小文字を無視して一致 → ③表示名の**部分一致**がちょうど1件、の順で探す。見つからなければ「見つからない」。

### 9.4 テンプレート（子タスクの雛形）
- 形式（付録A）: `items: [{ n(名前), w(週), lv(入れ子の深さ。0=直下の子), after:[先行する項目の番号], t(種別・省略可), lab:[ラベル], d(説明) }]`。名前の `{名前}` は追加先の表示名に置き換える。
- **追加 `applyTemplate(parentIds, tpl)`**（複数の親へまとめて。**Undo 1回**）: 各親について、親の**配下の最後の後ろ**から順に課題を作る。
  - 深さ `lv` は 0 〜 直前までの最大深さに丸める。上位 = `lv==0` なら親、そうでなければ直前の `lv-1` の項目。
  - `type` = 項目の `t`、なければ 上位が Epic なら `Task`、それ以外 `Sub-task`。
  - **番号タグ**: 親に番号タグがあれば、子は `{親のタグ}-{連番}`（`B-1` の子は `B-1-1`、`B-1-2` …。すでに子がいれば続きから）。孫以降も同様。タグがなければ付けない。
  - `durW` = `w`（なければ1）、`labels` = `lab`、`desc` = `d`。
  - `after` の各 `j` について、`j` 番目の課題の `blocks` に `i` 番目を追加（自分自身・重複を除く）。
  - 親を展開する。通知「N件の子タスクを追加しました」＋「元に戻す」。
- **保存 `saveTemplate`**: 子（子孫を含む）を持つ課題から作る。件名の番号タグは外し、先行関係は項目の番号に直す。名前は必須。保存先は `roadmap_prefs.json`。
- 組み込みテンプレートは付録A。削除できるのは利用者のテンプレートだけ。

### 9.5 プロジェクトの操作
新規（空・サンプル）、名前とキーの変更（キーは大文字化）、複製（名前に「 のコピー」）、削除（8.5）、CSVの取り込み（新規として追加 / 今のプロジェクトを置き換え。置き換えは Undo で戻せる）、書き出し（JIRA用 / アプリ用 / 共有JSON / 全バックアップ）。
- **取り込みの種類**: CSV（`{` で始まらないテキスト）。JSON（`{` で始まる）は共有・バックアップ形式として読む（付録B）。

---

## 10. 画面仕様

寸法は 100% 表示（96dpi）の論理ピクセル。高DPIでは拡大する。色は 10.12 のトークン名で指定する。細部で迷ったら `reference-js/css/style.css` と `reference-js/src/` を見る。**見た目をJS版とピクセル単位で合わせる必要はない**が、情報の種類・配置・操作は同じにする。

### 10.1 全体レイアウト
縦に積む:
1. **上部バー**（高さ 48、コンパクト 44）
2. **ツールバー**（高さ 40、コンパクト 36。はみ出したら横スクロール）
3. **バナー**（必要なときだけ。赤=エラー、橙=警告。1行、右に操作ボタン）
4. **作業領域**: 左に**メイン**（現在のビュー）、右に**詳細パネル**（幅 400。開いているときだけ）
5. 重ねて出すもの: コンテキストメニュー、コマンドパレット、ダイアログ、通知（トースト。左下）、ガントのツールチップ

最小ウィンドウサイズは 960×600。初期サイズは 1440×900（前回のサイズを `roadmap_prefs.json` に覚える）。

### 10.2 上部バー（左から右へ）
| 要素 | 動作 |
|---|---|
| **プロジェクト切替**ボタン | 「アイコン + プロジェクト名 + キー（小さなバッジ）+ ▾」。押すとメニュー（幅300）: プロジェクト一覧（現在のものにチェック。`名前  [キー]`）→ 区切り → 新しいプロジェクト / 名前・キー・削除 → 区切り → CSVを取り込む / 書き出し |
| 縦の区切り線 | |
| **ビュー切替タブ** | ガント / ボード / リスト / ギャップ / 統計（アイコン + 名前）。選択中を強調。キー `1` `2` `3` `4` `5` |
| **絞り込み欄** | 虫眼鏡 + 入力。プレースホルダ「件名・ID・ラベルで絞り込み  ( / )」。入力するたびに反映。`Esc` で消して離れる。入力があれば右端に×ボタン |
| **状況チップ** | 「遅れ N」「余裕小 N」（押すとその条件で絞り込み。もう一度押すと解除。0 のときは淡色）、「完了 N / 全体 M」（表示のみ）、**ゴール**ボタン（10.7.2） |
| （余白） | |
| **保存状態** | 8.4 の状態を1つのチップで: 保存済み（緑、`保存済み HH:MM`）/ 保存中…（青）/ 保存できません（赤。押すと詳細ダイアログ）。データフォルダのパスは**ツールチップ**に出す |
| **Ctrl+K** ボタン | コマンドパレットを開く |
| 元に戻す / やり直す | アイコンボタン。履歴が空なら無効 |
| **＋ 課題** | 主ボタン。最上位の課題を追加 |
| **⋯ その他** | プロジェクト設定 / 担当者の負荷 / 読み込み時の注意 / 区切り / テーマ（自動・ライト・ダーク。現在にチェック）/ 密度（標準・コンパクト）/ 区切り / ショートカット一覧（`?`） |

### 10.3 ツールバー
左から:
1. **担当者**セレクト（アイコン: 人）: 「全員」/ プロジェクト内の担当者名（五十音順 = `localeCompare("ja")` 相当。**日本語照合は ICU 不要の簡易実装でよい**: 並び順の細部は golden の対象外）/ 「未割当」/ 「担当者あり」。
2. **状況**セレクト: 状況: すべて / 未完了 / To Do / 進行中 / 完了。
3. **優先度**セレクト: 優先度: すべて / Highest … Lowest。
4. （ガントのときだけ）**拡大率**セグメント（日 / 週 / 月 / 全体）、**色分け**セレクト（色: Epic / 色: 余裕 / 色: 担当者）、**階層**セレクト（階層: すべて / 1階層まで … 4階層まで）、**展開**メニュー（すべて開く / すべて閉じる / 2階層目まで開く）、**基準日へ**ボタン。
5. （ボードのときだけ）**スイムレーン**セレクト（なし / 担当者 / ルート課題）。
6. （リストのときだけ）**展開**メニュー（すべて開く / すべて閉じる）。
7. 絞り込みが1つでも効いているときだけ**絞り込み解除**ボタン（効いている条件名を添える）。
8. （余白）
9. 警告があれば「注意 N」ボタン（押すと「読み込み時の注意」ダイアログ。N = 計算の警告 + 取り込みの警告の合計）。
10. **基準日**の日付入力（変更すると再計算。Undo対象外）。
11. **シナリオ**セレクト（`標準` と CSV の `@名前` 列）。
12. **担当者ごとの同時作業の制限**のトグルボタン（オン = 強調）。

### 10.4 共通の操作

#### 10.4.1 選択
- 状態: `selection`（順序つきの id の集合）、`cursor`（主たる1件）、`anchor`（範囲選択の起点）。
- クリック = その1件だけ選択。`Ctrl+クリック` = 追加/解除（解除したらカーソルは残りの最後）。`Shift+クリック` = `anchor` から `ui.order`（いま画面に並んでいる順）で範囲選択。
- `↑` `↓` = カーソル移動（1件選択）。`Shift+↑↓` = 範囲を広げる。`Alt+↑↓` = 選択した課題を兄弟の中で上下に移動（9.2 の `moveIssue`。複数選択なら各件に順に）。
- `←` `→`: 親行で `→` = 開く（すでに開いていれば次の行へ）、`←` = 閉じる（すでに閉じている/子なしなら親を選ぶ）。
- `Esc`: メニューが開いていれば閉じる → 選択があれば解除 → 詳細パネルが開いていれば閉じる、の順。
- 計算や削除で消えた課題は、選択・カーソル・編集中から自動的に外す。
- 選択が2件以上のとき、メインの下端に**まとめ編集バー**を出す: 「N件を選択」/ 状況（To Do・In Progress・Done）/ 優先度 / 担当者の入力（Enter で全件に設定）/ テンプレ / 階層を下げる / 階層を上げる / 削除 / 選択解除。すべて **Undo 1回**で戻る。

#### 10.4.2 行のその場編集
- **件名**: ダブルクリック / `F2` / `Enter`（行にフォーカスがあるとき）で編集に入る。全選択された状態で始まる。
  - `Enter` = 確定して終わる。**追加直後の行**（`fresh`）の `Enter`、または `Ctrl+Enter` = 確定して**同じ階層に新しい行を追加**して続けて編集。`Shift+Enter` = 確定して**子を追加**して編集。
  - `Esc` = 取り消し。`Tab` / `Shift+Tab` = 編集を続けたまま階層を下げる/上げる（確定も行う）。`↑` `↓` = 確定して**隣の行の編集**に移る（`Alt` つきは何もしない）。
  - **IME変換中（composition中）の `Enter` は無視する**（確定のためのEnterで編集が終わったり行が増えたりしないこと）。
  - フォーカスを失ったら確定。空文字なら変更しない。変更がなければ何もしない（Undoを積まない）。
- **担当者**: セルをダブルクリック → 1行入力（候補 = 既存の担当者名）。`Enter` 確定、`Esc` 取消、`Tab` 確定、フォーカス喪失で確定。前後の空白は除く。親課題では編集できない（表示もしない）。
- **期間（週）**: 末端の課題だけ。ダブルクリック → 数値入力（0以上、0.5刻み）。完了した課題は編集不可。マイルストーンは「◆」表示で編集不可。親は「N件」表示。
- **状況**: ピル（To Do=灰、In Progress=青、Done=緑）を押すと小メニュー（To Do / In Progress / Done。現在にチェック）。選択に含まれている課題なら**選択全体**に適用。親課題では配下の「完了数/全体数」を表示する（編集不可）。
- **余裕**: 表示のみ（チップ。10.12）。

#### 10.4.3 コンテキストメニュー（行・カードの右クリック）
右クリックした行が選択に含まれていなければ、それだけを選択してから開く。項目（複数選択のときは単体向けの項目を出さず、見出しや件数表示を付ける）:
- 詳細を開く（Space）/ 名前を変更（F2）/ 区切り
- 子を追加（Shift+N）/ 同じ階層に追加（N）/ テンプレから子タスクを追加… / この子タスクをテンプレとして保存…（子がなければ無効）/ 区切り
- 見出し「状況」+ To Do / In Progress / Done（単体のときは現在にチェック）/ 区切り
- 階層を下げる（Tab）/ 階層を上げる（Shift+Tab）/ 上へ（Alt+↑）/ 下へ（Alt+↓）（動かせないときは無効）/ 区切り
- 削除（Del。赤）

#### 10.4.4 キーボード（フォーカスが入力欄にないとき）
| キー | 動作 |
|---|---|
| `Ctrl+K` | コマンドパレットを開閉（入力欄にいても有効） |
| `Ctrl+Z` / `Ctrl+Shift+Z` / `Ctrl+Y` | 元に戻す / やり直す |
| `1` `2` `3` `4` `5` | ガント / ボード / リスト / ギャップ / 統計 |
| `/` | 絞り込み欄にフォーカス |
| `?` | ショートカット一覧 |
| `↑` `↓` `←` `→` `Shift+↑↓` `Alt+↑↓` | 10.4.1 |
| `Enter` / `F2` | カーソル行の件名を編集 |
| `N` / `Insert` | カーソル行と同じ階層に追加（カーソルなしなら最上位に追加）。`Shift+N` = 子を追加 |
| `Tab` / `Shift+Tab` | 階層を下げる/上げる（選択全体） |
| `Delete` / `Backspace` | 選択を削除（トースト「N件を削除しました」+ 元に戻す） |
| `Space` | 詳細パネルを開閉 |
| `Esc` | 10.4.1 |
モーダル（ダイアログ・パレット）が開いている間は、`Ctrl+K` と `Esc` 以外のグローバルキーを無効にする。

#### 10.4.5 通知（トースト）
左下に積む。3.6 秒で消える（「元に戻す」などのボタン付きは 7 秒。完了の提案（8.11）は 20 秒）。×で閉じられる。入りきらない長い文は省略せず折り返す（完了の提案は常に折り返し、ボタンは「完了にする」）。

#### 10.4.6 ドラッグ＆ドロップ
- **ファイルのドロップ**: ウィンドウにCSV/JSONファイルをドロップすると取り込みダイアログ（10.11）を開く（中身は読み込み済み）。
- **添付のドラッグ**: 添付タブの行をつかんで、別の課題の行・カード・詳細パネルへ離すと移動（Ctrl でコピー）、一覧の中なら並べ替え（8.9）。ファイルのドロップと同じドロップ先で判定する。
- 行の並べ替え（ガント）は 10.5.4。ボードのカードの移動は 10.6。

### 10.5 ガント

#### 10.5.1 構造
- 全体が1つのスクロール領域（縦横）。**左の表**（課題・担当・状況・期間・余裕の列）は**横スクロールで左に固定**（sticky）、**見出し行は縦スクロールで上に固定**。
- 行の高さ `RH` = 30（コンパクト 26）。見出しの高さ = 拡大率が日・週で 46、月・全体で 30。
- 左の表の幅 = `18（つまみ） + 名前列の幅 + 表示中の各列の幅`。既定: 名前 300、担当 84、状況 92、期間 52、余裕 62。最小: 名前 160、担当 48、状況 64、期間 40、余裕 48。**列の幅は見出しの右端をドラッグして変更でき、プロジェクトの設定（`ui.nameW` / `ui.colW`）に保存**する。列の表示/非表示は設定ダイアログ（`ui.cols`）。
- 行ごとの内容: ドラッグつまみ(⋮⋮) / 件名セル（開閉▸▾、種別アイコン、表示ID、件名、コメント数、ホバー時の「子を追加」「詳細を開く」小ボタン）/ 担当 / 状況 / 期間 / 余裕 / 右側のタイムライン。インデントは 1階層あたり 14px。親行の件名は太字。最上位（深さ0）の行は上に細い線。

#### 10.5.2 時間軸
- 範囲: `lo` = 基準日と全バーの最早開始の最小、`hi` = max(基準日+120, 各バーの最早完了+21, 最遅完了+21, 期限+21)。左端 `rs` = `lo` を含む週の**月曜日の1週前**（`lo - ((dow(lo)+6)%7) - 7`）。
- 1日の幅 `px`: 日=22、週=6、月=2.4、全体=`max(0.9, min(12, (表示幅 - 左の表の幅 - 24) / (hi - rs + 1)))`。
- 見出し: 月ラベル（`YYYY/M`。1日の位置と先頭。ラベル間隔が58px未満なら間引く）。日=日付の数字の行（土日は淡色）。週=週の頭（月曜）の `M/D`（1週間の幅が 40px 未満なら 2週おき、26px 未満なら 3週おき）。
- 背景: 土日（日の拡大率のときだけ帯）、**休業日**（平日の休業日に淡い帯）、1週間の幅が 18px 以上なら週ごとの縦の薄い線、**基準日**に縦線 + 見出しに「基準日」ラベル。
- 初期スクロール: プロジェクトを開いたとき・拡大率を変えたとき、基準日が左端から 90px の位置に来るようにする（全体は 0）。「基準日へ」ボタンも同じ。

#### 10.5.3 バーの描画
色は `colorBy`: `root` = ルート課題の出現順 mod 8 のパレット色 `--c0〜c7`、`float` = 余裕による色（遅れ=赤、余裕小=橙、余裕あり=アクセント、完了=灰 `--c0`）、`asg` = 担当者名のハッシュ mod 8（`h = h*31 + ch` を 32bit 符号なしで）。リスク `risk` = 完了 `done` / 余裕<0 `late` / 余裕≤10 `tight` / それ以外 `ok`。

| 種類 | 描画 |
|---|---|
| 末端の通常課題 | 最早開始〜最早完了+1 の矩形（角丸4px、最小幅3px、高さ `RH-12`）。幅が34px超なら中に「`N`w」（完了は✓）。**進行中**は斜線模様、**完了**は半透明、**余裕0（クリティカル）**は橙の縁、**遅れ**は赤の縁、**担当者待ち**（`waited`）は黒の破線の縁。 |
| 余裕の影 | 完了していない課題で `最遅完了 > 最早完了` なら、バーの右に `(最遅完了-最早完了)` 日分の斜線の細い帯（`risk` が tight/late なら橙/赤）。`最遅完了 < 最早完了`（遅れ）なら、最遅完了の位置から最早完了までの赤い帯（`over`）。 |
| 親課題 | 配下の最早開始〜最早完了+1 の「サマリーバー」（両端が下に尖った形）。最遅までの余裕の帯付き。 |
| マイルストーン（末端） | 菱形（12px、45度回転）を `最早完了` の日の中央に。 |
| 期限 | 期限のある行は、期限の日に白抜きの菱形（黒い縁）。ツールチップ「期限 YYYY/M/D(曜)」。 |
| 計画のバー（ベースライン） | 表示するベースラインがあれば、各行のバーの下（行の下端から 5〜2px）に薄い帯（親は計画の開始〜完了、マイルストーンは小さな菱形）と、右に差の文字「! +3日」「-2日」。10.7.7。 |

#### 10.5.4 操作
- **行クリック**: 10.4.1。**ダブルクリック**: 詳細パネル。**右クリック**: 10.4.3。
- **バーのドラッグ（移動）**: 完了でも固定（`pinned`）でもない末端の課題で可能。ドラッグ中は1日単位に丸めてバーを動かし、横にツールチップ「着手 YYYY/M/D(曜)」。離したとき、右へ動かした場合は `ns = calendar.workday(ns-1, 1)`、左へなら `calendar.lastWork(ns)` に丸めて `setEarliest`（9.2。反映されなければ元の位置に戻る）。動かしていない/日が変わらないときは何もしない。
- **バー右端のつまみ（期間変更）**: ドラッグ中ツールチップ「期間 N週（M営業日）」。離したとき `nf` を営業日に丸め、`days = max(1, workdaysIn(開始, nf))`、変わっていれば `setDuration(id, days/5)`。
- **バー端のハンドル（依存をつなぐ）**: バーにマウスを載せると、左右の端の外側に丸いハンドルが出る（バー内の移動・期間変更とは重ならない）。右のハンドルから別のバーへドラッグ = つかんだバーが先行、左のハンドルから = つかんだバーが後続。ドラッグ中はゴムひも線がカーソルに追従し、マウスの下の行のバーに吸着して強調する。つなげない相手（循環・すでに設定済み・親子）は赤とその理由、自分の行は相手なし。つなげるときは `previewLink` で日程の影響を計算し、後続の位置に薄いバー（ゴースト）と「N営業日 遅れます」を出す。離したら `addLink`（Undo 1回）。元の場所で離す・Esc は取り消し。依存の選択モード（`linkPick`）の間は出さない。
- **一覧・ボードのハンドル（依存をつなぐ）**: 一覧の行・ボードのカードにマウスを載せると、左右にハンドルが出る（一覧は表の左右の余白、ボードはカードの左右の縁）。操作・プレビュー・確定・取り消しはガントのバー端のハンドルと同じ（`linkDrag` を共有）。ゴムひもは、ボードでは右のハンドル → 相手の左の縁（同じ列の相手へは同じ側の縁）、一覧では同じ側の余白の中を通る。ハンドルを押したときは行・カードの選択・列の移動にしない。依存の選択モード・ダイアログ・その場編集の間は出さない。ドラッグ中にビューを切り替えたら終わる。ドラッグ中、見えている範囲の端へマウスを近づけると自動でスクロールする（ガントを含む）。
- **空いた場所で離す（新しい課題を作ってつなぐ）**: ガント・一覧・ボードのハンドルのドラッグを、相手の行・カードのない場所（ガント・一覧は最後の行より下、ボードは列の空き。つかんだ課題自身の上は「元の場所」で取り消し、ビューの外も取り消し）で離すと、その場に小さなメニュー（「新しい後続の課題を作ってつなぐ」「新しい先行の課題を作ってつなぐ」「取り消し」）を出す。右のハンドルなら「後続」、左なら「先行」を先頭に置く。Esc・メニューの外のクリックで閉じ、何も変えない。選ぶと、起点の兄弟（親は起点と同じ。起点の直後）として課題を作り（名前は「同じ階層に追加」と同じ規則 = アプリの設定の新しい課題の名前（`makeNewIssueName`）、値の引き継ぎも同じ）、起点 → 新しい課題（先行を選んだときは 新しい課題 → 起点）とつなぐ。作成とつなぎは Undo 1回。つなげなければ作った課題も戻す。作ったあとは件名のその場編集を始める。ボードでは作る課題の状況を起点と同じにする。メニューの間はほかのハンドル・ドラッグを出さず、ダイアログ・ビュー・プロジェクトの切り替えで閉じる。
- **依存の矢印の操作・見せ方**: 矢印にマウスを載せると強調（両端につまみ）し、ツールチップに「A → B」。矢印をつかんで動かすと、近い方の端（矢じり = 後続、根元 = 先行）を別のバーへ付け替える（`relinkIssue`。元のつなぎを外した状態で循環・重複・親子を判定し、日程の影響をプレビュー。Undo 1回）。バーのない場所（タイムライン内）で離すと `removeLink`（Undo 1回）。タイムライン外・元の相手の上で離す・Esc は取り消し。課題に書かれたつなぎがその1本だけ（両端が行の課題そのもの）の矢印だけ操作でき、親や折りたたみをまたぐ依存は詳細パネルで編集する。後続が先行の完了より前に始まる矢印（`isDateConflict`。完了した後続は除く）は赤の点線と「!」。ツールバー「依存 ▼」: 「選んだ課題の先行・後続をたどる」（`traceDependencies`。連鎖の外の行と矢印を薄くする）、「クリティカルパスを強調」（`isCriticalLink` = 未完了どうしで余裕 0 以下の線を太く、ほかを薄く）。どちらも既定はオフで、プロジェクトの表示設定（`ui.dep`）に覚える。
- **行のドラッグ（並べ替え・階層変更）**: 左端のつまみをドラッグ。ドロップ先の行の上端 28% = 「前に」、下端 28% = 「後ろに」、中間 = 「子として」。ドロップ位置を縁取り線で示す。離したら `relocate`（9.2）。自分や子孫の上は無効。
- **ツールチップ**（バーにホバー）: 1行目「表示ID  件名」/ 「計画 開始 〜 完了（N週）」/ 「最遅 開始 〜 完了」（完了していなければ）/ 「余裕 N営業日」または「余裕 −N週（遅れ）」/ 期限 / 担当 / 担当者の別作業待ちの注記。カーソルの右下に出し、画面外に出ないよう寄せる。
- **依存の矢印**: 末端課題どうしの先行関係を、表示されている最も近い祖先行どうし（折りたたまれているときは親行）に付け替えて描く（重複は1本）。始点 = 先行の完了+1 の位置（マイルストーンは菱形の右）、終点 = 後続の開始位置（マイルストーンは菱形の左）。経路: 終点が始点の右 12px 以上なら「右へ6 → 縦 → 右へ」の直角線、そうでなければ「右へ6 → 半行ぶん縦 → 左へ → 縦 → 右へ」の迂回線。矢じり付き。後続の余裕が負なら赤、0なら橙、そうでなければ灰。**カーソル行に関係する矢印は強調**（アクセント色・前面）し、それ以外は薄くする。カーソル行に関係する先行/後続の行は左端にアクセント色の縦線。
- 条件に合う課題がなければ「条件に合う課題がありません。」を表示。課題が0件なら中央に案内（「最初の課題を追加」「CSVを取り込む」ボタン）。
- 初回だけ下端に**操作ヒントのバー**（`ダブルクリックで名前を編集 ／ N で行を追加 ／ Tab で階層を下げる ／ バーをドラッグで日程変更 ／ Ctrl+K でコマンド`、閉じるボタン。閉じたら二度と出さない）。

### 10.6 ボード
- 末端の課題だけをカードにする（絞り込みを適用）。**列** = `To Do` / `In Progress` / `Done` + 課題の `status` にある**それ以外の文字列**（出現順に追加。大文字小文字を無視して同一視）。`status` が空のカードは先頭列。
- 列の見出し: 列名と件数。列の最小幅 200、残りは均等。
- **スイムレーン**: なし / 担当者（未割当は最後。名前順）/ ルート課題（出現順）。レーンの見出しは「名前 N件」。
- **カード**: 左に 3px の色帯（ルート課題の色）/ 1行目: 種別アイコン・表示ID・（右端に）余裕チップ / 件名 / 祖先の名前を「 › 」でつないだ小さな文字 / 下段: 優先度記号（`▲▲` `▲` `＝` `▼` `▼▼`。色は赤・赤・橙・アクセント・アクセント）・コメント数・`M/D〜M/D`（最早）・（右端）担当者のイニシャル。
- 操作: クリック/Ctrl+クリックで選択、ダブルクリックで詳細、右クリックでメニュー（10.4.3）。**カードを別の列へドラッグ → `setStatus(id, 列名)`**（9.2）。
- 条件に合う課題がなければ「条件に合う課題がありません。」。

### 10.7 リスト・ゴール（ボタン）・ギャップ

#### 10.7.1 リスト
- 列（この順・固定幅）: 課題（残り全部、最小260）/ 種別 86 / 状況 96 / 担当 110 / 優先度 80 / 開始（計画）104 / 完了（計画）104 / 期間 64 / 余裕 72 / 期限 104。日付は長い形（`2026/10/19(月)`）から年を除いた `10/19(月)` の形。
- 見出しをクリックで並べ替え（昇順 → 降順 → 解除）。**並べ替え中は末端の課題だけをフラットに並べ**（インデントなし）、見出しの下に注記「並べ替え中は、末端の課題だけをフラットに表示します。見出しをもう一度押すと解除します。」。並べ替えキー: 課題=名前、種別=文字列、状況=todo0/prog1/done2、担当=名前（空は最後）、優先度=順位、開始/完了=日、期間=週、余裕=営業日（完了は最後）、期限=日（なしは最後）。同値は元の順序。文字列比較は日本語を意識した簡易照合でよい。
- 行の操作はガントの左の表と同じ（選択・その場編集・メニュー・開閉）。

#### 10.7.2 ゴールボタンのポップオーバー（上部バー）
ルート課題（子を持つか Epic のもの）が1つもなければボタン自体を出さない。ポップオーバー（幅380）: 見出し「ルート課題ごとの余裕」+ 各ルートの行 = **表示チェック**（ガント・ボード・リストでそのルートを出すか。`ui.hide[id]`）/ 色の丸 / 件名（押すと選択してスクロール）/ `完了/全体` / 余裕チップ（完了=「完了」、<0=赤、≤10=橙、その他=緑）/ 期限 `〜M/D`。

#### 10.7.3 ギャップ（ゴール）ビュー
JS版 `views/Goals.jsx` と同じ。**ゴール** = 子を持たず、種別またはラベルに `milestone` / `マイルストーン` を含む課題。
- 見出し: 「ゴールと現状のギャップ」+ 説明文 + 集計（未達のゴール数 / 期限に間に合わない数 / 期限が未設定の数 / 達成済み数）。
- 未達のゴールを、**優先度（Highest が先）→ 期限の有無（ありが先）→ 期限が早い順 → 見込みが早い順**に並べ、カードで出す。達成済み（`done`）は最後に1行「達成済み: …」。
- カード: 見出し（種別アイコン・表示ID・件名。Highest なら「最重要」）/ 期限（理想）/ 見込み（現状 = ゴールの最早完了）/ **差**（`gap` = 営業日差 `floatDays(min(期限, 見込み), max(…))`）の文: 期限なし=「期限が未設定」、遅れ=「N営業日 遅れる見込み」、ちょうど=「余裕なし（ちょうど間に合う）」、それ以外=「N営業日の余裕」+ 補足文。
- **ギャップの帯**: 基準日・期限・見込みを1本の線上に置く（`lo = min(基準日, 期限)`、`hi = max(期限か見込み, 見込み) + 4`）。期限と見込みの間を色帯（遅れ=赤、それ以外=緑）で塗る。
- **いま日程を決めている連なり**（`drivingChain`）: ゴールから、未完了の先行のうち**最早完了が最も遅いもの（同日なら余裕が小さいもの）**を1つずつ遡った一本道。上から順にステップ表示（番号・種別・表示ID・件名・担当・日付・期限・余裕・状況ピル）。クリックで詳細を開く。最後にゴール自身の行。
- 「並行して必要な未完了の前提 N 件」（`allBlockers` から連なりを除いたもの。最早完了順）を折りたたみで出す。
- 該当する課題がなければ案内「ゴール（マイルストーン）がありません」+ 「種別を Milestone にすると…」。

#### 10.7.4 統計ビュー（0.4.0 で追加。JS版にはない）
キー `5`・コマンドパレット「統計を表示」。プロジェクト全体の集計で、絞り込みには従わない（`core/stats` の `computeStats`）。数える対象は**作業**（子を持たない課題。マイルストーンは別に数える）。
- **全体**: 進み（完了した作業の期間の合計 ÷ 全体の期間。期間が 0 のときは件数）・作業 完了/全体・遅れ・余裕小（≤10日）・完了の見込み（未完了の最早完了の最大）・担当なし のタイル、状況（完了・進行中・未着手）の積み上げ棒、全体の週数・マイルストーン・期限なしの件数。
- **余裕の分布**（未完了）: 遅れ / 0〜5日 / 6〜10日 / 11〜20日 / 21日以上。
- **週ごとの完了**: 月曜始まり。基準日の週の前 8 週は完了日（Resolved）の件数、基準日の週から先は最早の完了日の件数（最大 26 週。それより先は最後の週にまとめる）。基準日の週を強調する。
- **担当別**: 未完了の期間が多い順（担当なしは最後）。行を押すと、その担当者で上の絞り込みを掛けてリストを開く。
- **担当者別の負荷（週ごと）**（`core/load` の `computeLoad`。#41）: 縦に担当者（負荷の合計が大きい順。担当なしは最後）、横に週（月曜始まり。基準日の週から最大 26 週で、それより先は最後の列にまとめる）のヒートマップ。未完了の作業（マイルストーンを除く）の期間（`days`。0 以下は 1）を、最早の開始〜最早の完了の営業日へ均等に広げ、担当・週ごとに足す。負荷の割合 = その合計 ÷（その週の営業日数 × cap）。営業日数は休業日を除く（0 の週は割合 0）。cap は資源制限がオンのときだけその値、それ以外は 1。色は 80% 未満＝緑、80〜100%＝橙、100% 超＝赤（境は丸めたパーセントで決め、数字を重ねる。100% 超は「!」つき）。セルにマウスを載せると、その週の作業（件名・期間）を最大 8 件と「ほか N 件」で出す。行を押すと、その担当者で絞ってリストを開く（担当別の行と同じ。週では絞らない）。
- **担当者別の完了（過去 8 週）**: 基準日の週を含まない過去 8 週の、完了した作業（`Resolved` の日付で週を決める。完了日のないものは数えない）を、担当者の積み上げ縦棒（週ごと）と、担当者ごとの完了数・期間（週）の合計・平均の表で出す。名前順（担当なしは最後）で、順位にはしない。見積り対実績は担当者別に出さない。
- **ルート課題別**: 進み・完了の見込み・最小の余裕・遅れ。親のない単独の作業は1行にまとめる。
- **優先度別**: 完了と未完了の件数。
- **推移**（`core/trends` の `computeTrends`。#54 の A。次の 3 つの図は履歴を保存せず、CSV の日付だけで出す。最後の「状況の件数の推移（累積フロー）」だけ、記録した件数から出す）: 節の中に担当者（全員・担当なし・名前）とルート課題（すべて・親のない単独の課題・ルート）の選択欄があり、3 つの図がそれで絞られる（上のバーの絞り込みや他の節には連動しない）。数える対象は作業（マイルストーンを除く）。`Created`（作成日）・`Updated`（更新日）は CSV の未知の列から見出しで読み、Resolved は状況が完了のときだけ使う。
  - **バーンダウン／バーンアップ**: 週（月曜始まり。基準日の週から過去 26 週まで）ごとに、作った数（Created）・完了した数（Resolved）の棒と、その週の終わり（基準日の週は基準日）の残りの折れ線。直近 4 週（基準日の週を含まない）の完了数の平均で残りを割った週数（切り上げ）を、点線で先へ伸ばし、「完了の見込み」の週を出す。完了のある週が 2 週未満・直近に完了がない・残りが 0・104 週より先のときは予測を出さず、理由を 1 行（先頭に「!」）出す。週にマウスを載せると値（予測の週は予測の値）。
  - **リードタイム**: 作成日の翌日から完了日までの営業日（休業日を除く）。件数・中央値・90 パーセンタイル（nearest-rank）・最長と、階級別（0-1 / 2-4 / 5-9 / 10-19 / 20+ 営業日）の棒。
  - **滞留日数**: 未完了で、最後に動いた日（Updated → 開始日 → Created）から基準日までの営業日。階級別の棒（20+ は赤と「!」）と、長い順の上位 10 件（件名・担当・日数。押すとその課題を選んで詳細を開く）。基準日より先の日付は 0 日（作成日が基準日より先の課題は数えない）。
  - 日付のないもの（作成日がない・完了なのに完了日がない・最後に動いた日がわからない）、完了日が作成日より前のもの（日付の矛盾）、作成日が基準日より先のものは数えず、件数を注記する。暫定判断は B41。
  - **状況の件数の推移（累積フロー）**（`core/history`。#64＝#54 の B。暫定判断は B45）: 週（月曜始まり）ごとの未着手・進行中・完了の件数（マイルストーンを除く作業。分類は統計と同じ）の履歴を積み上げた面。完了（下・実線）→ 進行中（中・破線）→ 未着手（上・点線）で、凡例に線種を添え、最新の週の合計を数字でも出す（色だけに頼らない）。マウスを載せると週と 3 つの値（と合計）。**記録のない週は線を引かず**、点線の枠の薄い帯に「記録なし」と書く（間の空き週・基準日の週が未記録のときの末尾。最初の記録より前は何も出さず、前後を補った線は引かない。孤立した記録は細い柱）。記録が 2 週ぶん未満なら図は出さず「記録が溜まるとここに出ます（いま N 週ぶん）」。列は幅に収まるまで縮め（最大 104 列）、見出しは間引く。**記録のきっかけ**は 3 つ: ①プロジェクトを開いたとき、今週の点がまだなければ自動で追加（同じ週なら何もしない＝開くたびに設定ファイルを書き換えない。作業が 0 件なら足さない）、②節の「いま記録する」ボタン（同じ週は置き換え。変わりがなければ書かない）、③週次レポートをコピー・保存したとき（同じ週は置き換え）。記録の入口は `AppController::recordHistory` の 1 か所。取り消し（Undo）の対象外で、保存はする。持つのは件数だけ（名前・担当者などは入れない）。

#### 10.7.5 報告（HTML）の書き出し（0.5.0 で追加。Issue #42）
「…」メニューとコマンドパレットの「HTML に書き出す…」。ガント・統計・リストを、外部のファイル（CSS・フォント・スクリプト・画像）を読まない 1 つの HTML（UTF-8・BOM なし・`<meta charset="utf-8">`・インライン SVG・スクリプトなし）にする。書き出しはユーザーが押したときだけで、自動では出さない。
- **ダイアログ**: 内容（ガント／統計／リスト。1 つ以上）・範囲（プロジェクト全体＝既定／今の表示のまま＝画面の絞り込み・折りたたみ・薄くする行／選んだ課題とその配下＝課題を 1 つ選んでいるときだけ）・ガントの拡大率（日・週・月・全体。画面の今の値から始める）・色分け（Epic・余裕・担当者。同）・依存の矢印・色（ライト＝印刷向けが既定／ダーク）。注意書き「担当者名・件名がファイルに入ります。メールや共有フォルダに置く前に、内容を確かめてください。」。「書き出す…」で保存ダイアログを開く（描画が終わってから）。
- **保存**: `IPlatform::pickSaveFile`（Win32 は `IFileSaveDialog`・拡張子 .html が既定・上書きの確認はダイアログ）。既定の名前は `<プロジェクト名>_<今日>.html`（パスに使えない記号と制御文字は `_`、Windows の予約名と末尾の `.`・空白は避ける。60 文字まで）。選んだパスは `.html` / `.htm` だけを受ける。`IFileStore::writeAtomic` で書き、成功したら通知「書き出しました: ファイル名」に「フォルダを開く」、失敗したら「書き出せません: 理由」（例外にしない）。
- **ページ**: ヘッダにプロジェクト名・基準日・出力した日時（`IClock`）・完了/全体・遅れ・余裕小、フッタに「このアプリで出力」。ライトは背景白（インクを節約）。`@page { size: A4 landscape }`・`@media print`・節ごとの改ページ・図と表の行が途中で切れない指定。印刷でも背景色を落とさない（`print-color-adjust: exact`）。色だけに頼らない: 遅れは赤＋「!」（余裕の列・バーの枠と「!」・統計の遅れ・100% 超のセル）。
- **ガント**: 画面と同じ並び・色分け・拡大率（日 22・週 6・月 2.4 px/日。全体は幅 900 に合わせる）・基準日の線と見出しの「基準日」・期限の菱形・依存の矢印・親のまとめバー・余裕の斜線・遅れの帯・進行中の斜線・担当者待ちの破線の枠。座標は `core/gantt_geom` を画面と共有する（日付 → x、行 → y、矢印の経路、バーの色の判断）。行の高さ 26。長いガントは行ごと（A4 横 1 ページぶん）に区切った SVG にし、見出し・背景は最初の SVG にだけ定義して `<use>` で使い回す。行が 600 以下なら、バーにマウスを載せると出る説明（`<title>`）を付ける。
- **統計**: 10.7.4 と同じ内容を、`core/stats`・`core/load`・`core/trends` の集計から出す。タイルは HTML、図は SVG。絞り込みには従わない（「推移」もプロジェクト全体。画面の選択欄は反映しない）。「推移」は統計の最後の節で、週次レポートには足さない（B41）。「状況の件数の推移（累積フロー）」も同じ節に、記録した履歴（`ExportInput::history`）から同じ内容をインライン SVG で出す（面は背景に混ぜた不透明な色・スクリプトなし。記録のない週の帯と「記録なし」・各週の `<title>` つき。B45）。
- **リスト**: 件名（階層はインデント）・状況・担当・期間（計画の開始〜完了と週）・余裕・期限の表。印刷では見出しを各ページに繰り返す。
- **エスケープ**: 本文・属性に入るデータは `escapeXml` を通す（`& < > " '` を実体参照、制御文字・向きを変える文字は除去、壊れた UTF-8 は U+FFFD、改行・タブは空白）。データから URL・スクリプト・リンクは作らない。
- **上限**: ガントは 5,000 行まで（超えたら注記。リストには全部出る）・リストは 20,000 行・時間軸は 4,000 日・統計の 1 つの図の担当者・ルート課題は 200 行・依存の矢印の断片は 40,000（省いたら注記）。

#### 10.7.6 週次レポート（0.5.0 で追加。Issue #43）
「…」メニューとコマンドパレットの「週次レポート…」。週に一度の報告の下書きを、数字と一覧は自動で、所感だけ自分で書く形で作る。`core/weekly`（UI 非依存）が見出しごとのリストを持つ構造体 `WeeklyReport` を返し、Markdown（`weeklyMarkdown`）と HTML（`weeklyHtml`）のレンダラが別々に文字にする。
- **期間**: 今週・先週（月曜〜日曜。基準日の週）・任意の範囲（両端を含み 366 日まで）。「次の 7 日間」（来週の予定）は期間の最後の日の翌日から。
- **見出し**（項目ごとにオン・オフ）: サマリー（進み・作業の件数・遅れ・余裕小・完了の見込み日・期間内の完了数・マイルストーン）／今週の完了／進行中／来週の予定（開始・完了）／遅れ・余裕小（先行を 1 行）／マイルストーン／担当別（名前順。順位にしない）／先週から変わったこと（スナップショットがあるときだけ）／所感・課題・お願い（空なら見出しだけ）。1 つのリストは 100 件まで（超えたら「ほか N 件」）。
- **画面**: ダイアログに期間・項目・所感の入力欄と、Markdown のプレビュー（等幅・スクロール）。「Markdown をコピー」（`IPlatform::setClipboardText`）・「Markdown を保存…」・「HTML を保存…」（`IPlatform::pickSaveFile`。既定の名前 `<プロジェクト名>_週報_<期間の最初の日>.md` / `.html`。保存先の拡張子は `.md` `.markdown` `.txt` / `.html` `.htm`）。担当者名・件名が出力に入る注意書き。コピー・保存は押したときだけ。
- **エスケープ**: Markdown は `escapeMarkdown`（`\` `` ` `` `*` `_` `~` `[` `]` `<` `>` `&` `|` を `\` で打ち消し、改行・タブは空白、制御文字・向きを変える文字は除去、壊れた UTF-8 は U+FFFD、行頭の `#` `-` `+` `>` と番号つきリストも打ち消す。1 件 200 文字まで）。所感は Markdown のまま入れ、`<` だけ打ち消す。HTML は `escapeXml`（10.7.5）で、外部のファイル・スクリプトを使わない。
- **覚えるもの**（`project.settings.json` の `"weekly"`。B.1）: 所感（`notes`）と、コピー・保存したときのスナップショット（`snaps`。週ごとに 1 つ・2 つまで）。次の週の「先週から変わったこと」（完了の見込み日が動いた課題・新たに遅れた課題・新しく完了した課題・増減）とサマリーの増減に使う。
- **週の頭のスナップショット（自動。0.10.0 で追加。Issue #70。暫定判断は B47）**: プロジェクトを開いたとき、その週（月曜始まり。IClock の今日の週）のスナップショットがなければ 1 つ自動で取る（`Session::recordWeeklyAuto`。呼ぶのは `AppController::recordWeeklyAuto` の 1 か所）。同じ週なら何もしない（設定ファイルを書き換えない）。作業が 0 件なら足さない。中身は `"weekly"` のスナップショットと同じ（Issue Id・状況・完了の見込み日・遅れ）。保存は別のキー `"weeklyAuto"`（8 週まで・1 週 1 つ・1 つ 5,000 件まで）で、設定だけを書く。手動（コピー・保存）のスナップショットとは別に持ち、お互いを置き換えない。
- **比べる元**（`weeklyChooseCompare`。基準日の週で決める。自動のものを先に使う）: 今週 = 今週の頭の自動と、いま（今週の頭がなければ、今週より前の手動・自動のうち新しいほう）。先週 = 先週の頭の自動（なければそれより前で一番近い自動）と、今週の頭の自動（なければ、いま）。任意の範囲 = 範囲の始まり以前で一番新しい自動と、終わり以前で一番新しい自動（範囲が今週に届くなら、いま）。自動の元がなく手動だけのときは、今週より前の手動と、いま（従来どおり）。元がなければ「先週から変わったこと」の節は出さない。終わりが自動のスナップショットのときは、サマリーの増減は出さず、節の見出しの下に「…から…までの変化です」と書く。

#### 10.7.7 ベースライン比較（0.9.0 で追加。Issue #53）
「最初に決めた計画に対して、どれだけずれたか」を見る。自分で選んだ時点の日程を名前つきで保存し（ベースライン）、いまの見込みとの差を営業日で出す。`core/baseline`（UI 非依存。`Result` を返し、例外を投げない）。日程の計算（JIRA 互換）と CSV は変えない。
- **保存**: 「…」メニューとコマンドパレットの「ベースラインを保存…」（ダイアログ。名前の既定は「YYYY-MM-DD の計画」。名前は制御文字・向きを変える文字を除き 40 文字まで）。課題ごとに Issue Id・見込みの開始日・完了日（親は配下のまとめ）・期限（なければ空）・状況（未着手・進行中・完了）・親かどうか・件名を写す。**担当者・工数は写さない**。件名は、あとで「消えた課題」を見分けるためだけに持つ（100 文字まで）。最大 5 つ（超える保存は理由を出して断る）。1 つ 5,000 件・Issue Id 64 バイトまで（超えた分は持たず `truncated`）。ダイアログに「件名が設定ファイルに入ります」と注意を出す。
- **管理**: 「ベースラインの管理…」（一覧・表示するものの選択（ラジオ）・名前の変更・削除（確認のダイアログ））。ツールバーにも「計画: …」の選択欄（ベースラインがあるとき。ガント・リスト・統計）。表示する 1 つだけが比較の相手になる。保存・名前の変更・削除・表示の切り替えは取り消し（Undo）の対象外で、保存はする。
- **突き合わせ**: Issue Id で行う（表示 ID は階層で変わる。階層を移しても同じ課題）。ベースラインにあっていまない課題＝「消えた」、いまあってベースラインにない課題＝「計画になかった」（ベースラインが `truncated` のときは「計画になかった」と言えないので数えない）。削除した直後に課題を足すと同じ Issue Id が振られることがあり、その場合は同じ課題として比べる。
- **差**: 完了日の差・開始日の差を営業日で出す（`+` は遅れ）。営業日の数え方は `(from, to]` の平日から休業日を引いた数（平日の数は式、休業日は集合を見るだけで、長い期間でも速い）。判定は完了日の差で、遅れ（>0）／早まり（<0）／計画どおり（0）。計画に完了日がなければ「比較できない」。完了した課題も、実際の完了日（Resolved）で早い・遅いを見る（「完了」は状態ではなく印 `done`）。**営業日の差が 0 でも完了日が違う場合**（休業日・土日をまたぐ）は日付の違い（`finishDiffers`）を別に返し、週次レポート（10.7.6）と同じ「営業日の差は 0（休業日・土日をまたいだ日付の違い）」を出す。集計（早まり・計画どおり・遅れ・計画になかった・消えた・いま完了）は末端の課題だけを数える（親のまとめは二重に数えない）。遅れの大きい順の上位 10 件（同じ大きさは表の順）。
- **ガント**: 表示するベースラインがあると、各バーの下に薄い「計画のバー」（高さ 3px。親のまとめバー・マイルストーン〈小さな菱形〉も）と、ずれた分を右に「+3日」「-2日」で書く。遅れは色だけに頼らず「! +3日」（「+」と「!」）。ぴったりなら文字なし、計画になかった課題は「新規」、営業日の差が 0 で日付が違えば「±0日」。マウスを載せると計画・いま・差と注記。座標・文字の位置は `core/gantt_geom`（`ganttPlanShape`。画面と HTML 書き出しで共有）。
- **リスト・ガントの表**: 列「計画との差」（完了日の差。遅れは「! +N日」。親は配下のまとめの差）。プロジェクト設定の「計画との差の列」で出す（既定オフ。ベースラインを表示しているときだけ列が出る）。見出しの漏斗で絞り込み（遅れ／計画どおり／早い／計画になかった／比較なし）、見出しで並べ替え（完了日の差。差のないものは最後）。絞り込みは末端の課題で決まる（値は `Session` が比較から `ColumnFilter::planValues` に入れる）。
- **統計**: 「計画との差」の節（件数のタイル 5 つ＝早まり・計画どおり・遅れ・計画になかった・消えた、遅れの大きい順の上位 10 件。行を押すと課題を選んで詳細を開く）。
- **HTML 書き出し（10.7.5）**: 選んだベースラインがあれば、ガントに計画のバー（`class="plan"`）と差の文字・凡例・注記、統計に同じ節を出す（ダイアログのチェックでオフにできる）。**週次レポート（10.7.6）**: 項目「ベースラインとの差」（既定オフ。表示するベースラインがあるときだけ選べる）で、件数・遅れた課題・早まった課題・計画になかった課題・消えた課題を出す（一覧は上限まで）。
- **保存先**: `project.settings.json` の `"baselines"`（B.1）。共有・バックアップ JSON（B.2）には入れない。
- 入れていない: 担当者・工数の写し、進み方の推移（#54 の B）とのスナップショットの共有、ベースラインどうしの比較、ベースラインを書き出して別のプロジェクトで使うこと。暫定判断は docs/spec-questions.md の B42。

### 10.8 絞り込み（ガント・ボード・リスト共通）
状態: `q`（検索語）/ `asg` / `st` / `pr` / `flt`（`late` `tight` `done` `open` のいずれか。状況チップとコマンドパレットから設定）。**いずれかが空でなければ「絞り込み中」**。

**可視集合の計算 `visSet(filter)`**（JS版 `core.js` の同名の関数と同じ）:
1. 各課題 `id` について `textOk(id)` = `q` が空、または `「表示名 + 空白 + 表示ID + 空白 + key + 空白 + ラベルを空白でつないだもの + 空白 + 担当者」` を小文字にしたものに `q`（小文字・前後空白除去）を**部分一致**で含む。
2. ツリーを根から走査する。`m` = 親から引き継いだ一致 `||` `textOk(id)`（つまり**祖先が一致すれば配下は全部一致扱い**）。
3. 子を持つ課題: 子のどれかが可視なら可視。**末端**: `leafOk(id, m)` が真なら可視。
4. `leafOk`: `m` が偽なら偽。`asg`: `\u0000` = 担当者あり / `\u0001` = 未割当 / それ以外 = 担当者名が**完全一致**、に合わなければ偽。`st`: `open` = 完了でない、`todo`/`prog`/`done` = 状況の分類が一致、に合わなければ偽。`pr`: `優先度(空なら Medium)` を小文字にして一致しなければ偽。`flt`: `late` = 未完了かつ余裕<0、`tight` = 未完了かつ 0≤余裕≤10、`done` = 完了、`open` = 未完了。
5. **絞り込み中は、折りたたみを無視して開く**（`open = !collapsed || filtering`）。

**行の列挙 `visibleRows`**: 可視な課題を深さ優先で並べる。`ui.hide[id]` が真のルート（深さ0）は出さない。子の展開は `open` かつ、階層の制限 `level`（0=制限なし、N=深さ N-1 まで）に収まるとき。**ボードとリストの並べ替え中は `visibleLeaves`**（可視な末端の id のみ）。

### 10.9 詳細パネル（右。幅400）
- 上部: 種別アイコン / 表示ID / 状況ピル（押すと状況メニュー）/ 余裕チップ / （余白）/ 操作メニュー（子を追加・同じ階層に追加・階層を下げる/上げる・上へ/下へ・削除）/ 閉じる。
- タブ: **詳細** / **依存関係**（先行+後続の件数を添える）/ **コメント**（件数を添える）/ **添付**（件数を添える。8.9。ドロップの案内・「ファイルを選んで追加」・サムネイルの大きさのスライダー（24〜160。プロジェクトの `ui.attachThumb`）・行（サムネイル。つかんで別の課題へ移動・Ctrl でコピー、一覧の中では並べ替え。載せると SHA-256 の先頭 12 文字）・開く・外す）。行・カードの添付バッジに載せると、先頭 4 件のサムネイル（画像）・名前（画像以外）・「ほか N 件」のツールチップ（8.9）。**同じタブの下に「リンク」の節**（8.10。下の「添付タブ」）。リンクがあるとタブの見出しに「・リンク N」を足す。行・カードには、リンクの数のバッジ（外へ出る矢印のアイコンと数。押すと添付タブ）があり、載せると先頭 6 件と「ほか N 件」のツールチップ。
- 選択がなければ案内「課題を選ぶと、ここで詳しく見られます」。
- 入力欄は**フォーカスが外れたときに確定**、`Enter` で確定、`Esc` で取消（IME中のEnterは無視）。変更がなければ何もしない。

**詳細タブ**:
- パンくず（祖先の件名のボタン列）。件名の大きな入力欄（番号タグを除いた名前）。
- グループ「基本」（2列のグリッド）: 番号（表示用ID。プレースホルダ = key か id）/ 状況（セレクト）/ 担当者（候補つき）/ 優先度 / 種別（Epic・Story・Task・Sub-task・Bug・Milestone）/ ラベル（カンマまたは読点区切り）/ 親（表示ID・key・件名の一部で指定。9.3。空欄 = 最上位。自分の配下は拒否して通知）。
- グループ「日程」: **期間（週）**（末端のみ。−/＋ボタンで1週増減、直接入力は0.5刻み。シナリオが「標準」以外なら見出しに `@シナリオ名`、値があれば「標準に戻す」）。親は「配下の N 件から集計（完了 M）」。**期限**（日付）。**着手可能日**（末端・未完了・固定でない）。**残り（週）**（進行中のみ）。**実績開始**（進行中・完了）。**完了日**（完了）。
- 「計画」の要約: 計画（最早）の開始〜完了 / 最遅の開始〜完了（完了は「—」）/ 余裕（チップ + `N営業日 + 状態`）。
- 注記（該当するとき）: 担当者待ち（「担当者「X」の別の作業が終わるのを待っています。先行だけなら YYYY/M/D(曜) から始められます。」）/ 着手可能日より後になる（「着手可能日の指定（…）より後になります。先行や担当者の都合です。」）/ 親課題（「子があるため、期間と状況は配下から集計されます。…」）。
- グループ「説明」（複数行）。末尾に「{世代名}（N階層目） ／ Issue Id X ／ key」（世代名 = 最上位・子・孫・ひ孫・玄孫・第N世代）。

**依存関係タブ**: 「先行」（これが終わってから始める）と「後続」（これが終わるのを待つ）の2つ。各々、課題のピル（種別・表示ID・件名。クリックで選択、×で削除）の一覧 + 追加欄（表示ID・件名を入力して Enter。9.3 で解決。見つからなければ通知）。親課題でなければ、先行の実効の一覧（親の依存を含む）が直接の先行と違うとき注記を出す。

**コメントタブ**: コメントの一覧（イニシャル、投稿者、日時 `YYYY-MM-DD HH:MM`、本文、削除ボタン）。下に入力欄（`Ctrl+Enter` で投稿）+ 投稿者名（既定 = 設定の `me`）+ 投稿ボタン。

**添付タブ**: ファイルをドロップする案内、件数と使用量（プロジェクト全体と上限）、一覧（種類のアイコン・名前・サイズ・日時・付けた人。ダブルクリックか「開く」で開く。ごみ箱で外す = `_削除済み` へ移す）。JIRA から取り込んだ Attachment 列があれば、その下に「本体はありません」として出す。

**添付タブの「リンク」の節**（8.10。添付ファイルの節の下）: 見出し「リンク N 件」・説明と「URL・パスがプロジェクトの設定ファイルに入ります」の注意・場所の入力欄・表示名の入力欄（省略可）・「リンクを追加」（編集中は「保存」と「やめる」）と「貼り付けて追加」（クリップボードの各行を 1 行 1 リンク。20 行まで）・断った理由（入力欄の下）・一覧（種類のアイコン・表示名・場所を薄く。クリックで開く、行の「開く」、ごみ箱。右クリックで 開く／場所（フォルダ）を開く／コピー／編集／削除／上へ／下へ。つかんで同じ一覧の中で並べ替え、別の課題の行・カード・詳細パネルへ離すと移動、Ctrl でコピー）。`Ctrl+V` は入力欄の中ではふつうに貼り付き、入力欄の外の動き（子課題・画像）は変えない。実行ファイルのリンクは開く前に警告（「実行できるファイルです」）。

**GitHub の PR・Issue のリンク**（8.11）: 専用のアイコン（PR・Issue）。確認済みなら行の右上に状態のラベル（`open` `draft` `merged` `closed（マージなし）` `closed（完了）` `closed（計画外）`。確認中は「確認中…」、失敗は「確認できません」。色だけに頼らない）と、2 行目の右に確認した日時（「確認 10/19 18:36」。保存しない）。右クリックのメニューに「状態を確認」（そのリンクだけ）。入力欄の上に、その課題に GitHub のリンクがあるときだけ「GitHub の状態を確認」（その課題の GitHub のリンクを順に確認。確認中は押せない）と、押したときだけ `gh` で取りに行く旨の説明。

### 10.10 コマンドパレット（`Ctrl+K`）
- 画面中央上寄りに幅 640 のパネル。入力欄（プレースホルダ「コマンドや課題を検索…」）+ 一覧（グループ見出し付き）。
- 一覧 = **コマンド**（下記）+ **課題へ移動**（全課題。入力があるときだけ、一致した先頭8件）。入力語は空白で分割し、**すべての語**が「グループ名 + 題名 + 表示ID」の小文字に部分一致したものを残す。入力が空ならコマンドの先頭40件。
- `↑` `↓` で移動、`Enter` で実行（パレットを閉じてから実行）、`Esc` / 外側クリックで閉じる。IME変換中のキーは無視。
- コマンド（グループ: 題名 [キー]）:
  - 課題: 課題を追加（最上位）/ 選択した課題の子を追加 [Shift+N] / 同じ階層に追加 [N] / テンプレートから子タスクを追加 / 子タスクをテンプレートとして保存 / 名前を変更 [Enter / F2] / 削除 [Del] / 詳細パネルを開く・閉じる [Space]（選択が要るものは選択があるときだけ出す）
  - 表示: ガント [1] / ボード [2] / リスト [3] / ギャップ [4] を表示 / ガントの拡大率（日・週・月・全体）/ バーの色分け（Epic・余裕・担当者）/ すべて開く / すべて閉じる / 遅れだけを表示 / 余裕小だけを表示 / 絞り込みを解除 / 未完了だけを表示 / 担当者が未割当の課題だけを表示 / 密度の切替 / テーマの切替（自動→ライト→ダーク）
  - 計画: 同時作業の制限のオン/オフ / 担当者の負荷を見る / 基準日を今日にする / プロジェクト設定
  - 編集: 元に戻す [Ctrl+Z] / やり直す [Ctrl+Shift+Z]
  - プロジェクト: 新しいプロジェクト / 名前・キーを変更 / 複製 / CSVを取り込む / 書き出し / 全プロジェクトをバックアップ / ショートカット一覧 [?]
  - プロジェクトを開く: 他のプロジェクト名（1件ずつ）

### 10.11 ダイアログ
共通: 画面を暗くして中央に表示。タイトル + 本文 + 下端のボタン列（右寄せ。主ボタンは右端）。`Esc` と外側クリックで閉じる（編集内容が失われる確認ダイアログは例外で、外側クリックでは閉じない）。最初の入力欄にフォーカス。IME中のEnterは確定に使わない。

| ダイアログ | 内容 |
|---|---|
| **新しいプロジェクト**（幅標準） | 名前（既定「新しいプロジェクト」）/ キー（例: DEV）/ 雛形の選択: 空 / 階層デモ（5階層の例）。「作成」。名前欄の Enter で作成。作成したら開く |
| **名前・キーの変更** | 名前 / キー（大文字化して保存）/ 説明「キーは、これから追加する課題の番号に使います。すでにある課題のキーは変わりません。」/ 複製ボタン / 左下に赤い「削除」→ 確認ダイアログ（「「名前」を削除します。…」。削除は プロジェクトのフォルダごと `_削除済み` へ移す旨を書く）|
| **確認**（幅460） | タイトル + 本文 + キャンセル / 実行（危険な操作は赤） |
| **CSVを取り込む**（幅640） | 「ファイルを選ぶ」ボタン（OSのファイルダイアログ）+ 説明 / 貼り付け欄（9行）/ 取り込み方法: 新しいプロジェクトとして追加（名前欄）または 今のプロジェクトを置き換え（Undoで戻せる）/ 「取り込む」。JSON（共有・バックアップ）なら 9.5 / 付録B の規則。同じ `id` のプロジェクトがあれば**競合ダイアログ**（「同じプロジェクトがあります」: 上書き / 別のプロジェクトとして追加 / キャンセル） |
| **書き出し**（幅640） | 形式: JIRAに取り込む用 / このアプリ用CSV（入力した値のまま）/ 共有用JSON（このプロジェクトだけ）。書き出す内容の表示欄（読み取り専用）+ 件数/サイズ。「コピー」「ファイルに保存」（OSの保存ダイアログ。ファイル名 = プロジェクト名 + 拡張子）。「全プロジェクトのバックアップ」のときは形式選択なしで JSON |
| **保存先フォルダ**（幅600） | データフォルダのパス（表示）/ 状態 / 「フォルダを開く」（エクスプローラで開く）/ 「フォルダの変更を読み込む」/ 注意書き（バックアップと削除済みの扱い）。C++版ではフォルダの切り替えはしない（起動引数で決める）。保存エラー時はここにエラーの理由と「再試行」 |
| **プロジェクト設定**（幅620） | 「日程の計算」: 基準日（+「今日」ボタン）/ シナリオ（一覧のボタン + 新しいシナリオ名の入力と追加）/ 担当者ごとの同時作業の制限（チェック + 件数 1〜9）/ 休業日（折りたたみの複数行入力 + 「反映」「既定に戻す」）。「表示」: ガントの列（担当・状況・期間・余裕のチェック）/ 密度 / テーマ。「データ」: 保存先フォルダ / 全プロジェクトのバックアップ / CSVの列の説明（折りたたみ。6.3 の表を要約）|
| **担当者の負荷**（幅720） | 表: 担当者 / 未完了 / 合計(週) / 計画上の同時作業の最大（制限値を超えたら赤）/ 最後の完了予定 / 待たされる作業。注記付き（7.7） |
| **読み込み時の注意**（幅560） | 計算の警告と取り込みの警告を箇条書き。なければ「注意はありません」 |
| **ショートカット一覧**（幅620） | 10.4.4 とその他（編集中のキー、全体のキー）をグループ見出しつきで2列に |
| **テンプレから子タスクを追加**（TplApply） | 追加先（選択した課題の名前。複数なら N件）/ テンプレの選択（組み込み + 利用者）/ 内容のプレビュー（名前・期間・先行）/ 「追加」。利用者のテンプレートには削除ボタン |
| **子タスクをテンプレとして保存**（TplSave） | 名前（必須）/ 説明 / プレビュー / 「保存」 |
| **ベースラインを保存**（幅560） | 名前（既定「YYYY-MM-DD の計画」）/ 保存済みの数 / 注意「件名が設定ファイル（project.settings.json）に入ります（担当者名は入りません）。共有・バックアップ用のJSONには入りません」/ 5 つあるときは理由と「ベースラインの管理…」。「保存」「キャンセル」 |
| **ベースラインの管理**（幅720） | 表示しない / 一覧（表示・名前・保存した日時・課題の数・「名前の変更」「削除」）/ 「いまの日程を保存…」。削除は確認のダイアログ（幅460）を経て一覧に戻る |
| **はじめに** | 8.7。データフォルダが空のときの初回画面 |

### 10.12 テーマ・密度・トークン
`theme`: `auto`（OSのダーク設定に従う）/ `light` / `dark`。`density`: `normal` / `compact`（行 30→26、上部バー 48→44、ツールバー 40→36）。

**色トークン**（ライト / ダーク）:
| トークン | ライト | ダーク | 用途 |
|---|---|---|---|
| `--bg` | #f5f6f8 | #0e1116 | 背景 |
| `--surface` / `--surface2` / `--surface3` | #ffffff / #f4f5f7 / #e9ecf0 | #161b22 / #1b222b / #252d38 | 面（パネル/ホバー/強調） |
| `--ink` / `--muted` | #1a2230 / #667085 | #e6e9ee / #8b95a5 | 文字 / 補助文字 |
| `--line` / `--line2` | #e4e7ec / #cfd4dc | #262e39 / #37414e | 罫線 / 入力欄の枠 |
| `--accent` / `--accent-ink` / `--accent-bg` | #2563eb / #fff / #e8f0ff | #6ea0ff / #0b1220 / #1a2a48 | 強調（青） |
| `--late` / `--late-bg` | #d92d20 / #fdecea | #ff7b6f / #3a1d1b | 遅れ（赤） |
| `--tight` / `--tight-bg` / `--tight-edge` | #b54708 / #fff1dc / #e08a1e | #f0b34a / #382a10 / #f0b34a | 余裕小（橙） |
| `--ok` / `--ok-bg` | #12805c / #e3f5ec | #58c987 / #16301f | 余裕あり（緑） |
| `--c0`〜`--c7`（ルート課題の色） | #7d8798 #b5667f #5b86d6 #3f8f88 #8c78c6 #a38a4a #4793b3 #a56db0 | #8f9aae #d0809b #6c98ea #4fb0a7 #a891e0 #c5a85c #5db0d2 #c68bd0 | Epic・担当者の色分け |
| `--hover` / `--selbg` / `--rowline` | rgba(26,34,48,.04) / rgba(37,99,235,.10) / rgba(26,34,48,.06) | rgba(230,233,238,.05) / rgba(110,160,255,.16) / rgba(230,233,238,.06) | 行のホバー/選択/行線 |
| `--wkend` / `--holbg` / `--arrow` | rgba(26,34,48,.035) / rgba(26,34,48,.06) / #98a2b3 | rgba(230,233,238,.035) / rgba(230,233,238,.07) / #667085 | 土日/休業日の帯/矢印 |

- 赤・橙は「遅れ」「余裕小」のときだけ使う（装飾に使わない）。
- 間隔は 4 の倍数（4/8/12/16/24）。角丸 6（バーは4）。
- フォント: 本文 13px、日付・数値は等幅（`Consolas` 等。数字は等幅で桁をそろえる）。日本語フォントは 3.4 の順に読み込む。
- 1行 = 余裕の表示は、遅れ=「−N週」（N = `max(1, round(|余裕|/5))`）、それ以外=「N日」（営業日）、完了=「完了」。

---

## 11. 非機能要件

| 項目 | 基準 |
|---|---|
| 起動 | 5,000件のプロジェクト1つを含むデータフォルダで、起動から操作可能まで 2 秒以内（SSD、フォント読み込み込み） |
| 計算 | `buildTree` + 最早 + 最遅 + 余裕（資源制限オフ）が 5,000件で 50ms 以内、オンでも 200ms 以内（リリースビルド） |
| 描画 | 5,000行のガントで、スクロール・ドラッグが 30fps 以上。**可視範囲の行だけ**を描く（仮想化）。矢印も可視範囲に関係するものだけ |
| 編集の反映 | キー入力・ドラッグ後、次のフレーム（16ms 目安）で画面に反映。再計算が 50ms を超えそうなときもUIを固めない（ドラッグ中は離したときだけ再計算） |
| メモリ | 5,000件で 200MB 以内 |
| 堅牢性 | 不正なCSV・JSON・巨大ファイル（50MB）で**クラッシュしない**（エラー表示のみ）。例外は最上位で捕まえて、通知を出し、直前の状態に戻す。保存中に電源が切れても、CSVが壊れない（8.4） |
| データ保全 | 書き込みに失敗してもメモリ上のデータを捨てない。終了時に未保存があれば確認する。異常終了の後も、最後に成功した保存の内容が残る |
| 文字 | 内部は UTF-8。CSV読み込みは UTF-8（BOM可）→ 不正なら Shift_JIS（CP932）→ それも不正なら UTF-8 の置換。**書き出しは UTF-8 BOMなし**（6.1） |
| 国際化 | 画面の文言は日本語のみ。文言は1か所（`strings.hpp` 等）に集め、将来の翻訳を妨げない |
| アクセシビリティ | キーボードだけで主要操作ができる（10.4）。コントラスト比 4.5:1 以上（本文）。色だけで状態を伝えない（遅れ/余裕小は文字でも示す） |
| ログ | `--verbose` でデバッグログを標準エラーへ。通常は何も出さない。個人の課題の中身はログに出さない |
| 通信 | **ネットワーク通信は一切しない**（更新確認・テレメトリも） |
| ライセンス | 依存のライセンスを `THIRD_PARTY.md` に列挙する。商用利用可のものだけを使う |

---

## 12. テストと品質

### 12.1 方針
- **正解データ先行（golden-first）**: 第6〜7章の実装は、`golden/` の照合テストを**先に**書き、赤（失敗）にしてから実装して緑にする。仕様文と食い違ったら golden が正（0.2）。
- **core と cli は Linux でもビルド・テストできる**ように、Win32 や ImGui を `include` しない（3.1）。CI では Linux（GCC/Clang + ASan/UBSan）と Windows（MSVC）の両方で `core` のテストを回す。GUI は Windows だけ。
- 1つの機能を入れたら、同じ変更の中でテストも入れる。テストのない `core` の関数を増やさない。

### 12.2 正解データ（golden）の照合
`golden/` のファイルと、それぞれの照合の方法:

| ファイル | 入力と条件 | 照合 |
|---|---|---|
| `{sample}.base.json` | `samples/{sample}.csv`、基準日 2026-10-19、シナリオ「標準」、資源制限オフ、休業日 = `holidays.txt` | `rows` を**配列の順（深さ優先）・全フィールド**で比較 |
| `{sample}.level1.json` | 同上 + 資源制限オン・同時1件 | 同上 |
| `{sample}.level2.json` | 同上 + 資源制限オン・同時2件 | 同上 |
| `{sample}.base_1102.json` | 基準日 2026-11-02 | 同上 |
| `scenario_demo.scn_short.json` | シナリオ「短縮案」（`scenario_demo` だけ） | 同上。`scenarios` 配列も一致 |
| `{sample}.roundtrip.csv` | `samples/{sample}.csv` を取り込んで「アプリ用」で書き出す（基準日 2026-10-19） | **バイト単位で一致**（改行コードも） |
| `{sample}.jira.csv` | 同じものを「JIRA用」で書き出す（基準日 2026-10-19、資源制限オフ、シナリオ「標準」） | **バイト単位で一致** |

`{sample}` = `scenario_demo`（シナリオ・複数のラベル・複数の依存・期限の確認用。画面には出さないテスト専用）/ `hierarchy_demo` / `leveling_demo`。

**JSON の `rows` の各フィールドと比較の規則**:
| フィールド | 型 | 規則 |
|---|---|---|
| `id` `did` `name` | 文字列 | 完全一致 |
| `depth` `float` `n` `nDone` | 整数 | 完全一致 |
| `leaf` `waited` `pinned` | 真偽 | 完全一致 |
| `cat` | `todo`/`prog`/`done` | 完全一致 |
| `kind` | `task`（末端の通常課題）/ `ms`（末端のマイルストーン）/ null（親課題） | 完全一致 |
| `asapStart` `asapFinish` `alapStart` `alapFinish` | `YYYY-MM-DD` | 完全一致 |
| `effectiveDeadline` `ownDeadline` | `YYYY-MM-DD` / null | 完全一致 |
| `status` | `遅れ` / `余裕小` / `OK` / `完了`（7.6。文字列そのもの） | 完全一致 |
| `weeks` | 数値 / null | 誤差 1e-9 まで |
| `days` | 整数 / null | 完全一致 |
- JSON の `warns` は件数と、各文字列の完全一致（順序も）。
- 照合に失敗したとき、**最初に食い違った行の `did` とフィールド名、期待値と実際の値**を出す（デバッグしやすいように）。

**追加の golden を作る手順**（0.3.1 で `reference-js/` を削除したため、今は使えない。当時の手順）: JS版を Playwright で開き、`window.__rm.ops.doImport(csv, name, 'new')` の後に `setProject({base, scn}, true)` / `setRes({on, cap})` を呼んで、`Engine.compute` の結果と `exportText('app' | 'jira')` を保存する（JS版を改変する場合に限る。C++版の開発では不要）。

### 12.3 単体テストの必須項目
各章の規則に対応する、**最低限**のテスト:

- **calendar**: `dow` が負の日でも正しい / 月末・うるう年 / `workday(n,k)` の前進・後退・k=0 / 休業日をまたぐ / `lastWork` / `floatDays` の正負 / 休業日テキストの区切り4種と不正トークンの無視。
- **csv 字句解析**: 引用符内の改行・カンマ・`""`、BOM、CRLF/LF/CR 混在、末尾に改行なし、空行、列数が不揃い、巨大セル、区切りがタブ（TSV）。
- **見出し対応**: 6.3 の同義語表の**すべて**、大文字小文字・前後空白の違い、同じ見出しの複数列（Labels・Comment・Blocks）、未知の列の保持と書き戻し。
- **値の読み取り**: 日付の各書式、期間（`2w 3d`、`1.5`、`40h`、`3d`）、不正値は null、優先度・状況の正規化。
- **tree**: 親子・循環の検出・存在しない親の扱い・存在しない依存先の無視・重複 id・親への依存の末端への展開・期限の継承。
- **engine**: 最早/最遅/余裕/親のまとめ/マイルストーン/完了・進行中/着手可能日/資源制限（cap 1・2）/シナリオ/担当者待ちの `waited`/`freeStart`/負荷 `workload`/`summary`。golden でカバーされない境界（空プロジェクト、末端が1件、すべて完了、依存の循環でエラー）。
- **commands**: 9.2 の全操作（成功・拒否の両方）、`mutate` の失敗時の巻き戻し、Undo/Redo（100件の上限・Redoの消去）、`bulk` が Undo 1回で戻る、`applyTemplate` の番号タグ採番。
- **filter**: 10.8 の全組み合わせ（検索×担当×状況×優先度×`flt`）、祖先一致、絞り込み中の展開、`hide`、`level`。
- **store**: 原子的書き込み（途中で失敗しても元のファイルが残る）、バックアップの作成とプロジェクトごとの30個の上限、`_削除済み` へのフォルダごとの移動と戻し、フォルダ名の衝突回避（`-2`）、`settings.json` の未知キーの保持、外部変更の取り込みの3ケース（8.5）、不正なCSVを上書きしない、平置きの互換読みとフォルダ構成への移行（バックアップ・バイト一致・失敗時に元を消さない。8.8）、添付の置き場所とパスの長さの規則（8.2）。
- **文字コード**: UTF-8 / UTF-8 BOM / Shift_JIS の読み込みと、UTF-8（BOMなし）での書き出し。

### 12.4 性質のテスト（property-based）
乱数で課題ツリーと依存を生成して（シード固定。失敗したらシードを表示）、次の性質を確認する:
1. **往復の安定**: `export(import(export(x))) == export(x)`（アプリ用）。
2. **Undo の完全性**: 任意の操作列の後、全部 Undo すると初期状態の `export` と一致する。
3. **計算の不変条件**: 末端で未完了の課題について、`asapStart ≤ asapFinish`、`alapStart ≤ alapFinish`、`float ≥ 0 ⇔ alapFinish ≥ asapFinish`、先行 `p` → 後続 `s` で `asapStart(s) > asapFinish(p)`（営業日で）。資源制限オフなら `waited` は常に偽。
4. **資源制限の単調性**: 制限をオンにしても、どの課題の最早完了も、オフのときより**早くならない**。
5. **フィルタの整合**: 可視な末端の祖先はすべて可視。

### 12.5 UIの受け入れ確認（手動チェックリスト）
各フェーズの終わり（13章）に、`golden/samples/hierarchy_demo.csv`（または `scenario_demo.csv`）を読み込んで確認する。1つでも崩れたら直す。

1. 起動して、日本語が欠けずに出る（常用漢字・全角記号・半角カナ）。150%・200%表示でも崩れない。
2. 件名をダブルクリック → 日本語を変換して `Enter` で**確定**（行が増えたり編集が終わったりしない）→ もう一度 `Enter` で編集終了。
3. 追加直後の行で、続けて `Enter` を押すと次の行が増える。`Shift+Enter` で子、`Ctrl+Enter` で同じ階層。
4. `Tab` / `Shift+Tab` で階層が変わる。複数選択でも変わる。
5. バーをドラッグして日程が変わり、離したあと矢印・余裕が更新される。先行より前には動かせない旨の通知が出る。
6. 行のつまみをドラッグして、前・後ろ・子としての移動ができる。自分の子孫の上には置けない。
7. ボードでカードを別の列にドラッグすると状況が変わる。
8. 右クリックメニューの全項目が動く。
9. 担当者・状況・優先度・検索語で絞り込める。絞り込み中は折りたたみが開く。解除ボタンで戻る。
10. `Ctrl+K` で、コマンドと課題が検索できる。日本語入力中の `Enter` で誤実行しない。
11. `Ctrl+Z` / `Ctrl+Shift+Z` が、追加・削除・移動・日程変更・一括編集で効く。
12. 5,000件のCSV（`rmcli gen` で生成）でスクロール・編集・ドラッグが重くない（11章）。
13. ウィンドウを最小サイズ（960×600）にしても操作できる。
14. ダーク/ライト/自動、標準/コンパクトの切替で崩れない。
15. データフォルダのCSVをExcelで編集して保存 → アプリに戻ると反映される。アプリ側に未保存の編集があれば「（フォルダの版）」が別プロジェクトとして増える。
16. データフォルダを書き込み禁止にすると、保存エラーのバナーが出て、解除後に自動で再試行されて消える。
17. 終了時に未保存があれば確認が出る。
18. `rmcli check-jira` が、書き出したJIRA用CSVでエラーなし。

### 12.6 CI（GitHub Actions）
- `windows-latest`: vcpkg のバイナリキャッシュ → CMake（Ninja, Release, `/MT`）→ `ctest` → exe をアーティファクトに。
- `ubuntu-latest`: `core` と `cli` のみ。GCC と Clang、`-Wall -Wextra -Werror`、ASan + UBSan で `ctest`。
- `main` への push でどちらも緑でなければ、マージしない。タグ `v*` で exe とチェックサムをリリースに添付。

---

## 13. 実装フェーズ

各フェーズは**独立して動く状態**で終える（ビルドが通り、テストが緑）。受け入れ条件を満たしてから次へ。

| フェーズ | 内容 | 受け入れ条件 |
|---|---|---|
| **P0 環境とスパイク** | リポジトリ骨格（2.2）、CMake/vcpkg、CI の雛形、`CLAUDE.md`（付録C）。**UIスパイク**: 3.4 の6項目を ImGui で確かめる使い捨てアプリ | CI がビルドを通す。スパイクの結果を `docs/ui-spike.md` に記録し、**採用するUI（ImGui か Qt）を決める**。決まるまで P5 以降に進まない（P1〜P4 は並行して進めてよい） |
| **P1 日付とCSV** | `calendar`、`model`、`csv`（5・6章）。`rmcli import-export` | `roundtrip.csv` と `jira.csv` が 3サンプルすべてでバイト一致。12.3 の calendar・csv・見出し・値の項目が緑 |
| **P2 エンジン** | `tree`、`engine`（7章）。`rmcli schedule` が JSON を出す | `*.base.json` `*.level1.json` `*.level2.json` `*.base_1102.json` `*.scn_nb.json` がすべて一致。5,000件の計算が 50ms 以内（11章）。12.4 の性質テスト緑 |
| **P3 編集・保存・テンプレ** | `commands`、`filter`、`templates`、`store`（8・9・10.8章） | 12.3 の commands・filter・store・文字コード・テンプレの項目が緑。Undo の完全性（12.4-2）緑。外部変更の取り込み3ケースが緑 |
| **P4 CLI** | `rmcli`: `schedule`、`import-export`、`check-jira`（付録E）、`gen`（テスト用の大きなCSVを生成）、`validate`（CSVが読めるかと警告の一覧） | 全コマンドに `--help`。終了コードは成功 0 / 入力の誤り 2 / 内部エラー 1。`check-jira` が 3サンプルの `jira.csv` で OK |
| **P5 アプリ基盤とリスト** | ウィンドウ、フォント、IME、高DPI、テーマ、`AppController`、上部バー、ツールバー、**リストビュー**、その場編集（10.4）、選択、コンテキストメニュー、通知、保存状態、プロジェクト切替、はじめに | 12.5 の 1〜4, 8〜11, 13〜17 が通る（リストで）。データが保存され、Excelで編集した内容を取り込める |
| **P6 ガント・詳細・ボード・ギャップ** | ガント（10.5）、詳細パネル（10.9）、ボード（10.6）、ギャップ（10.7.3）、ゴールのポップオーバー | 12.5 の 5〜7, 12 が通る。5,000件で 30fps 以上 |
| **P7 仕上げと配布** | コマンドパレット（10.10）、全ダイアログ（10.11）、取り込み/書き出し、全バックアップ、設定、負荷、ショートカット一覧、アイコン、バージョン情報、単一exe化、`README.md`、`THIRD_PARTY.md` | 12.5 の全項目。exe が 10MB 以下で、まっさらな Windows 11 で動く（追加のランタイム不要） |

- 各フェーズの終わりに、変更点・未解決の点を `docs/CHANGELOG.md` に書く。判断に迷って仕様に書かれていない点は、**仕様書の該当箇所に追記する提案**を `docs/spec-questions.md` に書き、暫定の判断で進める（止まらない）。
- **P0 で ImGui を採用した場合**: 即時モードでは、毎フレームの全行の描画を避ける（可視範囲のみ）。テキスト編集中の IME は、プラットフォーム層（`ImGui_ImplWin32` の IME 対応 + `io.SetPlatformImeDataFn`）で候補ウィンドウの位置を合わせる。

---

## 14. 未確定事項・実装者への注意

仕様を決め切れていない点。**暫定の判断で進め、決めたことを `docs/spec-questions.md` に書く**。

1. **UIフレームワーク**: 3.4 のスパイク結果で決める。Qt に切り替える場合、10章の「ImGuiの即時モード前提の記述」（毎フレーム再描画など）は読み替える。
2. **日本語の並べ替え**: JS版は `localeCompare("ja")`（ICU）。C++版は「Unicode のコードポイント順」を既定とし、**かな（ひらがな/カタカナ）と漢字の並びが自然になる**簡易照合（カタカナ→ひらがなに正規化してから比較）を入れてよい。担当者の並びとリストの並べ替えだけに影響し、golden には出ない。
3. **休業日の既定**: 33日（2026-11-03 〜 2028-03-20）しかない。それ以降の祝日は利用者が設定で追加する。祝日の自動生成は**しない**（法改正で変わるため）。
4. **「今日」の扱い**: 状況を変えるときの `resolved` / `start` は `min(今日, 基準日)`（9.2）。時計は `IClock` で差し替えられるようにし、テストでは固定する。
5. **ファイル監視**: 8.5 の `ReadDirectoryChangesW` は任意。入れる場合も、自分の書き込みによる通知を無視すること（`mtime` が自分の記録と同じなら無視）。プロジェクトのフォルダの中も見るため、サブフォルダも監視する。
6. **コメントの日時書式**: JIRAの `dd/MMM/yy hh:mm a`（英語の月名・AM/PM）は、ロケールに依存せず自前で整形する（`std::put_time` を使わない）。
7. **大きなファイル**: 50MB を超えるCSVは「大きすぎる」として拒否し、読み込みは試みない。
8. **JS版との既知の差**: 1.4 の表のみ。それ以外の差を見つけたら、`docs/spec-questions.md` に書いて**JS版に合わせる**（意図した変更と区別するため）。
9. **アイコン**: JS版のSVGパス（`reference-js/src/components/ui.jsx` の `PATHS`）をそのまま描画してよい（ImGui なら `ImDrawList` でパスを描く）。ビットマップのアイコンは使わない。
10. **印刷**: 範囲外。必要なら、ガントのPNG/PDF出力を将来の拡張にする。

---

## 付録A. 組み込みテンプレート（子タスクの雛形）

形式は 9.4。`lab` の `期間仮置き` は、期間が仮置きであることを示すラベル。`{名前}` は追加先の表示名に置き換える。

```json
[
  { "id": "b:doc", "builtin": true, "name": "外部認証向けの文書対応",
    "desc": "内部レビュー → 英訳 → 外部提出 → 外部指摘の対応",
    "items": [
      { "n": "内部レビューと指摘修正", "w": 1,   "lv": 0, "after": [],  "lab": ["期間仮置き"] },
      { "n": "英訳",                   "w": 1,   "lv": 0, "after": [0], "lab": ["期間仮置き"] },
      { "n": "外部認証機関への提出",   "w": 0.2, "lv": 0, "after": [1], "lab": ["期間仮置き"] },
      { "n": "外部指摘への対応とクローズ", "w": 2, "lv": 0, "after": [2], "lab": ["期間仮置き"] } ] },
  { "id": "b:eval", "builtin": true, "name": "評価の実施と報告",
    "desc": "評価方案 → 評価実施 → 報告書 → 提出と指摘クローズ",
    "items": [
      { "n": "評価方案の作成",   "w": 1, "lv": 0, "after": [],  "lab": ["期間仮置き"] },
      { "n": "評価の実施",       "w": 1, "lv": 0, "after": [0], "lab": ["期間仮置き"] },
      { "n": "評価報告書の作成", "w": 1, "lv": 0, "after": [1], "lab": ["期間仮置き"] },
      { "n": "評価報告書の提出と指摘クローズ", "w": 1, "lv": 0, "after": [2], "lab": ["期間仮置き"] } ] },
  { "id": "b:review", "builtin": true, "name": "作成 → レビュー → 承認",
    "desc": "成果物をつくり、レビューして承認を得る基本形",
    "items": [
      { "n": "{名前}の作成", "w": 2,   "lv": 0, "after": [],  "lab": ["期間仮置き"] },
      { "n": "レビュー",     "w": 1,   "lv": 0, "after": [0], "lab": ["期間仮置き"] },
      { "n": "指摘の修正",   "w": 1,   "lv": 0, "after": [1], "lab": ["期間仮置き"] },
      { "n": "承認",         "w": 0.2, "lv": 0, "after": [2], "lab": ["期間仮置き"] } ] },
  { "id": "b:publish", "builtin": true, "name": "公開用の原稿づくり",
    "desc": "原稿作成 → レビュー → 校了 → 公開（印刷）",
    "items": [
      { "n": "公開用原稿の作成", "w": 2,   "lv": 0, "after": [],  "lab": ["期間仮置き"] },
      { "n": "レビュー",         "w": 1,   "lv": 0, "after": [0], "lab": ["期間仮置き"] },
      { "n": "校了",             "w": 0.4, "lv": 0, "after": [1], "lab": ["期間仮置き"] },
      { "n": "公開・印刷",       "w": 0.4, "lv": 0, "after": [2], "lab": ["期間仮置き"] } ] }
]
```
利用者のテンプレートは同じ形式（`id` は `u:` + 乱数、`builtin` なし）で `roadmap_prefs.json` の `templates` に入れる。`items` の省略可能なキー: `t`（種別）、`lab`（ラベル）、`d`（説明）。

## 付録B. ファイル形式

### B.1 `project.settings.json`（平置きでは `プロジェクト名.settings.json`）
JS版は使わなくなったため、JS版との互換は求めない（形式は JS版の設定と同じ）。
```json
{
  "app": "regroadmap",
  "version": 3,
  "id": "pmgx3k2ab1c",
  "name": "サンプルプロジェクト",
  "key": "SMP",
  "base": "2026-10-19",
  "hol": "2026-11-03\n2026-11-23\n…",
  "scn": "標準",
  "scenarios": ["短縮案"],
  "res": { "on": false, "cap": 1 },
  "ui": {
    "view": "gantt", "zoom": "week", "level": 0, "lane": "none", "colorBy": "root",
    "cols": { "asg": true, "status": true, "dur": true, "float": true },
    "nameW": 300, "colW": { "asg": 84 },
    "collapsed": { "<課題id>": true }, "hide": { "<ルート課題id>": true }
  },
  "attachments": { "<Issue Id>": [ { "name": "仕様書.pdf", "size": 12345, "at": "2026-10-20T09:30", "who": "山田" } ] },
  "weekly": {
    "notes": "所感・課題・お願い（Markdown）",
    "snaps": [ { "base": "2026-10-19", "progress": 0.42, "tasks": 13, "done": 5, "late": 2, "tight": 1, "finish": "2026-12-01", "truncated": false,
                 "items": [ "<Issue Id>\t<t|p|d>\t<見込みの完了日 or 空>\t<遅れ 0|1>" ] } ]
  }
}
```
- **フィールド名の対応**（4.1 の `ProjectSettings` ↔ JSON）: `baseDate` ↔ `base`、`holidays` ↔ `hol`（改行区切りで書く。読むときは5.2の区切りすべて）、`scenario` ↔ `scn`、`resource` ↔ `res`。`scenarios` は「標準」以外のシナリオ名（CSVの `@名前` 列にないものも含む。両者の和集合を使う）。
- `ui.collapsed[id] == true` は**閉じている**。`hide[id] == true` はそのルート課題を表示しない。`view` は `gantt`/`board`/`list`/`goals`、`zoom` は `day`/`week`/`month`/`all`、`lane` は `none`/`asg`/`root`、`colorBy` は `root`/`float`/`asg`。
- 読むとき、欠けているキーは既定値（`defaultUI`: zoom=week, level=0, view=gantt, lane=none, colorBy=root, cols=すべて true, nameW=300）で補う。**未知のキーは捨てずに保持**して書き戻す。`id` がなければ新しく振る。`name` がなければ stem。
- `attachments` は添付のメタ情報（8.9。`name`・`size`・`at`・`who` と、あれば `sha256`）。空なら書かない。
- `links` は関連リンク（8.10）。`{ "<Issue Id>": [ { "title", "target", "at" } ] }`（`at` は追加した時刻の UTC ミリ秒）。空なら書かない。共有・バックアップ JSON（B.2）には入れない。読むとき、型・Issue Id・開けない場所・重複の壊れた値は読み飛ばす（1 課題 50 件・全体 5,000 件・表示名 200 バイト・URL 2,048 バイト・パス 1,024 バイトまで）。項目の未知のキーは保持して書き戻す。
- `baselines` はベースライン（10.7.7）。`{"shown": 表示する id, "list": [{"id", "name", "at", "truncated", "items": [...]}]}`。`items` は 1 課題 1 行の文字列（タブ区切り `Issue Id・状況(t/p/d)・親(0/1)・開始・完了・期限・件名`）。空なら書かない。共有・バックアップ JSON（B.2）には入れない。読むとき、型・日付・状況・id・重複の壊れた値は読み飛ばす（5 つ・1 つ 5,000 件・Issue Id 64 バイトまで。名前の制御文字は除く）。未知のキーは `unknown` に保持して書き戻す。
- `history` は状況の件数の履歴（10.7.4。Issue #64）。`{"points": [{"week": 月曜の ISO 日付, "todo", "doing", "done", "total"}]}`（週の昇順・最大 104 週）。空なら書かない。共有・バックアップ JSON（B.2）には入れない。読むとき、型・日付・月曜でない日付・範囲（0〜10,000,000）・負数・合計の食い違い・重複・順序が逆の点は読み飛ばし、104 週を超えれば古いものから落とす。未知のキーは `unknown` に保持して書き戻す。`weekly`・`baselines` とは別のキー（保存を共有しない）。
- `weeklyAuto` は週の頭の自動スナップショット（10.7.6。Issue #70）。`{"snaps": [スナップショット…]}`（スナップショットの形は `weekly.snaps` と同じ。基準日の昇順・週ごとに 1 つ・最大 8 週）。空なら書かない。共有・バックアップ JSON（B.2）には入れない。読むとき、型・日付・範囲・id の壊れた値は 1 つずつ読み飛ばし、未知のキーは壊さない。0.9.x は未知のキーとして書き戻す（`weekly` の中身は自動の有無で変わらない）。
- `weekly` は週次レポートの所感とスナップショット（10.7.6）。空なら書かない。共有・バックアップ JSON（B.2）には入れない。読むとき、型・日付・範囲・id の壊れた値は読み飛ばす（スナップショット 1 つ 5,000 件・id 64 バイト・所感 20,000 バイトまで）。課題は 1 件 1 行の文字列（タブ区切り）で持つ。
- 書くときは `JSON.stringify(…, null, 1)` 相当（インデント1スペース、UTF-8、BOMなし）。キー順は上の通り。

### B.2 共有JSON・バックアップJSON
```json
{
  "app": "regroadmap",
  "version": 3,
  "exported": "2026-10-19T09:00:00.000Z",
  "projects": [
    { "v": 3, "id": "pmgx3k2ab1c", "name": "…", "key": "REG", "base": "2026-10-19", "hol": "…",
      "scn": "標準", "scenarios": [], "ui": { }, "res": { "on": false, "cap": 1 },
      "csv": "Issue Id,Issue Type,Summary,…\n1,Epic,…\n" }
  ],
  "templates": [ /* 利用者のテンプレート（付録A） */ ]
}
```
- 共有 = `projects` が1件、バックアップ = 全件。インデント1スペース。
- **取り込み**: `app == "regroadmap"` かつ `projects` が配列で、`csv` を持つ要素だけ使う。`id` が既存のプロジェクトと同じなら「競合」（上書き / 別のプロジェクトとして追加 / キャンセル）。上書きしないときは新しい `id` を振る。`templates` のうち、`id` と `name` と `items` があり、既存の `id` と重ならず、`b:` で始まらないものだけ追加する。最初に取り込んだプロジェクトを開く。通知「N件のプロジェクトを読み込みました（テンプレート M件を含む）」。
- 注: JS版の `id` は `p` + 時刻の36進数 + 乱数4文字（例 `pmgx3k2ab1c`）。C++版は**任意の文字列**として扱い、新規は同じ形式で作る。

### B.3 `roadmap_prefs.json`
（`autoCompleteGitHub`: GitHub のリンクが閉じられたら自動で完了にする（8.11）。真偽。既定は偽。）
8.6 のとおり。`theme`、`density`、`me`、`templates`、`currentProject`（最後に開いたプロジェクトの id）、`window`、`hintDismissed`。未知のキーは保持する。

## 付録C. `CLAUDE.md`（案）

リポジトリ直下に置く。Claude Code が毎回読むので、短く保つ。

```markdown
# Project Roadmap (C++)

JIRA CSVを読み、営業日でスケジュールを計算し、ガント・ボード・リスト・ギャップで見せるデスクトップアプリ。
仕様は docs/SPEC.md、正解データは golden/。

## ルール
- 仕様と golden が食い違ったら golden が正。仕様に書かれていない点は暫定判断で進め、docs/spec-questions.md に書く（止まらない）。
- core/ と cli/ は GUI・Win32・ImGui を include しない。Linux でもビルド・テストできること。
- まず golden の照合テストを書き、赤を確認してから実装する（TDD）。
- std::regex は使わない。文字列は UTF-8 の std::string。ファイル名・パスは std::filesystem::path（u8path / char8_t に注意）。
- 例外はUI層の最上位だけで捕まえる。core はエラーを戻り値（Result 型）で返す。
- 乱数・時計・ファイルシステムは注入できるようにする（IClock, IFileStore）。
- 画面の文言は日本語。コード中のコメントも日本語でよい。
- 依存を足すときは理由をPR/コミットに書く。

## ビルドとテスト
- 構成: `cmake --preset windows-release` / `cmake --preset linux-asan`（core+cli のみ）
- ビルド: `cmake --build --preset <preset>`
- テスト: `ctest --preset <preset> --output-on-failure`
- golden だけ: `ctest -R golden`

## フェーズ
docs/SPEC.md 第13章の P0〜P7 を順に。各フェーズの受け入れ条件を満たしてから次へ。完了ごとに docs/CHANGELOG.md を更新。

## 触らないもの
- golden/（正解データ。更新するときはユーザーに確認）
```

## 付録D. JS版の癖（再現すること）

JS版の挙動のうち、見落としやすいもの。**意図的な仕様として再現する**。

1. **IME**: キー入力イベントの `isComposing` が真、または `keyCode == 229` の間の `Enter` は無視する（確定のEnterを編集の確定と取り違えない）。C++版では、IMEの変換中フラグ（`WM_IME_COMPOSITION` から `GCS_COMPSTR` が続いている間）で判定する。
2. **絞り込みの特殊値**: 担当者の絞り込みは、空 = 全員、「担当者あり」「未割当」、それ以外 = 名前の完全一致。JS版は `\u0000` / `\u0001` の文字を使っているが、C++版は**列挙型**で持つ。
3. **優先度が空の課題**は、絞り込みでは `Medium` として扱う。
4. **絞り込み中の展開**: 絞り込みが効いているあいだ、折りたたみは無視して全部開く（設定の `collapsed` は変えない）。
5. **祖先の一致**: 検索語が親の名前に一致すれば、その配下の末端はすべて一致扱い。
6. **状況の分類**は前方一致（`done` 系）と部分一致（`prog` 系）が混在する（4.4）。`Done` の判定が `Doneness` のような文字列でも真になるが、そのまま再現する。
7. **ドラッグで日程を動かす丸め**: 右へ動かしたら `workday(ns-1, 1)`、左へなら `lastWork(ns)`（10.5.4）。休業日・土日にバーの端が乗らない。
8. **番号タグの重複**は、表示IDどうしを**大文字小文字を無視**して比べて拒否する。
9. **コメントの `;`** は全角 `；` に置き換えて保存する（JIRAの区切りと衝突するため）。
10. **Milestone の書き出し**: JIRA用ではラベル `Milestone` を足し、種別は Task 系にする。取り込み時、ラベルの `Milestone` / `マイルストーン` でも Milestone 扱いにする。
11. **ボードの列**: 状況の文字列を、大文字小文字を無視して列に対応づける。`status` が空のカードは先頭列（To Do）。
12. **期限の列がない CSV** では、後続のない課題の `Due date` を期限として扱う（`hasDeadlineCol == false`）。期限を編集した瞬間に、すべての `Due date` 由来の期限を `deadline` へ移す（9.2）。
13. **書き出しは UTF-8（BOMなし）**。読み込みは UTF-8 → Shift_JIS のフォールバック。
14. **プロジェクトを開き直すたび**に Undo 履歴は消える。
15. **基準日・休業日・シナリオ・資源制限・表示の変更**は Undo に入らない（9.1）。

## 付録E. `rmcli check-jira` の規則

JIRAの標準のCSV取り込みで問題になる点を機械的に検査する。入力 = 「JIRA用」で書き出したCSV（または、アプリのCSVを読んで内部で書き出したもの）。出力は1ファイルにつき1行「`名前: N件 / 列 M / OK`」または「`NG K件`」と、続けて問題点（最大12件）。全ファイルがOKなら終了コード 0。

- **標準列**: `Issue Id` `Issue key` `Issue Type` `Summary` `Parent` `Epic Name` `Epic Link` `Priority` `Status` `Assignee` `Start date` `Due date` `Resolved` `Original estimate` `Labels` `Description` `Outward issue link (Blocks)` `Comment`。
- **アプリ独自列**: `Earliest start` `Deadline` `Remaining (weeks)` `Duration (weeks)` `Duration (weeks) @…`。

| 検査 | エラーにする条件 |
|---|---|
| 列の並び | 標準列が、最初のアプリ独自列より**後ろ**にある |
| 不明な列 | 標準列でもアプリ独自列でもない |
| Issue Id | 重複 |
| Epic Name | Epic どうしで重複 |
| 種別 | `Epic` `Task` `Sub-task` `Story` `Bug` 以外 |
| Summary | 空、または 255 文字を超える（文字数は Unicode のコードポイント数） |
| Epic | `Epic Name` が空 |
| Sub-task | `Parent` が空 |
| Parent | 存在しない id / 親が Sub-task（Sub-task の下に子） / Sub-task の親が Epic / Sub-task でない課題の親が Epic でない |
| 優先度 | 空でなく、`Highest` `High` `Medium` `Low` `Lowest` 以外 |
| ラベル | 空白（任意の空白文字）を含む |
| 見積 | 空でなく、`^(\d+w)?( ?\d+d)?( ?\d+h)?( ?\d+m)?$` に合わない（手書きで判定） |
| 日付 | `Start date` `Due date` `Resolved` が空でなく `YYYY-MM-DD` でない |
| Blocks | 参照先が存在しない、または自分自身 |
| コメント | `dd/Mon/yy hh:mm AM|PM;投稿者;本文` の形でない（`dd` 2桁、`Mon` = 英語3文字、`yy` 2桁、`hh:mm` 2桁ずつ、AM/PM） |
| 期間 | `Start date` が `Due date` より後 |
