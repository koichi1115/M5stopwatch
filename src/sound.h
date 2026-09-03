// 音センサ: マイクの音量と簡易スペクトルから「急な大きい音」「音楽らしさ」を判定する
//   - M5.Mic (ES8311, モノラル) から 512 サンプル (32ms @16kHz) 単位で取り込む
//   - 音量 [dBFS] とその背景フロア、スペクトル平坦度 (雑音=1 / トーン=0) を追跡
//   - 急な大きい音: フロアより十分大きく、直前より急に上がったとき (不応期あり)
//   - 音楽: 音がほぼ途切れず続き (activeRatio)、かつトーン成分が多い (平坦度が低い) 状態が数秒続いたとき
//   マイクは 1 つなので方向は分からない。
#pragma once
#include <M5Unified.h>
#include <math.h>
#include <stdint.h>
#include <utility>
#include "config.h"

class SoundSensor {
public:
  static constexpr int N = SOUND_BLOCK_SAMPLES;

  bool begin() {
    if (!M5.Mic.isEnabled()) return false;
    auto cfg = M5.Mic.config();
    cfg.sample_rate = SOUND_SAMPLE_RATE;
    cfg.magnification = SOUND_MIC_GAIN;
    M5.Mic.config(cfg);
    M5.Speaker.end();   // マイクと I2S ピンを共有するため
    for (int i = 0; i < N; ++i) _window[i] = 0.5f - 0.5f * cosf(2.0f * 3.14159265f * i / (N - 1));
    _running = M5.Mic.begin();
    if (_running && M5.getBoard() == m5::board_t::board_M5StopWatch) {
      // M5Unified は ES8311 の PGA ゲインを最小 (0dB) にする。環境音を拾うので上げる (3dB 刻み, 0..10)
      const uint8_t g = SOUND_ES8311_PGA_GAIN > 10 ? 10 : SOUND_ES8311_PGA_GAIN;
      M5.In_I2C.writeRegister8(0x18, 0x14, (uint8_t)(0x10 | g), 100000);
    }
    return _running;
  }
  void end() {
    if (_running) { M5.Mic.end(); _running = false; }
  }
  bool running() const { return _running; }

  // loop 毎に呼ぶ。完了したブロックを処理し、録音キューを補充する (ブロックしない)
  void update(uint32_t now) {
    if (!_running) return;
    size_t pending = M5.Mic.isRecording();
    while (_queued - _processed > (uint32_t)pending) {
      process(_buf[_processed % kBufs], now);
      _processed++;
    }
    while (pending < 2 && _queued - _processed < kBufs - 1) {
      if (!M5.Mic.record(_buf[_queued % kBufs], N, SOUND_SAMPLE_RATE)) break;
      _queued++;
      pending++;
    }
  }

  bool takeLoud() { bool l = _loudPending; _loudPending = false; return l; }
  bool isMusic() const { return _music; }
  float levelDb() const { return _level; }
  float floorDb() const { return _floor; }
  float flatness() const { return _tonal; }
  float activeRatio() const { return _activeRatio; }
  bool active() const { return _active; }
  float dc() const { return _dc; }       // 直近ブロックの DC オフセット (生値)
  int peak() const { return _peak; }     // 直近ブロックのピーク (DC 除去後, 生値)

private:
  static constexpr int kBufs = 4;

