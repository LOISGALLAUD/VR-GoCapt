#include <Wire.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <PWFusion_TCA9548A.h>
#include <ArduinoJson.h>
#include <SPI.h>
//#include <SD.h>
#include "SdFat.h"

#define WIRE Wire
#define N_CHANNELS 5
#define N_ADDRESS 4 //8 max
#define UDP_TX_PACKET_MAX_SIZE 100
#define CHIP_SELECT 5
#define ADDR_BEGIN 1
#define CMPS_GET_ANGLE16 2
#define CMPS_DELAY 5
#define REC_LED 13
#define REC_BUTTON 33
#define ERR_LED 12
#define LED1 14
#define LED2 27
#define LED3 26

//---------------------------------------------------------------------------
/*PROTOTYPES*/

void merge(JsonObject dest, JsonObjectConst src);
void setupSensors();
void connectToWiFi();
void setupSDCard();
void printDirectory(File dir);
String getNextFileName();
void readSensorData(JsonDocument &rawDataDocs);
void writeDataToFile(String fileName, String data);
void readDataFromFile(String fileName);

//---------------------------------------------------------------------------
/*VARIABLES*/

// State machine
int state = 0;

// Record button
int buttonTime = millis();
struct Button {
	const uint8_t pin;
	bool pressed;
};
Button recButton = {
  .pin = REC_BUTTON,
  .pressed = false
}

// testbench
int time = millis();

// SD setup
String fileName;
SdFat sd;

// Sensors setup
TCA9548A i2cMux;
byte addresses[N_ADDRESS] = {0xC0, 0xC2, 0xC4, 0xC6/*,
                             0xC8, 0xCA, 0xCC, 0xCE*/}; // list of addresses from the CMPS sensors
int channels[N_CHANNELS] = {CHAN0, CHAN1, CHAN2, CHAN3,
                            CHAN4/*, CHAN5, CHAN6, CHAN7*/}; // CHAN(i) are defined in PWFusion_TCA9548A.h
int validCMPSs[N_CHANNELS * N_ADDRESS] = {0};
int sensorTime = millis();

// Wi-Fi setup
const char *ssid = "TP-Link_6A88";
const char *password = "07867552";
unsigned int serverPort = 8080;
IPAddress server(172, 23, 5, 54);
int status = WL_IDLE_STATUS;
WiFiClient client;
bool hasRemote = false;
IPAddress remote;
unsigned int remotePort;
char packetBuffer[UDP_TX_PACKET_MAX_SIZE];
WiFiUDP udp;

// JSON documents setup
type struct SensorData
{
  int time;
  int x;
  int y;
  int z;
  int accx;
  int accy;
  int accz;

};
JsonDocument rawDataDocs;

//---------------------------------------------------------------------------
/*FUNCTIONS*/

void merge(JsonObject dest, JsonObjectConst src)
{
  for (JsonPairConst kvp : src)
  {
    dest[kvp.key()] = kvp.value();
  }
}

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
      Serial.print("\n");

      Wire.beginTransmission(addresses[j] >> 1); // starts communication with CMPS12
      Wire.write(ADDR_BEGIN);                    // Sends the register we wish to start reading from
      Wire.endTransmission();

      Wire.requestFrom(addresses[j] >> 1, 1);
      int angle8 = Wire.read();
      if (angle8 == -1)
      {
        Serial.print("no CMPS (ch ");
        Serial.print(i);
        Serial.print(" adr ");
        Serial.print(j);
        Serial.print(")\n");
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

void connectToWiFi()
{
  Serial.println("");
  Serial.print("\nAttempting to connect to WPA network...");
  status = WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED)
  {
    Serial.print(".");
    delay(100);
  }
  Serial.println("\nConnected to network");
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
  Serial.println("\nList of files :");
  printDirectory(sd.open("/",O_READ));
}

