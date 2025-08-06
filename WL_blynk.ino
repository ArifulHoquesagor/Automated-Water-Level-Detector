#define BLYNK_TEMPLATE_ID "TMPL6wMpQ-cJg"
#define BLYNK_TEMPLATE_NAME "Water Level Device"
#define BLYNK_AUTH_TOKEN "-1ew1nIGHZDimZpv95E3nOCy9TaQgAuu"
#include <SPI.h>
#include <LoRa.h>
#include <WiFi.h>
#include <WiFi.h>
#include <BlynkSimpleEsp32.h>

char ssid[] = "STELLAR";
char pass[] = "stellarBD";

unsigned long lastWiFiCheck = 0;
const unsigned long wifiCheckInterval = 10000;  // Check every 10 seconds

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
#define TFT_BLUE 0x001F
#define TFT_RED 0xF800
#define TFT_GREEN 0x07E0
#define TFT_WHITE 0xFFFF
#define TFT_CYAN 0x07FF
#define TFT_NAVY 0x18C6
#define TFT_ASH 0xC618

// Tank bar properties
//#define TANK1_X 5
//#define TANK2_X 130
//#define TANK_Y 50
#define BAR_WIDTH 20
#define BAR_HEIGHT 10
#define SEGMENTS 10

// LoRa Pins
#define SS 2
#define RST 25

// LoRa Transmiter and Receiver Address
const byte receiverAddress = 0x15;
const byte senderAddress = 0x16;

String deviceID = "WL1000000000";
int level = 0;
int snr = 0;
int count = 0;
int state = 0;
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);

void setup() {
    Serial.begin(9600);
  // put your setup code here, to run once:

  // Connect to WiFi and Blynk
  
  WiFi.begin(ssid, pass);
  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);

  // Wait for WiFi connection
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
  drawWiFiSignal(4);  // Adjust signal strength (0 to 4) to test
  drawPattern();
  drawLevelBar(level);
  drawDivider();
  showLevel(level);
  showSNR(snr);

  LoRa.setPins(SS, RST);
  if (!LoRa.begin(433E6)) {
    Serial.println("LoRa error!");
    while (1)
      ;
  }
  LoRa.setSpreadingFactor(8);
  LoRa.setSignalBandwidth(62.5E3);
  LoRa.setCodingRate4(8);
  Serial.println("LoRa Receiver Ready...");
}

void loop() {
  // put your main code here, to run repeatedly:
  Blynk.run();
    // Check WiFi connection periodically
  if (millis() - lastWiFiCheck >= wifiCheckInterval) {
    lastWiFiCheck = millis();
    checkWiFiConnection();
  }
  parseLoRaData();
}
void displayDeviceID(String deviceID) {
  tft.fillScreen(TFT_NAVY);    // Clear screen
  tft.setTextColor(TFT_CYAN);  // Set text color to Cyan
  tft.setTextSize(1);          // Text size
  tft.setCursor(3, 4);         // Move cursor for ID
  tft.println(deviceID);       // Print Device ID
}

// Function to draw a semi-circle (arc) using pixels
void drawArc(int x, int y, int r, uint16_t color) {
  for (int angle = 1; angle <= 90; angle++) {  // Draw arc from 0° to 90°
    float rad = angle * 3.14159 / 180;         // Convert angle to radians
    int xPos = x + r * cos(rad);               // Calculate x position
    int yPos = y - r * sin(rad);               // Calculate y position (invert y-axis)

    //        tft.setTextSize(2);
    tft.setFont(&FreeSerifBold18pt7b);
    tft.drawPixel(xPos, yPos, color);  // Draw the pixel
  }
}


// Function to draw WiFi signal arcs
void drawWiFiSignal(int signalStrength) {
  tft.setTextColor(TFT_GREEN);  // Set text color to Cyan
  tft.setTextSize(1);           // Text size
  tft.setCursor(110, 4);        // Move cursor for ID
  tft.println("WIFI");

  // WiFi signal arcs
  int x = 140, y = 12;  // Base position
  drawArc(x, y, 1, (signalStrength > 0) ? TFT_CYAN : TFT_ASH);
  drawArc(x, y, 4, (signalStrength > 1) ? TFT_CYAN : TFT_ASH);
  drawArc(x, y, 7, (signalStrength > 2) ? TFT_CYAN : TFT_ASH);
  drawArc(x, y, 10, (signalStrength > 3) ? TFT_CYAN : TFT_ASH);
}

