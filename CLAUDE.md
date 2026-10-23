# Cairn (C++)

JIRA CSVを読み、営業日でスケジュールを計算し、ガント・ボード・リスト・ギャップで見せるデスクトップアプリ。
仕様は docs/SPEC.md、正解データは golden/。

## ルール
- 仕様と golden が食い違ったら golden が正。仕様に書かれていない点は暫定判断で進め、docs/spec-questions.md に書く（止まらない）。
- core/ と cli/ は GUI・ImGui を include しない（core は Win32 も）。Linux でもビルド・テストできること。
- まず golden の照合テストを書き、赤を確認してから実装する（TDD）。
- std::regex は使わない。文字列は UTF-8 の std::string。ファイル名・パスは std::filesystem::path（u8path / char8_t に注意）。
- 例外はUI層の最上位だけで捕まえる。core はエラーを戻り値（Result 型）で返す。
- 乱数・時計・ファイルシステムは注入できるようにする（IClock, IFileStore）。
- 画面の文言は日本語。コード中のコメントも日本語でよい。文言を足したら `python3 tools/gen_ui_glyphs.py`（UI の煙テストが検出する）。
- 描画中に編集しても参照が壊れないよう、画面は `Session::viewIssues()` と `tree()`/`rows()` を使う（フレームの終わりまで古い計算結果を残す）。
- 依存を足すときは理由をPR/コミットに書く。

## ビルドとテスト
- 構成: `cmake --preset windows-release` / `cmake --preset linux-asan`（core+cli のみ）/ `cmake --preset linux-ui`（UI の煙テスト）
- ビルド: `cmake --build --preset <preset>`
- テスト: `ctest --preset <preset> --output-on-failure`
- golden だけ: `ctest -R golden`（`cli_golden_*` は rmcli の出力とのバイト比較）
- Windows 向けのクロスビルド（コンパイル確認）: `cmake --preset mingw-cross`
- ネットワークがないとき: `-DFETCHCONTENT_SOURCE_DIR_IMGUI=<imgui のソース>` などで依存を差し替える

## フェーズ
docs/SPEC.md 第13章の P0〜P7 を順に。各フェーズの受け入れ条件を満たしてから次へ。完了ごとに docs/CHANGELOG.md を更新。

## 触らないもの
- golden/（正解データ。更新するときはユーザーに確認）
