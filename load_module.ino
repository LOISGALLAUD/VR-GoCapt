/*
Code to implement inside Arduino Nano located inside the shoe.
Data is collected from the FlexiForce sensors and the built-in IMU
and transmitted to the central module through I2C protocol.
*/

#include <Arduino.h>
#include <Wire.h>
#include <Arduino_LSM9DS1.h>
#include <Math.h>

#define FFS1 A0
#define FFS2 A1
#define FFS3 A2
#define FFS4 A3
#define VCC 3.3 // Voltage supplied to sensors
#define BITS 1024.0 // Number of bits in ADC
#define NUMBER_OF_VALUES_TO_AVERAGE 10 // Number of values to average over
#define SLOPE 69.9 // Slope of the linear regression
#define OFFSET 529 // Offset of the linear regression
#define LOAD_MODULE_ADDRESS 0x40 // I2C address of the load module 
#define CALIBRATION_FACTOR 1.0 // Calibration factor

#define ANALOG_TO_VOLTAGE(analogValue) (analogValue * VCC) / BITS

//---------------------------------------------------------------------------
/*PROTOTYPES*/

float analogToVoltage(int analogValue);
void floatToBytes(float value, byte* msb, byte* lsb);
void readFlexiForceSensors();
void readIMUData();
void writeTwoBytes(int value);
void sendDataOverI2C();

//---------------------------------------------------------------------------
/*VARIABLES*/

float weightMeasurements[NUMBER_OF_VALUES_TO_AVERAGE];
unsigned int ind = 0;

// Transmitted Bytes of the load
float loadData; // avgLoad

// IMU data
int accData[3]; // {accX, accY, accZ}
int gyroData[3]; // {gyroX, gyroY, gyroZ}
int magData[3]; // {magX, magY, magZ}
int *nanoData[4] = {accData, gyroData, magData};

//---------------------------------------------------------------------------
/*FUNCTIONS*/

void floatToBytes(int value, byte* msb, byte* lsb) {
  *lsb = value & 0xFF; // LSB
  *msb = (value >> 8) & 0xFF; // MSB
}

void writeTwoBytes(int value) {
  byte dataBytes[2];
  floatToBytes(value, &dataBytes[0], &dataBytes[1]);
  Wire.write(dataBytes[0]); // msb
  Wire.write(dataBytes[1]); // lsb
}

void readFlexiForceSensors() {
  float voutTotal = 0.0;
  for (int i = 0; i < 1; i++) {
    float vout = ANALOG_TO_VOLTAGE(analogRead(FFS1 + i));
    vout *= CALIBRATION_FACTOR;
    voutTotal += vout;
  }
  float weightMeasurement = SLOPE * voutTotal - OFFSET; // Linear regression
  weightMeasurements[ind] = weightMeasurement;
  ind = ++ind % NUMBER_OF_VALUES_TO_AVERAGE;

  if (ind == 0) {
    float sum = 0.0;
    for (int i = 0; i < NUMBER_OF_VALUES_TO_AVERAGE; i++) {
      sum += weightMeasurements[i];
    }

    // Mean value (conserving 2 decimal places)
    loadData = sum / NUMBER_OF_VALUES_TO_AVERAGE;
    Serial.println("loadData : " + String(loadData));

    // Resetting the array
    memset(weightMeasurements, 0, sizeof(weightMeasurements));
  }
}

void readIMUData() {
  if (IMU.accelerationAvailable()) {
    float acc[3] = {accX, accY, accZ};
    float gyro[3] = {gyroX, gyroY, gyroZ};
    float mag[3] = {magX, magY, magZ};

    IMU.readAcceleration(acc[0], acc[1], acc[2]);
    IMU.readGyroscope(gyro[0], gyro[1], gyro[2]);
    IMU.readMagneticField(mag[0], mag[1], mag[2]);

    // Linear Acceleration
    for (int i = 0; i < 3; i++) {
      accData[i] = map(acc[i]*100, -400, 400, -100, 100);
    }

    // Angular Acceleration
    for (int i = 0; i < 3; i++) {
      gyroData[i] = map(gyro[i], -1000, 1000, -100, 100);
    }

    // Magnetic Field
    for (int i = 0; i < 3; i++) {
      magData[i] = map(mag[i], -400, 400, -100, 100);
    }

    Serial.println("----------------------------------");
    Serial.println("accX : " + String(accData[0]));
    Serial.println("accY : " + String(accData[1]));
    Serial.println("accZ : " + String(accData[2]));
    Serial.println("gyroX : " + String(gyroData[0]));
    Serial.println("gyroY : " + String(gyroData[1]));
    Serial.println("gyroZ : " + String(gyroData[2]));
    Serial.println("magX : " + String(magData[0]));
    Serial.println("magY : " + String(magData[1]));
    Serial.println("magZ : " + String(magData[2]));
  }
}

void sendDataOverI2C() {
  // Optimisation : envoi de 3 valeurs en même temps
  for (int j = 0; j < 3; j++) {
    for (int i = 0; i < 3; i++) {
      writeTwoBytes(nanoData[j][i]);
    }
  }
  writeTwoBytes(loadData);
}

//---------------------------------------------------------------------------
/*MAIN*/

void setup() {
  // Serial setup
  Serial.begin(9600);

  // Set sensor pins as inputs
  pinMode(FFS1, INPUT);
  pinMode(FFS2, INPUT);
  pinMode(FFS3, INPUT);
  pinMode(FFS4, INPUT);

  // I2C setup
  Wire.begin(LOAD_MODULE_ADDRESS);
  Wire.onRequest(sendDataOverI2C);

  // IMU setup
  if (!IMU.begin()) {
    Serial.println("Failed to initialize IMU!");
    while (1);
  }
}

void loop() {
  readFlexiForceSensors();
  readIMUData();
  delay(100);
}