  void process(const int16_t* buf, uint32_t now) {
    // ---- DC 除去と音量 [dBFS] ----
    float mean = 0.0f;
    int peak = 0;
    for (int i = 0; i < N; ++i) mean += buf[i];
    mean /= N;
    float sum = 0.0f;
    for (int i = 0; i < N; ++i) {
      const float v = buf[i] - mean;
      sum += v * v;
      const int a = v < 0 ? (int)-v : (int)v;
      if (a > peak) peak = a;
    }
    _dc = mean;
    _peak = peak;
    const float rms = sqrtf(sum / N);
    const float level = 20.0f * log10f((rms < 1.0f ? 1.0f : rms) / 32768.0f);
    if (!_init) {
      if (rms < 2.0f) return;   // 起動直後の無出力ブロックは無視 (これでフロアを初期化すると二度と戻らない)
      _init = true;
      _floor = level; _recent = level;
      for (int i = 0; i < kLevelHist; ++i) _levelHist[i] = level;
    }

    // ---- 背景フロア (下がるときは速く追従、上がるのはゆっくり。音がしている間はさらにゆっくり) ----
    {
      const float rise = (level < _floor + SOUND_ACTIVE_ABOVE_FLOOR_DB) ? SOUND_FLOOR_RISE_RATE : SOUND_FLOOR_RISE_RATE_ACTIVE;
      _floor += (level - _floor) * (level < _floor ? 0.2f : rise);
    }

    // ---- 音量の時間変動 (直近 1 秒の標準偏差 [dB])。しゃべり声 > 音楽 > 定常ノイズ ----
    {
      _levelHist[_levelPos] = level;
      _levelPos = (_levelPos + 1) % kLevelHist;
      float m = 0.0f;
      for (int i = 0; i < kLevelHist; ++i) m += _levelHist[i];
      m /= kLevelHist;
      float v = 0.0f;
      for (int i = 0; i < kLevelHist; ++i) { const float d = _levelHist[i] - m; v += d * d; }
      _levelStd = sqrtf(v / kLevelHist);
    }

    // ---- 急な大きい音 ----
    if (!_music && (now - _lastLoud) > SOUND_LOUD_REFRACTORY_MS &&
        level > SOUND_LOUD_MIN_DB && (level - _floor) > SOUND_LOUD_ABOVE_FLOOR_DB && (level - _recent) > SOUND_LOUD_RISE_DB) {
      _loudPending = true;
      _lastLoud = now;
    }
    _recent += (level - _recent) * 0.35f;
    _level = level;

    // ---- 継続性とトーン性 ----
    _active = (level - _floor) > SOUND_ACTIVE_ABOVE_FLOOR_DB && level > SOUND_ACTIVE_MIN_DB;
    // 直近 kHist ブロックの「音がしている」割合 (移動窓)
    _activeCount += (_active ? 1 : 0) - _hist[_histPos];
    _hist[_histPos] = _active ? 1 : 0;
    _histPos = (_histPos + 1) % kHist;
    _activeRatio = (float)_activeCount / kHist;
    if (_active) {
      const float sfm = spectralFlatness(buf);
      _tonal += (sfm - _tonal) * 0.06f;
    }

    // ---- 音楽判定 (ヒステリシス付き) ----
    const bool musicLike = _activeRatio > SOUND_MUSIC_ACTIVE_RATIO && _tonal < SOUND_MUSIC_FLATNESS_MAX &&
                           _levelStd >= SOUND_MUSIC_LEVEL_STD_MIN && _levelStd <= SOUND_MUSIC_LEVEL_STD_MAX;
    if (musicLike) {
      if (_musicSince == 0) _musicSince = now;
      _musicLost = 0;
      if (!_music && (now - _musicSince) > SOUND_MUSIC_ENTER_MS) _music = true;
    } else {
      _musicSince = 0;
      if (_music) {
        if (_musicLost == 0) _musicLost = now;
        if ((now - _musicLost) > SOUND_MUSIC_EXIT_MS) _music = false;
      }
    }
  }

