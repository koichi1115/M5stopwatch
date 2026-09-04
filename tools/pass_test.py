# 開発用: バッジをリセット → (PASS_DEBUG_RECEIVE_ON_BOOT=true のファームなら) 自動で受信待ち → tools/pass_send.py で送信 → バッジのログを表示
#   PYTHONUTF8=1 python tools/pass_test.py --fake-pkpass
#   PYTHONUTF8=1 python tools/pass_test.py screenshot.png
import serial, subprocess, sys, threading, time
args = sys.argv[1:]
s = serial.Serial('COM6', 115200, timeout=0.2)
buf = []
stop = False
def reader():
    while not stop:
        d = s.read(4096)
        if d: buf.append(d)
threading.Thread(target=reader, daemon=True).start()
def logtext(): return b''.join(buf).decode(errors='replace')

time.sleep(0.3)
# リセットして起動から取る (PASS_DEBUG_RECEIVE_ON_BOOT で起動直後に受信待ちになる)
s.dtr = True; s.rts = True; time.sleep(0.2)
s.dtr = False; s.rts = True; time.sleep(0.2)
s.rts = False; s.dtr = True
t0 = time.time(); ok = False
while time.time() - t0 < 150:
    txt = logtext()
    if '[pass] connected' in txt or 'starting AP' in txt: ok = True; break
    time.sleep(0.5)
print('--- receive ready:', ok, f'({time.time()-t0:.1f}s)')
if ok:
    time.sleep(1.5)
    r = subprocess.run([sys.executable, 'tools/pass_send.py'] + args, capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=120)
    print('--- pass_send ---'); print(r.stdout.strip()); print(r.stderr.strip()[-600:])
    time.sleep(4)
stop = True; time.sleep(0.3); s.close()
print('--- badge log (full) ---')
print(logtext()[-3000:] or '(nothing)')
