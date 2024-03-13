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

float weightMeasurements[NUM_VALUES];
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
  for (int i = 0; i < 4; i++) {
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
    // loadData = round(sum*100 / NUM_VALUES);
    loadData = 69;

    // Resetting the array
    memset(weightMeasurements, 0, sizeof(weightMeasurements));
  }
}

void readIMUData() {
  if (IMU.accelerationAvailable()) {
    float accX, accY, accZ;
    float gyroX, gyroY, gyroZ;
    float magX, magY, magZ;

    IMU.readAcceleration(accX, accY, accZ);
    IMU.readGyroscope(gyroX, gyroY, gyroZ);
    IMU.readMagneticField(magX, magY, magZ);

    // Mapping to 180° field
    accData[0] = map(accX*100, -97, 100, 0, 180);
    accData[1] = map(accY*100, -97, 100, 0, 180);
    accData[2] = map(accZ*100, -97, 100, 0, 180);

    gyroData[0] = map(gyroX*100, -97, 100, 0, 180);
    gyroData[1] = map(gyroY*100, -97, 100, 0, 180);
    gyroData[2] = map(gyroZ*100, -97, 100, 0, 180);

    magData[0] = map(magX*100, -97, 100, 0, 180);
    magData[1] = map(magY*100, -97, 100, 0, 180);
    magData[2] = map(magZ*100, -97, 100, 0, 180);
  }
}

void sendDataOverI2C() {
  writeTwoBytes(accData[0]); // accX
  writeTwoBytes(accData[1]); // accY
  writeTwoBytes(accData[2]); // accZ

  writeTwoBytes(gyroData[0]); // gyroX
  writeTwoBytes(gyroData[1]); // gyroY
  writeTwoBytes(gyroData[2]); // gyroZ

  writeTwoBytes(magData[0]); // magX
  writeTwoBytes(magData[1]); // magY
  writeTwoBytes(magData[2]); // magZ

  // Optimisation : envoi de 3 valeurs en même temps
  // for (int j = 0; j < 3; j++) {
  //   for (int i = 0; i < 3; i++) {
  //     writeTwoBytes(nanoData[j][i]);
  //   }
  // }
  // writeTwoBytes(loadData);
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
