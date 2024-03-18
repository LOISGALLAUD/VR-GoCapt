#include <Wire.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <PWFusion_TCA9548A.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include "SdFat.h"

#define BYTES_TO_READ_IN_POSITIONING_MODULE 18
#define BYTES_TO_READ_IN_LOAD_MODULE 18
#define N_CHANNELS 5
#define N_ADDRESS 4  // 8 max
#define ATTRIBUTES_SIZE 10
#define ADDR_BEGIN 1
#define CMPS_GET_ANGLE16 2
#define CMPS_RAW9 6
#define CMPS_DELAY 5
#define UDP_TX_PACKET_MAX_SIZE 100
#define CHIP_SELECT 5
#define REC_LED 13
#define REC_BUTTON 33
#define ERR_LED 12
#define LED1 14
#define LED2 27
#define LED3 26

//---------------------------------------------------------------------------
/*PROTOTYPES*/

// SETUP FUNCTIONS
void setupSensors();
void setupWifi();
void setupSDCard();

// SD FUNCTIONS
String getNextFileName();
void createFile(String fileName);
void appendDataToFile(String fileName, int* data, size_t dataSize);
void appendDataToFile(String fileName, const char* data);

// IMU FUNCTIONS
int readTwoBytesAsInt();
void readSensorData(int* sensorArray);
void sendToServer(int* data, int size);
void sendToServer(const char* data);
void sendToServer(String data);
int binaryArrayToInt(bool* binaryArray);
int countNonZero(bool* arr, int size);

// STATE MACHINE FUNCTIONS
void stateMachine();
void setIndicatorLeds(int count);
void resetLeds();
void startAcquisition();

// INTERRUPTS
void IRAM_ATTR isr_rec();

//---------------------------------------------------------------------------
/*VARIABLES*/

// STATE MACHINE
int state = 0;

// REC BUTTON
int buttonTime = millis();
struct Button {
  const uint8_t pin;
  bool pressed;
};
Button recButton = {
  .pin = REC_BUTTON,
  .pressed = false
};

// SD SETUP
String fileName;
SdFat sd;

// IMU SETUP
TCA9548A i2cMux;
byte addresses[N_ADDRESS] = {
  0xC0, 0xC2, 0xC4, 0xC6 /*,
                             0xC8, 0xCA, 0xCC, 0xCE*/
};                       // list of addresses from the CMPS sensors
byte loadModuleAddresses[2] = { 0xC6, 0x0 };
int channels[N_CHANNELS] = { CHAN0, CHAN1, CHAN2, CHAN3,
                             CHAN4 /*, CHAN5, CHAN6, CHAN7*/ };  // CHAN(i) are defined in PWFusion_TCA9548A.h
bool sensorMask[N_CHANNELS * N_ADDRESS];
char sensorMaskToSend[7];
int validSensors;
int sensorTime = millis();

// WIFI SETUP
const char* ssid = "TP-Link_4AA1";
const char* password = "33372884";
unsigned int serverPort = 8080;      // Port utilisé par configuration pour l'envoi des données
IPAddress server(192, 168, 0, 255);  //Broadcast Address
int status = WL_IDLE_STATUS;
WiFiUDP udp;

// DATA GLOSSARY
// Example of data glossary for 3 sensors
const char* dataGlossary = "['time','magx','magy','magz','accx','accy','accz','gyrx','gyry','gyrz']";

//---------------------------------------------------------------------------//
/*FUNCTIONS*/

// SETUP FUNCTIONS

void setupSensors() {
  Wire.begin();
  i2cMux.begin();
  for (int i = 0; i < N_CHANNELS; i++) {
    i2cMux.setChannel(channels[i]);
    for (int j = 0; j < N_ADDRESS; j++) {
      Serial.print("sensor_" + String(j + i * N_ADDRESS) + String(" : "));

      Wire.beginTransmission(addresses[j] >> 1);
      Wire.write(ADDR_BEGIN);
      Wire.endTransmission();

      Wire.requestFrom(addresses[j] >> 1, 1);
      bool sensorFound = (Wire.read() != -1);
      if (!sensorFound) {  // Sensor not found
        Serial.print("no sensor detected (ch " + String(i));
        Serial.print(" addr " + String(j) + String(")"));
      } else {  // Sensor found
        sensorMask[j + i * N_ADDRESS] = 1;
        Serial.print("sensor is found.");
      }
      Serial.println("");
      sensorMask[j + i * N_ADDRESS] = sensorFound;
    }
  }
  validSensors = countNonZero(sensorMask, sizeof(sensorMask) / sizeof(bool));
  Serial.println("Valid sensors : " + String(validSensors));
}

