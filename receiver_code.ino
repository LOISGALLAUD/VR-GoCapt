#include <Wire.h>
#include <Arduino.h>

const byte LOAD_MODULE_ADDRESS = 0x40;

void I2C_receive() {
  while (Wire.available()) {
    for (int i = 0; i < 3; i++) {
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
    Wire.requestFrom(LOAD_MODULE_ADDRESS, 20);
    I2C_receive();

    delay(1000);
}