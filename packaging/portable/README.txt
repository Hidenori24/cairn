Project Roadmap（ポータブル版）

■ 使い方
  ProjectRoadmap.exe をダブルクリックで起動します。
  インストールは不要で、レジストリにも書き込みません。ネットワークにもつなぎません。
  このフォルダごと USB メモリや別の PC に移せば、データも設定もそのまま使えます。

■ フォルダ構成
  ProjectRoadmap.exe    アプリ本体（これ1つで動きます。追加の DLL は不要）
  rmcli.exe             コマンドライン版（日程の計算・CSV の変換・JIRA 用 CSV の検査）
  roadmap_prefs.json    アプリの設定（テーマ・文字の大きさ・保存先など）。初回の起動で作られます
  data\
    projects\           プロジェクトの保存先（プロジェクトごとのフォルダ）
      プロジェクト名\   1つのプロジェクト（フォルダごと持ち運べます。zip で共有もできます）
        project.csv           課題の CSV（JIRA 互換）。Excel などで編集して保存すると、
                              すぐに（0.5秒ほどで）アプリに取り込まれます
        project.settings.json 設定（基準日・休業日・表示など）
        .roadmap.lock        編集中のロック（開いている間だけできます。共有ドライブで交代して使うとき、
                              ほかの人が編集中なら読み取り専用で開きます。zip に入れる必要はありません）
        添付\<Issue Id>\     課題の添付ファイル（今後の版で使います）
        バックアップ\        上書きする前の CSV（自動で作られ、プロジェクトごとに最大30個）
      _削除済み\        アプリで削除したプロジェクト（フォルダごと。ここから戻せます）

  以前の版の保存形式（プロジェクト名.csv と プロジェクト名.settings.json が projects\ に並ぶ形）も、
  そのまま読めます。自動では動かしません。アプリの「アプリの設定」→「保存先」の
  「フォルダ構成に移す」で、_バックアップ\移行前… に控えを作ってから、上の形に移せます。
  samples\              試しに取り込めるサンプルの CSV（「CSVを取り込む」から）
  THIRD_PARTY.md        使っているライブラリとライセンス

  data\ が無くなっても、起動時に作り直します。

■ 保存先を変えたいとき
  画面右上の「…」→「アプリの設定」（Ctrl+,）の「保存先」で、別のフォルダを選べます。
  今のプロジェクトを新しい場所にコピーするかも選べます（元の場所のファイルは残ります）。
  このフォルダの中を選んだときは相対パスで覚えるので、フォルダごと移しても壊れません。
  次の指定は、アプリの設定より優先します（そのときは画面からは変えられません）。
  起動引数:    ProjectRoadmap.exe --data "D:\共有\projects"
  環境変数:    ROADMAP_DATA=D:\共有\projects
  書き込めない場所（Program Files など）に置いたときは、
  %LOCALAPPDATA%\ProjectRoadmap\ に保存し、画面右上の保存状態のチップに保存先を出します。

■ rmcli の例（このフォルダでコマンドプロンプトを開いて）
  rmcli schedule samples\hierarchy_demo.csv
  rmcli import-export samples\hierarchy_demo.csv --format jira -o jira.csv
  rmcli --help
