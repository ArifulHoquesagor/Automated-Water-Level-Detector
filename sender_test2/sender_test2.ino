#include <SPI.h>
#include <LoRa.h>

const int csPin = D8;    // LoRa radio chip select
const int resetPin = D0;  // LoRa radio reset
// const int irqPin = D1;    // change for your board; must be a hardware interrupt pin

void setup() {
  Serial.begin(9600);
  while (!Serial);

  Serial.println("LoRa Sender");

  // Initialize LoRa
  LoRa.setPins(csPin, resetPin, irqPin);
  if (!LoRa.begin(433E6)) { // or 868E6 for 868 MHz frequency or 915E6 for 915 MHz frequency
    Serial.println("Starting LoRa failed!");
    while (1);
  }
}

void loop() {
  // Check if data is available in the serial buffer
  if (Serial.available()) {
    String data = Serial.readStringUntil('\n'); // Read string until newline character

    Serial.print("Sending: ");
    Serial.println(data);

    // Send data via LoRa
    LoRa.beginPacket();
    LoRa.print(data);
    LoRa.endPacket();
  }
}
