#define BLYNK_TEMPLATE_ID "TMPL6wMpQ-cJg"
#define BLYNK_TEMPLATE_NAME "Water Level Device"
#define BLYNK_AUTH_TOKEN "-1ew1nIGHZDimZpv95E3nOCy9TaQgAuu"

#include <SPI.h>
#include <LoRa.h>
#include <WiFi.h>
#include <BlynkSimpleEsp32.h>

char ssid[] = "STELLAR";
char pass[] = "stellarBD";

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

// Relay + app state
#define RELAY_PIN 26
int state = 0;                 // 0 = off, 1 = on (mirrors relay & V2)
bool motorStatus = false;      // actual intended output
bool userOffOverride = false;  // Auto@100 → user can force OFF via V2

// Mode button (Manual default)
#define MODE_BTN_PIN 5
bool autoMode = false;          // false = MANUAL (default), true = AUTO

// Debounce vars
int lastStableBtn  = HIGH;      // with INPUT_PULLUP, HIGH = not pressed
int lastReadingBtn = HIGH;
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50;

// Mode display variable (stores the current mode)
char currentMode = 'M';  // Default to Manual mode ('M' or 'A')

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

void driveRelayAndSync() {
  // flip logic here if your relay module is active-LOW
  digitalWrite(RELAY_PIN, motorStatus ? HIGH : LOW);
  state = motorStatus ? 1 : 0;
  Blynk.virtualWrite(V2, state);
}

void applyLogic() {
  if (autoMode) {
    if (level != 100) {
      motorStatus = false;
      userOffOverride = false;         // leaving full resets override
    } else {
      motorStatus = !userOffOverride;  // full → ON unless user turned OFF
    }
  } else {
    // Manual: motorStatus is controlled only by V2; do nothing here
  }
  driveRelayAndSync();
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

  displayDeviceID(deviceID);
  drawWiFiSignal(getSignalBars());
  drawPattern();
  drawLevelBar(level);
  drawDivider();
  showLevel(level);
  showSNR(snr);
  
  // Initialize Mode indicator based on the push button state
  autoMode = (digitalRead(MODE_BTN_PIN) == LOW);  // If button is pressed, Auto mode, else Manual
  currentMode = autoMode ? 'A' : 'M';  // Set mode as 'A' for Auto or 'M' for Manual
  drawModeIndicator(currentMode);  // Show the initial mode ("A" or "M")

  LoRa.setPins(SS, RST);
  if (!LoRa.begin(433E6)) { Serial.println("LoRa error!"); while (1); }
  LoRa.setSpreadingFactor(8);
  LoRa.setSignalBandwidth(62.5E3);
  LoRa.setCodingRate4(8);
  Serial.println("LoRa Receiver Ready...");

  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);

  pinMode(MODE_BTN_PIN, INPUT_PULLUP);  // button to GND

  applyLogic(); // reflect initial MANUAL OFF to app/relay
}

BLYNK_CONNECTED() {
  Blynk.syncVirtual(V2);                 // get last app switch position
  Blynk.virtualWrite(V0, level);
  Blynk.virtualWrite(V1, snr);
  Blynk.virtualWrite(V2, state);

  // Ensure V3 mode button is synced when connected
  Blynk.virtualWrite(V3, autoMode ? 1 : 0);  // Sync mode button (V3)
}

void loop() {
  Blynk.run();

  // Debounced mode toggle button (GPIO 5)
  int reading = digitalRead(MODE_BTN_PIN);
  if (reading != lastReadingBtn) {
    lastDebounceTime = millis();
  }
  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (reading != lastStableBtn) {
      lastStableBtn = reading;
      if (lastStableBtn == LOW) {             // pressed
        autoMode = !autoMode;
        currentMode = autoMode ? 'A' : 'M';  // Update the mode variable
        Serial.println(autoMode ? "Mode: AUTO" : "Mode: MANUAL");
        if (autoMode) userOffOverride = false; // entering Auto clears override
        applyLogic();                           // re-evaluate with current level
        // Update Blynk V3 to reflect mode change
        Blynk.virtualWrite(V3, autoMode ? 1 : 0);
        drawModeIndicator(currentMode);  // Update mode display on the TFT screen
      }
    }
  }
  lastReadingBtn = reading;

  if (millis() - lastWiFiCheck >= wifiCheckInterval) {
    lastWiFiCheck = millis();
    checkWiFiConnection();
  }
  if (millis() - lastSignalUpdate >= 3000) {
    lastSignalUpdate = millis();
    drawWiFiSignal(getSignalBars());
  }

  parseLoRaData();
}

// ---------- display functions (unchanged) ----------
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

// New function to show "A" or "M" on display
void drawModeIndicator(char currentMode) {
  tft.setFont(&FreeSerifBold12pt7b);
  tft.setTextSize(1);
  
  // Clear the previous mode indicator first (clear a small rectangle where the text is drawn)
  tft.fillRect(65, 18, 30, 20, TFT_NAVY);  // Adjust the rectangle size if needed
  
  // Print the new mode indicator ("A" for Auto and "M" for Manual)
  if (currentMode == 'A') {
    tft.setTextColor(TFT_RED);
    tft.setCursor(70, 35);  // Position for "A"
    tft.print("A");  // Auto Mode
  } else {
    tft.setTextColor(TFT_BLUE);
    tft.setCursor(70, 37);  // Position for "M"
    tft.print("M");  // Manual Mode
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

  // Update UI & telemetry
  level = newLevel;
  count = newCount;
  snr = snrLocal;

  showLevel(level);
  drawLevelBar(level);
  showSNR(snr);
  Blynk.virtualWrite(V0, level);
  Blynk.virtualWrite(V1, snr);

  // Apply current mode rules
  applyLogic();
}

// ---------- Blynk controls ----------
BLYNK_WRITE(V2) {
  int btn = param.asInt();  // 1=ON, 0=OFF

  if (autoMode) {
    if (level == 100) {
      userOffOverride = (btn == 0);  // allow OFF while full
    } else {
      userOffOverride = false;       // not full → always OFF in auto
      motorStatus = false;
    }
  } else {
    // Manual: direct control
    motorStatus = (btn == 1);
    userOffOverride = false;
  }

  applyLogic();
}

// ---------- Blynk mode change ----------
BLYNK_WRITE(V3) {
  autoMode = param.asInt();  // Update mode based on Blynk button (V3)
  Serial.println(autoMode ? "Mode: AUTO" : "Mode: MANUAL");
  
  // Ensure that the push button (GPIO 5) and display are updated
  Blynk.virtualWrite(V3, autoMode ? 1 : 0);  // Update Blynk button (V3)
  currentMode = autoMode ? 'A' : 'M';  // Update mode variable
  drawModeIndicator(currentMode);  // Update mode indicator on the display
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
