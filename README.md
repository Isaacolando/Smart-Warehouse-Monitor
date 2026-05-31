# Smart Room / Warehouse Monitoring System
### ESP32-Based Multi-Zone Environmental Monitor

Members: Isaac Mburu (SPH32/2910/2023) · Garrison Kirimi (SPH32/2725/2023)


## Project Overview

This system monitors and automatically controls environmental conditions across
multiple rooms, warehouse zones, livestock enclosures, or poultry/feeding rooms.
Each zone can have its own sensors; all data is synchronized wirelessly to a
master ESP32 that runs a web dashboard and logs data to ThingSpeak.

---

## Use Cases

| Environment | Monitored Parameters | Controlled Devices |
|---|---|---|
| Warehouse | Temp, humidity, CO₂, light | Fans, heaters, lights |
| Poultry house | Temp, ammonia (MQ135), noise | Ventilation, heating, alarms |
| Livestock room | Temp, humidity, air quality | Fans, heaters, buzzers |
| Server room | Temp, humidity | Cooling fans, alerts |
| Greenhouse | Temp, light, humidity | Grow lights, irrigation relay |

---

## File Structure

```
SmartWarehouse/
├── SmartWarehouse.ino          ← Flash to MASTER ESP32
├── RemoteZoneNode/
│   └── RemoteZoneNode.ino      ← Flash to ZONE 2 / ZONE 3 ESP32
└── Wokwi/
    ├── SmartWarehouse_Wokwi.ino  ← Simulation code (paste into Wokwi)
    ├── diagram.json              ← Wokwi wiring (paste into Wokwi)
    └── libraries.txt             ← All required libraries listed
```

---

## Hardware (Master Node – Zone 1)

| Component | Purpose | ESP32 Pin |
|---|---|---|
| DHT22 | Temperature & Humidity | GPIO 4 |
| MQ-135 | CO₂ / Air Quality | GPIO 34 (ADC) |
| Sound Sensor Module | Noise level | GPIO 35 (ADC) |
| LDR + 10kΩ resistor | Light level | GPIO 32 (ADC) |
| OLED SSD1306 (I2C) | Display | SDA→21, SCL→22 |
| 4-Channel Relay Module | Fan, Heater, Lights, Buzzer | GPIO 26, 27, 14, 12 |
| RGB LED + 220Ω resistors | Status indicator | GPIO 23, 18, 19 |
| ESP32 DevKit V1 | Main controller | — |
| 5V power supply | Relays + sensors | VIN |

---

## Wiring Quick Reference

```
DHT22:
  VCC → 3.3V
  GND → GND
  DATA → GPIO4

MQ-135:
  VCC → 5V
  GND → GND
  AOUT → GPIO34

Sound Sensor:
  VCC → 5V
  GND → GND
  AOUT → GPIO35

LDR (voltage divider):
  LDR one end → 3.3V
  LDR other end → GPIO32 + 10kΩ to GND

OLED SSD1306:
  VCC → 3.3V
  GND → GND
  SDA → GPIO21
  SCL → GPIO22

4-Channel Relay:
  VCC → 5V
  GND → GND
  IN1 (Fan)    → GPIO26
  IN2 (Heater) → GPIO27
  IN3 (Lights) → GPIO14
  IN4 (Buzzer) → GPIO12

RGB LED (common cathode):
  Red anode   → 220Ω → GPIO23
  Green anode → 220Ω → GPIO18
  Blue anode  → 220Ω → GPIO19
  Cathode     → GND
```

---

## Software Setup (Arduino IDE)

### 1. Install ESP32 Board

