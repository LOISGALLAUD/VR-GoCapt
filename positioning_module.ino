#include <Wire.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <PWFusion_TCA9548A.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include "SdFat.h"

#define MAX_SENSORS 21
#define BYTES_TO_READ_IN_POSITIONING_MODULE 18
#define BYTES_TO_READ_IN_POSITIONING_MODULE_2 12
#define BYTES_TO_READ_IN_LOAD_MODULE 22
#define N_CHANNELS 5
#define N_ADDRESS 4  // 8 max
#define ATTRIBUTES_SIZE 16
#define ADDR_BEGIN 1
#define CMPS_GET_ANGLE16 2
#define CMPS_RAW9 6
#define CMPS_RAW6 0x1F
#define CMPS_DELAY 5
#define UDP_TX_PACKET_MAX_SIZE 100
#define CHIP_SELECT 5
#define REC_LED 13
#define REC_BUTTON 33
#define ERR_LED 26
#define LED1 27
#define LED2 14
#define LED3 12

//---------------------------------------------------------------------------
/*PROTOTYPES*/

// SETUP FUNCTIONS
void setupSensors();
bool readWifiConfig(String& ssid, String& password);
void setupWifi();
void setupSDCard();

// SD FUNCTIONS
String getNextFileName();
void createFile(String fileName);
void appendDataToFile(String fileName, int* data, size_t dataSize);
void appendDataToFile(String fileName, const char* data);
void appendDataToFile(String fileName, String *data);

// SENSORS FUNCTIONS
int readTwoBytesAsInt();
void readSensorData(int* sensorArray);
void sendToServer(int* data, int size);
void sendToServer(const char* data);
void sendToServer(String data);
int countNonZero(bool* arr, int size);
void sensorMaskToLimbsGlossary(bool* sensorMask,String* limbsGlossary);

// STATE MACHINE FUNCTIONS
void stateMachine();
void setIndicatorLeds(int count);
void resetLeds();
void resetLedsForErr();
void resetAllLeds();
void startAcquisition();
void blinkForFun();

// INTERRUPTS
void IRAM_ATTR isr_rec();

//---------------------------------------------------------------------------
/*VARIABLES*/

// STATE MACHINE
int state = 0;
int blinkForFunVar = 1;
int steadyState = 0;
int SStime = 0;

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
byte addresses[N_ADDRESS] = {0xC0, 0xC2, 0xC4, 0xC6};  // 0xC0=Near Central Module -> 0xC6=Far Central Module                   
byte loadModuleAddresses[2] = { 0xC6, 0xC6 };
struct Channel {
  int chan;
  int length;
};
Channel channels[N_CHANNELS] = { 
  {CHAN0, 4}, // LEFT ARM
  {CHAN1, 4}, // LEFT LEG
  {CHAN2, 4}, // RIGHT LEG
  {CHAN3, 4}, // HEAD
  {CHAN4, 4}  // RIGHT ARM
};
// int channels[N_CHANNELS] = { CHAN0, CHAN1, CHAN2, CHAN3, CHAN4};
bool sensorMask[N_CHANNELS * N_ADDRESS];
int validSensors = 0;
int validLoadSensors = 0;
int sensorTime = millis();

// DATA GLOSSARY
const char* dataGlossary = "[\"time\",\"magx\",\"magy\",\"magz\",\"accx\",\"accy\",\"accz\",\"gyrx\",\"gyry\",\"gyrz\",\"accnrx\",\"accnry\",\"accnrz\",\"gyrcalx\",\"gyrcaly\",\"gyrcalz\",]""";
const String limbs[N_CHANNELS * N_ADDRESS] = { //nas means Not a Sensor, head on last address as hands and feets
  "\"lShoulder\"", "\"lArm\"", "\"lForearm\"", "\"lHand\"",  // CHAN0
  "\"NaS\"", "\"lThigh\"", "\"lLeg\"", "\"lFoot\"",         // CHAN1
  "\"NaS\"", "\"rThigh\"", "\"rLeg\"", "\"rFoot\"",         // CHAN2
  "\"hips\"", "\"spine\"", "\"NaS\"", "\"head\"",           // CHAN3 
  "\"rShoulder\"", "\"rArm\"", "\"rForearm\"", "\"rHand\"",  // CHAN4
};

