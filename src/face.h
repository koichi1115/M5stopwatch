// まっくろくろすけ風の顔: 目の描画と「気分」の状態機械
#pragma once
#include <M5Unified.h>
#include <math.h>
#include "config.h"

enum class Mood : uint8_t { Awake, Drowsy, Sleep, Surprised, Happy };

struct FaceInput {
  float dt;              // [s]
  uint32_t now;          // [ms]
  bool moving;           // 動いている
  bool shaken;           // 振られた
  bool tapped;           // タップされた
  bool touching;         // 触り続けている
  float touchGazeX;      // 触っている位置 (顔ローカル, -1..1)
  float touchGazeY;
  uint32_t idleMs;       // 最後に動いてからの時間
  bool autoSleepEnabled;
  float proximity;       // 0 = 何もない .. 1 = 指が触れる直前 (近接電極が無ければ常に 0)
};

class Face {
public:
  bool begin() {
    _sprite.setPsram(true);
    _sprite.setColorDepth(16);
    if (!_sprite.createSprite(FACE_SIZE, FACE_SIZE)) {
      // PSRAM が使えない場合の保険: 内蔵 RAM で小さめに
      _sprite.setPsram(false);
      if (!_sprite.createSprite(FACE_SIZE / 2, FACE_SIZE / 2)) return false;
      _scale = 0.5f;
    }
    _sprite.setPivot(_sprite.width() / 2.0f, _sprite.height() / 2.0f);
    scheduleBlink(0);
    scheduleGaze(0);
    return true;
  }

  Mood mood() const { return _mood; }
  bool isSleeping() const { return _mood == Mood::Sleep; }
  bool isDrowsy() const { return _mood == Mood::Drowsy; }
  float breathScale() const { return _breath; }

