
/*
esp8266 with BME280
- config menu with serial
- ntp update
- upload weather data on aprs servers
- local web page for weather informations and configuration
 f4goh@orange.fr
*/

#include <Wire.h>
#include <SPI.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>


#include <NTPtimeESP.h>

#include "FS.h"

NTPtime NTPch("ch.pool.ntp.org");
//NTPtime NTPch("time.nist.gov");



#include <SparkFunBME280.h>
//Global sensor object
BME280 mySensor;

/*
const char* ssid = "Livebox-0E00";
const char* password = "FzhSJGFouVGNLYKAMa";
const char* host = "cwop.aprs.net";
*/

ESP8266WebServer server(80);

strDateTime dateTime;
byte nextMinTx;


typedef struct  {
  int temperatureC;
  int temperatureF;
  int pression ;
  int humidite;
} WeatherStruct;
WeatherStruct wx;    //declare la structure

typedef struct  {
  char ssid[50];
  char password[50];
} configStruct;
configStruct internet;    //declare la structure

typedef struct  {
  char callsign[10];
  char longitude[10];
  char latitude[10];
  char clientAdress[20];
  int clientPort;
  long transmitDelay;
  byte logger;
} positionStruct;
positionStruct station;    //declare la structure

long previousMillis = 0;
long currentMillis;
long EcratMillis;
char car;

void setup(void)
{
  strcpy(station.clientAdress, "cwop.aprs.net");
  station.clientPort = 14580;
  station.transmitDelay = 10;
  station.logger = 0;

  Serial.begin(115200);
  Serial.println();
  delay(10);
  SPIFFS.begin();
  if (SPIFFS.exists("/ssid.txt") == 0) {
    configMenu();
  }
  else
  {
    readSsidFile();
  }
  if (SPIFFS.exists("/station.txt") == 0) {
    configMenu();
  }
  else
  {
    readStationFile();
  }
  if  (detectMenu() == 1) configMenu();
  ssidConnect();
  initBme();
  printBme();

  delay(1000);


  server.on("/", handleRoot);
  /*
  server.on("/inline", []() {
    server.send(200, "text/plain", "this works as well");
  });
server.on ( "/test.svg", drawGraph );
*/
  server.onNotFound(handleNotFound);
  server.begin();

  ntp();
  previousMillis = millis();

}


void handleRoot() {
  char temp[600];
  snprintf ( temp, 600,
             "<html>\
  <head>\
    <meta http-equiv='refresh' content='5'/>\
    <title>%s Weather Station</title>\
    <style>\
      body { background-color: #cccccc; font-family: Arial, Helvetica, Sans-Serif; Color: #000088; font-size: 50px;}\
    </style>\
  </head>\
  <body>\
    <h1>%s Weather Station</h1>\
    <p>Uptime: %02d:%02d:%02d</p><br>\
    <p>Temperature: %d degC</p><br>\
    <p>Pressure: %d pa</p><br>\
    <p>Humidity: %d %</p><br>\
  </body>\
</html>",
             station.callsign,station.callsign,dateTime.hour, dateTime.minute, dateTime.second, wx.temperatureC / 10, wx.pression, wx.humidite
           );
  server.send ( 200, "text/html", temp );
}





void handleNotFound() {
  //digitalWrite(led, 1);
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i = 0; i < server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
  //digitalWrite(led, 0);
}

void loop()
{
 
  server.handleClient();

  updateTime();


  if ((dateTime.minute >= nextMinTx) && (dateTime.second == 0)) {
    updateServer();
    previousMillis = millis();
  }

  if (Serial.available() > 0) {
    car= Serial.read();
    if (car == 'm') {
      while (Serial.read() != '\n') {};
      configMenu();
      ntp();
    }
    if (car == 'f') {
      while (Serial.read() != '\n') {};
      updateServer();
    }
  }
}


