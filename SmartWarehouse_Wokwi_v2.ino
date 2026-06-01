

#include <Wire.h>
#include "DHT.h"
#include <Adafruit_SSD1306.h>
#include <Adafruit_GFX.h>

// ── Pins ──────────────────────────────────────────────────────
#define DHT_PIN      4
#define DHT_TYPE     DHT22
#define RELAY_FAN    26
#define RELAY_HEATER 27
#define RELAY_LIGHTS 14
#define RELAY_BUZZER 12
#define LED_RED      23
#define LED_GREEN    18
#define LED_BLUE     19
#define OLED_SDA     21
#define OLED_SCL     22
#define OLED_ADDR    0x3C

// ── Thresholds ────────────────────────────────────────────────
#define TEMP_HIGH   28.0f
#define TEMP_LOW    18.0f
#define TEMP_TARGET 23.0f    // safe midpoint heater/fan aim for
#define CO2_HIGH    1000
#define CO2_TARGET  500      // fan pulls CO2 down toward this
#define NOISE_HIGH  70
#define NOISE_TARGET 45      // buzzer "alarm" pulls attention → noise drops
#define LIGHT_LOW   300
#define LIGHT_TARGET 600     // lights push lux up toward this

// ── Smoothing ─────────────────────────────────────────────────
const float TEMP_SMOOTH  = 0.20f;
const float HUM_SMOOTH   = 0.15f;
const float CO2_SMOOTH   = 0.22f;
const float NOISE_SMOOTH = 0.25f;
const float LIGHT_SMOOTH = 0.25f;

// ── Sim (ground truth) values ─────────────────────────────────
float sim_temp  = 23.0f;
float sim_hum   = 55.0f;
int   sim_co2   = 500;
int   sim_noise = 45;
float sim_light = 600.0f;

// ── Smoothed display values ───────────────────────────────────
float temp     = 23.0f;
float hum      = 55.0f;
float lightLux = 600.0f;
int   co2      = 500;
int   noiseDb  = 45;

// ── Actuator states ───────────────────────────────────────────
bool fanOn = false, heaterOn = false, lightsOn = false, buzzerOn = false;

// ── Scenario engine ───────────────────────────────────────────
int  currentScenario   = 0;
int  previousScenario  = 0;
unsigned long scenarioStart = 0;
const unsigned long SCENARIO_MS = 22000UL;   // 22 s per scenario

// Scenario names for Serial readability
const char* scenarioNames[] = {
  "NORMAL",
  "HIGH TEMP  → FAN should activate",
  "LOW TEMP   → HEATER should activate",
  "HIGH CO2   → FAN should activate",
  "HIGH NOISE → BUZZER should activate",
  "LOW LIGHT  → LIGHTS should activate",
  "CRITICAL   → HIGH TEMP + HIGH CO2 (both actuators)"
};

// ── Objects ───────────────────────────────────────────────────
DHT dht(DHT_PIN, DHT_TYPE);
Adafruit_SSD1306 display(128, 64, &Wire, -1);

// ─────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────
void setRelay(uint8_t pin, bool on) { digitalWrite(pin, on ? LOW : HIGH); }
void setRGB(bool r, bool g, bool b) {
  digitalWrite(LED_RED,   r ? HIGH : LOW);
  digitalWrite(LED_GREEN, g ? HIGH : LOW);
  digitalWrite(LED_BLUE,  b ? HIGH : LOW);
}
float randF(float lo, float hi) {
  return lo + ((float)random(0, 10000) / 10000.0f) * (hi - lo);
}

