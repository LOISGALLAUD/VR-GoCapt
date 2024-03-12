#include <Arduino.h>
#include <Wire.h>
#include <Arduino_LSM9DS1.h>

//---------------------------------------------------------------------------
/*PROTOTYPES*/

float analogToVoltage(int analogValue);
void floatToBytes(float value, byte* msb, byte* lsb);
void readFlexiForceSensors();
void readIMUData();
void sendDataOverI2C();

//---------------------------------------------------------------------------
/*VARIABLES*/

// I2C Address
const int LOAD_MODULE_ADDRESS = 0x40; // A MODIFIER

// Constants for FlexiForce sensors
const float VCC = 3.3; // Voltage supplied to sensors
const float BITS = 1024.0; // Number of bits in ADC
const int NUM_VALUES = 10; // Number of values to average over
const float SLOPE = 69.9;
const float OFFSET = 529;

float weightMeasurements[NUM_VALUES];
unsigned int ind = 0;

// Calibration factor
const float cf = 1.0;

// Pins for FlexiForce sensors
const int ffs1 = A0;
const int ffs2 = A1;
const int ffs3 = A2;
const int ffs4 = A3;

// Transmitted Bytes of the load
float* loadData[1]; // {avgLoad}

// IMU data
float *accData[3]; // {accX, accY, accZ}
float *gyroData[3]; // {gyroX, gyroY, gyroZ}
float *magData[3]; // {magX, magY, magZ}

// Every sensor data
float **nanoData[4] = {accData, gyroData, magData, loadData};

//---------------------------------------------------------------------------
/*FUNCTIONS*/

// Function to convert analog value to voltage
float analogToVoltage(int analogValue) {
  return (analogValue * VCC) / BITS;
}

void floatToBytes(float value, byte* msb, byte* lsb) {
  uint16_t float_as_int = *(uint16_t*)&value;
  *lsb = float_as_int & 0xFF; // LSB
  *msb = (float_as_int >> 8) & 0xFF; // MSB
}

void readFlexiForceSensors() {
  float voutTotal = 0.0;
  for (int i = 0; i < 4; i++) {
    float vout = analogToVoltage(analogRead(ffs1 + i));
    vout *= cf;
    voutTotal += vout;
  }

  float weightMeasurement = SLOPE * voutTotal - OFFSET; // Linear regression

  weightMeasurements[ind] = weightMeasurement;
  ind = (ind + 1) % NUM_VALUES;

  if (ind == 0) {
    float sum = 0.0;
    for (int i = 0; i < NUM_VALUES; i++) {
      sum += weightMeasurements[i];
    }

    // Mean value
    *loadData[0] = sum / NUM_VALUES;

    // Resetting the array
    memset(weightMeasurements, 0, sizeof(weightMeasurements));
  }
}

void readIMUData() {
  if (IMU.accelerationAvailable()) {
    IMU.readAcceleration(accData[0], accData[1], accData[2]);
    IMU.readGyroscope(gyroData[0], gyroData[1], gyroData[2]);
    IMU.readMagneticField(magData[0], magData[1], magData[2]);
  }
}

void sendDataOverI2C() {
  for (int j = 0, j < 4; j++) {
    for (int i = 0; i < 6; i++) {
      bytes* dataBytes[2];
      floatToBytes(*imuData[j][i], dataBytes[0], dataBytes[1]);
      Wire.write(*dataBytes[0]); // msb
      Wire.write(*dataBytes[1]); // lsb
    }
  }
}

//---------------------------------------------------------------------------
/*INTERRUPTS*/

//---------------------------------------------------------------------------
/*MAIN*/

void setup() {
  // Serial setup
  Serial.begin(9600);

  // Set sensor pins as inputs
  pinMode(ffs1, INPUT);
  pinMode(ffs2, INPUT);
  pinMode(ffs3, INPUT);
  pinMode(ffs4, INPUT);

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
}
