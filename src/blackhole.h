// ブラックホールモード: 重力レンズで歪んだ降着円盤が回り続ける
//   光の曲がり方の計算 (光線の逆追跡) は重いので PC で済ませてある (tools/make_blackhole.py → blackhole_lut.h)。
//   表には「顔スプライトの各画素が円盤のどのリング・どの方位角を映すか」と明るさが入っている。
//   毎フレームはリングごとにケプラー回転 (内側ほど速い) の分だけ方位角をずらし、模様と色の表を引くだけ。
//
//   - 投げ込み: タップした所に光の塊が生まれ、渦を巻いて落ちながら細長く引き伸ばされ、内縁で消える。
//     塊は円盤の座標 (リング, 方位角) で持つので、重力レンズで上や下に映る像も一緒に動く
//   - 冷える: しばらく動かさないと円盤が赤く暗く冷え、回転もゆっくりになる。動かすと燃え上がる (焼き付き対策も兼ねる)
//   - 焼き付き対策に、絵全体を数 px だけゆっくり動かす
#pragma once
#include <M5Unified.h>
#include <math.h>
#include <string.h>
#include "config.h"
#include "blackhole_lut.h"

class BlackHole {
public:
  // 顔スプライト (BH_SIZE 角, 16bit, 透過色で塗りつぶし済み) に描く。回転は呼び出し側が行う
  //   idleMs: 最後に動かした / 触ってからの時間 (冷え具合を決める)
  void draw(M5Canvas& sp, uint32_t now, uint32_t idleMs) {
    if (sp.width() != BH_SIZE || sp.height() != BH_SIZE || sp.getColorDepth() != 16) {
      sp.setTextDatum(middle_center);
      sp.setFont(&fonts::lgfxJapanGothic_16);
      sp.setTextColor(COLOR_LEVER_MUTE);
      sp.drawString("メモリ不足で表示できません", sp.width() / 2, sp.height() / 2);
      return;
    }
    if (!_ready) init(now);
    float dt = (now - _lastMs) / 1000.0f;
    if (dt > 0.2f) dt = 0.2f;              // 他のモードから戻ったとき等に飛ばない
    _lastMs = now;
    updateTemperature(idleMs, dt);
    updateBlobs(dt);

    uint16_t* buf = (uint16_t*)sp.getBuffer();   // スプライトのメモリは RGB565 のバイト順を入れ替えて持つ

    // 焼き付き対策: ゆっくり円を描くように全体をずらす
    const float dp = (now % BLACKHOLE_DRIFT_PERIOD_MS) * (2.0f * (float)M_PI / BLACKHOLE_DRIFT_PERIOD_MS);
    _dx = (int)lroundf(cosf(dp) * BLACKHOLE_DRIFT_PX);
    _dy = (int)lroundf(sinf(2 * dp) * BLACKHOLE_DRIFT_PX * 0.6f);

    for (int i = 0; i < BH_STAR_COUNT; ++i) {
      const uint32_t s = BH_STARS[i];
      put(buf, s >> 8, _dx, _dy, _star[s & 255]);
    }

    // 回転: 内縁が BLACKHOLE_PERIOD_MS で 1 周 (冷えるとゆっくり)。リングごとの方位角のずれ (0..255)
    const float speed = BLACKHOLE_COLD_SPEED + (1.0f - BLACKHOLE_COLD_SPEED) * _temp;
    _phase += (uint32_t)(dt * 1000.0f * 65536.0f / BLACKHOLE_PERIOD_MS * speed + 0.5f);
    uint8_t step[BH_RINGS];
    for (int r = 0; r < BH_RINGS; ++r) step[r] = (uint8_t)(((uint64_t)BH_RING_SPEED[r] * _phase) >> 24);

    const uint32_t temp = (uint32_t)(_temp * 256);
    const uint32_t flash = (uint32_t)(_flash * 256);
    for (int i = 0; i < BH_DISK_COUNT; ++i) {
      const uint32_t d = BH_DISK[i];
      const int ring = (d >> 8) & 63;
      const uint8_t phi = d & 255;
      const uint32_t bri = BH_DISK_BRIGHT[i];
      uint32_t v = ((uint32_t)BH_TEXTURE[ring][(uint8_t)(phi - step[ring])] * bri) >> 7;   // 模様の平均 (約 0.5) で 1 倍
      v = (v * temp) >> 8;
      if (flash) v += (bri * flash) >> 9;                        // 吸い込んだ瞬間 / 燃え上がる瞬間の閃光
      if (v > 255) v = 255;
      if (_anyHeat) {
        // 投げ込んだ塊は青白いプラズマの色で、円盤の半分より明るい所を上書きする (暗い外側でも見えるよう底上げ)
        uint32_t h = (_heat[ring][phi] * (bri + 96)) >> 8;
        if (h > (v >> 1)) { put(buf, d >> 14, _dx, _dy, _hot[h > 255 ? 255 : h]); continue; }
      }
      put(buf, d >> 14, _dx, _dy, _pal[v]);
    }
  }

