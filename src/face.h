// バッジの表情: 「気分」の状態機械と、2 つの見た目 (まっくろくろすけ / Bot マーク) の描画
#pragma once
#include <M5Unified.h>
#include <math.h>
#include "config.h"
#include "mood.h"
#include "bot.h"

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
      _nextNote = in.now;
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
        // 大口を開けるほど目は細くなる (トトロのあくび)
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

    // ---- 音符 ----
    for (auto& n : _notes) {
      if (n.active) { n.phase += dt / NOTE_LIFE_SEC; if (n.phase >= 1.0f) n.active = false; }
    }
    if (_mood == Mood::Music && (int32_t)(in.now - _nextNote) >= 0) {
      for (auto& n : _notes) {
        if (n.active) continue;
        n.active = true;
        n.phase = 0.0f;
        n.x = (float)((random(2) ? 1 : -1) * (EYE_OFFSET_X + EYE_RADIUS - 10 + random(40)));
        n.y = (float)(EYE_OFFSET_Y - EYE_RADIUS + random(40));
        n.sway = 10.0f + random(14);
        n.size = 0.8f + random(50) / 100.0f;
        n.kind = (uint8_t)random(3);
        break;
      }
      _nextNote = in.now + NOTE_SPAWN_MS + random(250);
    }
  }

  // 口を閉じた瞬間 (バイブ用)。1 回だけ true を返す
  bool takeChomp() { bool c = _chompPending; _chompPending = false; return c; }

  // Bot の見た目の状態を進める (体の伸縮・形の morph・目の動き)。update() の後に呼ぶ
  void updateBot(const FaceInput& in) {
    _bot.update(_mood, in.dt, in.now, in.touchGazeX, in.touchGazeY, clamp01(_open * (1.0f - _blinkAmount)),
                _mouthVisible, _mouthOpen, in.touching || _mood == Mood::Bite);
  }

  // 顔スプライトに描く (回転は呼び出し側で)
  // styleIndex: 0 = まっくろくろすけ、1 = Bot
  void draw(uint8_t styleIndex) {
    _sprite.fillScreen(COLOR_TRANSPARENT);
    if (styleIndex == STYLE_BOT) {
      _bot.draw(_sprite, _sprite.width() / 2.0f, _sprite.width() / 2.0f, _scale);
      drawNotes(_sprite.width() / 2.0f, _scale);
      if (_mood == Mood::Sleep && _open < 0.15f) drawZzz(_sprite.width() / 2.0f, _scale);
    } else {
      drawMakkuro();
    }
  }

  M5Canvas& sprite() { return _sprite; }

