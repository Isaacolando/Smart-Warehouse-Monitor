/*
 * ╔══════════════════════════════════════════════════════════════╗
 * ║  SMART WAREHOUSE — MASTER NODE  (Wokwi Single-Board v3)     ║
 * ║                                                              ║
 * ║  All 3 zones simulated internally — no UART, no extra       ║
 * ║  boards needed. Drop this + diagram.json into one Wokwi     ║
 * ║  project and hit Run.                                        ║
 * ║                                                              ║
 * ║  Zone 1 – MainHall   starts Scenario 1                      ║
 * ║  Zone 2 – StorageA   starts Scenario 3  (staggered)         ║
 * ║  Zone 3 – StorageB   starts Scenario 5  (staggered)         ║
 * ╚══════════════════════════════════════════════════════════════╝
 */

#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_GFX.h>

// ── OLED
Adafruit_SSD1306 display(128, 64, &Wire, -1);

// ── Pins 
#define RELAY_FAN    26
#define RELAY_HEATER 27
#define RELAY_LIGHTS 14
#define RELAY_BUZZER 12
#define LED_RED      23
#define LED_GREEN    18
#define OLED_SDA     21
#define OLED_SCL     22
#define OLED_ADDR    0x3C

// ── Thresholds 
#define TEMP_HIGH    28.0f
#define TEMP_LOW     18.0f
#define TEMP_TARGET  23.0f
#define CO2_HIGH     1000
#define CO2_TARGET   500
#define NOISE_HIGH   70
#define NOISE_TARGET 45
#define LIGHT_LOW    300
#define LIGHT_TARGET 600

// ── Smoothing factors 
#define TEMP_SMOOTH  0.20f
#define HUM_SMOOTH   0.15f
#define CO2_SMOOTH   0.22f
#define NOISE_SMOOTH 0.25f
#define LIGHT_SMOOTH 0.25f

#define SCENARIO_MS  22000UL

const char* scenarioNames[] = {
  "NORMAL","HI-TEMP","LO-TEMP","HI-CO2","HI-NOISE","LO-LIGHT","CRITICAL"
};

// ── Zone struct 
struct Zone {
  const char* name;
  float temp, hum, lightLux;
  int   co2, noise;
  bool  fanOn, heaterOn, lightsOn, buzzerOn;
  float s_temp, s_hum, s_light;
  int   s_co2,  s_noise;
  int   scenario;
  unsigned long scenarioStart;
  int   startOffset;
};

Zone zones[3] = {
  { "Zone1-MainHall", 23,55,600,500,45, false,false,false,false,
    23,55,600,500,45, 0,0, 1 },
  { "Zone2-StorageA", 23,55,600,500,45, false,false,false,false,
    23,55,600,500,45, 0,0, 3 },
  { "Zone3-StorageB", 23,55,600,500,45, false,false,false,false,
    23,55,600,500,45, 0,0, 5 },
};

uint8_t       oledPage     = 0;
unsigned long lastOledFlip = 0;


float randF(float lo, float hi){
  return lo + ((float)random(0,10000)/10000.f)*(hi-lo);
}
void setRelay(uint8_t pin, bool on){ digitalWrite(pin, on ? LOW : HIGH); }
void setRGB(bool r, bool g){
  digitalWrite(LED_RED,   r ? HIGH : LOW);
  digitalWrite(LED_GREEN, g ? HIGH : LOW);
}


