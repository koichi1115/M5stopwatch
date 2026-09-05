// M5Stack StopWatch digital badge
//  - Keeps a local geometric Bot mark upright using the IMU
//  - Cycles marks with the existing BtnA+BtnB controls
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
#include "pass.h"

#include <nvs.h>
#include <esp_system.h>
#include <Wire.h>

// ループタスクのスタック (既定 8KB)。JSON / zip / QR デコードで深くなるので広げる
SET_LOOP_TASK_STACK_SIZE(16 * 1024);

// ------------------------------------------------------------
// M5GFX は起動時に I2C を探って基板を判定し、結果を NVS ("M5GFX"/"AUTODETECT") に保存する。
// タッチ (0x15) が一瞬応答しないと PaperMono と誤認識してそれを保存してしまい、以後ずっと画面が出なくなる
// (実際に起きた)。起動前にキャッシュを検査し、起動後に基板が違えばキャッシュを消して再起動する。
RTC_DATA_ATTR static uint8_t boardRetry = 0;

static void clearBoardCache() {
  nvs_handle_t h;
  if (nvs_open("M5GFX", NVS_READWRITE, &h) == ESP_OK) {
    nvs_erase_key(h, "AUTODETECT");
    nvs_commit(h);
    nvs_close(h);
  }
}

// M5IOE1 (I2C 0x4F) の IO4 = TP RST, IO5 = OLED RST を HIGH にしてリセットを解除する。
// この IO エキスパンダは ESP をリセットしても状態を保つので、一度 LOW にされると (以前の deep sleep 実装が
// そうしていた) タッチが応答せず、M5GFX の自動検出が「タッチ無し = PaperMono」になり続ける。
// 診断用: IOE1 のレジスタ 0x00..0x27 をダンプ (Wire を開いた状態で呼ぶ)
static void dumpIoe1(const char* tag) {
  printf("[ioe1] %s:", tag);
  for (uint8_t reg = 0; reg < 0x28; ++reg) {
    Wire.beginTransmission(0x4F); Wire.write(reg);
    if (Wire.endTransmission(false) == 0 && Wire.requestFrom(0x4F, 1) == 1) printf(" %02X", Wire.read());
    else printf(" --");
  }
  printf("\n");
}

static void releaseTouchAndOledReset() {
  Wire.begin(47, 48, 100000);
  dumpIoe1("before");
  Wire.beginTransmission(0x4F); Wire.write(0x23); Wire.write(0x00); Wire.endTransmission();   // I2C_CFG: idle sleep off (M5GFX と同じ)
  // GPIO_PU_L (0x09): 以前の版が誤って 0 にしていた。オープンドレイン (DRV=1) のピンにはプルアップが要るので DRV と同じビットを立てる
  {
    Wire.beginTransmission(0x4F); Wire.write(0x13);
    if (Wire.endTransmission(false) == 0 && Wire.requestFrom(0x4F, 1) == 1) {
      const uint8_t drv = Wire.read();
      Wire.beginTransmission(0x4F); Wire.write(0x09);
      if (Wire.endTransmission(false) == 0 && Wire.requestFrom(0x4F, 1) == 1) {
        const uint8_t pu = Wire.read();
        if ((pu & drv) != drv) {
          Wire.beginTransmission(0x4F); Wire.write(0x09); Wire.write((uint8_t)(pu | drv)); Wire.endTransmission();
          printf("[boot] IOE1 PU_L %02X -> %02X (open-drain pins %02X)\n", pu, pu | drv, drv);
        }
      }
    }
  }
  Wire.beginTransmission(0x4F); Wire.write(0x05);                                               // GPIO_OUT_L
  if (Wire.endTransmission(false) == 0 && Wire.requestFrom(0x4F, 1) == 1) {
    const uint8_t out = Wire.read();
    if ((out & 0b00011000) != 0b00011000) {
      Wire.beginTransmission(0x4F); Wire.write(0x05); Wire.write((uint8_t)(out | 0b00011000)); Wire.endTransmission();
      printf("[boot] IOE1 GPIO_OUT was 0x%02X - released TP/OLED reset\n", out);
      delay(80);   // タッチコントローラの起動待ち
    }
  } else {
    puts("[boot] IOE1 not reachable before begin (ok if first boot)");
  }
  Wire.end();
}