  // 顔ローカル座標 (スプライト中心が原点) をタップした: そこに光の塊を落とす
  void drop(float lx, float ly) {
    int slot = -1;
    for (int i = 0; i < BLACKHOLE_MAX_BLOBS; ++i) if (!_blobs[i].active) { slot = i; break; }
    if (slot < 0) {                        // 空きが無ければいちばん内側 (もうすぐ消える) を使い回す
      slot = 0;
      for (int i = 1; i < BLACKHOLE_MAX_BLOBS; ++i) if (_blobs[i].ring < _blobs[slot].ring) slot = i;
    }
    Blob& b = _blobs[slot];
    const int sx = (int)lroundf(lx) + BH_SIZE / 2 - _dx, sy = (int)lroundf(ly) + BH_SIZE / 2 - _dy;
    int ring, phi;
    if (findDisk(sx, sy, ring, phi)) {
      b.ring = ring + 0.5f;
      b.theta = phi;
    } else {
      // 円盤から外れた所 (宇宙や影) を触った: 円盤の外縁から、触った向きに近い所へ落とす
      b.ring = BH_RINGS - 0.5f;
      b.theta = fmodf(atan2f(ly, lx) * (128.0f / (float)M_PI) + 256.0f, 256.0f);
    }
    b.active = true;
    printf("[blackhole] drop ring %.1f phi %.0f\n", b.ring, b.theta);
  }

  // 塊が吸い込まれた (呼び出し側でバイブを鳴らす)
  bool takeGulp() { bool g = _gulp; _gulp = false; return g; }

private:
  struct Blob { float ring = 0, theta = 0; bool active = false; };

  bool _ready = false;
  uint16_t _pal[256];    // バイト順を入れ替え済みの色
  uint16_t _hot[256];
  uint16_t _star[256];
  uint8_t _heat[BH_RINGS][256];   // 塊の明るさ [リング][円盤上の方位角]
  bool _anyHeat = false;
  Blob _blobs[BLACKHOLE_MAX_BLOBS];
  uint32_t _lastMs = 0, _phase = 0;
  float _temp = 1.0f;    // 1: 熱い, BLACKHOLE_COLD_LEVEL: 冷えきった
  float _flash = 0.0f;
  bool _gulp = false;
  int _dx = 0, _dy = 0;

  void init(uint32_t now) {
    for (int i = 0; i < 256; ++i) {
      _pal[i] = __builtin_bswap16(BH_PALETTE[i]);
      _hot[i] = __builtin_bswap16(BH_HOT_PALETTE[i]);
      const uint16_t g = ((i >> 3) << 11) | ((i >> 2) << 5) | (i >> 3);   // 灰色
      _star[i] = __builtin_bswap16(g == COLOR_TRANSPARENT ? 0 : g);
    }
    memset(_heat, 0, sizeof(_heat));
    _lastMs = now;
    _ready = true;
  }

  // 放っておく時間に応じて冷やす。動いたらすぐ温め、冷えていたぶんだけ閃光を出す
  void updateTemperature(uint32_t idleMs, float dt) {
    float target = 1.0f;
    if (idleMs > BLACKHOLE_COOL_AFTER_MS) {
      const float k = (float)(idleMs - BLACKHOLE_COOL_AFTER_MS) / BLACKHOLE_COOL_MS;
      target = 1.0f - (k > 1.0f ? 1.0f : k) * (1.0f - BLACKHOLE_COLD_LEVEL);
    }
    if (target >= 1.0f && _temp < 0.9f && _flash < 1.0f - _temp) _flash = 1.0f - _temp;   // 燃え上がる
    const float rate = target > _temp ? 1.5f : 0.5f;                                        // 温まるのは速く
    _temp += (target - _temp) * (dt * rate > 1.0f ? 1.0f : dt * rate);
    _flash -= dt * 1.2f;
    if (_flash < 0) _flash = 0;
  }

