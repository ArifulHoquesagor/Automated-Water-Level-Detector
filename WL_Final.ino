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
int prevLevel = -1;

// Relay + app state
#define RELAY_PIN 26
int  state = 0;                 // 0 = off, 1 = on
bool motorStatus = false;
bool userOffOverride = false;

// Mode button (Manual default)
#define MODE_BTN_PIN 5
bool autoMode = false;          // false = MANUAL, true = AUTO

// Motor push button
#define MOTOR_BTN_PIN 33

// -------- Buzzer (GPIO 27) + Alarm button (GPIO 4) --------
#define BUZZER_PIN 27
#define ALARM_BTN_PIN 4   // to GND, using INPUT_PULLUP

bool buzzerMuted = false;        // true = muted

// Alarm pattern (gentle, non-blocking)
bool buzzerActive  = false;
bool buzzerOnPhase = false;
unsigned long buzzerNextEvent = 0;
const unsigned long BEEP_ON_MS  = 180;
const unsigned long BEEP_OFF_MS = 220;

// Short feedback beep (on any button press)
bool clickBeepActive = false;
unsigned long clickBeepEnd = 0;
const unsigned long CLICK_BEEP_MS = 70;

// Debounce vars (mode button)
int lastStableBtn  = HIGH;
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

// ----- UI state machine -----
enum UIScreen { UI_WIFI, UI_TEMP_MAIN, UI_MAIN };
UIScreen uiScreen = UI_WIFI;     // start with Wi-Fi screen at boot

// When Wi-Fi is down and LoRa arrives, show main UI for 5s:
bool tempMainActive = false;
unsigned long tempMainUntil = 0;

// For Wi-Fi splash animation
uint8_t wifiDotsStep = 0;
unsigned long lastWifiDots = 0;

// Blynk non-blocking mode
bool blynkReady = false;

// TFT display setup
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);

// Forward declarations
void drawMainUI();
void showWiFiConnectingInit();
void drawWiFiConnectingDots(uint8_t step);
void applyLogic();
void driveRelayAndSync();
void drawModeIndicator(char);
void drawMotorStatus();
void showLevel(int);
void drawLevelBar(int);
void showSNR(int);
void drawPattern();
void drawWiFiSignal(int);
void displayDeviceID(String);
void updateBuzzer();
void rearmBuzzerIfNeeded();
void requestClickBeep();

// Helper: main-like UI guard
inline bool isMainLikeUI() {
  return (uiScreen == UI_MAIN || uiScreen == UI_TEMP_MAIN);
}

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
  if (!isMainLikeUI()) return;
  tft.setFont();
  tft.setTextSize(2);
  tft.setCursor(4, 113);
  tft.setTextColor(TFT_WHITE, TFT_NAVY);
  tft.print("Motor: ");
  tft.setTextColor(motorStatus ? TFT_GREEN : TFT_RED, TFT_NAVY);
  tft.print(motorStatus ? "ON " : "OFF");
}

void driveRelayAndSync() {
  // flip logic here if your relay module is active-LOW
  digitalWrite(RELAY_PIN, motorStatus ? HIGH : LOW);
  state = motorStatus ? 1 : 0;
  if (blynkReady) Blynk.virtualWrite(V2, state);
  drawMotorStatus(); // guarded inside
}

// Auto/Manual logic (unchanged)
void applyLogic() {
  if (autoMode) {
    if (level != 0) {
      motorStatus = false;
      userOffOverride = false;
    } else {
      motorStatus = !userOffOverride;
    }
  } else {
    // Manual: motorStatus via V2 or physical motor button
  }
  driveRelayAndSync();
}

// ----- CLICK BEEP helper -----
void requestClickBeep() {
  clickBeepActive = true;
  clickBeepEnd = millis() + CLICK_BEEP_MS;
  digitalWrite(BUZZER_PIN, HIGH);   // immediate short chirp
}

