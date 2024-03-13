/*
Code to implement inside central module to receive data from the load module
located inside the shoe. The data is transmitted through I2C protocol.
*/

#include <Wire.h>
#include <Arduino.h>

//---------------------------------------------------------------------------
/*PROTOTYPES*/

int readTwoBytesAsInt();
void I2C_receive();

//---------------------------------------------------------------------------
/*VARIABLES*/

const byte LOAD_MODULE_ADDRESS = 0x40;
const byte BYTES_TO_READ = 18;

//---------------------------------------------------------------------------
/*FUNCTIONS*/

int readTwoBytesAsInt() {
  unsigned char msb = Wire.read();
  unsigned char lsb = Wire.read();
  int16_t itermediate_result = (msb << 8) | lsb;
  int result = itermediate_result;
  return result;
}

void I2C_receive() {
  while (Wire.available()) {
    Serial.println("----------------------------------");
    Serial.println("accX : " + String(readTwoBytesAsInt()));
    Serial.println("accY : " + String(readTwoBytesAsInt()));
    Serial.println("accZ : " + String(readTwoBytesAsInt()));
    Serial.println("gyroX : " + String(readTwoBytesAsInt()));
    Serial.println("gyroY : " + String(readTwoBytesAsInt()));
    Serial.println("gyroZ : " + String(readTwoBytesAsInt()));
    Serial.println("magX : " + String(readTwoBytesAsInt()));
    Serial.println("magY : " + String(readTwoBytesAsInt()));
    Serial.println("magZ : " + String(readTwoBytesAsInt()));
  }
}

void setup() {
  Serial.begin(9600);
  Wire.begin();
}

void loop() {
    // Read sensors
    Wire.requestFrom(LOAD_MODULE_ADDRESS, BYTES_TO_READ);
    I2C_receive();
    delay(500);
}