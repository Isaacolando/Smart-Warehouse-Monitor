/*
 * ============================================================
 *  SMART WAREHOUSE MONITOR – WOKWI SIMULATION VERSION
 * ============================================================
 *  This version is optimised for Wokwi online simulation.
 *  - No real Wi-Fi / ThingSpeak calls (Wokwi can't hit internet)
 *  - MQ-135 simulated via potentiometer on GPIO34
 *  - Sound sensor simulated via potentiometer on GPIO35
 *  - LDR simulated via photoresistor sensor on GPIO32
 *  - All outputs visible: OLED, LEDs, relay states on Serial
 * ============================================================
 */

#include <Wire.h>
#include "DHT.h"
#include <Adafruit_SSD1306.h>
#include <Adafruit_GFX.h>

// ── Pins ─────────────────────────────────────────────────────
#define DHT_PIN      4
#define DHT_TYPE     DHT22
#define MQ135_PIN    34
#define SOUND_PIN    35
#define LDR_PIN      32

#define RELAY_FAN    26
#define RELAY_HEATER 27
#define RELAY_LIGHTS 14
#define RELAY_BUZZER 12

#define LED_RED   23
#define LED_GREEN 18
#define LED_BLUE  19

#define OLED_SDA  21
#define OLED_SCL  22
#define OLED_ADDR 0x3C

// ── Thresholds ───────────────────────────────────────────────
#define TEMP_HIGH  28.0
#define TEMP_LOW   18.0
#define CO2_HIGH   1000
#define NOISE_HIGH 70
#define LIGHT_LOW  300

// ── Objects ──────────────────────────────────────────────────
DHT dht(DHT_PIN, DHT_TYPE);
Adafruit_SSD1306 display(128, 64, &Wire, -1);

// ── Sensor readings ──────────────────────────────────────────
float temp, hum, lightLux;
int   co2, noiseDb;

// ── Actuator states ──────────────────────────────────────────
bool fanOn, heaterOn, lightsOn, buzzerOn;

void setRelay(uint8_t pin, bool on) {
  digitalWrite(pin, on ? LOW : HIGH);
}

void setRGB(bool r, bool g, bool b) {
  digitalWrite(LED_RED,   r);
  digitalWrite(LED_GREEN, g);
  digitalWrite(LED_BLUE,  b);
}

void readSensors() {
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  if (!isnan(t)) temp = t;
  if (!isnan(h)) hum  = h;

  // Potentiometers → simulated sensor values
  int raw_mq  = analogRead(MQ135_PIN);
  int raw_snd = analogRead(SOUND_PIN);
  int raw_ldr = analogRead(LDR_PIN);

  co2      = map(raw_mq,  0, 4095, 200, 2000);
  noiseDb  = map(raw_snd, 0, 4095, 30,  120);
  lightLux = map(raw_ldr, 0, 4095, 0,   1000);
}

void evaluateAndApply() {
  fanOn    = (temp > TEMP_HIGH) || (co2 > CO2_HIGH);
  heaterOn = (temp < TEMP_LOW);
  lightsOn = (lightLux < LIGHT_LOW);
  buzzerOn = (noiseDb  > NOISE_HIGH);

  setRelay(RELAY_FAN,    fanOn);
  setRelay(RELAY_HEATER, heaterOn);
  setRelay(RELAY_LIGHTS, lightsOn);
  setRelay(RELAY_BUZZER, buzzerOn);

  if (co2 > CO2_HIGH || noiseDb > NOISE_HIGH) {
    setRGB(1, 0, 0);  // RED – alert
  } else if (temp > TEMP_HIGH || temp < TEMP_LOW) {
    setRGB(1, 1, 0);  // YELLOW
  } else {
    setRGB(0, 1, 0);  // GREEN – OK
  }
}

void updateOLED() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(0, 0);
  display.println("== WAREHOUSE MONITOR ==");
  display.drawLine(0, 9, 127, 9, SSD1306_WHITE);

  display.setCursor(0, 12);
  display.printf("Temp : %.1f C\n",  temp);
  display.printf("Humid: %.1f %%\n", hum);
  display.printf("CO2  : %d ppm\n",  co2);
  display.printf("Noise: %d dB\n",   noiseDb);
  display.printf("Light: %.0f lux\n",lightLux);

  // Actuator icons at bottom
  display.setCursor(0, 57);
  display.print(fanOn    ? "[FAN]"  : "     ");
  display.print(heaterOn ? "[HTR]"  : "     ");
  display.print(lightsOn ? "[LIT]"  : "     ");
  display.print(buzzerOn ? "[BUZ]"  : "     ");

  display.display();
}

void printSerial() {
  Serial.println("─────────────────────────");
  Serial.printf("Temp    : %.1f °C\n",  temp);
  Serial.printf("Humidity: %.1f %%\n",  hum);
  Serial.printf("CO2     : %d ppm\n",   co2);
  Serial.printf("Noise   : %d dB\n",    noiseDb);
  Serial.printf("Light   : %.0f lux\n", lightLux);
  Serial.printf("Fan:%d  Heater:%d  Lights:%d  Buzzer:%d\n",
                fanOn, heaterOn, lightsOn, buzzerOn);
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n=== Smart Warehouse Monitor (Wokwi) ===");

  pinMode(RELAY_FAN,    OUTPUT); setRelay(RELAY_FAN,    false);
  pinMode(RELAY_HEATER, OUTPUT); setRelay(RELAY_HEATER, false);
  pinMode(RELAY_LIGHTS, OUTPUT); setRelay(RELAY_LIGHTS, false);
  pinMode(RELAY_BUZZER, OUTPUT); setRelay(RELAY_BUZZER, false);
  pinMode(LED_RED,   OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_BLUE,  OUTPUT);
  setRGB(0, 0, 1);   // BLUE – booting

  dht.begin();

  Wire.begin(OLED_SDA, OLED_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("OLED failed!");
    while (true);
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(15, 24);
  display.println("Warehouse Monitor");
  display.setCursor(20, 36);
  display.println("Initialising...");
  display.display();
  delay(1500);

  setRGB(0, 1, 0);   // GREEN – ready
  Serial.println("Setup complete. Monitoring started.");
  Serial.println("Adjust potentiometers to simulate sensors.");
}

void loop() {
  readSensors();
  evaluateAndApply();
  updateOLED();
  printSerial();
  delay(2000);
}