void startScenario(Zone& z, int s){
  z.scenario      = s;
  z.scenarioStart = millis();
  Serial.printf("\n[%s] >>> SCN %d: %s <<<\n", z.name, s, scenarioNames[s]);
  switch(s){
    case 1: z.s_temp=33.f; z.s_hum=60.f; z.s_co2=500;  z.s_noise=45; z.s_light=600.f; break;
    case 2: z.s_temp=10.f; z.s_hum=65.f; z.s_co2=450;  z.s_noise=40; z.s_light=550.f; break;
    case 3: z.s_temp=23.f; z.s_hum=55.f; z.s_co2=1400; z.s_noise=42; z.s_light=580.f; break;
    case 4: z.s_temp=23.f; z.s_hum=55.f; z.s_co2=480;  z.s_noise=95; z.s_light=560.f; break;
    case 5: z.s_temp=23.f; z.s_hum=55.f; z.s_co2=470;  z.s_noise=44; z.s_light=80.f;  break;
    case 6: z.s_temp=36.f; z.s_hum=70.f; z.s_co2=1700; z.s_noise=55; z.s_light=450.f; break;
    default:z.s_temp=23.f; z.s_hum=55.f; z.s_co2=500;  z.s_noise=45; z.s_light=600.f; break;
  }
  z.temp=z.s_temp; z.hum=z.s_hum; z.co2=z.s_co2;
  z.noise=z.s_noise; z.lightLux=z.s_light;
}


void simulateZone(Zone& z){
  if(millis()-z.scenarioStart >= SCENARIO_MS){
    startScenario(z, (z.scenario % 6) + 1);
    return;
  }
  switch(z.scenario){
    case 1:
      z.s_temp  = z.fanOn
        ? constrain(z.s_temp - randF(0.8f,1.5f), TEMP_TARGET-1, 40.f)
        : constrain(z.s_temp + randF(0.3f,0.7f), 10.f, 38.f);
      z.s_co2   = constrain(z.s_co2  + (int)randF(-8,8),  350,700);
      z.s_noise = constrain(z.s_noise+ (int)randF(-3,3),  35, 65);
      z.s_light = constrain(z.s_light+ randF(-15,15),     400,800);
      z.s_hum   = constrain(z.s_hum  + randF(-0.2f,0.3f),40.f,80.f); break;
    case 2:
      z.s_temp  = z.heaterOn
        ? constrain(z.s_temp + randF(0.8f,1.6f), -5.f, TEMP_TARGET+1)
        : constrain(z.s_temp - randF(0.4f,0.8f),  5.f, 25.f);
      z.s_co2   = constrain(z.s_co2  + (int)randF(-5,5),  350,650);
      z.s_noise = constrain(z.s_noise+ (int)randF(-2,2),  35, 55);
      z.s_light = constrain(z.s_light+ randF(-10,10),     350,750);
      z.s_hum   = constrain(z.s_hum  + randF(-0.2f,0.4f),40.f,85.f); break;
    case 3:
      z.s_co2   = z.fanOn
        ? constrain(z.s_co2 - (int)randF(40,80), CO2_TARGET-50, 2000)
        : constrain(z.s_co2 + (int)randF(20,50), 300, 1800);
      z.s_temp  = constrain(z.s_temp + randF(-0.1f,0.2f),20.f,28.f);
      z.s_hum   = constrain(z.s_hum  + randF(-0.2f,0.3f),45.f,75.f);
      z.s_noise = constrain(z.s_noise+ (int)randF(-2,2),  35, 60);
      z.s_light = constrain(z.s_light+ randF(-10,10),     350,750); break;
    case 4:
      z.s_noise = z.buzzerOn
        ? constrain(z.s_noise - (int)randF(5,12),  NOISE_TARGET-5, 120)
        : constrain(z.s_noise + (int)randF(3,9),   30, 115);
      z.s_temp  = constrain(z.s_temp + randF(-0.1f,0.1f),20.f,27.f);
      z.s_hum   = constrain(z.s_hum  + randF(-0.1f,0.2f),45.f,70.f);
      z.s_co2   = constrain(z.s_co2  + (int)randF(-5,8),  350,750);
      z.s_light = constrain(z.s_light+ randF(-10,10),     350,800); break;
    case 5:
      z.s_light = z.lightsOn
        ? constrain(z.s_light + randF(30,60),  0.f, LIGHT_TARGET+50)
        : constrain(z.s_light - randF(10,25), 20.f, 500.f);
      z.s_temp  = constrain(z.s_temp + randF(-0.1f,0.1f),20.f,26.f);
      z.s_hum   = constrain(z.s_hum  + randF(-0.1f,0.2f),45.f,70.f);
      z.s_co2   = constrain(z.s_co2  + (int)randF(-5,5),  350,650);
      z.s_noise = constrain(z.s_noise+ (int)randF(-2,2),  35, 55); break;
    case 6:
      if(z.fanOn){
        z.s_temp = constrain(z.s_temp - randF(0.6f,1.2f), TEMP_TARGET, 42.f);
        z.s_co2  = constrain(z.s_co2  - (int)randF(30,60), CO2_TARGET, 2000);
      } else {
        z.s_temp = constrain(z.s_temp + randF(0.4f,0.9f), 20.f, 40.f);
        z.s_co2  = constrain(z.s_co2  + (int)randF(25,55), 500, 2000);
      }
      z.s_hum   = constrain(z.s_hum  + randF(0.1f,0.4f), 50.f,90.f);
      z.s_noise = constrain(z.s_noise+ (int)randF(-2,5),  40, 80);
      z.s_light = constrain(z.s_light+ randF(-15,5),      200,700); break;
    default:
      z.s_temp  = constrain(z.s_temp + randF(-0.2f,0.2f),20.f,26.f);
      z.s_hum   = constrain(z.s_hum  + randF(-0.2f,0.2f),45.f,70.f);
      z.s_co2   = constrain(z.s_co2  + (int)randF(-5,5),  380,700);
      z.s_noise = constrain(z.s_noise+ (int)randF(-2,2),  35, 60);
      z.s_light = constrain(z.s_light+ randF(-10,10),     400,800); break;
  }
  z.temp     = z.temp    *(1-TEMP_SMOOTH)  + z.s_temp  *TEMP_SMOOTH;
  z.hum      = z.hum     *(1-HUM_SMOOTH)   + z.s_hum   *HUM_SMOOTH;
  z.co2      = (int)(z.co2   *(1-CO2_SMOOTH)   + z.s_co2   *CO2_SMOOTH);
  z.noise    = (int)(z.noise *(1-NOISE_SMOOTH)  + z.s_noise *NOISE_SMOOTH);
  z.lightLux = z.lightLux*(1-LIGHT_SMOOTH) + z.s_light *LIGHT_SMOOTH;
}

