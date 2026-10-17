// 時計モード: 円形画面を活かした表示。タップで 2 つの文字盤を切り替える (NVS に保存)
//   [アナログ] 手書き数字 (hand_digits.h) の文字盤に時針・分針・秒針
//   [ゲージ]
//   - 外周のゲージ: 1〜12 の目盛りで「時」を表す。経過したぶんだけ点灯し、
//     いま指している時間の目盛りは分の進みに合わせて少しずつ満ちていく
//   - 中央の数字: 「分」
//   - 下に小さく秒と日付
//   時刻は本体の RTC (RX8130) から読む。Wi-Fi が使えるときは NTP で合わせられる。
#pragma once
#include <M5Unified.h>
#include <math.h>
#include "config.h"
#include "hand_digits.h"

class ClockFace {
public:
  enum Style : uint8_t { STYLE_ANALOG = 0, STYLE_GAUGE = 1, STYLE_COUNT = 2 };

  void setStyle(uint8_t s) { _style = s < STYLE_COUNT ? s : STYLE_ANALOG; }
  uint8_t style() const { return _style; }
  uint8_t nextStyle() { _style = (_style + 1) % STYLE_COUNT; return _style; }

  // 顔スプライト (正方形, 中心 c) に描く。回転は呼び出し側が行うので常に正立する
  void draw(M5Canvas& sp, float c, uint32_t now) {
    m5::rtc_datetime_t dt;
    const bool ok = M5.Rtc.isEnabled() && M5.Rtc.getDateTime(&dt);
    if (_style == STYLE_ANALOG) drawAnalog(sp, c, ok, dt);
    else drawGauge(sp, c, ok, dt);
  }

