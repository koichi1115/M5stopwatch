#!/usr/bin/env python3
"""ブラックホール表示の対応表 (src/blackhole_lut.h) を作る。ついでにプレビュー動画も出せる。

   python3 tools/make_blackhole.py                       # ヘッダを作る
   python3 tools/make_blackhole.py --preview out.gif     # 実機と同じ計算でアニメーションを書き出す

シュバルツシルト・ブラックホール (質量 M = 1, 地平線 r = 2) の周りを、顔スプライト (380x380) の
各画素から光線を逆にたどる (重力レンズ)。光線が降着円盤 (赤道面, 内縁 r = 6 = ISCO) に当たった画素には
  - 当たった半径 (リング番号 0..RINGS-1) と方位角 (0..255)
  - 明るさ (円盤の放射 × ドップラー / 重力赤方偏移 g^4。近づく側が明るい)
を記録する。光線がブラックホールに落ちた画素は黒 (影)、外へ抜けた画素は抜けた方向の星を置く。
実機では毎フレーム、リングごとにケプラー回転 (内側ほど速い) の分だけ方位角をずらして模様を引くだけ。
"""
import argparse
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "src" / "blackhole_lut.h"

SIZE = 380              # 顔スプライトの一辺
VIEW_R = 188            # この半径の円の中だけ描く (スプライトが回るので内接円の外は見えない)
INCL_DEG = 80.0         # 円盤の法線から見た視線の角度 (90 で真横)
FOV_M = 16.0            # 画面の半径 VIEW_R が何 M にあたるか
CAM_D = 80.0            # 光線の出発点の距離
R_IN, R_OUT = 6.0, 15.5 # 円盤の内縁 / 外縁
RINGS = 64              # 半径方向のリング数
ANG = 256               # 方位角の分割数
STAR_DENSITY = 0.004
SEED = 7


def trace():
    rng_px = np.arange(SIZE) + 0.5 - SIZE / 2
    px, py = np.meshgrid(rng_px, rng_px)
    inside = px * px + py * py < VIEW_R * VIEW_R
    a = px[inside] * FOV_M / VIEW_R          # 画面の右 = +a
    b = -py[inside] * FOV_M / VIEW_R         # 画面の上 = +b
    i = np.radians(INCL_DEG)
    o = np.array([0.0, -np.sin(i), np.cos(i)])        # ブラックホールから観測者への向き
    ex = np.array([1.0, 0.0, 0.0])
    ey = np.array([0.0, np.cos(i), np.sin(i)])
    n = a.size
    x = CAM_D * o + a[:, None] * ex + b[:, None] * ey
    v = np.tile(-o, (n, 1))
    lz = (np.cross(x, -v))[:, 2]             # 観測者へ向かう光子の角運動量 z 成分 (保存量)
    h2 = np.sum(np.cross(x, v) ** 2, axis=1)

    state = np.zeros(n, np.int8)     # 0 進行中, 1 円盤, 2 吸い込まれた, 3 抜けた
    hit_r = np.zeros(n)
    hit_phi = np.zeros(n)
    out_dir = np.zeros((n, 3))
    act = np.arange(n)
    for _ in range(3000):
        if act.size == 0:
            break
        p, w = x[act], v[act]
        r = np.linalg.norm(p, axis=1)
        h = np.clip(0.04 * (r - 1.6), 0.01, 2.0)[:, None]
        ha = h2[act][:, None]
        # RK4。光線の加速度 a = -(3/2) rs h^2 x / r^5 (rs = 2) でシュバルツシルト時空の光の道筋になる
        def f(pp):
            r2 = np.sum(pp * pp, axis=1)
            return -3.0 * ha * pp / (r2 ** 2.5)[:, None]
        k1x, k1v = w, f(p)
        k2x, k2v = w + 0.5 * h * k1v, f(p + 0.5 * h * k1x)
        k3x, k3v = w + 0.5 * h * k2v, f(p + 0.5 * h * k2x)
        k4x, k4v = w + h * k3v, f(p + h * k3x)
        pn = p + h / 6 * (k1x + 2 * k2x + 2 * k3x + k4x)
        wn = w + h / 6 * (k1v + 2 * k2v + 2 * k3v + k4v)
        # 赤道面を横切ったか
        cross = (p[:, 2] * pn[:, 2] < 0)
        t = p[:, 2] / np.where(cross, p[:, 2] - pn[:, 2], 1)
        q = p + (pn - p) * t[:, None]
        rq = np.hypot(q[:, 0], q[:, 1])
        disk = cross & (rq >= R_IN) & (rq <= R_OUT)
        rn = np.linalg.norm(pn, axis=1)
        fall = ~disk & (rn < 2.02)
        esc = ~disk & ~fall & (rn > CAM_D * 1.2) & (np.sum(pn * wn, axis=1) > 0)
        ids = act[disk]
        state[ids] = 1
        hit_r[ids] = rq[disk]
        hit_phi[ids] = np.arctan2(q[disk, 1], q[disk, 0])
        state[act[fall]] = 2
        state[act[esc]] = 3
        out_dir[act[esc]] = wn[esc] / np.linalg.norm(wn[esc], axis=1)[:, None]
        x[act], v[act] = pn, wn
        act = act[state[act] == 0]
    state[act] = 2   # 回りきれなかった光線 (光子球のすぐ近く) は影に含める
    return inside, state, hit_r, hit_phi, lz, out_dir