// ─────────────────────────────────────────────────────────────
void evaluateZone(Zone& z){
  z.fanOn    = (z.temp > TEMP_HIGH) || (z.co2 > CO2_HIGH);
  z.heaterOn = (z.temp < TEMP_LOW);
  z.lightsOn = (z.lightLux < LIGHT_LOW);
  z.buzzerOn = (z.noise > NOISE_HIGH);
  if(z.co2  > CO2_HIGH*1.35f || z.temp > TEMP_HIGH+6) z.fanOn    = true;
  if(z.temp < TEMP_LOW-3)                              z.heaterOn = true;
}

// ─────────────────────────────────────────────────────────────
void applyZone1Actuators(){
  Zone& z = zones[0];
  setRelay(RELAY_FAN,    z.fanOn);
  setRelay(RELAY_HEATER, z.heaterOn);
  setRelay(RELAY_LIGHTS, z.lightsOn);
  setRelay(RELAY_BUZZER, z.buzzerOn);
  bool critical = z.co2>CO2_HIGH || z.noise>NOISE_HIGH || z.temp>TEMP_HIGH+4;
  bool warning  = z.temp>TEMP_HIGH || z.temp<TEMP_LOW  || z.lightLux<LIGHT_LOW/2;
  if     (critical) setRGB(1,0);
  else if(warning)  setRGB(1,1);
  else              setRGB(0,1);
}