  // 気分と目の状態を進める
  void update(const FaceInput& in) {
    const float dt = in.dt;
    _time += dt;

    // ---- 一時的なリアクション ----
    if (in.shaken && _mood != Mood::Surprised) {
      _mood = Mood::Surprised;
      _reactionUntil = in.now + 1400;
      _gazeTargetX = 0; _gazeTargetY = 0;
    } else if (in.tapped && _mood != Mood::Surprised) {
      _mood = Mood::Happy;
      _reactionUntil = in.now + 1600;
    }
    if ((_mood == Mood::Surprised || _mood == Mood::Happy) && (int32_t)(in.now - _reactionUntil) >= 0) {
      _mood = Mood::Awake;
      scheduleGaze(in.now);
    }

    // ---- 眠気の状態遷移 ----
    if (_mood == Mood::Awake || _mood == Mood::Drowsy || _mood == Mood::Sleep) {
      if (!in.autoSleepEnabled || in.moving || in.touching) {
        if (_mood != Mood::Awake) {
          _mood = Mood::Awake;
          _wakeUntil = in.now + 900;   // 目をこする風の「ゆっくり開く」
          scheduleBlink(in.now + 600);
        }
      } else if (in.idleMs >= SLEEP_AFTER_MS) {
        _mood = Mood::Sleep;
      } else if (in.idleMs >= DROWSY_AFTER_MS) {
        _mood = Mood::Drowsy;
      }
    }

    // ---- 呼吸っぽいスケール ----
    {
      const float period = (_mood == Mood::Sleep) ? 4.2f : 3.0f;
      const float amp = (_mood == Mood::Sleep) ? 0.035f : 0.015f;
      _breath = 1.0f + amp * sinf(_time * 6.2831853f / period);
    }

    // ---- 目標の「開き具合」 ----
    float targetOpen = 1.0f;
    float targetEyeScale = 1.0f, targetPupilScale = 1.0f, targetBlush = 0.0f;
    switch (_mood) {
      case Mood::Awake:
        targetOpen = 1.0f;
        if ((int32_t)(in.now - _wakeUntil) < 0) targetOpen = 0.6f;
        if (in.proximity > 0.02f) {
          // 何か近づいてくる: 目を見開いて瞳孔が開く。触れる直前は少し身構えて細目に
          const float p = in.proximity;
          targetEyeScale = 1.0f + 0.10f * p;
          targetPupilScale = (p < 0.7f) ? 1.0f + 0.25f * p : 1.175f - 0.55f * (p - 0.7f);
          if (p > 0.8f) targetOpen = 1.0f - 0.45f * (p - 0.8f) / 0.2f;
        }
        break;
      case Mood::Drowsy:
        targetOpen = 0.42f + 0.10f * sinf(_time * 1.3f);
        targetPupilScale = 1.05f;
        break;
      case Mood::Sleep:
        targetOpen = 0.0f;
        break;
      case Mood::Surprised:
        targetOpen = 1.0f; targetEyeScale = 1.16f; targetPupilScale = 0.62f;
        break;
      case Mood::Happy:
        targetOpen = 0.0f; targetBlush = 1.0f; targetEyeScale = 1.05f;
        break;
    }
    const float lidRate = (_mood == Mood::Sleep || _mood == Mood::Drowsy) ? 2.5f : 9.0f;
    _open += (targetOpen - _open) * clamp01(dt * lidRate);
    _eyeScale += (targetEyeScale - _eyeScale) * clamp01(dt * 10.0f);
    _pupilScale += (targetPupilScale - _pupilScale) * clamp01(dt * 10.0f);
    _blush += (targetBlush - _blush) * clamp01(dt * 6.0f);

    // ---- まばたき ----
    _blinkAmount = 0.0f;
    if (_mood == Mood::Awake || _mood == Mood::Drowsy) {
      if (_blinking) {
        const float dur = (_mood == Mood::Drowsy) ? (BLINK_DURATION_MS * 2.6f) : (float)BLINK_DURATION_MS;
        const float t = (in.now - _blinkStart) / dur;
        if (t >= 1.0f) {
          _blinking = false;
          if (_doubleBlinkPending) { _doubleBlinkPending = false; _blinking = true; _blinkStart = in.now + 70; }
          else scheduleBlink(in.now);
        } else if (t > 0.0f) {
          _blinkAmount = sinf(t * 3.14159265f);
        }
      } else if ((int32_t)(in.now - _nextBlink) >= 0) {
        _blinking = true;
        _blinkStart = in.now;
        _doubleBlinkPending = (random(1000) < (long)(DOUBLE_BLINK_PROBABILITY * 1000));
      }
    }

    // ---- 視線 (キョロキョロ) ----
    if (in.touching) {
      _gazeTargetX = clampf(in.touchGazeX, -1.0f, 1.0f);
      _gazeTargetY = clampf(in.touchGazeY, -1.0f, 1.0f);
      _nextGaze = in.now + 400;
    } else if (in.proximity > 0.15f && _mood == Mood::Awake) {
      // 近づいてくるものを見つめる (方向は分からないので正面〜少し上)
      _gazeTargetX += (0.0f - _gazeTargetX) * clamp01(dt * 6.0f);
      _gazeTargetY += (-0.15f - _gazeTargetY) * clamp01(dt * 6.0f);
      _nextGaze = in.now + 300;
    } else if (_mood == Mood::Surprised) {
      // 小刻みに震える
      _gazeTargetX = (random(200) - 100) / 900.0f;
      _gazeTargetY = (random(200) - 100) / 900.0f;
    } else if (_mood == Mood::Sleep) {
      _gazeTargetX = 0.0f; _gazeTargetY = 0.35f;
    } else if (_mood == Mood::Drowsy) {
      if ((int32_t)(in.now - _nextGaze) >= 0) {
        _gazeTargetX = (random(200) - 100) / 250.0f;
        _gazeTargetY = 0.25f + random(100) / 300.0f;
        _nextGaze = in.now + 1500 + random(2500);
      }
    } else if ((int32_t)(in.now - _nextGaze) >= 0) {
      scheduleGaze(in.now);
    }
    const float gazeRate = (_mood == Mood::Drowsy || _mood == Mood::Sleep) ? 2.0f : GAZE_SPEED;
    _gazeX += (_gazeTargetX - _gazeX) * clamp01(dt * gazeRate);
    _gazeY += (_gazeTargetY - _gazeY) * clamp01(dt * gazeRate);
  }

  // 顔スプライトに描く (回転は呼び出し側で)
  void draw() {
    _sprite.fillScreen(COLOR_TRANSPARENT);
    const float s = _scale;
    const float c = _sprite.width() / 2.0f;
    const float open = clamp01(_open * (1.0f - _blinkAmount));

    for (int i = -1; i <= 1; i += 2) {
      const float cx = c + i * EYE_OFFSET_X * s;
      const float cy = c + EYE_OFFSET_Y * s;
      drawEye(cx, cy, open, s);
    }

    if (_blush > 0.05f) {
      const uint16_t col = lerpColor(0x0000, COLOR_BLUSH, _blush);
      for (int i = -1; i <= 1; i += 2) {
        _sprite.fillEllipse((int)(c + i * (EYE_OFFSET_X + 8) * s), (int)(c + (EYE_OFFSET_Y + EYE_RADIUS + 26) * s),
                            (int)(28 * s), (int)(11 * s), col);
      }
    }

    if (_mood == Mood::Sleep && _open < 0.15f) drawZzz(c, s);
  }

  M5Canvas& sprite() { return _sprite; }

private:
  static float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
  static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

  static uint16_t lerpColor(uint16_t a, uint16_t b, float t) {
    auto ch = [](uint16_t c, int shift, int bits) { return (c >> shift) & ((1 << bits) - 1); };
    int r = ch(a, 11, 5) + (int)((ch(b, 11, 5) - ch(a, 11, 5)) * t);
    int g = ch(a, 5, 6) + (int)((ch(b, 5, 6) - ch(a, 5, 6)) * t);
    int bl = ch(a, 0, 5) + (int)((ch(b, 0, 5) - ch(a, 0, 5)) * t);
    return (uint16_t)((r << 11) | (g << 5) | bl);
  }

