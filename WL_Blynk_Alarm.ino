#define BLYNK_TEMPLATE_ID "TMPL6wMpQ-cJg"
#define BLYNK_TEMPLATE_NAME "Water Level Device"
#define BLYNK_AUTH_TOKEN "-1ew1nIGHZDimZpv95E3nOCy9TaQgAuu"

#include <SPI.h>
#include <LoRa.h>
#include <WiFi.h>
#include <BlynkSimpleEsp32.h>

char ssid[] = "iQOO Neo9";
char pass[] = "19702011";

unsigned long lastWiFiCheck = 0;
const unsigned long wifiCheckInterval = 10000;
unsigned long lastSignalUpdate = 0;

#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <Fonts/FreeSerifBold18pt7b.h>
#include <Fonts/FreeSerifBold12pt7b.h>
#include <Fonts/FreeSerifBold9pt7b.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSans18pt7b.h>

// TFT pins
#define TFT_CS 15
#define TFT_DC 18
#define TFT_SCLK 14
#define TFT_MOSI 13
#define TFT_RST 88

// Colors
#define TFT_BLACK 0x0000
#define TFT_BLUE  0x001F
#define TFT_RED   0xF800
#define TFT_GREEN 0x07E0
#define TFT_WHITE 0xFFFF
#define TFT_CYAN  0x07FF
#define TFT_NAVY  0x18C6
#define TFT_ASH   0xC618

// LoRa pins
#define SS 2
#define RST 25

const byte receiverAddress = 0x15;
const byte senderAddress   = 0x16;

String deviceID = "WL1000000000";

// Telemetry/state
int level = 0;
int snr = 0;
int count = 0;
int prevLevel = -1;   // track last level to re-arm buzzer when it drops

// Relay + app state
#define RELAY_PIN 26
int  state = 0;                 // 0 = off, 1 = on (mirrors relay & V2)
bool motorStatus = false;       // actual intended output

// Mode button (Manual default)
#define MODE_BTN_PIN 5
bool autoMode = false;          // false = MANUAL (default), true = AUTO

// Motor push button
#define MOTOR_BTN_PIN 33

// -------- Buzzer (GPIO 27) + Alarm button (GPIO 4) --------
#define BUZZER_PIN 27
#define ALARM_BTN_PIN 4   // wired to GND, using INPUT_PULLUP

// “buzzerMuted” mutes only during the current full state (level==100).
// When level goes below 100, it auto re-arms (buzzerMuted=false).
bool buzzerMuted = false;        // true = muted; false = allowed to beep now

// Beep pattern (gentle, non-blocking)
bool buzzerActive  = false;      // currently running the pattern
bool buzzerOnPhase = false;
unsigned long buzzerNextEvent = 0;
const unsigned long BEEP_ON_MS  = 180;
const unsigned long BEEP_OFF_MS = 220;

// Debounce vars (mode button)
int lastStableBtn  = HIGH;      // with INPUT_PULLUP, HIGH = not pressed
int lastReadingBtn = HIGH;
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50;

// Debounce vars (motor button)
int lastStableMotorBtn  = HIGH;
int lastReadingMotorBtn = HIGH;
unsigned long lastMotorDebounceTime = 0;

// Debounce vars (alarm button)
int lastStableAlarmBtn  = HIGH;
int lastReadingAlarmBtn = HIGH;
unsigned long lastAlarmDebounceTime = 0;

// Mode display variable
char currentMode = 'M';  // 'M' or 'A'

// TFT display setup
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);

// ---------- helpers ----------
int getSignalBars() {
  long rssi = WiFi.RSSI();
  if (rssi >= -50) return 4;
  else if (rssi >= -60) return 3;
  else if (rssi >= -70) return 2;
  else if (rssi >= -80) return 1;
  else return 0;
}

