#!/usr/bin/env python3
"""手書き数字の写真 10 枚から、アナログ時計の文字盤用グリフ (src/hand_digits.h) を作る。

   python3 tools/make_hand_digits.py --photos 0.jpg 1.jpg ... 9.jpg   # 写真から (0〜9 の順)
   python3 tools/make_hand_digits.py                                  # tools/hand_digits/*.png から作り直す

写真 → tools/hand_digits/N.png (白黒に整えた数字。手で修正してもよい) → src/hand_digits.h (8bit アルファ)
  - 明度 (RGB の最大値) で二値化するので、赤鉛筆の線や紙の影は消える
  - いちばん大きな塊と同程度の塊だけ残す (端の汚れや点を除く)
  - 写真ごとに撮影距離が違っても線の太さが揃うよう、高さを揃えてから太らせる / 細らせる
"""
import argparse
from collections import deque
from pathlib import Path

import numpy as np
from PIL import Image, ImageFilter

ROOT = Path(__file__).resolve().parents[1]
PNG_DIR = ROOT / "tools" / "hand_digits"
HEADER = ROOT / "src" / "hand_digits.h"

NORM_H = 240          # 整形時の高さ [px]
STROKE_RATIO = 0.085  # 線の太さ / 高さ
GLYPH_H = 72          # ファームに入れるグリフの高さ [px]。描画時に拡大縮小する
PAD = 8


def ink_mask(path: Path) -> np.ndarray:
    im = Image.open(path).convert("RGB")
    scale = 900 / max(im.size)
    if scale < 1:
        im = im.resize((round(im.width * scale), round(im.height * scale)), Image.LANCZOS)
    v = np.asarray(im).max(axis=2).astype(np.float32)   # 明度。赤や鉛筆の薄い線は明るいので落ちる
    v = np.asarray(Image.fromarray(v.astype(np.uint8)).filter(ImageFilter.GaussianBlur(1.5)), np.float32)
    paper = np.percentile(v, 60)
    ink = np.percentile(v, 0.5)
    return v < (paper + ink) / 2


def components(mask: np.ndarray):
    h, w = mask.shape
    label = np.zeros((h, w), np.int32)
    comps = []
    for y0, x0 in zip(*np.nonzero(mask)):
        if label[y0, x0]:
            continue
        n = len(comps) + 1
        label[y0, x0] = n
        q = deque([(y0, x0)])
        area = 0
        while q:
            y, x = q.popleft()
            area += 1
            for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                yy, xx = y + dy, x + dx
                if 0 <= yy < h and 0 <= xx < w and mask[yy, xx] and not label[yy, xx]:
                    label[yy, xx] = n
                    q.append((yy, xx))
        comps.append(area)
    return label, comps


def clean(mask: np.ndarray) -> np.ndarray:
    label, comps = components(mask)
    big = max(comps)
    keep = [i + 1 for i, a in enumerate(comps) if a >= big * 0.15]
    m = np.isin(label, keep)
    ys, xs = np.nonzero(m)
    return m[ys.min():ys.max() + 1, xs.min():xs.max() + 1]


def stroke_width(m: np.ndarray) -> float:
    area = m.sum()
    inner = m[1:-1, 1:-1] & m[:-2, 1:-1] & m[2:, 1:-1] & m[1:-1, :-2] & m[1:-1, 2:]
    perim = area - inner.sum()
    return 2.0 * area / max(perim, 1)


def normalize(m: np.ndarray) -> Image.Image:
    img = Image.fromarray((m * 255).astype(np.uint8))
    s = NORM_H / img.height
    img = img.resize((max(1, round(img.width * s)), NORM_H), Image.LANCZOS)
    img = Image.eval(img, lambda p: 255 if p >= 128 else 0)
    canvas = Image.new("L", (img.width + 2 * PAD * 4, img.height + 2 * PAD * 4), 0)
    canvas.paste(img, (PAD * 4, PAD * 4))
    w = stroke_width(np.asarray(canvas) > 0)
    target = NORM_H * STROKE_RATIO
    k = round(abs(target - w) / 2)
    for _ in range(k):   # 1px ずつ太らせる / 細らせる
        canvas = canvas.filter(ImageFilter.MaxFilter(3) if w < target else ImageFilter.MinFilter(3))
    canvas = canvas.filter(ImageFilter.GaussianBlur(2))
    canvas = Image.eval(canvas, lambda p: 255 if p >= 128 else 0)
    return canvas.crop(canvas.getbbox())


def to_glyph(img: Image.Image) -> Image.Image:
    s = GLYPH_H / img.height
    g = img.resize((max(1, round(img.width * s)), GLYPH_H), Image.LANCZOS)
    out = Image.new("L", (g.width + 2, g.height + 2), 0)   # 補間のため周りに 1px の余白
    out.paste(g, (1, 1))
    return out


def write_header(glyphs):
    lines = [
        "// 手書き数字 0〜9 のグリフ (8bit アルファ)。tools/make_hand_digits.py で生成。手で編集しない",
        "#pragma once",
        "#include <stdint.h>",
        "",
        "struct HandGlyph { uint8_t w, h; const uint8_t* alpha; };",
        "",
    ]
    for d, g in enumerate(glyphs):
        data = list(g.tobytes())
        lines.append(f"static const uint8_t HAND_DIGIT_{d}[{len(data)}] = {{")
        for i in range(0, len(data), 24):
            lines.append("  " + ",".join(str(v) for v in data[i:i + 24]) + ",")
        lines.append("};")
    lines.append("")
    lines.append(f"constexpr int HAND_GLYPH_H = {GLYPH_H + 2};   // 上下 1px の余白込み")
    lines.append("static const HandGlyph HAND_DIGITS[10] = {")
    for d, g in enumerate(glyphs):
        lines.append(f"  {{{g.width}, {g.height}, HAND_DIGIT_{d}}},")
    lines.append("};")
    HEADER.write_text("\n".join(lines) + "\n")
    print(f"{HEADER.relative_to(ROOT)}: {sum(g.width * g.height for g in glyphs)} bytes of glyphs")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--photos", nargs=10, metavar="IMG", help="0〜9 の順の写真")
    a = ap.parse_args()
    PNG_DIR.mkdir(parents=True, exist_ok=True)
    if a.photos:
        for d, p in enumerate(a.photos):
            m = clean(ink_mask(Path(p)))
            img = normalize(m)
            img.save(PNG_DIR / f"{d}.png", optimize=True)
            print(f"{d}: {p} -> {img.width}x{img.height}")
    write_header([to_glyph(Image.open(PNG_DIR / f"{d}.png").convert("L")) for d in range(10)])


if __name__ == "__main__":
    main()
