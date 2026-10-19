# 変更履歴

## 0.1.0（2026-10-19）

SPEC 第13章の P0〜P7 を一通り実装した最初の版。

### P0 環境
- リポジトリの骨格、CMake（プリセット: windows-release / linux-asan / linux-ui / mingw-cross）、依存の版の固定（FetchContent）
- GitHub Actions（Linux: GCC・Clang + ASan/UBSan、UI の煙テスト、Windows: MSVC で exe とテスト）
- UI スパイク: ImGui を暫定採用。Windows 実機での確認が残る（[ui-spike.md](ui-spike.md)）

### P1 日付と CSV
- 営業日カレンダー、日付の読み取り（3つの書式）、期間の読み取り、コメント
- CSV の字句解析（区切りの自動判定・引用符・改行）、見出しの同義語、未知の列の保持、JIRA用の階層の畳み込み
- 文字コード: UTF-8（BOM可）→ Shift_JIS（CP932）→ 置換。書き出しは BOM なし UTF-8・CRLF
- golden: 3サンプルの `roundtrip.csv`・`jira.csv` がバイト一致

### P2 エンジン
- ツリー構築（親ループ・依存ループの検出、親への依存の展開、期限の継承）、最早・最遅・余裕、親のまとめ、シナリオ、担当者ごとの同時作業の制限、負荷
- golden: 計算13ケースが全フィールド一致。5,000件で約5ms（制限ありで約40ms）
- 性質のテスト（往復・Undo・不変条件・単調性・フィルタ）

### P3 編集・保存・テンプレ
- `Session`（mutate・Undo/Redo 100件・まとめての操作）、9.2 の全操作、テンプレート（番号タグの採番）
- 絞り込み（10.8）、データフォルダ（原子的な書き込み・バックアップ30個・_削除済み・外部変更の3ケース・不正なCSVを上書きしない）
- settings.json・共有/バックアップJSON・roadmap_prefs.json（未知のキーを保持）

### P4 CLI
- `rmcli schedule / import-export / check-jira / gen / validate`。出力は golden とバイト一致

### P5〜P7 アプリ
- AppController（選択・絞り込み・保存の予約 400ms・再試行 2→5→10秒・アクティブ時の同期）
- ガント・ボード・リスト・ギャップ・ゴールのポップオーバー・詳細パネル・コマンドパレット・全ダイアログ・通知・まとめ編集バー・はじめに
- Win32: 高DPI・IME・日本語フォント（足りない字は作り直して追加）・ファイルのドロップ・ダイアログ・終了時の確認・ダーク/ライト
- 単一 exe（MinGW のクロスビルドで 3.5MB、システムDLLのみ）
- GitHub Actions の windows-latest（MSVC 19.51・/MT）でビルドとテスト 25 件が通る。exe は 1.9MB

### データ
- サンプルの製品固有の語を一般的な表現に置き換え、golden を JS版のライブラリで作り直した（[spec-questions.md](spec-questions.md) の C）

### 未解決
- Windows 実機での IME・高DPI・fps の確認（MSVC のビルドとテストは CI で確認済み）（[ui-spike.md](ui-spike.md)）
- フォルダの常時監視（ReadDirectoryChangesW。任意）
- 仕様の疑問は [spec-questions.md](spec-questions.md)
