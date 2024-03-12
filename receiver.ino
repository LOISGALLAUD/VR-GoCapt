#include <Wire.h>
#include <Arduino.h>

const byte LOAD_MODULE_ADDRESS = 0x40;

int readTwoBytesAsInt() {
  unsigned char high_byte = Wire.read();  // Read MSB
  unsigned char low_byte = Wire.read();   // Read LSB
  
  int16_t itermediate_result = (low_byte << 8) | high_byte;
  int result = itermediate_result;
  
  return result;
}

void I2C_receive() {
  while (Wire.available()) {
    Serial.println("-------------DEBUT DE SEQUENCE-------------");

    Serial.println("Acceleration: " + String(readTwoBytesAsInt()) + " " + String(readTwoBytesAsInt()) + " " + String(readTwoBytesAsInt()));
    Serial.println("Gyroscope: " + String(readTwoBytesAsInt()) + " " + String(readTwoBytesAsInt()) + " " + String(readTwoBytesAsInt()));
    Serial.println("Magnetic Field: " + String(readTwoBytesAsInt()) + " " + String(readTwoBytesAsInt()) + " " + String(readTwoBytesAsInt()));
    
    Serial.println("-------------FIN DE SEQUENCE-------------");
  }
}

void setup() {
  Serial.begin(9600);
  Wire.begin();
}

void loop() {
    // Read sensors
    Wire.requestFrom(LOAD_MODULE_ADDRESS, 20);
    I2C_receive();
    delay(500);
}