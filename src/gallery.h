// ギャラリー: 円形画面に画像を全画面表示する。
//   - 画像は内蔵フラッシュのデータ領域 (LittleFS, 3.5MB) の /gallery に JPEG / PNG で置く
//   - チケットと同じ HTTP サーバで受け取る (POST /image)
//   - 画像は一度 PSRAM のスプライトに展開し、顔と同じように IMU の角度で回して貼る
//     (バッジをどう持っても絵が正立する)
#pragma once
#include <M5Unified.h>
#include <LittleFS.h>
#include <vector>
#include "config.h"

class Gallery {
public:
  bool begin() {
    _mounted = LittleFS.begin(false, GALLERY_MOUNT_POINT);
    if (!_mounted) {
      // 初回はフォーマットが要る
      _mounted = LittleFS.format() && LittleFS.begin(false, GALLERY_MOUNT_POINT);
      printf("[gallery] formatted: %d\n", (int)_mounted);
    }
    if (_mounted && !LittleFS.exists(GALLERY_DIR)) LittleFS.mkdir(GALLERY_DIR);
    scan();
    printf("[gallery] %s, %u images\n", _mounted ? "mounted" : "NOT MOUNTED", (unsigned)_files.size());
    return _mounted;
  }

  bool mounted() const { return _mounted; }
  int count() const { return (int)_files.size(); }
  int current() const { return _index; }
  const char* currentName() const { return _files.empty() ? "" : _files[_index].c_str(); }

  void next() { if (_files.size() > 1) { _index = (_index + 1) % _files.size(); _dirty = true; } }
  void prev() { if (_files.size() > 1) { _index = (int)((_index + _files.size() - 1) % _files.size()); _dirty = true; } }
  void markDirty() { _dirty = true; }

  // 保存されている画像を数え直す
  void scan() {
    _files.clear();
    if (!_mounted) return;
    File dir = LittleFS.open(GALLERY_DIR);
    if (!dir) return;
    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
      if (f.isDirectory()) continue;
      String n(f.name());
      String low = n; low.toLowerCase();
      if (low.endsWith(".jpg") || low.endsWith(".jpeg") || low.endsWith(".png")) _files.push_back(n);
    }
    if (_index >= (int)_files.size()) _index = 0;
    _dirty = true;
  }

  // 受け取った画像を保存する。true なら成功
  bool save(const uint8_t* data, size_t len, const char* suggestedName) {
    if (!_mounted || len < 16 || len > GALLERY_MAX_BYTES) return false;
    const bool png = data[0] == 0x89 && data[1] == 'P';
    const bool jpg = data[0] == 0xFF && data[1] == 0xD8;
    if (!png && !jpg) return false;
    if ((int)_files.size() >= GALLERY_MAX_FILES) {                 // 一杯なら一番古いものを消す
      String victim = String(GALLERY_DIR) + "/" + _files.front();
      LittleFS.remove(victim);
    }
    char path[64];
    String base(suggestedName && suggestedName[0] ? suggestedName : "image");
    base.replace("/", "_"); base.replace(" ", "_");
    if (base.length() > 24) base = base.substring(0, 24);
    snprintf(path, sizeof(path), "%s/%s_%lu.%s", GALLERY_DIR, base.c_str(),
             (unsigned long)(millis() / 1000), png ? "png" : "jpg");
    File f = LittleFS.open(path, FILE_WRITE);
    if (!f) return false;
    const size_t wrote = f.write(data, len);
    f.close();
    if (wrote != len) { LittleFS.remove(path); return false; }
    printf("[gallery] saved %s (%u bytes)\n", path, (unsigned)len);
    scan();
    for (size_t i = 0; i < _files.size(); ++i) {                    // 保存したものを表示する
      if (String(path).endsWith(_files[i])) { _index = (int)i; break; }
    }
    _dirty = true;
    return true;
  }

  void removeCurrent() {
    if (_files.empty()) return;
    String path = String(GALLERY_DIR) + "/" + _files[_index];
    LittleFS.remove(path);
    printf("[gallery] deleted %s\n", path.c_str());
    scan();
  }

  // 表示中の画像をスプライトに展開する (変わったときだけ)
  void ensureLoaded() {
    if (!_dirty) return;
    if (_img.width() == 0) {
      _img.setPsram(true);
      _img.setColorDepth(16);
      if (!_img.createSprite(GALLERY_SIZE, GALLERY_SIZE)) { puts("[gallery] sprite alloc failed"); return; }
      _img.setPivot(GALLERY_SIZE / 2.0f, GALLERY_SIZE / 2.0f);
    }
    _dirty = false;
    _img.fillScreen(TFT_BLACK);
    _img.setTextDatum(middle_center);
    _img.setTextSize(1);
    const int c = GALLERY_SIZE / 2;

    if (_files.empty()) {
      _img.setFont(&fonts::lgfxJapanGothic_20);
      _img.setTextColor(COLOR_UI);
      _img.drawString("画像がありません", c, c - 16);
      _img.setFont(&fonts::lgfxJapanGothic_16);
      _img.drawString("タップで受信 / 上スワイプで戻る", c, c + 20);
      return;
    }
    // M5GFX は LittleFS を直接扱えないので、いったん PSRAM に読み込んでから展開する
    String path = String(GALLERY_DIR) + "/" + _files[_index];
    String low = path; low.toLowerCase();
    bool ok = false;
    File f = LittleFS.open(path, FILE_READ);
    if (f) {
      const size_t len = f.size();
      uint8_t* buf = (len && len <= GALLERY_MAX_BYTES) ? (uint8_t*)ps_malloc(len) : nullptr;
      if (buf && f.read(buf, len) == (int)len) {
        ok = low.endsWith(".png") ? _img.drawPng(buf, len, 0, 0, GALLERY_SIZE, GALLERY_SIZE)
                                  : _img.drawJpg(buf, len, 0, 0, GALLERY_SIZE, GALLERY_SIZE);
      }
      free(buf);
      f.close();
    }
    if (!ok) {
      _img.setFont(&fonts::Font2);
      _img.setTextColor(COLOR_LEVER_MUTE);
      _img.drawString("decode failed", c, c);
    }
  }

  // IMU の角度で回して貼る (顔と同じ扱いなので、どう持っても絵が正立する)
  void pushRotated(LovyanGFX* dst, float angle, float cx, float cy) {
    ensureLoaded();
    if (_img.width() == 0) return;
    _img.pushRotateZoom(dst, cx, cy, angle, 1.0f, 1.0f);
  }

private:
  M5Canvas _img;
  bool _mounted = false, _dirty = true;
  int _index = 0;
  std::vector<String> _files;
};
