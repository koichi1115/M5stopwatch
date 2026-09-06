// iPhone の通知を受け取る (ANCS = Apple Notification Center Service)。
//
//   iPhone とペアリングすると、iPhone 側が ANCS のサーバになる。バッジは同じ接続の上で
//   GATT クライアントとして次を行う:
//     Notification Source (通知) を購読 → 追加イベントの UID を受け取る
//     Control Point に「この UID の アプリ名 / タイトル / 本文 をくれ」と書く
//     Data Source (通知) で内容が返る (複数回に分かれて届くので繋ぎ合わせる)
//
//   広告に ANCS の Service Solicitation を載せておくと、iOS 側が「通知を出す相手」として扱ってくれる。
//   iOS は BLE キーボード相手だとパスキー入力を求めるので、ペアリングは画面に数字を出す方式にしてある
//   (hid.h 側の設定)。
#pragma once
#include <NimBLEDevice.h>
#include <string.h>
#include <ctype.h>
#include "config.h"

struct AncsNotification {
  uint32_t uid;
  uint8_t category;
  char app[48];
  char title[64];
  char message[160];
};

class AncsClient {
public:
  static NimBLEUUID serviceUuid() { return NimBLEUUID("7905F431-B5CE-4E99-A40F-4B1E122D00D0"); }

  // ---- サーバ側のコールバックから呼ぶ ----
  void onPeerConnect(NimBLEServer* server, uint16_t handle) {
    if (_handle != 0xFFFF) return;   // 既に別の相手を見ている
    _server = server;
    _handle = handle;
    _client = nullptr;
    _state = State::Wait;
    _nextTry = 0;
    _tries = 0;
  }
  void onPeerDisconnect(uint16_t handle) {
    if (handle != _handle) return;
    _handle = 0xFFFF;
    _client = nullptr;
    _state = State::Idle;
    _ns = _ds = _cp = nullptr;
    _accLen = 0;
    _pendingUid = 0;
  }

  bool connected() const { return _state == State::Ready; }
  bool takeNotification(AncsNotification& out) {
    if (!_ready) return false;
    out = _note;
    _ready = false;
    return true;
  }

  // ---- loop から呼ぶ (ブロックしない) ----
  void update(uint32_t now) {
    if (_handle == 0xFFFF) return;

    if (_state == State::Wait) {
      if ((int32_t)(now - _nextTry) < 0) return;
      _nextTry = now + ANCS_DISCOVER_RETRY_MS;
      if (++_tries > ANCS_DISCOVER_TRIES) { _state = State::Idle; puts("[ancs] no ANCS on this peer (probably the PC)"); return; }
      if (!_server) return;
      NimBLEClient* c = _server->getClient(_handle);
      if (!c || !c->isConnected()) return;
      // iOS はボンディングが済むまで ANCS を見せない
      NimBLEConnInfo info = NimBLEDevice::getServer()->getPeerInfoByHandle(_handle);
      if (!info.isEncrypted()) return;
      NimBLERemoteService* svc = c->getService(serviceUuid());
      if (!svc) return;
      _client = c;
      _ns = svc->getCharacteristic(NimBLEUUID("9FBF120D-6301-42D9-8C58-25E699A21DBD"));
      _cp = svc->getCharacteristic(NimBLEUUID("69D1D8F3-45E1-49A8-9821-9BBDFDAAD9D9"));
      _ds = svc->getCharacteristic(NimBLEUUID("22EAC6E9-24D6-4BB5-BE44-B36ACE7C7BFB"));
      if (!_ns || !_cp || !_ds) { puts("[ancs] characteristics missing"); return; }
      _self = this;
      if (!_ds->subscribe(true, dsCb) || !_ns->subscribe(true, nsCb)) { puts("[ancs] subscribe failed"); return; }
      _state = State::Ready;
      puts("[ancs] connected to iPhone notifications");
      return;
    }

    // 内容の取得は loop 側で行う (BLE のコールバック内で書き込まない)
    if (_state == State::Ready && _pendingUid && !_awaitingData) {
      const uint32_t uid = _pendingUid;
      _pendingUid = 0;
      _accLen = 0;
      _awaitingData = true;
      _awaitSince = now;
      uint8_t cmd[16];
      int n = 0;
      cmd[n++] = 0x00;                         // GetNotificationAttributes
      memcpy(cmd + n, &uid, 4); n += 4;
      cmd[n++] = 0x00;                         // AppIdentifier (長さ指定なし)
      cmd[n++] = 0x01; cmd[n++] = (uint8_t)(sizeof(_note.title) - 1); cmd[n++] = 0x00;   // Title
      cmd[n++] = 0x03; cmd[n++] = (uint8_t)(sizeof(_note.message) - 1); cmd[n++] = 0x00; // Message
      if (!_cp->writeValue(cmd, n, true)) { _awaitingData = false; puts("[ancs] control point write failed"); }
    }
    if (_awaitingData && now - _awaitSince > 3000) _awaitingData = false;   // 応答が来なければ諦める
  }

private:
  enum class State : uint8_t { Idle, Wait, Ready };

