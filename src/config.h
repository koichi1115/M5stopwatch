// M5Stack StopWatch デジタルバッジ - 調整用パラメータ
// 実機で挙動を見ながらここだけ触れば大体のことは変えられるようにしてあります。
#pragma once
#include <stdint.h>

// ============================================================
// 1. 姿勢 (クルクル回しても正立を保つ)
// ============================================================
// IMU (BMI270) の軸 → 画面の軸 の対応。
// 画面右方向を +X、画面下方向を +Y とする。
// 実測 (2026-09-03): USB を下にして立てると ax=-1.0, 画面を上に寝かせると az=+1.0。
//   → 画面下 = -ax, 画面右 = -ay (右手系から導出。逆に回るなら BtnA ダブルクリックで反転)
// 残りのズレは「向きの較正 (BtnB 長押し)」で再ビルド無しに吸収できる。
#define ACC_TO_SCREEN_X(ax, ay, az) (-(ay))
#define ACC_TO_SCREEN_Y(ax, ay, az) (-(ax))

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
// 画面の描画中心の補正 [px]。診断ファーム (env:diag) でドラッグして合わせた値。実測 2026-09-03
constexpr int DISPLAY_OFFSET_X = -2;
constexpr int DISPLAY_OFFSET_Y = -1;
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

// 口 (口元に指を置き続けると出てきて、かじろうとする)
constexpr uint32_t MOUTH_TOUCH_HOLD_MS = 2000;   // 口元に触れ続けてから口が出るまで
constexpr int MOUTH_OFFSET_Y = 112;              // 口の中心 (顔中心から下へ)
constexpr int MOUTH_HALF_W = 120;                // 全開時の口の半幅 (トトロのあくび級)
constexpr int MOUTH_MAX_OPEN = 150;              // 最大の開き (高さ)
constexpr int MOUTH_ZONE_HALF_W = 110;           // 「口元」と判定する範囲 (顔ローカル, px)
constexpr int MOUTH_ZONE_TOP = 50;
constexpr int MOUTH_ZONE_BOTTOM = 190;
constexpr uint16_t COLOR_MOUTH = 0x6000;         // 口の中 (暗い赤)
constexpr uint16_t COLOR_MOUTH_LINE = 0xC618;
constexpr uint16_t COLOR_TONGUE = 0xF98E;
constexpr uint16_t COLOR_TEETH = 0xFFFF;

// 音符 (音楽を聴いているとき)
constexpr int NOTES_MAX = 4;
constexpr float NOTE_LIFE_SEC = 2.4f;
constexpr uint32_t NOTE_SPAWN_MS = 520;
constexpr float NOTE_RISE_PX = 150.0f;
constexpr uint16_t COLOR_NOTE = 0xFFE0;          // 黄

// 驚いてキョロキョロする時間
constexpr uint32_t STARTLE_DURATION_MS = 2600;
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
// 6. 音 (マイク)。診断ファームの SOUND モードで値を見ながら調整する
// ============================================================
constexpr bool SOUND_ENABLED = true;
constexpr uint32_t SOUND_SAMPLE_RATE = 16000;
constexpr int SOUND_BLOCK_SAMPLES = 512;         // 32ms
constexpr uint8_t SOUND_MIC_GAIN = 16;           // M5Unified の magnification (デジタル)
constexpr uint8_t SOUND_ES8311_PGA_GAIN = 4;     // ES8311 のアナログ PGA ゲイン 0..10 (3dB 刻み, 4 = 12dB)。M5Unified 既定は 0
constexpr float SOUND_FLOOR_RISE_RATE = 0.005f;         // 背景フロアが上がる速さ (静かなとき, 1 ブロックあたり)
constexpr float SOUND_FLOOR_RISE_RATE_ACTIVE = 0.0002f; // 音がしている間 (長い曲でもフロアが追いつかないよう極小)
// 急な大きい音
constexpr float SOUND_LOUD_MIN_DB = -24.0f;          // これより小さい音では驚かない [dBFS]
constexpr float SOUND_LOUD_ABOVE_FLOOR_DB = 20.0f;   // 背景よりこれだけ大きい
constexpr float SOUND_LOUD_RISE_DB = 12.0f;          // 直前よりこれだけ急に上がった
constexpr uint32_t SOUND_LOUD_REFRACTORY_MS = 6000;  // 連続で驚かない時間
// 自分の出す音・衝撃で驚かないための抑止 (マイクは筐体内なのでバイブやタッチ、持ち替えの衝撃を拾う)
constexpr uint32_t SOUND_SUPPRESS_AFTER_VIBRATION_MS = 400;  // バイブ終了後
constexpr uint32_t SOUND_SUPPRESS_AFTER_TOUCH_MS = 500;      // タッチ / ボタン操作後
constexpr uint32_t SOUND_SUPPRESS_MOTION_MS = 400;           // IMU が動きを検知してからこの間の大きい音は「衝撃」とみなして無視
// 音楽
constexpr float SOUND_ACTIVE_ABOVE_FLOOR_DB = 6.0f;  // 「音がしている」判定
constexpr float SOUND_ACTIVE_MIN_DB = -55.0f;
constexpr uint32_t SOUND_MUSIC_WINDOW_MS = 4000;     // 継続性を見る窓
constexpr float SOUND_MUSIC_ACTIVE_RATIO = 0.85f;    // 窓の中でこれ以上の割合で音が続いている
constexpr float SOUND_MUSIC_FLATNESS_MAX = 0.50f;    // スペクトル平坦度 (ホワイトニング後) がこれ未満 (トーン成分が多い)
constexpr float SOUND_WHITEN_RATE = 0.0005f;         // 長期平均スペクトルの更新速度 (1 ブロックあたり。~64 秒)
constexpr float SOUND_MUSIC_LEVEL_STD_MIN = 1.0f;    // 音量の時間変動 [dB] がこの範囲なら音楽らしい (定常ノイズは小さく、しゃべり声は大きい)
constexpr float SOUND_MUSIC_LEVEL_STD_MAX = 6.0f;
constexpr uint32_t SOUND_MUSIC_ENTER_MS = 3000;      // 条件がこれだけ続いたら音楽
constexpr uint32_t SOUND_MUSIC_EXIT_MS = 2000;       // 条件が切れてこれだけ経ったら解除

