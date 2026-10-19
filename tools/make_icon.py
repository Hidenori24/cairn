#!/usr/bin/env python3
"""app/resources/app.ico を作る（外部ライブラリなし）。ガントのバーを模した単純なアイコン。
使い方: python3 tools/make_icon.py
"""
import os
import struct

BG = (37, 99, 235)      # --accent
BAR = (255, 255, 255)


def image(n):
    px = [[(0, 0, 0, 0)] * n for _ in range(n)]
    r = n * 0.18
    for y in range(n):
        for x in range(n):
            # 角丸の四角
            cx = min(max(x + 0.5, r), n - r)
            cy = min(max(y + 0.5, r), n - r)
            if (x + 0.5 - cx) ** 2 + (y + 0.5 - cy) ** 2 <= r * r:
                px[y][x] = BG + (255,)
    bars = [(0.18, 0.55, 0.22), (0.35, 0.80, 0.45), (0.55, 0.85, 0.68)]
    h = max(1, round(n * 0.12))
    for x0, x1, yc in bars:
        for y in range(round(n * yc - h / 2), round(n * yc + h / 2)):
            for x in range(round(n * x0), round(n * x1)):
                px[y][x] = BAR + (255,)
    return px


def bmp(px):
    n = len(px)
    head = struct.pack("<IiiHHIIiiII", 40, n, n * 2, 1, 32, 0, 0, 0, 0, 0, 0)
    data = bytearray()
    for row in reversed(px):
        for (r, g, b, a) in row:
            data += bytes((b, g, r, a))
    mask = bytes(((n + 31) // 32) * 4 * n)
    return head + bytes(data) + mask


def main():
    sizes = [16, 24, 32, 48, 64]
    imgs = [bmp(image(n)) for n in sizes]
    out = struct.pack("<HHH", 0, 1, len(sizes))
    off = 6 + 16 * len(sizes)
    for n, b in zip(sizes, imgs):
        out += struct.pack("<BBBBHHII", n % 256, n % 256, 0, 0, 1, 32, len(b), off)
        off += len(b)
    out += b"".join(imgs)
    path = os.path.join(os.path.dirname(__file__), "..", "app", "resources", "app.ico")
    with open(path, "wb") as f:
        f.write(out)
    print("wrote", os.path.normpath(path), len(out), "bytes")


if __name__ == "__main__":
    main()
