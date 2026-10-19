import React from "react";
import { createRoot } from "react-dom/client";
import * as core from "./core/core.js";
import * as state from "./core/state.js";
import * as actions from "./core/actions.js";
import { App } from "./App.jsx";

core.init();
core.fsInit();   // 前回選んだ作業フォルダがあれば、つなぎ直して内容をそろえる
window.addEventListener("beforeunload", (e) => { if (core.fsPending()) { e.preventDefault(); e.returnValue = ""; } });   // 書き込み待ちのまま閉じるのを防ぐ
window.__rm = { core: core.core, ops: core, ui: state.ui, state, actions };   // テスト・デバッグ用の入口
createRoot(document.getElementById("root")).render(<App />);
