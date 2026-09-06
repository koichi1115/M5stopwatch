// Bot の見た目: 白い柔らかい体 (プニプニ変形する) と、黒い細長い目だけ。
//
// 実物 (x.ai/bot) の特徴に合わせている:
//   - 体は白 → 灰のなだらかなグラデーションが付いた丸みのある塊。輪郭は常にゆっくり形を変える
//   - 目は白目/黒目に分かれておらず、少し傾いた黒い楕円が 2 つだけ。ハイライトも無い
//   - 口は無い (かじるときだけ体に穴が開くように現れる)
//   - 動きは「squash & stretch」。驚けば縦に伸び、喜べば潰れて弾み、形は滑らかに次の形へ変わる
//     (まっくろくろすけの「黒目がキョロキョロ動く」動きとは別物にしてある。Bot は目玉を動かさず、
//      目そのものと体ごと、ゆっくり大きく動く)
//
// 描画: 極座標で輪郭を作り、走査線ごとに横線で塗る (グラデーションのため)。
//       形は 2 つの目標をブレンドして滑らかに morph する。
#pragma once
#include <M5Unified.h>
#include <math.h>
#include <stdint.h>
#include "config.h"
#include "mark_config.h"
#include "mood.h"

class BotRenderer {
public:
  // ---- 状態を進める ----
  // gazeX/gazeY: -1..1 (体ごと向く方向), open: 目の開き 0..1, mouthVisible/mouthOpen: かじる口
  // follow: 指を追う / かじる など、外から与えた方向をそのまま使う場面
  void update(Mood mood, float dt, uint32_t now, float gazeX, float gazeY, float open,
              float mouthVisible, float mouthOpen, bool follow) {
    updateDent(dt, gazeX, gazeY, follow);
    // バネの積分が発散しないよう時間刻みを制限する (フレーム落ち時に暴れて NaN になるのを防ぐ)
    if (!(dt > 0.0f) || dt > 0.05f) dt = 0.05f;
    _time += dt;
    _mood = mood;
    _open = open;
    _mouthVisible = mouthVisible;
    _mouthOpen = mouthOpen;

    // ---- 形の morph (プニプニ): 一定時間ごとに次の形へ ----
    if ((int32_t)(now - _nextShape) >= 0) {
      _shapeFrom = _shapeTo;
      _shapeTo = (uint8_t)((_shapeTo + 1 + random(MARK_COUNT - 1)) % MARK_COUNT);
      _morph = 0.0f;
      _nextShape = now + BOT_SHAPE_HOLD_MIN_MS + random(BOT_SHAPE_HOLD_MAX_MS - BOT_SHAPE_HOLD_MIN_MS);
      _wobble += 0.5f;   // 形が変わる瞬間にプルッと揺れる
    }
    if (_morph < 1.0f) _morph = clamp01(_morph + dt / BOT_MORPH_SEC);

    // ---- squash & stretch の目標値 (気分ごと) ----
    float targetSquash = 0.0f;   // + で縦に潰れる、- で縦に伸びる
    float targetScale = 1.0f;
    switch (mood) {
      case Mood::Startled:  targetSquash = -0.26f; targetScale = 1.06f; break;   // 驚いて縦に伸びる
      case Mood::Surprised: targetSquash = -0.16f; targetScale = 1.03f; break;
      case Mood::Happy:     targetSquash =  0.20f; targetScale = 1.04f; break;   // 潰れて弾む
      case Mood::Music:     targetSquash =  0.10f * sinf(_time * 5.2f); break;   // リズムに合わせて上下
      case Mood::Drowsy:    targetSquash =  0.12f; targetScale = 0.98f; break;
      case Mood::Sleep:     targetSquash =  0.22f; targetScale = 0.96f; break;   // ぺたんと落ち着く
      case Mood::Bite:      targetSquash = -0.08f; targetScale = 1.02f; break;
      default: break;
    }
    // 呼吸
    targetSquash += 0.03f * sinf(_time * (mood == Mood::Sleep ? 1.5f : 2.2f));

    // 気分が変わった瞬間に弾ませる
    if (mood != _prevMood) {
      _prevMood = mood;
      if (mood == Mood::Startled || mood == Mood::Happy || mood == Mood::Surprised) _wobble += 1.0f;
      else _wobble += 0.35f;
    }

    // バネで追従 (行き過ぎて戻る = 弾力)。semi-implicit Euler + クランプで安定させる
    const float k = 90.0f, damp = 11.0f;
    _squashVel += ((targetSquash - _squash) * k - _squashVel * damp) * dt;
    _squashVel = clampf(_squashVel, -12.0f, 12.0f);
    _squash = clampf(_squash + _squashVel * dt, -0.45f, 0.45f);
    if (!isfinite(_squash) || !isfinite(_squashVel)) { _squash = 0.0f; _squashVel = 0.0f; }
    _scale += (targetScale - _scale) * clamp01(dt * 8.0f);
    _scale = clampf(_scale, 0.5f, 1.5f);

    // ゼリーのような揺れ (時間とともに収まる)
    _wobble *= expf(-dt * 3.2f);
    if (_wobble > 1.4f) _wobble = 1.4f;

    // ---- 向き ----
    // まっくろくろすけの「黒目が素早くキョロキョロ」とは別物にする。
    // Bot は目玉が無いので、目と体ごと、ゆっくり大きく向きを変える。指を追うときだけ外の方向に従う。
    if (follow) {
      _targetX = gazeX; _targetY = gazeY;
      _nextLook = now + 500;
    } else if (mood == Mood::Startled) {
      // 驚いたときだけ素早く見回す (それでも目玉ではなく体ごと)
      if ((int32_t)(now - _nextLook) >= 0) {
        _targetX = ((random(200) - 100) / 100.0f) * 0.9f;
        _targetY = ((random(100) - 50) / 100.0f) * 0.5f;
        _nextLook = now + 260 + random(220);
      }
    } else if (mood == Mood::Sleep || mood == Mood::Drowsy) {
      _targetX = 0.0f; _targetY = 0.35f;
    } else if ((int32_t)(now - _nextLook) >= 0) {
      // ふだんは「しばらく止まって、たまにゆっくり首を振る」
      if (random(100) < 45) { _targetX = 0.0f; _targetY = 0.0f; }
      else {
        _targetX = ((random(200) - 100) / 100.0f) * 0.75f;
        _targetY = ((random(200) - 100) / 100.0f) * 0.35f;
      }
      _nextLook = now + BOT_LOOK_HOLD_MIN_MS + random(BOT_LOOK_HOLD_MAX_MS - BOT_LOOK_HOLD_MIN_MS);
    }
    const float lookRate = (mood == Mood::Startled) ? 8.0f : (follow ? 5.0f : 2.4f);
    _lookX += (_targetX - _lookX) * clamp01(dt * lookRate);
    _lookY += (_targetY - _lookY) * clamp01(dt * lookRate);
  }