// Show Motor ON/OFF just below the blue pattern line (blue line y=110)
void drawMotorStatus() {
  tft.setFont();                               // small default font
  tft.setTextSize(2);
  tft.setCursor(4, 113);
  tft.setTextColor(TFT_WHITE);
  tft.print("Motor: ");
  tft.fillRect(80, 111, 160, 16, TFT_NAVY);    // clear the value area
  tft.setFont();
  tft.setTextSize(2);
  tft.setTextColor(motorStatus ? TFT_GREEN : TFT_RED);
  tft.print(motorStatus ? "ON" : "OFF");
}

void driveRelayAndSync() {
  // flip logic here if your relay module is active-LOW
  digitalWrite(RELAY_PIN, motorStatus ? HIGH : LOW);
  state = motorStatus ? 1 : 0;
  Blynk.virtualWrite(V2, state);
  drawMotorStatus(); // keep display in sync
}

// Apply the new AUTO rules:
// - AUTO: level==0 -> ON, level==100 -> OFF, otherwise keep last state
// - MANUAL: controlled by V2/MOTOR_BTN only
void applyLogic() {
  if (autoMode) {
    if (level == 100) {
      motorStatus = false;
    } else if (level == 0) {
      motorStatus = true;
    } // for 25/50/75 we keep previous motorStatus (no change)
  }
  driveRelayAndSync();
}

// ---------- Buzzer helpers ----------
// Start/stop the buzzer non-blocking pattern based on level and mute
void updateBuzzer() {
  unsigned long now = millis();
  bool shouldBeeping = (level == 100) && !buzzerMuted;

  if (!shouldBeeping) {
    // ensure off
    buzzerActive = false;
    buzzerOnPhase = false;
    digitalWrite(BUZZER_PIN, LOW);
    buzzerNextEvent = now;
    return;
  }

  if (!buzzerActive) {
    // start pattern
    buzzerActive = true;
    buzzerOnPhase = true;
    digitalWrite(BUZZER_PIN, HIGH);
    buzzerNextEvent = now + BEEP_ON_MS;
    return;
  }

  // advance pattern
  if (now >= buzzerNextEvent) {
    if (buzzerOnPhase) {
      digitalWrite(BUZZER_PIN, LOW);
      buzzerOnPhase = false;
      buzzerNextEvent = now + BEEP_OFF_MS;
    } else {
      digitalWrite(BUZZER_PIN, HIGH);
      buzzerOnPhase = true;
      buzzerNextEvent = now + BEEP_ON_MS;
    }
  }
}

// Re-arm buzzer when level leaves 100
void rearmBuzzerIfNeeded() {
  if (prevLevel == 100 && level != 100) {
    buzzerMuted = false;               // allow next alarm when full again
    Blynk.virtualWrite(V4, 1);         // show "enabled" in app
  }
}

// ---------- setup/loop ----------
void setup() {
  Serial.begin(9600);

  WiFi.begin(ssid, pass);
  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println();
  Serial.print("WiFi Connected. IP Address: ");
  Serial.println(WiFi.localIP());

  SPI.begin();
  tft.initR(INITR_BLACKTAB);
  tft.fillScreen(TFT_NAVY);
  tft.setRotation(3);

  // Buttons
  pinMode(MODE_BTN_PIN, INPUT_PULLUP);
  pinMode(MOTOR_BTN_PIN, INPUT_PULLUP);
  pinMode(ALARM_BTN_PIN, INPUT_PULLUP);   // button to GND

  // Buzzer pin
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  displayDeviceID(deviceID);
  drawWiFiSignal(getSignalBars());
  drawPattern();
  drawLevelBar(level);
  drawDivider();
  showLevel(level);
  showSNR(snr);

  // Initial mode from physical button (optional)
  autoMode = (digitalRead(MODE_BTN_PIN) == LOW);  // pressed -> AUTO
  currentMode = autoMode ? 'A' : 'M';
  drawModeIndicator(currentMode);

  LoRa.setPins(SS, RST);
  if (!LoRa.begin(433E6)) { Serial.println("LoRa error!"); while (1); }
  LoRa.setSpreadingFactor(8);
  LoRa.setSignalBandwidth(62.5E3);
  LoRa.setCodingRate4(8);
  Serial.println("LoRa Receiver Ready...");

  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);

  applyLogic(); // reflect initial state to app/relay + draws motor status
}

