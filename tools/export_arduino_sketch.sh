#!/usr/bin/env bash
# Arduino IDE で開けるスケッチフォルダを生成する (PlatformIO を使わない場合用)
#   ./tools/export_arduino_sketch.sh  →  arduino_sketch/M5StopWatchBadge/M5StopWatchBadge.ino
set -euo pipefail
cd "$(dirname "$0")/.."
out="arduino_sketch/M5StopWatchBadge"
rm -rf "$out"
mkdir -p "$out"
cp src/config.h src/orientation.h src/face.h src/sound.h "$out/"
# main.cpp を .ino に。Arduino IDE では <Arduino.h> は暗黙 include なので残しても問題ない。
cp src/main.cpp "$out/M5StopWatchBadge.ino"
echo "created: $out"
echo "Arduino IDE: ファイル > 開く > $out/M5StopWatchBadge.ino"
echo "ボード設定は README.md の『Arduino IDE でビルドする場合』を参照"
