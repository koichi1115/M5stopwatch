# 音判定の較正: PC のスピーカーから刺激を順に鳴らし、端末の [snd] ログを刺激ごとに集計する
#   python .pio/snd_calib.py            (本番ファームを書き込んだ状態で)
import serial, time, wave, struct, math, winsound, os, tempfile, random, subprocess, re, statistics, sys

SR = 44100
tmp = tempfile.gettempdir()

def make_wav(path, seconds, gen):
    with wave.open(path, 'wb') as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR)
        frames = bytearray()
        for i in range(int(SR * seconds)):
            v = max(-1.0, min(1.0, gen(i / SR)))
            frames += struct.pack('<h', int(v * 32767))
        w.writeframes(bytes(frames))

def chord_gen(t):
    env = 0.75 + 0.2 * math.sin(2 * math.pi * 0.9 * t)
    f = [220, 277.2, 329.6, 440]
    mel = 523.25 * (2 ** ((int(t * 2) % 4) / 12))
    return env * (0.18 * sum(math.sin(2 * math.pi * fi * t) for fi in f) + 0.25 * math.sin(2 * math.pi * mel * t))

def band_gen(t):
    beat = t % 0.5
    kick = math.sin(2 * math.pi * (60 + 80 * math.exp(-beat * 30)) * beat) * math.exp(-beat * 12) if beat < 0.25 else 0
    hat = (random.random() * 2 - 1) * math.exp(-((t % 0.25)) * 60) * 0.25
    bass = 0.25 * math.sin(2 * math.pi * 55 * (2 ** ((int(t) % 4) * 2 / 12)) * t)
    mel = 0.2 * math.sin(2 * math.pi * 440 * (2 ** (([0, 2, 4, 7, 4, 2][int(t * 4) % 6]) / 12)) * t)
    return 0.6 * kick + hat + bass + mel

def noise_gen(t): return (random.random() * 2 - 1) * 0.5
def burst_gen(t): return (random.random() * 2 - 1) * (1.0 if t < 0.2 else 0)
def tone(f): return lambda t: 0.6 * math.sin(2 * math.pi * f * t)

random.seed(1)
files = {}
for name, sec, gen in [('chord', 15, chord_gen), ('band', 15, band_gen), ('noise', 15, noise_gen),
                       ('burst', 0.25, burst_gen), ('tone440', 6, tone(440)), ('tone1k', 6, tone(1000))]:
    files[name] = os.path.join(tmp, '_' + name + '.wav'); make_wav(files[name], sec, gen)
speech = os.path.join(tmp, '_speech.wav')
text = ("Hello there. This is a test of the speech detector on the badge. "
        "People usually talk with short pauses between phrases, like this. "
        "The weather today is fine, and I am reading a few sentences out loud. "
        "Let us see whether the badge thinks this is music or not.")
subprocess.run(['powershell', '-NoProfile', '-Command',
                "Add-Type -AssemblyName System.Speech; $s = New-Object System.Speech.Synthesis.SpeechSynthesizer; "
                "$s.Rate = 0; $s.SetOutputToWaveFile('" + speech + "'); $s.Speak('" + text + "'); $s.Dispose()"],
               check=True, capture_output=True)
files['speech'] = speech

s = serial.Serial('COM6', 115200, timeout=0.2)
pat = re.compile(r'\[snd\] level\s+(-?\d+) floor\s+(-?\d+) sfm ([\d.]+) act ([\d.]+) std ([\d.]+) dc (-?\d+) peak (\d+) pk (\d+)Hz\((\d+)%\)[^\n]*?( MUSIC)?$', re.M)
verbose = '-v' in sys.argv

def run(name, path, sec):
    buf = b''
    if path: winsound.PlaySound(path, winsound.SND_FILENAME | winsound.SND_ASYNC)
    t0 = time.time()
    while time.time() - t0 < sec:
        buf += s.read(4096)
    if path: winsound.PlaySound(None, 0)
    txt = buf.decode(errors='replace')
    if verbose: print('\n'.join(l for l in txt.splitlines() if '[snd]' in l))
    rows = [m for m in pat.finditer(txt)]
    loud = txt.count('loud!')
    music = ('music on' in txt) or any(m[10] for m in rows)
    if rows:
        f = lambda i: [float(m[i]) for m in rows]
        lv, fl, sf, ac, sd, pk, phz, psh = f(1), f(2), f(3), f(4), f(5), f(7), f(8), f(9)
        print(f"{name:8s} n={len(rows):2d} lvl {statistics.mean(lv):5.1f} floor {statistics.mean(fl):5.1f} "
              f"sfm {statistics.mean(sf):.2f}[{min(sf):.2f}-{max(sf):.2f}] act {statistics.mean(ac):.2f} "
              f"std {statistics.mean(sd):4.1f}[{min(sd):.1f}-{max(sd):.1f}] peak {max(pk):5.0f} "
              f"top {statistics.median(phz):5.0f}Hz({statistics.mean(psh):2.0f}%)  MUSIC={'YES' if music else 'no '} loud={loud}")
    else:
        print(f"{name:8s} n= 0 (quiet)  loud={loud}")

print('collecting ...')
run('quiet', None, 5)
run('burst', files['burst'], 3)
run('quiet', None, 3)
run('tone440', files['tone440'], 6)
run('quiet', None, 3)
run('tone1k', files['tone1k'], 6)
run('quiet', None, 4)
run('speech', files['speech'], 16)
run('quiet', None, 6)
run('noise', files['noise'], 16)
run('quiet', None, 6)
run('chord', files['chord'], 16)
run('quiet', None, 6)
run('band', files['band'], 16)
run('quiet', None, 4)
s.close()
