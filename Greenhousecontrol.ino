/*
 * ============================================================
 *  SMART GREENHOUSE CONTROLLER
 *  ESP32 + Blynk + TFT + Keypad + NeoPixel
 * ============================================================
 *
 *  SENSORS / HARDWARE:
 *    - Potentiometer  → simulates soil moisture (0–100%)
 *    - LDR            → ambient light level (0–100%)
 *    - 4x4 Keypad     → password entry + menu control
 *    - TFT Display    → live local dashboard (SPI)
 *    - NeoPixel LEDs  → status ring (green/amber/red)
 *    - No relay used  → pump & light are display-only indicators
 *
 *  BLYNK VIRTUAL PINS:
 *    V0  ← ESP32 sends  soil moisture %
 *    V1  ← ESP32 sends  light level %
 *    V2  ← ESP32 sends  pump status (0/1)
 *    V3  ← ESP32 sends  light status (0/1)
 *    V4  → Blynk sends  force pump override (button widget)
 *    V5  → Blynk sends  moisture threshold override (slider 0–100)
 *    V6  ← ESP32 sends  greenhouse lock status (label widget)
 *
 * ============================================================
 *  REQUIRED LIBRARIES (install via Arduino Library Manager):
 *    - Blynk (by Volodymyr Shymanskyy)       "Blynk"
 *    - TFT_eSPI (by Bodmer)                  "TFT_eSPI"
 *    - Keypad (by Mark Stanley)              "Keypad"
 *    - Adafruit NeoPixel                     "Adafruit NeoPixel"
 * ============================================================
 */

// ============================================================
//  SECTION 1: CREDENTIALS — EDIT THESE
// ============================================================

#define BLYNK_TEMPLATE_ID   "TMPL3tU0-cDQW"       // <-- Your Blynk Template ID
#define BLYNK_TEMPLATE_NAME "Greenhouse"    // <-- Your Blynk Template Name
#define BLYNK_AUTH_TOKEN    "ouH3gWAPQ1K-dO81T6hajuC6X5hnVWeg" // <-- Your Blynk Auth Token

const char* WIFI_SSID     = "RB 0088";      // <-- Your WiFi SSID
const char* WIFI_PASSWORD = "H02622;z";  // <-- Your WiFi Password

#include <Wire.h>
#include "I2CKeyPad.h"

const uint8_t KEYPAD_ADDRESS = 0x3D;
I2CKeyPad keypad(KEYPAD_ADDRESS);

#define BUZZER_PIN 25

// ============================================================
//  SECTION 2: PIN DEFINITIONS — CHANGE THESE TO MATCH YOUR WIRING
// ============================================================

// --- Analog Sensors ---
#define PIN_SOIL_MOISTURE   36   // Potentiometer middle pin → GPIO34 (ADC1_CH6)
#define PIN_LDR             39   // LDR voltage divider output → GPIO35 (ADC1_CH7)
// NOTE: Use only ADC1 pins (32–39) for analogRead on ESP32.
//       ADC2 pins conflict with WiFi.

// --- NeoPixel ---
#define PIN_NEOPIXEL        17   // NeoPixel data pin → GPIO27
#define NEOPIXEL_COUNT      8    // Number of NeoPixels in your ring/strip

// --- TFT Display (SPI) ---
// TFT_eSPI uses its own User_Setup.h for pin config.
// Open: Arduino/libraries/TFT_eSPI/User_Setup.h and set:
//   #define TFT_MOSI  23   // SPI MOSI → GPIO23
//   #define TFT_SCLK  18   // SPI CLK  → GPIO18
//   #define TFT_CS    15   // Chip Select → GPIO15
//   #define TFT_DC     2   // Data/Command → GPIO2
//   #define TFT_RST    4   // Reset → GPIO4
//   #define TFT_BL    -1   // Backlight (connect to 3.3V directly if unused)
// Also set: #define ILI9341_DRIVER  (or your specific TFT driver)

// --- Keypad ---
// Keypad rows and columns connect to these GPIO pins:

// NOTE: Adjust these if your keypad is wired differently.
//       These are safe GPIO pins that don't conflict with SPI or ADC1.

// ============================================================
//  SECTION 3: GREENHOUSE PASSWORD
// ============================================================

