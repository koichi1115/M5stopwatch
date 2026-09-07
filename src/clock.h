// 時計モード: 円形画面を活かした表示
//   - 外周のゲージ: 1〜12 の目盛りで「時」を表す。経過したぶんだけ点灯し、
//     いま指している時間の目盛りは分の進みに合わせて少しずつ満ちていく
//   - 中央の数字: 「分」
//   - 下に小さく秒と日付
//   時刻は本体の RTC (RX8130) から読む。Wi-Fi が使えるときは NTP で合わせられる。
#pragma once
#include <M5Unified.h>
#include <math.h>
#include "config.h"

class ClockFace {
public:
  // 顔スプライト (正方形, 中心 c) に描く。回転は呼び出し側が行うので常に正立する
  void draw(M5Canvas& sp, float c, uint32_t now) {
    m5::rtc_datetime_t dt;
    const bool ok = M5.Rtc.isEnabled() && M5.Rtc.getDateTime(&dt);
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
};
