// M5Stack StopWatch デジタルバッジ
//  - IMU で重力方向を検出し、どう回しても顔が正立する
//  - まっくろくろすけ風の目がキョロキョロ / まばたき / 眠る / 驚く / 喜ぶ
//  - 常時点灯 (AMOLED なので黒背景ならそれなりに省電力)、動きが無いと段階的に省電力へ
//
// 操作 (BtnA = GPIO2, BtnB = GPIO1。物理的にどちらがどれかは実機で確認):
//   BtnA クリック      : ステータス表示 (電池 / 姿勢 / IMU 生値)
//   BtnA ダブルクリック : 回転方向を反転 (傾けたときに顔が逆に回るとき)
//   BtnA 長押し        : 明るさを切り替え
//   BtnB クリック      : 起こす (寝ていたら) / ウインクっぽく驚かせる
//   BtnB ダブルクリック : 自動で寝る機能の ON/OFF
//   BtnB 長押し        : 今の向きを「正立」として記憶 (較正)
//   タップ             : 喜ぶ。触り続けると指を目で追う
//   口元に指を 2 秒    : 口が出てきてかじろうとする (閉じる瞬間にバイブ)
//   急な大きい音       : 驚いてキョロキョロ見回す (寝ていても起きる)
//   音楽               : 目を閉じて音符を流す

#include <Arduino.h>
#include <M5Unified.h>
#include <utility/M5IOE1_Class.hpp>
#include <Preferences.h>
#include <esp_sleep.h>
#include <stdio.h>
#include <driver/rtc_io.h>

#include "config.h"
#include "orientation.h"
#include "face.h"
#include "sound.h"
#include "hid.h"

// ------------------------------------------------------------
static M5Canvas screen(&M5.Display);
static LovyanGFX* out = &screen;   // PSRAM が無いときは M5.Display に直接描く
static Face face;
static OrientationTracker orient;
static MotionDetector motion;
static Preferences prefs;
static SoundSensor sound;

static uint8_t brightnessIndex = DEFAULT_BRIGHTNESS_INDEX;
static bool autoSleepEnabled = true;
static uint32_t statusUntil = 0;
static uint32_t lastFrameMs = 0;
static uint32_t lastLoopUs = 0;
static uint32_t bootMs = 0;
static bool lowClock = false;
static int screenW = 468, screenH = 468;
static uint16_t furSeed = 12345;
static uint32_t sleepEnteredMs = 0;
static uint32_t mouthTouchStart = 0;   // 口元に触れ始めた時刻 (0 = 触れていない)
static uint32_t vibUntil = 0;          // ノンブロッキングのバイブ停止時刻
static bool musicShown = false;
// BLE キーボードとミュートレバー
static BleKeyboardMini kb;
static bool micMuted = false;          // 端末側で覚えている状態 (PC からは返ってこない)
static float leverKnob = 0.0f;         // 表示上のつまみ位置 0 = 上 (MIC) .. 1 = 下 (MUTE)
static bool leverDragging = false;
static float leverDragPos = 0.0f, leverDragStart = 0.0f;

static constexpr gpio_num_t WAKE_BUTTON_1 = GPIO_NUM_1;
static constexpr gpio_num_t WAKE_BUTTON_2 = GPIO_NUM_2;

// ------------------------------------------------------------
static void loadSettings() {
  prefs.begin("badge", false);
  orient.setSign(prefs.getChar("sign", 1));
  orient.setOffset(prefs.getFloat("offset", 0.0f));
  brightnessIndex = prefs.getUChar("bri", DEFAULT_BRIGHTNESS_INDEX);
  if (brightnessIndex >= sizeof(BRIGHTNESS_LEVELS)) brightnessIndex = DEFAULT_BRIGHTNESS_INDEX;
  autoSleepEnabled = prefs.getBool("asleep", true);
}

static void saveOrientation() {
  prefs.putChar("sign", orient.sign());
  prefs.putFloat("offset", orient.offset());
}

