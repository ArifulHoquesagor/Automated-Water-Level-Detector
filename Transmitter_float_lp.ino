#include <LoRa.h>
#include <ESP8266WiFi.h>
#include <FastLED.h>
#include <user_interface.h> // For light sleep

#define NUM_LEDS 1
CRGB leds[NUM_LEDS];
#define LED_PIN 2  // GPIO2

#define SS 15  // LoRa SS
#define RST 10 // LoRa reset

#define LORA_FREQUENCY 433E6 // 433 MHz

// Sensor pins
#define SENSOR_BLUE 5
#define SENSOR_GREEN 4
#define SENSOR_YELLOW 16
#define SENSOR_BLACK -1 // ADC probe on A0

int count = 0;
int level = 0;

// LoRa Addresses
const byte destinationAddress = 0x15;
const byte senderAddress = 0x16;

// Timeout settings
unsigned long lastTransmitTime = 0;
const unsigned long timeoutPeriod = 60000; // 1 min
bool failed_to_send = false;

// Sleep time between sends (ms)
const uint32_t SLEEP_INTERVAL_MS = 10000; // 10 seconds

// Light Sleep Helper
void enterLightSleep(uint32_t sleepMs) {
  wifi_station_disconnect();
  wifi_set_opmode_current(NULL_MODE);
  wifi_fpm_set_sleep_type(LIGHT_SLEEP_T);
  wifi_fpm_open();
  wifi_fpm_do_sleep(sleepMs * 1000); // microseconds
  delay(sleepMs + 1); // wake after sleep
}

void setup() {
  delay(1000);
  Serial.begin(9600);
  delay(1000);

  // Disable WiFi
  WiFi.mode(WIFI_OFF);
  WiFi.forceSleepBegin();
  delay(10);

  // LED setup
  FastLED.addLeds<WS2812, LED_PIN, GRB>(leds, NUM_LEDS);
  leds[0] = CRGB::Black;
  FastLED.show();
  delay(100);

  // Sensor setup
  pinMode(SENSOR_BLUE, INPUT_PULLUP);
  pinMode(SENSOR_GREEN, INPUT_PULLUP);
  pinMode(SENSOR_YELLOW, INPUT);

  // LoRa reset pin
  pinMode(RST, OUTPUT);
  digitalWrite(RST, HIGH);

  Serial.println("Sender Host");

  // LoRa init
  LoRa.setPins(SS, RST);
  if (!LoRa.begin(LORA_FREQUENCY)) {
    Serial.println("LoRa Error");
    while (1);
  }
  LoRa.setSpreadingFactor(8);
  LoRa.setSignalBandwidth(62.5E3);
  LoRa.setCodingRate4(8);
  LoRa.setTxPower(20, PA_OUTPUT_PA_BOOST_PIN);

  lastTransmitTime = millis();
}

void loop() {
  leds[0] = CRGB(255, 0, 0); // Red while active
  FastLED.show();

  LoRa.idle(); // Wake LoRa
  delay(1000);

  Serial.println("LoRa wake up from sleep");

  // -------- Level Detection --------
  bool level_one = digitalRead(SENSOR_BLUE);
  bool level_two = digitalRead(SENSOR_GREEN);
  bool level_three = digitalRead(SENSOR_YELLOW);

  // ---- Your ADC code block (unchanged) ----
  //  int level_four = digitalRead(SENSOR_BLACK);
  int adcValue = analogRead(A0);              // Read the ADC value from A0 pin
  float voltage = (adcValue / 1023.0) * 1.0;  // Convert ADC value to voltage (assuming 0–1V)
  bool level_four;
  if (voltage > 0.3)
    level_four = 1;
  else
    level_four = 0;
  // -----------------------------------------

  Serial.println(level_one);
  Serial.println(level_two);
  Serial.println(level_three);
  Serial.println(level_four);

  // Water level logic
  if (level_one == LOW && level_two == LOW && level_three == HIGH && level_four == HIGH) {
    level = 100;
  } else if (level_one == LOW && level_two == LOW && level_three == HIGH && level_four == LOW) {
    level = 75;
  } else if (level_one == LOW && level_two == LOW && level_three == LOW && level_four == LOW) {
    level = 50;
  } else if (level_one == LOW && level_two == HIGH && level_three == LOW && level_four == LOW) {
    level = 25;
  } else if (level_one == HIGH && level_two == HIGH && level_three == LOW && level_four == LOW) {
    level = 0;
  }

  count++;

  bool success = sendPacket(level, count);
  if (success) {
    lastTransmitTime = millis();
    failed_to_send = false;
  } else {
    failed_to_send = true;
  }

  // Restart LoRa if no send for 1 min
  if (millis() - lastTransmitTime > timeoutPeriod) {
    Serial.println("No data transmitted for 1 minute. Restarting LoRa module...");
    restartLoRaModule();
    lastTransmitTime = millis();
  }

  // Sleep only if send succeeded
  if (!failed_to_send) {
    LoRa.sleep(); // Put LoRa into sleep mode
    leds[0] = CRGB(0, 0, 255); // Blue while sleeping
    FastLED.show();
    Serial.println("Entering light sleep...");
    enterLightSleep(SLEEP_INTERVAL_MS); // ESP8266 light sleep
  }
}

bool sendPacket(int level, int count) {
  LoRa.beginPacket();
  LoRa.write(destinationAddress);
  LoRa.write(senderAddress);
  LoRa.print(level);
  LoRa.print(",");
  LoRa.print(count);
  bool sent = (LoRa.endPacket() == 1);
  if (sent) {
    Serial.print("Sending Data: Level=");
    Serial.print(level);
    Serial.print(", Count=");
    Serial.println(count);
  } else {
    Serial.println("No Data Sending");
  }
  return sent;
}

void restartLoRaModule() {
  Serial.println("Restarting LoRa module...");
  digitalWrite(RST, LOW);
  delay(100);
  digitalWrite(RST, HIGH);
  delay(1000);

  LoRa.setPins(SS, RST);
  if (!LoRa.begin(LORA_FREQUENCY)) {
    Serial.println("LoRa Error");
    while (1);
  }
  LoRa.setSpreadingFactor(8);
  LoRa.setSignalBandwidth(62.5E3);
  LoRa.setCodingRate4(8);
  LoRa.setTxPower(20, PA_OUTPUT_PA_BOOST_PIN);
}
