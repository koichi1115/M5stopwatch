// 姿勢トラッカ: 加速度から「画面の下＝重力方向」になる顔の回転角を求める
#pragma once
#include <math.h>
#include <stdint.h>
#include "config.h"

class OrientationTracker {
public:
  static constexpr float kRadToDeg = 57.29577951f;
  static constexpr float kDegToRad = 0.01745329252f;

  void setOffset(float deg) { _offset = wrap(deg); }
  float offset() const { return _offset; }
  void setSign(int8_t s) { _sign = (s < 0) ? -1 : 1; }
  int8_t sign() const { return _sign; }

  // 回転方向を反転する。表示中の向きが変わらないよう offset も追従させる。
  void flipSign() {
    _sign = -_sign;
    _offset = wrap(-_offset);
  }

  // 現在の向きを「正立」として記憶する
  bool calibrate() {
    if (!_reliable) return false;
    _offset = wrap(_offset + _angle);   // 表示角が 0 になるように補正
    _angle = 0.0f;
    _fx = 1.0f; _fy = 0.0f;
    _shown = 0.0f;
    return true;
  }

  // ax,ay,az [g], gyro_mag [deg/s], dt [s]
  // 戻り値: 面内重力が十分にあり角度が信頼できるか
  bool update(float ax, float ay, float az, float dt) {
    const float sx = ACC_TO_SCREEN_X(ax, ay, az);
    const float sy = ACC_TO_SCREEN_Y(ax, ay, az);
    _inPlane = sqrtf(sx * sx + sy * sy);
    _magnitude = sqrtf(ax * ax + ay * ay + az * az);
    _rawX = sx; _rawY = sy;

    if (_inPlane < ORIENT_MIN_INPLANE_G) {
      _reliable = false;
      return false;
    }
    // 顔のローカル +Y (下) を回転させて重力方向 (sx,sy) に一致させる角度
    //   R(r)·(0,1) = (-sin r, cos r) = (sx,sy)/|g|  →  r = atan2(-sx, sy)
    float raw = atan2f(-sx, sy) * kRadToDeg;
    raw = wrap(_sign * raw - _offset);

    // 動き中 (|a| が 1g から離れている) は信頼度を下げて追従を鈍らせる
    float trust = 1.0f - fabsf(_magnitude - 1.0f) / 0.6f;
    if (trust < 0.08f) trust = 0.08f;
    if (trust > 1.0f) trust = 1.0f;
    float alpha = 1.0f - expf(-dt / ORIENT_FILTER_TAU_SEC);
    alpha *= trust;

    // ラップアラウンド対策: 単位ベクトルでローパス
    const float tx = cosf(raw * kDegToRad), ty = sinf(raw * kDegToRad);
    if (!_initialized) { _fx = tx; _fy = ty; _initialized = true; }
    _fx += (tx - _fx) * alpha;
    _fy += (ty - _fy) * alpha;
    const float n = sqrtf(_fx * _fx + _fy * _fy);
    if (n > 1e-4f) { _fx /= n; _fy /= n; }
    _angle = atan2f(_fy, _fx) * kRadToDeg;

    // デッドバンド (静止時の微振動を殺す)
    if (fabsf(wrap(_angle - _shown)) > ORIENT_DEADBAND_DEG || !_shownInit) {
      _shown = _angle;
      _shownInit = true;
    }
    _reliable = true;
    return true;
  }

  // 顔を描くときの回転角 [deg]
  float displayAngle() const {
    if (!ORIENT_SNAP_90) return _shown;
    return snapped();
  }
  float rawAngle() const { return _angle; }
  bool reliable() const { return _reliable; }
  float inPlaneG() const { return _inPlane; }
  float magnitudeG() const { return _magnitude; }
  float screenGx() const { return _rawX; }
  float screenGy() const { return _rawY; }

  static float wrap(float deg) {
    while (deg >= 180.0f) deg -= 360.0f;
    while (deg < -180.0f) deg += 360.0f;
    return deg;
  }

private:
  float snapped() const {
    // ヒステリシス付き 90 度スナップ
    float candidate = roundf(_shown / 90.0f) * 90.0f;
    float diffCur = fabsf(wrap(_shown - _snapAngle));
    if (diffCur > 45.0f + ORIENT_SNAP_HYSTERESIS_DEG || !_snapInit) {
      _snapAngle = wrap(candidate);
      _snapInit = true;
    }
    return _snapAngle;
  }

  float _offset = 0.0f;
  int8_t _sign = 1;
  float _fx = 1.0f, _fy = 0.0f;
  float _angle = 0.0f;
  float _shown = 0.0f;
  float _inPlane = 0.0f, _magnitude = 1.0f;
  float _rawX = 0.0f, _rawY = 1.0f;
  bool _initialized = false, _shownInit = false, _reliable = false;
  mutable float _snapAngle = 0.0f;
  mutable bool _snapInit = false;
};

// 動き検出 (寝る/起きる/驚く の判定に使う)
class MotionDetector {
public:
  // accel [g], gyro [deg/s], now [ms]
  void update(float ax, float ay, float az, float gx, float gy, float gz, uint32_t now) {
    const float amag = sqrtf(ax * ax + ay * ay + az * az);
    const float gmag = sqrtf(gx * gx + gy * gy + gz * gz);
    const float dev = fabsf(amag - 1.0f);
    _activity += ((dev * 4.0f + gmag / 90.0f) - _activity) * 0.15f;
    if (dev > MOTION_ACCEL_THRESHOLD_G || gmag > MOTION_GYRO_THRESHOLD_DPS) {
      _lastMotionMs = now;
    }
    if (dev > SHAKE_THRESHOLD_G) {
      if (now - _shakeWindowStart > SHAKE_WINDOW_MS) { _shakeWindowStart = now; _shakeCount = 0; }
      if (now - _lastShakeHit > 60) { _shakeCount++; _lastShakeHit = now; }
      if (_shakeCount >= SHAKE_COUNT) { _shakeCount = 0; _shakePending = true; }
    }
  }
  void touch(uint32_t now) { _lastMotionMs = now; }
  uint32_t lastMotionMs() const { return _lastMotionMs; }
  uint32_t idleMs(uint32_t now) const { return now - _lastMotionMs; }
  float activity() const { return _activity; }
  bool takeShake() { bool s = _shakePending; _shakePending = false; return s; }

private:
  uint32_t _lastMotionMs = 0;
  uint32_t _shakeWindowStart = 0, _lastShakeHit = 0;
  uint8_t _shakeCount = 0;
  bool _shakePending = false;
  float _activity = 0.0f;
};
