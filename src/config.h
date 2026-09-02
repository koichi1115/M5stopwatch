// M5Stack StopWatch デジタルバッジ - 調整用パラメータ
// 実機で挙動を見ながらここだけ触れば大体のことは変えられるようにしてあります。
#pragma once
#include <stdint.h>

// ============================================================
// 1. 姿勢 (クルクル回しても正立を保つ)
// ============================================================
// IMU (BMI270) の軸 → 画面の軸 の対応。
// 画面右方向を +X、画面下方向を +Y とする。
// 実機到着前なので暫定。ズレは「向きの較正 (BtnB 長押し)」と
// 「回転方向の反転 (BtnA ダブルクリック)」で再ビルド無しに吸収できる。
#define ACC_TO_SCREEN_X(ax, ay, az) ((ax))
#define ACC_TO_SCREEN_Y(ax, ay, az) ((ay))

// 角度の平滑化の時定数 [秒]。小さいほど機敏、大きいほどヌルっと追従。
constexpr float ORIENT_FILTER_TAU_SEC = 0.22f;
// これ以下の角度変化は無視する (静止時のプルプル防止) [度]
constexpr float ORIENT_DEADBAND_DEG = 0.6f;
// 画面面内の重力成分がこれ未満 [g] のとき (＝画面が上/下を向いて寝ている) は角度を更新しない
constexpr float ORIENT_MIN_INPLANE_G = 0.30f;
// true にすると 0/90/180/270 度にスナップ (ヒステリシス付き)。false で連続回転。
constexpr bool ORIENT_SNAP_90 = false;
constexpr float ORIENT_SNAP_HYSTERESIS_DEG = 12.0f;

// ============================================================
// 2. 目・顔の見た目
// ============================================================
constexpr int FACE_SIZE = 380;          // 顔スプライトの一辺 [px] (回転して画面に貼る)
constexpr int EYE_OFFSET_X = 84;        // 両目の中心からの横オフセット
constexpr int EYE_OFFSET_Y = 4;         // 目の縦位置 (+で下)
constexpr int EYE_RADIUS = 70;          // 白目の半径
constexpr int PUPIL_RADIUS = 34;        // 黒目の半径
constexpr float PUPIL_TRAVEL = 0.80f;   // 黒目が動ける範囲 (白目半径-黒目半径 に対する比率)
constexpr uint16_t COLOR_EYE_WHITE = 0xFFFF;
constexpr uint16_t COLOR_PUPIL = 0x0000;
constexpr uint16_t COLOR_FUR = 0x31A6;   // 毛の色 (暗いグレー)
constexpr uint16_t COLOR_FUR_TIP = 0x528A;
constexpr int FUR_COUNT = 120;           // 縁の毛の本数
constexpr int FUR_MIN_LEN = 12;
constexpr int FUR_MAX_LEN = 30;
constexpr uint16_t COLOR_BLUSH = 0xFB56; // 照れ (ピンク)
constexpr uint16_t COLOR_ZZZ = 0x9CD3;
constexpr uint16_t COLOR_UI = 0xC618;
constexpr uint16_t COLOR_UI_ACCENT = 0x07FF;
constexpr uint16_t COLOR_TRANSPARENT = 0x0001; // 顔スプライトの透過キー (ほぼ黒。描画には使わない)
// 顔を回転して貼るときにアンチエイリアスをかける (きれいだが重い)。fps が足りなければ false に。
constexpr bool FACE_ANTIALIAS = true;

// ============================================================
// 3. 目の動き (キョロキョロ) とまばたき
// ============================================================
constexpr uint32_t GAZE_HOLD_MIN_MS = 500;    // 視線を固定する時間の下限
constexpr uint32_t GAZE_HOLD_MAX_MS = 2600;   // 上限
constexpr float GAZE_SPEED = 14.0f;           // 視線移動の速さ (大きいほど速い)
constexpr uint32_t BLINK_INTERVAL_MIN_MS = 1800;
constexpr uint32_t BLINK_INTERVAL_MAX_MS = 6000;
constexpr uint32_t BLINK_DURATION_MS = 170;
constexpr float DOUBLE_BLINK_PROBABILITY = 0.22f;

