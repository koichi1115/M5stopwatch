// チケット (QR) モード: Wallet のパスやスクリーンショットを Wi-Fi で受け取り、QR を描き直して表示する
//   - 保存: NVS (namespace "pass") に title / text を PASS_MAX 枚まで
//   - 受信: Wi-Fi (secrets.h の一覧。繋がらなければ自分が AP になる) + 手書きの小さな HTTP サーバ
//       GET  /          … ブラウザ用の入力フォーム
//       POST /pass      … 本文の種類で分岐
//           .pkpass / zip  → pass.json から barcode.message と organizationName / description を取る
//           png / jpeg     → 画像から QR を読む (quirc)
//           それ以外        → 本文をそのまま QR の内容にする (form の text= / title= も可)
//   - 表示: QRCode ライブラリで描き直す (スキャナ向けに白背景 + 余白)。作品名は日本語フォント
#pragma once
#include <M5Unified.h>
#include <Preferences.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>
#include <qrcode.h>
#include <string.h>
#include <stdlib.h>
#include "miniz.h"          // ROM の tinfl (zip の deflate 展開)
#include "quirc.h"
#include "config.h"
#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif

class PassManager {
public:
  struct Entry { String title; String text; };

  void begin() {
    _prefs.begin("pass", false);
    _count = _prefs.getUChar("n", 0);
    if (_count > PASS_MAX) _count = PASS_MAX;
    for (int i = 0; i < _count; ++i) {
      _entries[i].title = _prefs.getString(key('t', i).c_str(), "");
      _entries[i].text = _prefs.getString(key('x', i).c_str(), "");
    }
    _current = _prefs.getUChar("cur", 0);
    if (_current >= _count) _current = 0;
  }

  int count() const { return _count; }
  int current() const { return _current; }
  void next() { if (_count > 1) { _current = (_current + 1) % _count; _prefs.putUChar("cur", _current); } }
  void prev() { if (_count > 1) { _current = (_current + _count - 1) % _count; _prefs.putUChar("cur", _current); } }
  const Entry* entry() const { return _count ? &_entries[_current] : nullptr; }

  bool add(const String& title, const String& text) {
    if (text.length() == 0 || text.length() > 2900) return false;
    // 同じ内容があれば差し替え (二重登録しない)
    for (int i = 0; i < _count; ++i) {
      if (_entries[i].text == text) { _entries[i].title = title; _current = i; save(); return true; }
    }
    if (_count >= PASS_MAX) {               // 一杯なら一番古いものを捨てる
      for (int i = 1; i < _count; ++i) _entries[i - 1] = _entries[i];
      _count--;
    }
    _entries[_count].title = title;
    _entries[_count].text = text;
    _current = _count;
    _count++;
    save();
    return true;
  }

  void removeCurrent() {
    if (!_count) return;
    for (int i = _current + 1; i < _count; ++i) _entries[i - 1] = _entries[i];
    _count--;
    if (_current >= _count) _current = _count ? _count - 1 : 0;
    save();
  }

  // ---------------- 表示 ----------------
  // 顔スプライト (正方形, 中心 c) に現在のパスを描く
  void draw(M5Canvas& sp, float c, uint32_t now) {
    sp.setTextDatum(middle_center);
    sp.setTextSize(1);
    if (_receiving) {
      drawReceiving(sp, c, now);
      return;
    }
    if (!_count) {
      sp.setFont(&fonts::lgfxJapanGothic_20);
      sp.setTextColor(COLOR_UI);
      sp.drawString("チケットはありません", (int)c, (int)(c - 30));
      sp.setFont(&fonts::lgfxJapanGothic_16);
      sp.drawString("タップで受信 / 左右スワイプで戻る", (int)c, (int)(c + 10));
      return;
    }
    const Entry& e = _entries[_current];
    // QR
    const int size = drawQr(sp, e.text.c_str(), (int)c, (int)(c + 22), PASS_QR_MAX_PX);
    // タイトル (上)
    sp.setFont(&fonts::lgfxJapanGothic_20);
    sp.setTextColor(COLOR_EYE_WHITE);
    String t = e.title.length() ? e.title : hostOf(e.text);
    if (sp.textWidth(t.c_str()) > 330) { sp.setFont(&fonts::lgfxJapanGothic_16); }
    while (t.length() > 4 && sp.textWidth(t.c_str()) > 340) t = t.substring(0, t.length() - 1);
    sp.drawString(t.c_str(), (int)c, (int)(c + 22 - size / 2 - 26));
    // 枚数 (下)
    char buf[32];
    snprintf(buf, sizeof(buf), "%d / %d", _current + 1, _count);
    sp.setFont(&fonts::Font2);
    sp.setTextColor(COLOR_UI);
    sp.drawString(buf, (int)c, (int)(c + 22 + size / 2 + 18));
  }