// ---------- Buzzer helpers ----------
void updateBuzzer() {
  unsigned long now = millis();

  // Click beep has priority
  if (clickBeepActive) {
    if (now >= clickBeepEnd) {
      clickBeepActive = false;
      digitalWrite(BUZZER_PIN, LOW);
      // reset alarm pattern timing
      buzzerActive = false;
      buzzerOnPhase = false;
      buzzerNextEvent = now;
    }
    return;
  }

  bool shouldBeeping = (level == 100) && !buzzerMuted;

  if (!shouldBeeping) {
    buzzerActive = false;
    buzzerOnPhase = false;
    digitalWrite(BUZZER_PIN, LOW);
    buzzerNextEvent = now;
    return;
  }

  if (!buzzerActive) {
    buzzerActive = true;
    buzzerOnPhase = true;
    digitalWrite(BUZZER_PIN, HIGH);
    buzzerNextEvent = now + BEEP_ON_MS;
    return;
  }

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

void rearmBuzzerIfNeeded() {
  if (prevLevel == 100 && level != 100) {
    buzzerMuted = false;
    if (blynkReady) Blynk.virtualWrite(V4, 1); // show enabled again
  }
}

// ---------------- Wi-Fi splash helpers ----------------
void showWiFiConnectingInit() {
  uiScreen = UI_WIFI;
  tempMainActive = false; // cancel any temp main
  tft.fillScreen(TFT_NAVY);
  tft.setFont(&FreeSans12pt7b);
  tft.setTextColor(TFT_CYAN);
  tft.setCursor(8, 30);
  tft.print("Connecting");
  tft.setCursor(8, 55);
  tft.print("to WiFi");
  tft.setFont(); // default
  tft.setTextColor(TFT_WHITE, TFT_NAVY);
  tft.setCursor(8, 75);
  tft.setFont(&FreeSans9pt7b);
  tft.print("SSID: ");
  tft.print(ssid);
  // tft.setCursor(8, 95);
  // tft.print("Please be patient");
}

void drawWiFiConnectingDots(uint8_t step) {
  if (uiScreen != UI_WIFI) return;
  uint8_t n = step % 4;
  tft.setFont();
  tft.setTextSize(1);
  tft.setTextColor(TFT_RED, TFT_NAVY);
  tft.setCursor(8, 95);
  tft.print("Please be patient");
  for (uint8_t i = 0; i < n; i++) tft.print(".");
  for (uint8_t i = n; i < 3; i++) tft.print(" ");
}

void drawMainUI() {
  uiScreen = UI_MAIN;
  tft.fillScreen(TFT_NAVY);

  displayDeviceID(deviceID);
  drawWiFiSignal(getSignalBars());
  drawPattern();
  drawLevelBar(level);
  drawDivider();
  showLevel(level);
  showSNR(snr);
  drawModeIndicator(currentMode);
  drawMotorStatus();
}

// ---------- setup/loop ----------
void setup() {
  Serial.begin(9600);

  SPI.begin();
  tft.initR(INITR_BLACKTAB);
  tft.fillScreen(TFT_NAVY);
  tft.setRotation(3);

  // Pins
  pinMode(MODE_BTN_PIN,  INPUT_PULLUP);
  pinMode(MOTOR_BTN_PIN, INPUT_PULLUP);
  pinMode(ALARM_BTN_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);

  // Show Wi-Fi screen and start Wi-Fi (non-blocking)
  showWiFiConnectingInit();
  WiFi.begin(ssid, pass);

  // LoRa
  LoRa.setPins(SS, RST);
  if (!LoRa.begin(433E6)) { Serial.println("LoRa error!"); while (1); }
  LoRa.setSpreadingFactor(8);
  LoRa.setSignalBandwidth(62.5E3);
  LoRa.setCodingRate4(8);
  Serial.println("LoRa Receiver Ready...");

  // Initial mode from physical button (optional)
  autoMode = (digitalRead(MODE_BTN_PIN) == LOW);  // pressed -> AUTO
  currentMode = autoMode ? 'A' : 'M';
}

BLYNK_CONNECTED() {
  Blynk.syncVirtual(V2);
  Blynk.virtualWrite(V0, level);
  Blynk.virtualWrite(V1, snr);
  Blynk.virtualWrite(V2, state);
  Blynk.virtualWrite(V3, autoMode ? 1 : 0);
  Blynk.virtualWrite(V4, buzzerMuted ? 0 : 1);
}

void loop() {
  if (blynkReady) Blynk.run();

  // Animate Wi-Fi splash dots every 400ms
  if (uiScreen == UI_WIFI) {
    unsigned long now = millis();
    if (now - lastWifiDots > 400) {
      lastWifiDots = now;
      drawWiFiConnectingDots(wifiDotsStep++);
    }
  }

  // --- Mode button (GPIO 5) ---
  int reading = digitalRead(MODE_BTN_PIN);
  if (reading != lastReadingBtn) lastDebounceTime = millis();
  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (reading != lastStableBtn) {
      lastStableBtn = reading;
      if (lastStableBtn == LOW) {
        autoMode = !autoMode;
        currentMode = autoMode ? 'A' : 'M';
        Serial.println(autoMode ? "Mode: AUTO" : "Mode: MANUAL");
        if (autoMode) userOffOverride = false;
        applyLogic();
        if (blynkReady) Blynk.virtualWrite(V3, autoMode ? 1 : 0);
        if (isMainLikeUI()) drawModeIndicator(currentMode);
        requestClickBeep();
      }
    }
  }
  lastReadingBtn = reading;

  // --- Motor button (GPIO 33) ---
  int motorReading = digitalRead(MOTOR_BTN_PIN);
  if (motorReading != lastReadingMotorBtn) lastMotorDebounceTime = millis();
  if ((millis() - lastMotorDebounceTime) > debounceDelay) {
    if (motorReading != lastStableMotorBtn) {
      lastStableMotorBtn = motorReading;
      if (lastStableMotorBtn == LOW) {
        if (autoMode) {
          if (level == 100) {
            userOffOverride = motorStatus ? true : false;
            applyLogic();
          } else {
            userOffOverride = false;
            applyLogic();
            Serial.println("AUTO mode: ignored motor button (level != 100)");
          }
        } else {
          motorStatus = !motorStatus;
          userOffOverride = false;
          driveRelayAndSync();
        }
        requestClickBeep();
      }
    }
  }
  lastReadingMotorBtn = motorReading;

  // --- Alarm/mute button (GPIO 4) ---
  int alarmReading = digitalRead(ALARM_BTN_PIN);
  if (alarmReading != lastReadingAlarmBtn) lastAlarmDebounceTime = millis();
  if ((millis() - lastAlarmDebounceTime) > debounceDelay) {
    if (alarmReading != lastStableAlarmBtn) {
      lastStableAlarmBtn = alarmReading;
      if (lastStableAlarmBtn == LOW) {
        buzzerMuted = true;                 // mute
        if (blynkReady) Blynk.virtualWrite(V4, 0);
        requestClickBeep();
      }
    }
  }
  lastReadingAlarmBtn = alarmReading;

  // housekeeping
  if (millis() - lastWiFiCheck >= wifiCheckInterval) {
    lastWiFiCheck = millis();
    // Only switch to Wi-Fi screen if not in temp main preview
    if (WiFi.status() != WL_CONNECTED) {
      if (!tempMainActive) {
        // keep trying to reconnect (non-blocking visual)
        showWiFiConnectingInit();
        WiFi.begin(ssid, pass);
      }
    } else {
      // Wi-Fi connected
      if (!blynkReady) {
        // Start Blynk without touching WiFi
        Blynk.config(BLYNK_AUTH_TOKEN);
        Blynk.connect(5000);
        blynkReady = Blynk.connected();
      }
      drawMainUI(); // show full UI permanently
      applyLogic();
    }
  }

  // If we are in temporary main screen, auto-return to Wi-Fi after 5s
  if (tempMainActive && millis() > tempMainUntil && WiFi.status() != WL_CONNECTED) {
    showWiFiConnectingInit();
    tempMainActive = false;
  }

  // periodic Wi-Fi bar refresh while on main-like UI
  if (millis() - lastSignalUpdate >= 3000) {
    lastSignalUpdate = millis();
    if (isMainLikeUI()) drawWiFiSignal(getSignalBars());
  }

  // buzzer handling
  updateBuzzer();

  parseLoRaData();
}

