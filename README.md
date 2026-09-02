# M5Stack StopWatch デジタルバッジ 「まっくろくろすけ風の目」

M5Stack **StopWatch** (ESP32-S3R8 / 1.75" 円形 AMOLED / BMI270 IMU) をカバンに付けて使う
常時点灯デジタルバッジ用ファームウェアです。

- **どう回しても正立**: IMU の重力方向から顔の回転角を求め、連続的に回して表示 (90° スナップにも切替可)
- **キョロキョロ**: 大きな白目と黒目がランダムに視線を動かし、まばたき (時々二度まばたき)
- **眠る**: 動きが無いと 眠そう → 就寝 (Zzz) → 深い眠り (画面 OFF, deep sleep) と段階的に省電力
- **反応**: 振ると驚く、画面をタップすると喜ぶ (照れ)、触り続けると指を目で追う
- ボタンで明るさ / 向きの較正 / 回転方向の反転 / 自動就寝の ON-OFF。設定は NVS に保存

> 実機到着前に書いたコードです。**コンパイルは通っています**が実機動作は未確認です。
> 到着後に確認すべき点を「[到着後のチェックリスト](#到着後のチェックリスト)」にまとめました。

---

## ハードウェア (M5Unified / M5GFX のソースから確認した情報)

| 項目 | 内容 |
|---|---|
| SoC | ESP32-S3R8 (16MB Flash, 8MB **Octal** PSRAM) |
| 画面 | CO5300 AMOLED, QSPI (SCLK G40 / IO0-3 G41,G42,G46,G45 / CS G39 / TE G38), パネル 468x468 (物理 466x466) |
| タッチ | CST820 (CST816S 互換ドライバ) I2C 0x15, INT G13 |
| IMU | BMI270 I2C 0x68 (内部 I2C: SDA G47 / SCL G48) |
| RTC | RX8130CE I2C 0x32 |
| 電源 IC | M5PM1 (充電状態は PM1_G2)、IO エキスパンダ M5IOE1 (I2C 0x4F) が画面電源 / オーディオ / モーターを制御 |
| ボタン | **BtnA = GPIO2, BtnB = GPIO1** (アクティブ Low)。物理的にどのボタンかは実機で確認 |
| 振動モーター | M5IOE1 の PWM1 (IO9) → `M5.Power.setVibration()` |
| ライブラリ | M5Unified **0.2.21** 以上 / M5GFX **0.2.28** 以上 (`board_M5StopWatch` として自動認識) |

M5GFX は OPI-PSRAM が有効なときだけ AMOLED 用フレームバッファを確保します。
無効だと「奇数座標の描画が乱れる」警告が出て文字が崩れるので、ビルド設定の `qio_opi` は必須です。

---

## ビルドと書き込み (PlatformIO)

```bash
pip install platformio            # 未導入なら
pio run                           # ビルド
pio run -t upload                 # USB-C で接続して書き込み
pio device monitor                # シリアルログ (115200bps, USB CDC)
```

- `platformio.ini` は pioarduino 版 platform-espressif32 (Arduino core 3.2.1 / ESP-IDF 5.4) を使用
- ライブラリは GitHub のタグ (M5GFX 0.2.28 / M5Unified 0.2.21) から取得。レジストリが使えるなら `m5stack/M5Unified@^0.2.21` 形式でも可
- 書き込みポートが見つからないときは、**ボタンを押しながら USB を挿す**とダウンロードモードに入ります (工場出荷ファームのマニュアルの手順に従ってください)

### Arduino IDE でビルドする場合

```bash
./tools/export_arduino_sketch.sh   # arduino_sketch/M5StopWatchBadge/ を生成
```

1. ボードマネージャで M5Stack のボードパッケージ (最新) を入れる。`M5StopWatch` があればそれを選択
2. 無ければ `ESP32S3 Dev Module` を選び、**PSRAM: OPI PSRAM / Flash Size: 16MB / Partition: 16MB (default) / USB CDC On Boot: Enabled / USB Mode: Hardware CDC and JTAG**
3. ライブラリマネージャで **M5Unified 0.2.21 以上** (M5GFX 0.2.28 以上が一緒に入る)
4. `arduino_sketch/M5StopWatchBadge/M5StopWatchBadge.ino` を開いて書き込み

---

## 操作

| 操作 | 動作 |
|---|---|
| BtnA クリック | ステータス表示 5 秒 (電池 %, 充電, 角度, IMU 生値, 設定) |
| BtnA ダブルクリック | **回転方向を反転** (傾けたとき顔が逆に回るならこれ) |
| BtnA 長押し (1.2s) | 明るさ 4 段階を切替 |
| BtnB クリック | 起こす |
| BtnB ダブルクリック | 自動就寝の ON / OFF (OFF なら常に起きている) |
| BtnB 長押し (1.2s) | **今の向きを「正立」として記憶** (画面を立てて、上にしたい方を上にして押す) |
| 画面タップ | 喜ぶ (^ ^ と照れ) |
| 画面を触り続ける | 指を目で追う |
| 振る | 驚く |

設定 (回転方向 / 較正角 / 明るさ / 自動就寝) は NVS に保存され、電源を切っても残ります。

---

## 到着後のチェックリスト

実機が無いと確定できなかった点です。上から順に潰してください。

1. **起動ログ**: `pio device monitor` で `[Autodetect] board_M5StopWatch` と `[boot] ... imu=6 (bmi270)` が出るか。
   `imu=0` なら IMU 未検出 → 顔は回らない。
2. **画面が真っ暗**: `M5.Display.setBrightness` が効いていない可能性。`config.h` の `BRIGHTNESS_LEVELS` を上げる。
   文字が崩れる → ビルド設定 (`qio_opi`) を確認。
3. **顔が逆さま / 90° ズレ**: 画面を立てて上にしたい向きにして **BtnB 長押し**。
4. **傾けると逆に回る**: **BtnA ダブルクリック**で反転。(較正の前後どちらでも可)
5. **ボタンの物理配置**: どれが BtnA(GPIO2)/BtnB(GPIO1) か確認。逆が良ければ `main.cpp` の `handleButtons()` で入れ替え。
6. **fps**: ステータス表示中に動きがカクつくなら `config.h` の `FACE_ANTIALIAS = false`、それでも重ければ `FACE_SIZE` を小さく。
7. **タッチ座標**: 触った場所を目が追うか。ズレるなら `main.cpp` のタッチ→顔座標変換の `R` を調整。
8. **深い眠り**: 90 秒静止で寝る → 30 分後に画面 OFF → ボタンで復帰 することを確認。
   画面 OFF は「輝度 0 + IO エキスパンダで OLED/タッチのリセットを assert」で実現しているので、
   復帰後に画面やタッチが死んでいたら `main.cpp` の `enterDeepSleep()` のリセット部分を外す。
   復帰しない場合は `config.h` の `DEEP_SLEEP_AFTER_MS = 0` で無効化 (電池は減りやすくなる)。
9. **電池の持ち**: 下記参照。実測してから `DROWSY_AFTER_MS` 等を調整。

---

## 電池について (率直な見積り)

480mAh に対し、ESP32-S3 を 240MHz で回しつつ AMOLED を常時点灯すると **おそらく 4〜8 時間**です。
「カバンに付けて一日中」は、そのままでは厳しい可能性が高いです。このファームでは次で延命しています。

- 背景が黒 (AMOLED は黒画素が消灯するので消費が小さい)
- 動きが無いと 眠り顔 (fps↓, 輝度↓, CPU 80MHz) → 30 分で deep sleep (µA オーダー)
- deep sleep 中は 5 分毎に 1.5 秒だけ起きて IMU を見る。動いていれば復帰、静止なら寝直す

歩いているとき (人に見せたいとき) は起きていて、置きっぱなしなら寝る、という設計です。
それでも足りなければ `BRIGHTNESS_LEVELS` を下げる / `FRAME_INTERVAL_AWAKE_MS` を 50 に、が効きます。
本当に丸一日必要ならモバイルバッテリーを USB-C に繋ぐのが確実です。

---

## 指が近づくのを感じる (近接検知)

**結論: 本体だけでは無理です。** StopWatch には近接センサー (IR / ToF) が無く、
タッチパネル (CST820) はホバーを返さないので「触れた」瞬間しか分かりません。
ただし、小さな電極を 1 本足せば「数 cm 手前で気づく」は実現できます。

| 方式 | 精度 / 距離 | 追加ハード | 見た目 | 状態 |
|---|---|---|---|---|
| **A. 静電容量 (ESP32-S3 内蔵タッチ)** | 1〜5cm、手の大きさ・電極依存 | 銅箔テープ or 針金 1 本 | ほぼ変わらない | **実装済 (要電極)** |
| B. ToF ユニット (M5 Unit ToF / ToF4M) を Grove に接続 | 2cm〜数 m、方向も分かる | Unit 1 個 + ケーブル | 横に箱が出っ張る | 未実装 (簡単) |
| C. 本体タッチのみ | 接触した瞬間だけ | 無し | - | 現状 |

### A の作り方

1. Grove (HY2.0-4P) の **SDA ピン (GPIO10)** に線を出す。Grove ケーブルを半分に切って白線 (SDA) を使うのが楽
   (ピン並びは GND / 5V / SDA / SCL。実機で確認すること)
2. 線の先に銅箔テープ (ベゼル裏に一周) か、ケース内に這わせた針金を繋ぐ。面積が大きいほど遠くから反応
3. `src/config.h` の `PROXIMITY_ENABLED = true` にしてビルド
4. BtnA クリックのステータス表示に `prox 生値/基準値 r比率 lv` が出るので、指を近づけて比率がどれだけ上がるか見る
5. 上がり幅に合わせて `PROXIMITY_NEAR_RATIO` (反応開始) と `PROXIMITY_CLOSE_RATIO` (触れる直前) を調整
6. 環境が変わって誤反応するときは BtnB クリックで基準値を取り直す (自動でもゆっくり追従する)

反応: 近づくと目を見開いて瞳孔が開き、じっと正面を見る。触れる直前は少し身構えて細目になる。
寝ていても手が伸びてくれば起きる。タッチすると今まで通り喜ぶ。

注意: 電極は Grove の I2C ピンを使うので、Grove に I2C ユニットを挿すのと排他。方向 (どこから来たか) は分からない。
B の ToF 方式が欲しくなったら `proximity.h` と同じインターフェースで差し替えられる作りにしてある。

---

## 調整箇所

ほぼ全て `src/config.h` に集約しています。

| 変えたいこと | パラメータ |
|---|---|
| IMU 軸と画面軸の対応 | `ACC_TO_SCREEN_X / Y` (ただし通常は較正+反転で足りる) |
| 回転の追従の速さ / ぬるぬる感 | `ORIENT_FILTER_TAU_SEC`, `ORIENT_DEADBAND_DEG` |
| 90° 単位にしたい | `ORIENT_SNAP_90 = true` |
| 目の大きさ・間隔 | `EYE_RADIUS`, `PUPIL_RADIUS`, `EYE_OFFSET_X` |
| 毛の量 | `FUR_COUNT`, `FUR_MIN_LEN`, `FUR_MAX_LEN` |
| キョロキョロの頻度 | `GAZE_HOLD_MIN/MAX_MS`, `GAZE_SPEED` |
| まばたき | `BLINK_INTERVAL_*`, `BLINK_DURATION_MS`, `DOUBLE_BLINK_PROBABILITY` |
| 眠くなるまでの時間 | `DROWSY_AFTER_MS`, `SLEEP_AFTER_MS`, `DEEP_SLEEP_AFTER_MS` |
| 動き / 振り の感度 | `MOTION_*`, `SHAKE_*` |
| 明るさ | `BRIGHTNESS_LEVELS`, `SLEEP_BRIGHTNESS` |
| 近接検知 | `PROXIMITY_ENABLED`, `PROXIMITY_TOUCH_PIN`, `PROXIMITY_NEAR_RATIO`, `PROXIMITY_CLOSE_RATIO` |

`tools/preview_face.py` (要 Pillow) で顔レイアウトの静止画プレビューを出せます (幾何を Python で再現したもので、実機描画そのものではありません)。

---

## 構成

```
platformio.ini        ビルド設定 (ESP32-S3, 16MB Flash, OPI PSRAM, USB CDC)
src/config.h          調整パラメータ
src/orientation.h     姿勢トラッカ (重力→回転角, 較正, 反転, スナップ) と動き検出
src/face.h            顔の描画と気分の状態機械 (起床 / 眠い / 就寝 / 驚き / 喜び / 近づくものを見る)
src/proximity.h       近接検知 (ESP32-S3 タッチセンサー + 外付け電極)
src/main.cpp          入力 (ボタン, タッチ, IMU), 電源管理 (輝度, CPU クロック, deep sleep), 描画ループ
tools/export_arduino_sketch.sh   Arduino IDE 用スケッチを生成
tools/preview_face.py            顔レイアウトの PNG プレビュー
```

描画は「顔スプライト (380x380, PSRAM) に正立で描く → 回転して全画面キャンバス (468x468, PSRAM) に貼る → パネルへ転送」。
毛は全画面キャンバス側に顔と同じ角度で描いています。

## 参考

- [StopWatch 製品ドキュメント (m5-docs)](https://docs.m5stack.com/en/core/StopWatch)
- [StopWatch 工場出荷ファーム説明](https://docs.m5stack.com/en/guide/display_device/stopwatch/usage)
- [M5Unified](https://github.com/m5stack/M5Unified) / [M5GFX](https://github.com/m5stack/M5GFX)
