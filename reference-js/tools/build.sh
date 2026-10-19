#!/bin/sh
# src/main.jsx を js/app.js にまとめる（React は同梱）。初回だけ tools フォルダで `npm install` が必要です。
cd "$(dirname "$0")/.."
NODE_PATH="$PWD/tools/node_modules" "$PWD/tools/node_modules/.bin/esbuild" src/main.jsx --bundle --format=iife --jsx=automatic --minify --target=es2019 --define:process.env.NODE_ENV=\"production\" --outfile=js/app.js --log-level=warning