  // NTP で時刻を合わせる (Wi-Fi に繋がっているときだけ)
  static bool syncFromNtp() {
    configTzTime(CLOCKFACE_TZ, CLOCKFACE_NTP_SERVER);
    struct tm t;
    if (!getLocalTime(&t, 8000)) { puts("[clock] ntp failed"); return false; }
    M5.Rtc.setDateTime(&t);
    printf("[clock] synced: %04d-%02d-%02d %02d:%02d:%02d\n",
           t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
    return true;
  }

private:
  uint8_t _style = STYLE_ANALOG;

  // ---- 手書き数字 ----
  // グリフを高さ h [px] に拡大縮小して描く (双一次補間)。背景は黒の前提で、色にアルファを掛けて置く
  static float glyphWidth(int d, float h) {
    return HAND_DIGITS[d].w * h / HAND_GLYPH_H;
  }
  static void drawGlyph(M5Canvas& sp, int d, float left, float top, float h, uint16_t color) {
    const HandGlyph& g = HAND_DIGITS[d];
    const float s = h / HAND_GLYPH_H;          // 拡大率
    const int dw = (int)ceilf(g.w * s), dh = (int)ceilf(g.h * s);
    const int x0 = (int)floorf(left), y0 = (int)floorf(top);
    const int r = (color >> 11) & 31, gr = (color >> 5) & 63, b = color & 31;
    for (int y = 0; y < dh; ++y) {
      const float fy = (y + 0.5f) / s - 0.5f;
      const int iy = (int)floorf(fy);
      const float ty = fy - iy;
      for (int x = 0; x < dw; ++x) {
        const float fx = (x + 0.5f) / s - 0.5f;
        const int ix = (int)floorf(fx);
        const float tx = fx - ix;
        auto at = [&](int xx, int yy) -> float {
          return (xx < 0 || yy < 0 || xx >= g.w || yy >= g.h) ? 0.0f : g.alpha[yy * g.w + xx];
        };
        const float a = ((at(ix, iy) * (1 - tx) + at(ix + 1, iy) * tx) * (1 - ty) +
                         (at(ix, iy + 1) * (1 - tx) + at(ix + 1, iy + 1) * tx) * ty) / 255.0f;
        if (a < 0.12f) continue;
        uint16_t c = ((uint16_t)(r * a + 0.5f) << 11) | ((uint16_t)(gr * a + 0.5f) << 5) | (uint16_t)(b * a + 0.5f);
        if (c == COLOR_TRANSPARENT) c = 0;
        sp.drawPixel(x0 + x, y0 + y, c);
      }
    }
  }
  // 数 (1〜2 桁) の幅
  static float numberWidth(int n, float h) {
    if (n < 10) return glyphWidth(n, h);
    return glyphWidth((n / 10) % 10, h) + h * CLOCKFACE_DIGIT_GAP + glyphWidth(n % 10, h);
  }
  // 数 (1〜2 桁) を中心 (cx, cy) に描く
  static void drawNumber(M5Canvas& sp, int n, float cx, float cy, float h, uint16_t color) {
    float x = cx - numberWidth(n, h) / 2;
    if (n >= 10) {
      drawGlyph(sp, (n / 10) % 10, x, cy - h / 2, h, color);
      x += glyphWidth((n / 10) % 10, h) + h * CLOCKFACE_DIGIT_GAP;
    }
    drawGlyph(sp, n % 10, x, cy - h / 2, h, color);
  }

  // 中心から角度 deg (12 時 = 0, 時計回り) の方向に、根元 r0 から先端 r1 まで細くなる針 (w0, w1 は半幅)
  static void drawHand(M5Canvas& sp, float c, float deg, float r0, float r1, float w0, float w1, uint16_t color) {
    const float a = (deg - 90.0f) * (float)M_PI / 180.0f;
    const float ca = cosf(a), sa = sinf(a);
    sp.drawWedgeLine(c + ca * r0, c + sa * r0, c + ca * r1, c + sa * r1, w0, w1, color);
  }

  // ---- アナログ (手書き数字) ----
  void drawAnalog(M5Canvas& sp, float c, bool ok, const m5::rtc_datetime_t& dt) {
    const float k = c / 190.0f;                  // 顔スプライト 380 基準の倍率
    const int hour24 = ok ? dt.time.hours : 10;  // 未設定のときは 10:10 の「見本の針」
    const int minute = ok ? dt.time.minutes : 10;
    const int second = ok ? dt.time.seconds : 0;

    // 目盛り: 分は小さな点、時は少し長い線
    for (int i = 0; i < 60; ++i) {
      const float a = (i * 6.0f - 90.0f) * (float)M_PI / 180.0f;
      const float ca = cosf(a), sa = sinf(a);
      const float ro = CLOCKFACE_TICK_R * k;
      if (i % 5 == 0) {
        const float ri = ro - CLOCKFACE_TICK_LEN * k;
        sp.drawWideLine(c + ca * ri, c + sa * ri, c + ca * ro, c + sa * ro, 1.5f * k, COLOR_CLOCK_TICK);
      } else {
        sp.fillSmoothCircle(c + ca * (ro - 2 * k), c + sa * (ro - 2 * k), 1.6f * k, COLOR_CLOCK_OFF_TICK);
      }
    }

    // 手書きの数字
    const float h = CLOCKFACE_DIGIT_H * k;
    for (int n = 1; n <= 12; ++n) {
      // 数字の外側の縁が同じ円 (CLOCKFACE_DIGIT_OUTER) に揃うよう、幅に合わせて中心を内側へ寄せる
      const float a = (n * 30.0f - 90.0f) * (float)M_PI / 180.0f;
      const float ca = cosf(a), sa = sinf(a);
      const float r = CLOCKFACE_DIGIT_OUTER * k - (fabsf(ca) * numberWidth(n, h) + fabsf(sa) * h) / 2;
      drawNumber(sp, n, c + ca * r, c + sa * r, h, ok ? COLOR_CLOCK_DIGIT : COLOR_CLOCK_SUB);
    }

    // 日付 (手書きの数字で、中心の下に小さく)
    if (ok) drawNumber(sp, dt.date.date, c, c + CLOCKFACE_DAY_Y * k, CLOCKFACE_DAY_H * k, COLOR_CLOCK_SUB);
    else {
      sp.setTextDatum(middle_center);
      sp.setFont(&fonts::lgfxJapanGothic_16);
      sp.setTextColor(COLOR_LEVER_MUTE);
      sp.drawString("時計が未設定", (int)c, (int)(c + CLOCKFACE_DAY_Y * k));
    }

    // 針
    const float hourDeg = (hour24 % 12 + minute / 60.0f) * 30.0f;
    const float minDeg = (minute + second / 60.0f) * 6.0f;
    const float secDeg = second * 6.0f;
    drawHand(sp, c, hourDeg, 0, CLOCKFACE_HOUR_LEN * k, 4.5f * k, 2.0f * k, COLOR_CLOCK_HAND);
    drawHand(sp, c, minDeg, 0, CLOCKFACE_MIN_LEN * k, 3.5f * k, 1.25f * k, COLOR_CLOCK_HAND);
    if (ok) {
      drawHand(sp, c, secDeg + 180.0f, 0, CLOCKFACE_SEC_TAIL * k, 1.0f * k, 1.0f * k, COLOR_CLOCK_SECOND);
      drawHand(sp, c, secDeg, 0, CLOCKFACE_SEC_LEN * k, 1.0f * k, 0.5f * k, COLOR_CLOCK_SECOND);
    }
    sp.fillSmoothCircle(c, c, 8.0f * k, ok ? COLOR_CLOCK_SECOND : COLOR_CLOCK_HAND);
    sp.fillSmoothCircle(c, c, 3.0f * k, TFT_BLACK);
  }

  // ---- ゲージ (外周 12 目盛り + 分の数字) ----
  void drawGauge(M5Canvas& sp, float c, bool ok, const m5::rtc_datetime_t& dt) {
    const int hour24 = ok ? dt.time.hours : 0;
    const int minute = ok ? dt.time.minutes : 0;
    const int second = ok ? dt.time.seconds : 0;
    const int hour12 = ((hour24 + 11) % 12) + 1;        // 0時 → 12
    const float progress = (minute + second / 60.0f) / 60.0f;

    sp.setTextDatum(middle_center);
    sp.setTextSize(1);

    // ---- 外周のゲージ (12 個の弧) ----
    const float R = CLOCKFACE_GAUGE_R * (c / 190.0f);
    const float thick = CLOCKFACE_GAUGE_THICK * (c / 190.0f);
    for (int i = 1; i <= 12; ++i) {
      // 12 時が真上。1 目盛り = 30 度。目盛りの間に隙間を空ける
      const float a0 = -90.0f + (i - 1) * 30.0f + CLOCKFACE_GAUGE_GAP_DEG;
      const float a1 = -90.0f + i * 30.0f - CLOCKFACE_GAUGE_GAP_DEG;
      uint16_t col = COLOR_CLOCK_OFF;
      float end = a1;
      if (i < hour12) {
        col = COLOR_CLOCK_ON;                            // 過ぎた時間
      } else if (i == hour12) {
        col = COLOR_CLOCK_NOW;                           // いまの時間 (分だけ満ちる)
        end = a0 + (a1 - a0) * progress;
        sp.fillArc(c, c, R - thick, R, a0, a1, COLOR_CLOCK_OFF);   // 残りは消灯色で下敷き
      }
      if (end > a0 + 0.5f) sp.fillArc(c, c, R - thick, R, a0, end, col);
    }
    // 12 時の位置に印
    sp.fillCircle((int)c, (int)(c - R - thick * 0.6f), (int)(thick * 0.45f), COLOR_CLOCK_ON);

    // ---- 中央: 分を大きく ----
    char buf[16];
    snprintf(buf, sizeof(buf), "%02d", minute);
    sp.setFont(&fonts::Font7);                            // 7 セグ風の大きな数字
    sp.setTextColor(ok ? COLOR_CLOCK_MINUTE : COLOR_CLOCK_OFF);
    sp.drawString(buf, (int)c, (int)(c - CLOCKFACE_MINUTE_Y));

    // ---- 時と秒 ----
    sp.setFont(&fonts::Font4);
    sp.setTextColor(COLOR_UI);
    snprintf(buf, sizeof(buf), "%d時", hour12);
    sp.drawString(buf, (int)c, (int)(c + CLOCKFACE_HOUR_Y));

    sp.setFont(&fonts::Font2);
    sp.setTextColor(COLOR_CLOCK_SUB);
    snprintf(buf, sizeof(buf), "%02d", second);
    sp.drawString(buf, (int)c, (int)(c + CLOCKFACE_SECOND_Y));

    if (!ok) {
      sp.setFont(&fonts::lgfxJapanGothic_16);
      sp.setTextColor(COLOR_LEVER_MUTE);
      sp.drawString("時計が未設定", (int)c, (int)(c + CLOCKFACE_SECOND_Y + 26));
      return;
    }
    // 日付 (AM/PM も添える)
    sp.setFont(&fonts::Font2);
    sp.setTextColor(COLOR_CLOCK_SUB);
    snprintf(buf, sizeof(buf), "%s  %d/%d", hour24 < 12 ? "AM" : "PM", dt.date.month, dt.date.date);
    sp.drawString(buf, (int)c, (int)(c + CLOCKFACE_DATE_Y));
  }
};