const String GREENHOUSE_PASSWORD = "1234"; // <-- Change to your desired password
// Users enter this on the keypad to unlock/lock the greenhouse.
// '#' confirms entry. '*' clears the current input.

// ============================================================
//  SECTION 4: INCLUDES & OBJECT SETUP
// ============================================================

#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <TFT_eSPI.h>
#include <Keypad.h>
#include <Adafruit_NeoPixel.h>

TFT_eSPI tft = TFT_eSPI();

Adafruit_NeoPixel pixels(NEOPIXEL_COUNT, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);


// ============================================================
//  SECTION 5: STATE VARIABLES
// ============================================================
int buzzerPin = 25;
int buzzerChannel = 0;

void buzzerTone(int freq) {
  ledcSetup(buzzerChannel, 2000, 8);
  ledcAttachPin(buzzerPin, buzzerChannel);
  ledcWriteTone(buzzerChannel, freq);
}

void buzzerOff() {
  ledcWriteTone(buzzerChannel, 0);
}

// --- Sensor readings ---
int soilMoisture    = 0;   // 0–100%
int lightLevel      = 0;   // 0–100%

int wrongAttempts = 0;
const int MAX_ATTEMPTS = 3;

// --- Control states ---
int  moistureThreshold = 40;  // % — below this, pump turns "ON"
bool pumpOn            = false;
bool growLightOn       = false;

// --- Blynk overrides ---
bool  blynkForcePump   = false;  // V4: Blynk can force pump ON
int   blynkThreshold   = -1;     // V5: -1 means "not set by Blynk, use potentiometer"

// --- Greenhouse lock ---
bool   greenHouseLocked = true;
String keypadInput      = "";

// --- Timing ---
unsigned long lastSensorRead  = 0;
unsigned long lastBlynkUpdate = 0;
unsigned long lastTFTUpdate   = 0;
const unsigned long SENSOR_INTERVAL  = 500;   // ms
const unsigned long BLYNK_INTERVAL   = 2000;  // ms
const unsigned long TFT_INTERVAL     = 800;   // ms

// --- TFT colours (RGB565) ---
#define COL_BG       0x0841   // Very dark green-black
#define COL_GREEN    0x07E0
#define COL_AMBER    0xFD20
#define COL_RED      0xF800
#define COL_BLUE     0x02FF
#define COL_WHITE    0xFFFF
#define COL_GRAY     0x8410
#define COL_DARKGRAY 0x4208

// ============================================================
//  SECTION 6: BLYNK VIRTUAL PIN HANDLERS
// ============================================================

// V4: Force pump button from Blynk app
BLYNK_WRITE(V4) {
  blynkForcePump = (param.asInt() == 1);
}

// V5: Moisture threshold slider from Blynk app (0–100)
BLYNK_WRITE(V5) {
  blynkThreshold = param.asInt();
}

// ============================================================
//  SECTION 7: SENSOR READING
// ============================================================

void readSensors() {
  // --- Soil moisture from potentiometer ---
  // Potentiometer gives 0–4095 (12-bit ADC on ESP32)
  // We map it to 0–100% (0 = bone dry, 100 = fully wet)
  int rawSoil = analogRead(PIN_SOIL_MOISTURE);
  soilMoisture = map(rawSoil, 0, 4095, 0, 100);

  // --- Light level from LDR ---
  // LDR in voltage divider: bright = high ADC value
  // Map so 100% = bright, 0% = dark
  int rawLDR = analogRead(PIN_LDR);
  lightLevel = map(rawLDR, 0, 4095,100,0);
  // NOTE: Invert if your LDR divider is wired the other way:
  // lightLevel = 100 - lightLevel;
}

// ============================================================
//  SECTION 8: CONTROL LOGIC
// ============================================================

void runControlLogic() {
  // Use Blynk threshold override if set, otherwise use pot-derived value
  // (pot isn't used for threshold here — threshold comes from soilMoisture
  //  mapping + Blynk slider; the pot IS the soil moisture sensor itself)
  int activeThreshold = (blynkThreshold >= 0) ? blynkThreshold : moistureThreshold;

  // --- Pump logic ---
  // Pump ON if: soil moisture below threshold OR Blynk forces it ON
  pumpOn = (soilMoisture < activeThreshold) || blynkForcePump;

  // --- Grow light logic ---
  // Light ON when ambient light is below 30% (simulates supplemental lighting)
  growLightOn = (lightLevel < 30);
}

