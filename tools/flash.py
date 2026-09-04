# 書き込み専用スクリプト (pio run -t upload の代わり)
#   python tools/flash.py            # 本番 (env m5stopwatch) の firmware.bin を書く。ビルドは先に pio run -e m5stopwatch
#   python tools/flash.py diag       # 診断ファーム
#   python tools/flash.py --build    # ビルドも行う
#   python tools/flash.py --full     # bootloader / partitions / boot_app0 も書く (初回や壊れたとき)
#
# なぜ pio run -t upload を使わないか:
#   PlatformIO の upload は SCons 経由で esptool を起動し、ポートの検出やリセット待ちで
#   まれに永久に止まる (この環境で 2 回発生。8 時間止まったこともある)。
#   このスクリプトは esptool を直接呼び、タイムアウト付きで実行し、止まったら
#   ポートを掴んでいる残りプロセスを片付けてリセットしてから再試行する。
import argparse, glob, os, subprocess, sys, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ESPTOOL = os.path.expanduser('~/.platformio/packages/tool-esptoolpy/esptool.py')
PIO = os.path.expanduser('~/AppData/Roaming/Python/Python313/Scripts/pio.exe')
if not os.path.exists(PIO): PIO = 'pio'

ap = argparse.ArgumentParser()
ap.add_argument('env', nargs='?', default='m5stopwatch')
ap.add_argument('--port', default=None)
ap.add_argument('--build', action='store_true')
ap.add_argument('--full', action='store_true')
ap.add_argument('--timeout', type=int, default=90, help='esptool 1 回あたりの上限秒')
a = ap.parse_args()

def find_port():
    if a.port: return a.port
    import serial.tools.list_ports
    for p in serial.tools.list_ports.comports():
        if (p.vid == 0x303A) or 'JTAG' in (p.description or '') or 'USB' in (p.description or ''):
            return p.device
    ports = [p.device for p in serial.tools.list_ports.comports()]
    return ports[0] if ports else None

def kill_stale():
    # 以前の esptool / pio が残っていればポートを奪い合うので止める
    if os.name != 'nt': return
    ps = ("Get-CimInstance Win32_Process | Where-Object { $_.Name -match 'python|pio' } | "
          "ForEach-Object { \"$($_.ProcessId)|$($_.CommandLine)\" }")
    out = subprocess.run(['powershell', '-NoProfile', '-Command', ps], capture_output=True, text=True, errors='replace').stdout
    me = str(os.getpid())
    for line in out.splitlines():
        pid, _, cmd = line.partition('|')
        pid = pid.strip()
        if not pid.isdigit() or pid == me or 'flash.py' in cmd: continue
        if 'esptool' in cmd or 'pio.exe' in cmd or 'scons' in cmd:
            subprocess.run(['taskkill', '/F', '/PID', pid], capture_output=True)
            print(f'killed stale process {pid}: {cmd[:80]}')

def reset_via_rts(port):
    try:
        import serial
        s = serial.Serial(port, 115200, timeout=0.2)
        s.setDTR(False); s.setRTS(True); time.sleep(0.1); s.setRTS(False); s.close()
        time.sleep(1.0)
    except Exception as e:
        print(f'reset failed: {e}')

def port_free(port):
    try:
        import serial
        s = serial.Serial(port, 115200, timeout=0.2); s.close(); return True
    except Exception as e:
        print(f'port {port} busy: {e}'); return False

if a.build:
    print(f'== build {a.env}')
    r = subprocess.run([PIO, 'run', '-e', a.env], cwd=ROOT)
    if r.returncode != 0: sys.exit(r.returncode)

build = os.path.join(ROOT, '.pio', 'build', a.env)
fw = os.path.join(build, 'firmware.bin')
if not os.path.exists(fw): sys.exit(f'{fw} not found (run: pio run -e {a.env})')
parts = ['0x10000', fw]
if a.full:
    boot_app0 = glob.glob(os.path.expanduser('~/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin'))
    parts = ['0x0', os.path.join(build, 'bootloader.bin'), '0x8000', os.path.join(build, 'partitions.bin')] + \
            (['0xe000', boot_app0[0]] if boot_app0 else []) + parts

port = find_port()
if not port: sys.exit('no serial port found')
print(f'== port {port}, firmware {os.path.getsize(fw)} bytes ({time.ctime(os.path.getmtime(fw))})')

for attempt in range(1, 4):
    kill_stale()
    if not port_free(port):
        time.sleep(2)
    cmd = [sys.executable, ESPTOOL, '--chip', 'esp32s3', '--port', port, '--baud', '921600',
           '--before', 'default-reset', '--after', 'hard-reset', 'write_flash', '-z'] + parts
    print(f'== esptool attempt {attempt}')
    # esptool の進捗バーは Unicode。日本語 Windows の cp932 では出力時に落ちるので UTF-8 を強制する
    env = dict(os.environ, PYTHONIOENCODING='utf-8', PYTHONUTF8='1')
    try:
        r = subprocess.run(cmd, timeout=a.timeout, capture_output=True, text=True, encoding='utf-8', errors='replace', env=env)
        tail = '\n'.join(l for l in (r.stdout + r.stderr).splitlines() if any(k in l for k in ('Wrote', 'Hash', 'error', 'Error', 'fatal', 'Connecting')))
        print(tail)
        if r.returncode == 0 and 'Hash of data verified' in r.stdout:
            print('== OK'); sys.exit(0)
    except subprocess.TimeoutExpired:
        print(f'esptool hung for {a.timeout}s, resetting the device and retrying')
    kill_stale()
    reset_via_rts(port)
sys.exit('flash failed after 3 attempts (try: hold a button while plugging USB to enter download mode)')