void updateTime()
{
  currentMillis = millis();
  EcratMillis = currentMillis - previousMillis;
  if (EcratMillis > 1000) {
    previousMillis = currentMillis;
    dateTime.second = (dateTime.second + 1) % 60;
    char currentTime[10];
    sprintf(currentTime, "%02d:%02d:%02d", dateTime.hour, dateTime.minute, dateTime.second);
    Serial.println(currentTime);
    if (dateTime.second == 0) {
      dateTime.minute = (dateTime.minute + 1) % 60;
      if (dateTime.minute == 0) {
        dateTime.hour = (dateTime.hour + 1) % 24;
      }
    }
  }
}


void updateServer()
{
  if (WiFi.status() == WL_CONNECTED)
  {
    printBme();
    ntp();
    connexion();
  }
  if (station.logger == 1) {
    if (SPIFFS.exists("/logger.txt") == 1) {
      String s;
      //long sizefile;
      File f = SPIFFS.open("/logger.txt", "a");
      if (!f) {
        Serial.println("file open failed");
      }
      Serial.println("====== add data logger =========");
      char buffer[50];
      sprintf(buffer, "%02d/%02d/%04d;", dateTime.day, dateTime.month, dateTime.year);
      f.print(buffer);
      sprintf(buffer, "%02d:%02d:%02d;", dateTime.hour, dateTime.minute, dateTime.second);
      f.print(buffer);
      sprintf(buffer, "%03d;%02d;%05d\n", wx.temperatureF / 10, wx.humidite, wx.pression / 10);
      f.print(buffer);
      f.close();
    }
  }
}

/*
 connexion ntp et acces au serveur meto pour uploder les donnees
 */

/*
 * The structure contains following fields:
 * struct strDateTime
{
  byte hour;
  byte minute;
  byte second;
  int year;
  byte month;
  byte day;
  byte dayofWeek;
  boolean valid;
};
 */
void ntp()
{
  if (WiFi.status() == WL_CONNECTED)
  {
    // first parameter: Time zone in floating point (for India); second parameter: 1 for European summer time; 2 for US daylight saving time (not implemented yet)
    dateTime = NTPch.getNTPtime(0.0, 1);
    NTPch.printDateTime(dateTime);

    nextMinTx = (dateTime.minute + station.transmitDelay) % 60;
    Serial.print("->>>>> next tx at : ");
    char buffer[20];
    sprintf(buffer, "%02d:%02d:%02d", dateTime.hour, nextMinTx, 0);
    Serial.println(buffer);
  }
}


void connexion()
{
  char login[60];
  char sentence[150];

  sprintf(login, "user %s pass -1 vers VERSION ESP8266", station.callsign);
  sprintf(sentence, "%s>APRS,TCPXX*:@%02d%02d%02dz%s/%s_.../...g...t%03dr...p...P...h%02db%05d", station.callsign, dateTime.hour, dateTime.minute, dateTime.second, station.latitude, station.longitude, wx.temperatureF / 10, wx.humidite, wx.pression / 10);
  Serial.println(sentence);
  WiFiClient client;

  if (!client.connect(station.clientAdress, station.clientPort)) {
    Serial.println("connection failed");
    return;
  }
  client.println(login);


  unsigned long timeout = millis();
  while (client.available() == 0) {
    if (millis() - timeout > 5000) {
      Serial.println(">>> Client Timeout !");
      client.stop();
      return;
    }
  }
  while (client.available()) {
    String line = client.readStringUntil('\r');
    Serial.print(line);
  }
  client.println(sentence);

  Serial.println();
  Serial.println("closing connection");

  client.stop();

}


