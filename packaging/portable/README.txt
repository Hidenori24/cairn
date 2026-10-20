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
    projects\           プロジェクトの保存先（プロジェクトごとに CSV と設定の JSON）
                        Excel などで CSV を編集して保存すると、アプリに戻ったときに取り込まれます
      _バックアップ\    上書きする前の版（自動で作られ、最大30個）
      _削除済み\        アプリで削除したプロジェクト（ここから戻せます）
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
  rmcli import-export samples\regulation_roadmap.csv --format jira -o jira.csv
  rmcli --help