BLYNK_CONNECTED() {
  Blynk.syncVirtual(V2);                 // get last app switch position (for manual mode)
  Blynk.virtualWrite(V0, level);
  Blynk.virtualWrite(V1, snr);
  Blynk.virtualWrite(V2, state);
  Blynk.virtualWrite(V3, autoMode ? 1 : 0);
  Blynk.virtualWrite(V4, buzzerMuted ? 0 : 1); // 1=enabled (not muted), 0=muted
}

void loop() {
  Blynk.run();

  // --- Mode toggle button (GPIO 5) ---
  int reading = digitalRead(MODE_BTN_PIN);
  if (reading != lastReadingBtn) lastDebounceTime = millis();
  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (reading != lastStableBtn) {
      lastStableBtn = reading;
      if (lastStableBtn == LOW) {             // pressed
        autoMode = !autoMode;
        currentMode = autoMode ? 'A' : 'M';
        Serial.println(autoMode ? "Mode: AUTO" : "Mode: MANUAL");
        applyLogic();                          // apply current rules immediately
        Blynk.virtualWrite(V3, autoMode ? 1 : 0);
        drawModeIndicator(currentMode);
      }
    }
  }
  lastReadingBtn = reading;

  // --- Motor ON/OFF button (GPIO 33) ---
  int motorReading = digitalRead(MOTOR_BTN_PIN);
  if (motorReading != lastReadingMotorBtn) lastMotorDebounceTime = millis();
  if ((millis() - lastMotorDebounceTime) > debounceDelay) {
    if (motorReading != lastStableMotorBtn) {
      lastStableMotorBtn = motorReading;
      if (lastStableMotorBtn == LOW) {       // pressed
        if (autoMode) {
          // In AUTO we ignore manual motor toggles; logic is tied to level
          Serial.println("AUTO mode: motor button ignored");
        } else {
          // MANUAL: direct toggle
          motorStatus = !motorStatus;
          driveRelayAndSync();
        }
      }
    }
  }
  lastReadingMotorBtn = motorReading;

  // --- Alarm button (GPIO 4): mutes buzzer while level stays 100 ---
  int alarmReading = digitalRead(ALARM_BTN_PIN);
  if (alarmReading != lastReadingAlarmBtn) lastAlarmDebounceTime = millis();
  if ((millis() - lastAlarmDebounceTime) > debounceDelay) {
    if (alarmReading != lastStableAlarmBtn) {
      lastStableAlarmBtn = alarmReading;
      if (lastStableAlarmBtn == LOW) {       // pressed (active-LOW)
        buzzerMuted = true;                  // mute now
        Blynk.virtualWrite(V4, 0);           // reflect in app
      }
    }
  }
  lastReadingAlarmBtn = alarmReading;

  // housekeeping
  if (millis() - lastWiFiCheck >= wifiCheckInterval) {
    lastWiFiCheck = millis();
    checkWiFiConnection();
  }
  if (millis() - lastSignalUpdate >= 3000) {
    lastSignalUpdate = millis();
    drawWiFiSignal(getSignalBars());
  }

  // continuous buzzer handling
  updateBuzzer();

  parseLoRaData();
}

// ---------- display functions ----------
void displayDeviceID(String deviceID) {
  tft.fillScreen(TFT_NAVY);
  tft.setFont();
  tft.setTextColor(TFT_CYAN);
  tft.setTextSize(1);
  tft.setCursor(3, 4);
  tft.println(deviceID);
}