void initBme()
{
  //***Driver settings********************************//
  //commInterface can be I2C_MODE or SPI_MODE
  //specify chipSelectPin using arduino pin names
  //specify I2C address.  Can be 0x77(default) or 0x76

  //For I2C, enable the following and disable the SPI section
  mySensor.settings.commInterface = I2C_MODE;
  mySensor.settings.I2CAddress = 0x77;

  //For SPI enable the following and dissable the I2C section
  //mySensor.settings.commInterface = SPI_MODE;
  //mySensor.settings.chipSelectPin = 10;


  //***Operation settings*****************************//

  //renMode can be:
  //  0, Sleep mode
  //  1 or 2, Forced mode
  //  3, Normal mode
  mySensor.settings.runMode = 3; //Normal mode

  //tStandby can be:
  //  0, 0.5ms
  //  1, 62.5ms
  //  2, 125ms
  //  3, 250ms
  //  4, 500ms
  //  5, 1000ms
  //  6, 10ms
  //  7, 20ms
  mySensor.settings.tStandby = 0;

  //filter can be off or number of FIR coefficients to use:
  //  0, filter off
  //  1, coefficients = 2
  //  2, coefficients = 4
  //  3, coefficients = 8
  //  4, coefficients = 16
  mySensor.settings.filter = 0;

  //tempOverSample can be:
  //  0, skipped
  //  1 through 5, oversampling *1, *2, *4, *8, *16 respectively
  mySensor.settings.tempOverSample = 1;

  //pressOverSample can be:
  //  0, skipped
  //  1 through 5, oversampling *1, *2, *4, *8, *16 respectively
  mySensor.settings.pressOverSample = 1;

  //humidOverSample can be:
  //  0, skipped
  //  1 through 5, oversampling *1, *2, *4, *8, *16 respectively
  mySensor.settings.humidOverSample = 1;


  Serial.print("Program Started\n");
  Serial.print("Starting BME280... result of .begin(): 0x");

  //Calling .begin() causes the settings to be loaded
  delay(10);  //Make sure sensor had enough time to turn on. BME280 requires 2ms to start up.
  Serial.println(mySensor.begin(), HEX);

  Serial.print("Displaying ID, reset and ctrl regs\n");

  Serial.print("ID(0xD0): 0x");
  Serial.println(mySensor.readRegister(BME280_CHIP_ID_REG), HEX);
  Serial.print("Reset register(0xE0): 0x");
  Serial.println(mySensor.readRegister(BME280_RST_REG), HEX);
  Serial.print("ctrl_meas(0xF4): 0x");
  Serial.println(mySensor.readRegister(BME280_CTRL_MEAS_REG), HEX);
  Serial.print("ctrl_hum(0xF2): 0x");
  Serial.println(mySensor.readRegister(BME280_CTRL_HUMIDITY_REG), HEX);

  Serial.print("\n\n");

  Serial.print("Displaying all regs\n");
  uint8_t memCounter = 0x80;
  uint8_t tempReadData;
  for (int rowi = 8; rowi < 16; rowi++ )
  {
    Serial.print("0x");
    Serial.print(rowi, HEX);
    Serial.print("0:");
    for (int coli = 0; coli < 16; coli++ )
    {
      tempReadData = mySensor.readRegister(memCounter);
      Serial.print((tempReadData >> 4) & 0x0F, HEX);//Print first hex nibble
      Serial.print(tempReadData & 0x0F, HEX);//Print second hex nibble
      Serial.print(" ");
      memCounter++;
    }
    Serial.print("\n");
  }


  Serial.print("\n\n");

  Serial.print("Displaying concatenated calibration words\n");
  Serial.print("dig_T1, uint16: ");
  Serial.println(mySensor.calibration.dig_T1);
  Serial.print("dig_T2, int16: ");
  Serial.println(mySensor.calibration.dig_T2);
  Serial.print("dig_T3, int16: ");
  Serial.println(mySensor.calibration.dig_T3);

  Serial.print("dig_P1, uint16: ");
  Serial.println(mySensor.calibration.dig_P1);
  Serial.print("dig_P2, int16: ");
  Serial.println(mySensor.calibration.dig_P2);
  Serial.print("dig_P3, int16: ");
  Serial.println(mySensor.calibration.dig_P3);
  Serial.print("dig_P4, int16: ");
  Serial.println(mySensor.calibration.dig_P4);
  Serial.print("dig_P5, int16: ");
  Serial.println(mySensor.calibration.dig_P5);
  Serial.print("dig_P6, int16: ");
  Serial.println(mySensor.calibration.dig_P6);
  Serial.print("dig_P7, int16: ");
  Serial.println(mySensor.calibration.dig_P7);
  Serial.print("dig_P8, int16: ");
  Serial.println(mySensor.calibration.dig_P8);
  Serial.print("dig_P9, int16: ");
  Serial.println(mySensor.calibration.dig_P9);

  Serial.print("dig_H1, uint8: ");
  Serial.println(mySensor.calibration.dig_H1);
  Serial.print("dig_H2, int16: ");
  Serial.println(mySensor.calibration.dig_H2);
  Serial.print("dig_H3, uint8: ");
  Serial.println(mySensor.calibration.dig_H3);
  Serial.print("dig_H4, int16: ");
  Serial.println(mySensor.calibration.dig_H4);
  Serial.print("dig_H5, int16: ");
  Serial.println(mySensor.calibration.dig_H5);
  Serial.print("dig_H6, uint8: ");
  Serial.println(mySensor.calibration.dig_H6);

  Serial.println();

}


