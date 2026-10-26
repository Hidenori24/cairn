Cairn（ポータブル版）— オフラインで動く、JIRA 互換のガント／スケジューラ

■ 使い方
  Cairn.exe をダブルクリックで起動します。
  インストールは不要で、設定は JSON に保存します。通常の計画・編集・報告はネット接続不要で、利用状況の送信はありません。
  GitHub の「状態を確認」を押した場合だけ、別途導入・ログインした gh が通信します。リンクを開くと関連付けアプリが開きます。
  このフォルダごと USB メモリや別の PC に移せば、データも設定もそのまま使えます。

■ 画面・文字の大きさ
  上部の「100% ▼」で125・150・175・200%を選べます（Windows の表示倍率に追加）。
  上部の「レポート ▼」から HTML・週報・CSV を書き出せます。HTML のガントは約2か月ごとに分け、
  印刷はガントが A4 横、ほかが A4 縦です。ブラウザのプレビューで確認してください。
  検索・列の条件がある間は、一致する課題を見せるため折りたたみを無視します。条件のチップで解除できます。

■ フォルダ構成
  Cairn.exe             アプリ本体（これ1つで動きます。追加の DLL は不要）
  rmcli.exe             コマンドライン版（日程の計算・CSV の変換と検査・HTML 報告・週報）
  roadmap_prefs.json    アプリの設定（テーマ・文字の大きさ・保存先など）。初回の起動で作られます
  data\
    projects\           プロジェクトの保存先（プロジェクトごとのフォルダ）
      プロジェクト名\   1つのプロジェクト（フォルダごと持ち運べます。zip で共有もできます）
        project.csv           課題の CSV（JIRA 互換）。Excel などで編集して保存すると、
                              変更通知から0.5秒ほど待ち、編集中の操作が終わった後に取り込まれます
        project.settings.json 設定（基準日・休業日・表示など）
        .roadmap.lock        編集中のロック（開いている間だけできます。共有ドライブで交代して使うとき、
                              ほかの人が編集中なら読み取り専用で開きます。zip に入れる必要はありません）
        添付\<Issue Id>\     課題の添付ファイル（ドロップやファイル選択で追加できます）
        バックアップ\        上書きする前の CSV（自動で作られ、プロジェクトごとに最大30個）
      _削除済み\        アプリで削除したプロジェクト（フォルダごと。ここから戻せます）

  以前の版の保存形式（プロジェクト名.csv と プロジェクト名.settings.json が projects\ に並ぶ形）も、
  そのまま読めます。自動では動かしません。アプリの「アプリの設定」→「保存先」の
  「フォルダ構成に移す」で、_バックアップ\移行前… に控えを作ってから、上の形に移せます。
  samples\              試しに取り込めるサンプルの CSV（「CSVを取り込む」から）
  THIRD_PARTY.md        使っているライブラリとライセンス

  data\ が無くなっても、空の保存先は起動時に作ります。削除したデータが復元されるわけではありません。

■ 以前の版（名前が Project Roadmap だった版）から
  名前を Cairn に変えました。設定・データの名前と場所は変わりません（roadmap_prefs.json・data\projects\・
  project.settings.json はそのまま使えます）。新しい zip を同じフォルダに展開した場合は、古い ProjectRoadmap.exe は
  要らないので消してください（インストーラ版は上書きの更新で自動的に消します）。

■ 保存先を変えたいとき
  画面右上の「…」→「アプリの設定」（Ctrl+,）の「保存先」で、別のフォルダを選べます。
  今のプロジェクトを新しい場所にコピーするかも選べます（元の場所のファイルは残ります）。
  このフォルダの中を選んだときは相対パスで覚えるので、フォルダごと移しても壊れません。
  次の指定は、アプリの設定より優先します（そのときは画面からは変えられません）。
  起動引数:    Cairn.exe --data "D:\共有\projects"
  環境変数:    ROADMAP_DATA=D:\共有\projects
  書き込めない場所（Program Files など）に置いたときは、
  %LOCALAPPDATA%\ProjectRoadmap\ に保存し（改名前からの互換のため、この名前のままです）、画面右上の保存状態のチップに保存先を出します。

■ バックアップ
  丸ごと控えるときは、アプリを閉じてプロジェクトのフォルダ（CSV・設定・添付）をコピーしてください。
  端末設定も残す場合は roadmap_prefs.json を控えます。保存先を変更した場合は実際の保存先も必要です。
  自動のバックアップは CSV だけです。「全プロジェクトをバックアップ」の JSON には、
  添付・関連リンク・週報の所感やスナップショット・ベースライン・推移の履歴・EVM設定は含みません。
  共有場所の .roadmap.lock はコピーに入れません。OneDrive などの同期フォルダはロックの対象外です。

■ ダウンロードと説明
  公式配布元: https://github.com/Hidenori24/cairn/releases
  使い方: https://github.com/Hidenori24/cairn/blob/main/docs/GUIDE.md
  CLI: https://github.com/Hidenori24/cairn/blob/main/docs/CLI.md
  Windows の実行ファイルは署名済みの場合もありますが、SmartScreen は評判などにより警告を
  表示することがあります。署名の有無と最新情報は公式リリースで確認してください。
  SHA256SUMS.txt を確認し、信頼できる場合は「詳細情報」→「実行」で起動してください。

■ rmcli の例（このフォルダでコマンドプロンプトを開いて）
  rmcli schedule samples\hierarchy_demo.csv
  rmcli import-export samples\hierarchy_demo.csv --format jira -o jira.csv
  rmcli report samples\hierarchy_demo.csv -o report.html
  rmcli weekly samples\hierarchy_demo.csv --period last -o weekly.md
  rmcli --help
  CLI は CSV だけを読みます。GUI の設定・履歴・ベースラインは読みません。
  条件を揃える場合は --base・--holidays・--scenario・--level を指定します。
  PowerShell でこのフォルダから起動する場合は .\rmcli.exe と指定します。
