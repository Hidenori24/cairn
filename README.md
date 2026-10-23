# Cairn

**オフラインで動く、JIRA 互換のガント／スケジューラ。営業日・余裕の計算、週報、ベースライン比較**

*A signpost for your schedule — an offline, JIRA-compatible Gantt chart and scheduler for Windows.*

![ガント。営業日ベースの日程と余裕を計算して表示します（同梱のサンプル）](docs/images/gantt.png)

[日本語](#日本語) ・ [English](#english)

---

## 日本語

Cairn（ケルン）は、登山道で道しるべに石を積んだもの。マイルストーン（道しるべ）を積み上げて進む、という意味で名づけました。

JIRA 互換の CSV を原本に、課題の階層・依存関係・期間から **営業日ベースの日程（最早・最遅・余裕）** を自動で計算し、
ガント・ボード・リスト・ギャップ（ゴール）・統計・今日（今日と今週にやること）で見ながら編集する Windows 用のデスクトップアプリです。
ブラウザ版（HTML/JavaScript）を C++ で作り直したもので、計算・CSV入出力・保存の挙動はそのブラウザ版と同じです（仕様: [docs/SPEC.md](docs/SPEC.md)）。
（以前の名前は「プロジェクト管理ツール」「Project Roadmap」です。設定・データの名前と場所は変わっていないので、そのまま使えます。）

### ダウンロード

[Releases](https://github.com/Hidenori24/task_management_tool/releases) から取ります（リポジトリ名の変更後も、古い URL は自動で転送されます）。

| 配布物 | 中身 |
|---|---|
| `Cairn-Setup-<版>.exe` | インストーラ。管理者権限なしで入ります。今までの版の上に上書きして更新できます |
| `Cairn-windows-x64.zip` | ポータブル版。展開して `Cairn.exe` を実行するだけ（インストール不要） |
| `SHA256SUMS.txt` | 上のファイルのチェックサム。`certutil -hashfile <ファイル> SHA256`（PowerShell なら `Get-FileHash`）で照合できます |

`-rc` が付いた版（`1.0.0-rc.1` など）は、リリース候補の pre-release です。

**Windows の SmartScreen について**: 配布物にはコード署名をしていないため、初めて実行すると「Windows によって PC が保護されました」と表示されます。公式の配布元から取得したことを確認し、信頼できる場合は「詳細情報」→「実行」で起動できます。`SHA256SUMS.txt` でも取得したファイルが変わっていないことを確かめられます。

### 機能

- **日程の自動計算**: 課題の階層・依存関係・期間から、営業日ベースで最早・最遅・余裕を計算（休業日・祝日・同時作業の制限・シナリオ）。遅れ・余裕小の課題がひと目で分かる
- **見方いろいろ**: ガント（日・週・月）・ボード・リスト・ギャップ（ゴールまでの差）・統計・今日（今日と今週にやること）。依存の矢印・クリティカルな経路の強調
- **編集**: その場の編集・ドラッグで日程と依存を変更・一括編集・元に戻す／やり直し・コマンドパレット（`Ctrl+K`）
- **JIRA 互換の CSV**: 取り込み・書き出し・互換チェック。Excel などで CSV を編集して保存すると、すぐに反映
- **週報・報告**: 週次レポート（今週の完了・触った課題・先週から変わったこと・遅れ・余裕小）、HTML の書き出し
- **ベースライン比較**: 計画を保存して、いまの日程との差を見る。EVM（出来高管理）・マイルストーンの予測の推移・状況の件数の推移（累積フロー図）・負荷のヒートマップ
- **添付・関連リンク**: 課題へのファイル添付、GitHub の PR・Issue へのリンク
- **共有フォルダで交代して使う**: 編集中のロック（読み取り専用で開く・引き継ぎ）
- **オフライン**: ネットワーク通信なし・レジストリに書かない。データは CSV と JSON（`data/projects/`）で、フォルダごと持ち運べる
- 日本語 UI（IME 入力対応）、ライト／ダーク、標準／コンパクト、高 DPI。コマンドライン版 `rmcli` つき

### 画面

| ボード | リスト |
|---|---|
| ![ボード](docs/images/board.png) | ![リスト](docs/images/list.png) |

| 詳細パネル | 統計 |
|---|---|
| ![詳細パネル](docs/images/detail.png) | ![統計](docs/images/stats.png) |

（画面の画像は、同梱のサンプル（架空のデータ）を、`roadmap_ui_screens`（Linux のヘッドレス描画）で出したものです。Windows の実際の見た目とは細部が違います。）

## 構成（開発者向け）

| フォルダ | 中身 |
|---|---|
| `core/` | UI 非依存のライブラリ（営業日カレンダー・CSV・スケジュール計算・編集と Undo・絞り込み・統計の集計・保存）。Linux でもビルド・テストできる |
| `cli/` | `rmcli`（計算・変換・JIRA互換チェック・テスト用CSVの生成を画面なしで実行） |
| `app/controller/` | `AppController`（選択・絞り込み・保存の予約・外部変更の取り込み）。ImGui に依存しない |
| `app/ui/` | 画面（Dear ImGui）。ガント・ボード・リスト・ギャップ・統計・今日・詳細パネル・コマンドパレット・ダイアログ |
| `app/platform/` | Win32 + DirectX 11（ウィンドウ・フォント・IME・高DPI・ファイルダイアログ） |
| `tests/` | 単体テスト・性質のテスト・golden との照合（doctest） |
| `golden/` | 正解データ（計算結果JSON・書き出しCSV・サンプルCSV・休業日） |
| `docs/` | 仕様書・未確定事項のメモ・UIスパイクの記録・変更履歴 |
| `tools/` | アイコン（`make_icon.py`）・画面の文字一覧・リリースノートの生成 |
| `packaging/` | 配布物の中身（ポータブル版のフォルダ `portable/`・インストーラの Inno Setup スクリプト `installer/`） |

## ビルド

必要なもの: CMake 3.24+、Ninja、C++20 コンパイラ（MSVC 2022 17.8+ / GCC 12+ / Clang 16+）。
依存（Dear ImGui 1.90.1・nlohmann/json 3.11.3・doctest 2.5.3）は CMake の FetchContent で版を固定して取得します
（vcpkg などでインストール済みならそちらを使います）。

```sh
# Windows（MSVC。exe を作る）— 「x64 Native Tools Command Prompt for VS 2022」から
cmake --preset windows-release
cmake --build --preset windows-release
ctest --preset windows-release
# → build/windows-release/app/Cairn.exe

# Linux（core と cli、ASan + UBSan）
cmake --preset linux-asan
cmake --build --preset linux-asan
ctest --preset linux-asan

# Linux で UI をヘッドレスで動かすスモークテスト
cmake --preset linux-ui && cmake --build --preset linux-ui && ctest --preset linux-ui
```

### インストーラ版

Releases の `Cairn-Setup-<版>.exe`（Inno Setup。CI で作る）。ポータブル版の zip と中身は同じです。

- 既定は管理者権限なしで `%LOCALAPPDATA%\Programs\Cairn` に入ります（インストール時に「すべてのユーザー」も選べます）。スタートメニュー・デスクトップ（選んだとき）にショートカットを作ります。
- 保存先は、インストール先の `data\projects` のままです（アプリの設定で変えられます）。
- 新しい版を上書きインストールするだけで更新できます（名前が Project Roadmap だった版の上にも入れられます。古い名前の `ProjectRoadmap.exe` とショートカットは更新時に消え、データ・設定はそのまま使えます。その場合のインストール先のフォルダ名は `ProjectRoadmap` のままです）。**アンインストールしても `data\projects` と `roadmap_prefs.json` は消しません**（要らなければフォルダを手で消してください）。
- 署名していないため、実行すると Windows の SmartScreen が警告します（「詳細情報 → 実行」）。
- 作り方: `cmake --install <build> --component portable --prefix dist/Cairn` のあと `ISCC /DAppVersion=<版> /DSourceDir=dist\Cairn /DRepoDir=. /DOutDir=dist packaging\installer\Cairn.iss`

### ポータブル版のフォルダ

配布用には exe の隣に保存先などのフォルダを並べた形にします（GitHub Actions の成果物とリリースの zip も同じ形）。

```sh
cmake --install build/windows-release --component portable --prefix dist/Cairn
```

```
Cairn/
  Cairn.exe            アプリ本体（DLL 不要）
  rmcli.exe            コマンドライン版
  README.txt           使い方とフォルダの説明
  THIRD_PARTY.md
  data/projects/       保存先（プロジェクトごとのフォルダ。_削除済み/ は必要になったら作る）
  samples/             取り込みを試せるサンプル CSV
```

フォルダごと移せば、データと設定（`roadmap_prefs.json` も exe の隣）がそのまま使えます。中身は `packaging/portable/` にあります。

## 使い方

`Cairn.exe` を実行します。初回はデータフォルダが空なので「はじめに」画面から、空のプロジェクト・サンプル・CSVの取り込みを選びます。

- データフォルダ: `--data <フォルダ>` > 環境変数 `ROADMAP_DATA` > 「アプリの設定」で選んだ場所 > 実行ファイルの横の `data/projects/`（書き込めなければ `%LOCALAPPDATA%\ProjectRoadmap\projects\`）
- アプリの設定（`…` メニュー・`Ctrl+,`）: 保存先の変更（今のプロジェクトをコピーするか選べる。元の場所のファイルは残る）・既定に戻す、テーマ・密度・文字の大きさ・絞り込みで薄くする濃さ、自分の名前、削除の前の確認。`roadmap_prefs.json` に保存し、保存先は実行ファイルのフォルダの中なら相対パスで覚える（フォルダごと移しても壊れない）
- 端末ごとの設定（テーマ・密度・テンプレートなど）: 実行ファイルの横の `roadmap_prefs.json`
- 保存先の中は、プロジェクトごとのフォルダです（`<プロジェクト>/project.csv`・`project.settings.json`・`バックアップ/`、添付は `添付/<Issue Id>/`）。フォルダごと持ち運べます
- 上書きする前の CSV は、起動してから最初の1回だけ、そのプロジェクトの `バックアップ/` に残します（プロジェクトごとに30個まで）。削除したプロジェクトは、フォルダごと `_削除済み/` へ移します（戻すときもフォルダごと）
- 以前の平置き（`プロジェクト名.csv` と `プロジェクト名.settings.json`）は、そのまま読み書きできます。自動では動かしません。「アプリの設定」の保存先にある「フォルダ構成に移す」で、移す前に `_バックアップ/移行前…` へ控えを作ってから移せます
- 共有フォルダで交代して使う（編集中のロック）: 開いているプロジェクトのフォルダに `.roadmap.lock`（アプリの設定の「自分の名前」とパソコンの名前が入ります）を作り、ほかの人が編集中なら**読み取り専用**で開きます（上部の帯の「再確認」「編集を引き継ぐ」。更新が 3 分ない古いロックは引き継げます）。保存のたびにロックを確かめ、奪われていたら保存せず、未保存の編集は別のプロジェクト「（未保存の編集）」に残します。共有ドライブ（SMB）を想定しています。**OneDrive・Google Drive などの同期フォルダでは確実に働きません**。ロックを知らない古い版と混ぜて使うと効きません。アプリの設定でオフにできます
- 絞り込み: 上のバーの担当・状況・優先度と「遅れ」「余裕小」のチップは、一致しない課題を**薄く**表示します（行は減らない）。検索語と、表の列名の右の漏斗（列の絞り込み）は、一致しない課題を**隠し**ます
- 拡大縮小: `Ctrl+ホイール`（タッチパッドのピンチも）・`Ctrl+「+」「-」`・`Ctrl+0`（100%）で画面全体。ガントの時間軸の上の `Ctrl+ホイール` は時間軸だけを広げ縮めます（マウスの位置の日付は動きません）
- 削除: 行（ボードはカード）にマウスを載せると出るごみ箱、`Delete` キー、右クリックのメニュー。通知の「元に戻す」で戻せます
- 主なキー: `Ctrl+K` コマンドパレット、`1`〜`4` ビュー切替、`N` 追加、`Tab`/`Shift+Tab` 階層、`Enter`/`F2` 名前の編集、`?` ショートカット一覧

### rmcli

```sh
rmcli schedule samples/hierarchy_demo.csv --base 2026-10-19 --level 1 -o out.json   # 計算結果（golden と同じ形式）
rmcli import-export input.csv --format jira -o jira.csv                                 # JIRA用に書き出す
rmcli check-jira input.csv                                                              # JIRA取り込みの互換チェック
rmcli gen --count 5000 -o big.csv                                                       # テスト用の大きなCSV
rmcli validate input.csv                                                                # 読めるかと警告の一覧
```

終了コードは 成功 0 / 入力の誤り 2 / 内部エラー 1。各コマンドに `--help` があります。

## テストと確認の状況

- golden との照合: 計算13ケース（全フィールド）と書き出しCSV 6ファイル（バイト一致）。`rmcli` の出力も golden とバイト一致
- 単体テスト・性質のテスト（往復の安定・Undo の完全性・計算の不変条件・資源制限の単調性・フィルタの整合）
- 性能（リリースビルド）: 5,000件の計算が約5ms（制限ありで約40ms）
- UI: ImGui をヘッドレスで動かすスモークテスト（全ビュー・全ダイアログ・パレット・その場編集・5,000行）を ASan/UBSan で実行
- Windows の exe は MinGW でクロスビルドし、Wine 上で起動・クリック・その場編集・保存まで確認
- **Windows 実機（MSVC ビルド）での IME・高DPI の確認は未実施**です。手順は [docs/ui-spike.md](docs/ui-spike.md)

## 互換の方針

保存したデータ（CSV・フォルダ構成・設定ファイル・共有／バックアップの JSON）を新しい版でも開ける約束、未知のキーを消さない方針、JIRA 互換、保存形式を変えるときの手順は [docs/COMPAT.md](docs/COMPAT.md) にあります。版ごとの見本（架空のデータ）を `tests/compat/` に置き、単体テストで開けること・書き戻しても変わらないことを確かめています。

## サンプルデータについて

`golden/samples/` とアプリに埋め込むサンプル（階層デモ・資源制約デモ）は、外部に出せるよう製品固有の語を一般的な表現にしています。`scenario_demo.csv` は機能の確認用の汎用データで、テストだけが使います。
登場する担当者名（田中・佐藤・鈴木）は架空の例です。

## ライセンス

Cairn（Copyright (c) 2026 Hidenori24）は [MIT ライセンス](LICENSE) で公開しています。使用・改変・再配布は自由です（著作権表示とライセンス文を残してください）。

依存ライブラリのライセンスは [THIRD_PARTY.md](THIRD_PARTY.md) を参照してください。

---

## English

**Cairn** is an offline, JIRA-compatible Gantt chart and scheduler for Windows. It reads a JIRA-compatible CSV and computes the schedule in **business days** (earliest / latest dates and float) from the issue hierarchy, dependencies and durations. A cairn is a stack of stones that marks a trail: the milestones you pile up as you go. (Formerly "Project Roadmap".)

### Download

Get a build from [Releases](https://github.com/Hidenori24/task_management_tool/releases) (the old URL is redirected after the repository is renamed).

| File | What it is |
|---|---|
| `Cairn-Setup-<version>.exe` | Installer. No administrator rights needed. Installs over an older version to upgrade it |
| `Cairn-windows-x64.zip` | Portable version. Unzip and run `Cairn.exe` (no installation) |
| `SHA256SUMS.txt` | Checksums of the files above (`Get-FileHash <file>` in PowerShell) |

Versions with `-rc` (for example `1.0.0-rc.1`) are release candidates, published as pre-releases.

**Windows SmartScreen**: the files are not code-signed, so Windows may show "Windows protected your PC" the first time you run them. Verify that you downloaded them from the official release and, if you trust the source, choose "More info" and then "Run anyway". You can also compare the file with `SHA256SUMS.txt` to check it was not changed.

### Features

- **Automatic scheduling** in business days: earliest / latest dates and float from hierarchy, dependencies and durations (holidays, work-in-progress limits, scenarios). Late and tight issues stand out
- **Views**: Gantt (day / week / month), board, list, gap (distance to the goal), statistics, and "today" (what to do today and this week). Dependency arrows and critical path highlighting
- **Editing**: in-place editing, drag to change dates and dependencies, bulk edit, undo / redo, command palette (`Ctrl+K`)
- **JIRA-compatible CSV**: import, export and a compatibility check. Edit the CSV in Excel and save; Cairn picks the change up immediately
- **Weekly report**: completed and touched issues, what changed since last week, late / tight issues; HTML export
- **Baseline comparison**: save a plan and compare it with the current schedule. Also EVM, milestone forecast trends, cumulative flow, workload heatmap
- **Attachments and links**: attach files to issues, link GitHub pull requests and issues
- **Shared-folder editing lock**: take turns on a shared drive (read-only open, take over)
- **Offline**: no network access, no registry writes. Data is plain CSV + JSON under `data/projects/` and moves with the folder
- Japanese UI (IME supported), light / dark, compact mode, high DPI. Comes with the command-line tool `rmcli`

Your existing settings and data keep working: `roadmap_prefs.json`, `data/projects/` and `project.settings.json` have the same names and locations as before the rename.

### Build

CMake 3.24+, Ninja and a C++20 compiler (MSVC 2022 17.8+ / GCC 12+ / Clang 16+). Dependencies (Dear ImGui, nlohmann/json, doctest) are fetched by CMake.

```sh
cmake --preset windows-release && cmake --build --preset windows-release && ctest --preset windows-release
# -> build/windows-release/app/Cairn.exe
cmake --preset linux-asan && cmake --build --preset linux-asan && ctest --preset linux-asan   # core + CLI
```

The specification is in Japanese: [docs/SPEC.md](docs/SPEC.md).

### License

MIT. See [LICENSE](LICENSE). Copyright (c) 2026 Hidenori24. Third-party licenses: [THIRD_PARTY.md](THIRD_PARTY.md).
