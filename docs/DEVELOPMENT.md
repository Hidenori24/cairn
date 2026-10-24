# 開発

## 必要なもの

- C++20 コンパイラ: MSVC 2022 17.8+ / GCC 12+ / Clang 16+。
- CMake **3.25+**（同梱の `CMakePresets.json` はスキーマ6）。プリセットを使わない構成の `CMakeLists.txt` 自体は3.24以上です。
- Ninja。Windows は「x64 Native Tools Command Prompt for VS 2022」など、MSVC の環境を設定したシェルを使います。
- UI の字形一覧の更新には Python 3。インストーラの作成には Inno Setup 6。

依存の取得は `cmake/Dependencies.cmake`。FetchContent の既定は nlohmann/json 3.11.3、doctest 2.5.3、Dear ImGui 1.90.1 です。JSON と doctest は条件を満たすインストール済みパッケージを先に使います。ImGui は固定したソースを取得し、stb_image はリポジトリに同梱します。用途・ライセンスは [THIRD_PARTY.md](../THIRD_PARTY.md)。

## ビルド

```sh
# Windows: GUI + CLI + 単体テスト
cmake --preset windows-release
cmake --build --preset windows-release
ctest --preset windows-release --output-on-failure
# GUI: build/windows-release/app/Cairn.exe
# CLI: build/windows-release/cli/rmcli.exe

# Linux: core + controller + CLI、ASan / UBSan、警告をエラーにする
cmake --preset linux-asan
cmake --build --preset linux-asan
ctest --preset linux-asan --output-on-failure
# CLI: build/linux-asan/cli/rmcli

# Linux: ImGui をヘッドレスで描くスモークテスト（Windows の GUI ではない）
cmake --preset linux-ui -DROADMAP_SANITIZE=ON -DROADMAP_WERROR=ON -DROADMAP_BUILD_TESTS=OFF
cmake --build --preset linux-ui
ctest --preset linux-ui --output-on-failure
```

Windows でもヘッドレスの UI テストを含める場合は、構成時に `-DROADMAP_UI_TESTS=ON` を付けます。CI はこの指定を使います。

Linux のリリースビルドは `linux-release`、Linux から Windows 向けのコンパイル確認は MinGW-w64 を入れて `mingw-cross` を使います。クロスビルドだけでは Windows 実機での IME・描画・ファイル操作を確認したことにはなりません。

オフライン構成では展開済みの依存を指定できます。

```sh
cmake --preset linux-ui -DFETCHCONTENT_SOURCE_DIR_IMGUI=/path/to/imgui
```

JSON・doctest も、インストール済みパッケージまたは `FETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON` / `FETCHCONTENT_SOURCE_DIR_DOCTEST` で用意します。

## 構成と実装ルール

| 場所 | 役割 |
|---|---|
| `core/` | カレンダー・CSV・日程・編集・集計・保存。GUI・Win32 に依存しない |
| `cli/` | 画面を開かない `rmcli`。GUI に依存しない。Windows の引数は UTF-16 から UTF-8 へ変換 |
| `app/controller/` | 選択・表示条件・保存・外部変更の取り込み。ImGui に依存しない |
| `app/ui/` | Dear ImGui の画面・ダイアログ・入力 |
| `app/platform/` | Win32 / DX11、IME、DPI、ファイル選択、任意の GitHub CLI 呼び出し |
| `tests/` / `app/tests/` | core・controller・CLI と、ヘッドレス UI のテスト |
| `golden/` | 計算と CSV の固定の正解データ |
| `tests/compat/` | 以前の版の保存形式の見本 |
| `packaging/` | ポータブル版の説明とフォルダ、Inno Setup |
| `tools/` | 字形・アイコン・リリースノートの生成 |

実装ルールは [CLAUDE.md](../CLAUDE.md)、保存形式を変更するときの手順は [COMPAT.md](COMPAT.md)。golden は利用者の確認なしに変更しません。新しい画面の文言を入れた場合は `python3 tools/gen_ui_glyphs.py` で字形一覧を更新します。変更は CHANGELOG に記録します。

## 品質確認の範囲

CI は `.github/workflows/ci.yml` にあります。

- Ubuntu 24.04 / GCC・Clang: core・controller・CLI のテスト、golden との照合、ASan / UBSan、警告をエラーにする。
- Linux UI: ヘッドレスのスモークテストと ASan / UBSan。
- clang-tidy: core・CLI・controller。一部の危険な警告をエラーにする。
- Windows / MSVC: ビルド・テスト・10MBのサイズ制限・配布フォルダ・インストーラ・無人のインストール／更新／アンインストール・配布名の確認。

画面の確認用には `roadmap_ui_screens <出力フォルダ> [日本語フォント.ttc] [フォント番号]` があります。`ROADMAP_SHOT_ONLY=ux` は操作周り、`ROADMAP_SHOT_ONLY=readability` は表示倍率の画像だけを出します。架空のサンプルを使ってください。Windows のスクリーンショットではありません。

Windows 実機での報告と、ヘッドレス・Wine での確認の区別は [UI の記録](ui-spike.md) にまとめています。性能の目標値は [SPEC 11章](SPEC.md#11-非機能要件) にあり、環境を明記していない数値を全 PC の保証として扱わないでください。
