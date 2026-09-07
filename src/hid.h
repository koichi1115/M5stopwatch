// BLE HID キーボード (NimBLE)。PC にペアリングしてショートカットキーを送る
//   - 標準キーボードレポート (修飾キー + 6 キー)
//   - 接続/切断で再アドバタイズ。ボンディングは Just Works
#pragma once
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include "config.h"
#include "ancs.h"

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
    // iOS は HID キーボードとのペアリングにパスキーを要求する。バッジには画面があるので
    // 「数字を表示して相手に入力してもらう」方式にする (Windows も同じ方式で通る)。
    NimBLEDevice::setSecurityAuth(true, true, true);    // bonding, MITM, secure connections
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);
    NimBLEDevice::setSecurityPasskey(HID_PAIRING_PASSKEY);
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
    // ANCS の Service Solicitation (AD type 0x15 = 128bit UUID のリスト) をスキャン応答に載せる
    {
      NimBLEUUID ancs = AncsClient::serviceUuid();
      const uint8_t* raw = ancs.getValue();
      uint8_t ad[18];
      ad[0] = 17; ad[1] = 0x15;
      memcpy(ad + 2, raw, 16);
      NimBLEAdvertisementData scan;
      scan.addData(ad, sizeof(ad));
      adv->setScanResponseData(scan);
    }
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
  int peerCount() const { return _server ? _server->getConnectedCount() : 0; }

  // ---- キーを送る相手 (PC) の一覧と選択 ----
  // iPhone (ANCS を持つ相手) は除く。名前は GAP から読む。
  struct KeyPeer { uint16_t handle; char name[20]; };

  int keyPeerCount() const { return _peerCount; }
  const KeyPeer& keyPeer(int i) const { return _peers[i < _peerCount ? i : 0]; }
  int keyTargetIndex() const { return _target < _peerCount ? _target : 0; }
  const char* keyTargetName() const { return _peerCount ? _peers[keyTargetIndex()].name : "-"; }
  // 送り先を次の PC へ。切り替えたら true
  bool cycleKeyTarget() {
    if (_peerCount < 2) return false;
    _target = (_target + 1) % _peerCount;
    return true;
  }

  // loop から呼ぶ。繋がっている PC の一覧を作り直す
  void updatePeers() {
    if (!_server) { _peerCount = 0; return; }
    const uint16_t phone = _cb.ancs ? _cb.ancs->peerHandle() : 0xFFFF;
    const uint16_t prev = _peerCount ? _peers[keyTargetIndex()].handle : 0xFFFF;
    KeyPeer fresh[kMaxPeers];
    int n = 0;
    const int total = _server->getConnectedCount();
    for (int i = 0; i < total && n < kMaxPeers; ++i) {
      const uint16_t h = _server->getPeerInfo(i).getConnHandle();
      if (h == phone) continue;                      // iPhone は対象外
      fresh[n].handle = h;
      // 既に名前を読んであれば引き継ぐ
      const char* known = nullptr;
      for (int k = 0; k < _peerCount; ++k) if (_peers[k].handle == h) known = _peers[k].name;
      if (known && known[0]) snprintf(fresh[n].name, sizeof(fresh[n].name), "%s", known);
      else readPeerName(h, fresh[n].name, sizeof(fresh[n].name));
      n++;
    }
    memcpy(_peers, fresh, sizeof(fresh));
    _peerCount = n;
    // 直前の送り先が残っていればそれを維持する
    _target = 0;
    for (int i = 0; i < _peerCount; ++i) if (_peers[i].handle == prev) _target = i;
  }
  void attachAncs(AncsClient* a) { _cb.ancs = a; }
  // ペアリング中に画面へ出す数字 (0 = 表示しない)
  uint32_t pairingPasskey() const {
    return (_cb.passkeyShownUntil && (int32_t)(millis() - _cb.passkeyShownUntil) < 0) ? _cb.passkey : 0;
  }
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
    AncsClient* ancs = nullptr;
    volatile uint32_t passkeyShownUntil = 0;
    volatile uint32_t passkey = 0;

    void onConnect(NimBLEServer* s, NimBLEConnInfo& info) override {
      connected = true; changed = true;
      if (ancs) ancs->onPeerConnect(s, info.getConnHandle());
      NimBLEDevice::startAdvertising();   // もう 1 台 (PC と iPhone) も繋げるようにする
    }
    void onDisconnect(NimBLEServer* s, NimBLEConnInfo& info, int reason) override {
      connected = s->getConnectedCount() > 0;
      changed = true;
      if (ancs) ancs->onPeerDisconnect(info.getConnHandle());
      NimBLEDevice::startAdvertising();
    }
    uint32_t onPassKeyDisplay() override {
      passkey = HID_PAIRING_PASSKEY;
      passkeyShownUntil = millis() + 60000;   // 画面に出す (main.cpp が読む)
      printf("[hid] pairing passkey: %06u\n", (unsigned)HID_PAIRING_PASSKEY);
      return HID_PAIRING_PASSKEY;
    }
    void onAuthenticationComplete(NimBLEConnInfo& info) override {
      passkeyShownUntil = 0;
      printf("[hid] paired: encrypted=%d bonded=%d\n", (int)info.isEncrypted(), (int)info.isBonded());
    }
  };

  void send(const uint8_t* r) {
    if (!_cb.connected || !_input) return;
    _input->setValue(r, 8);
    // 選んだ PC にだけ送る (iPhone は通知のために繋がっているだけ)
    if (!HID_SEND_TO_PC_ONLY || _peerCount == 0) { _input->notify(); return; }
    _input->notify(_peers[keyTargetIndex()].handle);
  }

  // 相手の機器名 (GAP 0x1800 / 0x2A00) を読む。取れなければアドレスの下 2 桁
  void readPeerName(uint16_t handle, char* out, size_t cap) {
    out[0] = 0;
    NimBLEClient* c = _server->getClient(handle);
    if (c) {
      NimBLEAttValue v = c->getValue(NimBLEUUID((uint16_t)0x1800), NimBLEUUID((uint16_t)0x2A00));
      if (v.length()) snprintf(out, cap, "%s", v.c_str());
    }
    if (!out[0]) {
      const std::string a = _server->getPeerInfoByHandle(handle).getAddress().toString();
      snprintf(out, cap, "%s", a.size() >= 5 ? a.substr(a.size() - 5).c_str() : a.c_str());
    }
    printf("[hid] peer %u name \"%s\"\n", (unsigned)handle, out);
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
  static constexpr int kMaxPeers = 3;
  KeyPeer _peers[kMaxPeers] = {};
  int _peerCount = 0, _target = 0;
  Cb _cb;
  bool _started = false;
};