  // ---------------- 受信 ----------------
  bool receiving() const { return _receiving; }
  bool takeReceived() { bool r = _received; _received = false; return r; }
  const char* lastError() const { return _lastError; }

  void startReceive(uint32_t now) {
    if (_receiving) return;
    _receiving = true;
    _received = false;
    _state = State::Connecting;
    _stateSince = now;
    _lastActivity = now;
    _netIndex = 0;
    _lastError[0] = 0;
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(PASS_HOSTNAME);
    tryNextNetwork(now);
  }

  void stopReceive() {
    if (!_receiving) return;
    _receiving = false;
    _server.stop();
    MDNS.end();
    WiFi.disconnect(true, true);
    WiFi.mode(WIFI_OFF);
    _state = State::Off;
    puts("[pass] wifi off");
  }

  void update(uint32_t now) {
    if (!_receiving) return;
    if (now - _lastActivity > PASS_WIFI_TIMEOUT_MS) { puts("[pass] receive timeout"); stopReceive(); return; }
    switch (_state) {
      case State::Connecting:
        if (WiFi.status() == WL_CONNECTED) {
          _ip = WiFi.localIP();
          startServer();
          _state = State::Listening;
          printf("[pass] connected to %s ip=%s\n", WiFi.SSID().c_str(), _ip.toString().c_str());
        } else if (now - _stateSince > PASS_WIFI_CONNECT_TIMEOUT_MS) {
          if (!tryNextNetwork(now)) startAp();
        }
        break;
      case State::Listening:
        serve(now);
        break;
      default: break;
    }
  }

private:
  enum class State { Off, Connecting, Listening };

  // ---------- NVS ----------
  static String key(char k, int i) { char b[4] = {k, (char)('0' + i), 0, 0}; return String(b); }
  void save() {
    _prefs.putUChar("n", _count);
    _prefs.putUChar("cur", _current);
    for (int i = 0; i < PASS_MAX; ++i) {
      if (i < _count) {
        _prefs.putString(key('t', i).c_str(), _entries[i].title);
        _prefs.putString(key('x', i).c_str(), _entries[i].text);
      } else {
        _prefs.remove(key('t', i).c_str());
        _prefs.remove(key('x', i).c_str());
      }
    }
  }

  // ---------- QR 描画 ----------
  // 中心 (cx,cy)、最大辺 maxPx で描く。描いた一辺 (余白込み) を返す
  static int drawQr(M5Canvas& sp, const char* text, int cx, int cy, int maxPx) {
    QRCode qr;
    static uint8_t buf[3200];   // version 30 まで
    int version = 0;
    for (int v = 1; v <= 30; ++v) {
      if (qrcode_getBufferSize(v) > sizeof(buf)) break;
      if (qrcode_initText(&qr, buf, v, ECC_LOW, text) == 0) { version = v; break; }
    }
    if (!version) {
      sp.setFont(&fonts::Font2);
      sp.setTextColor(COLOR_LEVER_MUTE);
      sp.drawString("QR too long", cx, cy);
      return 60;
    }
    const int modules = qr.size;
    const int quiet = PASS_QR_QUIET_PX;
    int scale = (maxPx - 2 * quiet) / modules;
    if (scale < 1) scale = 1;
    const int qrPx = modules * scale;
    const int size = qrPx + 2 * quiet;
    const int x0 = cx - size / 2, y0 = cy - size / 2;
    sp.fillRoundRect(x0, y0, size, size, 10, 0xFFFF);
    const int ox = x0 + quiet, oy = y0 + quiet;
    for (int y = 0; y < modules; ++y) {
      for (int x = 0; x < modules; ++x) {
        if (qrcode_getModule(&qr, x, y)) sp.fillRect(ox + x * scale, oy + y * scale, scale, scale, 0x0000);
      }
    }
    return size;
  }