  // 指で押されている所を凹ませる。touchX/Y は顔ローカルの正規化座標 (-1..1)
  void updateDent(float dt, float touchX, float touchY, bool pressed) {
    if (pressed) {
      const float d = sqrtf(touchX * touchX + touchY * touchY);
      if (d > 0.03f) { _dentAngle = atan2f(touchY, touchX); _dentDist = d; }
      _dent += (1.0f - _dent) * clamp01(dt * 14.0f);       // 押した瞬間はすばやく凹む
    } else {
      _dent += (0.0f - _dent) * clamp01(dt * 7.0f);        // 離すとぷるんと戻る
      if (_dent < 0.004f) _dent = 0.0f;
    }
  }

  // ---- 描画 (スプライトの中心 cx,cy に上向きで描く) ----
  void draw(M5Canvas& sp, float cx, float cy, float s) const {
    const float baseR = BOT_BODY_RADIUS * s * _scale;
    // squash: 面積を保つように縦横を反対に伸縮させる
    const float sq = _squash;
    const float ry = baseR * (1.0f - sq);
    const float rx = baseR * (1.0f + sq * 0.72f);
    // 見ている方向へ体ごと少し傾ける
    const float leanX = _lookX * BOT_LEAN_PX * s;
    const float leanY = _lookY * BOT_LEAN_PX * 0.6f * s;
    const float bx = cx + leanX, by = cy + leanY;

    // ---- 輪郭 (極座標) ----
    float px[kPoints], py[kPoints];
    for (int i = 0; i < kPoints; ++i) {
      const float th = i * (2.0f * 3.14159265f / kPoints);
      const float r = radiusAt(th);
      px[i] = bx + cosf(th) * r * rx;
      py[i] = by + sinf(th) * r * ry;
    }

    // ---- 走査線で塗る (上が白、下が灰のグラデーション) ----
    float minY = py[0], maxY = py[0];
    for (int i = 1; i < kPoints; ++i) { if (py[i] < minY) minY = py[i]; if (py[i] > maxY) maxY = py[i]; }
    if (!isfinite(minY) || !isfinite(maxY)) return;   // 念のため (NaN が来たら描かない)
    const int h1 = sp.height(), w1 = sp.width();
    const int y0 = clampi((int)floorf(minY), 0, h1 - 1);
    const int y1 = clampi((int)ceilf(maxY), 0, h1 - 1);
    const float h = (maxY - minY) > 1.0f ? (maxY - minY) : 1.0f;
    for (int y = y0; y <= y1; ++y) {
      float xl = 1e9f, xr = -1e9f;
      const float fy = y + 0.5f;
      for (int i = 0; i < kPoints; ++i) {
        const int j = (i + 1) % kPoints;
        const float ay = py[i], byy = py[j];
        if ((ay <= fy && byy > fy) || (byy <= fy && ay > fy)) {
          const float t = (fy - ay) / (byy - ay);
          const float x = px[i] + (px[j] - px[i]) * t;
          if (x < xl) xl = x;
          if (x > xr) xr = x;
        }
      }
      if (xr < xl || !isfinite(xl) || !isfinite(xr)) continue;
      const int ix0 = clampi((int)floorf(xl), 0, w1 - 1);
      const int ix1 = clampi((int)ceilf(xr), 0, w1 - 1);
      if (ix1 < ix0) continue;
      const float t = (fy - minY) / h;
      sp.drawFastHLine(ix0, y, ix1 - ix0 + 1, bodyColor(t));
    }

    // ---- 口 (かじるときだけ、体に開いた穴として現れる) ----
    if (_mouthVisible > 0.02f && _mouthOpen > 0.03f) {
      const float mw = BOT_MOUTH_W * s * (0.5f + 0.5f * _mouthOpen) * _mouthVisible;
      const float mh = BOT_MOUTH_H * s * _mouthOpen * _mouthVisible;
      const float my = by + BOT_MOUTH_OFFSET_Y * s * (1.0f - sq);
      sp.fillEllipse((int)bx, (int)my, (int)mw, (int)mh, COLOR_BOT_MOUTH);
      if (_mouthOpen > 0.35f) {   // 奥の暗がり
        sp.fillEllipse((int)bx, (int)(my + mh * 0.25f), (int)(mw * 0.6f), (int)(mh * 0.45f), COLOR_BOT_THROAT);
      }
    }

    // ---- 目 (黒い細長い楕円が 2 つだけ。白目も黒目も無い) ----
    const float eyeY = by + BOT_EYE_OFFSET_Y * s * (1.0f - sq * 0.6f) + _lookY * BOT_EYE_SHIFT_PX * s;
    const float eyeDX = BOT_EYE_OFFSET_X * s * (1.0f + sq * 0.5f);
    for (int i = -1; i <= 1; i += 2) {
      const float ex = bx + i * eyeDX + _lookX * BOT_EYE_SHIFT_PX * s;
      drawEye(sp, ex, eyeY, s, i);
    }
  }

private:
  static constexpr int kPoints = 72;

