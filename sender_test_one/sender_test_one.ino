#include <LoRa.h>
#define SS D8
#define RST D0
String data = "A quick brown fox";
void setup()
{
  Serial.begin(9600);
  while (!Serial);
  Serial.println("Sender Host");
  LoRa.setPins(SS, RST, DIO0);
  if (!LoRa.begin(433E6)) {
    Serial.println("LoRa Error");
    delay(100);
    while (1);
  }
  LoRa.setSpreadingFactor(8);

  // Set signal bandwidth to 62.5 kHz
  LoRa.setSignalBandwidth(62.5E3);

  // Set coding rate to 4/8 for maximum reliability
  LoRa.setCodingRate4(8);

  // Set transmit power to maximum
  LoRa.setTxPower(20, PA_OUTPUT_PA_BOOST_PIN);
}
void loop()
{
  Serial.print("Sending Data: ");
  Serial.println(data);
  LoRa.beginPacket();
  LoRa.print(data);
  LoRa.endPacket();
  delay(1000);
}