  static String hostOf(const String& text) {
    int p = text.indexOf("://");
    if (p < 0) return text.length() > 24 ? text.substring(0, 24) + "…" : text;
    int s = p + 3, e = text.indexOf('/', s);
    return e < 0 ? text.substring(s) : text.substring(s, e);
  }

  void drawReceiving(M5Canvas& sp, float c, uint32_t now) {
    sp.setFont(&fonts::lgfxJapanGothic_20);
    sp.setTextColor(COLOR_UI_ACCENT);
    sp.drawString("受信待ち", (int)c, (int)(c - 110));
    sp.setFont(&fonts::lgfxJapanGothic_16);
    sp.setTextColor(COLOR_UI);
    char buf[64];
    if (_state == State::Connecting) {
      const int n = sizeof(WIFI_NETWORKS) / sizeof(WIFI_NETWORKS[0]);
      if (_netIndex > 0 && _netIndex <= n) snprintf(buf, sizeof(buf), "Wi-Fi 接続中: %s", WIFI_NETWORKS[_netIndex - 1].ssid);
      else snprintf(buf, sizeof(buf), "Wi-Fi 接続中...");
      sp.drawString(buf, (int)c, (int)(c - 60));
      const int dots = (now / 400) % 4;
      sp.drawString(String("....").substring(0, dots).c_str(), (int)c, (int)(c - 30));
    } else {
      if (_apMode) {
        sp.drawString("Wi-Fi に繋がらないので AP になりました", (int)c, (int)(c - 70));
        snprintf(buf, sizeof(buf), "SSID: %s", apSsid());
        sp.drawString(buf, (int)c, (int)(c - 44));
        snprintf(buf, sizeof(buf), "PASS: %s", apPass());
        sp.drawString(buf, (int)c, (int)(c - 20));
      } else {
        snprintf(buf, sizeof(buf), "Wi-Fi: %s", WiFi.SSID().c_str());
        sp.drawString(buf, (int)c, (int)(c - 60));
      }
      sp.setTextColor(COLOR_EYE_WHITE);
      snprintf(buf, sizeof(buf), "http://%s.local/pass", PASS_HOSTNAME);
      sp.drawString(buf, (int)c, (int)(c + 14));
      snprintf(buf, sizeof(buf), "http://%s/", _ip.toString().c_str());
      sp.drawString(buf, (int)c, (int)(c + 40));
      sp.setTextColor(COLOR_UI);
      sp.drawString("iPhone のショートカットで送ってください", (int)c, (int)(c + 76));
    }
    if (_lastError[0]) {
      sp.setTextColor(COLOR_LEVER_MUTE);
      sp.drawString(_lastError, (int)c, (int)(c + 110));
    }
    sp.setFont(&fonts::Font2);
    sp.setTextColor(COLOR_UI);
    const int remain = (int)((PASS_WIFI_TIMEOUT_MS - (now - _lastActivity)) / 1000);
    snprintf(buf, sizeof(buf), "tap to stop  (%ds)", remain < 0 ? 0 : remain);
    sp.drawString(buf, (int)c, (int)(c + 140));
  }

  // ---------- Wi-Fi ----------
  static const char* apSsid() { return "Makkuro-Badge"; }
  static const char* apPass() { return "makkuro1234"; }

  bool tryNextNetwork(uint32_t now) {
    const int n = sizeof(WIFI_NETWORKS) / sizeof(WIFI_NETWORKS[0]);
    while (_netIndex < n) {
      const WifiCred& w = WIFI_NETWORKS[_netIndex++];
      if (!w.ssid || !w.ssid[0] || strcmp(w.ssid, "your-home-ssid") == 0) continue;
      printf("[pass] connecting to %s\n", w.ssid);
      WiFi.disconnect();
      WiFi.begin(w.ssid, w.pass);
      _stateSince = now;
      return true;
    }
    return false;
  }

  void startAp() {
    puts("[pass] no known wifi, starting AP");
    WiFi.disconnect(true);
    WiFi.mode(WIFI_AP);
    WiFi.softAP(apSsid(), apPass());
    _ip = WiFi.softAPIP();
    _apMode = true;
    startServer();
    _state = State::Listening;
  }

  void startServer() {
    if (!MDNS.begin(PASS_HOSTNAME)) puts("[pass] mDNS failed");
    else MDNS.addService("http", "tcp", 80);
    _server.begin();
    _server.setNoDelay(true);
  }

