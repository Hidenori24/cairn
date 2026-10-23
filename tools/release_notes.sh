#!/bin/sh
# docs/CHANGELOG.md から、指定した版の節（「## <版>」の見出しから次の「## 」の前まで）を標準出力へ出す。
# CI のリリース（.github/workflows/ci.yml）のリリースノートに使う。
# 使い方: tools/release_notes.sh <版（v なし。例: 1.0.0-rc.1）> <CHANGELOG.md>
# 見出しは「## <版>」の直後が英数字・「.」「_」「-」でないときだけ一致させる
# （「1.0.0」が「## 1.0.0-rc.1」や「## 1.0.01」の節に一致しない。「## 0.10.0（未リリース）」には一致する）。
if [ $# -ne 2 ]; then
  echo "使い方: $0 <版> <CHANGELOG.md>" >&2
  exit 2
fi
awk -v v="## $1" '
  index($0, "## ") == 1 {
    p = 0
    if (index($0, v) == 1) {
      rest = substr($0, length(v) + 1)
      p = (rest !~ /^[0-9A-Za-z._-]/)
    }
  }
  p' "$2"
