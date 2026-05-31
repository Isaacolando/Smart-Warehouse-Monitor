/*
 * ============================================================
 *  SMART ROOM / WAREHOUSE MONITORING SYSTEM
 *  Based on ESP32 | Multi-Room / Multi-Sensor Version
 * ============================================================
 *  Authors : Isaac Mburu   SPH32/2910/2023
 *            Garrison Kirimi SPH32/2725/2023
 *            Fredrick Ngumo  SPH32/2722/2023
 *
 *  Description:
 *    Monitors temperature, humidity, air quality, noise, and
 *    light across multiple rooms/zones. Controls actuators
 *    (fans, heaters, lights, buzzers) automatically or via
 *    a Wi-Fi web dashboard. Data is logged to ThingSpeak.
 *
 *  Rooms / Zones supported (expandable):
 *    Zone 1 - Main Hall / Primary Sensor Node (this ESP32)
 *    Zone 2 - Remote Node via ESP-NOW wireless protocol
 *    Zone 3 - Remote Node via ESP-NOW wireless protocol
 *
 *  Libraries required (install via Arduino Library Manager):
 *    - DHT sensor library      by Adafruit
 *    - Adafruit Unified Sensor by Adafruit
 *    - Adafruit SSD1306        by Adafruit
 *    - Adafruit GFX Library    by Adafruit
 *    - MQ135                   by GeorgK
 *    - ThingSpeak              by MathWorks
 *    - ESPAsyncWebServer       by ESP Async WebServer
 *    - AsyncTCP                by dvarrel
 *    - ArduinoJson             by Benoit Blanchon
 * ============================================================
 */

// ─── Core Libraries ──────────────────────────────────────────
#include <WiFi.h>
#include <esp_now.h>
#include <Wire.h>
#include <ArduinoJson.h>
#include "ESPAsyncWebServer.h"

// ─── Sensor Libraries ────────────────────────────────────────
#include "DHT.h"
#include <Adafruit_SSD1306.h>
#include <Adafruit_GFX.h>
#include "MQ135.h"
#include "ThingSpeak.h"

// ============================================================
//  CONFIGURATION  –  edit these to match your setup
// ============================================================

// Wi-Fi credentials
const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// ThingSpeak
const unsigned long TS_CHANNEL_ID  = 0;          // replace with your channel ID
const char*         TS_API_KEY     = "YOUR_THINGSPEAK_WRITE_KEY";
const unsigned long TS_INTERVAL_MS = 20000;       // upload every 20 s

// Zone name shown on OLED / dashboard
const char* ZONE_NAME = "Zone 1 – Main Hall";

// ── Thresholds (adjust per use-case) ─────────────────────────
#define TEMP_HIGH_C      28.0   // Fan ON above this
#define TEMP_LOW_C       18.0   // Heater ON below this
#define HUMIDITY_HIGH    70.0   // High humidity warning
#define CO2_HIGH_PPM     1000   // Fan ON + Red LED
#define NOISE_HIGH_DB    70     // Buzzer warning
#define LIGHT_LOW_LUX    300    // Lights ON below this

// ============================================================
//  PIN DEFINITIONS
// ============================================================

// Sensors
#define DHT_PIN          4
#define DHT_TYPE         DHT22
#define MQ135_PIN        34    // ADC
#define SOUND_PIN        35    // ADC
#define LDR_PIN          32    // ADC

// Actuators  (relay = active LOW)
#define RELAY_FAN        26
#define RELAY_HEATER     27
#define RELAY_LIGHTS     14
#define RELAY_BUZZER     12

// RGB LED
#define LED_RED          23
#define LED_GREEN        18
#define LED_BLUE         19

// OLED (I2C)
#define OLED_SDA         21
#define OLED_SCL         22
#define OLED_ADDR        0x3C
#define SCREEN_W         128
#define SCREEN_H         64

