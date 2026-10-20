# プロジェクト管理ツール（C++版）

JIRA 互換の CSV を原本に、課題の階層・依存関係・期間から **営業日ベースの日程（最早・最遅・余裕）** を自動で計算し、
ガント・ボード・リスト・ギャップ（ゴール）で見ながら編集する Windows 用のデスクトップアプリです。
ブラウザ版（JS版）を C++ で作り直したもので、計算・CSV入出力・保存の挙動は JS版と同じです（仕様: [docs/SPEC.md](docs/SPEC.md)）。

- 単一の `.exe`（ポータブル・インストール不要・レジストリに書かない・ネットワーク通信なし）
- データは実行ファイルの横の `data/projects/` に CSV + 設定JSON で自動保存（Excel で編集しても戻ると反映）
- 日本語UI（IME入力対応）、ライト/ダーク、標準/コンパクト、高DPI

## 構成

| フォルダ | 中身 |
|---|---|
| `core/` | UI 非依存のライブラリ（営業日カレンダー・CSV・スケジュール計算・編集と Undo・絞り込み・保存）。Linux でもビルド・テストできる |
| `cli/` | `rmcli`（計算・変換・JIRA互換チェック・テスト用CSVの生成を画面なしで実行） |
| `app/controller/` | `AppController`（選択・絞り込み・保存の予約・外部変更の取り込み）。ImGui に依存しない |
| `app/ui/` | 画面（Dear ImGui）。ガント・ボード・リスト・ギャップ・詳細パネル・コマンドパレット・ダイアログ |
| `app/platform/` | Win32 + DirectX 11（ウィンドウ・フォント・IME・高DPI・ファイルダイアログ） |
| `tests/` | 単体テスト・性質のテスト・golden との照合（doctest） |
| `golden/` | JS版が出した正解データ（計算結果JSON・書き出しCSV・サンプルCSV・休業日） |
| `reference-js/` | JS版のソース（参照のみ。`index.html` をブラウザで開けば動く） |
| `docs/` | 仕様書・未確定事項のメモ・UIスパイクの記録・変更履歴 |
| `tools/` | golden の再生成（Node）・アイコン・画面の文字一覧の生成 |

## ビルド

必要なもの: CMake 3.24+、Ninja、C++20 コンパイラ（MSVC 2022 17.8+ / GCC 12+ / Clang 16+）。
依存（Dear ImGui 1.90.1・nlohmann/json 3.11.3・doctest 2.4.11）は CMake の FetchContent で版を固定して取得します
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
  data/projects/       保存先（_バックアップ/・_削除済み/ は必要になったら作る）
  samples/             取り込みを試せるサンプル CSV
```

フォルダごと移せば、データと設定（`roadmap_prefs.json` も exe の隣）がそのまま使えます。中身は `packaging/portable/` にあります。

## 使い方

`ProjectRoadmap.exe` を実行します。初回はデータフォルダが空なので「はじめに」画面から、空のプロジェクト・サンプル・CSVの取り込みを選びます。

- データフォルダ: `--data <フォルダ>` > 環境変数 `ROADMAP_DATA` > 実行ファイルの横の `data/projects/`（書き込めなければ `%LOCALAPPDATA%\ProjectRoadmap\projects\`）
- 端末ごとの設定（テーマ・密度・テンプレートなど）: 実行ファイルの横の `roadmap_prefs.json`
- 上書きする前の CSV は、起動してから最初の1回だけ `_バックアップ/` に残します（30個まで）。削除したプロジェクトは `_削除済み/` へ移します
- 絞り込み: 上のバーの担当・状況・優先度と「遅れ」「余裕小」のチップは、一致しない課題を**薄く**表示します（行は減らない）。検索語と、表の列名の右の漏斗（列の絞り込み）は、一致しない課題を**隠し**ます
- 削除: 行（ボードはカード）にマウスを載せると出るごみ箱、`Delete` キー、右クリックのメニュー。通知の「元に戻す」で戻せます
- 主なキー: `Ctrl+K` コマンドパレット、`1`〜`4` ビュー切替、`N` 追加、`Tab`/`Shift+Tab` 階層、`Enter`/`F2` 名前の編集、`?` ショートカット一覧

### rmcli

```sh
rmcli schedule samples/regulation_roadmap.csv --base 2026-10-19 --level 1 -o out.json   # 計算結果（golden と同じ形式）
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

`golden/samples/` とアプリに埋め込むサンプルは、外部に出せるよう製品固有の語（製品名・社内の開発手法など）を一般的な表現に置き換えています。
置き換えたあとの正解データは、JS版のライブラリで作り直しています（`node tools/golden/gen_golden.js`。作り直す前に、元の正解データを1バイトも違わず再現できることを確かめています）。
登場する担当者名（田中・佐藤・鈴木）は架空の例です。

## ライセンス

依存ライブラリのライセンスは [THIRD_PARTY.md](THIRD_PARTY.md) を参照してください。