  // リング位置 (小数) でのケプラー回転の角速度 [方位角単位 / 秒]
  static float angularSpeed(float ring) {
    int r = (int)ring;
    if (r < 0) r = 0;
    if (r > BH_RINGS - 1) r = BH_RINGS - 1;
    return BH_RING_SPEED[r] * (256.0f * 1000.0f / 65536.0f / BLACKHOLE_PERIOD_MS);
  }

  void updateBlobs(float dt) {
    if (_anyHeat) memset(_heat, 0, sizeof(_heat));
    _anyHeat = false;
    for (int i = 0; i < BLACKHOLE_MAX_BLOBS; ++i) {
      Blob& b = _blobs[i];
      if (!b.active) continue;
      const float depth = 1.0f - b.ring / BH_RINGS;        // 0: 外縁, 1: 内縁
      b.theta = fmodf(b.theta + angularSpeed(b.ring) * dt, 256.0f);
      b.ring -= dt * BLACKHOLE_FALL_SPEED * (0.4f + 1.6f * depth * depth);   // 内側ほど速く落ちる
      if (b.ring < 0) {                                      // 内縁を越えた: 吸い込まれる
        b.active = false;
        _gulp = true;
        if (_flash < 0.8f) _flash = 0.8f;
        continue;
      }
      // 内側ほど方位方向に引き伸ばし、半径方向に細くする (スパゲッティ化)
      splat(b.ring, b.theta, 6.0f - 4.5f * depth, 9.0f + 50.0f * depth * sqrtf(depth), 200 + (int)(55 * depth));
    }
  }

  // (rc, tc) を中心に、半径方向 wr リング / 方位方向 wa の楕円で明るさ amp を置く
  void splat(float rc, float tc, float wr, float wa, int amp) {
    const int r0 = (int)floorf(rc - wr), r1 = (int)ceilf(rc + wr);
    const int a0 = (int)floorf(tc - wa), a1 = (int)ceilf(tc + wa);
    for (int r = r0; r <= r1; ++r) {
      if (r < 0 || r >= BH_RINGS) continue;
      const float fr = (r + 0.5f - rc) / wr;
      for (int a = a0; a <= a1; ++a) {
        const float fa = (a - tc) / wa;
        const float w = 1.0f - fr * fr - fa * fa;
        if (w <= 0) continue;
        const int v = (int)(amp * w);
        uint8_t& h = _heat[r][(uint8_t)a];
        if (v > h) h = (uint8_t)(v > 255 ? 255 : v);
      }
    }
    _anyHeat = true;
  }

  // スプライト上の画素 (sx, sy) の近くで円盤を映す画素を探し、そのリングと方位角を返す
  static bool findDisk(int sx, int sy, int& ring, int& phi) {
    for (int rad = 0; rad <= BLACKHOLE_DROP_SEARCH_PX; rad += 2) {
      for (int y = sy - rad; y <= sy + rad; y += 2) {
        for (int x = sx - rad; x <= sx + rad; x += 2) {
          if (abs(x - sx) != rad && abs(y - sy) != rad) continue;   // 正方形の縁だけ (内側は調べ済み)
          if ((unsigned)x >= (unsigned)BH_SIZE || (unsigned)y >= (unsigned)BH_SIZE) continue;
          const uint32_t want = (uint32_t)(y * BH_SIZE + x);
          int lo = 0, hi = BH_DISK_COUNT - 1;    // BH_DISK は画素番号の昇順
          while (lo <= hi) {
            const int mid = (lo + hi) / 2;
            const uint32_t p = BH_DISK[mid] >> 14;
            if (p == want) { ring = (BH_DISK[mid] >> 8) & 63; phi = BH_DISK[mid] & 255; return true; }
            if (p < want) lo = mid + 1; else hi = mid - 1;
          }
        }
      }
    }
    return false;
  }

  static inline void put(uint16_t* buf, uint32_t idx, int dx, int dy, uint16_t c) {
    const int x = (int)(idx % BH_SIZE) + dx, y = (int)(idx / BH_SIZE) + dy;
    if ((unsigned)x < (unsigned)BH_SIZE && (unsigned)y < (unsigned)BH_SIZE) buf[y * BH_SIZE + x] = c;
  }
};
