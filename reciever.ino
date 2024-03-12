#include <Wire.h>
#include <Arduino.h>

const byte LOAD_MODULE_ADDRESS = 0x40;

int readTwoBytesAsInt() {
  unsigned char msb = Wire.read();  // Read MSB
  unsigned char lsb = Wire.read();   // Read LSB
  
  Serial.println("lsb : " + String(lsb));
  Serial.println("msb : " + String(msb));

  int16_t itermediate_result = (lsb << 8) & msb;
  int result = itermediate_result;
  
  return result;
}

void I2C_receive() {
  while (Wire.available()) {
    Serial.println("accX : " + String(readTwoBytesAsInt()));
  }
}

void setup() {
  Serial.begin(9600);
  Wire.begin();
}

void loop() {
    // Read sensors
    Wire.requestFrom(LOAD_MODULE_ADDRESS, 2);
    I2C_receive();
    delay(500);
}