#!/usr/bin/env python3
"""顔のレイアウト確認用プレビュー (実機なしで見た目を確認する)。
src/config.h と src/face.h の幾何をなぞって PNG を出力する。ロジックの完全な再現ではない。
   python3 tools/preview_face.py out.png
"""
import math, random, sys
from PIL import Image, ImageDraw, ImageFont

W = H = 468
FACE_SIZE = 380
EYE_OFFSET_X, EYE_OFFSET_Y = 84, 4
EYE_RADIUS, PUPIL_RADIUS = 70, 34
PUPIL_TRAVEL = 0.80
FUR = (0x29 << 3, 0x28 << 2, 0x05 << 3)  # 0x2945 rgb565 -> approx rgb888

def rgb565(c):
    r = (c >> 11) & 31; g = (c >> 5) & 63; b = c & 31
    return (r * 255 // 31, g * 255 // 63, b * 255 // 31)

COLOR_FUR = rgb565(0x31A6); COLOR_FUR_TIP = rgb565(0x528A); COLOR_BLUSH = rgb565(0xFB56); COLOR_ZZZ = rgb565(0x9CD3)

def draw_curve(d, cx, cy, rx, ry, a0, a1, width, color):
    pts = []
    for k in range(15):
        t = math.radians(a0 + (a1 - a0) * k / 14)
        pts.append((cx + rx * math.cos(t), cy + ry * math.sin(t)))
    d.line(pts, fill=color, width=int(width * 2), joint="curve")

def draw_face(mood="awake", gaze=(0.4, -0.2), open_=1.0, eye_scale=1.0, pupil_scale=1.0, blush=0.0):
    img = Image.new("RGBA", (FACE_SIZE, FACE_SIZE), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    c = FACE_SIZE / 2
    for i in (-1, 1):
        cx, cy = c + i * EYE_OFFSET_X, c + EYE_OFFSET_Y
        er, pr = EYE_RADIUS * eye_scale, PUPIL_RADIUS * pupil_scale
        if mood == "happy":
            draw_curve(d, cx, cy + er * 0.25, er * 0.95, er * 0.85, 205, 335, 7, "white"); continue
        if open_ < 0.07:
            ry = er * 0.35 if mood == "sleep" else er * 0.12
            draw_curve(d, cx, cy - er * 0.15, er * 0.9, ry, 20, 160, 6.5, "white"); continue
        ry = er * open_
        d.ellipse([cx - er, cy - ry, cx + er, cy + ry], fill="white")
        travel = (er - pr) * PUPIL_TRAVEL
        px, py = cx + gaze[0] * travel, cy + gaze[1] * travel * open_
        d.ellipse([px - pr, py - pr * open_, px + pr, py + pr * open_], fill="black")
        if open_ > 0.35:
            hr = pr * 0.24
            hx, hy = px - pr * 0.36, py - pr * 0.36 * open_
            d.ellipse([hx - hr, hy - hr, hx + hr, hy + hr], fill="white")
    if blush > 0.05:
        for i in (-1, 1):
            bx, by = c + i * (EYE_OFFSET_X + 8), c + EYE_OFFSET_Y + EYE_RADIUS + 26
            d.ellipse([bx - 28, by - 11, bx + 28, by + 11], fill=COLOR_BLUSH)
    if mood == "sleep":
        try: font = ImageFont.truetype("DejaVuSans-Bold.ttf", 26)
        except Exception: font = ImageFont.load_default()
        bx, by = c + EYE_OFFSET_X + EYE_RADIUS - 4, c + EYE_OFFSET_Y - EYE_RADIUS + 4
        for k in range(3):
            ph = (0.3 + k * 0.33) % 1.0
            d.text((bx + 14 * k + 18 * ph, by - 22 * k - 46 * ph), "z", fill=COLOR_ZZZ, font=font, anchor="mm")
    return img

def render(angle, **kw):
    scr = Image.new("RGB", (W, H), "black")
    d = ImageDraw.Draw(scr)
    cx, cy, R = W / 2, H / 2, W / 2 + 2
    rnd = random.Random(12345)
    for i in range(120):
        a = math.radians(angle + i * 360 / 120 + rnd.randrange(30) / 10)
        ln = 12 + rnd.randrange(19); w = 1.4 + rnd.randrange(10) / 6
        x0, y0 = cx + math.cos(a) * R, cy + math.sin(a) * R
        x1, y1 = cx + math.cos(a) * (R - ln), cy + math.sin(a) * (R - ln)
        d.line([(x0, y0), (x1, y1)], fill=COLOR_FUR, width=int(w * 2))
        d.ellipse([x1 - w, y1 - w, x1 + w, y1 + w], fill=COLOR_FUR_TIP)
    face = draw_face(**kw).rotate(-angle, resample=Image.BICUBIC, expand=False)  # PIL は反時計回りが正
    scr.paste(face, (int(cx - FACE_SIZE / 2), int(cy - FACE_SIZE / 2)), face)
    # 丸いパネルの外をマスク (実機は円形)
    mask = Image.new("L", (W, H), 0); ImageDraw.Draw(mask).ellipse([1, 1, W - 2, H - 2], fill=255)
    out = Image.new("RGB", (W, H), (40, 40, 40)); out.paste(scr, (0, 0), mask)
    return out

if __name__ == "__main__":
    tiles = [
        render(0, mood="awake", gaze=(0.5, -0.3)),
        render(35, mood="awake", gaze=(-0.6, 0.2), open_=0.45),
        render(-90, mood="drowsy", gaze=(0.1, 0.4), open_=0.45),
        render(150, mood="sleep", open_=0.0),
        render(0, mood="surprised", gaze=(0.05, 0.02), eye_scale=1.16, pupil_scale=0.62),
        render(20, mood="happy", blush=1.0),
    ]
    sheet = Image.new("RGB", (W * 3 + 40, H * 2 + 30), (60, 60, 60))
    for i, t in enumerate(tiles):
        sheet.paste(t, (10 + (i % 3) * (W + 10), 10 + (i // 3) * (H + 10)))
    sheet.save(sys.argv[1] if len(sys.argv) > 1 else "preview.png")
    print("saved")
