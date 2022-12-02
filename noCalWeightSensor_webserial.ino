#include <HX711_ADC.h>
#if defined(ESP8266)|| defined(ESP32) || defined(AVR)
#include <EEPROM.h>
#include <FirebaseESP32.h>
#include  <WiFi.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <SPIFFS.h>
#include <Arduino.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <WebSerial.h>
#endif

#define ONBOARD_LED  2

//variables:
#define FIREBASE_HOST "[REDACTED]"
#define WIFI_SSID "[REDACTED]" // Change the name of your WIFI
#define WIFI_PASSWORD "[REDACTED]" // Change the password of your WIFI
#define FIREBASE_Authorization_key "[REDACTED]"

FirebaseData firebaseData;
FirebaseJson json;
AsyncWebServer server(80);

// Define NTP Client to get time
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);

// Variables to save date and time
String formattedDate;
String dayStamp;
String timeStamp;
String ipAddr;
//pins:
const int HX711_dout = 35; //mcu > HX711 dout pin
const int HX711_sck = 32; //mcu > HX711 sck pin

//HX711 constructor:
HX711_ADC LoadCell(HX711_dout, HX711_sck);

const int calVal_eepromAdress = 0;
unsigned long t = 0;

void setup() {
  Serial.begin(57600); delay(10);
  pinMode(ONBOARD_LED,OUTPUT);
  if (!SPIFFS.begin(true)) {
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }
  WiFi.begin (WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting...");
  while (WiFi.status() != WL_CONNECTED)
  {
    Serial.print(".");
    digitalWrite(ONBOARD_LED,HIGH);
    delay(150);
    digitalWrite(ONBOARD_LED,LOW);
    delay(150);
  }
  Serial.println();
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
  //Serial.println(WiFi.localIP().toString());
  Serial.println();
  Firebase.begin(FIREBASE_HOST,FIREBASE_Authorization_key);
  Firebase.setString(firebaseData, "/WEIGHT_SENSOR/IP", WiFi.localIP().toString());

// Initialize a NTPClient to get time
  timeClient.begin();
  // Set offset time in seconds to adjust for your timezone, for example:
  // GMT +1 = 3600
  // GMT +8 = 28800
  // GMT -1 = -3600
  // GMT 0 = 0
  timeClient.setTimeOffset(-18000);
  Serial.println();
  Serial.println("Starting...");

  File file = SPIFFS.open("/data.csv", FILE_WRITE);
  if (!file) {
    Serial.println("There was an error opening the file for writing");
    return;
  }
  file.print("");

  LoadCell.begin();
  //LoadCell.setReverseOutput(); //uncomment to turn a negative output value to positive
  float calibrationValue; // calibration value (see example file "Calibration.ino")
  //calibrationValue = 696.0; // uncomment this if you want to set the calibration value in the sketch
#if defined(ESP8266)|| defined(ESP32)
  EEPROM.begin(512); // uncomment this if you use ESP8266/ESP32 and want to fetch the calibration value from eeprom
#endif
  EEPROM.get(calVal_eepromAdress, calibrationValue); // uncomment this if you want to fetch the calibration value from eeprom

  unsigned long stabilizingtime = 2000; // preciscion right after power-up can be improved by adding a few seconds of stabilizing time
  boolean _tare = true; //set this to false if you don't want tare to be performed in the next step
  LoadCell.start(stabilizingtime, _tare);
  if (LoadCell.getTareTimeoutFlag()) {
    Serial.println("Timeout, check MCU>HX711 wiring and pin designations");
    while (1);
  }
  else {
    LoadCell.setCalFactor(calibrationValue); // set calibration value (float)
    Serial.println("Startup is complete");
  }
  WebSerial.begin(&server);
  WebSerial.msgCallback(recvMsg);
  server.begin();
}

void recvMsg(uint8_t *data, size_t len){
  String d = "";
  for(int i=0; i < len; i++){
    d += char(data[i]);
  }
  //WebSerial.println(d);
  if (d == "T"){
    WebSerial.println("Taring...");
    LoadCell.tareNoDelay();
    WebSerial.println("Tare Complete!");
  }
  //if (d=="C"){
  //  WebSerial.print("Current calibration factor: ");
  //  WebSerial.println(LoadCell.getCalFactor());
  //  delay(2000);
  //}
}

void loop() {
  while(!timeClient.update()) {
    timeClient.forceUpdate();
  }
  // The formattedDate comes with the following format:
  // 2018-05-28T16:00:13Z
  // We need to extract date and time
  formattedDate = timeClient.getFormattedDate();
  //Serial.println(formattedDate);

  // Extract date
  int splitT = formattedDate.indexOf("T");
  dayStamp = formattedDate.substring(0, splitT);
  dayStamp.remove(4, 1);
  dayStamp.remove(6, 1);
  //Serial.print("DATE: ");
  //Serial.println(dayStamp);
  // Extract time
  timeStamp = formattedDate.substring(splitT+1, formattedDate.length()-1);
  timeStamp.remove(2, 1);
  timeStamp.remove(4, 1);
  //Serial.print("HOUR: ");
  //Serial.println(timeStamp);
  //delay(1000);
//------------------------------------------------------------------------------------
  static boolean newDataReady = 0;
  const int serialPrintInterval = 0; //increase value to slow down serial print activity

  // check for new data/start next conversion:
  if (LoadCell.update()) newDataReady = true;

  // get smoothed value from the dataset:
  if (newDataReady) {
    if (millis() > t + serialPrintInterval) {
      float i = LoadCell.getData();
      Serial.print("Weight (kg): ");
      Serial.println(i);
      WebSerial.print("Weight (kg): ");
      WebSerial.println(i);
//------------------------------------------------------------------------------------
      if (i > 2) {
       Firebase.setString(firebaseData, "/WEIGHT_SENSOR/DATE", String(dayStamp));
       Firebase.setString(firebaseData, "/WEIGHT_SENSOR/TIME", String(timeStamp));
       Firebase.setString(firebaseData, "/WEIGHT_SENSOR/WEIGHT", i);
       File file = SPIFFS.open("/data.csv", FILE_APPEND);
       if (!file) {
         Serial.println("There was an error opening the file for writing");
         return;
       }
       file.print(String(dayStamp));
       file.print(",");
       file.print(String(timeStamp));
       file.print(",");
       file.println(i);
       file.close();
       //delay(1000); 
      }
      //else if (i < -5) {
      //  LoadCell.tareNoDelay();
      //}
//------------------------------------------------------------------------------------
      newDataReady = 0;
      t = millis();
    }
  }
    if (Serial.available() > 0) {
    char inByte = Serial.read();
    if (inByte == 't') LoadCell.tareNoDelay(); //tare
    else if (inByte == 'r'){
      File file = SPIFFS.open("/data.csv");
      if(!file || file.isDirectory()){
        Serial.println("Failed to open file for reading!");
        delay(1000);
        return;
      }
      while(file.available()){
        Serial.write(file.read());
      }
      delay(5000);
      file.close();
    }
    else if (inByte == 'd'){
      File file = SPIFFS.open("/data.csv", FILE_WRITE);
      if(!file){
        Serial.println("Failed to open file for reading!");
        delay(1000);
        return;
      }
      if(file.print("")){
        Serial.println("Data Cleared!");
        delay(1000);
    }
    file.close();
    }
    else if (inByte == 'c'){
      changeSavedCalFactor();
    }
  }
  //delay(125);
}

void changeSavedCalFactor() {
  float oldCalibrationValue = LoadCell.getCalFactor();
  boolean _resume = false;
  Serial.println("***");
  Serial.print("Current value is: ");
  Serial.println(oldCalibrationValue);
  Serial.println("Now, send the new value from serial monitor, i.e. 696.0");
  float newCalibrationValue;
  while (_resume == false) {
    if (Serial.available() > 0) {
      newCalibrationValue = Serial.parseFloat();
      if (newCalibrationValue != 0) {
        Serial.print("New calibration value is: ");
        Serial.println(newCalibrationValue);
        LoadCell.setCalFactor(newCalibrationValue);
        _resume = true;
      }
    }
  }
  _resume = false;
  Serial.print("Save this value to EEPROM adress ");
  Serial.print(calVal_eepromAdress);
  Serial.println("? y/n");
  while (_resume == false) {
    if (Serial.available() > 0) {
      char inByte = Serial.read();
      if (inByte == 'y') {
#if defined(ESP8266)|| defined(ESP32)
        EEPROM.begin(512);
#endif
        EEPROM.put(calVal_eepromAdress, newCalibrationValue);
#if defined(ESP8266)|| defined(ESP32)
        EEPROM.commit();
#endif
        EEPROM.get(calVal_eepromAdress, newCalibrationValue);
        Serial.print("Value ");
        Serial.print(newCalibrationValue);
        Serial.print(" saved to EEPROM address: ");
        Serial.println(calVal_eepromAdress);
        _resume = true;
      }
      else if (inByte == 'n') {
        Serial.println("Value not saved to EEPROM");
        _resume = true;
      }
    }
  }
  Serial.println("End change calibration value");
  Serial.println("***");
}