void setupWifi() {
  Serial.println("Attempting to connect to WPA network...");
  status = WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(100);
  }
  Serial.println("\nCONNECTED TO NETWORK.");
  udp.begin(serverPort);
  Serial.println("UDP server started at port " + String(serverPort));
  sendToServer("UDP server started at port " + String(serverPort));
  Serial.print("IP Address Microcontroller: ");
  Serial.println(WiFi.localIP());
  Serial.println("WIFI SETUP IS DONE.");
}

void setupSDCard() {
  Serial.println("Initializing SD card...");
  if (!sd.begin(CHIP_SELECT, SD_SCK_MHZ(35))) {
    while (true) {
      Serial.print(".");
      delay(2000);
    }
    Serial.println("SD initialization failed!");
    return;
  }
  Serial.println("SD initialization done.");

  // Check list of files
  // Serial.println("List of files :");
  // sd.open("/", O_READ).ls();
}

// SD FUNCTIONS
String getNextFileName() {
  int maxNumber = 0;

  File root = sd.open("/", O_READ);
  if (!root) {
    Serial.println("Error opening root directory!");
    return String();
  }

  while (true) {
    File entry = root.openNextFile();
    if (!entry) {
      break;
    }
    char fileNameC[20];
    entry.getName(fileNameC, 20);
    String fileName(fileNameC);


    if (fileName.startsWith("data_") && fileName.endsWith(".txt")) {
      int fileNumber = fileName.substring(5, fileName.lastIndexOf('.')).toInt();

      if (fileNumber > maxNumber) {
        maxNumber = fileNumber;
      }
    }
    entry.close();
  }
  root.close();
  String fileName = "/data_" + String(maxNumber + 1) + ".txt";

  return fileName;
}

void createFile(String fileName) {
  /*
  Create a file on the SD card
  */

  File dataFile = sd.open(fileName.c_str(), O_RDWR | O_CREAT | O_AT_END);
  if (!dataFile) {
    Serial.println("Error opening file!");
    return;
  }
  dataFile.close();
}

void appendDataToFile(String fileName, int* data, size_t dataSize) {
  /*
  Append the data to the existing file
  Utilisée pour envoyer les données des capteurs (qui sont des entiers)
  */
  File dataFile = sd.open(fileName.c_str(), O_WRITE | O_AT_END);
  if (!dataFile) {
    Serial.println("Error opening file!");
    digitalWrite(ERR_LED, HIGH);
    return;
  }

  dataFile.print(String("["));
  // serializeJson(data, dataFile);
  for (int i = 0; i < dataSize; i++) {
    //Serial.println(data[i]); //DEBUG
    dataFile.print(String(data[i]) + ',');  //can cause prbls due to SdFat
  }
  dataFile.print(String("],"));
  dataFile.close();
}

void appendDataToFile(String fileName, const char* data) {
  /*
  Append the data to the existing file
  Utilisée pour envoyer le glossaire (qui est déjà une chaîne de caractères)
  */
  File dataFile = sd.open(fileName.c_str(), O_WRITE | O_AT_END);
  if (!dataFile) {
    Serial.println("Error opening file!");
    digitalWrite(ERR_LED, HIGH);
    return;
  }
  dataFile.print(data);
  dataFile.print(',');
  dataFile.close();
}

// SENSOR FUNCTIONS
int readTwoBytesAsInt() {
  unsigned char msb = Wire.read();
  unsigned char lsb = Wire.read();
  int16_t itermediate_result = (msb << 8) | lsb;
  int result = itermediate_result;
  return result;
}

void readSensorData(int* sensorArray) {
  int index = 0;
  // Iterate over each channel
  for (int i = 0; i < N_CHANNELS; i++) {
    i2cMux.setChannel(channels[i]);
    // Iterate over each sensor
    for (int j = 0; j < N_ADDRESS; j++) {
      if (sensorMask[j + i * N_ADDRESS] == 1) {
        byte currentAddress = addresses[j] >> 1;
        Wire.beginTransmission(currentAddress);
        Wire.write(CMPS_RAW9);
        Wire.endTransmission();

        int bytesToRead;
        if (currentAddress == (loadModuleAddresses[0] >> 1) |
        currentAddress == (loadModuleAddresses[1] >> 1)) {
          bytesToRead = BYTES_TO_READ_IN_LOAD_MODULE;
        } else {
          bytesToRead = BYTES_TO_READ_IN_POSITIONING_MODULE;
        }
        Serial.println("bytesToRead : " + String(bytesToRead));
        Wire.requestFrom(addresses[j] >> 1, bytesToRead);
        while (Wire.available() < bytesToRead)
          ;

        // Add the sensor data to the sensorArray
        sensorArray[index++] = millis() - sensorTime;
        for (int k = 0; k < ATTRIBUTES_SIZE - 1; k++) {
          sensorArray[index++] = readTwoBytesAsInt();
        }
      }
    }
  }
}

