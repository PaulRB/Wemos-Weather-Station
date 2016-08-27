/*
 * Wemos D1 mini Weather Station v3
 * PaulRB
 * July 2016
 * Uses ATTiny85 as slave MCU to monitor wind/rain sensors
 * Commmunication with slave via soft serial from https://github.com/plerup/espsoftwareserial/blob/master/SoftwareSerial.h
 */

extern "C" {
#include "user_interface.h"
}

#include <ESP8266WiFi.h>
#include <Wire.h>
#include <AM2320.h>
#include <SoftwareSerial.h>

#define BATT_LEVEL A0
#define SERVER_UPDATE_PERIOD 900000UL
#define WIND_GUST_PERIOD 6000UL
#define SLAVE_RX D3
#define SLAVE_TX D4 //not used

// Use WiFiClient class to create TCP connections
WiFiClient wsClient;

// Serial connection with slave MCU
SoftwareSerial slaveMCU(SLAVE_RX, SLAVE_TX, false, 64);

// AM2320 i2c temp & humidity sensor
AM2320 tempHumidSensor;

const char ssid[]     = "SSID";
const char password[] = "password";
const char host[]     = "www.hostname.co.uk";

const double e = 2.71828;

int readSerialWord() {
  
  byte h = slaveMCU.read();
  byte l = slaveMCU.read();
  return word(h, l);
  
}

void setup() {

  Serial.begin(115200);
  Wire.begin();
  Serial.println("Started.");

  WiFi.persistent(false);
  wifi_fpm_set_sleep_type(LIGHT_SLEEP_T); // Enable light sleep mode

}

