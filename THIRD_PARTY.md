# サードパーティのソフトウェア

商用利用できるライセンスのものだけを使っています。

| 名前 | 版 | 用途 | ライセンス | 配布物に含むか |
|---|---|---|---|---|
| [Dear ImGui](https://github.com/ocornut/imgui) | 1.90.1 | GUI（app のみ。Win32・DX11 のバックエンドを含む） | MIT | exe に静的リンク |
| [nlohmann/json](https://github.com/nlohmann/json) | 3.11.3 | settings.json・prefs・golden の読み書き | MIT | exe に静的リンク（ヘッダのみ） |
| [doctest](https://github.com/doctest/doctest) | 2.5.3 | テスト | MIT | 含まない（テストのみ） |
| CP932 の変換表（`core/src/cp932_table.inc`） | — | Shift_JIS（Windows-31J）の CSV の読み込み | Microsoft が公開している cp932 の対応表に基づく（Python の `cp932` コーデックから生成した事実データ） | exe に含む |

## 実行時に使うもの（同梱しない）

- Windows のシステムフォント（Yu Gothic UI / Meiryo UI / MS UI Gothic、Consolas、Segoe UI Symbol）を `C:\Windows\Fonts` から読み込みます。フォントファイルは配布物に含みません。
- DirectX 11（`d3d11.dll`・`d3dcompiler_47.dll`）、IMM32 などの Windows のシステムDLL。

## 開発時だけ使うもの

- Python 3（`tools/make_icon.py`・`tools/gen_ui_glyphs.py`）
