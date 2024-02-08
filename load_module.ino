#include <Arduino.h>

//---------------------------------------------------------------------------
/*PROTOTYPES*/

float analogToVoltage(int analogValue);

//---------------------------------------------------------------------------
/*VARIABLES*/

// Constants for FlexiForce sensors
const float VCC = 3.3; // Voltage supplied to sensors
const float BITS = 1024.0; // Number of bits in ADC
const int NUM_VALUES = 10; // Number of values to average over
const float SLOPE = 69.9;
const float OFFSET = 529;

float mean_weight = 0.0;
float poids_mesures[NUM_VALUES];
unsigned int index = 0;

// Calibration factor
const float cf = 1.0;

// Pins for FlexiForce sensors
const int ffs1 = A0;
const int ffs2 = A1;
const int ffs3 = A2;
const int ffs4 = A3;

// Variables for weight measurement
unsigned long Time; // Variable to store the time

//---------------------------------------------------------------------------
/*FUNCTIONS*/

// Function to convert analog value to voltage
float analogToVoltage(int analogValue) {
  return (analogValue * VCC) / BITS;
}

//---------------------------------------------------------------------------
/*INTERRUPTS*/

//---------------------------------------------------------------------------
/*MAIN*/

void setup() {
  Serial.begin(9600);
  // Set sensor pins as inputs
  pinMode(ffs1, INPUT);
  pinMode(ffs2, INPUT);
  pinMode(ffs3, INPUT);
  pinMode(ffs4, INPUT);
  Time = millis(); // Initialize Time variable
}

void loop() {
    float vout_tot = 0.0;
    for (int i = 0; i < 4; i++) {
        float vout = analogToVoltage(analogRead(ffs1 + i));
        vout *= cf;
        vout_tot += vout;
    }

    float poids_mesure = SLOPE * vout_tot - OFFSET; // Linear regression

    poids_mesures[index] = poids_mesure;
    index = (index + 1) % NUM_VALUES;

    if (index == 0) {
        float sum = 0.0;
        for (int i = 0; i < NUM_VALUES; i++) {
            sum += poids_mesures[i];
        }
        
        // Mean value
        mean_weight = sum / NUM_VALUES;

        // Réinitialisation du tableau
        memset(poids_mesures, 0, sizeof(poids_mesures));
    }

    Serial.println(mean_weight);

    delay(0);
}
