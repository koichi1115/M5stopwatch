// Badge activity state and geometric Bot mark rendering.
#pragma once
#include <M5Unified.h>
#include <math.h>
#include "config.h"
#include "mark_renderer.h"

enum class Mood : uint8_t { Awake, Drowsy, Sleep, Surprised, Happy, Startled, Music, Bite };

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
  bool loudNoise;        // 急に大きい音がした
  bool music;            // 音楽が聞こえている
  uint32_t mouthTouchMs; // 口元に触れ続けている時間 (0 = 触れていない)
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

    // ---- 口元に指 (2 秒以上) → 口が出てきてかじろうとする ----
    if (in.mouthTouchMs >= MOUTH_TOUCH_HOLD_MS) {
      if (_mood != Mood::Bite) { _mood = Mood::Bite; _mouthCycleStart = in.now; _chompDone = false; }
    } else if (_mood == Mood::Bite) {
      _mood = Mood::Awake;
      scheduleGaze(in.now);
    }

    // ---- 一時的なリアクション ----
    if (_mood != Mood::Bite) {
      if (in.loudNoise) {
        _mood = Mood::Startled;              // 驚いてキョロキョロ
        _reactionUntil = in.now + STARTLE_DURATION_MS;
        _nextGaze = in.now;
        _startleSide = (random(2) == 0) ? -1 : 1;
      } else if (in.shaken && _mood != Mood::Surprised) {
        _mood = Mood::Surprised;
        _reactionUntil = in.now + 1400;
        _gazeTargetX = 0; _gazeTargetY = 0;
      } else if (in.tapped && _mood != Mood::Surprised && _mood != Mood::Startled) {
        _mood = Mood::Happy;
        _reactionUntil = in.now + 1600;
      }
    }
    if ((_mood == Mood::Surprised || _mood == Mood::Happy || _mood == Mood::Startled) && (int32_t)(in.now - _reactionUntil) >= 0) {
      _mood = Mood::Awake;
      scheduleGaze(in.now);
    }

    // ---- 音楽 → 目を閉じて聴き入る ----
    if (in.music && (_mood == Mood::Awake || _mood == Mood::Drowsy || _mood == Mood::Sleep)) {
      _mood = Mood::Music;
    } else if (!in.music && _mood == Mood::Music) {
      _mood = Mood::Awake;
      _wakeUntil = in.now + 900;
      scheduleBlink(in.now + 600);
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
      case Mood::Startled:
        targetOpen = 1.0f; targetEyeScale = 1.18f; targetPupilScale = 0.55f;
        break;
      case Mood::Music:
        targetOpen = 0.0f; targetBlush = 0.35f; targetEyeScale = 1.04f;
        break;
      case Mood::Bite:
        // Keep the activity state while the touch gesture is held.
        targetOpen = 0.6f - 0.45f * clamp01(_mouthOpen); targetPupilScale = 1.1f;
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
    if (_mood == Mood::Startled) {
      // 左右に素早く見回す
      if ((int32_t)(in.now - _nextGaze) >= 0) {
        _startleSide = -_startleSide;
        _gazeTargetX = _startleSide * (0.7f + random(30) / 100.0f);
        _gazeTargetY = (random(100) - 50) / 140.0f;
        _nextGaze = in.now + 170 + random(160);
      }
    } else if (_mood == Mood::Bite) {
      // 口元の指を見下ろす
      _gazeTargetX = clampf(in.touchGazeX, -1.0f, 1.0f) * 0.6f;
      _gazeTargetY = 1.0f;
    } else if (in.touching) {
      _gazeTargetX = clampf(in.touchGazeX, -1.0f, 1.0f);
      _gazeTargetY = clampf(in.touchGazeY, -1.0f, 1.0f);
      _nextGaze = in.now + 400;
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
    const float gazeRate = (_mood == Mood::Drowsy || _mood == Mood::Sleep) ? 2.0f
                         : (_mood == Mood::Startled ? GAZE_SPEED * 2.2f : GAZE_SPEED);
    _gazeX += (_gazeTargetX - _gazeX) * clamp01(dt * gazeRate);
    _gazeY += (_gazeTargetY - _gazeY) * clamp01(dt * gazeRate);

    // ---- 口 (かじる: 開く → 一瞬止まる → パクッと閉じる → 間) ----
    {
      const float visTarget = (_mood == Mood::Bite) ? 1.0f : 0.0f;
      _mouthVisible += (visTarget - _mouthVisible) * clamp01(dt * (visTarget > _mouthVisible ? 7.0f : 3.0f));
      if (_mood == Mood::Bite) {
        const uint32_t T = in.now - _mouthCycleStart;
        if (T < 650)       _mouthOpen = easeOut(T / 650.0f);          // ぐわっと開く
        else if (T < 800)  _mouthOpen = 1.0f;                          // 全開で一瞬止まる
        else if (T < 920)  { _mouthOpen = 1.0f - (T - 800) / 120.0f; if (!_chompDone) { _chompDone = true; _chompPending = true; } }  // パクッ
        else if (T < 1300) _mouthOpen = 0.0f;
        else { _mouthCycleStart = in.now; _chompDone = false; }
      } else {
        _mouthOpen += (0.0f - _mouthOpen) * clamp01(dt * 8.0f);
      }
    }

  }

  // 口を閉じた瞬間 (バイブ用)。1 回だけ true を返す
  bool takeChomp() { bool c = _chompPending; _chompPending = false; return c; }

  // Draw the selected local mark into the rotating badge sprite.
  void draw(const MarkDefinition& mark) {
    _sprite.fillScreen(COLOR_TRANSPARENT);
    const float s = _scale;
    const float c = _sprite.width() / 2.0f;
    const float reactionScale = _eyeScale * _breath;
    const int radius = (int)(MARK_RADIUS * s * reactionScale);
    drawBotMark(_sprite, (int)c, (int)c, radius, mark);
  }

  M5Canvas& sprite() { return _sprite; }

private:
  static float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
  static float easeOut(float t) { t = clamp01(t); return 1.0f - (1.0f - t) * (1.0f - t); }
  static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

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

  M5Canvas _sprite;
  float _scale = 1.0f;
  Mood _mood = Mood::Awake;
  float _time = 0.0f;
  float _open = 1.0f, _eyeScale = 1.0f, _pupilScale = 1.0f, _blush = 0.0f, _breath = 1.0f;
  float _gazeX = 0.0f, _gazeY = 0.0f, _gazeTargetX = 0.0f, _gazeTargetY = 0.0f;
  uint32_t _nextGaze = 0, _nextBlink = 0, _blinkStart = 0, _reactionUntil = 0, _wakeUntil = 0;
  bool _blinking = false, _doubleBlinkPending = false;
  float _blinkAmount = 0.0f;
  // 口
  float _mouthOpen = 0.0f, _mouthVisible = 0.0f;
  uint32_t _mouthCycleStart = 0;
  bool _chompDone = false, _chompPending = false;
  // 驚き
  int8_t _startleSide = 1;
};
