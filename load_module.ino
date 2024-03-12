#include <Arduino.h>
#include <Wire.h>
#include <Arduino_LSM9DS1.h>

//---------------------------------------------------------------------------
/*PROTOTYPES*/

float analogToVoltage(int analogValue);

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

float mean_weight = 0.0;
float weight_measurements[NUM_VALUES];
unsigned int index = 0;

// Calibration factor
const float cf = 1.0;

// Pins for FlexiForce sensors
const int ffs1 = A0;
const int ffs2 = A1;
const int ffs3 = A2;
const int ffs4 = A3;

// Transmitted Bytes
byte inertalMSB = 0x0A;
byte inertalLSB = 0x0B;
byte loadMSB = 0x0C;
byte loadLSB = 0x0D;

// Acc data
float x, y, z;
int degreesX = 0;
int degreesY = 0;

//---------------------------------------------------------------------------
/*FUNCTIONS*/

// Function to convert analog value to voltage
float analogToVoltage(int analogValue) {
  return (analogValue * VCC) / BITS;
}

void I2C_send(void) {
  Wire.write(inertalMSB);
  Wire.write(inertalLSB);
  Wire.write(loadMSB);
  Wire.write(loadLSB);
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
  Wire.onRequest(I2C_send);

  // IMU setup
  if (!IMU.begin()) {
    Serial.println("Failed to initialize IMU!");
    while (1);
  }
  Serial.print("Accelerometer sample rate = ");
  Serial.print(IMU.accelerationSampleRate());
  Serial.println("Hz");
}

void loop() {
    // float vout_total = 0.0;
    // for (int i = 0; i < 4; i++) {
    //     float vout = analogToVoltage(analogRead(ffs1 + i));
    //     vout *= cf;
    //     vout_total += vout;
    // }

    // float weight_measurement = SLOPE * vout_total - OFFSET; // Linear regression

    // weight_measurements[index] = weight_measurement;
    // index = (index + 1) % NUM_VALUES;

    // if (index == 0) {
    //     float sum = 0.0;
    //     for (int i = 0; i < NUM_VALUES; i++) {
    //         sum += weight_measurements[i];
    //     }
        
    //     // Mean value
    //     mean_weight = sum / NUM_VALUES;

    //     // Resetting the array
    //     memset(weight_measurements, 0, sizeof(weight_measurements));
    // }

    if (IMU.accelerationAvailable()) {
      IMU.readAcceleration(x, y, z);

    }

    if (x > 0.1) {
      x = 100 * x;
      degreesX = map(x, 0, 97, 0, 90);
      Serial.print("Tilting up ");
      Serial.print(degreesX);
      Serial.println("  degrees");
    }
    if (x < -0.1) {
      x = 100 * x;
      degreesX = map(x, 0, -100, 0, 90);
      Serial.print("Tilting down ");
      Serial.print(degreesX);
      Serial.println("  degrees");
    }
    if (y > 0.1) {
      y = 100 * y;
      degreesY = map(y, 0, 97, 0, 90);
      Serial.print("Tilting left ");
      Serial.print(degreesY);
      Serial.println("  degrees");
    }
    if (y < -0.1) {
      y = 100 * y;
      degreesY = map(y, 0, -100, 0, 90);
      Serial.print("Tilting right ");
      Serial.print(degreesY);
      Serial.println("  degrees");
    }
    delay(1000);
}