void printBme()
{
  wx.temperatureC = (int) (mySensor.readTempC() * 10);
  wx.temperatureF = (int) (mySensor.readTempF() * 10);
  wx.pression = (int) mySensor.readFloatPressure();
  wx.humidite = (int) mySensor.readFloatHumidity();


  Serial.print("Temperature: ");
  Serial.print(mySensor.readTempC(), 2);
  Serial.println(" degrees C");

  Serial.print("Temperature: ");
  Serial.print(mySensor.readTempF(), 2);
  Serial.println(" degrees F");

  Serial.print("Pressure: ");
  Serial.print(mySensor.readFloatPressure(), 2);
  Serial.println(" Pa");

  Serial.print("%RH: ");
  Serial.print(mySensor.readFloatHumidity(), 2);
  Serial.println(" %");

  Serial.println();


}

/****************************************************************************
 * menu
 * */


byte detectMenu()
{
  long previousMillisSerial = 0;
  long currentMillisSerial;
  long EcratMillisSerial;
  int countDown = 0;
  Serial.println(F("m for boot menu"));
  previousMillisSerial = millis();
  do {
    currentMillisSerial = millis();
    EcratMillisSerial = currentMillisSerial - previousMillisSerial;
    if (Serial.available() > 0) {
      if (Serial.read() == 'm') {
        while (Serial.read() != '\n') {};
        return 1;
      }
    }
    if ((EcratMillisSerial / 1000) != countDown) {
      countDown++;
      Serial.write(countDown + 0x30);
    }
  }
  while (EcratMillisSerial < 5000);
  Serial.println();
  return 0;
}

void configMenu()
{
  char carMenu;
  do {
    carMenu = 0;
    Serial.println(F("-----------"));
    Serial.println(F("Config menu"));
    Serial.println(F("0 Quit menu"));
    Serial.println(F("1 format file system"));
    Serial.println(F("2 config wifi access point"));
    Serial.println(F("3 config weather station"));
    Serial.println(F("4 test ntp"));
    Serial.println(F("5 test bme 280"));
    Serial.println(F("6 test server upload"));
    Serial.println(F("7 print weather data logger (historic)"));
    Serial.println(F("8 create and erase weather data logger"));
    Serial.println(F("-----------"));
    carMenu = readCarMenu();
    switch (carMenu) {
      case '1' :
        Serial.println("Please wait 30 secs for SPIFFS to be formatted");
        SPIFFS.format();
        Serial.println("Spiffs formatted");
        break;
      case '2' : configAcessPoint();
        break;
      case '3' : configWeather();
        break;
      case '4' : ssidConnect(); ntp(); //prévoir un test de connexion
        break;
      case '5' : initBme(); printBme();
        break;
      case '6' : initBme(); printBme();  ssidConnect(); ntp(); connexion();
        break;
      case '7' : showlogger();
        break;
      case '8' : createEraselogger();
        break;
      case '0' :
        break;
      default : Serial.println(F("error"));
    }
  } while (carMenu != '0');
}