  // ---------- HTTP (手書き。大きな本文を PSRAM に受けるため) ----------
  void serve(uint32_t now) {
    WiFiClient client = _server.accept();
    if (!client) return;
    _lastActivity = now;
    client.setTimeout(5);

    String method, path, contentType;
    size_t contentLength = 0;
    // リクエスト行
    String line = client.readStringUntil('\n');
    line.trim();
    int sp1 = line.indexOf(' '), sp2 = line.indexOf(' ', sp1 + 1);
    if (sp1 < 0 || sp2 < 0) { client.stop(); return; }
    method = line.substring(0, sp1);
    path = line.substring(sp1 + 1, sp2);
    // ヘッダ
    while (client.connected()) {
      line = client.readStringUntil('\n');
      line.trim();
      if (line.length() == 0) break;
      String lower = line; lower.toLowerCase();
      if (lower.startsWith("content-length:")) contentLength = (size_t)line.substring(15).toInt();
      else if (lower.startsWith("content-type:")) { contentType = line.substring(13); contentType.trim(); contentType.toLowerCase(); }
    }
    printf("[pass] %s %s type=%s len=%u\n", method.c_str(), path.c_str(), contentType.c_str(), (unsigned)contentLength);

    String query;
    { int q = path.indexOf('?'); if (q >= 0) { query = path.substring(q + 1); path = path.substring(0, q); } }

    if (method == "GET") {
      respondForm(client);
      client.stop();
      return;
    }
    if (method != "POST" || contentLength == 0 || contentLength > PASS_MAX_UPLOAD_BYTES) {
      respond(client, 400, "text/plain", "bad request (POST with Content-Length up to 3MB)");
      client.stop();
      return;
    }
    uint8_t* body = (uint8_t*)ps_malloc(contentLength + 1);
    if (!body) { respond(client, 500, "text/plain", "out of memory"); client.stop(); return; }
    size_t got = 0;
    uint32_t last = millis();
    while (got < contentLength && client.connected() && millis() - last < 8000) {
      int n = client.read(body + got, contentLength - got);
      if (n > 0) { got += n; last = millis(); }
      else delay(1);
    }
    body[got] = 0;
    String title = urlParam(query, "title");
    String text, err;
    bool ok = false;
    if (got == contentLength) {
      if ((got > 4 && body[0] == 'P' && body[1] == 'K') || contentType.indexOf("pkpass") >= 0 || contentType.indexOf("zip") >= 0) {
        ok = parsePkpass(body, got, title, text, err);
      } else if ((got > 8 && body[0] == 0x89 && body[1] == 'P') || (got > 3 && body[0] == 0xFF && body[1] == 0xD8) ||
                 contentType.startsWith("image/")) {
        ok = decodeImage(body, got, text, err);
      } else if (contentType.indexOf("x-www-form-urlencoded") >= 0) {
        String form((const char*)body);
        text = urlParam(form, "text");
        if (title.length() == 0) title = urlParam(form, "title");
        ok = text.length() > 0;
        if (!ok) err = "text is empty";
      } else {
        text = String((const char*)body);
        text.trim();
        ok = text.length() > 0;
        if (!ok) err = "body is empty";
      }
    } else {
      err = "incomplete body";
    }
    free(body);

    if (ok && add(title, text)) {
      _received = true;
      _lastError[0] = 0;
      printf("[pass] received: title=\"%s\" text=%s\n", title.c_str(), text.c_str());
      String res = "{\"ok\":true,\"title\":\"" + jsonEscape(title) + "\",\"text\":\"" + jsonEscape(text) + "\"}";
      respond(client, 200, "application/json", res);
    } else {
      if (ok) err = "could not save";
      snprintf(_lastError, sizeof(_lastError), "%s", err.c_str());
      printf("[pass] failed: %s\n", err.c_str());
      respond(client, 422, "application/json", "{\"ok\":false,\"error\":\"" + jsonEscape(err) + "\"}");
    }
    client.stop();
    _lastActivity = millis();
  }

