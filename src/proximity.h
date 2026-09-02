// 近接 (指が近づいた) 検知
//
// StopWatch 本体には近接センサーが無く、タッチパネル (CST820) も「触れた」しか返さない。
// そこで ESP32-S3 内蔵のタッチセンサー周辺回路を使い、Grove ポートの空きピン (GPIO10 = SDA 側) に
// 電極 (銅箔テープや針金) を繋いで静電容量の変化で「近づいた」を検知する。
//   - 電極が大きいほど遠くから (数 cm) 反応する。ベゼル裏に銅箔を一周貼るのがおすすめ
//   - 何も繋がなければ config.h の PROXIMITY_ENABLED を false のままにしておく
#pragma once
#include <Arduino.h>
#include "config.h"

class ProximitySensor {
public:
  void begin() {
    if (!PROXIMITY_ENABLED) return;
    _enabled = true;
    // 最初の数回はウォームアップとして読み捨て、基準値を作る
    for (int i = 0; i < 8; ++i) { touchRead(PROXIMITY_TOUCH_PIN); delay(2); }
    _baseline = (float)touchRead(PROXIMITY_TOUCH_PIN);
  }

  // 戻り値: 0 = 何もない, 0..1 = 近づいている度合い (1 = 触れる直前)
  float update(uint32_t now) {
    if (!_enabled) return 0.0f;
    if (now - _lastReadMs < PROXIMITY_READ_INTERVAL_MS) return _level;
    _lastReadMs = now;

    const float raw = (float)touchRead(PROXIMITY_TOUCH_PIN);
    _raw += (raw - _raw) * 0.5f;                       // 軽いノイズ落とし
    if (_baseline <= 0.0f) _baseline = _raw;

    // 静電容量が増える (= 値が上がる) 方向を「近い」とみなす
    _ratio = _raw / _baseline;
    float target;
    if (_ratio < PROXIMITY_NEAR_RATIO) target = 0.0f;
    else if (_ratio >= PROXIMITY_CLOSE_RATIO) target = 1.0f;
    else target = (_ratio - PROXIMITY_NEAR_RATIO) / (PROXIMITY_CLOSE_RATIO - PROXIMITY_NEAR_RATIO);
    _level += (target - _level) * 0.35f;

    // 何も無いときだけ基準値をゆっくり追従 (温度ドリフト対策)。
    // 近い状態が長く続いても、いずれ基準に吸収されて「離れた」扱いになる (誤検知が固定化しない)
    const float k = (target <= 0.0f) ? PROXIMITY_BASELINE_TRACK : PROXIMITY_BASELINE_TRACK * 0.05f;
    _baseline += (_raw - _baseline) * k;
    return _level;
  }

  bool enabled() const { return _enabled; }
  float level() const { return _level; }
  float raw() const { return _raw; }
  float baseline() const { return _baseline; }
  float ratio() const { return _ratio; }
  void recalibrate() { _baseline = _raw; _level = 0.0f; }

private:
  bool _enabled = false;
  uint32_t _lastReadMs = 0;
  float _raw = 0.0f, _baseline = 0.0f, _ratio = 1.0f, _level = 0.0f;
};
