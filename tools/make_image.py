# 画像をバッジの円形画面 (466x466) 用に整えて JPEG にする
#   python tools/make_image.py 入力.jpg 出力.jpg [--top 0.30] [--zoom 1.0] [--circle]
#     --top   縦のどこを中心にするか (0=上端, 1=下端)。肖像画は顔が上寄りなので既定 0.35
#     --zoom  1.0 で短辺いっぱい。大きくすると寄る
#     --circle 円の外を黒で塗る (画面が円形なので四隅は元から見えないが、送る容量が減る)
import argparse, os
from PIL import Image, ImageDraw, ImageEnhance

SIZE = 466

ap = argparse.ArgumentParser()
ap.add_argument('src')
ap.add_argument('dst')
ap.add_argument('--top', type=float, default=0.35)
ap.add_argument('--zoom', type=float, default=1.0)
ap.add_argument('--circle', action='store_true')
ap.add_argument('--quality', type=int, default=88)
ap.add_argument('--contrast', type=float, default=1.0)
ap.add_argument('--brightness', type=float, default=1.0)
a = ap.parse_args()

im = Image.open(a.src).convert('RGB')
w, h = im.size

# 短辺を基準に正方形へ切り出す。縦位置は --top で選ぶ
side = int(min(w, h) / a.zoom)
side = max(16, min(side, min(w, h)))
left = (w - side) // 2
top = int((h - side) * a.top)
top = max(0, min(top, h - side))
im = im.crop((left, top, left + side, top + side)).resize((SIZE, SIZE), Image.LANCZOS)

if a.contrast != 1.0:
    im = ImageEnhance.Contrast(im).enhance(a.contrast)
if a.brightness != 1.0:
    im = ImageEnhance.Brightness(im).enhance(a.brightness)

if a.circle:
    mask = Image.new('L', (SIZE, SIZE), 0)
    ImageDraw.Draw(mask).ellipse((0, 0, SIZE - 1, SIZE - 1), fill=255)
    out = Image.new('RGB', (SIZE, SIZE), (0, 0, 0))
    out.paste(im, (0, 0), mask)
    im = out

im.save(a.dst, 'JPEG', quality=a.quality, optimize=True, progressive=False)
print(f'{a.dst}: {SIZE}x{SIZE}, {os.path.getsize(a.dst)} bytes')
