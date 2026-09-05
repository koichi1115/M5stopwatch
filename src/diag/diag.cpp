// 診断ファーム (env:diag): 画面ずれ / IMU 軸 / mark描画経路 / 音 を実機で確認するツール
//   pio run -e diag -t upload
//
// モード (BtnB クリックで切替):
//   0 ALIGN : 十字と円。画面をドラッグして縁にぴったり合わせる → 「off X Y」が DISPLAY_OFFSET_X/Y
//             BtnA クリック: 右へ 1px / BtnA 長押し: 下へ 1px / BtnB 長押し: リセット
//   1 MARK AA : 本番と同じ経路 (透過スプライト → pushRotatedWithAA → キャンバス) でmarkを描く。
//             IMU の角度で回る。緑の枠 = markスプライトの境界、白の十字 = 画面中心
//   2 MARK NOAA : 同上だが pushRotateZoom (AA 無し)
//   3 SOUND : マイクの音量バー (青) / 背景フロア (黄線) / 驚きしきい値 (赤線) / 平坦度 / 継続率 / 音楽判定。
//             急な大きい音で「LOUD!」が出る。config.h の SOUND_* を調整するときに使う
// シリアル (115200) に 0.5 秒毎 IMU 生値・補正量・音の値を出力
#include <M5Unified.h>
#include <stdio.h>
#include <math.h>
#include "../config.h"
#include "../mark_renderer.h"
#include "../orientation.h"
#include "../sound.h"

static M5Canvas canvas(&M5.Display);
static M5Canvas markSp(&canvas);
static OrientationTracker orient;
static SoundSensor sound;
static int W, H;
static int offX = DISPLAY_OFFSET_X, offY = DISPLAY_OFFSET_Y;
static int mode = 0;
static const char* MODE_NAMES[] = {"ALIGN", "MARK AA", "MARK NOAA", "SOUND"};
static constexpr int MODE_COUNT = 4;
static uint32_t loudFlashUntil = 0;

static void drawAlign() {
  const int cx = W / 2 + offX, cy = H / 2 + offY;
  canvas.fillScreen(TFT_BLACK);
  canvas.drawCircle(cx, cy, 232, TFT_WHITE);
  canvas.drawCircle(cx, cy, 231, TFT_WHITE);
  canvas.drawCircle(cx, cy, 200, TFT_DARKGREY);
  canvas.drawLine(cx - 232, cy, cx + 232, cy, TFT_WHITE);
  canvas.drawLine(cx, cy - 232, cx, cy + 232, TFT_WHITE);
  canvas.fillCircle(cx + 150, cy, 18, TFT_RED);      // 右 = 赤
  canvas.fillCircle(cx, cy - 150, 18, TFT_GREEN);    // 上 = 緑
  canvas.fillCircle(cx - 150, cy, 18, TFT_BLUE);     // 左 = 青
  canvas.fillCircle(cx, cy + 150, 18, TFT_YELLOW);   // 下 = 黄
  canvas.setTextDatum(middle_center);
  canvas.setFont(&fonts::Font4);
  canvas.setTextColor(TFT_WHITE, TFT_BLACK);
  char buf[48];
  snprintf(buf, sizeof(buf), "off %+d %+d", offX, offY);
  canvas.drawString(buf, cx, cy - 40);
  canvas.setFont(&fonts::Font2);
  canvas.drawString("ALIGN: drag to fit", cx, cy + 40);
  canvas.pushSprite(0, 0);
}