static void checkBoardCacheBeforeBegin() {
  releaseTouchAndOledReset();
  nvs_handle_t h;
  if (nvs_open("M5GFX", NVS_READONLY, &h) != ESP_OK) return;
  uint32_t b = 0;
  const bool have = nvs_get_u32(h, "AUTODETECT", &b) == ESP_OK;
  nvs_close(h);
  if (have && b != (uint32_t)m5::board_t::board_M5StopWatch) {
    printf("[boot] board cache is %u (not StopWatch) - clearing\n", (unsigned)b);
    clearBoardCache();
  }
}

static void checkBoardAfterBegin() {
  if (M5.getBoard() == m5::board_t::board_M5StopWatch) { boardRetry = 0; return; }
  printf("[boot] wrong board detected (%d). clearing cache, retry %d\n", (int)M5.getBoard(), boardRetry);
  clearBoardCache();
  if (boardRetry < 3) {
    boardRetry++;
    delay(300);
    esp_restart();
  }
}

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
static uint8_t markIndex = DEFAULT_MARK_INDEX;
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
static bool leverCancel = false;       // スワイプでモード切替したときレバー操作を無効にする
// 会議モード
static bool meetingMode = false;
static uint32_t meetingToggleFlashUntil = 0;
// チケット (QR) モード
static PassManager pass;
static bool passMode = false;
static uint32_t passHoldStart = 0;     // QR 長押し (削除) の開始時刻
static bool passHoldDone = false;

// レバーの形 (顔ローカル座標)。通常 = 顔の右に縦、会議モード = 中央に横
struct LeverGeom { float x, y; bool vertical; float halfLen, knobR, zoneHalfW; };
static LeverGeom leverGeom() {
  if (meetingMode) return {0.0f, 0.0f, false, (float)MEET_LEVER_HALF_LEN, (float)MEET_LEVER_KNOB_R, (float)MEET_LEVER_ZONE_HALF_W};
  return {(float)LEVER_X, 0.0f, true, (float)LEVER_HALF_LEN, (float)LEVER_KNOB_R, (float)LEVER_ZONE_HALF_W};
}

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
  meetingMode = prefs.getBool("meet", false);
  markIndex = normalizeMarkIndex(prefs.getUChar("mark", DEFAULT_MARK_INDEX));
}

static void saveOrientation() {
  prefs.putChar("sign", orient.sign());
  prefs.putFloat("offset", orient.offset());
}

static void vibrate(uint8_t level, uint32_t ms) {
  M5.Power.setVibration(level);
  delay(ms);
  M5.Power.setVibration(0);
  sound.suppressLoudUntil(millis() + SOUND_SUPPRESS_AFTER_VIBRATION_MS);
}

// ノンブロッキングのバイブ (loop 内で止める)
static void buzz(uint8_t level, uint32_t ms, uint32_t now) {
  if (meetingMode && MEET_SILENT) return;   // 会議中はバイブ音がマイクに乗るので鳴らさない
  M5.Power.setVibration(level);
  vibUntil = now + ms;
  sound.suppressLoudUntil(now + ms + SOUND_SUPPRESS_AFTER_VIBRATION_MS);   // 自分の振動音で驚かない
}

static bool leverVisible() { return HID_ENABLED && meetingMode; }   // ミュートトグルは会議モードだけ

