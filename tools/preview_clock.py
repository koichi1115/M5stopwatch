#!/usr/bin/env python3
"""アナログ時計 (手書き数字) のレイアウト確認用プレビュー。実機なしで見た目を確認する。
src/config.h の CLOCKFACE_* と src/hand_digits.h を読み、src/clock.h の drawAnalog をなぞって PNG を出す。
   python3 tools/preview_clock.py out.png [10:08:42] [--day 17]
"""
import argparse, math, re
from pathlib import Path
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
SS = 4   # スーパーサンプリング


def rgb565(c):
    return ((c >> 11 & 31) * 255 // 31, (c >> 5 & 63) * 255 // 63, (c & 31) * 255 // 31)


def load_config():
    cfg = {}
    for m in re.finditer(r"constexpr\s+\w+\s+(\w+)\s*=\s*([0-9.]+|0x[0-9A-Fa-f]+)f?;", (ROOT / "src/config.h").read_text()):
        v = m.group(2)
        cfg[m.group(1)] = int(v, 16) if v.lower().startswith("0x") else float(v)
    return cfg


def load_glyphs():
    text = (ROOT / "src/hand_digits.h").read_text()
    sizes = re.findall(r"\{(\d+), (\d+), HAND_DIGIT_(\d)\}", text)
    glyphs = {}
    for w, h, d in sizes:
        body = re.search(rf"HAND_DIGIT_{d}\[\d+\] = \{{(.*?)\}};", text, re.S).group(1)
        data = bytes(int(v) for v in re.findall(r"\d+", body))
        glyphs[int(d)] = Image.frombytes("L", (int(w), int(h)), data)
    gh = int(re.search(r"HAND_GLYPH_H = (\d+)", text).group(1))
    return glyphs, gh


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("time", nargs="?", default="10:08:42")
    ap.add_argument("--day", type=int, default=17)
    a = ap.parse_args()
    hh, mm, ss = (int(v) for v in a.time.split(":"))
    C = load_config()
    G, GH = load_glyphs()
    size = 466
    img = Image.new("RGB", (size * SS, size * SS), "black")
    d = ImageDraw.Draw(img)
    c = size / 2 * SS
    k = SS   # 顔スプライト (380) は画面 (466) の中央に等倍で置かれる

    def glyph_w(n, h):
        return G[n].width * h / GH

    def number_w(n, h):
        return glyph_w(n, h) if n < 10 else glyph_w(n // 10, h) + h * C["CLOCKFACE_DIGIT_GAP"] + glyph_w(n % 10, h)

    def number(n, cx, cy, h, col):
        x = cx - number_w(n, h) / 2
        parts = [n // 10, n % 10] if n >= 10 else [n]
        for i, g in enumerate(parts):
            gi = G[g].resize((max(1, round(G[g].width * h / GH)), round(h)), Image.BILINEAR)
            img.paste(Image.new("RGB", gi.size, col), (round(x), round(cy - h / 2)), gi)
            x += glyph_w(g, h) + h * C["CLOCKFACE_DIGIT_GAP"]

    def polar(deg, r):
        t = math.radians(deg - 90)
        return c + math.cos(t) * r, c + math.sin(t) * r

    def hand(deg, r1, w0, w1, col):
        t = math.radians(deg - 90)
        nx, ny = -math.sin(t), math.cos(t)
        x1, y1 = polar(deg, r1)
        d.polygon([(c + nx * w0, c + ny * w0), (x1 + nx * w1, y1 + ny * w1),
                   (x1 - nx * w1, y1 - ny * w1), (c - nx * w0, c - ny * w0)], fill=col)
        d.ellipse([c - w0, c - w0, c + w0, c + w0], fill=col)
        d.ellipse([x1 - w1, y1 - w1, x1 + w1, y1 + w1], fill=col)

    for i in range(60):
        ro = C["CLOCKFACE_TICK_R"] * k
        if i % 5 == 0:
            d.line([polar(i * 6, ro - C["CLOCKFACE_TICK_LEN"] * k), polar(i * 6, ro)], fill=rgb565(C["COLOR_CLOCK_TICK"]), width=round(3 * k))
        else:
            x, y = polar(i * 6, ro - 2 * k)
            r = 1.6 * k
            d.ellipse([x - r, y - r, x + r, y + r], fill=rgb565(C["COLOR_CLOCK_OFF_TICK"]))
    h = C["CLOCKFACE_DIGIT_H"] * k
    for n in range(1, 13):
        t = math.radians(n * 30 - 90)
        r = C["CLOCKFACE_DIGIT_OUTER"] * k - (abs(math.cos(t)) * number_w(n, h) + abs(math.sin(t)) * h) / 2
        number(n, c + math.cos(t) * r, c + math.sin(t) * r, h, rgb565(C["COLOR_CLOCK_DIGIT"]))
    number(a.day, c, c + C["CLOCKFACE_DAY_Y"] * k, C["CLOCKFACE_DAY_H"] * k, rgb565(0x8410))
    hand((hh % 12 + mm / 60) * 30, C["CLOCKFACE_HOUR_LEN"] * k, 4.5 * k, 2 * k, rgb565(C["COLOR_CLOCK_HAND"]))
    hand((mm + ss / 60) * 6, C["CLOCKFACE_MIN_LEN"] * k, 3.5 * k, 1.25 * k, rgb565(C["COLOR_CLOCK_HAND"]))
    sec = rgb565(C["COLOR_CLOCK_SECOND"])
    hand(ss * 6 + 180, C["CLOCKFACE_SEC_TAIL"] * k, 1 * k, 1 * k, sec)
    hand(ss * 6, C["CLOCKFACE_SEC_LEN"] * k, 1 * k, 0.5 * k, sec)
    d.ellipse([c - 8 * k, c - 8 * k, c + 8 * k, c + 8 * k], fill=sec)
    d.ellipse([c - 3 * k, c - 3 * k, c + 3 * k, c + 3 * k], fill="black")
    # 画面の外 (円の外) を暗く
    mask = Image.new("L", img.size, 0)
    ImageDraw.Draw(mask).ellipse([0, 0, img.width - 1, img.height - 1], fill=255)
    out = Image.new("RGB", img.size, (40, 40, 40))
    out.paste(img, (0, 0), mask)
    out.resize((size, size), Image.LANCZOS).save(a.out)
    print(a.out)


if __name__ == "__main__":
    main()
