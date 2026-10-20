#!/usr/bin/env python3
"""画面の文言に使っている文字（ASCII 以外）を集めて app/ui/ui_glyphs.inc を作る。
フォントの作成時に、日本語の範囲（約3,000字）に加えてこの文字を必ず入れる（字の欠けを防ぐ）。
文言を変えたら実行する（UI の煙テストが、ソースとこのファイルの食い違いを検出する）。
使い方: python3 tools/gen_ui_glyphs.py
"""
import glob
import os

ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), ".."))
PATTERNS = ["app/ui/*.cpp", "app/ui/*.hpp", "app/controller/*.cpp", "core/src/*.cpp", "core/include/roadmap/*.hpp"]


def collect():
    cps = set()
    for pat in PATTERNS:
        for p in sorted(glob.glob(os.path.join(ROOT, pat))):
            with open(p, encoding="utf-8") as f:
                for ch in f.read():
                    if 0x80 <= ord(ch) <= 0xFFFF:
                        cps.add(ord(ch))
    return sorted(cps)


def main():
    cps = collect()
    lines = ["// 自動生成（tools/gen_ui_glyphs.py）。画面の文言に使っている文字。編集しない"]
    for i in range(0, len(cps), 12):
        lines.append(", ".join("0x%04X" % c for c in cps[i:i + 12]) + ",")
    out = os.path.join(ROOT, "app", "ui", "ui_glyphs.inc")
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    print("wrote", out, len(cps), "chars")


if __name__ == "__main__":
    main()
