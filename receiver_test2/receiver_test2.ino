#include <SPI.h>
#include <LoRa.h>

const int csPin = D8;    // LoRa radio chip select
const int resetPin = D0;  // LoRa radio reset
// const int irqPin = D1;    // change for your board; must be a hardware interrupt pin

void setup() {
  Serial.begin(9600);
  while (!Serial);

  Serial.println("LoRa Receiver");

  // Initialize LoRa
  LoRa.setPins(csPin, resetPin, irqPin);
  if (!LoRa.begin(433E6)) { // or 868E6 for 868 MHz frequency or 915E6 for 915 MHz frequency
    Serial.println("Starting LoRa failed!");
    while (1);
  }
}

void loop() {
  // Try to parse packet
  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    // Received a packet
    Serial.print("Received packet: ");

    // Read packet
    while (LoRa.available()) {
      String received = LoRa.readString();
      Serial.println(received);
    }

    // Print RSSI (Received Signal Strength Indicator)
    // Serial.print(" with RSSI ");
    // Serial.println(LoRa.packetRssi());
  }
}
