#include <Wire.h>
#include <Arduino.h>


byte inertalMSB;
byte inertalLSB;
byte loadMSB;
byte loadLSB;

void I2C_receive() {
  while (Wire.available()) {
    inertalMSB = Wire.read();
    inertalLSB = Wire.read();
    loadMSB = Wire.read();
    loadLSB = Wire.read();
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

    Serial.print("Inertial: ");
    Serial.print(inertalMSB);
    Serial.print(inertalLSB);
    Serial.print(" Load: ");
    Serial.print(loadMSB);
    Serial.println(loadLSB);
    delay(1000);
}