// ============================================================
//  OBJECTS
// ============================================================
DHT             dht(DHT_PIN, DHT_TYPE);
Adafruit_SSD1306 display(SCREEN_W, SCREEN_H, &Wire, -1);
MQ135           mq135(MQ135_PIN);
AsyncWebServer  server(80);
WiFiClient      tsClient;

// ============================================================
//  DATA STRUCTURES
// ============================================================

// Sensor data for one zone
struct ZoneData {
  char   name[32];
  float  temperature;
  float  humidity;
  int    co2_ppm;
  int    noise_db;
  float  light_lux;
  bool   fanOn;
  bool   heaterOn;
  bool   lightsOn;
  bool   buzzerOn;
  uint32_t lastUpdate;   // millis() timestamp
  bool   online;
};

#define MAX_ZONES  3
ZoneData zones[MAX_ZONES];

// ESP-NOW remote data packet
typedef struct {
  char   zoneName[32];
  float  temperature;
  float  humidity;
  int    co2_ppm;
  int    noise_db;
  float  light_lux;
} ESPNowPacket;

// ============================================================
//  GLOBAL STATE
// ============================================================
bool     autoMode       = true;
uint32_t lastTSUpload   = 0;
uint8_t  oledPage       = 0;     // which zone to show
uint32_t lastOledFlip   = 0;

// ============================================================
//  HELPER: RGB LED color
// ============================================================
void setRGB(bool r, bool g, bool b) {
  digitalWrite(LED_RED,   r ? HIGH : LOW);
  digitalWrite(LED_GREEN, g ? HIGH : LOW);
  digitalWrite(LED_BLUE,  b ? HIGH : LOW);
}

// ============================================================
//  HELPER: Relay control (active-LOW)
// ============================================================
void setRelay(uint8_t pin, bool on) {
  digitalWrite(pin, on ? LOW : HIGH);
}

// ============================================================
//  READ LOCAL SENSORS → populate zones[0]
// ============================================================
void readLocalSensors() {
  ZoneData& z = zones[0];
  strncpy(z.name, ZONE_NAME, 31);

  // DHT22
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  if (!isnan(t)) z.temperature = t;
  if (!isnan(h)) z.humidity    = h;

  // MQ-135 → approximate CO₂ ppm
  z.co2_ppm = (int)mq135.getPPM();

  // Sound sensor (ADC 0-4095 → map to 0-100 dB crude estimate)
  int soundRaw = analogRead(SOUND_PIN);
  z.noise_db   = map(soundRaw, 0, 4095, 30, 120);

  // LDR (ADC 0-4095 → 0-1000 lux crude estimate)
  int ldrRaw   = analogRead(LDR_PIN);
  z.light_lux  = map(ldrRaw, 0, 4095, 0, 1000);

  z.lastUpdate = millis();
  z.online     = true;
}

// ============================================================
//  DECIDE ACTUATORS for a zone
// ============================================================
void evaluateActuators(ZoneData& z) {
  if (!autoMode) return;   // manual mode: do nothing automatically

  z.fanOn    = (z.temperature > TEMP_HIGH_C) || (z.co2_ppm > CO2_HIGH_PPM);
  z.heaterOn = (z.temperature < TEMP_LOW_C);
  z.lightsOn = (z.light_lux  < LIGHT_LOW_LUX);
  z.buzzerOn = (z.noise_db   > NOISE_HIGH_DB);
}

// ============================================================
//  APPLY LOCAL ACTUATORS  (only zone 0 drives this ESP32's pins)
// ============================================================
void applyLocalActuators() {
  ZoneData& z = zones[0];
  setRelay(RELAY_FAN,    z.fanOn);
  setRelay(RELAY_HEATER, z.heaterOn);
  setRelay(RELAY_LIGHTS, z.lightsOn);
  setRelay(RELAY_BUZZER, z.buzzerOn);

  // RGB status
  if (z.co2_ppm > CO2_HIGH_PPM || z.noise_db > NOISE_HIGH_DB) {
    setRGB(true,  false, false);  // RED  – alert
  } else if (z.temperature > TEMP_HIGH_C || z.temperature < TEMP_LOW_C) {
    setRGB(true,  true,  false);  // YELLOW – temperature warning
  } else {
    setRGB(false, true,  false);  // GREEN  – all OK
  }
}

