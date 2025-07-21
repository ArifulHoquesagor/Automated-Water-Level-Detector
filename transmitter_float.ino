#include <LoRa.h>
#include <ESP8266WiFi.h>
#include <FastLED.h>
#define NUM_LEDS 1
CRGB leds[NUM_LEDS];
#define LED_PIN 02//02    //GPIO4

#define SS 15
#define RST 10  // GPIO pin for LoRa reset

// LoRa frequency
#define LORA_FREQUENCY 433E6  // 433 MHz

// Sensor pins
#define SENSOR_BLUE 5
#define SENSOR_GREEN 4
#define SENSOR_YELLOW 16//0
#define SENSOR_BLACK -1//A0//2

int count = 0;
int level = 0;

//LoRa Addresses
const byte destinationAddress = 0x15;  //7 Address of the receiver
const byte senderAddress = 0x16;       //8 Address of the sender

// Timeout settings
unsigned long lastTransmitTime = 0;         // Time of last successful transmission
const unsigned long timeoutPeriod = 60000;  // Timeout period (1 min)
unsigned lora_sleep_time = 0;
bool failed_to_send = false;
void setup() {
   delay(1000);
  Serial.begin(9600);
  delay(1000);
  WiFi.mode(WIFI_OFF);  // Disable Wi-Fi completely
  WiFi.forceSleepBegin();  // Puts Wi-Fi into deep sleep mode
  delay(10);  // Short delay to ensure it has time to enter sleep
    FastLED.addLeds<WS2812, 02, GRB>(leds, NUM_LEDS);
  leds[0] = CRGB::Black;
  FastLED.show();
  delay(100);
  // Sensor Setup
  pinMode(SENSOR_BLUE, INPUT_PULLUP);
  pinMode(SENSOR_GREEN, INPUT_PULLUP);
  pinMode(SENSOR_YELLOW, INPUT);
  //  pinMode(SENSOR_BLACK, INPUT_PULLUP);

  pinMode(RST, OUTPUT);     // Set RST pin as output
  digitalWrite(RST, HIGH);  // Ensure LoRa is not in reset state

  //  while (!Serial)
  //    ;
  Serial.println("Sender Host");

  LoRa.setPins(SS, RST);

  if (!LoRa.begin(LORA_FREQUENCY)) {
    Serial.println("LoRa Error");
    while (1);  // Halt the program if LoRa initialization fails
  }

  LoRa.setSpreadingFactor(8);
  LoRa.setSignalBandwidth(62.5E3);
  LoRa.setCodingRate4(8);
  LoRa.setTxPower(20, PA_OUTPUT_PA_BOOST_PIN);

  // Initialize lastTransmitTime
  lastTransmitTime = millis();
}

void loop() {
leds[0] = CRGB(255, 0, 0);
      FastLED.show();
  LoRa.idle();           // Wake it up
  delay(1000);
 

  Serial.println("lora wake up from sleep");

  // Level Detection
  bool level_one = digitalRead(SENSOR_BLUE);
  bool level_two = digitalRead(SENSOR_GREEN);
  bool level_three = digitalRead(SENSOR_YELLOW);

  //  int level_four = digitalRead(SENSOR_BLACK);
  int adcValue = analogRead(A0);  // Read the ADC value from A0 pin
  float voltage = (adcValue / 1023.0) * 1.0;  // Convert ADC value to voltage (assuming 0–1V)
  bool level_four;
  if (voltage > 0.3)
    level_four = 1;
  else
    level_four = 0;
    
     Serial.println(level_one);
     Serial.println(level_two);
     Serial.println(level_three);
     Serial.println(level_four);
     
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
//  level = random(9);
  bool success = sendPacket(level, count);
  if (success) {
    lastTransmitTime = millis();  // Update last transmission time
    failed_to_send = false;
  }
  else
    failed_to_send = true;

  // Check if it's time to restart
  if (millis() - lastTransmitTime > timeoutPeriod) {
    Serial.println("No data transmitted for 1 minute. Restarting LoRa module...");
    restartLoRaModule();          // Restart only the LoRa module
    lastTransmitTime = millis();  // Reset lastTransmitTime to avoid continuous resets
  }
  if (failed_to_send == false) {
    LoRa.sleep();
     leds[0] = CRGB(0, 0, 255);
      FastLED.show();
    delay(10000);  // Delay before the next transmission
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
  digitalWrite(RST, LOW);   // Trigger reset
  delay(100);               // Wait for a short period
  digitalWrite(RST, HIGH);  // Release reset
  delay(1000);              // Wait for LoRa module to initialize
  //  LoRa.begin(LORA_FREQUENCY);

  LoRa.setPins(SS, RST);

  if (!LoRa.begin(LORA_FREQUENCY)) {
    Serial.println("LoRa Error");
    while (1);  // Halt the program if LoRa initialization fails
  }

  LoRa.setSpreadingFactor(8);
  LoRa.setSignalBandwidth(62.5E3);
  LoRa.setCodingRate4(8);
  LoRa.setTxPower(20, PA_OUTPUT_PA_BOOST_PIN);
}