void drawArc(int x, int y, int r, uint16_t color) {
  tft.setFont(&FreeSerifBold18pt7b);
  for (int angle = 1; angle <= 90; angle++) {
    float rad = angle * 3.14159 / 180;
    int xPos = x + r * cos(rad);
    int yPos = y - r * sin(rad);
    tft.drawPixel(xPos, yPos, color);
  }
  tft.setFont();
}

void drawWiFiSignal(int signalStrength) {
  tft.fillRect(105, 0, 60, 20, TFT_NAVY);
  tft.setFont();
  tft.setTextColor(TFT_GREEN);
  tft.setTextSize(1);
  tft.setCursor(110, 4);
  tft.println("WIFI");

  int x = 140, y = 12;
  drawArc(x, y, 1,  (signalStrength > 0) ? TFT_CYAN : TFT_ASH);
  drawArc(x, y, 4,  (signalStrength > 1) ? TFT_CYAN : TFT_ASH);
  drawArc(x, y, 7,  (signalStrength > 2) ? TFT_CYAN : TFT_ASH);
  drawArc(x, y, 10, (signalStrength > 3) ? TFT_CYAN : TFT_ASH);
}

void drawPattern() {
  tft.drawLine(0, 30, 35, 30, TFT_RED);
  tft.drawLine(35, 30, 40, 40, TFT_RED);
  tft.drawLine(40, 40, 125, 40, TFT_RED);
  tft.drawLine(125, 40, 130, 30, TFT_RED);
  tft.drawLine(130, 30, 160, 30, TFT_RED);

  tft.drawLine(0, 110, 35, 110, TFT_BLUE);
  tft.drawLine(35, 110, 40, 100, TFT_BLUE);
  tft.drawLine(40, 100, 125, 100, TFT_BLUE);
  tft.drawLine(125, 100, 130, 110, TFT_BLUE);
  tft.drawLine(130, 110, 160, 110, TFT_BLUE);
}

void drawLevelBar(int level) {
  int segments = 10;
  int segmentWidth = 20;
  int segmentHeight = 5;
  int blueSegments = map(level, 0, 100, 0, segments);

  for (int i = 0; i < segments; i++) {
    int x = 5;
    int y = 40 + (segments - 1 - i) * (segmentHeight + 2);
    tft.fillRect(x, y, segmentWidth, segmentHeight,
                 (i < blueSegments) ? TFT_BLUE : TFT_ASH);
  }
}

void drawDivider() {
  tft.fillRect(78, 42, 4, 6, TFT_RED);
  tft.fillRect(78, 52, 4, 6, TFT_RED);
  tft.fillRect(78, 62, 4, 6, TFT_RED);
  tft.fillRect(78, 72, 4, 6, TFT_RED);
  tft.fillRect(78, 82, 4, 6, TFT_RED);
  tft.fillRect(78, 92, 4, 6, TFT_RED);
}

void showLevel(int level) {
  tft.fillRect(30, 55, 45, 40, TFT_NAVY);
  tft.setFont();
  tft.setTextColor(TFT_WHITE);
  tft.setFont(&FreeSans12pt7b);
  tft.setCursor(31, 72);
  tft.setTextSize(1);
  tft.print(level);
  tft.setFont(&FreeSans9pt7b);
  tft.setCursor(31, 90);
  tft.print("Level");
}

void showSNR(int snr) {
  tft.fillRect(95, 55, 45, 40, TFT_NAVY);
  tft.setFont();
  tft.setTextColor(TFT_WHITE);
  tft.setFont(&FreeSans12pt7b);
  tft.setCursor(98, 72);
  tft.setTextSize(1);
  tft.print(snr);
  tft.setFont(&FreeSans9pt7b);
  tft.setCursor(95, 90);
  tft.print("SNR");
}