void sendToServer(int* data) {
  int size = validSensors * ATTRIBUTES_SIZE;
  //4+size + 10*size ([+]+,+\O)
  char buffer[size * 11 + 4];
  buffer[0] = '[';
  int offset = 1;

  for (int i = 0; i < size; i++) {
    offset += sprintf(buffer + offset, "%d,", data[i]);
  }
  buffer[offset] = ']';
  buffer[offset + 1] = ',';
  buffer[offset + 2] = '\0';  // Remove the last comma and add a null terminator

  udp.beginPacket(server, serverPort);
  udp.print(buffer);
  udp.endPacket();
}

// Utilisé pour l'envoi du glossaire et du masque de capteurs
void sendToServer(const char* data) {
  udp.beginPacket(server, serverPort);
  udp.print(data);
  udp.print(',');
  udp.endPacket();
}

void sendToServer(String data) {
  udp.beginPacket(server, serverPort);
  udp.print(data);
  udp.endPacket();
}

void sendToServer(int data) {
  udp.beginPacket(server, serverPort);
  udp.print(data);
  udp.endPacket();
}

int binaryArrayToInt(bool* binaryArray, int size) {
  int num = 0;
  for (int i = 0; i < size; i++) {
    num = (num << 1) | binaryArray[i];
  }
  return num;
}

int countNonZero(bool* arr, int size) {
  int count = 0;
  for (int i = 0; i < size; i++) {
    if (arr[i] != 0) {
      count++;
    }
  }
  return count;
}

// STATE MACHINE FUNCTIONS
void stateMachine() {
  if (recButton.pressed && !digitalRead(recButton.pin)) {
    unsigned long currentTime = millis();
    unsigned long elapsedTime = currentTime - buttonTime;

    int count;
    if (elapsedTime < 800) {
      count = 1;
    } else if (elapsedTime < 1800) {
      count = 2;
    } else if (elapsedTime < 2800) {
      count = 3;
    } else {
      startAcquisition();
      return;  // Exit early if startAcquisition() is called
    }
    setIndicatorLeds(count);
  } else {
    resetLeds();
  }
}

void setIndicatorLeds(int count) {
  digitalWrite(LED1, HIGH);
  digitalWrite(LED2, count > 1);
  digitalWrite(LED3, count > 2);
}

void resetLeds() {
  digitalWrite(LED1, LOW);
  digitalWrite(LED2, LOW);
  digitalWrite(LED3, LOW);
}

void startAcquisition() {
  resetLeds();
  if (state == 0) {
    sensorTime = millis();
    sendToServer(sensorMaskToSend);
    sendToServer(dataGlossary);
    fileName = getNextFileName();
    createFile(fileName);
    appendDataToFile(fileName, sensorMaskToSend);
    appendDataToFile(fileName, dataGlossary);
  }
  state = !state;
  recButton.pressed = false;
}


//---------------------------------------------------------------------------//
/* INTERRUPTS */

void IRAM_ATTR isr_rec() {
  recButton.pressed = true;
  buttonTime = millis();
}

//---------------------------------------------------------------------------//
/*MAIN*/

void setup() {
  Serial.begin(9600);
  pinMode(REC_LED, OUTPUT);
  pinMode(ERR_LED, OUTPUT);
  pinMode(LED1, OUTPUT);
  pinMode(LED2, OUTPUT);
  pinMode(LED3, OUTPUT);
  pinMode(REC_BUTTON, INPUT_PULLUP);

  // LEDS STARTING SETUP
  digitalWrite(LED3, HIGH);
  digitalWrite(ERR_LED, HIGH);

  // Setup
  delay(1000);
  setupSDCard();
  setupSensors();
  setupWifi();

  // LEDS SETUP STATE
  digitalWrite(LED3, LOW);
  digitalWrite(ERR_LED, LOW);

  int maskSize = sizeof(sensorMask) / sizeof(sensorMask[0]);
  Serial.println("maskSize : " + String(maskSize));
  Serial.print("MASK : 0b");
  for (int i = 0; i < maskSize; i++) {
    Serial.print(sensorMask[i]);
  }
  sprintf(sensorMaskToSend, "%d", binaryArrayToInt(sensorMask, maskSize));
  Serial.println(" = " + String(sensorMaskToSend));

  // INTERRUPTS
  attachInterrupt(recButton.pin, isr_rec, FALLING);
  Serial.println("SETUP IS DONE. READY TO START.");
}

void loop() {
  stateMachine();
  if (state == 1) {
    int sensorData[validSensors * ATTRIBUTES_SIZE] = { 0 };
    readSensorData(sensorData);
    sendToServer(sensorData);
    appendDataToFile(fileName, sensorData, validSensors * ATTRIBUTES_SIZE);
    digitalWrite(REC_LED, HIGH);
  } else {
    digitalWrite(REC_LED, LOW);
  }
}