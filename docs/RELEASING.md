# リリースと配布

## CI の実行と成果物の保持

- `main` への push と `v*` タグで CI が動きます。作業ブランチの検査は PR 作成・更新時に行い、ブランチの push と PR 更新で同じ検査が二重に走るのを防ぎます。PR 作成前に検査したい場合は、そのブランチを選んで `workflow_dispatch` を実行できます。
- PR 更新では同じ PR の古い実行をキャンセルします。main・タグ・手動実行とは別のグループで管理し、公開処理を PR の更新で取り消さないようにします。
- GCC・Clang・UI・clang-tidy は各15分、Windows は25分、公開ジョブは各10分を上限とします。各検査とインストーラの確認は従来どおり実行します。
- Actions の `Cairn-windows` 成果物は7日保存します。GitHub Releases に添付した配布物の保持期間は変わりません。`artifact_run` で古い成果物を使う場合は、期限内であることを確認してください。期限切れなら対象コミットから再ビルドが必要です。この保持期限は既存の成果物には遡って適用されません。

## 正式版を出す手順

1. 対象 PR を main にマージし、そのコミットの CI 全体が成功したことを確認します（GCC・Clang・UI・clang-tidy・Windows）。
2. 版を揃えます。`CMakeLists.txt`、`vcpkg.json`、`app/resources/app.rc`（数値と文字列の両方）、`app/resources/app.manifest`、`cli/rmcli.manifest` が対象です。インストーラとアプリ内の版表示は CMake から引き継ぎます。
3. `docs/CHANGELOG.md` に対象版の節を作り、公開日・変更・互換上の注意を記載します。生成処理は `tools/release_notes.sh` で、その見出しの節だけを使います。
4. 未タグであることを確認し、CI が成功したコミットにタグを付けて push します。既存の公開タグは動かしません。

```sh
git fetch origin --tags
git ls-remote --tags origin 'v1.1.0*'
# <CIが成功したコミットSHA> は実際の値に置き換える
git tag -a v1.1.0 <CIが成功したコミットSHA> -m "Cairn v1.1.0"
git push origin refs/tags/v1.1.0
```

5. タグの CI が再度ビルド・テストして GitHub Release を公開します。**タグを付けただけでは配布の完了ではありません。** 最後の `release` ジョブと、次の3ファイルの添付を確認します。

| 配布物 | 内容 |
|---|---|
| `Cairn-Setup-<版>.exe` | インストーラ |
| `Cairn-windows-x64.zip` | Cairn・rmcli・説明・サンプル・データ用フォルダ |
| `SHA256SUMS.txt` | 配布物などの SHA-256 |

タグに `-` を含む `v1.2.0-rc.1` などは pre-release になり、「最新」にはしません。ハイフンのない版は正式版として扱います。タグと CMake の版の一致は公開前に確認してください（タグから版を自動で書き換える仕組みではありません）。

### Actions から作る場合

GitHub Actions → `ci` → `Run workflow` で、main を選び `release_tag` を指定する方法もあります。

| 入力 | 通常の正式版 |
|---|---|
| `release_tag` | `v1.1.0` など |
| `target` | 空（実行したブランチのコミット） |
| `artifact_run` | 空（この実行で作る配布物） |
| `keep_notes` | 空 |

`target` は過去のコミット、`artifact_run` は過去の CI の成果物を使うための保守用です。成果物と対象コミットの対応を確認して使います。`keep_notes=true` は既存リリースの説明を保ち、zip とチェックサムのみを差し替えます（インストーラは追加しません）。

現行ワークフローの公開ジョブの `needs` は Linux・UI・Windows です。clang-tidy は公開ジョブの依存に入っていないため、公開する前に CI 全体の成功を確認します。

## ローカルで配布フォルダを作る

```sh
cmake --install build/windows-release --component portable --prefix dist/Cairn
```

Windows で Inno Setup の `ISCC` を使います。例はリポジトリ直下からの実行です。版は CMake と揃えます。

```text
ISCC /DAppVersion=1.1.0 /DSourceDir=<絶対パス>\dist\Cairn /DRepoDir=<リポジトリの絶対パス> /DOutDir=<絶対パス>\dist packaging\installer\Cairn.iss
```

AppId・設定ファイルの旧名・データの退避先は、以前の版から更新できるよう変更しません。

## コード署名

Windows 配布 exe の署名には、信頼された認証局が発行し、秘密鍵を含む PFX 形式の Authenticode コード署名証明書を使います。ワークフローは次の **GitHub Actions repository secrets** が両方設定されていると、インストーラとポータブル版の exe に署名して検証します。片方だけ設定されている場合はエラーになります。`v1.2.1` と `v1.3.0` は署名必須で、設定がなければ Windows ジョブを失敗させます。両方未設定の場合は署名なしで続行します。

- `CAIRN_SIGNING_CERTIFICATE_BASE64`: 信頼された認証局のコード署名証明書（秘密鍵を含む PFX）を Base64 化した値。
- `CAIRN_SIGNING_CERTIFICATE_PASSWORD`: PFX のパスワード。

GitHub の `Settings → Secrets and variables → Actions → New repository secret` で登録します。PFX を Base64 にする例（PowerShell。出力をチャットや Issue に貼らず、そのまま secret に登録してください）:

```powershell
[Convert]::ToBase64String([IO.File]::ReadAllBytes("C:\path\to\codesign.pfx")) | Set-Clipboard
```

証明書は発行者が配布するプログラムに署名するためのもので、ダウンロードする各 PC に購入・設定するものではありません。自己署名証明書を付けても、一般の PC が信頼する署名にはなりません。信頼された署名があっても、SmartScreen の評判などにより警告が出る場合があります。

`SHA256SUMS.txt` はダウンロードしたファイルの一致を確かめるためのもので、発行者を認証するコード署名とは別です。

```powershell
Get-FileHash .\Cairn-windows-x64.zip -Algorithm SHA256
```

## 利用状況を把握できる範囲

Cairn は利用状況の送信・利用者 ID・起動回数の収集を実装していません。**実際の利用人数や継続利用率は分かりません。**

| 指標 | 見方と限界 |
|---|---|
| 配布ファイルのダウンロード数 | GitHub Releases API の各 asset の `download_count`。累計の回数で、重複・開発者の確認・自動取得を含みます。インストール数でも人数でもありません |
| リポジトリの閲覧・clone | 権限のあるメンテナーが [Insights → Traffic](https://github.com/Hidenori24/cairn/graphs/traffic) で確認。通常は直近14日間で、アプリの起動とは別です |
| Stars・forks・Issue | 関心やフィードバックの目安。利用者全体の数ではありません |

GitHub CLI で配布ファイルの回数を確認する例（メンテナー用の `gh` であり、Cairn の `rmcli` ではありません）:

```sh
gh api --paginate repos/Hidenori24/cairn/releases --jq '.[] | .tag_name as $tag | .assets[] | [$tag, .name, .download_count] | @tsv'
```

「入手された回数」を見るときはインストーラとポータブル zip を対象にし、チェックサムを加算しません。両方を取る人もいるため、合計も人数には換算しません。再添付・再作成された asset は同じファイル名でも別のカウンターになります。長期の推移が必要なら、日付付きで数値を控えて比較します。