  // スペクトル平坦度 (幾何平均 / 算術平均)。1 に近いほど雑音、0 に近いほど純音・和音
  float spectralFlatness(const int16_t* buf) {
    for (int i = 0; i < N; ++i) { _re[i] = (buf[i] - _dc) * _window[i]; _im[i] = 0.0f; }
    fft(_re, _im, N);
    // 125Hz .. 6.25kHz (bin 幅 = fs/N = 31.25Hz)
    const int lo = 4, hi = 200;
    float sumLog = 0.0f, sumP = 0.0f;
    float top[3] = {0, 0, 0}; int topBin[3] = {0, 0, 0};
    for (int k = lo; k < hi; ++k) {
      const float raw = _re[k] * _re[k] + _im[k] * _im[k] + 1.0f;
      // ホワイトニング: マイク/筐体の固定の周波数特性を長期平均で割って打ち消す
      if (_avgSpec[k] <= 0.0f) _avgSpec[k] = raw;
      else _avgSpec[k] += (raw - _avgSpec[k]) * SOUND_WHITEN_RATE;
      const float p = raw / _avgSpec[k];
      sumLog += logf(p);
      sumP += p;
      if (p > top[0]) { top[2] = top[1]; topBin[2] = topBin[1]; top[1] = top[0]; topBin[1] = topBin[0]; top[0] = p; topBin[0] = k; }
      else if (p > top[1]) { top[2] = top[1]; topBin[2] = topBin[1]; top[1] = p; topBin[1] = k; }
      else if (p > top[2]) { top[2] = p; topBin[2] = k; }
    }
    for (int i = 0; i < 3; ++i) { _peakHz[i] = topBin[i] * (float)SOUND_SAMPLE_RATE / N; _peakShare[i] = top[i] / sumP; }
    const float n = (float)(hi - lo);
    return expf(sumLog / n) / (sumP / n);
  }

public:
  // 診断用: 直近のスペクトル上位 3 ピークの周波数 [Hz] と全体に占める割合
  float peakHz(int i) const { return _peakHz[i < 0 ? 0 : (i > 2 ? 2 : i)]; }
  float peakShare(int i) const { return _peakShare[i < 0 ? 0 : (i > 2 ? 2 : i)]; }
private:
  float _peakHz[3] = {0, 0, 0}, _peakShare[3] = {0, 0, 0};

  static void fft(float* re, float* im, int n) {
    for (int i = 1, j = 0; i < n; ++i) {
      int bit = n >> 1;
      for (; j & bit; bit >>= 1) j ^= bit;
      j ^= bit;
      if (i < j) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); }
    }
    for (int len = 2; len <= n; len <<= 1) {
      const float ang = -2.0f * 3.14159265f / len;
      const float wr = cosf(ang), wi = sinf(ang);
      for (int i = 0; i < n; i += len) {
        float cr = 1.0f, ci = 0.0f;
        for (int k = 0; k < len / 2; ++k) {
          const int a = i + k, b = i + k + len / 2;
          const float tr = re[b] * cr - im[b] * ci;
          const float ti = re[b] * ci + im[b] * cr;
          re[b] = re[a] - tr; im[b] = im[a] - ti;
          re[a] += tr;        im[a] += ti;
          const float ncr = cr * wr - ci * wi;
          ci = cr * wi + ci * wr;
          cr = ncr;
        }
      }
    }
  }

  int16_t _buf[kBufs][N];
  float _re[N], _im[N], _window[N];
  uint32_t _queued = 0, _processed = 0;
  bool _running = false, _init = false;
  float _level = -90.0f, _floor = -90.0f, _recent = -90.0f;
  float _dc = 0.0f;
  int _peak = 0;
  // 音量の時間変動 (直近 ~1 秒)
  static constexpr int kLevelHist = 31;
  float _levelHist[kLevelHist] = {};
  int _levelPos = 0;
  float _levelStd = 0.0f;
  // ホワイトニング用の長期平均スペクトル
  float _avgSpec[N / 2] = {};
public:
  float levelStd() const { return _levelStd; }
private:
  float _tonal = 1.0f, _activeRatio = 0.0f;
  bool _active = false, _music = false, _loudPending = false;
  uint32_t _lastLoud = 0, _musicSince = 0, _musicLost = 0;
  // 移動窓: SOUND_MUSIC_WINDOW_MS をブロック数に換算
  static constexpr int kHist = (int)((uint64_t)SOUND_MUSIC_WINDOW_MS * SOUND_SAMPLE_RATE / (1000ULL * N));
  uint8_t _hist[kHist] = {};
  int _histPos = 0, _activeCount = 0;
};