  static void respond(WiFiClient& c, int code, const char* type, const String& body) {
    c.printf("HTTP/1.1 %d %s\r\nContent-Type: %s; charset=utf-8\r\nContent-Length: %u\r\nConnection: close\r\n\r\n",
             code, code == 200 ? "OK" : "Error", type, (unsigned)body.length());
    c.print(body);
  }
  static void respondForm(WiFiClient& c) {
    static const char html[] =
      "<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width'>"
      "<title>Makkuro Badge</title><body style='font-family:sans-serif;max-width:480px;margin:2em auto'>"
      "<h2>チケットを送る</h2><form method=post action=/pass enctype=application/x-www-form-urlencoded>"
      "<p>タイトル<br><input name=title style='width:100%'></p>"
      "<p>QR の内容<br><textarea name=text rows=6 style='width:100%'></textarea></p>"
      "<button>送る</button></form>"
      "<p style='color:#888'>iPhone: Wallet のパスを共有 → ショートカットで POST /pass (.pkpass / スクリーンショット)</p>";
    respond(c, 200, "text/html", html);
  }
  static String urlParam(const String& q, const char* name) {
    String k = String(name) + "=";
    int s = 0;
    while (s <= (int)q.length()) {
      int e = q.indexOf('&', s); if (e < 0) e = q.length();
      String kv = q.substring(s, e);
      if (kv.startsWith(k)) return urlDecode(kv.substring(k.length()));
      s = e + 1;
    }
    return String();
  }
  static String urlDecode(const String& s) {
    String o; o.reserve(s.length());
    for (size_t i = 0; i < s.length(); ++i) {
      char ch = s[i];
      if (ch == '+') o += ' ';
      else if (ch == '%' && i + 2 < s.length()) { o += (char)strtol(s.substring(i + 1, i + 3).c_str(), nullptr, 16); i += 2; }
      else o += ch;
    }
    return o;
  }
  static String jsonEscape(const String& s) {
    String o; o.reserve(s.length() + 8);
    for (size_t i = 0; i < s.length(); ++i) {
      char ch = s[i];
      if (ch == '"' || ch == '\\') { o += '\\'; o += ch; }
      else if (ch == '\n') o += "\\n";
      else if ((uint8_t)ch < 0x20) { }
      else o += ch;
    }
    return o;
  }

  // ---------- .pkpass (zip) → pass.json ----------
  static uint32_t rd32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
  static uint16_t rd16(const uint8_t* p) { return p[0] | (p[1] << 8); }

