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
const char *udpAddress = "192.168.2.108"; // 相手のIPアドレス
const int udpPort = 3333;                 // 相手のポート
WiFiUDP udp;

unsigned long action_time = millis();

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
  auto spk_cfg = M5.Speaker.config();
  spk_cfg.task_pinned_core = APP_CPU_NUM;
  spk_cfg.pin_bck = GPIO_NUM_5;       //SCK, BCLK, BCK
  spk_cfg.pin_ws = GPIO_NUM_6;        //WS, LRCLK, LRCK
  spk_cfg.pin_data_out = GPIO_NUM_8;  //SD, SDATA
  M5.Speaker.config(spk_cfg);
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
  udp.begin(udpPort);
  M5.Mic.begin();
}

void loop() {
  M5.update();

  if (M5.BtnA.wasPressed()) {
    action_time = millis();
    M5.Mic.begin();
  }

  if (M5.Mic.isRunning() && millis() - action_time >= 20000) {
    M5.Mic.end();
  }

  if (M5.Mic.isRunning()) {
    if (M5.Mic.record(record_data, record_size, record_samplerate)) {
      udp.beginPacket(udpAddress, udpPort);
      udp.write((uint8_t*)record_data, record_size * sizeof(int16_t));
      udp.endPacket();
    }
  }
}
