# CLI（rmcli）

対象: v1.3.2。Windows 配布物には `rmcli.exe` が同梱されています。Linux ではソースから `rmcli` をビルドできます（[開発](DEVELOPMENT.md)）。以下は `rmcli` のあるフォルダを PATH に加えた場合の例です。PowerShell で同じフォルダから実行する場合は `./rmcli.exe` と指定します。

## コマンド一覧

| コマンド | 入力と出力 |
|---|---|
| `schedule` | CSV → 日程計算の JSON（golden と同じ構造） |
| `import-export` | CSV → アプリ用／JIRA 用 CSV |
| `check-jira` | CSV の JIRA 取り込み互換性を検査 |
| `validate` | CSV が読めるか・計算できるかと警告の一覧 |
| `gen` | 架空のテスト用 CSV を生成 |
| `report` | CSV → ガント・統計・リストの HTML |
| `weekly` | CSV → 週報の Markdown／HTML |

`rmcli --help` と `rmcli <コマンド> --help` で詳細を表示します。終了コードは成功 `0`、入力・指定・出力のエラー `2`、内部エラー `1`。警告だけなら成功する場合があります。`--verbose` は診断用です。

## 計算条件

`schedule`・`import-export`・`report`・`weekly` では、次を指定できます。

| オプション | 内容／既定 |
|---|---|
| `--base YYYY-MM-DD` | 計算の基準日。省略時は実行する PC のローカル日付 |
| `--scenario 名前` | CSV の `Duration (weeks) @名前` に対応。既定「標準」 |
| `--level N` | 担当者が同時に持てる件数。1以上の整数。省略時は制限オフ |
| `--holidays ファイル` | 休業日のテキスト。省略時は組み込みの33日 |
| `-o ファイル` | ファイルへ出力。省略または `-o -` なら標準出力 |

**CSV だけを読みます。GUI の `project.settings.json` や `roadmap_prefs.json` は読みません。** GUI と同じ計算条件にしたい場合は、基準日・休業日・シナリオ・同時作業の制限を明示してください。CLI は GUI の履歴・ベースライン・関連リンク・添付・週報の所感やスナップショットを読み込んだり更新したりしません。

## 計算と CSV 変換

```sh
rmcli schedule samples/hierarchy_demo.csv --base 2026-10-19 --level 1 -o schedule.json
rmcli import-export input.csv --format app -o roundtrip.csv
rmcli import-export input.csv --format jira --base 2026-10-19 -o jira.csv
rmcli check-jira --raw jira.csv
rmcli validate input.csv
rmcli gen --count 5000 --seed 42 -o sample.csv
```

`import-export --format app` は入力値をアプリ形式の CSV に正規化します。元の任意の CSV とバイト一致する約束ではありません。JIRA 用では階層を3段に畳み、計算した計画日を出します。

`check-jira` は既定で取り込み後に JIRA 用へ変換した結果を検査します。`--raw` は既に書き出した CSV をそのまま検査します。実際の JIRA Cloud／Server への取り込みを試すものではありません。

## HTML レポート

```sh
rmcli report input.csv --base 2026-10-19 --name "開発計画" -o report.html
rmcli report input.csv --sections gantt --zoom week --color-by asg --no-deps -o gantt.html
rmcli report input.csv --sections stats,list --dark -o summary.html
```

| オプション | 値／既定 |
|---|---|
| `--name 名前` | 報告のプロジェクト名。既定は入力 CSV の拡張子を除くファイル名 |
| `--sections` | `gantt,stats,list` からカンマ区切り。既定はすべて |
| `--zoom` | `day` / `week` / `month` / `all`。既定 `week` |
| `--color-by` | `root` / `float` / `asg`。既定 `root` |
| `--dark` | ダーク配色。既定はライト |
| `--no-deps` | ガントの依存矢印を出さない |

GUI と同じ HTML 生成処理を使います。CLI のガント用紙は A4 横（297 × 210 mm）で、日・週・月の拡大率では全期間を読める幅の時間区間に分けます。`--zoom all` は全期間を横1ページに収めます。行が続く縦の区切りでは日付見出しを繰り返さず、ほかの節は A4 縦です。GUI の「画面に見えている日付範囲」と A3／A2 幅の選択、直接の PDF 保存は CLI にはありません。ベースライン・履歴を読まないので、それらが必要な EVM・推移などは GUI と同じ内容にはなりません。

## 週報

```sh
rmcli weekly input.csv --base 2026-10-19 --period this -o weekly.md
rmcli weekly input.csv --base 2026-10-19 --period last --format html -o weekly.html
rmcli weekly input.csv --from 2026-10-01 --to 2026-10-31 -o october.md
```

既定は Markdown、基準日を含む今週（月曜〜日曜）です。`--period last` は基準日の前の週です。`--from` と `--to` は両方指定し、`--period` と併用せず、両端を含む最大366日で指定します。計算もその期間の基準日に揃えたい場合は、`--base` を別途指定してください。

`Created`・`Updated` 列があれば「触った課題」の集計に使います。GUI の過去のスナップショットや所感は読みません。週ごとの差分やマイルストーンの予測推移など、過去の記録が必要な情報は出ません。

## 定期実行と出力先

定期実行で同じ日付の報告を再生成する場合は、`--base` と `--holidays` を固定してください。Windows のタスク スケジューラでは、プログラムに `rmcli.exe` の絶対パス、引数にコマンド・CSV の絶対パス・オプションを設定できます。

`report`・`weekly` の報告本文は標準出力または `-o` のファイル、読み込み・計算の注意は標準エラーへ出ます。文字コードや改行をシェルのリダイレクトで変えたくない場合は `-o` を使ってください。

HTML の出力先は `.html`／`.htm`、Markdown は `.md`／`.markdown`／`.txt` です。`--format` と拡張子は揃えてください。報告コマンドは入力 CSV・休業日ファイルと同じファイルへの出力を拒否します。別の既存ファイルは上書きするので、生成物用の場所を指定してください。
