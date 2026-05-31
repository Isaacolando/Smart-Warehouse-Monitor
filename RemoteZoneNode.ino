/*
 * ============================================================
 *  SMART WAREHOUSE – REMOTE ZONE NODE
 *  Upload this to Zone 2 / Zone 3 ESP32 boards
 * ============================================================
 *  Each remote node:
 *    - Reads local sensors (DHT22, MQ135, Sound, LDR)
 *    - Sends data to the Master node via ESP-NOW (wireless)
 *    - No Wi-Fi router needed for the inter-node link
 *
 *  SETUP STEPS:
 *    1. Flash the master (SmartWarehouse.ino) first.
 *    2. Note the master ESP32's MAC address from Serial monitor.
 *    3. Paste it into MASTER_MAC below.
 *    4. Change ZONE_NAME to something unique per node.
 *    5. Flash this sketch to each remote ESP32.
 * ============================================================
 *  Libraries: same as master (DHT, MQ135, ArduinoJson)
 * ============================================================
 */

#include <WiFi.h>
#include <esp_now.h>
#include "DHT.h"
#include "MQ135.h"

// ── Change these per node ────────────────────────────────────
const char* ZONE_NAME = "Zone 2 – Storage A";

// Master ESP32 MAC address (get from Serial of master at boot)
uint8_t MASTER_MAC[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};  // ← replace

// ── Pins ─────────────────────────────────────────────────────
#define DHT_PIN   4
#define DHT_TYPE  DHT22
#define MQ135_PIN 34
#define SOUND_PIN 35
#define LDR_PIN   32

// ── Packet (must match master struct) ────────────────────────
typedef struct {
  char  zoneName[32];
  float temperature;
  float humidity;
  int   co2_ppm;
  int   noise_db;
  float light_lux;
} ESPNowPacket;

DHT   dht(DHT_PIN, DHT_TYPE);
MQ135 mq135(MQ135_PIN);

esp_now_peer_info_t peerInfo;

void onSent(const uint8_t* mac, esp_now_send_status_t status) {
  Serial.printf("[ESP-NOW] Send %s\n",
    status == ESP_NOW_SEND_SUCCESS ? "OK" : "FAIL");
}

void setup() {
  Serial.begin(115200);
  Serial.printf("\n=== Remote Zone Node: %s ===\n", ZONE_NAME);

  dht.begin();

  WiFi.mode(WIFI_STA);
  Serial.printf("[MAC] This node: %s\n", WiFi.macAddress().c_str());

  if (esp_now_init() != ESP_OK) {
    Serial.println("[ESP-NOW] Init failed!");
    return;
  }
  esp_now_register_send_cb(onSent);

  memcpy(peerInfo.peer_addr, MASTER_MAC, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  esp_now_add_peer(&peerInfo);

  Serial.println("[Setup] Remote node ready");
}

void loop() {
  ESPNowPacket pkt;
  strncpy(pkt.zoneName, ZONE_NAME, 31);

  float t = dht.readTemperature();
  float h = dht.readHumidity();
  pkt.temperature = isnan(t) ? 0 : t;
  pkt.humidity    = isnan(h) ? 0 : h;
  pkt.co2_ppm     = (int)mq135.getPPM();
  pkt.noise_db    = map(analogRead(SOUND_PIN), 0, 4095, 30, 120);
  pkt.light_lux   = map(analogRead(LDR_PIN),   0, 4095, 0, 1000);

  Serial.printf("[%s] T=%.1f H=%.1f CO2=%d Noise=%d Light=%.0f\n",
    ZONE_NAME, pkt.temperature, pkt.humidity,
    pkt.co2_ppm, pkt.noise_db, pkt.light_lux);

  esp_now_send(MASTER_MAC, (uint8_t*)&pkt, sizeof(pkt));

  delay(5000);   // send every 5 s
}
