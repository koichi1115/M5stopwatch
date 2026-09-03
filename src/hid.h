// BLE HID キーボード (NimBLE)。PC にペアリングしてショートカットキーを送る
//   - 標準キーボードレポート (修飾キー + 6 キー)
//   - 接続/切断で再アドバタイズ。ボンディングは Just Works
#pragma once
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include "config.h"

class BleKeyboardMini {
public:
  // 修飾キー (HID)
  static constexpr uint8_t MOD_LCTRL = 0x01, MOD_LSHIFT = 0x02, MOD_LALT = 0x04, MOD_LGUI = 0x08;
  // キーコード (HID Usage, Keyboard page)
  static constexpr uint8_t KEY_NONE = 0x00, KEY_TAB = 0x2B, KEY_H = 0x0B, KEY_K = 0x0E, KEY_M = 0x10,
                           KEY_LEFT = 0x50, KEY_RIGHT = 0x4F, KEY_ESC = 0x29, KEY_SPACE = 0x2C,
                           KEY_HENKAN = 0x8A,      // 変換
                           KEY_MUHENKAN = 0x8B,    // 無変換
                           KEY_KANA = 0x88;        // カタカナ/ひらがな

  bool begin(const char* name) {
    if (_started) return true;
    NimBLEDevice::init(name);
    NimBLEDevice::setPower(ESP_PWR_LVL_P3);
    NimBLEDevice::setSecurityAuth(true, false, true);   // bonding, no MITM, secure connections
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
    _server = NimBLEDevice::createServer();
    _server->setCallbacks(&_cb);
    _hid = new NimBLEHIDDevice(_server);
    _input = _hid->getInputReport(1);
    _hid->setManufacturer("M5Stack");
    _hid->setPnp(0x02, 0xE502, 0xA111, 0x0210);
    _hid->setHidInfo(0x00, 0x01);
    _hid->setReportMap((uint8_t*)kReportMap, sizeof(kReportMap));
    _hid->startServices();
    _hid->setBatteryLevel(100);
    auto* adv = NimBLEDevice::getAdvertising();
    adv->setAppearance(0x03C1);   // HID Keyboard
    adv->addServiceUUID(_hid->getHidService()->getUUID());
    adv->setName(name);
    adv->enableScanResponse(true);
    _started = adv->start();
    return _started;
  }

  void end() {
    if (!_started) return;
    NimBLEDevice::deinit(true);
    _started = false;
    _cb.connected = false;
  }

  bool started() const { return _started; }
  bool connected() const { return _cb.connected; }
  // 接続状態が変わったら 1 回だけ true を返す (通知用)
  bool takeConnectionChange() { bool c = _cb.changed; _cb.changed = false; return c; }
  void setBatteryLevel(uint8_t pct) { if (_hid) _hid->setBatteryLevel(pct); }

  void press(uint8_t key, uint8_t mods = 0) {
    uint8_t r[8] = {mods, 0, key, 0, 0, 0, 0, 0};
    send(r);
  }
  void release() {
    uint8_t r[8] = {0};
    send(r);
  }
  // 押して離す。修飾キーは同時押し
  void tap(uint8_t key, uint8_t mods = 0, uint32_t holdMs = 40) {
    press(key, mods);
    delay(holdMs);
    release();
    delay(10);
  }

private:
  struct Cb : public NimBLEServerCallbacks {
    volatile bool connected = false;
    volatile bool changed = false;
    void onConnect(NimBLEServer* s, NimBLEConnInfo& info) override {
      connected = true; changed = true;
    }
    void onDisconnect(NimBLEServer* s, NimBLEConnInfo& info, int reason) override {
      connected = false; changed = true;
      NimBLEDevice::startAdvertising();
    }
  };

  void send(const uint8_t* r) {
    if (!_cb.connected || !_input) return;
    _input->setValue(r, 8);
    _input->notify();
  }

  // 標準キーボード (Report ID 1): 修飾 8bit + 予約 8bit + キー 6 バイト
  static constexpr uint8_t kReportMap[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x06,        // Usage (Keyboard)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x01,        //   Report ID (1)
    0x05, 0x07,        //   Usage Page (Key Codes)
    0x19, 0xE0,        //   Usage Minimum (224)
    0x29, 0xE7,        //   Usage Maximum (231)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x08,        //   Report Count (8)
    0x81, 0x02,        //   Input (Data, Variable, Absolute) ; 修飾キー
    0x95, 0x01,        //   Report Count (1)
    0x75, 0x08,        //   Report Size (8)
    0x81, 0x01,        //   Input (Constant) ; 予約
    0x95, 0x06,        //   Report Count (6)
    0x75, 0x08,        //   Report Size (8)
    0x15, 0x00,        //   Logical Minimum (0)
    0x26, 0xFF, 0x00,  //   Logical Maximum (255)
    0x05, 0x07,        //   Usage Page (Key Codes)
    0x19, 0x00,        //   Usage Minimum (0)
    0x2A, 0xFF, 0x00,  //   Usage Maximum (255)
    0x81, 0x00,        //   Input (Data, Array) ; キー
    0xC0               // End Collection
  };

  NimBLEServer* _server = nullptr;
  NimBLEHIDDevice* _hid = nullptr;
  NimBLECharacteristic* _input = nullptr;
  Cb _cb;
  bool _started = false;
};
