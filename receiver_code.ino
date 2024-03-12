#include <Wire.h>
#include <Arduino.h>

const byte LOAD_MODULE_ADDRESS = 0x40;

// Transmitted Bytes of the load
float* loadData[1]; // {avgLoad}

// IMU data
float *accData[3]; // {accX, accY, accZ}
float *gyroData[3]; // {gyroX, gyroY, gyroZ}
float *magData[3]; // {magX, magY, magZ}

void I2C_receive() {
  while (Wire.available()) {
    for (int i = 0; i < 4; i++) {
        byte msb = Wire.read();
        byte lsb = Wire.read();
        uint16_t float_as_int = (msb << 8) | lsb;
        float value = *(float*)&float_as_int;
        Serial.println(value);
        }
    Serial.println("-------------FIN DE SEQUENCE-------------")
  }
}

void setup() {
  Serial.begin(9600);
  Wire.begin();
}

void loop() {
    // Read sensors
    Wire.requestFrom(LOAD_MODULE_ADDRESS, 4);
    I2C_receive();

    delay(1000);
}