// ─────────────────────────────────────────────────────────────
// Start a new scenario — instantly pre-load sim values deep
// into the danger zone so actuators fire within 2-3 ticks
// ─────────────────────────────────────────────────────────────
void startScenario(int s) {
  currentScenario = s;
  scenarioStart   = millis();

  Serial.println("\n╔══════════════════════════════════════════╗");
  Serial.printf( "║  SCENARIO %d: %-28s║\n", s, scenarioNames[s]);
  Serial.println("╚══════════════════════════════════════════╝");

  // Pre-load sim values into the danger zone immediately
  switch (s) {
    case 1: // HIGH TEMP
      sim_temp  = 33.0f;   // well above TEMP_HIGH (28)
      sim_hum   = 60.0f;
      sim_co2   = 500;
      sim_noise = 45;
      sim_light = 600.0f;
      break;
    case 2: // LOW TEMP
      sim_temp  = 10.0f;   // well below TEMP_LOW (18)
      sim_hum   = 65.0f;
      sim_co2   = 450;
      sim_noise = 40;
      sim_light = 550.0f;
      break;
    case 3: // HIGH CO2
      sim_temp  = 23.0f;
      sim_hum   = 55.0f;
      sim_co2   = 1400;    // well above CO2_HIGH (1000)
      sim_noise = 42;
      sim_light = 580.0f;
      break;
    case 4: // HIGH NOISE
      sim_temp  = 23.0f;
      sim_hum   = 55.0f;
      sim_co2   = 480;
      sim_noise = 95;      // well above NOISE_HIGH (70)
      sim_light = 560.0f;
      break;
    case 5: // LOW LIGHT
      sim_temp  = 23.0f;
      sim_hum   = 55.0f;
      sim_co2   = 470;
      sim_noise = 44;
      sim_light = 80.0f;   // well below LIGHT_LOW (300)
      break;
    case 6: // CRITICAL — HIGH TEMP + HIGH CO2
      sim_temp  = 36.0f;
      sim_hum   = 70.0f;
      sim_co2   = 1700;
      sim_noise = 55;
      sim_light = 450.0f;
      break;
    default: // NORMAL / reset
      sim_temp  = 23.0f;
      sim_hum   = 55.0f;
      sim_co2   = 500;
      sim_noise = 45;
      sim_light = 600.0f;
      break;
  }

  // Also snap display values partway so OLED/serial react fast
  temp     = sim_temp;
  hum      = sim_hum;
  co2      = sim_co2;
  noiseDb  = sim_noise;
  lightLux = sim_light;
}

