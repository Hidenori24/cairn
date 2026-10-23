#!/usr/bin/env python3
"""app/resources/app.ico を作る（標準ライブラリだけ）。Cairn（ケルン）＝石を三つ積んだ図案。
青い角丸の四角の上に明るい石を積むので、ライト・ダークどちらの背景（タスクバー・エクスプローラ）でも見える。
使い方: python3 tools/make_icon.py [--png 出力.png]   （--png は 256px の確認用の画像も書く）
"""
import os
import struct
import sys
import zlib

BG = (37, 99, 235)  # --accent（アプリのアクセント色）
# 下から上へ（中心の x・y, 半径 rx・ry, 色〔上の明るい側, 下の影の側〕）。座標は 0..1
STONES = [
    (0.50, 0.715, 0.315, 0.125, (232, 238, 247), (186, 198, 219)),
    (0.53, 0.540, 0.245, 0.100, (244, 247, 252), (199, 210, 229)),
    (0.47, 0.385, 0.170, 0.080, (255, 255, 255), (214, 223, 238)),
]
GAP = 0.018  # 石と石の間の隙間（背景色の縁）。小さい寸法でも三つに見える
SS = 4       # 縦横の超サンプリング（縁をなめらかにする）


def lerp(a, b, t):
    return tuple(a[i] + (b[i] - a[i]) * t for i in range(3))


def sample(u, v):
    """0..1 の座標 (u, v) の色。(r, g, b, a) を返す。"""
    r = 0.18
    cx = min(max(u, r), 1 - r)
    cy = min(max(v, r), 1 - r)
    if (u - cx) ** 2 + (v - cy) ** 2 > r * r:
        return (0, 0, 0, 0.0)
    col = BG
    for (x, y, rx, ry, top, bot) in STONES:
        # 縁（背景色）→ 石 の順に塗る
        if ((u - x) / (rx + GAP)) ** 2 + ((v - y) / (ry + GAP * 0.8)) ** 2 <= 1.0:
            col = BG
        if ((u - x) / rx) ** 2 + ((v - y) / ry) ** 2 <= 1.0:
            t = (v - (y - ry)) / (2 * ry)  # 石の上端 0 → 下端 1
            col = lerp(top, bot, min(max(t, 0.0), 1.0) ** 1.4)
    return col + (1.0,)


def image(n):
    px = []
    for y in range(n):
        row = []
        for x in range(n):
            ar = ag = ab = aa = 0.0
            for sy in range(SS):
                for sx in range(SS):
                    r, g, b, a = sample((x + (sx + 0.5) / SS) / n, (y + (sy + 0.5) / SS) / n)
                    ar += r * a
                    ag += g * a
                    ab += b * a
                    aa += a
            if aa == 0:
                row.append((0, 0, 0, 0))
            else:
                row.append((round(ar / aa), round(ag / aa), round(ab / aa), round(255 * aa / (SS * SS))))
        px.append(row)
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


def png(px):
    n = len(px)
    raw = bytearray()
    for row in px:
        raw.append(0)
        for (r, g, b, a) in row:
            raw += bytes((r, g, b, a))

    def chunk(tag, body):
        c = struct.pack(">I", len(body)) + tag + body
        return c + struct.pack(">I", zlib.crc32(tag + body) & 0xFFFFFFFF)

    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", n, n, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b""))


def main():
    sizes = [16, 20, 24, 32, 40, 48, 64, 128, 256]
    imgs = []
    for n in sizes:
        px = image(n)
        imgs.append(png(px) if n >= 128 else bmp(px))  # 128 以上は PNG 圧縮（Vista 以降）
    out = struct.pack("<HHH", 0, 1, len(sizes))
    off = 6 + 16 * len(sizes)
    for n, b in zip(sizes, imgs):
        out += struct.pack("<BBBBHHII", n % 256, n % 256, 0, 0, 1, 32, len(b), off)
        off += len(b)
    out += b"".join(imgs)
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "app", "resources", "app.ico")
    with open(path, "wb") as f:
        f.write(out)
    print("wrote", os.path.normpath(path), len(out), "bytes")
    if "--png" in sys.argv:
        dst = sys.argv[sys.argv.index("--png") + 1]
        with open(dst, "wb") as f:
            f.write(png(image(256)))
        print("wrote", dst)


if __name__ == "__main__":
    main()