void configAcessPoint()
{
  if (SPIFFS.exists("/ssid.txt") == 1) {
    readSsidFile();
  }
  else
  {
    Serial.println(F("no ssid config file"));
  }
  char carMenu;
  do {
    carMenu = 0;
    Serial.println(F("-----------"));
    Serial.println(F("Config wifi access point menu"));
    Serial.println(F("0 Save and exit acess point menu"));
    Serial.println(F("1 ssid list"));
    Serial.println(F("2 set ssid"));
    Serial.println(F("3 set ssid password"));
    Serial.println(F("4 show ssid config"));
    Serial.println(F("5 test ssid"));
    Serial.println(F("-----------"));
    carMenu = readCarMenu();
    switch (carMenu) {
      case '1' :
        wifiScan();
        break;
      case '2' :
        Serial.println(F("type your ssid"));
        readCharArray(internet.ssid);
        break;
      case '3' :
        Serial.println(F("type your password"));
        readCharArray(internet.password);
        break;
      case '4' :
        Serial.println(F("your wifi ssid config is"));
        Serial.println(internet.ssid);
        Serial.println(internet.password);
        break;
      case '5' :
        Serial.println(F("test ssid internet access"));
        ssidConnect();
        break;
      default : Serial.println(F("error"));
    }
  } while (carMenu != '0');
  writeSsidFile();
}

void configWeather()
{
  if (SPIFFS.exists("/station.txt") == 1) {
    readStationFile();
  }
  else
  {
    Serial.println(F("no station config file"));
  }
  char carMenu;
  char buffer[10];
  do {
    carMenu = 0;
    Serial.println(F("-----------"));
    Serial.println(F("Config weather station"));
    Serial.println(F("0 Save and exit weather station menu"));
    Serial.println(F("1 set callsign station"));
    Serial.println(F("2 set longitude"));
    Serial.println(F("3 set latitude"));
    Serial.println(F("4 set server address"));
    Serial.println(F("5 set server port"));
    Serial.println(F("6 set transmit delay"));
    Serial.println(F("7 logger enable"));
    Serial.println(F("8 show weather config"));
    Serial.println(F("-----------"));
    carMenu = readCarMenu();
    switch (carMenu) {
      case '1' :
        Serial.println(F("type your callsign station ex: FWxxxx"));
        readCharArray(station.callsign);
        break;
      case '2' :
        Serial.println(F("type your longitude ex: 00012.21E"));
        readCharArray(station.longitude);
        break;
      case '3' :
        Serial.println(F("type your latitude ex: 4759.75N"));
        readCharArray(station.latitude);
        break;
      case '4' :
        Serial.println(F("type your server address, default : cwop.aprs.net"));
        readCharArray(station.clientAdress);
        break;
      case '5' :
        Serial.println(F("type your server port, default : 14580"));
        readCharArray(buffer);
        station.clientPort = atoi(buffer);
        break;
      case '6' :
        Serial.println(F("type transmit delay, default 10 minutes"));
        readCharArray(buffer);
        station.transmitDelay = atoi(buffer);
        break;
      case '7' :
        Serial.println(F("logger enable 0/1, defaut 0"));
        readCharArray(buffer);
        station.logger = atoi(buffer);
        break;
      case '8' :
        Serial.print(F("callsign : "));
        Serial.println(station.callsign);
        Serial.print(F("longitude : "));
        Serial.println(station.longitude);
        Serial.print(F("latitude : "));
        Serial.println(station.latitude);
        Serial.print(F("server address : "));
        Serial.println(station.clientAdress);
        Serial.print(F("server port : "));
        Serial.println(station.clientPort);
        Serial.print(F("tx delay : "));
        Serial.println(station.transmitDelay);
        Serial.print(F("logger enable : "));
        Serial.println(station.logger);
        break;
      case '0' :
        break;
      default : Serial.println(F("error"));
    }
  } while (carMenu != '0');
  writeStationFile();
}

void readCharArray(char *buffer)
{
  char car;
  int ptr = 0;
  do
  {
    if (Serial.available() > 0) {
      car = Serial.read();
      if (car != '\n') {
        buffer[ptr++] = car;
      }
    }
  }
  while (car != '\n');
  buffer[ptr] = 0;
}