  void scheduleBlink(uint32_t now) {
    _nextBlink = now + BLINK_INTERVAL_MIN_MS + random(BLINK_INTERVAL_MAX_MS - BLINK_INTERVAL_MIN_MS);
  }
  void scheduleGaze(uint32_t now) {
    // たまに正面を見る、それ以外はランダムな方向
    if (random(100) < 22) { _gazeTargetX = 0; _gazeTargetY = 0; }
    else {
      const float ang = random(360) * 0.01745329f;
      const float r = 0.45f + random(55) / 100.0f;
      _gazeTargetX = cosf(ang) * r;
      _gazeTargetY = sinf(ang) * r * 0.85f;
    }
    _nextGaze = now + GAZE_HOLD_MIN_MS + random(GAZE_HOLD_MAX_MS - GAZE_HOLD_MIN_MS);
  }

  // 楕円弧を太線で描く (角度は y 下向き座標系で時計回り, 度)
  void drawCurve(float cx, float cy, float rx, float ry, float a0, float a1, float width, uint16_t color) {
    const int segs = 14;
    float px = 0, py = 0;
    for (int k = 0; k <= segs; ++k) {
      const float t = (a0 + (a1 - a0) * k / segs) * 0.01745329f;
      const float x = cx + rx * cosf(t);
      const float y = cy + ry * sinf(t);
      if (k > 0) _sprite.drawWideLine(px, py, x, y, width, color);
      px = x; py = y;
    }
  }

  void drawEye(float cx, float cy, float open, float s) {
    const float er = EYE_RADIUS * s * _eyeScale * _breath;
    const float pr = PUPIL_RADIUS * s * _pupilScale * _breath;

    if (_mood == Mood::Happy) {
      // にっこり (^ ^)
      drawCurve(cx, cy + er * 0.25f, er * 0.95f, er * 0.85f, 205, 335, 7.0f * s, COLOR_EYE_WHITE);
      return;
    }
    if (open < 0.07f) {
      // 閉じた目 (‿)
      const float ry = (_mood == Mood::Sleep) ? er * 0.35f : er * 0.12f;
      drawCurve(cx, cy - er * 0.15f, er * 0.9f, ry, 20, 160, 6.5f * s, COLOR_EYE_WHITE);
      return;
    }

    // 白目 (まぶたは縦につぶして表現)
    const float ry = er * open;
    _sprite.fillEllipse((int)cx, (int)cy, (int)er, (int)ry, COLOR_EYE_WHITE);

    // 黒目
    const float travel = (er - pr) * PUPIL_TRAVEL;
    const float px = cx + _gazeX * travel;
    const float py = cy + _gazeY * travel * open;
    _sprite.fillEllipse((int)px, (int)py, (int)pr, (int)(pr * open), COLOR_PUPIL);

    // ハイライト
    if (open > 0.35f) {
      const float hr = pr * 0.24f;
      _sprite.fillCircle((int)(px - pr * 0.36f), (int)(py - pr * 0.36f * open), (int)hr, COLOR_EYE_WHITE);
    }
  }

  void drawZzz(float c, float s) {
    _sprite.setTextDatum(middle_center);
    _sprite.setTextColor(COLOR_ZZZ);
    const float baseX = c + (EYE_OFFSET_X + EYE_RADIUS - 4) * s;
    const float baseY = c + (EYE_OFFSET_Y - EYE_RADIUS + 4) * s;
    for (int k = 0; k < 3; ++k) {
      float ph = fmodf(_time * 0.55f + k * 0.33f, 1.0f);     // 0..1 上昇
      const float x = baseX + (14.0f * k + 18.0f * ph) * s;
      const float y = baseY - (22.0f * k + 46.0f * ph) * s;
      const float size = (0.9f + 0.6f * k + 0.7f * ph);
      _sprite.setFont(&fonts::FreeSansBold12pt7b);
      _sprite.setTextSize(size * s);
      if (ph < 0.85f) _sprite.drawString("z", (int)x, (int)y);
    }
    _sprite.setTextSize(1);
  }

  M5Canvas _sprite;
  float _scale = 1.0f;
  Mood _mood = Mood::Awake;
  float _time = 0.0f;
  float _open = 1.0f, _eyeScale = 1.0f, _pupilScale = 1.0f, _blush = 0.0f, _breath = 1.0f;
  float _gazeX = 0.0f, _gazeY = 0.0f, _gazeTargetX = 0.0f, _gazeTargetY = 0.0f;
  uint32_t _nextGaze = 0, _nextBlink = 0, _blinkStart = 0, _reactionUntil = 0, _wakeUntil = 0;
  bool _blinking = false, _doubleBlinkPending = false;
  float _blinkAmount = 0.0f;
};
