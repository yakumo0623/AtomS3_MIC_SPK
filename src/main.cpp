#include <M5Unified.h>
#include <Avatar.h>
#include <WiFi.h>
#include <WiFiUdp.h>

using namespace m5avatar;
Avatar avatar;

const char* ssid = "elecom2g-b09b64";     // WiFi SSID
const char* password =  "xfxuwecedyfn";   // WiFi Password
const char *udpAddress = "192.168.2.111"; // 相手のIPアドレス
const int udpPort = 50023;                // 相手(送信)のポート ★複数スタックチャンを使う場合ここを変える(50023, 50024、…)
WiFiUDP udp;
WiFiServer server(50022);                 // 自分(受信)のポート

static constexpr const size_t record_samplerate = 16000;  // マイクのサンプリングレート
static constexpr const size_t record_size = 512;          // マイクのバッファサイズ
static int16_t *record_data;                              // マイクのバッファ

static constexpr const size_t play_buffer_count = 200;             // スピーカーのバッファ数
static constexpr const size_t play_buffer_size = 1024;             // スピーカーのバッファサイズ
uint8_t play_buffer[play_buffer_count][play_buffer_size] = {{0}};  // スピーカーのバッファ
int idx = 0, idx2 = 0;                                             // idx：書き込みインデックス, idx2：読み込みインデックス
TaskHandle_t play_task_handle = NULL;                              // スピーカーのタスク

unsigned long rotation_time = millis();  // 画面の向きチェック用

// メモリの状態を出力
void log_memory_info(const char* text) {
  // DEFAULT
  uint32_t default_total_size = heap_caps_get_total_size(MALLOC_CAP_DEFAULT) / 1024;
  uint32_t default_free_size = heap_caps_get_free_size(MALLOC_CAP_DEFAULT) / 1024;
  uint32_t default_largest_free_block = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT) / 1024;
  // DMA
  uint32_t dma_total_size = heap_caps_get_total_size(MALLOC_CAP_DMA) / 1024;
  uint32_t dma_free_size = heap_caps_get_free_size(MALLOC_CAP_DMA) / 1024;
  uint32_t dma_largest_free_block = heap_caps_get_largest_free_block(MALLOC_CAP_DMA) / 1024;
  // SPIRAM
  uint32_t spiram_total_size = heap_caps_get_total_size(MALLOC_CAP_SPIRAM) / 1024;
  uint32_t spiram_free_size = heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024;
  uint32_t spiram_largest_free_block = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) / 1024;
  // ログ出力
  M5.Log.printf("%s DEFAULT: 総サイズ：%4dKB 残り：%4dKB 最大ブロック残：%3dKB\n", text, default_total_size, default_free_size, default_largest_free_block);
  M5.Log.printf("%s DMA:     総サイズ：%4dKB 残り：%4dKB 最大ブロック残：%3dKB\n", text, dma_total_size, dma_free_size, dma_largest_free_block);
  M5.Log.printf("%s SPIRAM:  総サイズ：%4dKB 残り：%4dKB 最大ブロック残：%3dKB\n", text, spiram_total_size, spiram_free_size, spiram_largest_free_block);
}

// スピーカーのタスク
void play_task_loop(void *args) {
  for (;;) {
    if (M5.Speaker.isRunning() && !M5.Speaker.isPlaying() && idx != idx2) {
      // 音声再生
      M5.Speaker.playRaw((const int16_t*)play_buffer[idx2], play_buffer_size >> 1, 24000);
      // リップシンク
      uint8_t level = abs(play_buffer[idx2][0]);
      if(level > 255) level = 255;
      float open = (float)level/255.0;
      avatar.setMouthOpenRatio(open);
      idx2 = (idx2 + 1) % play_buffer_count;
    } else if (!M5.Speaker.isPlaying() && idx == idx2) {
      avatar.setMouthOpenRatio(0);
    }
    vTaskDelay(1);
  }
}

