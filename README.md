# M5Stack StopWatch デジタルバッジ 「まっくろくろすけ風の目」

M5Stack **StopWatch** (ESP32-S3R8 / 1.75" 円形 AMOLED / BMI270 IMU) をカバンに付けて使う
常時点灯デジタルバッジ用ファームウェアです。

- **どう回しても正立**: IMU の重力方向から顔の回転角を求め、連続的に回して表示 (90° スナップにも切替可)
- **キョロキョロ**: 大きな白目と黒目がランダムに視線を動かし、まばたき (時々二度まばたき)
- **眠る**: 動きが無いと 眠そう → 就寝 (Zzz) → 深い眠り (画面 OFF, deep sleep) と段階的に省電力
- **反応**: 振ると驚く、画面をタップすると喜ぶ (照れ)、触り続けると指を目で追う
- **音に反応** (マイク): 急に大きい音がすると驚いてキョロキョロ見回す (寝ていても起きる)。音楽が聞こえると目を閉じて音符を流す
- **かじる**: 口元に指を 2 秒以上置くと口が出てきて、大きく開けてパクッと閉じる (閉じる瞬間にバイブ)
- ボタンで明るさ / 向きの較正 / 回転方向の反転 / 自動就寝の ON-OFF。設定は NVS に保存

> 2026-09-03 実機で動作確認済み (顔の表示 / IMU による正立 / タッチ)。
> IMU の軸対応と画面中心のオフセットは実測値を `config.h` に反映してあります。
> 未確認の項目は「[到着後のチェックリスト](#到着後のチェックリスト)」を参照。

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

### 診断ファーム (画面ずれ / IMU 軸 / 描画経路の確認ツール)

```bash
pio run -e diag -t upload           # src/diag/diag.cpp だけをビルドして書き込む
pio run -e m5stopwatch -t upload    # 本番ファームに戻す
```

BtnB クリックでモードが切り替わります。シリアル (115200) に 0.5 秒毎 IMU 生値と補正量を出力します。

| モード | 内容 |
|---|---|
| ALIGN | 十字と円。画面をドラッグして縁にぴったり合わせる。表示される `off X Y` を `config.h` の `DISPLAY_OFFSET_X/Y` に入れる。BtnA クリック: 右へ 1px / BtnA 長押し: 下へ 1px / BtnB 長押し: リセット |
| FACE AA | 本番と同じ経路 (透過スプライト → 回転 → キャンバス) で目と毛を描き、IMU で回す。緑の三角 = 顔の上、緑の枠 = 顔スプライト境界、白の十字 = 画面中心 |
| FACE NOAA | 同上、アンチエイリアス無し |
| SOUND | マイクの音量バー (青) / 背景フロア (黄線) / 驚きしきい値 (赤線) / 平坦度 / 継続率 / 音楽判定。急な大きい音で `LOUD!`。`config.h` の `SOUND_*` を調整するときに使う |

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
| 口元 (目の下) に指を 2 秒以上 | 口が出てきてかじろうとする。離すと消える |
| 振る | 驚く |
| 急な大きい音 (手を叩く等) | 驚いてキョロキョロ見回す。寝ていても起きる |
| 音楽 (数秒続く音) | 目を閉じて音符を流す。止まると戻る |

設定 (回転方向 / 較正角 / 明るさ / 自動就寝) は NVS に保存され、電源を切っても残ります。

---

## 到着後のチェックリスト

実機が無いと確定できなかった点です。上から順に潰してください。

1. ~~**起動ログ**~~ 確認済み: `board_M5StopWatch`, `imu=6 (bmi270)`, PSRAM 8MB。
   (Arduino の `Serial` 出力は USB CDC に届かなかったため、ログは `printf` に変更済み)
2. ~~**画面が真っ暗**~~ 確認済み (文字も崩れない)。
3. ~~**顔が逆さま / 90° ズレ**~~ 実測で `ACC_TO_SCREEN_X/Y` を修正済み (USB 下で正立)。別の向きにしたければ **BtnB 長押し**。
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
| 音に反応する感度 | `SOUND_LOUD_*` (驚き), `SOUND_MUSIC_*` (音楽判定)。`SOUND_ENABLED = false` でマイクごと無効 |
| 口 | `MOUTH_TOUCH_HOLD_MS`, `MOUTH_ZONE_*` (判定範囲), `MOUTH_OFFSET_Y`, `MOUTH_MAX_OPEN` |
| 音符 | `NOTE_*`, `COLOR_NOTE` |

`tools/preview_face.py` (要 Pillow) で顔レイアウトの静止画プレビューを出せます (幾何を Python で再現したもので、実機描画そのものではありません)。

---

## 構成

```
platformio.ini        ビルド設定 (ESP32-S3, 16MB Flash, OPI PSRAM, USB CDC)
src/config.h          調整パラメータ
src/orientation.h     姿勢トラッカ (重力→回転角, 較正, 反転, スナップ) と動き検出
src/face.h            顔の描画と気分の状態機械 (起床 / 眠い / 就寝 / 驚き / 喜び / びっくり / 音楽 / かじる)
src/sound.h           音センサ (音量・背景フロア・スペクトル平坦度 → 急な大きい音 / 音楽の判定)
src/main.cpp          入力 (ボタン, タッチ, IMU), 電源管理 (輝度, CPU クロック, deep sleep), 描画ループ
src/diag/diag.cpp     診断ファーム (env:diag)。画面ずれ / IMU 軸 / 描画経路の確認
tools/export_arduino_sketch.sh   Arduino IDE 用スケッチを生成
tools/preview_face.py            顔レイアウトの PNG プレビュー
```

描画は「顔スプライト (380x380, PSRAM) に正立で描く → 回転して全画面キャンバス (468x468, PSRAM) に貼る → パネルへ転送」。
毛は全画面キャンバス側に顔と同じ角度で描いています。

## 音の判定について

マイクは ES8311 コーデック経由の **1 本 (モノラル)** なので、音の方向は分かりません。判定はすべて音量と簡易スペクトルです。

- **急な大きい音**: 音量 [dBFS] の背景フロア (ゆっくり追従) を持ち、「フロアより `SOUND_LOUD_ABOVE_FLOOR_DB` 以上大きく」「直前より `SOUND_LOUD_RISE_DB` 以上急に上がり」「絶対値が `SOUND_LOUD_MIN_DB` 以上」のとき。不応期 `SOUND_LOUD_REFRACTORY_MS`。音楽中は反応しない
- **音楽**: 次の 3 条件が `SOUND_MUSIC_ENTER_MS` 続いたら音楽。切れて `SOUND_MUSIC_EXIT_MS` 経つと解除
  1. 直近 `SOUND_MUSIC_WINDOW_MS` の間に音がしている割合 (`act`) が `SOUND_MUSIC_ACTIVE_RATIO` 以上 (しゃべり声は途切れが多い)
  2. 音量の時間変動 (直近 1 秒の標準偏差 `std` [dB]) が `SOUND_MUSIC_LEVEL_STD_MIN..MAX` の範囲 (ファン等の定常ノイズは小さく、しゃべり声は大きい)
  3. スペクトル平坦度 `sfm` (マイクの固定特性を長期平均で割ってから計算。雑音=1, トーン=0) が `SOUND_MUSIC_FLATNESS_MAX` 未満
- **マイクの感度**: M5Unified は ES8311 のアナログゲインを 0dB にするので `SOUND_ES8311_PGA_GAIN` で上げている (既定 12dB)。無音時の自己ノイズが -28dBFS 程度あるので、それより十分大きい音でないと「音がしている」にならない

### 音の較正 (`tools/sound_calib.py`)

本番ファームは音がしている間 1 秒毎に `[snd] level .. floor .. sfm .. act .. std ..` をシリアルに出します。
`tools/sound_calib.py` は PC のスピーカーから 破裂音 / 純音 / 合成音声 / ホワイトノイズ / 和音 / ドラム入り を順に鳴らし、
刺激ごとに値を集計します (`-v` で全行表示)。

```bash
python tools/sound_calib.py        # COM6 固定。pyserial が必要 (PlatformIO と一緒に入る)
```

ただし PC スピーカーの音は端末から離れると自己ノイズと同程度にしかならないので、実際の調整は
**手を叩く / 近くで話す / スマホで音楽を流す** など実環境の音源で `[snd]` ログか診断ファームの SOUND モードを見て行うのが確実です。

## 参考

- [StopWatch 製品ドキュメント (m5-docs)](https://docs.m5stack.com/en/core/StopWatch)
- [StopWatch 工場出荷ファーム説明](https://docs.m5stack.com/en/guide/display_device/stopwatch/usage)
- [M5Unified](https://github.com/m5stack/M5Unified) / [M5GFX](https://github.com/m5stack/M5GFX)