  // ---- Notification Source: 8 バイト (eventID, flags, category, count, uid[4]) ----
  static void nsCb(NimBLERemoteCharacteristic* c, uint8_t* d, size_t len, bool notify) {
    if (!_self || len < 8) return;
    const uint8_t event = d[0], flags = d[1], category = d[2];
    if (event != 0) return;                    // 追加以外 (変更 / 削除) は無視
    if (flags & 0x04) return;                  // PreExisting: 接続時に溜まっていた分は無視
    if (ANCS_IGNORE_SILENT && (flags & 0x01)) return;
    uint32_t uid;
    memcpy(&uid, d + 4, 4);
    _self->_stageCategory = category;
    _self->_stageUid = uid;
    _self->_pendingUid = uid;                  // 続きは update() で取りに行く
  }

  // ---- Data Source: 断片で届くので繋いでから解析 ----
  static void dsCb(NimBLERemoteCharacteristic* c, uint8_t* d, size_t len, bool notify) {
    if (!_self) return;
    AncsClient* s = _self;
    if (s->_accLen + len > sizeof(s->_acc)) { s->_accLen = 0; s->_awaitingData = false; return; }
    memcpy(s->_acc + s->_accLen, d, len);
    s->_accLen += len;
    s->parse();
  }

  // 応答: cmdID(1) uid(4) [attrID(1) len(2) data(len)] ...
  void parse() {
    if (_accLen < 5) return;
    size_t p = 5;
    char app[48] = {0}, title[64] = {0}, msg[160] = {0};
    int got = 0;
    while (p + 3 <= _accLen) {
      const uint8_t attr = _acc[p];
      const uint16_t alen = (uint16_t)(_acc[p + 1] | (_acc[p + 2] << 8));
      p += 3;
      if (p + alen > _accLen) return;          // まだ続きが届いていない
      char* dst = nullptr; size_t cap = 0;
      if (attr == 0x00) { dst = app; cap = sizeof(app); }
      else if (attr == 0x01) { dst = title; cap = sizeof(title); }
      else if (attr == 0x03) { dst = msg; cap = sizeof(msg); }
      if (dst) {
        const size_t n = alen < cap - 1 ? alen : cap - 1;
        memcpy(dst, _acc + p, n);
        dst[n] = 0;
        got++;
      }
      p += alen;
      if (got >= 3) break;
    }
    if (got < 3) return;
    _accLen = 0;
    _awaitingData = false;
    if (!allowed(app)) {
      printf("[ancs] skipped %s: %s\n", app, title);
      return;
    }
    _note.uid = _stageUid;
    _note.category = _stageCategory;
    strncpy(_note.app, app, sizeof(_note.app) - 1);
    strncpy(_note.title, title, sizeof(_note.title) - 1);
    strncpy(_note.message, msg, sizeof(_note.message) - 1);
    _ready = true;
    printf("[ancs] %s | %s | %s\n", _note.app, _note.title, _note.message);
  }

  // アプリの絞り込み (bundle id の部分一致、大文字小文字は無視)
  static bool allowed(const char* app) {
    if (ANCS_APP_FILTER_COUNT == 0) return true;
    char low[64];
    size_t i = 0;
    for (; app[i] && i < sizeof(low) - 1; ++i) low[i] = (char)tolower((unsigned char)app[i]);
    low[i] = 0;
    for (int k = 0; k < ANCS_APP_FILTER_COUNT; ++k) {
      if (strstr(low, ANCS_APP_FILTER[k])) return true;
    }
    return false;
  }

  static AncsClient* _self;

  NimBLEServer* _server = nullptr;
  NimBLEClient* _client = nullptr;
  NimBLERemoteCharacteristic *_ns = nullptr, *_cp = nullptr, *_ds = nullptr;
  uint16_t _handle = 0xFFFF;
  State _state = State::Idle;
  uint32_t _nextTry = 0, _awaitSince = 0;
  int _tries = 0;
  volatile uint32_t _pendingUid = 0, _stageUid = 0;
  volatile uint8_t _stageCategory = 0;
  volatile bool _awaitingData = false, _ready = false;
  uint8_t _acc[512];
  volatile size_t _accLen = 0;
  AncsNotification _note{};
};

inline AncsClient* AncsClient::_self = nullptr;