static void drawMark(bool aa) {
  const float cx = W / 2.0f + offX, cy = H / 2.0f + offY;
  const float angle = orient.displayAngle();
  canvas.fillScreen(TFT_BLACK);
  const float c = markSp.width() / 2.0f;
  markSp.fillScreen(COLOR_TRANSPARENT);
  markSp.drawRect(0, 0, markSp.width(), markSp.height(), TFT_GREEN);
  drawBotMark(markSp, (int)c, (int)c, MARK_RADIUS, markAt(DEFAULT_MARK_INDEX));
  canvas.setPivot(cx, cy);
  if (aa) markSp.pushRotatedWithAA(&canvas, angle, COLOR_TRANSPARENT);
  else    markSp.pushRotateZoom(&canvas, cx, cy, angle, 1.0f, 1.0f, COLOR_TRANSPARENT);
  // 画面中心の十字 (白)
  canvas.drawLine(cx - 30, cy, cx + 30, cy, TFT_WHITE);
  canvas.drawLine(cx, cy - 30, cx, cy + 30, TFT_WHITE);
  canvas.setTextDatum(middle_center);
  canvas.setFont(&fonts::Font2);
  canvas.setTextColor(TFT_WHITE, TFT_BLACK);
  char buf[48];
  snprintf(buf, sizeof(buf), "%s  ang %.0f", aa ? "MARK AA" : "MARK NOAA", angle);
  canvas.drawString(buf, cx, cy + 200);
  canvas.pushSprite(0, 0);
}

static void drawSound(uint32_t now) {
  const int cx = W / 2 + offX, cy = H / 2 + offY;
  canvas.fillScreen(TFT_BLACK);
  canvas.setTextDatum(middle_center);
  canvas.setTextColor(TFT_WHITE, TFT_BLACK);
  canvas.setFont(&fonts::Font2);
  char buf[64];

  if (!sound.running()) {
    canvas.setFont(&fonts::Font4);
    canvas.drawString("MIC FAILED", cx, cy);
    canvas.pushSprite(0, 0);
    return;
  }
  // レベルバー: -90..0 dBFS → 幅 360px
  const int x0 = cx - 180, barW = 360;
  auto toX = [&](float db) { if (db < -90) db = -90; if (db > 0) db = 0; return x0 + (int)((db + 90.0f) * (barW / 90.0f)); };
  canvas.drawRect(x0 - 1, cy - 21, barW + 2, 42, TFT_DARKGREY);
  canvas.fillRect(x0, cy - 20, toX(sound.levelDb()) - x0, 40, sound.isMusic() ? TFT_GREEN : (sound.active() ? TFT_CYAN : 0x0339));
  canvas.drawFastVLine(toX(sound.floorDb()), cy - 32, 64, TFT_YELLOW);                               // 背景フロア
  canvas.drawFastVLine(toX(sound.floorDb() + SOUND_LOUD_ABOVE_FLOOR_DB), cy - 32, 64, TFT_RED);      // 驚きしきい値
  canvas.drawFastVLine(toX(SOUND_LOUD_MIN_DB), cy - 26, 52, 0x8000);                                 // 驚きの最低音量
  for (int db = -90; db <= 0; db += 10) canvas.drawFastVLine(toX((float)db), cy + 22, 6, TFT_DARKGREY);

  snprintf(buf, sizeof(buf), "level %5.1f dB   floor %5.1f dB", sound.levelDb(), sound.floorDb());
  canvas.drawString(buf, cx, cy - 60);
  snprintf(buf, sizeof(buf), "flatness %.2f (<%.2f)   active %.2f (>%.2f)", sound.flatness(), SOUND_MUSIC_FLATNESS_MAX,
           sound.activeRatio(), SOUND_MUSIC_ACTIVE_RATIO);
  canvas.drawString(buf, cx, cy + 50);
  canvas.setFont(&fonts::Font4);
  canvas.setTextColor(sound.isMusic() ? TFT_GREEN : TFT_DARKGREY, TFT_BLACK);
  canvas.drawString(sound.isMusic() ? "MUSIC" : "no music", cx, cy + 90);
  if ((int32_t)(now - loudFlashUntil) < 0) {
    canvas.setTextColor(TFT_RED, TFT_BLACK);
    canvas.drawString("LOUD!", cx, cy - 110);
  }
  canvas.setTextColor(TFT_WHITE, TFT_BLACK);
  canvas.setFont(&fonts::Font2);
  canvas.drawString("SOUND", cx, cy - 150);
  canvas.pushSprite(0, 0);
}