void drawPattern() {
  // First part of the pattern: -----\_____/-----
  tft.drawLine(0, 30, 35, 30, TFT_RED);   // Line 1: -----
  tft.drawLine(35, 30, 40, 40, TFT_RED);  // Line 2: \

  tft.drawLine(40, 40, 125, 40, TFT_RED);
  tft.drawLine(125, 40, 130, 30, TFT_RED);
  tft.drawLine(130, 30, 160, 30, TFT_RED);

  //  // Second part of the pattern: ______/-----\_____
  tft.drawLine(0, 110, 35, 110, TFT_BLUE);     // Line 6: ______
  tft.drawLine(35, 110, 40, 100, TFT_BLUE);    // Line 7: /
  tft.drawLine(40, 100, 125, 100, TFT_BLUE);   // Line 8: -----
  tft.drawLine(125, 100, 130, 110, TFT_BLUE);  // Line 9: \

  tft.drawLine(130, 110, 160, 110, TFT_BLUE);  // Line 10: ______
}
void drawLevelBar(int level) {
  int segments = 10;      // Number of segments
  int segmentWidth = 20;  // Width of each segment
  int segmentHeight = 5;  // Height of each segment

  int blueSegments = map(level, 0, 100, 0, segments);  // Calculate how many segments should be blue

  // Draw each segment from bottom to top
  for (int i = 0; i < segments; i++) {
    int x = 5;                                              // X position of the segment
    int y = 40 + (segments - 1 - i) * (segmentHeight + 2);  // Reverse order for bottom-to-top filling

    // If the segment index is less than the number of blue segments, fill it with blue
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
  tft.setTextColor(TFT_WHITE);
  tft.setFont(&FreeSans12pt7b);
  tft.setCursor(31, 72);  // Set position (X, Y)
  tft.setTextSize(1);
  tft.print(level);  // Print the integer
  tft.setFont(&FreeSans9pt7b);
  tft.setCursor(31, 90);  // Set position (X, Y)
  tft.print("Level");     // Print the integer
}

void showSNR(int snr) {
  tft.fillRect(95, 55, 45, 40, TFT_NAVY);
  tft.setTextColor(TFT_WHITE);
  tft.setFont(&FreeSans12pt7b);
  tft.setCursor(98, 72);  // Set position (X, Y)
  tft.setTextSize(1);
  tft.print(snr);  // Print the integer
  tft.setFont(&FreeSans9pt7b);
  tft.setCursor(95, 90);  // Set position (X, Y)
  tft.print("SNR");       // Print the integer
}
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
      int snr = LoRa.packetSnr() + 18;  // Get RSSI value
      Serial.print("SNR ");
      Serial.println(snr);
      if (separatorIndex != -1) {
        int level = data.substring(0, separatorIndex).toInt();
        int count = data.substring(separatorIndex + 1).toInt();
        Serial.print(" Received Data: Level= ");
        Serial.print(level);
        showLevel(level);
        drawLevelBar(level);
        showSNR(snr);
        Blynk.virtualWrite(V0, level);        // Gauge
        Blynk.virtualWrite(V1, snr);  // Label
        Blynk.virtualWrite(V2, state);        // Label or Switch
      }
    }
  }
}
void checkWiFiConnection() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected. Trying to reconnect...");
    WiFi.begin(ssid, pass);
    unsigned long startAttemptTime = millis();

    // Wait up to 5 seconds
    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 5000) {
      delay(500);
      Serial.print(".");
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nReconnected to WiFi.");
      Serial.print("IP Address: ");
      Serial.println(WiFi.localIP());

      Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass); // Re-init Blynk
    } else {
      Serial.println("\nFailed to reconnect to WiFi.");
    }
  }
}