String limbsGlossary[MAX_SENSORS];

// WIFI SETUP
String ssid;
String password;
unsigned int serverPort = 8080;
IPAddress server(192, 168, 0, 255);
WiFiUDP udp;

//---------------------------------------------------------------------------//
/*FUNCTIONS*/

// SETUP FUNCTIONS
void setupSensors() {
  Wire.begin();
  i2cMux.begin();
  for (int i = 0; i < N_CHANNELS; i++) {
    i2cMux.setChannel(channels[i].chan);
    for (int j = 0; j < channels[i].length; j++) {
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
      blinkForFun();
    }
  }
  validSensors = countNonZero(sensorMask, sizeof(sensorMask) / sizeof(bool));
  Serial.println("Valid sensors : " + String(validSensors));
}

void setupSDCard() {
  Serial.println("SD SETUP...");
  if (!sd.begin(CHIP_SELECT, SD_SCK_MHZ(35))) {
    while (true) {
      Serial.print(".");
      resetLedsForErr();
      digitalWrite(ERR_LED,HIGH);
      delay(2000);
    }
    Serial.println("SD FAILED.");
    return;
  }
  Serial.println("SD SETUP IS DONE.");
}

bool readWifiConfig(String& ssid, String& password) {
  File configFile = sd.open("/config.yml", O_READ);
  if (!configFile) {
    Serial.println("ERROR OPENING CONFIG FILE");
    return false;
  }

  char line[128];
  bool ssid_found = false;
  bool password_found = false;

  while (configFile.available()) {
    configFile.fgets(line, sizeof(line));
    String str = line;
    String(str).trim();

    if (str.startsWith("ssid:")) {
      ssid_found = true;
      int delimiter = str.indexOf(":");
      ssid = str.substring(delimiter + 1);
      ssid.trim();
    } else if (str.startsWith("password:")) {
      password_found = true;
      int delimiter = str.indexOf(":");
      password = str.substring(delimiter + 1);
      password.trim();
    }

    if (ssid_found && password_found) {
      break;
    }
  }

  configFile.close();

  if (!ssid_found || !password_found) {
    Serial.println("ERROR READING SSID OR PASSWORD FROM CONFIG FILE");
    return false;
  }

  return true;
}

void setupWifi() {
  Serial.println("CONNECTING TO NETWORK...");
  bool configFound = readWifiConfig(ssid, password); //passage par reference pour qu'on puisse sortir les string
  if (!configFound) {
    Serial.println("WIFI SETUP FAILED.");
    return;
  }
  bool status = WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(100);
  }
  Serial.println("\nCONNECTED TO NETWORK.");
  udp.begin(serverPort);
  Serial.println("UDP server started at port " + String(serverPort));
  sendToServer(String("UDP server started at port " + String(serverPort)));
  Serial.print("IP Address Microcontroller: ");
  Serial.println(WiFi.localIP());
  Serial.println("WIFI SETUP IS DONE.");
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
  for (int i = 0; i < dataSize; i++) {
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
    resetLedsForErr();
    digitalWrite(ERR_LED, HIGH);
    return;
  }
  dataFile.print(data);
  dataFile.print(',');
  dataFile.close();
}