static void vibrate(uint8_t level, uint32_t ms) {
  M5.Power.setVibration(level);
  delay(ms);
  M5.Power.setVibration(0);
}

// ノンブロッキングのバイブ (loop 内で止める)
static void buzz(uint8_t level, uint32_t ms, uint32_t now) {
  M5.Power.setVibration(level);
  vibUntil = now + ms;
}

static bool leverVisible() { return HID_ENABLED && kb.connected(); }

// PC にショートカットを送る。未接続なら長めのバイブで知らせる
static void sendHotkey(uint8_t key, uint8_t mods, uint32_t now) {
  motion.touch(now);
  if (!HID_ENABLED) return;
  if (!kb.connected()) {
    buzz(90, 90, now);
    statusUntil = now + 1500;
    puts("[hid] not connected");
    return;
  }
  kb.tap(key, mods);
  buzz(140, 25, now);
  printf("[hid] key 0x%02X mods 0x%02X\n", key, mods);
}

static void applyBrightness() {
  uint8_t b = BRIGHTNESS_LEVELS[brightnessIndex];
  if (face.isSleeping()) b = SLEEP_BRIGHTNESS;
  else if (face.isDrowsy()) b = (uint8_t)((uint32_t)b * DROWSY_BRIGHTNESS_SCALE_PERCENT / 100);
  if (b < 4) b = 4;
  if (M5.Display.getBrightness() != b) M5.Display.setBrightness(b);
}

static void setLowClock(bool enable) {
  if (!LOW_CPU_CLOCK_WHILE_SLEEPING || enable == lowClock) return;
  lowClock = enable;
  setCpuFrequencyMhz(enable ? 80 : 240);
}