  bool parsePkpass(const uint8_t* zip, size_t len, String& title, String& text, String& err) {
    // End of central directory を末尾から探す
    if (len < 22) { err = "zip too small"; return false; }
    size_t eocd = 0; bool found = false;
    for (size_t i = len - 22; i + 1 > 0 && len - i < 70000; --i) {
      if (rd32(zip + i) == 0x06054b50) { eocd = i; found = true; break; }
      if (i == 0) break;
    }
    if (!found) { err = "not a zip"; return false; }
    const uint16_t entries = rd16(zip + eocd + 10);
    size_t cd = rd32(zip + eocd + 16);
    for (int e = 0; e < entries && cd + 46 <= len; ++e) {
      if (rd32(zip + cd) != 0x02014b50) break;
      const uint16_t method = rd16(zip + cd + 10);
      const uint32_t csize = rd32(zip + cd + 20), usize = rd32(zip + cd + 24);
      const uint16_t nlen = rd16(zip + cd + 28), xlen = rd16(zip + cd + 30), clen = rd16(zip + cd + 32);
      const uint32_t lho = rd32(zip + cd + 42);
      String name((const char*)zip + cd + 46, nlen);
      cd += 46 + nlen + xlen + clen;
      if (name != "pass.json") continue;
      if (lho + 30 > len) { err = "bad local header"; return false; }
      const uint16_t lnlen = rd16(zip + lho + 26), lxlen = rd16(zip + lho + 28);
      const uint8_t* data = zip + lho + 30 + lnlen + lxlen;
      if (data + csize > zip + len) { err = "truncated"; return false; }
      char* json = (char*)ps_malloc(usize + 1);
      if (!json) { err = "oom"; return false; }
      bool ok = false;
      if (method == 0) { memcpy(json, data, usize); ok = true; }
      else if (method == 8) {
        const size_t n = tinfl_decompress_mem_to_mem(json, usize, data, csize, TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
        ok = (n != TINFL_DECOMPRESS_MEM_TO_MEM_FAILED) && n == usize;
      }
      json[usize] = 0;
      if (!ok) { free(json); err = "inflate failed"; return false; }
      const bool r = parsePassJson(json, title, text, err);
      free(json);
      return r;
    }
    err = "pass.json not found";
    return false;
  }

  bool parsePassJson(const char* json, String& title, String& text, String& err) {
    JsonDocument doc;
    DeserializationError de = deserializeJson(doc, json);
    if (de) { err = String("json: ") + de.c_str(); return false; }
    JsonObject bc = doc["barcode"].as<JsonObject>();
    if (bc.isNull() && doc["barcodes"].is<JsonArray>()) bc = doc["barcodes"][0].as<JsonObject>();
    if (bc.isNull() || !bc["message"].is<const char*>()) { err = "no barcode in pass"; return false; }
    text = bc["message"].as<const char*>();
    const char* fmt = bc["format"] | "";
    if (strstr(fmt, "QR") == nullptr) printf("[pass] note: barcode format is %s (drawn as QR)\n", fmt);
    if (title.length() == 0) {
      const char* org = doc["organizationName"] | "";
      const char* desc = doc["description"] | "";
      // イベント名などがあれば優先 (eventTicket.primaryFields[0].value)
      const char* ev = nullptr;
      for (const char* kind : {"eventTicket", "generic", "coupon", "storeCard", "boardingPass"}) {
        JsonArray pf = doc[kind]["primaryFields"].as<JsonArray>();
        if (!pf.isNull() && pf.size() > 0 && pf[0]["value"].is<const char*>()) { ev = pf[0]["value"].as<const char*>(); break; }
      }
      if (ev && ev[0]) title = ev;
      else if (desc[0]) title = desc;
      else title = org;
      if (org[0] && title != org && title.length() < 18) title = String(org) + " " + title;
    }
    return true;
  }

  // ---------- 画像 → QR (quirc) ----------
  bool decodeImage(const uint8_t* img, size_t len, String& text, String& err) {
    const bool png = img[0] == 0x89;
    // 縮小して描く (iPhone のスクショは 828x1792 〜 1290x2796)。まず 1/2、読めなければ 3/4
    for (float scale : {0.5f, 0.75f}) {
      const int W = (int)(1300 * scale), H = (int)(2800 * scale);
      M5Canvas cv(nullptr);
      cv.setPsram(true);
      cv.setColorDepth(8);
      if (!cv.createSprite(W, H)) { err = "canvas oom"; return false; }
      cv.fillScreen(0xFF);
      const bool drawn = png ? cv.drawPng(img, len, 0, 0, W, H, 0, 0, scale, scale) : cv.drawJpg(img, len, 0, 0, W, H, 0, 0, scale, scale);
      if (!drawn) { cv.deleteSprite(); err = "image decode failed"; return false; }
      struct quirc* q = quirc_new();
      if (!q || quirc_resize(q, W, H) < 0) { if (q) quirc_destroy(q); cv.deleteSprite(); err = "quirc oom"; return false; }
      int qw, qh;
      uint8_t* gray = quirc_begin(q, &qw, &qh);
      for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
          const RGBColor c = cv.readPixelRGB(x, y);
          gray[y * W + x] = (uint8_t)((c.r * 77 + c.g * 150 + c.b * 29) >> 8);
        }
      }
      cv.deleteSprite();
      quirc_end(q);
      const int n = quirc_count(q);
      printf("[pass] quirc: scale %.2f found %d code(s)\n", scale, n);
      bool ok = false;
      for (int i = 0; i < n && !ok; ++i) {
        struct quirc_code code;
        struct quirc_data data;
        quirc_extract(q, i, &code);
        const quirc_decode_error_t e = quirc_decode(&code, &data);
        if (e == QUIRC_SUCCESS) { text = String((const char*)data.payload, data.payload_len); ok = true; }
        else printf("[pass] quirc decode error: %s\n", quirc_strerror(e));
      }
      quirc_destroy(q);
      if (ok) return true;
    }
    err = "no QR found in image";
    return false;
  }

  Preferences _prefs;
  Entry _entries[PASS_MAX];
  int _count = 0, _current = 0;
  // 受信
  WiFiServer _server{80};
  bool _receiving = false, _received = false, _apMode = false;
  State _state = State::Off;
  uint32_t _stateSince = 0, _lastActivity = 0;
  int _netIndex = 0;
  IPAddress _ip;
  char _lastError[64] = {0};
};