// "A" or "M" on display, clearing background first
void drawModeIndicator(char currentMode) {
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);
  tft.setFont();
  tft.setCursor(42, 23);
  tft.print("Mode:");
  tft.setFont(&FreeSerifBold12pt7b);
  tft.setTextSize(1);
  tft.fillRect(100, 18, 26, 20, TFT_NAVY);  // clear previous

  if (currentMode == 'A') {
    tft.setTextColor(TFT_RED);
    tft.setCursor(100, 35);
    tft.print("A");
  } else {
    tft.setTextColor(TFT_BLUE);
    tft.setCursor(100, 37);
    tft.print("M");
  }
}

// ---------- LoRa parsing ----------
void parseLoRaData() {
  int packetSize = LoRa.parsePacket();
  if (!packetSize) return;

  byte destAddress = LoRa.read();
  byte senderAddress = LoRa.read();
  if (destAddress != receiverAddress) return;

  String data = "";
  while (LoRa.available()) data += (char)LoRa.read();

  int separatorIndex = data.indexOf(',');
  int snrLocal = LoRa.packetSnr() + 18;
  Serial.print("SNR "); Serial.println(snrLocal);
  if (separatorIndex == -1) return;

  int newLevel = data.substring(0, separatorIndex).toInt();
  Serial.print("Level "); Serial.println(newLevel);
  int newCount = data.substring(separatorIndex + 1).toInt();

  prevLevel = level;  // remember previous before update
  // Update UI & telemetry
  level = newLevel;
  count = newCount;
  snr = snrLocal;

  // If we left "full", re-arm buzzer for next time we hit 100
  rearmBuzzerIfNeeded();

  showLevel(level);
  drawLevelBar(level);
  showSNR(snr);
  Blynk.virtualWrite(V0, level);
  Blynk.virtualWrite(V1, snr);

  // Apply current mode rules (this will switch motor on/off for 0/100 in AUTO)
  applyLogic();
}

// ---------- Blynk controls ----------
// V2: Motor ON/OFF
BLYNK_WRITE(V2) {
  int btn = param.asInt();  // 1=ON, 0=OFF

  if (autoMode) {
    // In AUTO we ignore manual switch; push back real state
    Blynk.virtualWrite(V2, motorStatus ? 1 : 0);
  } else {
    // MANUAL: direct control
    motorStatus = (btn == 1);
    driveRelayAndSync();
  }
}

// V3: Mode (0=Manual, 1=Auto)
BLYNK_WRITE(V3) {
  autoMode = param.asInt();
  Serial.println(autoMode ? "Mode: AUTO" : "Mode: MANUAL");
  Blynk.virtualWrite(V3, autoMode ? 1 : 0);
  currentMode = autoMode ? 'A' : 'M';
  drawModeIndicator(currentMode);
  applyLogic();   // immediately apply auto rule if needed
}

// V4: Buzzer enable (1) / mute (0) from app
// V4: Buzzer enable (1) / mute (0) from app
BLYNK_WRITE(V4) {
  int v = param.asInt();
  if (v == 0) {
    // User requested to mute -> always allowed
    buzzerMuted = true;
  } else {
    // User tried to unmute from app -> not allowed while level==100.
    // We don't change buzzerMuted here; just resync the switch to reality.
    // (When level drops <100, rearmBuzzerIfNeeded() will auto-set V4=1.)
  }
  // Push actual state back to the app so UI matches logic:
  Blynk.virtualWrite(V4, buzzerMuted ? 0 : 1);
}

// ---------- WiFi reconnect ----------
void checkWiFiConnection() {
  if (WiFi.status() == WL_CONNECTED) return;

  Serial.println("WiFi disconnected. Trying to reconnect...");
  WiFi.begin(ssid, pass);
  unsigned long startAttemptTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 5000) {
    delay(500);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nReconnected to WiFi.");
    Serial.print("IP Address: "); Serial.println(WiFi.localIP());
    Blynk.virtualWrite(V0, level);
    Blynk.virtualWrite(V1, snr);
    Blynk.virtualWrite(V2, state);
  } else {
    Serial.println("\nFailed to reconnect to WiFi.");
  }
}
