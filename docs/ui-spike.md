# UI スパイク（SPEC 3.4・P0）の記録

## 結論（暫定）

**Dear ImGui + Win32/DX11 を採用**して実装を進めた。下の表のとおり、Windows 実機でしか確かめられない項目（IME の候補ウィンドウ・高DPIでのにじみ・Windows のフォント）が残っている。
**Windows 実機で「未確認」の項目を確かめ、1つでも満たせなければ Qt 6 Widgets に切り替える**（`core` と `AppController` は共通なので、差し替えは `app/ui` と `app/platform` だけ）。

## 確かめた環境

1. **Linux・ヘッドレス**（`app/tests/ui_smoke.cpp`、`ctest --preset linux-ui`）: ImGui を GPU なしで動かし、全ビュー・全ダイアログ・パレット・その場編集・5,000行のガントを描く。ASan/UBSan つき
2. **Linux・ソフトウェア描画**（`app/tests/ui_screens.cpp`）: ImGui の描画データを画像にして、レイアウト・文字の欠け・色を目で確認（Noto Sans CJK JP を使用）
3. **Wine 9.0 + Xvfb**: MinGW でクロスビルドした `ProjectRoadmap.exe` を起動し、xdotool でクリック・ダブルクリック・キー入力・その場編集・保存・Ctrl+K を確認

## 確認項目

| 確認項目 | 合格基準（SPEC 3.4） | 結果 |
|---|---|---|
| 日本語フォント | Yu Gothic UI → Meiryo UI → MS UI Gothic の順に読み、常用漢字・かな・全角記号が欠けない。起動 +300ms 以内 | **一部確認**: 読み込みの順序と TTC の番号を実装。Wine 上では日本語の表示を確認（Windows のフォントの代わりに Noto CJK を置いて確認）。ImGui の日本語の範囲にない常用漢字は、画面の文言・データから集めて追加する。**Windows 実機での表示と起動時間は未確認** |
| IME | 変換中の文字が見え、候補ウィンドウが入力位置の近くに出て、確定できる。確定の Enter で別の操作が走らない | **一部確認**: 候補ウィンドウの位置は ImGui の既定の実装（`ImmSetCompositionWindow`・`ImmSetCandidateWindow`。MinGW でも有効にした）。変換中・確定直後の Enter はプラットフォーム層で ImGui に渡さない。スモークテストで「変換中は編集が続く・Enter で確定」を確認。**Windows 実機の IME（MS-IME・Google 日本語入力）では未確認** |
| 表の編集 | 行をダブルクリックして件名をその場で編集 | **確認**（Wine 上の exe で、ダブルクリック → 入力 → Enter で確定・保存。スモークテストで F2・N・Esc・追加直後の Enter も確認） |
| ツリー表 | 5,000行で 60fps 近く（可視行だけ描画） | **一部確認**: 可視範囲の行・矢印だけを描く。計算は 5,000件で約5ms。**実機での fps は未計測** |
| ドラッグ | 行のドラッグ（並べ替え）、バーのドラッグ | **実装済み・スモークテストで描画のみ確認**。**実機での操作感は未確認** |
| 高DPI | 150% / 200% で文字がにじまず、崩れない | **一部確認**: PerMonitorV2 のマニフェスト、`WM_DPICHANGED` でフォントとスタイルを作り直す。スモークテストで 200% の描画を確認。**実機の 150%/200% は未確認** |

## Windows 実機での確認手順

1. `cmake --preset windows-release && cmake --build --preset windows-release`
2. `build/windows-release/app/Cairn.exe --data %TEMP%\roadmap-test` を起動し、SPEC 12.5 のチェックリストを上から確かめる
3. 特に: 件名をダブルクリック → 日本語を変換 → Enter で確定（行が増えたり編集が終わったりしない）→ もう一度 Enter で編集終了
4. 表示スケールを 150%・200% にし、ウィンドウを別のモニターへ動かしても文字がにじまないこと
5. `rmcli gen --count 5000 -o big.csv` を取り込み、ガントのスクロール・バーのドラッグが重くないこと

結果はこのファイルの表に追記する。

## 上部の操作・条件表示の確認

`roadmap_ui_smoke --ux` で、960〜1440px相当の幅・標準／コンパクト・100／150%での配置、レポートの入口、計画の「今日にする」、条件の個別解除と折り返しを確認できます。`ROADMAP_SHOT_ONLY=ux roadmap_ui_screens <出力フォルダ>` は、通常・条件付き・960px・レポートのメニュー・計画のメニュー・ダークの6枚を出力します。

`roadmap_ui_smoke --readability` は、倍率の選択と保存、4K相当の画面でDPI150／200%とアプリ100／125／150／200%の組み合わせを確認します。文字のフォントも作り直し、標準／コンパクトの両方で配置を確認します。`ROADMAP_SHOT_ONLY=readability roadmap_ui_screens <出力フォルダ>` は、標準サイズ・倍率メニュー・4KでWindows200%相当＋アプリ100／150%の画像を出力します。Windows 実機の表示とは別に、Noto Sans CJK のソフトウェア描画で確認するものです。
