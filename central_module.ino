/*
    * Acquire data from sensors and send it to the WiFi router.
    * Request from the load module the data from the IMU and the flexiforce sensors.
*/

#include <Wire.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <PWFusion_TCA9548A.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include "SdFat.h"

#define N_CHANNELS 5
#define N_ADDRESS 4 // 8 max
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
const int ATTRIBUTES_SIZE = 9;
TCA9548A i2cMux;
byte addresses[N_ADDRESS] = {0xC0, 0xC2, 0xC4, 0xC6/*,
                             0xC8, 0xCA, 0xCC, 0xCE*/}; // list of addresses from the CMPS sensors
int channels[N_CHANNELS] = {CHAN0, CHAN1, CHAN2, CHAN3,
                            CHAN4/*, CHAN5, CHAN6, CHAN7*/}; // CHAN(i) are defined in PWFusion_TCA9548A.h
int validCMPSs[N_CHANNELS * N_ADDRESS] = {0};
int sensorTime = millis();

// WIFI SETUP
const char *ssid = "TP-Link_6A88";
const char *password = "07867552";
unsigned int serverPort = 8080; // Port utilisé par configuration pour l'envoi des données
IPAddress server(192, 168, 0, 255);  //Broadcast Address
int status = WL_IDLE_STATUS;
WiFiUDP udp;

// DATA GLOSSARY
// Example of data glossary for 3 sensors
const char *dataGlossary = "['time', 'magx', 'magy', 'magz', 'accx', 'accy', 'accz', 'gyrx', 'gyry', 'gyrz']";

//---------------------------------------------------------------------------//
/*FUNCTIONS*/

// SETUP FUNCTIONS
void setupSensors()
{
  int timer = 0;
  Wire.begin();
  i2cMux.begin();
  for (int i = 0; i < N_CHANNELS; i++)
  {
    i2cMux.setChannel(channels[i]); // Select the current I2C channel using the multiplexer
    for (int j = 0; j < N_ADDRESS; j++)
    {
      Serial.print("init ");
      Serial.print(j + i * N_ADDRESS);

      Wire.beginTransmission(addresses[j] >> 1); // starts communication with CMPS12
      Wire.write(ADDR_BEGIN);                    // Sends the register we wish to start reading from
      Wire.endTransmission();

      Wire.requestFrom(addresses[j] >> 1, 1);
      int angle8 = Wire.read();
      if (angle8 == -1)
      {
        Serial.print(" no CMPS (ch");
        Serial.print(i);
        Serial.print("addr");
        Serial.print(j);
        Serial.println(")");
      }
      else
      {
        validCMPSs[j + i * N_ADDRESS] = 1;
        Serial.print(" Angle8 = ");
        Serial.println(angle8, DEC);
      }
    }
  }
}

void setupWifi()
{
  Serial.println("Attempting to connect to WPA network...");
  status = WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED)
  {
    Serial.print(".");
    delay(100);
  }
  Serial.println("Connected to network.");
}

void setupSDCard()
{
  Serial.print("Initializing SD card...");
  if (!sd.begin(CHIP_SELECT,SD_SCK_MHZ(35)))
  {
    Serial.println("SD initialization failed!");
    while(true){
      Serial.print(".");
      delay(2000);
    }
    return;
  }
  Serial.println("SD initialization done.");

  // Check list of files
  Serial.println("List of files :");
  sd.open("/",O_READ).ls();
}

// SD FUNCTIONS
String getNextFileName() {
  int maxNumber = 0;

  File root = sd.open("/",O_READ);
  if (!root)
  {
    Serial.println("Error opening root directory!");
    return String();
  }

  while (true)
  {
    File entry = root.openNextFile();
    if (!entry)
    {
      break;
    }
    char fileNameC[20];
    entry.getName(fileNameC,20);
    String fileName(fileNameC);
    

    if (fileName.startsWith("data_") && fileName.endsWith(".txt"))
    {
      int fileNumber = fileName.substring(5, fileName.lastIndexOf('.')).toInt();

      if (fileNumber > maxNumber)
      {
        maxNumber = fileNumber;
      }
    }
    entry.close();
  }
  root.close();
  String fileName = "/data_" + String(maxNumber + 1) + ".txt";

  return fileName;
}

void createFile(String fileName)
{
  /*
  Create a file on the SD card
  */

  File dataFile = sd.open(fileName.c_str(), O_RDWR | O_CREAT | O_AT_END);
  if (!dataFile)
  {
    Serial.println("Error opening file!");
    return;
  }
  dataFile.close();
}

void appendDataToFile(String fileName, int* data, size_t dataSize)
{
  /*
  Append the data to the existing file
  Utilisée pour envoyer les données des capteurs (qui sont des entiers)
  */
  File dataFile = sd.open(fileName.c_str(), O_WRITE | O_AT_END);
  if (!dataFile)
  {
    Serial.println("Error opening file!");
    digitalWrite(ERR_LED,HIGH);
    return;
  }
  // serializeJson(data, dataFile);
  for (int i=0;i<dataSize;i++){
    //Serial.println(data[i]); //DEBUG
    dataFile.print(String(data[i]) + ','); //can cause prbls due to SdFat
  }
  dataFile.close();
}