void writeDataToFile(String fileName, String data)
{
  /*
  Create a file on the SD card and write the data in it

  Only used when starting record mode
  */
  File dataFile = sd.open(fileName.c_str(), O_RDWR | O_CREAT | O_AT_END);
  if (!dataFile)
  {
    Serial.println("Error opening file!");
    return;
  }
  dataFile.print(data + "\n");
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

void readDataFromFile(String fileName)
{
  File dataFile = sd.open(fileName.c_str());
  if (!dataFile)
  {
    Serial.println("Error opening file!");
    return;
  }
  while (dataFile.available())
  {
    Serial.write(dataFile.read());
  }
  dataFile.close();
}

void printDirectory(File dir)
{
  dir.ls();
}

String getNextFileName()
{
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

void readSensorData(JsonDocument &rawDataDocs) {
  // Reading data from the sensors
  JsonArray data = rawDataDocs.createNestedArray("data");
  
  for (int i = 0; i < N_CHANNELS; i++) {
    JsonObject sensorObject = data.createNestedObject();
    sensorObject["time"] = millis() - sensorTime;

    i2cMux.setChannel(channels[i]);  // Sélectionne le canal I2C actuel en utilisant le multiplexeur
    
    for (int j = 0; j < N_ADDRESS; j++) {
      if (validCMPSs[j + i * N_ADDRESS] == 1) {
        Wire.beginTransmission(addresses[j] >> 1);
        Wire.write(CMPS_GET_ANGLE16);
        Wire.endTransmission();
        Wire.requestFrom(addresses[j] >> 1, 2);
        
        while (Wire.available() < 2); // Useful for multiple byte reading
        unsigned char high_byte = Wire.read();
        unsigned char low_byte = Wire.read();
        unsigned int angle16 = high_byte;
        angle16 <<= 8;
        angle16 += low_byte;

        // Store the sensor data in a SensorData object
        SensorData sensorData;
        sensorData.x = ...;
        sensorData.y = ...;
        sensorData.z = ...;

        // Add the sensor data to the JSON document
        JsonObject positionObject = sensorObject.createNestedObject("position");
        positionObject["x"] = sensorData.x;
        positionObject["y"] = sensorData.y;
        positionObject["z"] = sensorData.z;
      }
      else {
        // En cas d'erreur, ajoute un message d'erreur aux données du capteur
        JsonObject positionObject = sensorObject.createNestedObject("position");
        positionObject["x"] = -1;
        positionObject["y"] = -1;
        positionObject["z"] = -1;
      }
    }
  }
}

void readAndWriteData()
{
  // int packetSize = udp.parsePacket();         // Check if there's an incoming udp packet

  // // If an incoming packet is detected
  // if (packetSize)
  // {
  //   Serial.print("Received packet of size ");
  //   Serial.println(packetSize);
  //   Serial.print("From ");
  //   remote = udp.remoteIP();         // Get the IP address of the sender
  //   remotePort = atoi(packetBuffer); // Parse the port number from the packet (Note: There's a potential issue here, see below)
  //   hasRemote = true;

  //   for (int i = 0; i < 4; i++)
  //   {
  //     Serial.print(remote[i], DEC); // Print each part of the sender's IP address
  //     if (i < 3)
  //     {
  //       Serial.print(".");
  //     }
  //   }

  //   Serial.print(", port");
  //   Serial.print(remotePort);                       // Print the sender's port number
  //   udp.read(packetBuffer, UDP_TX_PACKET_MAX_SIZE); // Read the packet data into packetBuffer
  //   Serial.print("Contents");
  //   Serial.println(atoi(packetBuffer));
  // }

  // Reading data from the sensors
  for (int i = 0; i < N_CHANNELS; i++)
  {
    i2cMux.setChannel(channels[i]); // Select the current I2C channel using the multiplexer
    for (int j = 0; j < N_ADDRESS; j++)
    {
      if (validCMPSs[j + i * N_ADDRESS] == 1)
      {
        Wire.beginTransmission(addresses[j] >> 1); // starts communication with CMPS12
        Wire.write(CMPS_GET_ANGLE16);                    // Sends the register we wish to start reading from
        Wire.endTransmission();
        Wire.requestFrom(addresses[j] >> 1, 2);

        while (Wire.available() < 2); //useful for multiple byte reading
        unsigned char high_byte = Wire.read();
        unsigned char low_byte = Wire.read();
        unsigned int angle16 = high_byte;                // Calculate 16 bit angle
        angle16 <<= 8;
        angle16 += low_byte;
        rawDataDocs["a" + String(j) + "c" + String(i) + "time"] = millis() - sensorTime;
        rawDataDocs["a" + String(j) + "c" + String(i) + "angle16"] = angle16;        // Read the sensor data and assign it to the JSON document
      }
      else
      {
        rawDataDocs["a" + String(j) + "c" + String(i) + "time"] = millis() - sensorTime;
        rawDataDocs["a" + String(j) + "c" + String(i) + "angle16"] = -1;           // Error message 
      }
    }
  }

  // // Sending data to the server
  // if (hasRemote)
  // {
  //   // If a remote connection has been established
  //   udp.beginPacket(remote, atoi(packetBuffer)); // Begin udp packet transmission to the remote IP and port (Note: There's a potential issue here, see below)
  //   String sendBuffer;
  //   serializeJson(rawDataDocs,sendBuffer);
  //   udp.print(sendBuffer); // Send the sensor data for each channel
  //   udp.endPacket(); // End the udp packet transmission
  // }

  // Writing data to the SD card
  appendDataToFile(fileName, rawDataDocs);

  // Simulation
  delay(CMPS_DELAY);
}

void stateMachine()
{
  if (recButton.pressed && !digitalRead(recButton.PIN))
  {
    if (millis() - buttonTime < 800)
    {
      setIndicatorLeds(1);
    }
    else if (millis() - buttonTime < 1800)
    {
      setIndicatorLeds(2);
    }
    else if (millis() - buttonTime < 2800)
    {
      setIndicatorLeds(3);
    }
    else
    {
      startAcquisition();
    }
  }
  else
  {
    resetLeds();
  }
}

void setIndicatorLeds(int count)
{
  digitalWrite(LED1, HIGH);
  digitalWrite(LED2, count > 1 ? HIGH : LOW);
  digitalWrite(LED3, count > 2 ? HIGH : LOW);
}

void startAcquisition()
{
  resetLeds();
  if (state == 0)
  {
    sensorTime = millis();
    fileName = getNextFileName();
    writeDataToFile(fileName, fileName);
  }
  state = !state;
  recButton.pressed = false;
}

void resetLeds()
{
  digitalWrite(LED1, LOW);
  digitalWrite(LED2, LOW);
  digitalWrite(LED3, LOW);
}

//---------------------------------------------------------------------------
/* INTERRUPTS */

void IRAM_ATTR isr_rec(){
  recButton.pressed = true;
  buttonTime = millis();
}

//---------------------------------------------------------------------------
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

  digitalWrite(LED3,HIGH);
  digitalWrite(ERR_LED,HIGH);

  //connectToWiFi();
  setupSDCard();
  setupSensors();
  //udp.begin(serverPort);
  Serial.println("IP Address Microcontroller:");
  //Serial.println(WiFi.localIP());

  // time setup
  Serial.print("time after setup:");Serial.println(millis() - time);
  time = millis();

  digitalWrite(LED3,LOW);
  digitalWrite(ERR_LED,LOW);

  // Interrupts
  attachInterrupt(recButton.PIN, isr_rec, FALLING);
}

void loop()
{
  // state Machine
  stateMachine();

  // Output Logic
  if (state == 1)
  {
    readAndWriteData();
    digitalWrite(REC_LED, HIGH);
  }
  else
  {
    digitalWrite(REC_LED, LOW);
  }

  // Debugging (print the loop total execution time. It must be under 20ms for 50fps.)
  Serial.println(millis() - time);
  time = millis();
}