void loop() {

  Serial.println();
  Serial.println("Calculating Sensor Values");

  double battLevelNow = analogRead(BATT_LEVEL) / 209.66; // assumes external 180K resistor

  Serial.print("batt=");
  Serial.print(battLevelNow);

  //Read temp/humidity sensor
  double humidityNow, temperatureNow, absoluteHumidityNow;
  if (tempHumidSensor.Read() == 0) {
    humidityNow = tempHumidSensor.h;
    temperatureNow = tempHumidSensor.t;
    absoluteHumidityNow = (6.112 * pow(e, (17.67 * temperatureNow) / (temperatureNow + 243.5)) * humidityNow * 2.1674) / (273.15 + temperatureNow);
  }
  else {
    humidityNow = -999;
    temperatureNow = -999;
    absoluteHumidityNow = -999;
  }
  Serial.print(" temp=");
  Serial.print(temperatureNow);
  Serial.print(" humidity=");
  Serial.print(humidityNow);
  Serial.print(" Abs humidity=");
  Serial.println(absoluteHumidityNow);

  //Read Wind Speed/direction/Rain from slave MCU
  slaveMCU.flush();
  //Signal to the slave that it should transmit its readings, by pulling the serial line low.
  pinMode(SLAVE_RX, OUTPUT);
  digitalWrite(SLAVE_RX, LOW);
  delay(10);
  digitalWrite(SLAVE_RX, HIGH);
  pinMode(SLAVE_RX, INPUT);
  delay(10);
  slaveMCU.begin(9600);

  //Wait for all data to be received
  while (slaveMCU.available() < 10) delay(1);
  slaveMCU.read(); // not sure why, but 2 extra zero bytes seems to get transmitted. Discard them
  slaveMCU.read();
  //Get the readings
  int windDir = readSerialWord();
  int windSpeedCount = readSerialWord();
  int windGustCount = readSerialWord();
  int rainCount = readSerialWord();
  //slaveMCU.end();

  Serial.print("Slave Data: Dir=");
  Serial.print(windDir);
  Serial.print(" windSpeedCount=");
  Serial.print(windSpeedCount);
  Serial.print(" windGustCount=");
  Serial.print(windGustCount);
  Serial.print(" rainCount=");
  Serial.println(rainCount);

  // Calcualate average wind speed, in Km/hr, over reporting period
  // Note: 1 rotation per second = 2.4 Km/hr
  // 1 count per rotation, so 1 rotation per millisecond = 2400 Km/hr
  double windSpeed = 2400.0 * windSpeedCount / SERVER_UPDATE_PERIOD;
  Serial.print("windSpeed=");
  Serial.print(windSpeed);

  // Calculate wind gust speed, in Km/hr, over period of 6s
  // Note: 1 rotation per second = 2.4 Km/hr
  // 4 changes per rotation, so 1 change per millisecond = 600 Km/hr
  double windGustSpeed = 600.0 * windGustCount / WIND_GUST_PERIOD;
  Serial.print(" maxWindGust=");
  Serial.print(windGustSpeed);

  // Report rainfall rate in mm/hr
  // One counts = 0.011" of rain
  // One count per millisecond = 0.011 * 25.4 * 1000 * 60 * 60 = 1005840mm/hr
  double rainRate = 1005840.0 * rainCount / SERVER_UPDATE_PERIOD;
  Serial.print(" rainRate=");
  Serial.print(rainRate);

  Serial.println();
  Serial.print("Connecting to WiFi");

  unsigned long startTime = millis();
  WiFi.begin(ssid, password);
  WiFi.mode(WIFI_STA); // Station Mode
  WiFi.hostname("Weather Station");

  while (WiFi.status() != WL_CONNECTED && millis() - startTime < 30000UL) {
    delay(1000);
    Serial.print(".");
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println();
    Serial.println("WiFi connection failed");
  }
  else {

    Serial.println();
    Serial.print("WiFi connected, SSID: ");
    Serial.print(WiFi.SSID());
    Serial.print(" signal: ");
    Serial.print(WiFi.RSSI());
    Serial.print(" IP address: ");
    Serial.println(WiFi.localIP());

    Serial.println();
    Serial.print("connecting to ");
    Serial.println(host);

    const int httpPort = 80;
    if (!wsClient.connect(host, httpPort)) {
      Serial.println("Host connection failed");
    }
    else {

      String sensors, values;

      if (battLevelNow > 0 && battLevelNow < 20) {
        sensors = "BW";
        values = battLevelNow;
      }

      if (windDir >= 0.0 && windDir <= 360.0) {
        sensors += ",WD";
        values += ",";
        values += windDir;
      }

      if (temperatureNow >= -50 && temperatureNow <= 50) {
        sensors += ",TO";
        values += ",";
        values += temperatureNow;
      }

      if (humidityNow >= 0 && humidityNow <= 100) {
        sensors += ",HO";
        values += ",";
        values += humidityNow;
      }

      if (absoluteHumidityNow >= 0 && absoluteHumidityNow <= 100) {
        sensors += ",AO";
        values += ",";
        values += absoluteHumidityNow;
      }

      if (windSpeed >= 0 && windSpeed <= 200) {
        sensors += ",WS";
        values += ",";
        values += windSpeed;
      }

      if (windGustSpeed >= 0 && windGustSpeed <= 200) {
        sensors += ",WG";
        values += ",";
        values += windGustSpeed;
      }

      if (rainRate >= 0 && rainRate <= 1000) {
        sensors += ",RN";
        values += ",";
        values += rainRate;
      }

      // We now create a URI for the request
      String url = "/script.php";
      url += "?sensor=";
      url += sensors;
      url += "&value=";
      url += values;

      Serial.print("Requesting URL: ");
      Serial.println(url);

      // This will send the request to the server
      wsClient.print(String("GET ") + url + " HTTP/1.1\r\n" +
                     "Host: " + host + "\r\n" +
                     "Connection: close\r\n\r\n");

      while (!wsClient.available()) {
        if (millis() - startTime > 30000UL) {
          Serial.println("request failed");
          break;
        }
        delay(100);
      }

      Serial.println("Response from server:");
      Serial.println("----------------------------------------");
      // Read all the lines of the reply from server and print them to Serial
      while (wsClient.available()) {
        String line = wsClient.readStringUntil('\r');
        Serial.print(line);
      }

      Serial.println();
      Serial.println("----------------------------------------");
      Serial.print("Time taken: ");
      Serial.println( millis() - startTime);

    }
    Serial.println("Disconnecting Wifi");
    WiFi.disconnect();
  }

  Serial.println("Sleeping...!");
  ESP.deepSleep(SERVER_UPDATE_PERIOD * 1000); // 15 mins. requires ~1K between D0 --> RST connection to wake, wire connection prevents sketch upload!

}