// 会議モードの切替。入るとマイク (音センサ) を止め、顔のリアクションとバイブを止める
static void setMeetingMode(bool on, uint32_t now) {
  if (on == meetingMode) return;
  if (!on) { meetingMode = false; buzz(120, 40, now); }       // 戻るときは短く震える (会議モードの外で鳴る)
  else { buzz(120, 40, now); meetingMode = true; }             // 入るときも 1 回だけ (この時点ではまだ通常モード)
  prefs.putBool("meet", meetingMode);
  if (meetingMode) { sound.end(); M5.Power.setVibration(0); vibUntil = 0; }
  else if (SOUND_ENABLED) sound.begin();
  leverDragging = false;
  statusUntil = now + 1500;
  motion.touch(now);
  printf("[mode] meeting %s\n", meetingMode ? "ON" : "off");
}

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
  if (passMode) b = PASS_BRIGHTNESS;            // QR はスキャナ向けに最大
  else if (meetingMode) b = (uint8_t)((uint32_t)b * MEET_BRIGHTNESS_PERCENT / 100);
  else if (face.isSleeping()) b = SLEEP_BRIGHTNESS;
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
  pass.stopReceive();
  M5.Display.sleep();            // 輝度 0 (フレームバッファ経由だと sleep-in は届かない)
  M5.Display.waitDisplay();
  M5.Imu.sleep();
  if (M5.getBoard() == m5::board_t::board_M5StopWatch) {
    // M5IOE1: IO5 = OLED RST。リセットを assert したまま眠り、起動時の M5GFX 初期化で解除される。
    // IO4 = TP RST は触らない: 復帰直後の M5GFX 自動検出はタッチ (I2C 0x15) の応答で基板を判定するので、
    // リセット中だと PaperMono と誤認識して NVS に保存してしまう。
    auto& ioe = M5.getIOExpander(0);
    ioe.digitalWrite(m5::M5IOE1_Class::gpio5, false);
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

// ミュートレバー (フェーダー風)。顔スプライトに描くので顔と一緒に回る。上 = MIC ON, 下 = MUTE
static void drawLever() {
  M5Canvas& sp = face.sprite();
  const float c = sp.width() / 2.0f;
  const LeverGeom g = leverGeom();
  const float cx = c + g.x, cy = c + g.y;
  const float target = leverDragging ? leverDragPos : (micMuted ? 1.0f : 0.0f);
  leverKnob += (target - leverKnob) * (leverDragging ? 1.0f : 0.25f);
  const uint16_t col = micMuted ? COLOR_LEVER_MUTE : COLOR_LEVER_ON;
  const int kr = (int)g.knobR;
  const int tw = kr / 3;   // トラックの太さ

  // トラックと目盛り
  if (g.vertical) sp.fillRoundRect((int)(cx - tw / 2), (int)(cy - g.halfLen - tw / 2), tw, (int)(2 * g.halfLen + tw), tw / 2, COLOR_LEVER_TRACK);
  else            sp.fillRoundRect((int)(cx - g.halfLen - tw / 2), (int)(cy - tw / 2), (int)(2 * g.halfLen + tw), tw, tw / 2, COLOR_LEVER_TRACK);
  for (int k = 0; k <= 4; ++k) {
    const float p = -g.halfLen + 2 * g.halfLen * k / 4;
    if (g.vertical) { sp.drawFastHLine((int)(cx - kr + 5), (int)(cy + p), 8, COLOR_UI); sp.drawFastHLine((int)(cx + kr - 13), (int)(cy + p), 8, COLOR_UI); }
    else            { sp.drawFastVLine((int)(cx + p), (int)(cy - kr + 5), 8, COLOR_UI); sp.drawFastVLine((int)(cx + p), (int)(cy + kr - 13), 8, COLOR_UI); }
  }
  // つまみ
  const float p = -g.halfLen + 2 * g.halfLen * leverKnob;
  const float kx = g.vertical ? cx : cx + p, ky = g.vertical ? cy + p : cy;
  sp.fillCircle((int)kx, (int)ky, kr, col);
  sp.fillCircle((int)kx, (int)ky, kr - 6, 0x0000);
  sp.fillCircle((int)kx, (int)ky, kr - 10, col);
  if (g.vertical) sp.drawFastHLine((int)(kx - kr + 4), (int)ky, 2 * kr - 8, 0x0000);
  else            sp.drawFastVLine((int)kx, (int)(ky - kr + 4), 2 * kr - 8, 0x0000);

  // ラベル
  sp.setFont(&fonts::Font2);
  sp.setTextSize(1);
  sp.setTextDatum(middle_center);
  sp.setTextColor(micMuted ? COLOR_UI : COLOR_LEVER_ON);
  if (g.vertical) sp.drawString("MIC", (int)cx, (int)(cy - g.halfLen - 24));
  else            sp.drawString("MIC", (int)(cx - g.halfLen - 34), (int)cy);
  sp.setTextColor(micMuted ? COLOR_LEVER_MUTE : COLOR_UI);
  if (g.vertical) sp.drawString("MUTE", (int)cx, (int)(cy + g.halfLen + 24));
  else            sp.drawString("MUTE", (int)(cx + g.halfLen + 38), (int)cy);
}

// 会議モードの画面: 閉じた目 (静か) + 大きな横型ミュートトグル + 状態
static void drawMeeting(uint32_t now) {
  M5Canvas& sp = face.sprite();
  const int c = sp.width() / 2;
  sp.fillScreen(COLOR_TRANSPARENT);

  drawBotMark(sp, c, c - 118, 36, markAt(markIndex));

  sp.setFont(&fonts::Font2);
  sp.setTextSize(1);
  sp.setTextDatum(middle_center);
  sp.setTextColor(COLOR_UI);
  sp.drawString("MEETING  (swipe up to exit)", c, c - 62);

  drawLever();

  // 状態 (大きく)
  const bool flash = (int32_t)(now - meetingToggleFlashUntil) < 0;
  sp.setFont(&fonts::Font4);
  sp.setTextColor(micMuted ? COLOR_LEVER_MUTE : COLOR_LEVER_ON);
  sp.drawString(micMuted ? "MUTED" : "MIC ON", c, c + 82);
  if (flash) sp.drawRoundRect(c - 80, c + 64, 160, 36, 8, micMuted ? COLOR_LEVER_MUTE : COLOR_LEVER_ON);

  // 接続と電池
  char line[48];
  const int32_t bat = M5.Power.getBatteryLevel();
  snprintf(line, sizeof(line), "%s   bat %ld%%", kb.connected() ? "PC connected" : "PC not connected", (long)bat);
  sp.setFont(&fonts::Font2);
  sp.setTextColor(kb.connected() ? COLOR_UI : COLOR_LEVER_MUTE);
  sp.drawString(line, c, c + 130);
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
  snprintf(line, sizeof(line), "ble %s  mic %s%s", !HID_ENABLED ? "off" : (kb.connected() ? "connected" : "advertising"),
           micMuted ? "MUTED" : "on", meetingMode ? "  MEETING" : (passMode ? "  PASS" : ""));
  sp.drawString(line, x, y);
  y += 16;
  snprintf(line, sizeof(line), "mark %s %s", markColorName(markAt(markIndex).color), markShapeName(markAt(markIndex).shape));
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
  if (passMode) {
    face.sprite().fillScreen(COLOR_TRANSPARENT);
    pass.draw(face.sprite(), face.sprite().width() / 2.0f, now);
  } else if (meetingMode) {
    drawMeeting(now);
  } else {
    face.draw(markAt(markIndex));
    if (leverVisible()) drawLever();
  }
  if ((int32_t)(now - statusUntil) < 0) drawStatus(now);

  if (out != &screen) M5.Display.startWrite();
  out->fillScreen(TFT_BLACK);
  if (FACE_ANTIALIAS) face.sprite().pushRotatedWithAA(out, angle, COLOR_TRANSPARENT);
  else face.sprite().pushRotateZoom(out, screenW / 2.0f + DISPLAY_OFFSET_X, screenH / 2.0f + DISPLAY_OFFSET_Y, angle, 1.0f, 1.0f, COLOR_TRANSPARENT);
  if (out == &screen) screen.pushSprite(0, 0);
  else M5.Display.endWrite();
}

// ------------------------------------------------------------
static void handleButtons(uint32_t now) {
  static bool markChordActive = false;
  if (M5.BtnA.isPressed() && M5.BtnB.isPressed()) {
    if (!markChordActive) {
      markChordActive = true;
      markIndex = nextMarkIndex(markIndex);
      prefs.putUChar("mark", markIndex);
      statusUntil = now + 1500;
      vibrate(150, 60);
      printf("[cfg] mark -> %s %s\n",
             markColorName(markAt(markIndex).color),
             markShapeName(markAt(markIndex).shape));
    }
    return;
  }
  if (markChordActive) {
    if (!M5.BtnA.isPressed() && !M5.BtnB.isPressed()) markChordActive = false;
    return;
  }

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
  checkBoardCacheBeforeBegin();
  M5.begin(cfg);
  checkBoardAfterBegin();   // 基板の誤認識なら再起動して戻ってこない
  bootMs = millis();
  printf("[boot] ioe1 gpio4(display power)=%d gpio5=%d brightness=%d\n",
         (int)M5.getIOExpander(0).getWriteValue(m5::M5IOE1_Class::gpio4),
         (int)M5.getIOExpander(0).getWriteValue(m5::M5IOE1_Class::gpio5), (int)M5.Display.getBrightness());

  printf("\n[boot] M5Unified board=%d imu=%d psram=%u\n", (int)M5.getBoard(), (int)M5.Imu.getType(), (unsigned)ESP.getPsramSize());

  M5.BtnA.setHoldThresh(BUTTON_HOLD_MS);
  M5.BtnB.setHoldThresh(BUTTON_HOLD_MS);
  loadSettings();
  if (PASS_ENABLED) {
    pass.begin();
    printf("[boot] passes: %d\n", pass.count());
  }

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
  randomSeed(esp_random());

  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setBrightness(BRIGHTNESS_LEVELS[brightnessIndex]);
  motion.touch(millis());
  if (VIBRATE_ON_BOOT && cause != ESP_SLEEP_WAKEUP_TIMER) vibrate(160, 70);
  lastLoopUs = micros();
  printf("[boot] display %dx%d, sign=%+d offset=%.1f, mark=%s %s\n",
         screenW, screenH, orient.sign(), orient.offset(),
         markColorName(markAt(markIndex).color),
         markShapeName(markAt(markIndex).shape));
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

  if (M5.BtnA.isPressed() || M5.BtnB.isPressed()) sound.suppressLoudUntil(now + SOUND_SUPPRESS_AFTER_TOUCH_MS);   // ボタンのカチッで驚かない

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
  if (in.loudNoise && (motion.idleMs(now) < SOUND_SUPPRESS_MOTION_MS || M5.Touch.getCount() > 0)) {
    // 叩かれた / 持ち替えた / 触られた衝撃はマイクにも入る。空気の音ではないので無視
    in.loudNoise = false;
    printf("[snd] loud ignored (handling) %.0f dB\n", sound.levelDb());
  }
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
    const LeverGeom g = leverGeom();
    const bool onLever = leverVisible() &&
                         (g.vertical ? (fabsf(lx - g.x) < g.zoneHalfW && fabsf(ly - g.y) < g.halfLen + g.knobR)
                                     : (fabsf(ly - g.y) < g.zoneHalfW && fabsf(lx - g.x) < g.halfLen + g.knobR));

    // ---- 縦スワイプでモード切替 (下 = 会議モードへ, 上 = 通常へ)。通常モードのレバー上では無効 ----
    if (t.wasFlicked() && !(g.vertical && (onLever || leverDragging))) {
      const float sdx = (float)t.distanceX(), sdy = (float)t.distanceY();
      const float fly = sdx * sinf(a) + sdy * cosf(a);   // 顔ローカルの縦方向の移動量
      const float flx = sdx * cosf(a) - sdy * sinf(a);
      if (passMode) {
        // チケットモード: 左右で戻る、上下で次 / 前のチケット
        if (fabsf(flx) > SWIPE_MIN_PX && fabsf(flx) > 1.5f * fabsf(fly)) { passMode = false; pass.stopReceive(); buzz(120, 40, now); puts("[mode] pass off"); }
        else if (fabsf(fly) > SWIPE_MIN_PX && fabsf(fly) > 1.5f * fabsf(flx)) { if (fly > 0) pass.next(); else pass.prev(); }
        passHoldStart = 0;
      } else if (fabsf(fly) > SWIPE_MIN_PX && fabsf(fly) > 1.5f * fabsf(flx)) {
        leverCancel = true;   // このタッチをレバー操作として扱わない
        setMeetingMode(fly > 0, now);
      } else if (PASS_ENABLED && !meetingMode && fabsf(flx) > SWIPE_MIN_PX && fabsf(flx) > 1.5f * fabsf(fly)) {
        passMode = true;      // 左右スワイプでチケットモードへ
        passHoldStart = 0;
        buzz(120, 40, now);
        statusUntil = 0;
        puts("[mode] pass ON");
      }
    }

    if (passMode) {
      // タップ: 受信の開始 / 停止。長押し: 表示中のチケットを削除
      if (t.wasClicked()) {
        if (pass.receiving()) pass.stopReceive();
        else pass.startReceive(now);
        buzz(100, 30, now);
      }
      if (t.isPressed()) {
        pressed = true;
        motion.touch(now);
        sound.suppressLoudUntil(now + SOUND_SUPPRESS_AFTER_TOUCH_MS);
        if (passHoldStart == 0) { passHoldStart = now ? now : 1; passHoldDone = false; }
        else if (!passHoldDone && now - passHoldStart > PASS_DELETE_HOLD_MS && pass.count() > 0 && !pass.receiving()) {
          passHoldDone = true;
          pass.removeCurrent();
          buzz(200, 120, now);
          printf("[pass] deleted, %d left\n", pass.count());
        }
      } else {
        passHoldStart = 0;
      }
    }

    if (t.wasClicked() && !onLever && !leverDragging && !meetingMode && !passMode) in.tapped = true;
    if (t.isPressed() && !passMode) {
      pressed = true;
      motion.touch(now);
      sound.suppressLoudUntil(now + SOUND_SUPPRESS_AFTER_TOUCH_MS);   // タッチ音で驚かない
      if (onLever || leverDragging) {
        // ---- ミュートレバーをドラッグ中 (顔は反応しない) ----
        const float along = g.vertical ? (ly - g.y) : (lx - g.x);
        const float pos = (along + g.halfLen) / (2.0f * g.halfLen);
        leverDragPos = pos < 0 ? 0 : (pos > 1 ? 1 : pos);
        if (!leverDragging) { leverDragging = true; leverDragStart = leverDragPos; leverCancel = false; }
        mouthTouchStart = 0;
      } else if (meetingMode) {
        mouthTouchStart = 0;   // 会議モードでは顔は反応しない
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
    if (!leverCancel && newMuted != micMuted) {
      micMuted = newMuted;
      sendHotkey(HID_MUTE_KEY, HID_MUTE_MODS, now);
      buzz(200, micMuted ? 120 : 40, now);   // ミュートは長め、解除は短め (会議モードでは鳴らない)
      meetingToggleFlashUntil = now + 350;   // 会議モードは視覚で知らせる
      printf("[hid] mic %s\n", micMuted ? "MUTED" : "on");
    }
    leverCancel = false;
  }
  if (in.tapped) motion.touch(now);
  in.idleMs = motion.idleMs(now);
  in.moving = in.idleMs < 300;

  if (meetingMode || passMode) {
    // 会議モード / チケットモード: 顔は動かさず、寝もしない (画面を見られる状態を保つ)
    motion.touch(now);
    in = FaceInput{};
    in.dt = dt; in.now = now; in.autoSleepEnabled = false; in.moving = true;
  }
  // ---- チケット受信 (Wi-Fi) ----
  pass.update(now);
  if (PASS_DEBUG_RECEIVE_ON_BOOT) {   // 開発用: 起動 3 秒後に受信待ちへ + ループの生存確認
    static bool debugStarted = false;
    if (!debugStarted && now > 3000) { debugStarted = true; passMode = true; pass.startReceive(now); puts("[debug] pass receive started"); }
    static uint32_t lastBeat = 0;
    if (now - lastBeat >= 5000) {
      lastBeat = now;
      printf("[loop] t=%lus mode=%s wifi=%d recv=%d heap=%u dt=%.3f\n", (unsigned long)(now / 1000),
             passMode ? "pass" : (meetingMode ? "meeting" : "normal"), (int)WiFi.status(), (int)pass.receiving(),
             (unsigned)ESP.getFreeHeap(), dt);
    }
  }
  if (pass.takeReceived()) {
    buzz(180, 60, now);
    printf("[pass] now %d passes, showing #%d\n", pass.count(), pass.current() + 1);
  }
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