// ---------- display functions ----------
void displayDeviceID(String deviceID) {
  if (!isMainLikeUI()) return;
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
  if (!isMainLikeUI()) return;
  tft.fillRect(105, 0, 60, 20, TFT_NAVY);
  tft.setFont();
  tft.setTextColor(TFT_GREEN);
  tft.setTextSize(1);
  tft.setCursor(110, 4);
  tft.println("WIFI");

  int x = 140, y = 12;
  auto arc = [&](int r, bool on) {
    for (int angle = 1; angle <= 90; angle++) {
      float rad = angle * 3.14159 / 180;
      int xPos = x + r * cos(rad);
      int yPos = y - r * sin(rad);
      tft.drawPixel(xPos, yPos, on ? TFT_CYAN : TFT_ASH);
    }
  };
  arc(1,  (signalStrength > 0));
  arc(4,  (signalStrength > 1));
  arc(7,  (signalStrength > 2));
  arc(10, (signalStrength > 3));
}

void drawPattern() {
  if (!isMainLikeUI()) return;
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

void drawLevelBar(int level_) {
  if (!isMainLikeUI()) return;
  int segments = 10;
  int segmentWidth = 20;
  int segmentHeight = 5;
  int blueSegments = map(level_, 0, 100, 0, segments);

  for (int i = 0; i < segments; i++) {
    int x = 5;
    int y = 40 + (segments - 1 - i) * (segmentHeight + 2);
    tft.fillRect(x, y, segmentWidth, segmentHeight,
                 (i < blueSegments) ? TFT_BLUE : TFT_ASH);
  }
}

void drawDivider() {
  if (!isMainLikeUI()) return;
  tft.fillRect(78, 42, 4, 6, TFT_RED);
  tft.fillRect(78, 52, 4, 6, TFT_RED);
  tft.fillRect(78, 62, 4, 6, TFT_RED);
  tft.fillRect(78, 72, 4, 6, TFT_RED);
  tft.fillRect(78, 82, 4, 6, TFT_RED);
  tft.fillRect(78, 92, 4, 6, TFT_RED);
}

void showLevel(int level_) {
  if (!isMainLikeUI()) return;
  tft.fillRect(30, 55, 45, 40, TFT_NAVY);
  tft.setFont();
  tft.setTextColor(TFT_WHITE);
  tft.setFont(&FreeSans12pt7b);
  tft.setCursor(31, 72);
  tft.setTextSize(1);
  tft.print(level_);
  tft.setFont(&FreeSans9pt7b);
  tft.setCursor(31, 90);
  tft.print("Level");
}

void showSNR(int snr_) {
  if (!isMainLikeUI()) return;
  tft.fillRect(95, 55, 45, 40, TFT_NAVY);
  tft.setFont();
  tft.setTextColor(TFT_WHITE);
  tft.setFont(&FreeSans12pt7b);
  tft.setCursor(98, 72);
  tft.setTextSize(1);
  tft.print(snr_);
  tft.setFont(&FreeSans9pt7b);
  tft.setCursor(95, 90);
  tft.print("SNR");
}

// "A" or "M" on display, clearing background first
void drawModeIndicator(char currentMode_) {
  if (!isMainLikeUI()) return;
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);
  tft.setFont();
  tft.setCursor(42, 23);
  tft.print("Mode:");
  tft.setFont(&FreeSerifBold12pt7b);
  tft.setTextSize(1);
  tft.fillRect(100, 18, 26, 20, TFT_NAVY);  // clear previous

  if (currentMode_ == 'A') {
    tft.setTextColor(TFT_RED);
    tft.setCursor(100, 35);
    tft.print("A");
  } else {
    tft.setTextColor(TFT_GREEN);
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
  level = newLevel;
  count = newCount;
  snr = snrLocal;

  rearmBuzzerIfNeeded();

  // If we're on the Wi-Fi screen (disconnected), show a 5s preview of main UI
  if (uiScreen == UI_WIFI && WiFi.status() != WL_CONNECTED) {
    drawMainUI();
    uiScreen = UI_TEMP_MAIN;
    tempMainActive = true;
    tempMainUntil = millis() + 5000UL;
  }

  // Draw on any main-like UI
  if (isMainLikeUI()) {
    showLevel(level);
    drawLevelBar(level);
    showSNR(snr);
  }

  if (blynkReady) {
    Blynk.virtualWrite(V0, level);
    Blynk.virtualWrite(V1, snr);
  }

  applyLogic();
}

// ---------- Blynk controls ----------
// V2: Motor ON/OFF
BLYNK_WRITE(V2) {
  int btn = param.asInt();  // 1=ON, 0=OFF

  if (autoMode) {
    if (level == 100) {
      userOffOverride = (btn == 0);
    } else {
      userOffOverride = false;
      motorStatus = false;
    }
  } else {
    motorStatus = (btn == 1);
    userOffOverride = false;
  }

  applyLogic();
}

// V3: Mode (0=Manual, 1=Auto)
BLYNK_WRITE(V3) {
  autoMode = param.asInt();
  Serial.println(autoMode ? "Mode: AUTO" : "Mode: MANUAL");
  if (blynkReady) Blynk.virtualWrite(V3, autoMode ? 1 : 0);
  currentMode = autoMode ? 'A' : 'M';
  if (isMainLikeUI()) drawModeIndicator(currentMode);
}

// V4: Buzzer enable (1) / mute (0) from app (only mute allowed while full)
BLYNK_WRITE(V4) {
  int v = param.asInt();
  if (v == 0) {
    buzzerMuted = true;     // user can mute any time
  }
  // If user tries to set 1 while level==100, ignore (stay muted)
  if (blynkReady) Blynk.virtualWrite(V4, buzzerMuted ? 0 : 1);
}

// ---------- Wi-Fi reconnect (non-blocking UI) ----------
void checkWiFiConnection() {
  // If temp preview is active, do not interrupt it with Wi-Fi screen
  if (tempMainActive) return;

  if (WiFi.status() != WL_CONNECTED) {
    // Keep trying to reconnect and keep showing Wi-Fi UI
    showWiFiConnectingInit();
    WiFi.begin(ssid, pass);
  } else {
    // Wi-Fi connected: init Blynk if not yet, then show main UI permanently
    if (!blynkReady) {
      Blynk.config(BLYNK_AUTH_TOKEN);
      Blynk.connect(5000);
      blynkReady = Blynk.connected();
    }
    drawMainUI();
    applyLogic();
  }
}
