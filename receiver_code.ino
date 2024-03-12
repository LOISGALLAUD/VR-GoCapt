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
    for (int i = 0; i < 3; i++) {
        Serial.println("-------------DEBUT DE SEQUENCE-------------");
        Serial.println("Accel X: " + String(readTwoBytesAsInt()));
        Serial.println("Accel Y: " + String(readTwoBytesAsInt()));
        Serial.println("Accel Z: " + String(readTwoBytesAsInt()));
        Serial.println("Gyro X: " + String(readTwoBytesAsInt()));
        Serial.println("Gyro Y: " + String(readTwoBytesAsInt()));
        Serial.println("Gyro Z: " + String(readTwoBytesAsInt()));
        Serial.println("Mag X: " + String(readTwoBytesAsInt()));
        Serial.println("Mag Y: " + String(readTwoBytesAsInt()));
        Serial.println("Mag Z: " + String(readTwoBytesAsInt()));
        Serial.println("Load: " + String(readTwoBytesAsInt()));
        }
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

    delay(1000);
}