// ============================================================
//  SECTION 9: NEOPIXEL STATUS
// ============================================================

/*void updateNeoPixels() {
  uint32_t colour;

  if (greenHouseLocked) {
    // Locked → slow blue pulse
    colour = pixels.Color(0, 0, 60);
  } else if (pumpOn) {
    // Pump running → red
    colour = pixels.Color(180, 0, 0);
  } else if (soilMoisture < (moistureThreshold + 15)) {
    // Getting dry → amber warning
    colour = pixels.Color(180, 80, 0);
  } else {
    // All good → green
    colour = pixels.Color(0, 150, 0);
  }

  for (int i = 0; i < NEOPIXEL_COUNT; i++) {
    pixels.setPixelColor(i, colour);
  }
  pixels.show();
}*/

// ============================================================
//  SECTION 10: TFT DISPLAY
// ============================================================

void drawBar(int x, int y, int w, int h, int percent, uint16_t colour) {
  tft.drawRect(x, y, w, h, COL_GRAY);
  int filled = (w - 2) * percent / 100;
  tft.fillRect(x + 1, y + 1, filled, h - 2, colour);
  tft.fillRect(x + 1 + filled, y + 1, (w - 2) - filled, h - 2, COL_DARKGRAY);
}

void updateTFT() {
  int activeThreshold = (blynkThreshold >= 0) ? blynkThreshold : moistureThreshold;

  tft.fillScreen(COL_BG);

  // --- Title bar ---
  tft.setTextColor(COL_GREEN, COL_BG);
  tft.setTextSize(1);
  tft.setCursor(10, 6);
  tft.print("GREENHOUSE CTRL");

  // Lock status top right
  tft.setTextColor(greenHouseLocked ? COL_RED : COL_GREEN, COL_BG);
  tft.setCursor(170, 6);
  tft.print(greenHouseLocked ? "LOCKED" : "OPEN");

  tft.drawLine(0, 16, 240, 16, COL_GRAY);

  // --- Soil moisture ---
  tft.setTextColor(COL_WHITE, COL_BG);
  tft.setCursor(10, 24);
  tft.print("SOIL");

  uint16_t soilColor = (soilMoisture < activeThreshold) ? COL_RED :
                       (soilMoisture < activeThreshold + 15) ? COL_AMBER : COL_GREEN;
  tft.setTextColor(soilColor, COL_BG);
  tft.setCursor(160, 24);
  tft.print(soilMoisture);
  tft.print("%");

  drawBar(10, 36, 220, 12, soilMoisture, soilColor);

  // --- Light level ---
  tft.setTextColor(COL_WHITE, COL_BG);
  tft.setCursor(10, 56);
  tft.print("LIGHT");

  tft.setTextColor(COL_AMBER, COL_BG);
  tft.setCursor(160, 56);
  tft.print(lightLevel);
  tft.print("%");

  drawBar(10, 68, 220, 12, lightLevel, COL_AMBER);

  // --- Threshold ---
  tft.setTextColor(COL_GRAY, COL_BG);
  tft.setCursor(10, 88);
  tft.print("THRESHOLD");
  tft.setTextColor(COL_BLUE, COL_BG);
  tft.setCursor(160, 88);
  tft.print(activeThreshold);
  tft.print("% ");
  tft.print((blynkThreshold >= 0) ? "(app)" : "(auto)");

  tft.drawLine(0, 100, 240, 100, COL_GRAY);

  // --- Pump status ---
  tft.setTextColor(COL_WHITE, COL_BG);
  tft.setCursor(10, 108);
  tft.print("PUMP");

  if (pumpOn) {
    tft.fillRoundRect(155, 104, 70, 14, 3, COL_RED);
    tft.setTextColor(COL_WHITE, COL_RED);
    tft.setCursor(158, 108);
    tft.print("  ON  ");
  } else {
    tft.fillRoundRect(155, 104, 70, 14, 3, COL_DARKGRAY);
    tft.setTextColor(COL_GRAY, COL_DARKGRAY);
    tft.setCursor(158, 108);
    tft.print("  OFF ");
  }

  // --- Grow light status ---
  tft.setTextColor(COL_WHITE, COL_BG);
  tft.setCursor(10, 126);
  tft.print("GROW LIGHT");

  if (growLightOn) {
    tft.fillRoundRect(155, 122, 70, 14, 3, COL_AMBER);
    tft.setTextColor(0x0000, COL_AMBER);
    tft.setCursor(158, 126);
    tft.print("  ON  ");
  } else {
    tft.fillRoundRect(155, 122, 70, 14, 3, COL_DARKGRAY);
    tft.setTextColor(COL_GRAY, COL_DARKGRAY);
    tft.setCursor(158, 126);
    tft.print("  OFF ");
  }

  tft.drawLine(0, 140, 240, 140, COL_GRAY);

  // --- WiFi / Blynk status ---
  bool blynkConn = Blynk.connected();
  tft.setTextColor(blynkConn ? COL_GREEN : COL_RED, COL_BG);
  tft.setCursor(10, 148);
  tft.print(blynkConn ? "BLYNK: CONNECTED" : "BLYNK: OFFLINE");

  // --- Keypad input display (when entering password) ---
  if (!greenHouseLocked && keypadInput.length() == 0) {
    tft.setTextColor(COL_GRAY, COL_BG);
    tft.setCursor(10, 166);
    tft.print("# to lock  * to clear");
  }
  if (keypadInput.length() > 0) {
    tft.setTextColor(COL_BLUE, COL_BG);
    tft.setCursor(10, 166);
    tft.print("PIN: ");
    for (int i = 0; i < (int)keypadInput.length(); i++) tft.print("*");
    tft.print("_");
  }
}