// 画面の向き
void set_rotation() {
  float ax, ay, az;
  M5.Imu.getAccel(&ax, &ay, &az);
  if (abs(az) > abs(ax) && abs(az) > abs(ay)) {
    M5.Display.setRotation(0);
  } else if (abs(ax) > abs(ay)) {
    if (ax >= 0) {
      M5.Display.setRotation(1);
    } else {
      M5.Display.setRotation(3);
    }
  } else {
    if (ay >= 0) {
      M5.Display.setRotation(0);
    } else {
      M5.Display.setRotation(2);
    }
  }
}

void setup() {
  // M5Stackの初期化
  auto cfg = M5.config();
  cfg.external_speaker.hat_spk2 = true;
  M5.begin(cfg);

  // マイクの初期化
  auto mic_cfg = M5.Mic.config();
  mic_cfg.sample_rate = 16000;
  mic_cfg.pin_ws = 1;
  mic_cfg.pin_data_in = 2;
  // mic_cfg.noise_filter_level = 200;
  M5.Mic.config(mic_cfg);

  // スピーカーの初期化
  // HAT-SPK2 {前面：5VI, 3V3, BAT, 0(LRCLK), 25(SDATA), 26(BCLK), 5VO, G}
  // ATOM-MATE{         , 3V3,    , 19      , 33       , 22      , 5VO, G}
  // ATOMS3   {         , 3V3,    ,  6      ,  8       ,  5      , 5V0, G}
  auto spk_cfg = M5.Speaker.config();
  spk_cfg.sample_rate = 96000;
  spk_cfg.task_pinned_core = APP_CPU_NUM;
  spk_cfg.pin_bck = GPIO_NUM_5;       //SCK, BCLK, BCK
  spk_cfg.pin_ws = GPIO_NUM_6;        //WS, LRCLK, LRCK
  spk_cfg.pin_data_out = GPIO_NUM_8;  //SD, SDATA
  M5.Speaker.config(spk_cfg);
  M5.Speaker.setVolume(150);

  // アバターの初期化
  int8_t display_rotation = 0;
  float scale = 0.55f;
  int8_t position_top =  -60;
  int8_t position_left = -95;
  M5.Display.setRotation(display_rotation);
  avatar.setScale(scale);
  avatar.setPosition(position_top, position_left);
  avatar.init();

  // WiFiに接続
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }

  // マイクのバッファ
  record_data = (typeof(record_data))heap_caps_malloc(record_size * sizeof(int16_t), MALLOC_CAP_8BIT);
  memset(record_data, 0, record_size * sizeof(int16_t));

  // タスク実行
  xTaskCreateUniversal(play_task_loop, "play_task_loop", 8192, NULL, 1, &play_task_handle, APP_CPU_NUM);

  // 送受信の初期化
  udp.begin(udpPort);
  server.begin();
}

void loop() {
  M5.update();

  if (M5.BtnA.wasPressed()) {
    log_memory_info("ボタンA押下");
    if (M5.Speaker.isRunning()) {
      M5.Speaker.end();
      M5.Log.println("スピーカー：オフ（ボタンA押下）");
    }
    M5.Mic.begin();
    M5.Log.println("マイク：オン（ボタンA押下）");
    idx = 0; idx2 = 0;
  }

  if (M5.Mic.isRunning()) {
    if (M5.Mic.record(record_data, record_size, record_samplerate)) {
      udp.beginPacket(udpAddress, udpPort);
      udp.write((uint8_t*)record_data, record_size * sizeof(int16_t));
      udp.endPacket();
    }
  }

  WiFiClient client = server.available();
  if (client) {
    if (M5.Mic.isRunning()) {
      M5.Mic.end();
      M5.Speaker.begin();
      M5.Log.println("マイク：オフ（TCP受信）");
      M5.Log.println("スピーカー：オン（TCP受信）");
    }
    while (client.connected()) {
      int size = client.available();
      if (size) {
        int len = client.readBytes(play_buffer[idx], play_buffer_size);
        idx = (idx + 1) % play_buffer_count;
      }
    }
  }

  if (millis() - rotation_time >= 1000) {
    set_rotation();
    rotation_time = millis();
  }
}