1. Open Arduino IDE → File → Preferences
2. Add to "Additional board manager URLs":
   `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
3. Tools → Board → Board Manager → search "esp32" → Install by Espressif

### 2. Install Libraries

Open Library Manager (Sketch → Include Library → Manage Libraries) and install:

- `DHT sensor library` by Adafruit
- `Adafruit Unified Sensor` by Adafruit
- `Adafruit SSD1306` by Adafruit
- `Adafruit GFX Library` by Adafruit
- `MQ135` by GeorgK
- `ThingSpeak` by MathWorks
- `ESPAsyncWebServer` by ESP Async WebServer *(manual install – see below)*
- `AsyncTCP` by dvarrel *(manual install)*
- `ArduinoJson` by Benoit Blanchon

**Manual install for ESPAsyncWebServer:**
1. Download from: https://github.com/me-no-dev/ESPAsyncWebServer/archive/refs/heads/master.zip
2. Arduino IDE → Sketch → Include Library → Add .ZIP Library

### 3. Configure the Code

Open `SmartWarehouse.ino` and edit lines near top:

```cpp
const char* WIFI_SSID     = "YOUR_WIFI_NAME";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
const unsigned long TS_CHANNEL_ID = 12345;        // Your ThingSpeak channel
const char* TS_API_KEY    = "XXXXXXXXXXXXXXXX";   // ThingSpeak write key
const char* ZONE_NAME     = "Zone 1 – Main Hall"; // Name this zone
```

### 4. Flash

1. Select Board: `ESP32 Dev Module`
2. Select correct COM port
3. Upload
4. Open Serial Monitor (115200 baud) → note the IP address printed
5. Open that IP in a browser → web dashboard appears

---

## Multi-Zone Setup (Remote Nodes)

1. Flash master first → note its MAC address from Serial Monitor
2. Open `RemoteZoneNode/RemoteZoneNode.ino`
3. Paste MAC address into `MASTER_MAC[]`
4. Change `ZONE_NAME` to something unique
5. Flash to second/third ESP32
6. Remote data appears automatically on master dashboard

**No Wi-Fi router needed** between nodes – they communicate directly
via ESP-NOW (proprietary Espressif 2.4 GHz protocol, range ~200 m LOS).

---

## Wokwi Simulation (No Hardware Needed)

1. Go to **https://wokwi.com**
2. Click **New Project** → **ESP32**
3. Replace `sketch.ino` content with `Wokwi/SmartWarehouse_Wokwi.ino`
4. Click the `diagram.json` tab → replace with `Wokwi/diagram.json`
5. Click Libraries icon → add all libraries from `libraries.txt`
6. Click ▶ **Start Simulation**
7. Adjust the potentiometers to change CO₂ and sound readings
8. Watch OLED display and LEDs respond

---

## Automatic Control Logic

| Condition | Trigger | Action |
|---|---|---|
| Temp > 28 °C | Too hot | Fan ON |
| Temp < 18 °C | Too cold | Heater ON |
| CO₂ > 1000 ppm | Poor air | Fan ON + Red LED |
| Noise > 70 dB | Too loud | Buzzer pulse |
| Light < 300 lux | Too dark | Lights ON |
| All normal | — | Green LED |

---

## RGB LED Status Codes

| Color | Meaning |
|---|---|
| 🔵 Blue | Booting / Initialising |
| 🟢 Green | All conditions normal |
| 🟡 Yellow | Temperature warning |
| 🔴 Red | CO₂ or noise alert |

---

## ThingSpeak Data Fields

| Field | Data |
|---|---|
| Field 1 | Temperature (°C) |
| Field 2 | Humidity (%) |
| Field 3 | CO₂ (ppm) |
| Field 4 | Noise (dB) |
| Field 5 | Light (lux) |

Create a free account at https://thingspeak.com → New Channel → copy Channel ID and Write API Key.

---

## Web Dashboard

After connecting to Wi-Fi, open `http://<ESP32-IP>/` in any browser.
Features:
- Live sensor readings per zone
- Toggle AUTO / MANUAL mode
- Manual control of each actuator
- Auto-refreshes every 3 seconds

---

## Thresholds Customisation

Change these defines at the top of the `.ino` to match your environment:

```cpp
#define TEMP_HIGH_C   28.0   // warehouses may need 35.0
#define TEMP_LOW_C    18.0   // poultry may need 32.0
#define CO2_HIGH_PPM  1000   // livestock rooms may need 800
#define NOISE_HIGH_DB 70
#define LIGHT_LOW_LUX 300
```

Applying Common shared bus:

- I2C works for sensors within ~1 m (same PCB / enclosure). All OLED, temp
  sensors etc. can share SDA/SCL with different addresses.
- RS-485 for the industrial standard for multi-room long-distance buses (up to
  1200 m). Requires a MAX485 transceiver chip per node. Each node gets a unique
  address. Good for permanent installations.
- ESP-NOW (for our case) is wireless and simpler for room-scale
  deployments. No extra hardware needed.

Future upgrades to `ModbusMaster` library with
`HardwareSerial` on ESP32 pins TX2/RX2 (GPIO 17/16).
