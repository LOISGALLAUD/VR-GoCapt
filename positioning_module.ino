#include <Wire.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <PWFusion_TCA9548A.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include "SdFat.h"


//---------------------------------------------------------------------------
/*DEFINES*/

// SENSORS
#define N_CHANNELS 5
#define N_ADDRESS 4 // 8 max
#define ADDR_BEGIN 1
// CMPS12
#define CMPS_GET_ANGLE16 2
#define CMPS_RAW9 6
#define CMPS_DELAY 5
// UDP & SD
#define UDP_TX_PACKET_MAX_SIZE 100
#define CHIP_SELECT 5
// PINS
#define REC_LED 13
#define REC_BUTTON 33
// LEDS
#define ERR_LED 12
#define LED1 14
#define LED2 27
#define LED3 26

//---------------------------------------------------------------------------
/*PROTOTYPES*/


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

// SENSOR SETUP
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
unsigned int serverPort = 8080;
IPAddress server(172, 23, 5, 54);
int status = WL_IDLE_STATUS;
WiFiClient client;
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
      Serial.println(j + i * N_ADDRESS);

      Wire.beginTransmission(addresses[j] >> 1); // starts communication with CMPS12
      Wire.write(ADDR_BEGIN);                    // Sends the register we wish to start reading from
      Wire.endTransmission();

      Wire.requestFrom(addresses[j] >> 1, 1);
      int angle8 = Wire.read();
      if (angle8 == -1)
      {
        Serial.println("no CMPS (ch");
        Serial.print(i);
        Serial.print("addr");
        Serial.print(j);
        Serial.print(")");
      }
      else
      {
        validCMPSs[j + i * N_ADDRESS] = 1;
        Serial.print("Angle8 = ");
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

void appendDataToFile(String fileName, JsonDocument data)
{
  /*
  Append the data to the existing file
  */
  File dataFile = sd.open(fileName.c_str(), O_WRITE | O_AT_END);
  if (!dataFile)
  {
    Serial.println("Error opening file!");
    digitalWrite(ERR_LED,HIGH);
    return;
  }
  serializeJson(data, dataFile);
  dataFile.close();
}

// SENSOR FUNCTIONS
void readSensorData(int *sensorArray) {
  int index = 0;
  
  // Iterate over each channel
  for (int i = 0; i < N_CHANNELS; i++) {
    
    // Iterate over each sensor
    for (int j = 0; j < N_ADDRESS; j++) {
      if (validCMPSs[j + i * N_ADDRESS] == 1) {
        Wire.beginTransmission(addresses[j] >> 1);
        Wire.write(CMPS_RAW9);
        Wire.endTransmission();
        Wire.requestFrom(addresses[j] >> 1, ATTRIBUTES_SIZE);
        
        //-------------------------READING PART (HAVE TO CHECK THE DOCUMENTATION)-------------------------//
        while (Wire.available() < ATTRIBUTES_SIZE); // Useful for multiple byte reading
        // unsigned char high_byte = Wire.read();
        // unsigned char low_byte = Wire.read();
        // unsigned int angle16 = high_byte;
        // angle16 <<= 8;
        // angle16 += low_byte;
        //-------------------------READING PART (HAVE TO CHECK THE DOCUMENTATION)-------------------------//

        // Add the sensor data to the sensorArray
        sensorArray[index++] = millis() - sensorTime;
        sensorArray[index++] = Wire.read();
        sensorArray[index++] = Wire.read();
        sensorArray[index++] = Wire.read();
        sensorArray[index++] = Wire.read();
        sensorArray[index++] = Wire.read();
        sensorArray[index++] = Wire.read();
        sensorArray[index++] = Wire.read();
        sensorArray[index++] = Wire.read();
        sensorArray[index++] = Wire.read();
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

void sendToServer(int* data) {
  int packetSize = udp.parsePacket();
  IPAddress remote;
  unsigned int remotePort;
  char packetBuffer[UDP_TX_PACKET_MAX_SIZE];

  // If an incoming packet is detected
  if (packetSize)
  {
    remote = udp.remoteIP();
    udp.read(packetBuffer, UDP_TX_PACKET_MAX_SIZE);
    udp.beginPacket(remote, atoi(packetBuffer));

    String sendBuffer;
    serializeJson(*data, sendBuffer);
    udp.print(sendBuffer);
    udp.endPacket();
  }
}

// STATE MACHINE FUNCTIONS
void stateMachine() {
  if (recButton.pressed && !digitalRead(recButton.PIN)) {
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
    sendToServer(dataGlossary);

    // Create a new file in SD card
    fileName = getNextFileName();
    createFile(fileName);
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
  setupWifi();
  udp.begin(serverPort);
  Serial.println("UDP server started at port " + String(serverPort));
  Serial.println("IP Address Microcontroller:", WiFi.localIP());

  // LEDS SETUP STATE
  digitalWrite(LED3, LOW);
  digitalWrite(ERR_LED, LOW);

  //-----------------------------------ENDING SETUP-----------------------------------//

  // INTERRUPTS
  attachInterrupt(recButton.PIN, isr_rec, FALLING);
}

void loop()
{
  stateMachine();

  // Output Logic
  if (state == 1)
  {
    int *sensorData[N_CHANNELS * N_ADDRESS * ATTRIBUTES_SIZE];
    readSensorData(sensorData);
    sendSensorDataToServer(sensorData);
    appendDataToFile(fileName, sensorData);
    digitalWrite(REC_LED, HIGH);
  }
  else
  {
    digitalWrite(REC_LED, LOW);
  }
}
