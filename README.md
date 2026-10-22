# プロジェクト管理ツール（C++版）

JIRA 互換の CSV を原本に、課題の階層・依存関係・期間から **営業日ベースの日程（最早・最遅・余裕）** を自動で計算し、
ガント・ボード・リスト・ギャップ（ゴール）・統計で見ながら編集する Windows 用のデスクトップアプリです。
ブラウザ版（HTML/JavaScript）を C++ で作り直したもので、計算・CSV入出力・保存の挙動はそのブラウザ版と同じです（仕様: [docs/SPEC.md](docs/SPEC.md)）。

- 単一の `.exe`（ポータブル・インストール不要・レジストリに書かない・ネットワーク通信なし）
- データは実行ファイルの横の `data/projects/` に CSV + 設定JSON で自動保存（Excel などで CSV を編集して保存すると、フォルダの監視ですぐ反映）
- 日本語UI（IME入力対応）、ライト/ダーク、標準/コンパクト、高DPI

## 構成

| フォルダ | 中身 |
|---|---|
| `core/` | UI 非依存のライブラリ（営業日カレンダー・CSV・スケジュール計算・編集と Undo・絞り込み・統計の集計・保存）。Linux でもビルド・テストできる |
| `cli/` | `rmcli`（計算・変換・JIRA互換チェック・テスト用CSVの生成を画面なしで実行） |
| `app/controller/` | `AppController`（選択・絞り込み・保存の予約・外部変更の取り込み）。ImGui に依存しない |
| `app/ui/` | 画面（Dear ImGui）。ガント・ボード・リスト・ギャップ・統計・詳細パネル・コマンドパレット・ダイアログ |
| `app/platform/` | Win32 + DirectX 11（ウィンドウ・フォント・IME・高DPI・ファイルダイアログ） |
| `tests/` | 単体テスト・性質のテスト・golden との照合（doctest） |
| `golden/` | 正解データ（計算結果JSON・書き出しCSV・サンプルCSV・休業日） |
| `docs/` | 仕様書・未確定事項のメモ・UIスパイクの記録・変更履歴 |
| `tools/` | アイコン・画面の文字一覧の生成 |
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
# → build/windows-release/app/ProjectRoadmap.exe

# Linux（core と cli、ASan + UBSan）
cmake --preset linux-asan
cmake --build --preset linux-asan
ctest --preset linux-asan

# Linux で UI をヘッドレスで動かす煙テスト
cmake --preset linux-ui && cmake --build --preset linux-ui && ctest --preset linux-ui
```

### インストーラ版

Releases の `ProjectRoadmap-Setup-<版>.exe`（Inno Setup。CI で作る）。ポータブル版の zip と中身は同じです。

- 既定は管理者権限なしで `%LOCALAPPDATA%\Programs\ProjectRoadmap` に入ります（インストール時に「すべてのユーザー」も選べます）。スタートメニュー・デスクトップ（選んだとき）にショートカットを作ります。
- 保存先は、インストール先の `data\projects` のままです（アプリの設定で変えられます）。
- 新しい版を上書きインストールするだけで更新できます。**アンインストールしても `data\projects` と `roadmap_prefs.json` は消しません**（要らなければフォルダを手で消してください）。
- 署名していないため、実行すると Windows の SmartScreen が警告します（「詳細情報 → 実行」）。
- 作り方: `cmake --install <build> --component portable --prefix dist/ProjectRoadmap` のあと `ISCC /DAppVersion=<版> /DSourceDir=dist\ProjectRoadmap /DRepoDir=. /DOutDir=dist packaging\installer\ProjectRoadmap.iss`

### ポータブル版のフォルダ

配布用には exe の隣に保存先などのフォルダを並べた形にします（GitHub Actions の成果物とリリースの zip も同じ形）。

```sh
cmake --install build/windows-release --component portable --prefix dist/ProjectRoadmap
```

```
ProjectRoadmap/
  ProjectRoadmap.exe   アプリ本体（DLL 不要）
  rmcli.exe            コマンドライン版
  README.txt           使い方とフォルダの説明
  THIRD_PARTY.md
  data/projects/       保存先（プロジェクトごとのフォルダ。_削除済み/ は必要になったら作る）
  samples/             取り込みを試せるサンプル CSV
```

フォルダごと移せば、データと設定（`roadmap_prefs.json` も exe の隣）がそのまま使えます。中身は `packaging/portable/` にあります。

## 使い方

`ProjectRoadmap.exe` を実行します。初回はデータフォルダが空なので「はじめに」画面から、空のプロジェクト・サンプル・CSVの取り込みを選びます。

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
- UI: ImGui をヘッドレスで動かす煙テスト（全ビュー・全ダイアログ・パレット・その場編集・5,000行）を ASan/UBSan で実行
- Windows の exe は MinGW でクロスビルドし、Wine 上で起動・クリック・その場編集・保存まで確認
- **Windows 実機（MSVC ビルド）での IME・高DPI の確認は未実施**です。手順は [docs/ui-spike.md](docs/ui-spike.md)

## サンプルデータについて

`golden/samples/` とアプリに埋め込むサンプル（階層デモ・資源制約デモ）は、外部に出せるよう製品固有の語を一般的な表現にしています。`scenario_demo.csv` は機能の確認用の汎用データで、テストだけが使います。
登場する担当者名（田中・佐藤・鈴木）は架空の例です。

## ライセンス

このソフトウェアは [MIT ライセンス](LICENSE) で公開しています。使用・改変・再配布は自由です（著作権表示とライセンス文を残してください）。

依存ライブラリのライセンスは [THIRD_PARTY.md](THIRD_PARTY.md) を参照してください。