// ------------------------------------------------------------
// 深い眠り: 画面を消して deep sleep。ボタン (GPIO1/2) または定期タイマで起きる。
static void enterDeepSleep() {
  puts("[power] entering deep sleep");
  sound.end();
  kb.end();
  M5.Display.sleep();            // 輝度 0 (フレームバッファ経由だと sleep-in は届かない)
  M5.Display.waitDisplay();
  M5.Imu.sleep();
  if (M5.getBoard() == m5::board_t::board_M5StopWatch) {
    // M5IOE1: IO5 = OLED RST, IO4 = TP RST (M5GFX の初期化手順より)。
    // リセットを assert したまま眠り、起動時の M5GFX 自動検出で解除・再初期化される。
    auto& ioe = M5.getIOExpander(0);
    ioe.digitalWrite(m5::M5IOE1_Class::gpio5, false);
    ioe.digitalWrite(m5::M5IOE1_Class::gpio4, false);
    delay(5);
  }

  const uint64_t mask = (1ULL << WAKE_BUTTON_1) | (1ULL << WAKE_BUTTON_2);
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
  for (gpio_num_t pin : {WAKE_BUTTON_1, WAKE_BUTTON_2}) {
    rtc_gpio_init(pin);
    rtc_gpio_set_direction(pin, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pulldown_dis(pin);
    rtc_gpio_pullup_en(pin);
  }
  esp_sleep_enable_ext1_wakeup(mask, ESP_EXT1_WAKEUP_ANY_LOW);
  if (DEEP_SLEEP_MOTION_CHECK_SEC > 0) {
    esp_sleep_enable_timer_wakeup((uint64_t)DEEP_SLEEP_MOTION_CHECK_SEC * 1000000ULL);
  }
  esp_deep_sleep_start();
}

// タイマ起床時: 少しの間 IMU を見て、動いていなければそのまま寝直す
static void motionCheckAfterTimerWake() {
  if (!M5.Imu.isEnabled()) return;
  const uint32_t start = millis();
  bool moved = false;
  float ax, ay, az, gx, gy, gz;
  while (millis() - start < DEEP_SLEEP_MOTION_CHECK_WINDOW_MS) {
    if (M5.Imu.update()) {
      M5.Imu.getAccel(&ax, &ay, &az);
      M5.Imu.getGyro(&gx, &gy, &gz);
      motion.update(ax, ay, az, gx, gy, gz, millis());
      if (millis() - start > 200 && motion.lastMotionMs() > start + 200) { moved = true; break; }
    }
    delay(5);
  }
  printf("[power] timer wake, moved=%d\n", moved);
  if (!moved) enterDeepSleep();
}

// ------------------------------------------------------------
static void drawFur(float angleDeg) {
  // 画面の縁に生える毛。顔と一緒に回す。
  const float cx = screenW / 2.0f + DISPLAY_OFFSET_X, cy = screenH / 2.0f + DISPLAY_OFFSET_Y;
  const float R = (screenW < screenH ? screenW : screenH) / 2.0f + 2.0f;
  uint32_t seed = furSeed;
  auto rnd = [&seed]() { seed = seed * 1103515245u + 12345u; return (seed >> 16) & 0x7FFF; };
  const int hairs = FUR_COUNT;
  for (int i = 0; i < hairs; ++i) {
    const float a = (angleDeg + i * (360.0f / hairs) + (rnd() % 30) / 10.0f) * 0.01745329f;
    const float len = FUR_MIN_LEN + (rnd() % (FUR_MAX_LEN - FUR_MIN_LEN + 1));
    const float w = 1.4f + (rnd() % 10) / 6.0f;
    const float x0 = cx + cosf(a) * R, y0 = cy + sinf(a) * R;
    const float x1 = cx + cosf(a) * (R - len), y1 = cy + sinf(a) * (R - len);
    out->drawWideLine(x0, y0, x1, y1, w, COLOR_FUR);
    out->fillCircle((int)x1, (int)y1, (int)(w * 0.9f), COLOR_FUR_TIP);
  }
}

// ミュートレバー (フェーダー風)。顔スプライトに描くので顔と一緒に回る。上 = MIC ON, 下 = MUTE
static void drawLever() {
  M5Canvas& sp = face.sprite();
  const float c = sp.width() / 2.0f;
  const float x = c + LEVER_X, top = c - LEVER_HALF_LEN, bot = c + LEVER_HALF_LEN;
  const float target = leverDragging ? leverDragPos : (micMuted ? 1.0f : 0.0f);
  leverKnob += (target - leverKnob) * (leverDragging ? 1.0f : 0.25f);
  const uint16_t col = micMuted ? COLOR_LEVER_MUTE : COLOR_LEVER_ON;

  sp.fillRoundRect((int)(x - 7), (int)(top - 8), 14, (int)(bot - top + 16), 7, COLOR_LEVER_TRACK);
  for (int k = 0; k <= 4; ++k) {
    const int y = (int)(top + (bot - top) * k / 4);
    sp.drawFastHLine((int)(x - 17), y, 8, COLOR_UI);
    sp.drawFastHLine((int)(x + 9), y, 8, COLOR_UI);
  }
  const float ky = top + (bot - top) * leverKnob;
  sp.fillCircle((int)x, (int)ky, LEVER_KNOB_R, col);
  sp.fillCircle((int)x, (int)ky, LEVER_KNOB_R - 6, 0x0000);
  sp.fillCircle((int)x, (int)ky, LEVER_KNOB_R - 10, col);
  sp.drawFastHLine((int)(x - LEVER_KNOB_R + 4), (int)ky, 2 * LEVER_KNOB_R - 8, 0x0000);

  sp.setFont(&fonts::Font2);
  sp.setTextSize(1);
  sp.setTextDatum(middle_center);
  sp.setTextColor(micMuted ? COLOR_UI : COLOR_LEVER_ON);
  sp.drawString("MIC", (int)x, (int)(top - 24));
  sp.setTextColor(micMuted ? COLOR_LEVER_MUTE : COLOR_UI);
  sp.drawString("MUTE", (int)x, (int)(bot + 24));
}

static void drawStatus(uint32_t now) {
  M5Canvas& sp = face.sprite();
  const int w = sp.width();
  sp.setTextDatum(top_left);
  sp.setFont(&fonts::Font2);
  sp.setTextSize(1);
  sp.setTextColor(COLOR_UI);

  const int32_t bat = M5.Power.getBatteryLevel();
  const auto chg = M5.Power.isCharging();
  const char* chgs = (chg == m5::Power_Class::is_charging) ? "CHG" : (chg == m5::Power_Class::is_discharging ? "BAT" : "??");
  char line[64];
  int y = 8;
  const int x = w / 2 - 110;
  snprintf(line, sizeof(line), "%s %ld%%  %dmV", chgs, (long)bat, (int)M5.Power.getBatteryVoltage());
  sp.drawString(line, x, y); y += 16;
  snprintf(line, sizeof(line), "ANG %6.1f  raw %6.1f  %s", orient.displayAngle(), orient.rawAngle(), orient.reliable() ? "ok" : "flat");
  sp.drawString(line, x, y); y += 16;
  snprintf(line, sizeof(line), "sign %+d  offset %6.1f", orient.sign(), orient.offset());
  sp.drawString(line, x, y); y += 16;
  snprintf(line, sizeof(line), "gx %+5.2f gy %+5.2f |a| %4.2f", orient.screenGx(), orient.screenGy(), orient.magnitudeG());
  sp.drawString(line, x, y); y += 16;
  snprintf(line, sizeof(line), "bri %d/%d  autosleep %s  idle %lus", brightnessIndex + 1, (int)sizeof(BRIGHTNESS_LEVELS),
           autoSleepEnabled ? "on" : "off", (unsigned long)(motion.idleMs(now) / 1000));
  sp.drawString(line, x, y); y += 16;
  snprintf(line, sizeof(line), "up %lus  imu %s  fps %d", (unsigned long)((now - bootMs) / 1000),
           M5.Imu.isEnabled() ? "ok" : "NONE", (int)(1000 / (FRAME_INTERVAL_AWAKE_MS ? FRAME_INTERVAL_AWAKE_MS : 1)));
  sp.drawString(line, x, y); y += 16;
  snprintf(line, sizeof(line), "snd %4.0fdB fl %4.0f sfm %.2f act %.2f%s", sound.levelDb(), sound.floorDb(),
           sound.flatness(), sound.activeRatio(), sound.isMusic() ? " MUSIC" : "");
  sp.drawString(line, x, y); y += 16;
  snprintf(line, sizeof(line), "ble %s  mic %s", !HID_ENABLED ? "off" : (kb.connected() ? "connected" : "advertising"),
           micMuted ? "MUTED" : "on");
  sp.drawString(line, x, y);

  // 電池アーク (下部, 左から右へ伸びる)
  if (bat >= 0) {
    const float cx = w / 2.0f, cy = w / 2.0f;
    const float r = w / 2.0f - 8;
    const uint16_t col = bat < 20 ? TFT_RED : COLOR_UI_ACCENT;
    const int segs = 40;
    const float a0 = 150.0f, a1 = 150.0f - 120.0f * bat / 100.0f;
    float px = 0, py = 0;
    for (int k = 0; k <= segs; ++k) {
      const float t = (a0 + (a1 - a0) * k / segs) * 0.01745329f;
      const float xx = cx + r * cosf(t), yy = cy + r * sinf(t);
      if (k) sp.drawWideLine(px, py, xx, yy, 3.0f, col);
      px = xx; py = yy;
    }
  }
}

static void render(uint32_t now) {
  const float angle = orient.displayAngle();
  face.draw();
  if (leverVisible()) drawLever();
  if ((int32_t)(now - statusUntil) < 0) drawStatus(now);

  if (out != &screen) M5.Display.startWrite();
  out->fillScreen(TFT_BLACK);
  drawFur(angle);
  if (FACE_ANTIALIAS) face.sprite().pushRotatedWithAA(out, angle, COLOR_TRANSPARENT);
  else face.sprite().pushRotateZoom(out, screenW / 2.0f + DISPLAY_OFFSET_X, screenH / 2.0f + DISPLAY_OFFSET_Y, angle, 1.0f, 1.0f, COLOR_TRANSPARENT);
  if (out == &screen) screen.pushSprite(0, 0);
  else M5.Display.endWrite();
}

// ------------------------------------------------------------
static void handleButtons(uint32_t now) {
  // ---- BtnA ----
  if (M5.BtnA.wasDoubleClicked()) {
    orient.flipSign();
    saveOrientation();
    statusUntil = now + STATUS_OVERLAY_MS;
    vibrate(150, 60);
    printf("[cfg] rotation sign -> %+d\n", orient.sign());
  } else if (M5.BtnA.wasHold()) {
    brightnessIndex = (brightnessIndex + 1) % sizeof(BRIGHTNESS_LEVELS);
    prefs.putUChar("bri", brightnessIndex);
    statusUntil = now + 1500;
    vibrate(120, 40);
    printf("[cfg] brightness -> %d\n", BRIGHTNESS_LEVELS[brightnessIndex]);
  } else if (M5.BtnA.wasClicked()) {
    sendHotkey(HID_BTN_A_KEY, HID_BTN_A_MODS, now);   // Alt+Tab
  }

  // ---- BtnB ----
  if (M5.BtnB.wasDoubleClicked()) {
    autoSleepEnabled = !autoSleepEnabled;
    prefs.putBool("asleep", autoSleepEnabled);
    statusUntil = now + STATUS_OVERLAY_MS;
    vibrate(150, 60);
    printf("[cfg] autosleep -> %d\n", autoSleepEnabled);
  } else if (M5.BtnB.wasHold()) {
    if (orient.calibrate()) {
      saveOrientation();
      vibrate(200, 80); delay(80); vibrate(200, 80);
      printf("[cfg] calibrated: offset=%.1f sign=%+d\n", orient.offset(), orient.sign());
    } else {
      vibrate(90, 250);
      puts("[cfg] calibration failed: hold the device upright (screen vertical)");
    }
    statusUntil = now + STATUS_OVERLAY_MS;
  } else if (M5.BtnB.wasClicked()) {
    sendHotkey(HID_BTN_B_KEY, HID_BTN_B_MODS, now);   // 無変換
  }
}

// ------------------------------------------------------------
void setup() {
  auto cfg = M5.config();
  cfg.internal_imu = true;
  cfg.internal_rtc = true;
  cfg.internal_mic = SOUND_ENABLED;
  cfg.internal_spk = false;   // スピーカーはマイクと I2S ピンを共有するので使わない
  cfg.clear_display = true;
  M5.begin(cfg);
  bootMs = millis();

  printf("\n[boot] M5Unified board=%d imu=%d psram=%u\n", (int)M5.getBoard(), (int)M5.Imu.getType(), (unsigned)ESP.getPsramSize());

  M5.BtnA.setHoldThresh(BUTTON_HOLD_MS);
  M5.BtnB.setHoldThresh(BUTTON_HOLD_MS);
  loadSettings();

  const auto cause = esp_sleep_get_wakeup_cause();
  if (cause == ESP_SLEEP_WAKEUP_TIMER) {
    M5.Display.setBrightness(0);
    motionCheckAfterTimerWake();   // 動いていなければ戻ってこない
  }
  if (SOUND_ENABLED) printf("[boot] mic %s\n", sound.begin() ? "ok" : "FAILED");
  if (HID_ENABLED) printf("[boot] ble hid \"%s\" %s\n", HID_DEVICE_NAME, kb.begin(HID_DEVICE_NAME) ? "advertising" : "FAILED");

  M5.Display.setRotation(0);
  screenW = M5.Display.width();
  screenH = M5.Display.height();
  screen.setPsram(true);
  screen.setColorDepth(16);
  if (!screen.createSprite(screenW, screenH)) {
    puts("[boot] screen sprite alloc failed (PSRAM?) - drawing directly");
    out = &M5.Display;
  }
  out->setPivot(screenW / 2.0f + DISPLAY_OFFSET_X, screenH / 2.0f + DISPLAY_OFFSET_Y);
  if (!face.begin()) {
    puts("[boot] face sprite alloc failed");
  }
  furSeed = (uint16_t)esp_random();
  randomSeed(esp_random());

  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setBrightness(BRIGHTNESS_LEVELS[brightnessIndex]);
  motion.touch(millis());
  if (VIBRATE_ON_BOOT && cause != ESP_SLEEP_WAKEUP_TIMER) vibrate(160, 70);
  lastLoopUs = micros();
  printf("[boot] display %dx%d, sign=%+d offset=%.1f\n", screenW, screenH, orient.sign(), orient.offset());
}

void loop() {
  M5.update();
  const uint32_t now = millis();
  const uint32_t nowUs = micros();
  float dt = (nowUs - lastLoopUs) * 1e-6f;
  lastLoopUs = nowUs;
  if (dt > 0.2f) dt = 0.2f;

  // ---- IMU ----
  if (M5.Imu.isEnabled() && M5.Imu.update()) {
    float ax, ay, az, gx, gy, gz;
    M5.Imu.getAccel(&ax, &ay, &az);
    M5.Imu.getGyro(&gx, &gy, &gz);
    orient.update(ax, ay, az, dt);
    motion.update(ax, ay, az, gx, gy, gz, now);
  }

  // ---- 音 ----
  sound.update(now);
  if (vibUntil && (int32_t)(now - vibUntil) >= 0) { M5.Power.setVibration(0); vibUntil = 0; }

  // ---- BLE 接続状態 ----
  if (kb.takeConnectionChange()) {
    statusUntil = now + STATUS_OVERLAY_MS;
    buzz(150, 60, now);
    motion.touch(now);
    printf("[hid] %s\n", kb.connected() ? "connected" : "disconnected");
  }

  // ---- 入力 ----
  handleButtons(now);

  FaceInput in{};
  in.dt = dt;
  in.now = now;
  in.shaken = motion.takeShake();
  in.autoSleepEnabled = autoSleepEnabled;
  in.loudNoise = sound.takeLoud();
  in.music = sound.isMusic();
  if (in.loudNoise) {
    motion.touch(now);
    buzz(120, 35, now);
    printf("[snd] loud! %.0f dB (floor %.0f)\n", sound.levelDb(), sound.floorDb());
  }
  if (in.music) motion.touch(now);
  if (in.music != musicShown) {
    musicShown = in.music;
    printf("[snd] music %s (active %.2f flatness %.2f)\n", in.music ? "on" : "off", sound.activeRatio(), sound.flatness());
  }
  {  // 調整用: 音がしている間だけ 1 秒毎に値を出す
    static uint32_t lastSndLog = 0;
    if ((sound.active() || sound.activeRatio() > 0.05f) && now - lastSndLog >= 1000) {
      lastSndLog = now;
      printf("[snd] level %.0f floor %.0f sfm %.2f act %.2f std %.1f dc %.0f peak %d pk %.0fHz(%.0f%%) %.0fHz(%.0f%%) %.0fHz(%.0f%%)%s\n",
             sound.levelDb(), sound.floorDb(), sound.flatness(), sound.activeRatio(), sound.levelStd(), sound.dc(), sound.peak(),
             sound.peakHz(0), sound.peakShare(0) * 100, sound.peakHz(1), sound.peakShare(1) * 100, sound.peakHz(2), sound.peakShare(2) * 100,
             in.music ? " MUSIC" : "");
    }
  }

  bool pressed = false;
  if (M5.Touch.isEnabled() && M5.Touch.getCount() > 0) {
    const auto& t = M5.Touch.getDetail(0);
    // タッチ座標 → 顔ローカル座標 (顔の回転を打ち消す)
    const float a = -orient.displayAngle() * OrientationTracker::kDegToRad;
    const float dx = t.x - (screenW / 2.0f + DISPLAY_OFFSET_X), dy = t.y - (screenH / 2.0f + DISPLAY_OFFSET_Y);
    const float lx = dx * cosf(a) - dy * sinf(a);
    const float ly = dx * sinf(a) + dy * cosf(a);
    const bool onLever = leverVisible() && fabsf(lx - LEVER_X) < LEVER_ZONE_HALF_W && fabsf(ly) < LEVER_HALF_LEN + LEVER_KNOB_R;

    if (t.wasClicked() && !onLever && !leverDragging) in.tapped = true;
    if (t.isPressed()) {
      pressed = true;
      motion.touch(now);
      if (onLever || leverDragging) {
        // ---- ミュートレバーをドラッグ中 (顔は反応しない) ----
        const float pos = (ly + LEVER_HALF_LEN) / (2.0f * LEVER_HALF_LEN);
        leverDragPos = pos < 0 ? 0 : (pos > 1 ? 1 : pos);
        if (!leverDragging) { leverDragging = true; leverDragStart = leverDragPos; }
        mouthTouchStart = 0;
      } else {
        in.touching = true;
        const float R = screenW / 2.0f * 0.75f;
        in.touchGazeX = lx / R;
        in.touchGazeY = ly / R;
        // 口元 (顔ローカル座標) に触れ続けているか
        const bool inMouthZone = fabsf(lx) < MOUTH_ZONE_HALF_W && ly > MOUTH_ZONE_TOP && ly < MOUTH_ZONE_BOTTOM;
        if (inMouthZone) {
          if (mouthTouchStart == 0) mouthTouchStart = now ? now : 1;
          in.mouthTouchMs = now - mouthTouchStart;
        } else {
          mouthTouchStart = 0;
        }
      }
    }
  }
  if (!in.touching) mouthTouchStart = 0;
  // ---- レバーを離した: 少ししか動かしていなければトグル、動かしていれば位置で決める ----
  if (leverDragging && !pressed) {
    leverDragging = false;
    const bool moved = fabsf(leverDragPos - leverDragStart) > 0.15f;
    const bool newMuted = moved ? (leverDragPos > 0.5f) : !micMuted;
    if (newMuted != micMuted) {
      micMuted = newMuted;
      sendHotkey(HID_MUTE_KEY, HID_MUTE_MODS, now);
      buzz(200, micMuted ? 120 : 40, now);   // ミュートは長め、解除は短め
      printf("[hid] mic %s\n", micMuted ? "MUTED" : "on");
    }
  }
  if (in.tapped) motion.touch(now);
  in.idleMs = motion.idleMs(now);
  in.moving = in.idleMs < 300;

  face.update(in);
  if (face.takeChomp()) buzz(200, 50, now);   // かじった

  // ---- 電源まわり ----
  applyBrightness();
  setLowClock(face.isSleeping());
  if (face.isSleeping()) {
    if (sleepEnteredMs == 0) sleepEnteredMs = now;
    if (DEEP_SLEEP_AFTER_MS > 0 && autoSleepEnabled && (now - sleepEnteredMs) >= DEEP_SLEEP_AFTER_MS && (int32_t)(now - statusUntil) >= 0) {
      enterDeepSleep();
    }
  } else {
    sleepEnteredMs = 0;
  }

  // ---- 描画 ----
  const uint32_t interval = face.isSleeping() ? FRAME_INTERVAL_SLEEP_MS : (face.isDrowsy() ? FRAME_INTERVAL_DROWSY_MS : FRAME_INTERVAL_AWAKE_MS);
  if (now - lastFrameMs >= interval) {
    lastFrameMs = now;
    render(now);
  } else {
    delay(1);
  }

  // ---- ログ (ステータス表示中は 2Hz で姿勢を吐く) ----
  static uint32_t lastLog = 0;
  if ((int32_t)(now - statusUntil) < 0 && now - lastLog > 500) {
    lastLog = now;
    printf("[imu] gx=%+.2f gy=%+.2f |a|=%.2f angle=%.1f mood=%d idle=%lu snd=%.0f/%.0f sfm=%.2f act=%.2f\n",
                  orient.screenGx(), orient.screenGy(), orient.magnitudeG(), orient.displayAngle(), (int)face.mood(),
                  (unsigned long)in.idleMs, sound.levelDb(), sound.floorDb(), sound.flatness(), sound.activeRatio());
  }
}
