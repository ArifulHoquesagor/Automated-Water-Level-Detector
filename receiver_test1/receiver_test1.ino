#include <LoRa.h>
#define SS 5
#define RST 14
void setup() {
  Serial.begin(9600);
  while (!Serial);
  Serial.println("Receiver Host");
  LoRa.setPins(SS, RST, DIO0);
  if (!LoRa.begin(433E6)) {
    Serial.println("LoRa Error");
    while (1);
  }
  //Set spreading factor to 12 for maximum range
  LoRa.setSpreadingFactor(8);

  // Set signal bandwidth to 62.5 kHz
  LoRa.setSignalBandwidth(62.5E3);

  // Set coding rate to 4/8 for maximum reliability
  LoRa.setCodingRate4(8);
 }
void loop() {
  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    Serial.print("Receiving Data: ");
    while (LoRa.available()) {
      String data = LoRa.readString();
      Serial.println(data);
      Serial.print(" with RSSI ");
      Serial.println(LoRa.packetRssi());
      Serial.print(" and SNR ");
      Serial.println(LoRa.packetSnr());
    }
  }
}