// ============================================================
//  SECTION 11: KEYPAD PASSWORD + LOCK/UNLOCK
// ============================================================

char getKeyFromI2C() {
  char keys[] = "147*2580369#ABCDNF";  // mapping from your test code
  uint8_t idx = keypad.getKey();

  if (idx < 16) {
    char k = keys[idx];
    if (k != 'N' && k != 'F') {
      delay(200);  // debounce
      return k;
    }
  }
  return '\0';  // no key
}

void handleKeypad() {
  char key = getKeyFromI2C();
  if (!key) return;

  // ---------------- CLEAR INPUT ----------------
  if (key == '*') {
    keypadInput = "";
    return;
  }

  // ---------------- CONFIRM INPUT ----------------
  if (key == '#') {

    // ===== IF SYSTEM IS LOCKED → TRY UNLOCK =====
    if (greenHouseLocked) {

      if (keypadInput == GREENHOUSE_PASSWORD) {
        greenHouseLocked = false;
        wrongAttempts = 0;
        keypadInput = "";

        Blynk.virtualWrite(V6, "UNLOCKED");
        Blynk.logEvent("greenhouse_access", "UNLOCKED via keypad");

        // SUCCESS TONE
        buzzerTone(1200);
        delay(150);
        buzzerTone(1800);
        delay(150);
        buzzerOff();

      } else {
        wrongAttempts++;
        keypadInput = "";

        Blynk.virtualWrite(V6, "WRONG PASSWORD");
        Blynk.logEvent("greenhouse_access", "Wrong password attempt");

        // SHORT BEEP FOR WRONG PASSWORD
        for (int i = 0; i < 3; i++) {
          buzzerTone(2000);
          delay(150);
          buzzerOff();
          delay(150);
        }

        // ---------------- LOCK AFTER 3 FAILS ----------------
        if (wrongAttempts >= 3) {
          greenHouseLocked = true;
          wrongAttempts = 0;

          Blynk.virtualWrite(V6, "LOCKED - 3 FAILS");
          Blynk.logEvent("greenhouse_access", "🚨 LOCKED after 3 wrong attempts");

          // LONG ALARM SOUND
          for (int i = 0; i < 10; i++) {
            buzzerTone(3000);
            delay(200);
            buzzerOff();
            delay(200);
          }
        }
      }
    }

    // ===== IF SYSTEM IS UNLOCKED → LOCK IT =====
    else {
      greenHouseLocked = true;
      keypadInput = "";

      Blynk.virtualWrite(V6, "LOCKED");
      Blynk.logEvent("greenhouse_access", "Locked via keypad");

      // LOCK CONFIRM TONE
      buzzerTone(800);
      delay(200);
      buzzerOff();
    }

    return;
  }

  // ---------------- ADD DIGITS ----------------
  if (keypadInput.length() < 8) {
    keypadInput += key;
  }
}
  // --- ADD DIGITS ---