// ============================================================
// 7. BLE キーボード (PC にペアリングしてショートカットを送る)
// ============================================================
constexpr bool HID_ENABLED = true;
constexpr const char* HID_DEVICE_NAME = "Makkuro Badge";
// 修飾キー: 0x01 Ctrl / 0x02 Shift / 0x04 Alt / 0x08 Win
// ミュート切替 (画面のレバー): Teams の Alt+A (Teams のウィンドウにフォーカスがあること)。
//   Windows 11 の通話ミュート Win+Alt+K (mods 0x0C, key 0x0E) は「対応アプリなし」になる環境があった
constexpr uint8_t HID_MUTE_MODS = 0x04;
constexpr uint8_t HID_MUTE_KEY = 0x04;           // A
// BtnA クリック: Alt+Tab (直前のウィンドウと切り替え)
constexpr uint8_t HID_BTN_A_MODS = 0x04;
constexpr uint8_t HID_BTN_A_KEY = 0x2B;          // Tab
// BtnB クリック: 無変換 (音声入力)。Win+H にするなら mods 0x08, key 0x0B
constexpr uint8_t HID_BTN_B_MODS = 0x00;
constexpr uint8_t HID_BTN_B_KEY = 0x8B;          // 無変換
// ミュートレバー (顔ローカル座標, px)。PC と接続中だけ表示
constexpr int LEVER_X = 158;                     // 顔中心からの横位置 (右)
constexpr int LEVER_HALF_LEN = 105;              // レバーの半分の長さ (上 = MIC ON, 下 = MUTE)
constexpr int LEVER_KNOB_R = 22;
constexpr int LEVER_ZONE_HALF_W = 48;            // タッチ判定の半幅
constexpr uint16_t COLOR_LEVER_TRACK = 0x4208;
constexpr uint16_t COLOR_LEVER_ON = 0x07E0;      // 緑
constexpr uint16_t COLOR_LEVER_MUTE = 0xF800;    // 赤

// ============================================================
// 8. 会議モード (顔を下へスワイプで入る / 上へスワイプで戻る)
//    バイブ・音への反応・かじり・居眠りを止め、大きな横型ミュートトグルと状態だけを表示
// ============================================================
constexpr int SWIPE_MIN_PX = 110;                // モード切替とみなすスワイプの長さ (顔ローカル座標)
constexpr int MEET_LEVER_HALF_LEN = 105;         // 横型トグルの半分の長さ (左 = MIC ON, 右 = MUTE)
constexpr int MEET_LEVER_KNOB_R = 40;
constexpr int MEET_LEVER_ZONE_HALF_W = 95;       // タッチ判定の半分の高さ
constexpr uint8_t MEET_BRIGHTNESS_PERCENT = 70;  // 通常の明るさに対する割合
constexpr bool MEET_SILENT = true;               // 会議モード中はバイブを一切鳴らさない (マイクに乗るため)

// ============================================================
// 9. チケット (QR) モード。通常モードから左右スワイプで入る / もう一度左右スワイプで戻る
//    iPhone のショートカットから Wallet の .pkpass やスクリーンショットを Wi-Fi で受け取り、QR を描き直して表示
// ============================================================
constexpr bool PASS_ENABLED = true;
constexpr int PASS_MAX = 8;                      // 保存する枚数 (NVS)
constexpr const char* PASS_HOSTNAME = "makkuro"; // http://makkuro.local/pass
constexpr uint32_t PASS_WIFI_TIMEOUT_MS = 180UL * 1000;   // 受信待ちを自動で止めるまで (最後の通信から)
constexpr uint32_t PASS_WIFI_CONNECT_TIMEOUT_MS = 30000;  // 1 つの SSID を諦めるまで (一覧を 2 周してだめなら AP になる)
constexpr uint8_t PASS_BRIGHTNESS = 255;         // QR 表示中はスキャナ向けに最大
constexpr int PASS_QR_MAX_PX = 300;              // QR の最大辺 (顔スプライト 380 の中)
constexpr int PASS_QR_QUIET_PX = 14;             // 白い余白
constexpr uint32_t PASS_DELETE_HOLD_MS = 1500;   // QR を長押しで削除
constexpr size_t PASS_MAX_UPLOAD_BYTES = 3UL * 1024 * 1024;   // 受け付ける最大サイズ (PSRAM)
constexpr bool PASS_DEBUG_RECEIVE_ON_BOOT = false; // 開発用: 起動 3 秒後にチケットモード + 受信待ちにし、5 秒毎にループの生存ログを出す (tools/pass_test.py 用)。普段は false