private:
  static float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
  static float easeOut(float t) { t = clamp01(t); return 1.0f - (1.0f - t) * (1.0f - t); }
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

  // ---- まっくろくろすけ (大きな目 + 口 + 音符) ----
  void drawMakkuro() {
    const float s = _scale;
    const float c = _sprite.width() / 2.0f;
    const float open = clamp01(_open * (1.0f - _blinkAmount));
    float swayX = 0.0f, swayY = 0.0f;
    if (_mood == Mood::Music) {   // 音楽に合わせてゆらゆら
      swayX = 6.0f * sinf(_time * 4.8f);
      swayY = 4.0f * sinf(_time * 9.6f);
    }

    if (_mouthVisible > 0.02f) drawMouth(c, s, MOUTH_OFFSET_Y, 1.0f);   // 目より先に描く (大口が目に被らないよう目を上に)

    for (int i = -1; i <= 1; i += 2) {
      const float cx = c + (i * EYE_OFFSET_X + swayX) * s;
      const float cy = c + (EYE_OFFSET_Y + swayY) * s;
      drawEye(cx, cy, open, s, EYE_RADIUS, PUPIL_RADIUS);
    }

    if (_blush > 0.05f) {
      const uint16_t col = lerpColor(0x0000, COLOR_BLUSH, _blush);
      for (int i = -1; i <= 1; i += 2) {
        _sprite.fillEllipse((int)(c + i * (EYE_OFFSET_X + 8) * s), (int)(c + (EYE_OFFSET_Y + EYE_RADIUS + 26) * s),
                            (int)(28 * s), (int)(11 * s), col);
      }
    }

    drawNotes(c, s);
    if (_mood == Mood::Sleep && _open < 0.15f) drawZzz(c, s);
  }

  // 目を 1 つ描く (まっくろくろすけ: 白目 + 黒目)
  void drawEye(float cx, float cy, float open, float s, float eyeR, float pupilR) {
    const float er = eyeR * s * _eyeScale * _breath;
    const float pr = pupilR * s * _pupilScale * _breath;

    if (_mood == Mood::Happy || _mood == Mood::Music) {
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

  // 口。閉じているときは薄い笑いの線、開くと赤い口の中と歯と舌
  void drawMouth(float c, float s, int offsetY, float sizeScale) {
    const float vis = clamp01(_mouthVisible);
    const float open = clamp01(_mouthOpen);
    s *= sizeScale;
    const float mx = c, my = c + offsetY * _scale;
    // 幅も開きに合わせて広がる (閉じているときは半分、全開でトトロのあくび級)
    const float hw = MOUTH_HALF_W * s * (0.45f + 0.55f * open);
    const uint16_t lineCol = lerpColor(0x0000, COLOR_MOUTH_LINE, vis);
    if (open < 0.06f) {
      drawCurve(mx, my - 6 * s, hw, 10 * s, 20, 160, 5.0f * s, lineCol);
      return;
    }
    const float hh = (6.0f + MOUTH_MAX_OPEN * open) * s * 0.5f;
    _sprite.fillEllipse((int)mx, (int)my, (int)hw, (int)hh, lerpColor(0x0000, COLOR_MOUTH, vis));
    // 舌 (下側にどっしり)
    if (open > 0.25f) {
      _sprite.fillEllipse((int)mx, (int)(my + hh * 0.55f), (int)(hw * 0.55f), (int)(hh * 0.42f), lerpColor(0x0000, COLOR_TONGUE, vis));
    }
    // 歯: 上 8 本、下 6 本。開くほど長く
    if (open > 0.15f) {
      const uint16_t teeth = lerpColor(0x0000, COLOR_TEETH, vis);
      const float tl = (12.0f + 22.0f * open) * s;
      const int nTop = 8, nBot = 6;
      for (int k = 0; k < nTop; ++k) {
        const float u = (k + 0.5f) / nTop * 2.0f - 1.0f;             // -1..1
        const float x = mx + u * hw * 0.86f;
        const float edge = sqrtf(1.0f - u * u * 0.74f);              // 楕円の縁に沿わせる
        const float yTop = my - hh * edge + 2 * s;
        const float tw = 9.0f * s;
        _sprite.fillTriangle((int)(x - tw), (int)yTop, (int)(x + tw), (int)yTop, (int)x, (int)(yTop + tl * (0.7f + 0.3f * edge)), teeth);
      }
      for (int k = 0; k < nBot; ++k) {
        const float u = (k + 0.5f) / nBot * 2.0f - 1.0f;
        const float x = mx + u * hw * 0.72f;
        const float edge = sqrtf(1.0f - u * u * 0.52f);
        const float yBot = my + hh * edge - 2 * s;
        const float tw = 8.0f * s;
        _sprite.fillTriangle((int)(x - tw), (int)yBot, (int)(x + tw), (int)yBot, (int)x, (int)(yBot - tl * 0.75f * (0.7f + 0.3f * edge)), teeth);
      }
    }
    for (int k = 0; k < 4; ++k) _sprite.drawEllipse((int)mx, (int)my, (int)hw + k, (int)hh + k, lineCol);
  }

  // 音符 (玉 + 棒 + 旗。kind 2 は連桁の 2 音)。上に昇りながら左右に揺れ、フェードイン/アウト
  void drawNotes(float c, float s) {
    for (const auto& n : _notes) {
      if (!n.active) continue;
      const float ph = n.phase;
      float alpha = 1.0f;
      if (ph < 0.15f) alpha = ph / 0.15f;
      else if (ph > 0.65f) alpha = (1.0f - ph) / 0.35f;
      const uint16_t col = lerpColor(0x0000, COLOR_NOTE, clamp01(alpha));
      const float x = c + (n.x + sinf(ph * 9.42f + n.x) * n.sway) * s;
      const float y = c + (n.y - ph * NOTE_RISE_PX) * s;
      const float k = n.size * s;
      _sprite.fillEllipse((int)x, (int)y, (int)(8 * k), (int)(6 * k), col);
      _sprite.drawWideLine(x + 7 * k, y, x + 7 * k, y - 26 * k, 2.6f * k, col);
      if (n.kind == 2) {
        _sprite.fillEllipse((int)(x + 22 * k), (int)(y - 4 * k), (int)(8 * k), (int)(6 * k), col);
        _sprite.drawWideLine(x + 29 * k, y - 4 * k, x + 29 * k, y - 30 * k, 2.6f * k, col);
        _sprite.drawWideLine(x + 7 * k, y - 26 * k, x + 29 * k, y - 30 * k, 4.0f * k, col);
      } else {
        _sprite.drawWideLine(x + 7 * k, y - 26 * k, x + 17 * k, y - 16 * k, 3.0f * k, col);
      }
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
  BotRenderer _bot;
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
  // 音符
  struct Note { bool active; float x, y, phase, sway, size; uint8_t kind; };
  Note _notes[NOTES_MAX] = {};
  uint32_t _nextNote = 0;
};
