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
const unsigned long wifiCheckInterval = 10000;  // Check every 10 seconds
unsigned long lastSignalUpdate = 0; // For signal bar refresh

#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <Fonts/FreeSerifBold18pt7b.h>
#include <Fonts/FreeSerifBold12pt7b.h>
#include <Fonts/FreeSerifBold9pt7b.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSans12pt7b.h> 
#include <Fonts/FreeSans18pt7b.h>

// TFT Display Pins
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

// LoRa Pins
#define SS 2
#define RST 25

// LoRa Transmitter and Receiver Address
const byte receiverAddress = 0x15;
const byte senderAddress = 0x16;

String deviceID = "WL1000000000";
int level = 0;
int snr = 0;
int count = 0;
int state = 0;

#define RELAY_PIN 26  // Connect relay to GPIO 26

// Motor switch status flag (V2 control)
bool motorStatus = false;  // Default state is off (LED off)

Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);

// ==================== Function to get bars from RSSI ====================
int getSignalBars() {
  long rssi = WiFi.RSSI(); // Get current RSSI in dBm
  if (rssi >= -50) return 4;       // Excellent
  else if (rssi >= -60) return 3;  // Good
  else if (rssi >= -70) return 2;  // Fair
  else if (rssi >= -80) return 1;  // Weak
  else return 0;                   // Very weak or no signal
}

void setup() {
  Serial.begin(9600);

  // Connect to WiFi and Blynk
  WiFi.begin(ssid, pass);
  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("WiFi Connected. IP Address: ");
  Serial.println(WiFi.localIP());

  SPI.begin();
  tft.initR(INITR_BLACKTAB);
  tft.fillScreen(TFT_NAVY);
  tft.setRotation(3);

  displayDeviceID(deviceID);
  drawWiFiSignal(getSignalBars());  // Initial signal strength
  drawPattern();
  drawLevelBar(level);
  drawDivider();
  showLevel(level);
  showSNR(snr);

  LoRa.setPins(SS, RST);
  if (!LoRa.begin(433E6)) {
    Serial.println("LoRa error!");
    while (1);
  }
  LoRa.setSpreadingFactor(8);
  LoRa.setSignalBandwidth(62.5E3);
  LoRa.setCodingRate4(8);
  Serial.println("LoRa Receiver Ready...");

  pinMode(RELAY_PIN, OUTPUT);  // Set relay pin as output
  digitalWrite(RELAY_PIN, LOW); // Initially turn off the relay (LED off)
}

void loop() {
  Blynk.run();

  // Check WiFi connection periodically
  if (millis() - lastWiFiCheck >= wifiCheckInterval) {
    lastWiFiCheck = millis();
    checkWiFiConnection();
  }

  // Update WiFi signal bars every 3 seconds
  if (millis() - lastSignalUpdate >= 3000) {
    lastSignalUpdate = millis();
    drawWiFiSignal(getSignalBars());
  }

  parseLoRaData();
}

// ==================== Display Functions ====================
void displayDeviceID(String deviceID) {
  tft.fillScreen(TFT_NAVY);
  tft.setFont(); // reset to default small font
  tft.setTextColor(TFT_CYAN);
  tft.setTextSize(1);
  tft.setCursor(3, 4);
  tft.println(deviceID);
}

void drawArc(int x, int y, int r, uint16_t color) {
  tft.setFont(&FreeSerifBold18pt7b); // temporary font setting
  for (int angle = 1; angle <= 90; angle++) {
    float rad = angle * 3.14159 / 180;
    int xPos = x + r * cos(rad);
    int yPos = y - r * sin(rad);
    tft.drawPixel(xPos, yPos, color);
  }
  tft.setFont(); // reset to default small font after drawing arc
}

void drawWiFiSignal(int signalStrength) {
  tft.fillRect(105, 0, 60, 20, TFT_NAVY); // Clear old signal display area
  tft.setFont(); // reset to default font for WIFI text
  tft.setTextColor(TFT_GREEN);
  tft.setTextSize(1);
  tft.setCursor(110, 4);
  tft.println("WIFI");

  int x = 140, y = 12;
  drawArc(x, y, 1, (signalStrength > 0) ? TFT_CYAN : TFT_ASH);
  drawArc(x, y, 4, (signalStrength > 1) ? TFT_CYAN : TFT_ASH);
  drawArc(x, y, 7, (signalStrength > 2) ? TFT_CYAN : TFT_ASH);
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
    if (i < blueSegments) {
      tft.fillRect(x, y, segmentWidth, segmentHeight, TFT_BLUE);
    } else {
      tft.fillRect(x, y, segmentWidth, segmentHeight, TFT_ASH);
    }
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

// ==================== LoRa Parsing ====================
void parseLoRaData() {
  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    byte destAddress = LoRa.read();
    byte senderAddress = LoRa.read();
    if (destAddress == receiverAddress) {
      String data = "";
      while (LoRa.available()) {
        data += (char)LoRa.read();
      }
      int separatorIndex = data.indexOf(',');
      int snr = LoRa.packetSnr() + 18;
      Serial.print("SNR ");
      Serial.println(snr);
      if (separatorIndex != -1) {
        int level = data.substring(0, separatorIndex).toInt();
        int count = data.substring(separatorIndex + 1).toInt();
        Serial.print(" Received Data: Level= ");
        Serial.println(level);
        showLevel(level);
        drawLevelBar(level);
        showSNR(snr);
        Blynk.virtualWrite(V0, level);
        Blynk.virtualWrite(V1, snr);
        Blynk.virtualWrite(V2, state);
        // Call the controlRelay function to manage LED based on water level
        controlRelay(level);
      }
    }
  }
}

// ==================== WiFi Reconnect ====================
void checkWiFiConnection() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected. Trying to reconnect...");
    WiFi.begin(ssid, pass);
    unsigned long startAttemptTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 5000) {
      delay(500);
      Serial.print(".");
    }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nReconnected to WiFi.");
      Serial.print("IP Address: ");
      Serial.println(WiFi.localIP());
      Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);
    } else {
      Serial.println("\nFailed to reconnect to WiFi.");
    }
  }
}

void controlRelay(int waterLevel) {
  // Only activate the relay if the level is 100 AND motorStatus is true (V2 is ON)
  if (waterLevel == 100 && motorStatus) {
    digitalWrite(RELAY_PIN, HIGH);  // Turn on the LED
    Serial.println("LED turned ON");
  } else {
    digitalWrite(RELAY_PIN, LOW);   // Turn off the LED
    Serial.println("LED turned OFF");
  }
}

// ==================== Blynk Button Control ====================
// This function is called when the button in Blynk app is pressed
BLYNK_WRITE(V2) {
  motorStatus = param.asInt();  // Read button state (V2 control)
  
  if (motorStatus == 1) {
    Serial.println("Motor/LED turned ON via Blynk (V2)");
    state = 1; // Set state to 1 (LED on)
  } else {
    Serial.println("Motor/LED turned OFF via Blynk (V2)");
    state = 0; // Set state to 0 (LED off)
  }
  
  // Update the Blynk app with the current motor (LED) state
  Blynk.virtualWrite(V2, state); 
}
