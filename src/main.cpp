#include <M5Unified.h>
#include <Avatar.h>
#include <WiFi.h>
#include <WiFiUdp.h>

using namespace m5avatar;
Avatar avatar;

static constexpr const size_t record_samplerate = 16000;
static constexpr size_t record_size = 256 * 2;
static int16_t *record_data;

const char* ssid = "elecom2g-b09b64";     // WiFi SSID
const char* password =  "xfxuwecedyfn";   // WiFi Password
const char *udpAddress = "192.168.2.111"; // 相手のIPアドレス
const int udpPort = 3333;                 // 相手のポート
WiFiUDP udp;
WiFiServer server(4444);

unsigned long action_time = millis();

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

  M5.Log.printf("%s DEFAULT: 総サイズ：%4dKB 残り：%4dKB 最大ブロック残：%3dKB\n", text, default_total_size, default_free_size, default_largest_free_block);
  M5.Log.printf("%s DMA:     総サイズ：%4dKB 残り：%4dKB 最大ブロック残：%3dKB\n", text, dma_total_size, dma_free_size, dma_largest_free_block);
  M5.Log.printf("%s SPIRAM:  総サイズ：%4dKB 残り：%4dKB 最大ブロック残：%3dKB\n", text, spiram_total_size, spiram_free_size, spiram_largest_free_block);
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
  mic_cfg.noise_filter_level = 200;
  M5.Mic.config(mic_cfg);
  M5.Mic.end();

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
  M5.Speaker.setVolume(255);
  M5.Speaker.end();

  // アバターの初期化
  int8_t display_rotation = 0;
  float scale = 0.55f;
  int8_t position_top =  -60;
  int8_t position_left = -95;
  M5.Display.setRotation(display_rotation);
  avatar.setScale(scale);
  avatar.setPosition(position_top, position_left);
  avatar.init();

  record_data = (typeof(record_data))heap_caps_malloc(record_size * sizeof(int16_t), MALLOC_CAP_8BIT);
  memset(record_data, 0, record_size * sizeof(int16_t));

  // WiFiに接続
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }

  // 送受信の初期化
  udp.begin(udpPort);
  server.begin();
}

const size_t buffer_count = 200;
const size_t buffer_size = 1024;
uint8_t buffer[buffer_count][buffer_size] = {{0}};
int idx = 0, idx2 = 0;

void loop() {
  M5.update();

  if (M5.BtnA.wasPressed()) {
    log_memory_info("ボタンA押下");
    action_time = millis();
    if (M5.Speaker.isRunning()) {
      M5.Speaker.end();
      M5.Log.println("スピーカー：オフ（ボタンA押下）");
    }
    M5.Mic.begin();
    M5.Log.println("マイク：オン（ボタンA押下）");
    idx = 0; idx2 = 0;
  }

  if (M5.Mic.isRunning() && millis() - action_time >= 10000) {
    M5.Mic.end();
    M5.Log.println("スピーカー：オフ（ウェイト）");
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
        int len = client.readBytes(buffer[idx], buffer_size);
        idx = (idx + 1) % buffer_count;
      }
    }
  }

  if (M5.Speaker.isRunning() && !M5.Speaker.isPlaying() && idx != idx2) {
    M5.Speaker.playRaw((const int16_t*)buffer[idx2], buffer_size >> 1, 24000);
    idx2 = (idx2 + 1) % buffer_count;
  }
}
