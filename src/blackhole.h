// ブラックホールモード: 重力レンズで歪んだ降着円盤が回り続ける
//   光の曲がり方の計算 (光線の逆追跡) は重いので PC で済ませてある (tools/make_blackhole.py → blackhole_lut.h)。
//   表には「顔スプライトの各画素が円盤のどのリング・どの方位角を映すか」と明るさが入っている。
//   毎フレームはリングごとにケプラー回転 (内側ほど速い) の分だけ方位角をずらし、模様と色の表を引くだけ。
//   焼き付き対策に、絵全体を数 px だけゆっくり動かす。
#pragma once
#include <M5Unified.h>
#include <math.h>
#include "config.h"
#include "blackhole_lut.h"

class BlackHole {
public:
  // 顔スプライト (BH_SIZE 角, 16bit, 透過色で塗りつぶし済み) に描く。回転は呼び出し側が行う
  void draw(M5Canvas& sp, uint32_t now) {
    if (sp.width() != BH_SIZE || sp.height() != BH_SIZE || sp.getColorDepth() != 16) {
      sp.setTextDatum(middle_center);
      sp.setFont(&fonts::lgfxJapanGothic_16);
      sp.setTextColor(COLOR_LEVER_MUTE);
      sp.drawString("メモリ不足で表示できません", sp.width() / 2, sp.height() / 2);
      return;
    }
    if (!_ready) init();
    uint16_t* buf = (uint16_t*)sp.getBuffer();   // スプライトのメモリは RGB565 のバイト順を入れ替えて持つ

    // 焼き付き対策: ゆっくり円を描くように全体をずらす
    const float dp = (now % BLACKHOLE_DRIFT_PERIOD_MS) * (2.0f * (float)M_PI / BLACKHOLE_DRIFT_PERIOD_MS);
    const int dx = (int)lroundf(cosf(dp) * BLACKHOLE_DRIFT_PX);
    const int dy = (int)lroundf(sinf(2 * dp) * BLACKHOLE_DRIFT_PX * 0.6f);

    for (int i = 0; i < BH_STAR_COUNT; ++i) {
      const uint32_t s = BH_STARS[i];
      put(buf, s >> 8, dx, dy, _star[s & 255]);
    }

    // リングごとの方位角のずれ (0..255)。内縁が BLACKHOLE_PERIOD_MS で 1 周
    const uint32_t t = (uint32_t)((uint64_t)now * 65536 / BLACKHOLE_PERIOD_MS);
    uint8_t step[BH_RINGS];
    for (int r = 0; r < BH_RINGS; ++r) step[r] = (uint8_t)(((uint64_t)BH_RING_SPEED[r] * t) >> 24);

    for (int i = 0; i < BH_DISK_COUNT; ++i) {
      const uint32_t d = BH_DISK[i];
      const int ring = (d >> 8) & 63;
      const uint8_t a = (uint8_t)((d & 255) - step[ring]);
      uint32_t v = ((uint32_t)BH_TEXTURE[ring][a] * BH_DISK_BRIGHT[i]) >> 7;   // 模様の平均 (約 0.5) で 1 倍
      if (v > 255) v = 255;
      put(buf, d >> 14, dx, dy, _pal[v]);
    }
  }

private:
  bool _ready = false;
  uint16_t _pal[256];    // バイト順を入れ替え済みの色
  uint16_t _star[256];

  void init() {
    for (int i = 0; i < 256; ++i) {
      _pal[i] = __builtin_bswap16(BH_PALETTE[i]);
      const uint16_t g = ((i >> 3) << 11) | ((i >> 2) << 5) | (i >> 3);   // 灰色
      _star[i] = __builtin_bswap16(g == COLOR_TRANSPARENT ? 0 : g);
    }
    _ready = true;
  }

  static inline void put(uint16_t* buf, uint32_t idx, int dx, int dy, uint16_t c) {
    const int x = (int)(idx % BH_SIZE) + dx, y = (int)(idx / BH_SIZE) + dy;
    if ((unsigned)x < (unsigned)BH_SIZE && (unsigned)y < (unsigned)BH_SIZE) buf[y * BH_SIZE + x] = c;
  }
};