// ============================================================
// 4. 眠り (動きが無いと眠くなる → 寝る → 深い眠り)
// ============================================================
constexpr uint32_t DROWSY_AFTER_MS = 45UL * 1000;        // 動きなしでこれだけ経つと眠そうに
constexpr uint32_t SLEEP_AFTER_MS = 90UL * 1000;         // さらに経つと寝る
constexpr uint32_t DEEP_SLEEP_AFTER_MS = 30UL * 60 * 1000; // 寝たまま動きが無ければ画面を消して深い眠り (0 で無効)
constexpr uint32_t DEEP_SLEEP_MOTION_CHECK_SEC = 300;    // 深い眠り中、この間隔で起きて動きを確認 (0 でボタンのみ)
constexpr uint32_t DEEP_SLEEP_MOTION_CHECK_WINDOW_MS = 1500;
constexpr bool LOW_CPU_CLOCK_WHILE_SLEEPING = true;      // 寝ている間は CPU を 80MHz に落とす

// 動き検出のしきい値
constexpr float MOTION_ACCEL_THRESHOLD_G = 0.10f;   // |加速度| が 1g からこれ以上ズレたら「動いた」
constexpr float MOTION_GYRO_THRESHOLD_DPS = 25.0f;  // ジャイロがこれ以上なら「動いた」
constexpr float SHAKE_THRESHOLD_G = 1.10f;          // 「振られた!」判定 (1g からのズレ)
constexpr uint8_t SHAKE_COUNT = 3;                  // 短時間にこの回数超えたら驚く
constexpr uint32_t SHAKE_WINDOW_MS = 700;

// ============================================================
// 5. 明るさ・電源
// ============================================================
constexpr uint8_t BRIGHTNESS_LEVELS[] = {28, 70, 130, 210};
constexpr uint8_t DEFAULT_BRIGHTNESS_INDEX = 1;
constexpr uint8_t SLEEP_BRIGHTNESS = 18;   // 寝ている間の明るさ
constexpr uint8_t DROWSY_BRIGHTNESS_SCALE_PERCENT = 70;

constexpr uint32_t FRAME_INTERVAL_AWAKE_MS = 33;   // ~30fps
constexpr uint32_t FRAME_INTERVAL_DROWSY_MS = 50;  // ~20fps
constexpr uint32_t FRAME_INTERVAL_SLEEP_MS = 100;  // ~10fps

constexpr uint32_t STATUS_OVERLAY_MS = 5000;       // ステータス表示の継続時間
constexpr uint32_t BUTTON_HOLD_MS = 1200;          // 長押し判定

// 起動時の挨拶 (バイブ) を鳴らすか
constexpr bool VIBRATE_ON_BOOT = true;

// ============================================================
// 6. 近接検知 (指が近づいたのを感じる) - 要: Grove ポートに電極を接続
// ============================================================
// 本体に近接センサーは無い。ESP32-S3 のタッチセンサーを Grove の空きピンで使う。
// 電極を繋いでから true にする。詳細は README「指が近づくのを感じる」。
constexpr bool PROXIMITY_ENABLED = false;
constexpr uint8_t PROXIMITY_TOUCH_PIN = 10;        // Grove (HY2.0-4P) の SDA ピン = GPIO10 (T10)。SCL 側なら 11
constexpr uint32_t PROXIMITY_READ_INTERVAL_MS = 20;
constexpr float PROXIMITY_NEAR_RATIO = 1.015f;      // 基準値からこの比率以上で「近い」(電極次第。ステータス表示で ratio を見て調整)
constexpr float PROXIMITY_CLOSE_RATIO = 1.06f;      // この比率で「触れる直前」
constexpr float PROXIMITY_BASELINE_TRACK = 0.01f;   // 基準値の追従速度 (1 回の読み取りあたり)