  static float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
  static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
  static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

  // 形ごとの極座標パラメータ: r(θ) = 1 + w * cos(N * (θ - rot))
  struct ShapeParam { float n, w, rot; };
  static ShapeParam shapeParam(uint8_t index) {
    switch (markAt(index).shape) {
      case MarkShape::Circle:   return {0.0f, 0.00f, 0.0f};
      case MarkShape::Square:   return {4.0f, 0.13f, 0.7854f};
      case MarkShape::Triangle: return {3.0f, 0.20f, -1.5708f};
      case MarkShape::Diamond:  return {4.0f, 0.17f, -1.5708f};
      case MarkShape::Hexagon:  return {6.0f, 0.09f, 0.0f};
    }
    return {0.0f, 0.0f, 0.0f};
  }

  // 2 つの形をブレンドし、さらにゼリーの揺れを足した半径 (1.0 が基準)
  float radiusAt(float th) const {
    const ShapeParam a = shapeParam(_shapeFrom), b = shapeParam(_shapeTo);
    const float m = smoothstep(_morph);
    const float ra = 1.0f + a.w * cosf(a.n * (th - a.rot));
    const float rb = 1.0f + b.w * cosf(b.n * (th - b.rot));
    float r = ra + (rb - ra) * m;
    // プニプニ: ゆっくりした呼吸のうねり + 反応時のゼリー揺れ
    r += 0.022f * sinf(3.0f * th + _time * 1.3f);
    r += _wobble * 0.075f * sinf(4.0f * th - _time * 13.0f);
    // 指で押されている所だけ内側へ凹ませる (押している位置が縁に近いほど深い)
    if (_dent > 0.004f) {
      float d = th - _dentAngle;
      while (d > 3.14159265f) d -= 6.2831853f;
      while (d < -3.14159265f) d += 6.2831853f;
      const float falloff = expf(-(d * d) / (2.0f * BOT_DENT_WIDTH * BOT_DENT_WIDTH));
      const float reach = clamp01(_dentDist * BOT_TOUCH_TO_BODY);   // 中心を押しても凹まない
      r -= _dent * BOT_DENT_DEPTH * falloff * reach;
    }
    return r;
  }

  static float smoothstep(float t) { t = clamp01(t); return t * t * (3.0f - 2.0f * t); }