// ============================================================
//  OLED DISPLAY  (cycles through zones every 4 s)
// ============================================================
void updateOLED() {
  if (millis() - lastOledFlip > 4000) {
    oledPage = (oledPage + 1) % MAX_ZONES;
    lastOledFlip = millis();
  }

  ZoneData& z = zones[oledPage];
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // Header
  display.setCursor(0, 0);
  display.print(z.online ? ">> " : "-- ");
  display.println(z.name);
  display.drawLine(0, 9, 127, 9, SSD1306_WHITE);

  // Readings
  display.setCursor(0, 12);
  display.printf("Temp : %.1f C\n",   z.temperature);
  display.printf("Humid: %.1f %%\n",  z.humidity);
  display.printf("CO2  : %d ppm\n",   z.co2_ppm);
  display.printf("Noise: %d dB\n",    z.noise_db);
  display.printf("Light: %.0f lux\n", z.light_lux);

  // Actuator status bar
  display.setCursor(0, 57);
  display.print(z.fanOn    ? "FAN " : "    ");
  display.print(z.heaterOn ? "HTR " : "    ");
  display.print(z.lightsOn ? "LIT " : "    ");
  display.print(z.buzzerOn ? "BUZ"  : "   ");

  // Zone indicator dots
  for (int i = 0; i < MAX_ZONES; i++) {
    display.drawCircle(118 + (i > 0 ? (i * 5) : 0), 0, 2,
                       (i == oledPage) ? SSD1306_WHITE : SSD1306_BLACK);
  }

  display.display();
}

// ============================================================
//  ESP-NOW: callback when remote zone sends data
// ============================================================
void onDataReceived(const uint8_t* mac, const uint8_t* data, int len) {
  if (len != sizeof(ESPNowPacket)) return;

  ESPNowPacket pkt;
  memcpy(&pkt, data, sizeof(pkt));

  // Match by zone name to slot 1 or 2
  for (int i = 1; i < MAX_ZONES; i++) {
    if (strcmp(zones[i].name, pkt.zoneName) == 0 || !zones[i].online) {
      strncpy(zones[i].name, pkt.zoneName, 31);
      zones[i].temperature = pkt.temperature;
      zones[i].humidity    = pkt.humidity;
      zones[i].co2_ppm     = pkt.co2_ppm;
      zones[i].noise_db    = pkt.noise_db;
      zones[i].light_lux   = pkt.light_lux;
      zones[i].lastUpdate  = millis();
      zones[i].online      = true;

      evaluateActuators(zones[i]);
      break;
    }
  }
}

// ============================================================
//  THINGSPEAK UPLOAD (zone 0 data)
// ============================================================
void uploadThingSpeak() {
  if (millis() - lastTSUpload < TS_INTERVAL_MS) return;
  lastTSUpload = millis();

  ZoneData& z = zones[0];
  ThingSpeak.setField(1, z.temperature);
  ThingSpeak.setField(2, z.humidity);
  ThingSpeak.setField(3, z.co2_ppm);
  ThingSpeak.setField(4, z.noise_db);
  ThingSpeak.setField(5, z.light_lux);

  int rc = ThingSpeak.writeFields(TS_CHANNEL_ID, TS_API_KEY);
  Serial.printf("[ThingSpeak] HTTP %d\n", rc);
}