void setup() {
  auto cfg = M5.config();
  cfg.internal_imu = true;
  cfg.internal_mic = true;
  cfg.internal_spk = false;
  M5.begin(cfg);
  M5.Display.setRotation(0);
  M5.Display.setBrightness(120);
  W = M5.Display.width();
  H = M5.Display.height();
  canvas.setPsram(true);
  canvas.setColorDepth(16);
  const bool ok = canvas.createSprite(W, H);
  markSp.setPsram(true);
  markSp.setColorDepth(16);
  const bool ok2 = markSp.createSprite(FACE_SIZE, FACE_SIZE);
  markSp.setPivot(markSp.width() / 2.0f, markSp.height() / 2.0f);
  const bool ok3 = sound.begin();
  delay(1500);
  printf("\n[diag] board=%d imu=%d psram=%u display=%dx%d touch=%d canvas=%d mark=%d mic=%d\n",
         (int)M5.getBoard(), (int)M5.Imu.getType(), (unsigned)ESP.getPsramSize(), W, H,
         (int)M5.Touch.isEnabled(), (int)ok, (int)ok2, (int)ok3);
}

void loop() {
  static uint32_t lastLog = 0, lastLoopUs = 0, lastSoundDraw = 0;
  static bool dirty = true, dragging = false;
  static int lastX = 0, lastY = 0;
  M5.update();
  const uint32_t now = millis();
  const uint32_t nowUs = micros();
  float dt = (nowUs - lastLoopUs) * 1e-6f; lastLoopUs = nowUs;
  if (dt > 0.2f) dt = 0.2f;

  float ax = 0, ay = 0, az = 0;
  if (M5.Imu.isEnabled() && M5.Imu.update()) {
    M5.Imu.getAccel(&ax, &ay, &az);
    orient.update(ax, ay, az, dt);
  }
  sound.update(now);
  if (sound.takeLoud()) { loudFlashUntil = now + 700; printf("[diag] LOUD %.0f dB (floor %.0f)\n", sound.levelDb(), sound.floorDb()); }

  // ---- 入力 ----
  if (M5.BtnB.wasHold())    { offX = 0; offY = 0; dirty = true; printf("[diag] offset reset\n"); }
  else if (M5.BtnB.wasClicked()) { mode = (mode + 1) % MODE_COUNT; dirty = true; printf("[diag] mode -> %s\n", MODE_NAMES[mode]); }
  if (M5.BtnA.wasClicked()) { offX += 1; dirty = true; printf("[diag] offset -> x=%+d y=%+d\n", offX, offY); }
  if (M5.BtnA.wasHold())    { offY += 1; dirty = true; printf("[diag] offset -> x=%+d y=%+d\n", offX, offY); }
  const auto& t = M5.Touch.getDetail();
  if (t.isPressed()) {
    if (!dragging) { dragging = true; lastX = t.x; lastY = t.y; }
    else {
      const int dx = t.x - lastX, dy = t.y - lastY;
      if (dx || dy) { offX += dx; offY += dy; lastX = t.x; lastY = t.y; dirty = true; }
    }
  } else if (dragging) {
    dragging = false;
    printf("[diag] offset -> x=%+d y=%+d\n", offX, offY);
  }

  // ---- 描画 ----
  if (mode == 0) { if (dirty) { dirty = false; drawAlign(); } }
  else if (mode == 3) { if (now - lastSoundDraw >= 50) { lastSoundDraw = now; drawSound(now); } dirty = false; }
  else { drawMark(mode == 1); dirty = false; }

  if (now - lastLog > 500) {
    lastLog = now;
    printf("[imu] ax=%+.2f ay=%+.2f az=%+.2f ang=%.1f off=%+d,%+d mode=%s snd=%.0f/%.0f sfm=%.2f act=%.2f music=%d\n",
           ax, ay, az, orient.displayAngle(), offX, offY, MODE_NAMES[mode],
           sound.levelDb(), sound.floorDb(), sound.flatness(), sound.activeRatio(), (int)sound.isMusic());
  }
  delay(mode == 0 ? 10 : (mode == 3 ? 5 : 33));
}