// ─────────────────────────────────────────────────────────────
// Simulate: drift the danger condition + actuator feedback loop
//
// KEY IDEA: if the actuator is ON, it corrects the sim value
// toward the safe target. If actuator is OFF, the scenario
// keeps pushing the value back into the danger zone.
// This creates the realistic arc: spike → fix → re-spike → fix
// ─────────────────────────────────────────────────────────────
void simulateSensors() {
  // ── Rotate scenario ──
  if (millis() - scenarioStart >= SCENARIO_MS) {
    int next = (currentScenario % 6) + 1;
    startScenario(next);
    return;   // values just snapped; let smoothing catch up next tick
  }

  switch (currentScenario) {

    // ── Scenario 1: HIGH TEMP ──────────────────────────────
    case 1:
      if (fanOn) {
        // Fan cooling: push temp down toward TEMP_TARGET
        sim_temp = constrain(sim_temp - randF(0.8f, 1.5f), TEMP_TARGET - 1, 40.0f);
      } else {
        // No fan: temp keeps rising
        sim_temp = constrain(sim_temp + randF(0.3f, 0.7f), 10.0f, 38.0f);
      }
      sim_hum   = constrain(sim_hum   + randF(-0.2f, 0.3f), 40.0f, 80.0f);
      sim_co2   = constrain(sim_co2   + (int)randF(-8, 8),   350, 700);
      sim_noise = constrain(sim_noise + (int)randF(-3, 3),    35,  65);
      sim_light = constrain(sim_light + randF(-15, 15),      400, 800);
      break;

    // ── Scenario 2: LOW TEMP ───────────────────────────────
    case 2:
      if (heaterOn) {
        // Heater warming: push temp up toward TEMP_TARGET
        sim_temp = constrain(sim_temp + randF(0.8f, 1.6f), -5.0f, TEMP_TARGET + 1);
      } else {
        // No heater: temp keeps dropping
        sim_temp = constrain(sim_temp - randF(0.4f, 0.8f), 5.0f, 25.0f);
      }
      sim_hum   = constrain(sim_hum   + randF(-0.2f, 0.4f), 40.0f, 85.0f);
      sim_co2   = constrain(sim_co2   + (int)randF(-5, 5),   350, 650);
      sim_noise = constrain(sim_noise + (int)randF(-2, 2),    35,  55);
      sim_light = constrain(sim_light + randF(-10, 10),       350, 750);
      break;

    // ── Scenario 3: HIGH CO2 ───────────────────────────────
    case 3:
      if (fanOn) {
        // Fan ventilating: pull CO2 down
        sim_co2 = constrain(sim_co2 - (int)randF(40, 80), CO2_TARGET - 50, 2000);
      } else {
        // CO2 keeps building
        sim_co2 = constrain(sim_co2 + (int)randF(20, 50), 300, 1800);
      }
      sim_temp  = constrain(sim_temp  + randF(-0.1f, 0.2f), 20.0f, 28.0f);
      sim_hum   = constrain(sim_hum   + randF(-0.2f, 0.3f), 45.0f, 75.0f);
      sim_noise = constrain(sim_noise + (int)randF(-2, 2),    35,   60);
      sim_light = constrain(sim_light + randF(-10, 10),       350,  750);
      break;

    // ── Scenario 4: HIGH NOISE ─────────────────────────────
    case 4:
      if (buzzerOn) {
        // Buzzer alarm → people quiet down / issue resolved
        sim_noise = constrain(sim_noise - (int)randF(5, 12), NOISE_TARGET - 5, 120);
      } else {
        // Noise keeps climbing
        sim_noise = constrain(sim_noise + (int)randF(3, 9), 30, 115);
      }
      sim_temp  = constrain(sim_temp  + randF(-0.1f, 0.1f), 20.0f, 27.0f);
      sim_hum   = constrain(sim_hum   + randF(-0.1f, 0.2f), 45.0f, 70.0f);
      sim_co2   = constrain(sim_co2   + (int)randF(-5, 8),   350,  750);
      sim_light = constrain(sim_light + randF(-10, 10),       350,  800);
      break;

    // ── Scenario 5: LOW LIGHT ──────────────────────────────
    case 5:
      if (lightsOn) {
        // Lights ON: push lux up toward LIGHT_TARGET
        sim_light = constrain(sim_light + randF(30, 60), 0, LIGHT_TARGET + 50);
      } else {
        // Light keeps fading
        sim_light = constrain(sim_light - randF(10, 25), 20, 500);
      }
      sim_temp  = constrain(sim_temp  + randF(-0.1f, 0.1f), 20.0f, 26.0f);
      sim_hum   = constrain(sim_hum   + randF(-0.1f, 0.2f), 45.0f, 70.0f);
      sim_co2   = constrain(sim_co2   + (int)randF(-5, 5),   350,  650);
      sim_noise = constrain(sim_noise + (int)randF(-2, 2),    35,   55);
      break;

    // ── Scenario 6: CRITICAL (TEMP + CO2) ─────────────────
    case 6:
      if (fanOn) {
        sim_temp = constrain(sim_temp - randF(0.6f, 1.2f), TEMP_TARGET, 42.0f);
        sim_co2  = constrain(sim_co2  - (int)randF(30, 60), CO2_TARGET, 2000);
      } else {
        sim_temp = constrain(sim_temp + randF(0.4f, 0.9f), 20.0f, 40.0f);
        sim_co2  = constrain(sim_co2  + (int)randF(25, 55), 500,  2000);
      }
      sim_hum   = constrain(sim_hum   + randF(0.1f, 0.4f), 50.0f, 90.0f);
      sim_noise = constrain(sim_noise + (int)randF(-2, 5),   40,   80);
      sim_light = constrain(sim_light + randF(-15, 5),       200,  700);
      break;

    default:  // NORMAL — gentle drift around safe values
      sim_temp  = constrain(sim_temp  + randF(-0.2f, 0.2f), 20.0f, 26.0f);
      sim_hum   = constrain(sim_hum   + randF(-0.2f, 0.2f), 45.0f, 70.0f);
      sim_co2   = constrain(sim_co2   + (int)randF(-5, 5),   380,  700);
      sim_noise = constrain(sim_noise + (int)randF(-2, 2),    35,   60);
      sim_light = constrain(sim_light + randF(-10, 10),       400,  800);
      break;
  }

  // ── Smooth into display values ──
  temp     = temp     * (1 - TEMP_SMOOTH)  + sim_temp  * TEMP_SMOOTH;
  hum      = hum      * (1 - HUM_SMOOTH)   + sim_hum   * HUM_SMOOTH;
  co2      = (int)(co2 * (1 - CO2_SMOOTH)  + sim_co2   * CO2_SMOOTH);
  noiseDb  = (int)(noiseDb * (1-NOISE_SMOOTH) + sim_noise * NOISE_SMOOTH);
  lightLux = lightLux * (1 - LIGHT_SMOOTH) + sim_light * LIGHT_SMOOTH;
}

// ─────────────────────────────────────────────────────────────
// Actuator logic (unchanged)
// ─────────────────────────────────────────────────────────────
void evaluateAndApply() {
  fanOn    = (temp > TEMP_HIGH) || (co2 > CO2_HIGH);
  heaterOn = (temp < TEMP_LOW);
  lightsOn = (lightLux < LIGHT_LOW);
  buzzerOn = (noiseDb > NOISE_HIGH);

  // Extended thresholds (critical override)
  if (co2  > CO2_HIGH * 1.35 || temp > TEMP_HIGH + 6) fanOn    = true;
  if (temp < TEMP_LOW - 3)                             heaterOn = true;

  setRelay(RELAY_FAN,    fanOn);
  setRelay(RELAY_HEATER, heaterOn);
  setRelay(RELAY_LIGHTS, lightsOn);
  setRelay(RELAY_BUZZER, buzzerOn);

  // RGB status LED
  if (co2 > CO2_HIGH || noiseDb > NOISE_HIGH || temp > TEMP_HIGH + 4) {
    setRGB(1, 0, 0);   // RED — Critical
  } else if (temp > TEMP_HIGH || temp < TEMP_LOW || lightLux < LIGHT_LOW / 2) {
    setRGB(1, 1, 0);   // YELLOW — Warning
  } else {
    setRGB(0, 1, 0);   // GREEN — OK
  }
}