def disk_brightness(r, lz):
    omega = r ** -1.5
    g = np.sqrt(1 - 3 / r) / (1 - omega * lz)          # 円盤の回転は +z まわり (左から右へ近づく)
    emis = r ** -3 * (1 - np.sqrt(R_IN / r))              # 標準円盤の放射 (内縁で 0 から立ち上がる)
    fade = np.clip((R_OUT - r) / 2.5, 0, 1)               # 外縁はなだらかに消す
    return emis * g ** 4 * fade


def stars(dirs):
    # 方向を細かい格子に分け、格子ごとに乱数で星を置く (外へ抜けた光線の方向なので重力レンズで歪む)
    th = np.arccos(np.clip(dirs[:, 2], -1, 1))
    ph = np.arctan2(dirs[:, 1], dirs[:, 0])
    gi = (th / np.pi * 700).astype(np.int64) * 4096 + ((ph + np.pi) / (2 * np.pi) * 1400).astype(np.int64)
    hsh = (gi * 2654435761 + SEED * 97531) % 4294967296
    u = (hsh % 100000) / 100000
    lum = (hsh // 100000 % 1000) / 1000
    return np.where(u < STAR_DENSITY, 40 + lum * lum * 180, 0).astype(np.uint8)


def texture():
    # リング x 方位角の模様 (0..255)。方位方向には周期的、半径方向にはなめらかに変わるガスの縞
    rng = np.random.default_rng(SEED)
    ang = np.arange(ANG) / ANG * 2 * np.pi
    tex = np.zeros((RINGS, ANG))
    base = rng.normal(size=(RINGS + 8,))
    base = np.convolve(base, np.ones(5) / 5, "same")
    for ring in range(RINGS):
        row = np.full(ANG, 0.55 + 0.18 * base[ring])
        for k, amp in ((2, 0.10), (3, 0.09), (5, 0.08), (8, 0.07), (13, 0.06), (21, 0.05), (34, 0.04)):
            row += amp * np.sin(k * ang + rng.uniform(0, 2 * np.pi))
        tex[ring] = row
    # 隣のリングと少し混ぜて、半径方向にもつながった模様に
    for _ in range(2):
        tex[1:-1] = 0.25 * tex[:-2] + 0.5 * tex[1:-1] + 0.25 * tex[2:]
    return (np.clip(tex, 0, 1) * 255).astype(np.uint8)


DISK_KEYS = [(0, (0, 0, 0)), (40, (90, 10, 0)), (110, (220, 70, 5)), (175, (255, 165, 40)),
             (225, (255, 225, 140)), (255, (255, 255, 245))]
# 投げ込んだ塊の色。円盤より熱いプラズマとして青白く光らせ、円盤の上でも見分けられるようにする
HOT_KEYS = [(0, (0, 0, 0)), (90, (30, 60, 220)), (170, (110, 180, 255)), (225, (200, 235, 255)), (255, (255, 255, 255))]


def palette(keys=DISK_KEYS):
    # 明るさ 0..255 → 色。円盤は 暗い赤 → オレンジ → 黄 → 白 (黒体の色に近い順)
    out = []
    for v in range(256):
        for (v0, c0), (v1, c1) in zip(keys, keys[1:]):
            if v0 <= v <= v1:
                t = (v - v0) / (v1 - v0)
                out.append(tuple(round(c0[j] + (c1[j] - c0[j]) * t) for j in range(3)))
                break
    return out


def rgb565(c):
    v = ((c[0] >> 3) << 11) | ((c[1] >> 2) << 5) | (c[2] >> 3)
    return 0 if v == 0x0001 else v   # 0x0001 は顔スプライトの透過キー


def angular_speed():
    # リングごとの角速度 [方位角単位 (1/256 周) / 秒 × 256]。内縁が DISK_INNER_PERIOD 秒で 1 周
    r = R_IN + (np.arange(RINGS) + 0.5) / RINGS * (R_OUT - R_IN)
    return np.round((R_IN / r) ** 1.5 * 65536).astype(np.int64)   # 内縁 = 65536


def build():
    inside, state, hit_r, hit_phi, lz, out_dir = trace()
    idx = np.flatnonzero(inside.ravel())          # スプライト上の画素番号 (y * SIZE + x)
    d = state == 1
    ring = np.clip(((hit_r[d] - R_IN) / (R_OUT - R_IN) * RINGS).astype(int), 0, RINGS - 1)
    phi = (np.round(hit_phi[d] / (2 * np.pi) * ANG) % ANG).astype(int)
    bri = disk_brightness(hit_r[d], lz[d])
    bri = bri / np.percentile(bri, 96)
    bri = (np.clip(bri, 0, 1) ** 0.45 * 255).astype(int)   # ガンマで暗い部分を持ち上げる
    keep = bri > 2
    disk_px = idx[d][keep]
    disk = (disk_px.astype(np.uint32) << 14) | (ring[keep].astype(np.uint32) << 8) | phi[keep].astype(np.uint32)
    s = state == 3
    sb = stars(out_dir[s])
    st_keep = sb > 0
    star_px = idx[s][st_keep]
    star = (star_px.astype(np.uint32) << 8) | sb[st_keep].astype(np.uint32)
    return disk, bri[keep].astype(np.uint8), star, texture(), palette()


def write_header(disk, bri, star, tex, pal):
    L = ["// ブラックホール表示の対応表。tools/make_blackhole.py で生成。手で編集しない",
         "#pragma once",
         "#include <stdint.h>",
         "",
         f"constexpr int BH_SIZE = {SIZE};          // 顔スプライトの一辺",
         f"constexpr int BH_RINGS = {RINGS};",
         f"constexpr int BH_DISK_COUNT = {len(disk)};",
         f"constexpr int BH_STAR_COUNT = {len(star)};",
         "",
         "// 円盤の画素: 画素番号 << 14 | リング << 8 | 方位角",
         f"static const uint32_t BH_DISK[{len(disk)}] = {{"]
    for i in range(0, len(disk), 10):
        L.append("  " + ",".join(f"0x{v:08x}" for v in disk[i:i + 10]) + ",")
    L += ["};", "// 円盤の画素の明るさ (放射 x ドップラー)", f"static const uint8_t BH_DISK_BRIGHT[{len(bri)}] = {{"]
    for i in range(0, len(bri), 24):
        L.append("  " + ",".join(str(v) for v in bri[i:i + 24]) + ",")
    L += ["};", "// 星: 画素番号 << 8 | 明るさ", f"static const uint32_t BH_STARS[{len(star)}] = {{"]
    for i in range(0, len(star), 10):
        L.append("  " + ",".join(f"0x{v:08x}" for v in star[i:i + 10]) + ",")
    L += ["};", "// 円盤の模様 [リング][方位角]", f"static const uint8_t BH_TEXTURE[{RINGS}][256] = {{"]
    for row in tex:
        L.append("  {" + ",".join(str(v) for v in row) + "},")
    L += ["};", "// リングごとの角速度 (内縁 = 65536。ケプラー回転で外ほど遅い)", f"static const uint32_t BH_RING_SPEED[{RINGS}] = {{"]
    L.append("  " + ",".join(str(v) for v in angular_speed()) + ",")
    L += ["};", "// 明るさ → 色 (RGB565)", "static const uint16_t BH_PALETTE[256] = {"]
    p = [rgb565(c) for c in pal]
    for i in range(0, 256, 16):
        L.append("  " + ",".join(f"0x{v:04x}" for v in p[i:i + 16]) + ",")
    L += ["};", "// 投げ込んだ塊の明るさ → 色 (RGB565)", "static const uint16_t BH_HOT_PALETTE[256] = {"]
    p = [rgb565(c) for c in palette(HOT_KEYS)]
    for i in range(0, 256, 16):
        L.append("  " + ",".join(f"0x{v:04x}" for v in p[i:i + 16]) + ",")
    L += ["};", ""]
    HEADER.write_text("\n".join(L))
    print(f"{HEADER.relative_to(ROOT)}: disk {len(disk)} px, stars {len(star)}, "
          f"{len(disk) * 5 + len(star) * 4 + RINGS * 260 + 512} bytes")


def frame(disk, bri, star, tex, pal, t, period):
    """src/blackhole.h の draw と同じ計算で 1 フレームを作る"""
    img = np.zeros((SIZE * SIZE, 3), np.uint8)
    img[(star >> 8).astype(np.int64)] = (star & 255)[:, None].astype(np.uint8)
    step = (angular_speed() * int(t * 65536 / period)) >> 24        # 方位角のずれ (0..255)
    px = (disk >> 14).astype(np.int64)
    ring = ((disk >> 8) & 63).astype(np.int64)
    ph = (disk & 255).astype(np.int64)
    a = (ph - step[ring]) & 255
    v = np.minimum((tex[ring, a].astype(np.int64) * bri) >> 7, 255)   # 模様の平均 (約 0.5) で 1 倍
    pal = np.array(pal, np.uint8)
    img[px] = pal[v]
    return img.reshape(SIZE, SIZE, 3)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--preview", help="アニメーション GIF / PNG を書き出す")
    ap.add_argument("--seconds", type=float, default=6.0)
    ap.add_argument("--period", type=float, default=4.0, help="円盤の内縁が 1 周する秒数")
    a = ap.parse_args()
    disk, bri, star, tex, pal = build()
    write_header(disk, bri, star, tex, pal)
    if a.preview:
        from PIL import Image, ImageDraw
        fps = 20
        frames = []
        for k in range(int(a.seconds * fps) if a.preview.endswith(".gif") else 1):
            im = Image.fromarray(frame(disk, bri, star, tex, pal, k / fps, a.period))
            out = Image.new("RGB", (466, 466), (40, 40, 40))
            mask = Image.new("L", (466, 466), 0)
            ImageDraw.Draw(mask).ellipse([0, 0, 465, 465], fill=255)
            full = Image.new("RGB", (466, 466), "black")
            full.paste(im, (43, 43))
            out.paste(full, (0, 0), mask)
            frames.append(out)
        frames[0].save(a.preview, save_all=len(frames) > 1, append_images=frames[1:], duration=1000 // fps, loop=0)
        print(a.preview)


if __name__ == "__main__":
    main()
