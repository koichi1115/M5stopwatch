# 画像をバッジのギャラリーへ送る。バッジをギャラリーモードにしてタップし「受信待ち」にしてから実行する
#   python tools/send_image.py monalisa.jpg --title mona
#   python tools/send_image.py photo.jpg --prepare          # 466x466 に整えてから送る
#   --host で送り先を変更 (既定 makkuro.local)
import argparse, os, subprocess, sys, tempfile, urllib.parse, urllib.request

ap = argparse.ArgumentParser()
ap.add_argument('file')
ap.add_argument('--title', default='')
ap.add_argument('--host', default='makkuro.local')
ap.add_argument('--prepare', action='store_true', help='tools/make_image.py で 466x466 に整えてから送る')
ap.add_argument('--top', type=float, default=0.35)
ap.add_argument('--zoom', type=float, default=1.0)
a = ap.parse_args()

path = a.file
if a.prepare:
    out = os.path.join(tempfile.gettempdir(), '_badge_' + os.path.basename(a.file))
    if not out.lower().endswith(('.jpg', '.jpeg')):
        out += '.jpg'
    here = os.path.dirname(os.path.abspath(__file__))
    subprocess.run([sys.executable, os.path.join(here, 'make_image.py'), a.file, out,
                    '--top', str(a.top), '--zoom', str(a.zoom), '--circle'], check=True)
    path = out

data = open(path, 'rb').read()
ctype = 'image/png' if data[:2] == b'\x89P' else 'image/jpeg'
url = f'http://{a.host}/image'
if a.title:
    url += '?' + urllib.parse.urlencode({'title': a.title})

print(f'POST {url}  ({len(data)} bytes, {ctype})')
req = urllib.request.Request(url, data=data, method='POST', headers={'Content-Type': ctype})
try:
    with urllib.request.urlopen(req, timeout=90) as r:
        print(r.status, r.read().decode('utf-8', errors='replace'))
except urllib.error.HTTPError as e:
    print(e.code, e.read().decode('utf-8', errors='replace'))