// ============================================================
//  SECTION 12: BLYNK DATA PUSH
// ============================================================

void pushToBlynk() {
  if (!Blynk.connected()) return;

  int activeThreshold = (blynkThreshold >= 0) ? blynkThreshold : moistureThreshold;

  Blynk.virtualWrite(V0, soilMoisture);
  Blynk.virtualWrite(V1, lightLevel);
  Blynk.virtualWrite(V2, pumpOn ? 1 : 0);
  Blynk.virtualWrite(V3, growLightOn ? 1 : 0);
  // V6 is updated only on lock/unlock events (see handleKeypad)
}

// ============================================================
//  SECTION 13: SETUP
// ============================================================

void setup() {
  Serial.begin(115200);
  Wire.begin();

if (!keypad.begin()) {
  Serial.println("ERROR: Keypad not detected!");
  while (1);
}
pinMode(BUZZER_PIN, OUTPUT);
digitalWrite(BUZZER_PIN, LOW);
  // --- NeoPixels ---
  pixels.begin();
  pixels.setBrightness(80); // 0–255, keep moderate
  for (int i = 0; i < NEOPIXEL_COUNT; i++) pixels.setPixelColor(i, pixels.Color(0, 0, 60));
  pixels.show();

  // --- TFT ---
  tft.init();
  tft.setRotation(3); // Landscape. Change to 0/2/3 if your screen orientation differs.
  tft.fillScreen(COL_BG);
  tft.setTextColor(COL_GREEN, COL_BG);
  tft.setTextSize(4);
  tft.setCursor(40, 50);
  tft.print("Connecting to WiFi...");

  // --- Blynk + WiFi ---
  Blynk.begin(BLYNK_AUTH_TOKEN, WIFI_SSID, WIFI_PASSWORD);

  tft.fillScreen(COL_BG);
  tft.setCursor(40, 50);
  tft.setTextColor(COL_GREEN, COL_BG);
  tft.print(Blynk.connected() ? "Blynk connected!" : "Blynk offline");
  delay(1000);

  // Set initial Blynk lock status
  Blynk.virtualWrite(V6, "LOCKED (startup)");

  Serial.println("Greenhouse controller started.");
}

// ============================================================
//  SECTION 14: MAIN LOOP
// ============================================================

void loop() {
  Blynk.run();

  unsigned long now = millis();

  // --- Read sensors every 500ms ---
  if (now - lastSensorRead >= SENSOR_INTERVAL) {
    lastSensorRead = now;
    readSensors();
    runControlLogic();
    //updateNeoPixels();
  }

  // --- Update TFT every 800ms ---
  if (now - lastTFTUpdate >= TFT_INTERVAL) {
    lastTFTUpdate = now;
    updateTFT();
  }

  // --- Push to Blynk every 2s ---
  if (now - lastBlynkUpdate >= BLYNK_INTERVAL) {
    lastBlynkUpdate = now;
    pushToBlynk();
  }

  // --- Keypad (checked every loop — fast enough for real-time response) ---
  handleKeypad();
}

// ============================================================
//  END OF FILE
//
//  BLYNK APP SETUP GUIDE:
//  ----------------------
//  1. Create a new template in Blynk.Cloud
//  2. Add these datastreams (Virtual Pins):
//       V0  Integer  0–100   "Soil Moisture %"
//       V1  Integer  0–100   "Light Level %"
//       V2  Integer  0–1     "Pump Status"
//       V3  Integer  0–1     "Grow Light Status"
//       V4  Integer  0–1     "Force Pump" (from app)
//       V5  Integer  0–100   "Moisture Threshold" (from app)
//       V6  String            "Lock Status"
//  3. On your dashboard add:
//       Gauge       → V0  (Soil Moisture)
//       Gauge       → V1  (Light Level)
//       LED         → V2  (Pump — red)
//       LED         → V3  (Grow Light — green)
//       Button      → V4  (Force Pump, SWITCH mode)
//       Slider      → V5  (Threshold, 0–100)
//       Label       → V6  (Lock Status)
//  4. Create Events (for push notifications):
//       Event ID: "greenhouse_access"
//       Enable push notifications for this event.
//  5. Flash this code, open Serial Monitor at 115200 baud
//     to confirm WiFi + Blynk connection.
// ============================================================