  // パレットの色 (mark_config.h の MarkColor) を RGB で取り出す
  static void markRgb(MarkColor c, int& r, int& g, int& b) {
    switch (c) {
      case MarkColor::Cyan:    r = 0x14; g = 0xE6; b = 0xE8; return;
      case MarkColor::Green:   r = 0x36; g = 0xD3; b = 0x5A; return;
      case MarkColor::Yellow:  r = 0xF5; g = 0xC6; b = 0x2B; return;
      case MarkColor::Magenta: r = 0xE8; g = 0x53; b = 0xC8; return;
      case MarkColor::Orange:  r = 0xF7; g = 0x8A; b = 0x2E; return;
    }
    r = g = b = 0xFF;
  }

  // 体の色: 形が変わるのと一緒に色も混ざって変わる。上は白っぽく、下は濃く (つやのある塊に見せる)
  uint16_t bodyColor(float t) const {
    t = clamp01(t);
    int ra, ga, ba, rb, gb, bb;
    markRgb(markAt(_shapeFrom).color, ra, ga, ba);
    markRgb(markAt(_shapeTo).color, rb, gb, bb);
    const float m = smoothstep(_morph);
    const float cr = ra + (rb - ra) * m, cg = ga + (gb - ga) * m, cb = ba + (bb - ba) * m;
    // 上端は白に寄せ、下端は暗く
    const float hi = BOT_BODY_HIGHLIGHT, lo = BOT_BODY_SHADE;
    const float k = hi + (lo - hi) * t;   // 1.0 で白寄り、0.0 で暗い
    float r, g, b;
    if (k >= 0.5f) {   // 白へ寄せる
      const float w = (k - 0.5f) * 2.0f;
      r = cr + (255.0f - cr) * w; g = cg + (255.0f - cg) * w; b = cb + (255.0f - cb) * w;
    } else {           // 黒へ寄せる
      const float w = 1.0f - k * 2.0f;
      r = cr * (1.0f - w); g = cg * (1.0f - w); b = cb * (1.0f - w);
    }
    return (uint16_t)((((int)r & 0xF8) << 8) | (((int)g & 0xFC) << 3) | ((int)b >> 3));
  }

  // 目 1 つ。傾いた黒い楕円 (シアーで傾ける)。side は -1 = 左目、+1 = 右目
  void drawEye(M5Canvas& sp, float ex, float ey, float s, int side) const {
    float w = BOT_EYE_W * s, h = BOT_EYE_H * s;
    float shear = BOT_EYE_TILT * side;   // 外側に向かって少し傾く

    switch (_mood) {
      case Mood::Startled: h *= 1.25f; w *= 1.08f; break;
      case Mood::Surprised: h *= 1.15f; break;
      case Mood::Drowsy:   h *= 0.55f; break;
      case Mood::Bite:     h *= 0.7f;  break;
      default: break;
    }
    // まばたき / 眠り: 縦に潰れて線になる
    float open = clamp01(_open);
    // 喜び / 音楽でも目は閉じず、少し細めるだけ (笑った弧にはしない)
    if (_mood == Mood::Happy || _mood == Mood::Music) { if (open < 0.62f) open = 0.62f; }
    h *= open;
    if (h < BOT_EYE_LINE_W * s * 0.5f) {
      sp.drawWideLine(ex - w * 0.85f, ey, ex + w * 0.85f, ey, BOT_EYE_LINE_W * s, COLOR_BOT_EYE);
      return;
    }
    // 傾いた楕円 = 走査線ごとに横位置をずらした楕円
    const int hy = clampi((int)h, 1, 120);
    for (int dy = -hy; dy <= hy; ++dy) {
      const float k = 1.0f - (float)(dy * dy) / (float)(hy * hy + 1);
      if (k <= 0) continue;
      const float half = w * sqrtf(k);
      const float xc = ex + shear * dy * s;
      sp.drawFastHLine((int)(xc - half), (int)(ey + dy), (int)(half * 2) + 1, COLOR_BOT_EYE);
    }
  }

  Mood _mood = Mood::Awake, _prevMood = Mood::Awake;
  float _time = 0.0f;
  float _squash = 0.0f, _squashVel = 0.0f, _scale = 1.0f, _wobble = 0.0f;
  float _lookX = 0.0f, _lookY = 0.0f;
  float _open = 1.0f, _mouthVisible = 0.0f, _mouthOpen = 0.0f;
  float _dent = 0.0f, _dentAngle = 0.0f, _dentDist = 0.0f;
  float _targetX = 0.0f, _targetY = 0.0f;
  uint32_t _nextLook = 0;
  uint8_t _shapeFrom = 0, _shapeTo = 0;
  float _morph = 1.0f;
  uint32_t _nextShape = 0;
};