// ============================================================
//  WEB SERVER  –  routes
// ============================================================
String buildJsonAll() {
  StaticJsonDocument<1024> doc;
  JsonArray arr = doc.createNestedArray("zones");
  for (int i = 0; i < MAX_ZONES; i++) {
    ZoneData& z = zones[i];
    if (!z.online && i > 0) continue;
    JsonObject o = arr.createNestedObject();
    o["name"]        = z.name;
    o["temperature"] = z.temperature;
    o["humidity"]    = z.humidity;
    o["co2"]         = z.co2_ppm;
    o["noise"]       = z.noise_db;
    o["light"]       = z.light_lux;
    o["fanOn"]       = z.fanOn;
    o["heaterOn"]    = z.heaterOn;
    o["lightsOn"]    = z.lightsOn;
    o["buzzerOn"]    = z.buzzerOn;
  }
  doc["autoMode"] = autoMode;
  String out;
  serializeJson(doc, out);
  return out;
}

// Minimal responsive HTML dashboard
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Smart Warehouse Monitor</title>
<style>
  body{font-family:sans-serif;background:#1a1a2e;color:#eee;margin:0;padding:16px}
  h1{color:#00d4ff;text-align:center;font-size:1.3em}
  .grid{display:flex;flex-wrap:wrap;gap:12px;justify-content:center}
  .card{background:#16213e;border-radius:12px;padding:16px;min-width:220px;flex:1;max-width:300px}
  .card h2{margin:0 0 8px;font-size:1em;color:#00d4ff}
  .row{display:flex;justify-content:space-between;padding:3px 0;border-bottom:1px solid #0f3460}
  .val{font-weight:bold;color:#e94560}
  .act{margin-top:8px;display:flex;gap:6px;flex-wrap:wrap}
  .btn{padding:4px 10px;border-radius:6px;border:none;font-size:.8em;cursor:pointer}
  .on{background:#00b894;color:#fff}
  .off{background:#636e72;color:#fff}
  #mode{text-align:center;margin:12px}
  .modebtn{padding:8px 20px;border-radius:8px;border:none;cursor:pointer;font-size:1em}
  .auto{background:#0984e3;color:#fff}
  .manual{background:#fdcb6e;color:#333}
</style>
</head>
<body>
<h1>🏭 Smart Warehouse Monitor</h1>
<div id="mode"><button class="modebtn auto" onclick="toggleMode()">Mode: AUTO</button></div>
<div class="grid" id="cards">Loading...</div>
<script>
let autoMode=true;
async function fetchData(){
  const r=await fetch('/api/data');
  const d=await r.json();
  autoMode=d.autoMode;
  document.querySelector('.modebtn').textContent='Mode: '+(autoMode?'AUTO':'MANUAL');
  document.querySelector('.modebtn').className='modebtn '+(autoMode?'auto':'manual');
  const g=document.getElementById('cards');
  g.innerHTML='';
  d.zones.forEach(z=>{
    g.innerHTML+=`<div class="card">
      <h2>${z.name}</h2>
      <div class="row"><span>Temperature</span><span class="val">${z.temperature.toFixed(1)} °C</span></div>
      <div class="row"><span>Humidity</span><span class="val">${z.humidity.toFixed(1)} %</span></div>
      <div class="row"><span>CO₂</span><span class="val">${z.co2} ppm</span></div>
      <div class="row"><span>Noise</span><span class="val">${z.noise} dB</span></div>
      <div class="row"><span>Light</span><span class="val">${z.light.toFixed(0)} lux</span></div>
      <div class="act">
        <button class="btn ${z.fanOn?'on':'off'}" onclick="ctrl('fan',${z.fanOn?0:1})">Fan ${z.fanOn?'ON':'OFF'}</button>
        <button class="btn ${z.heaterOn?'on':'off'}" onclick="ctrl('heater',${z.heaterOn?0:1})">Heater ${z.heaterOn?'ON':'OFF'}</button>
        <button class="btn ${z.lightsOn?'on':'off'}" onclick="ctrl('lights',${z.lightsOn?0:1})">Lights ${z.lightsOn?'ON':'OFF'}</button>
        <button class="btn ${z.buzzerOn?'on':'off'}" onclick="ctrl('buzzer',${z.buzzerOn?0:1})">Buzzer ${z.buzzerOn?'ON':'OFF'}</button>
      </div></div>`;
  });
}
async function toggleMode(){
  await fetch('/api/mode?auto='+(autoMode?0:1));
  fetchData();
}
async function ctrl(dev,state){
  await fetch(`/api/control?dev=${dev}&state=${state}`);
  fetchData();
}
fetchData();
setInterval(fetchData,3000);
</script>
</body>
</html>
)rawliteral";

void setupWebServer() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->send_P(200, "text/html", INDEX_HTML);
  });

  server.on("/api/data", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->send(200, "application/json", buildJsonAll());
  });

  server.on("/api/mode", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (req->hasParam("auto")) {
      autoMode = req->getParam("auto")->value().toInt();
    }
    req->send(200, "text/plain", "OK");
  });

  server.on("/api/control", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (!req->hasParam("dev") || !req->hasParam("state")) {
      req->send(400); return;
    }
    String dev   = req->getParam("dev")->value();
    bool   state = req->getParam("state")->value().toInt();
    if      (dev == "fan")    { zones[0].fanOn    = state; setRelay(RELAY_FAN,    state); }
    else if (dev == "heater") { zones[0].heaterOn = state; setRelay(RELAY_HEATER, state); }
    else if (dev == "lights") { zones[0].lightsOn = state; setRelay(RELAY_LIGHTS, state); }
    else if (dev == "buzzer") { zones[0].buzzerOn = state; setRelay(RELAY_BUZZER, state); }
    req->send(200, "text/plain", "OK");
  });

  server.begin();
}

// ============================================================
//  SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== Smart Warehouse Monitoring System ===");

  // Pin modes
  pinMode(RELAY_FAN,    OUTPUT); setRelay(RELAY_FAN,    false);
  pinMode(RELAY_HEATER, OUTPUT); setRelay(RELAY_HEATER, false);
  pinMode(RELAY_LIGHTS, OUTPUT); setRelay(RELAY_LIGHTS, false);
  pinMode(RELAY_BUZZER, OUTPUT); setRelay(RELAY_BUZZER, false);
  pinMode(LED_RED,   OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_BLUE,  OUTPUT);
  setRGB(false, false, true);   // BLUE – booting

  // DHT
  dht.begin();

  // OLED
  Wire.begin(OLED_SDA, OLED_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("[OLED] Init failed – check wiring!");
  } else {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(20, 24);
    display.println("Warehouse Monitor");
    display.display();
  }

  // Init zone structs
  for (int i = 0; i < MAX_ZONES; i++) {
    zones[i] = {0};
    zones[i].online = false;
    sprintf(zones[i].name, "Zone %d", i + 1);
  }
  strncpy(zones[0].name, ZONE_NAME, 31);
  zones[0].online = true;

  // Wi-Fi
  WiFi.mode(WIFI_AP_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("[WiFi] Connecting");
  uint8_t tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries++ < 20) {
    delay(500); Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\n[WiFi] Connected → http://%s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("\n[WiFi] Not connected – running in standalone mode");
  }

  // ESP-NOW (receive from remote zones)
  if (esp_now_init() == ESP_OK) {
    esp_now_register_recv_cb(onDataReceived);
    Serial.println("[ESP-NOW] Ready");
  }

  // ThingSpeak
  ThingSpeak.begin(tsClient);

  // Web server
  setupWebServer();

  setRGB(false, true, false);   // GREEN – ready
  Serial.println("[Setup] Complete");
}

// ============================================================
//  LOOP
// ============================================================
void loop() {
  readLocalSensors();
  evaluateActuators(zones[0]);
  applyLocalActuators();
  updateOLED();

  // Mark remote zones offline if no update in 30 s
  for (int i = 1; i < MAX_ZONES; i++) {
    if (zones[i].online && millis() - zones[i].lastUpdate > 30000) {
      zones[i].online = false;
      Serial.printf("[Zone %d] Offline (timeout)\n", i + 1);
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    uploadThingSpeak();
  }

  delay(2000);
}
