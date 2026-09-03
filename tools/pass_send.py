# PC からバッジにチケット (QR) を送る。バッジをチケットモードにしてタップし「受信待ち」にしてから実行する
#   python tools/pass_send.py ticket.pkpass                  # Wallet のパス
#   python tools/pass_send.py screenshot.png                 # スクリーンショット (バッジが QR を読む)
#   python tools/pass_send.py --text "https://example/abc" --title "映画"   # 文字列をそのまま QR に
#   --host で送り先を変更 (既定 makkuro.local)。--fake-pkpass で動作確認用の .pkpass を作って送る
import argparse, io, json, mimetypes, os, sys, urllib.parse, urllib.request, zipfile

ap = argparse.ArgumentParser()
ap.add_argument('file', nargs='?')
ap.add_argument('--text')
ap.add_argument('--title', default='')
ap.add_argument('--host', default='makkuro.local')
ap.add_argument('--fake-pkpass', action='store_true', help='動作確認用の .pkpass を生成して送る')
a = ap.parse_args()

url = f'http://{a.host}/pass'
if a.title:
    url += '?' + urllib.parse.urlencode({'title': a.title})

if a.fake_pkpass:
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, 'w', zipfile.ZIP_DEFLATED) as z:
        z.writestr('pass.json', json.dumps({
            'formatVersion': 1, 'organizationName': 'MOVIX さいたま', 'description': 'テスト用チケット',
            'barcode': {'format': 'PKBarcodeFormatQR', 'message': 'https://example.com/ticket/ABC123456', 'messageEncoding': 'iso-8859-1'},
            'eventTicket': {'primaryFields': [{'key': 'title', 'label': '作品名', 'value': '【DolbyAtmos字幕】シラート'}]},
        }, ensure_ascii=False))
        z.writestr('manifest.json', '{}')
    data, ctype = buf.getvalue(), 'application/vnd.apple.pkpass'
elif a.text:
    data, ctype = a.text.encode('utf-8'), 'text/plain; charset=utf-8'
elif a.file:
    data = open(a.file, 'rb').read()
    ctype = mimetypes.guess_type(a.file)[0] or 'application/octet-stream'
    if a.file.lower().endswith('.pkpass'): ctype = 'application/vnd.apple.pkpass'
else:
    ap.print_help(); sys.exit(1)

req = urllib.request.Request(url, data=data, method='POST', headers={'Content-Type': ctype})
print(f'POST {url}  ({len(data)} bytes, {ctype})')
try:
    with urllib.request.urlopen(req, timeout=60) as r:
        print(r.status, r.read().decode('utf-8', errors='replace'))
except urllib.error.HTTPError as e:
    print(e.code, e.read().decode('utf-8', errors='replace'))