// ─────────────────────────────────────────────────────────────
void updateOLED(){
  if(millis()-lastOledFlip > 4000){
    oledPage = (oledPage+1) % 3;
    lastOledFlip = millis();
  }
  Zone& z = zones[oledPage];
  display.clearDisplay();
  display.setTextSize(1);

  display.fillRect(0,0,128,10,SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setCursor(2,1);
  char scnTag[6]; snprintf(scnTag,6,"S%d",z.scenario);
  display.printf("Z%d:%-12s %s", oledPage+1,
    oledPage==0?"MainHall":(oledPage==1?"StorageA":"StorageB"), scnTag);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(0,13);
  display.printf("Temp : %5.1fC %s\n", z.temp,
    z.temp>TEMP_HIGH?"^!":(z.temp<TEMP_LOW?"v!":" "));
  display.printf("Hum  : %5.1f%%\n",   z.hum);
  display.printf("CO2  : %5dppm%s\n",  z.co2,      z.co2>CO2_HIGH?"!":"");
  display.printf("Noise: %5ddB %s\n",  z.noise,    z.noise>NOISE_HIGH?"!":"");
  display.printf("Light: %4dlux %s\n", (int)z.lightLux, z.lightLux<LIGHT_LOW?"v":"");

  display.setCursor(0,57);
  display.print(z.fanOn    ? "FAN " : "    ");
  display.print(z.heaterOn ? "HTR " : "    ");
  display.print(z.lightsOn ? "LIT " : "    ");
  display.print(z.buzzerOn ? "BUZ"  : "   ");

  for(int i=0;i<3;i++){
    if(i==oledPage) display.fillCircle(110+i*7,57,3,SSD1306_WHITE);
    else            display.drawCircle(110+i*7,57,3,SSD1306_WHITE);
  }
  display.display();
}

// ─────────────────────────────────────────────────────────────
void printTable(){
  Serial.println("\n╔═══╦══════════════════╦═══════╦══════╦══════╦═══════╦═══════╦═══════════════╗");
  Serial.println(  "║SCN║ Zone             ║ Temp  ║ Hum  ║ CO2  ║ Noise ║ Light ║ Actuators     ║");
  Serial.println(  "╠═══╬══════════════════╬═══════╬══════╬══════╬═══════╬═══════╬═══════════════╣");
  for(int i=0;i<3;i++){
    Zone& z=zones[i];
    Serial.printf("║%3d║ %-16s ║%5.1f°C║%4.0f%% ║%5d ║%5ddB ║%4dlux ║%s%s%s%s║\n",
      z.scenario, z.name, z.temp, z.hum, z.co2, z.noise, (int)z.lightLux,
      z.fanOn?"FAN ":"    ", z.heaterOn?"HTR ":"    ",
      z.lightsOn?"LIT ":"    ", z.buzzerOn?"BUZ":"   ");
  }
  Serial.println("╚═══╩══════════════════╩═══════╩══════╩══════╩═══════╩═══════╩═══════════════╝");
}

// ─────────────────────────────────────────────────────────────
void setup(){
  Serial.begin(115200);
  Serial.println("\n=== Smart Warehouse Master Node [All-in-One v3] ===");

  uint8_t outPins[]={RELAY_FAN,RELAY_HEATER,RELAY_LIGHTS,RELAY_BUZZER,LED_RED,LED_GREEN};
  for(auto p:outPins) pinMode(p,OUTPUT);
  setRelay(RELAY_FAN,false); setRelay(RELAY_HEATER,false);
  setRelay(RELAY_LIGHTS,false); setRelay(RELAY_BUZZER,false);
  setRGB(0,0);

  Wire.begin(OLED_SDA,OLED_SCL);
  if(!display.begin(SSD1306_SWITCHCAPVCC,OLED_ADDR)){
    Serial.println("OLED failed!"); while(true);
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(10,20); display.println("WAREHOUSE MASTER");
  display.setCursor(10,35); display.println("3-Zone Monitor v3");
  display.display();
  delay(1500);

  randomSeed(42);
  for(int i=0;i<3;i++) startScenario(zones[i], zones[i].startOffset);

  setRGB(0,1);
  Serial.println("All 3 zones simulating. Ready.");
}

// ─────────────────────────────────────────────────────────────
void loop(){
  for(int i=0;i<3;i++){
    simulateZone(zones[i]);
    evaluateZone(zones[i]);
  }
  applyZone1Actuators();
  updateOLED();

  static unsigned long lastPrint=0;
  if(millis()-lastPrint > 3000){
    printTable();
    lastPrint=millis();
  }
  delay(500);
}
