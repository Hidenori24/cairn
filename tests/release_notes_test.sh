#!/bin/sh
# tools/release_notes.sh の確認（版が「-rc.1」などを含んでいても、前方一致で別の節を拾わない）
set -eu
script="$1"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
cat > "$tmp/CHANGELOG.md" <<'MD'
# 変更履歴

## 1.0.0-rc.2
rc2 の本文

## 1.0.0-rc.1
rc1 の本文
- 項目

## 1.0.0（未リリース）
1.0.0 の本文

## 0.10.0（2026-11-01）
0.10 の本文

## 0.1.0
0.1 の本文
MD

fail() { echo "NG: $1" >&2; exit 1; }
out="$("$script" 1.0.0-rc.1 "$tmp/CHANGELOG.md")"
[ "$(printf '%s\n' "$out" | head -n 1)" = "## 1.0.0-rc.1" ] || fail "rc.1 の見出し"
printf '%s\n' "$out" | grep -q 'rc1 の本文' || fail "rc.1 の本文"
printf '%s\n' "$out" | grep -q '項目' || fail "rc.1 の項目"
printf '%s\n' "$out" | grep -q 'rc2\|1.0.0 の本文' && fail "rc.1 に他の節が混ざる"

out="$("$script" 1.0.0 "$tmp/CHANGELOG.md")"
printf '%s\n' "$out" | grep -q '1.0.0 の本文' || fail "1.0.0 の本文"
printf '%s\n' "$out" | grep -q 'rc' && fail "1.0.0 に rc の節が混ざる"

out="$("$script" 0.10.0 "$tmp/CHANGELOG.md")"
printf '%s\n' "$out" | grep -q '0.10 の本文' || fail "0.10.0 の本文"
out="$("$script" 0.1.0 "$tmp/CHANGELOG.md")"
printf '%s\n' "$out" | grep -q '0.1 の本文' || fail "0.1.0 の本文"
printf '%s\n' "$out" | grep -q '0.10 の本文' && fail "0.1.0 に 0.10.0 の節が混ざる"
[ -z "$("$script" 9.9.9 "$tmp/CHANGELOG.md")" ] || fail "ない版は空"
echo "OK"