// ─────────────────────────────────────────────────────────────
// OLED
// ─────────────────────────────────────────────────────────────
void updateOLED() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // Header
  display.setCursor(0, 0);
  display.printf("SCN%d: ", currentScenario);
  switch (currentScenario) {
    case 1: display.print("HI-TEMP");  break;
    case 2: display.print("LO-TEMP");  break;
    case 3: display.print("HI-CO2");   break;
    case 4: display.print("HI-NOISE"); break;
    case 5: display.print("LO-LIGHT"); break;
    case 6: display.print("CRITICAL"); break;
    default:display.print("NORMAL");   break;
  }
  display.drawLine(0, 9, 127, 9, SSD1306_WHITE);

  // Sensor readings
  display.setCursor(0, 12);
  display.printf("Temp : %.1f C  %s\n",  temp,    temp > TEMP_HIGH ? "!" : (temp < TEMP_LOW ? "v" : " "));
  display.printf("Humid: %.1f %%\n",      hum);
  display.printf("CO2  : %d ppm  %s\n",   co2,     co2 > CO2_HIGH   ? "!" : " ");
  display.printf("Noise: %d dB  %s\n",    noiseDb, noiseDb > NOISE_HIGH ? "!" : " ");
  display.printf("Light: %.0f lux  %s\n", lightLux, lightLux < LIGHT_LOW ? "v" : " ");

  // Actuator status bar
  display.setCursor(0, 57);
  display.print(fanOn    ? "[FAN]" : "     ");
  display.print(heaterOn ? "[HTR]" : "     ");
  display.print(lightsOn ? "[LIT]" : "     ");
  display.print(buzzerOn ? "[BUZ]" : "     ");

  display.display();
}

// ─────────────────────────────────────────────────────────────
// Serial telemetry (replaces ThingsBoard for simulation)
// ─────────────────────────────────────────────────────────────
void printTelemetry() {
  // Compact human-readable line
  Serial.printf("[SCN%d] T:%.1f°C H:%.1f%% CO2:%d Noise:%ddB Light:%.0flux | "
                "FAN:%d HTR:%d LIT:%d BUZ:%d\n",
                currentScenario, temp, hum, co2, noiseDb, lightLux,
                fanOn, heaterOn, lightsOn, buzzerOn);

  // JSON line (pipe to logger/plotter if needed)
  Serial.printf("{\"scn\":%d,\"temp\":%.1f,\"hum\":%.1f,\"co2\":%d,"
                "\"noise\":%d,\"light\":%.0f,"
                "\"fan\":%d,\"heater\":%d,\"lights\":%d,\"buzzer\":%d}\n",
                currentScenario, temp, hum, co2, noiseDb, lightLux,
                fanOn, heaterOn, lightsOn, buzzerOn);
}

// ─────────────────────────────────────────────────────────────
// Setup
// ─────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  randomSeed(42);

  Serial.println("\n=== Smart Warehouse Monitor [Wokwi v2] ===");
  Serial.println("Scenarios rotate every 22 s.");
  Serial.println("Actuators correct danger values back to safe levels.\n");

  pinMode(RELAY_FAN,    OUTPUT); setRelay(RELAY_FAN,    false);
  pinMode(RELAY_HEATER, OUTPUT); setRelay(RELAY_HEATER, false);
  pinMode(RELAY_LIGHTS, OUTPUT); setRelay(RELAY_LIGHTS, false);
  pinMode(RELAY_BUZZER, OUTPUT); setRelay(RELAY_BUZZER, false);
  pinMode(LED_RED,   OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_BLUE,  OUTPUT);
  setRGB(0, 0, 1);   // Blue — booting

  dht.begin();

  Wire.begin(OLED_SDA, OLED_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("OLED failed!");
    while (true);
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(10, 20);
  display.println("  Warehouse v2");
  display.setCursor(10, 35);
  display.println(" All scenarios ON");
  display.display();
  delay(2000);

  setRGB(0, 1, 0);
  startScenario(1);   // kick off with scenario 1
}

// ─────────────────────────────────────────────────────────────
// Loop
// ─────────────────────────────────────────────────────────────
void loop() {
  simulateSensors();
  evaluateAndApply();
  updateOLED();
  printTelemetry();
  delay(1500);
}