void appendDataToFile(String fileName, const char* data)
{
  /*
  Append the data to the existing file
  Utilisée pour envoyer le glossaire (qui est déjà une chaîne de caractères)
  */
  File dataFile = sd.open(fileName.c_str(), O_WRITE | O_AT_END);
  if (!dataFile)
  {
    Serial.println("Error opening file!");
    digitalWrite(ERR_LED,HIGH);
    return;
  }
  dataFile.print(data);
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
      if (validCMPSs[j + i * N_ADDRESS] == 1) {
        Wire.beginTransmission(addresses[j] >> 1);
        Wire.write(CMPS_RAW9);
        Wire.endTransmission();
        Wire.requestFrom(addresses[j] >> 1, 18);
        
        while (Wire.available() < 18); // Useful for multiple byte reading

        // Add the sensor data to the sensorArray
        sensorArray[index++] = millis() - sensorTime;
        sensorArray[index++] = readTwoBytesAsInt();
        sensorArray[index++] = readTwoBytesAsInt();
        sensorArray[index++] = readTwoBytesAsInt();
        sensorArray[index++] = readTwoBytesAsInt();
        sensorArray[index++] = readTwoBytesAsInt();
        sensorArray[index++] = readTwoBytesAsInt();
        sensorArray[index++] = readTwoBytesAsInt();
        sensorArray[index++] = readTwoBytesAsInt();
        sensorArray[index++] = readTwoBytesAsInt();
      }
      else {
        // In case of error, add -1 to the sensorArray for each attribute
        for (int k = 0; k < ATTRIBUTES_SIZE; k++) {
          sensorArray[index++] = -1;
        }
      }
    }
  }
}

// Utilisée pour envoyer les données des capteurs (qui sont des entiers)
void sendToServer(int* data, int size) {
  char buffer[size * 10];
  int offset = 0;
  for (int i=0; i<size; i++){
    offset += sprintf(buffer + offset,"%d,",data[i]);
  }
  buffer[offset] = '\0';
  
  udp.beginPacket(server, serverPort);
  udp.print(buffer);
  udp.endPacket();
}

// Utilisée pour envoyer le glossaire (qui est déjà une chaîne de caractères)
void sendToServer(const char* data) {
  udp.beginPacket(server, serverPort);
  udp.print(data);
  udp.endPacket();
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
      return; // Exit early if startAcquisition() is called
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

    // Send the glossary to the server
    sendToServer(dataGlossary); // doit attendre la réception d'un paquet pour le faire ?

    // Create a new file in SD card
    fileName = getNextFileName();
    createFile(fileName);
    // Insert here dataglossary in SD
    appendDataToFile(fileName, dataGlossary);
  }
  state = !state;
  recButton.pressed = false;
}


//---------------------------------------------------------------------------//
/* INTERRUPTS */

void IRAM_ATTR isr_rec(){
  recButton.pressed = true;
  buttonTime = millis();
}

//---------------------------------------------------------------------------//
/*MAIN*/

void setup()
{
  Serial.begin(115200);

  // OUTPUTS AND INPUTS
  pinMode(REC_LED,OUTPUT);
  pinMode(ERR_LED,OUTPUT);
  pinMode(LED1,OUTPUT);
  pinMode(LED2,OUTPUT);
  pinMode(LED3,OUTPUT);
  pinMode(REC_BUTTON,INPUT_PULLUP);

  //----------------------------------STARTING SETUP----------------------------------//
  digitalWrite(LED3, HIGH);
  digitalWrite(ERR_LED, HIGH);

  // Setup
  setupSDCard();
  setupSensors();
  // setupWifi();
  // udp.begin(serverPort);
  // Serial.println("UDP server started at port " + String(serverPort));
  // sendToServer("UDP server started at port " + String(serverPort));
  // Serial.print("IP Address Microcontroller: ");
  // Serial.println(WiFi.localIP());
  // Serial.println(udp.remoteIP()); // debug: renvoie 0.0.0.0

  // LEDS SETUP STATE
  digitalWrite(LED3, LOW);
  digitalWrite(ERR_LED, LOW);

  //-----------------------------------ENDING SETUP-----------------------------------//

  // INTERRUPTS
  attachInterrupt(recButton.pin, isr_rec, FALLING);
}

void loop()
{
  stateMachine();
  
  // Output Logic
  if (state == 1)
  {
    int sensorData[N_CHANNELS * N_ADDRESS * ATTRIBUTES_SIZE];
    readSensorData(sensorData);
    // sendToServer(sensorData, N_CHANNELS * N_ADDRESS * ATTRIBUTES_SIZE);
    appendDataToFile(fileName, sensorData, N_CHANNELS * N_ADDRESS * ATTRIBUTES_SIZE);
    digitalWrite(REC_LED, HIGH);
  }
  else
  {
    digitalWrite(REC_LED, LOW);
  }
}