void appendDataToFile(String fileName, String *data) {
  /*
  Append the data to the existing file
  Utilisée pour envoyer le glossaire des membres (qui est un tableau de chaînes de caractères)
  */
  File dataFile = sd.open(fileName.c_str(), O_WRITE | O_AT_END);
  if (!dataFile) {
    Serial.println("Error opening file!");
    resetLedsForErr();
    digitalWrite(ERR_LED, HIGH);
    return;
  }
  dataFile.print('[');
  for (int i = 0; i < validSensors + validLoadSensors; i++) {
    dataFile.print(data[i]);
    dataFile.print(',');
  }
  dataFile.print("],");
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
    i2cMux.setChannel(channels[i].chan);
    // Iterate over each sensor
    for (int j = 0; j < N_ADDRESS; j++) {
      if (sensorMask[j + i * N_ADDRESS] == 1) {
        byte currentAddress = addresses[j] >> 1;
        Wire.beginTransmission(currentAddress);
        Wire.write(CMPS_RAW9);
        Wire.endTransmission();

        // Read sensor data the right amount of bytes
        int bytesToRead;
        int attributesToSend = ATTRIBUTES_SIZE - 1;
        if ((currentAddress == (loadModuleAddresses[0] >> 1) && (i == 1) )||( currentAddress == (loadModuleAddresses[1] >> 1) && (i==2))) { //OxCE and chan for feet
          bytesToRead = BYTES_TO_READ_IN_LOAD_MODULE;
          attributesToSend += 2;
        } else {
          bytesToRead = BYTES_TO_READ_IN_POSITIONING_MODULE;
        }

        // Serial.println("bytesToRead : " + String(bytesToRead)); //debug for load module /!\ really slows down the execution time /!
        Wire.requestFrom(addresses[j] >> 1, bytesToRead);
        while (Wire.available() < bytesToRead);
        // Add the sensor data to the sensorArray
        sensorArray[index++] = millis() - sensorTime;
        for (int k = 0; k < attributesToSend - 6; k++) {
          sensorArray[index++] = readTwoBytesAsInt();
        }

        Wire.beginTransmission(currentAddress);
        Wire.write(CMPS_RAW6);
        Wire.endTransmission();

        Wire.requestFrom(addresses[j] >> 1, BYTES_TO_READ_IN_POSITIONING_MODULE_2);
        while (Wire.available() < BYTES_TO_READ_IN_POSITIONING_MODULE_2);
        // Add the sensor data to the sensorArray
        for (int k = 0; k < 6; k++) {
          sensorArray[index++] = readTwoBytesAsInt();
        }
      }
    }
  }
}