char readCarMenu()
{
  char car = 0;
  char ret = 0;
  while (car != '\n')
  {
    if (Serial.available() > 0) {
      car = Serial.read();
      if ((car >= '0') && (car <= '9')) {
        ret = car;
      }
    }
  }
  return ret;
}

void wifiScan()
{
  Serial.println(F("scan start"));
  // WiFi.scanNetworks will return the number of networks found
  int n = WiFi.scanNetworks();
  Serial.println(F("scan done"));
  if (n == 0)
    Serial.println(F("no networks found"));
  else
  {
    Serial.print(n);
    Serial.println(F(" networks found"));
    for (int i = 0; i < n; ++i)
    {
      // Print SSID and RSSI for each network found
      Serial.print(i + 1);
      Serial.print(F(": "));
      Serial.print(WiFi.SSID(i));
      Serial.print(F(" ("));
      Serial.print(WiFi.RSSI(i));
      Serial.print(F(")"));
      Serial.println((WiFi.encryptionType(i) == ENC_TYPE_NONE) ? " " : "*");
      delay(10);
    }
  }
  Serial.println("");
}

void writeSsidFile()
{
  File f = SPIFFS.open("/ssid.txt", "w+");
  if (!f) {
    Serial.println(F("file open failed"));
    return;
  }
  Serial.println(F("====== Writing to ssid.txt file ========="));
  for (int i = 0; i < sizeof(configStruct); i++) {
    f.write(*((char*)&internet + i));
  }
  f.close();
  return;
}


void readSsidFile()
{
  File f = SPIFFS.open("/ssid.txt", "r+");
  if (!f) {
    Serial.println(F("file open failed"));
    return;
  }
  Serial.println(F("====== Reading ssid.txt file ========="));
  for (int i = 0; i < sizeof(configStruct); i++) {
    *((char*)&internet + i) = f.read();
  }
  f.close();
  return;
}

void writeStationFile()
{
  File f = SPIFFS.open("/station.txt", "w+");
  if (!f) {
    Serial.println(F("file open failed"));
    return;
  }
  Serial.println(F("====== Writing to station.txt file ========="));
  for (int i = 0; i < sizeof(positionStruct); i++) {
    f.write(*((char*)&station + i));
  }
  f.close();
  return;
}

void readStationFile()
{
  File f = SPIFFS.open("/station.txt", "r+");
  if (!f) {
    Serial.println(F("file open failed"));
    return;
  }
  Serial.println(F("====== Reading station.txt file ========="));
  for (int i = 0; i < sizeof(positionStruct); i++) {
    *((char*)&station + i) = f.read();
  }
  f.close();
  return;
}


void createEraselogger()
{
  File f = SPIFFS.open("/logger.txt", "w");
  if (!f) {
    Serial.println("file open failed");
  }
  Serial.println("====== new logger file =========");
  f.println("date;time;temperature;humidity;pressure");
  f.close();
}


void showlogger()
{
  if (SPIFFS.exists("/logger.txt") == 1) {
    String s;
    //long sizefile;
    File f = SPIFFS.open("/logger.txt", "r");
    if (!f) {
      Serial.println("file open failed");
    }
    //sizefile=f.size()-42;
    Serial.println("====== read logger file =========");
    do {
      s = f.readStringUntil('\n');
      Serial.println(s);
    }
    while (s.length() > 0);
    f.close();
  }
}




void ssidConnect()
{
  Serial.println(internet.ssid);
  Serial.println(internet.password);
  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.print(F("Connecting to "));
    Serial.println(internet.ssid);
    WiFi.mode(WIFI_AP_STA);
    //  WiFi.mode(WIFI_STA);
    WiFi.begin(internet.ssid, internet.password);
    Serial.println();
    // Wait for connection
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(F("."));
    }
  }
  Serial.println();
  Serial.print(F("Connected to "));
  Serial.println(internet.ssid);
  Serial.print(F("IP address: "));
  Serial.println(WiFi.localIP());
}