void sendToServer(int* data) {
  int size = validSensors * ATTRIBUTES_SIZE + validLoadSensors;
  //4+size + 10*size ([+]+,+\O)
  char buffer[size * 11 + 4];   // Initialement size*11
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

void sendToServer(String* data) {
  udp.beginPacket(server, serverPort);
  udp.print('[');
  for (int i = 0; i < validSensors + validLoadSensors; i++) {
    udp.print(data[i]);
    udp.print(',');
  }
  udp.print("],");
  udp.endPacket();
}

void sendToServer(int data) {
  udp.beginPacket(server, serverPort);
  udp.print(data);
  udp.endPacket();
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

void sensorMaskToLimbsGlossary(bool* sensorMask,String* limbsGlossary){
  int index = 0;
  
  for (int i = 0; i < sizeof(channels)/sizeof(channels[0]); i++) {
    for (int j = 0; j < channels[i].length; j++) {
      if (sensorMask[j + i * N_ADDRESS] == 1) {
        limbsGlossary[index++] = limbs[j + i * N_ADDRESS]; // Ajoute le membre au glossaire

        // Rajouter les deux capteurs de charge pour les pieds
        if (limbs[j + i * N_ADDRESS] == "\"lFoot\"") {
          validLoadSensors += 2;
          limbsGlossary[index++] = "\"lFrontLoad\"";
          limbsGlossary[index++] = "\"lBackLoad\"";
        } else if (limbs[j + i * N_ADDRESS] == "\"rFoot\"") {
          validLoadSensors += 2;
          limbsGlossary[index++] = "\"rFrontLoad\"";
          limbsGlossary[index++] = "\"rBackLoad\"";
        }
      }
    }
  }
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
    recButton.pressed = false; //correction
  }
}

void setIndicatorLeds(int count) {
  // digitalWrite(REC_LED, LOW);
  // digitalWrite(ERR_LED, LOW);
  digitalWrite(LED1, HIGH);
  digitalWrite(LED2, count > 1);
  digitalWrite(LED3, count > 2);
}

void resetLeds() {
  digitalWrite(LED1, LOW);
  digitalWrite(LED2, LOW);
  digitalWrite(LED3, LOW);
}

void resetAllLeds(){
  digitalWrite(REC_LED, LOW);
  digitalWrite(ERR_LED, LOW);
  digitalWrite(LED1, LOW);
  digitalWrite(LED2, LOW);
  digitalWrite(LED3, LOW);
}

void resetLedsForErr(){
  digitalWrite(REC_LED, LOW);
  digitalWrite(LED1, LOW);
  digitalWrite(LED2, LOW);
  digitalWrite(LED3, LOW);
}

void startAcquisition() {
  resetLeds();
  if (state == 0) {
    sendToServer(limbsGlossary);
    sendToServer(dataGlossary);
    fileName = getNextFileName();
    createFile(fileName);
    appendDataToFile(fileName, limbsGlossary); // Send the limb glossary
    appendDataToFile(fileName, dataGlossary);
    sensorTime = millis(); // Start the timer
  }
  state = !state;
  recButton.pressed = false;
}

void blinkForFun(){
  blinkForFunVar = (blinkForFunVar > 64)?(1):(blinkForFunVar*2);
  digitalWrite(REC_LED, (blinkForFunVar==1)?HIGH:LOW);
  digitalWrite(LED3, (blinkForFunVar==2 || blinkForFunVar==128)?HIGH:LOW);
  digitalWrite(LED2, (blinkForFunVar==4 || blinkForFunVar==64)?HIGH:LOW);
  digitalWrite(LED1, (blinkForFunVar==8 || blinkForFunVar==32)?HIGH:LOW);
  digitalWrite(ERR_LED, (blinkForFunVar==16)?HIGH:LOW);
  delay(100);
}

void PrintState(int validsensors){
  int test = validsensors-1;
  if(steadyState == 0){
    SStime = millis();
    steadyState = 1;
  }
  if (millis()-SStime < 800 && (recButton.pressed == false)){
    digitalWrite(REC_LED, (test&1<<0)?HIGH:LOW);
    digitalWrite(LED3, (test&1<<1)?HIGH:LOW);
    digitalWrite(LED2, (test&1<<2)?HIGH:LOW);
    digitalWrite(LED1, (test&1<<3)?HIGH:LOW);
    digitalWrite(ERR_LED, (test&1<<4)?HIGH:LOW);
  }
  else if((millis()-SStime)>=800 && (millis()-SStime)<1300 && (recButton.pressed == false)){
    resetAllLeds();
  }
  else{
    steadyState = 0;
  }
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
  digitalWrite(REC_LED, HIGH);
  digitalWrite(LED1, HIGH);
  digitalWrite(LED2, HIGH);
  digitalWrite(LED3, HIGH);
  digitalWrite(ERR_LED, HIGH);

  delay(500);
  blinkForFun();
  delay(500);
  blinkForFun();
  setupSDCard();
  blinkForFun();
  setupSensors();
  blinkForFun();
  setupWifi();
  blinkForFun();

  int maskSize = N_ADDRESS*N_CHANNELS;
  Serial.print("MASK : ");
  for (int i = 0; i < sizeof(sensorMask) / sizeof(sensorMask[0]); i++) {
    Serial.print(sensorMask[i]);
    blinkForFun();
  }
  sensorMaskToLimbsGlossary(sensorMask,limbsGlossary);
  Serial.println("\nLIMBS GLOSSARY : ");
  for (int i = 0; i < validSensors + validLoadSensors; i++) {
    Serial.println(limbsGlossary[i]);
    blinkForFun();
  }

  // INTERRUPTS
  attachInterrupt(recButton.pin, isr_rec, FALLING);

  // LEDS SETUP STATE

  digitalWrite(REC_LED, LOW);
  digitalWrite(LED1, LOW);
  digitalWrite(LED2, LOW);
  digitalWrite(LED3, LOW);
  digitalWrite(ERR_LED, LOW);

  Serial.println("SETUP IS DONE. READY TO START.");
}

void loop() {
  stateMachine();
  if (state == 1) {
    int sensorData[validSensors * ATTRIBUTES_SIZE + validLoadSensors] = { 0 };
    readSensorData(sensorData);
    sendToServer(sensorData);
    appendDataToFile(fileName, sensorData, validSensors * ATTRIBUTES_SIZE + validLoadSensors);
    digitalWrite(REC_LED, HIGH);
  } else {
    digitalWrite(REC_LED, LOW);
    PrintState(validSensors);
  }
}