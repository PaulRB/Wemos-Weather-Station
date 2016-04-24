/*
 * Wemos D1 mini Weather Station
 * PaulRB
 * April 2016
 */

extern "C" {
#include "user_interface.h"
}

#include <ESP8266WiFi.h>
#include <Ticker.h>

#define WIND_DIR_SENSOR D5
#define WIND_SPEED_SENSOR D6
#define RAIN_SENSOR D7
#define BATT_LEVEL A0

#define SERVER_UPDATE_PERIOD 30000UL
#define WIND_GUST_PERIOD 6000UL
#define WIND_DIR_PERIOD 1000UL

// Use WiFiClient class to create TCP connections
WiFiClient wsClient;

// Use Tickers to schedule various tasks
Ticker windDirTicker;
Ticker windGustTicker;
Ticker updateServerTicker;

const char ssid[]     = "ssid";
const char password[] = "password";
const char host[]     = "www.hostname.co.uk";

long windDirTot;
long windDirCount;
long windDirPrev;
long windDirNow;
long maxWindGustCount;

volatile long windSpeedCount = 0;
volatile long windGustCount = 0;
volatile long rainCount = 0;

const int reading[] = { 46, 54, 66, 90, 119, 142, 173, 203, 231, 254, 267, 284, 297, 307, 317, 328};
const int compass[] = {292, 247, 270, 337, 315, 22,  0, 202, 225, 67, 45, 157, 180, 112, 135, 90};

void windSpeedInterrupt() {

  // The wind speed sensor contains a magnet which rotates with the anemometer cups
  // The magnet activates a reed switch which opens and closes twice per rotation
  windSpeedCount++;
  windGustCount++;
}

void rainInterrupt() {

  // The rain sensor contains a magnet attached to a see-saw mechanism
  // with buckets on each side. As the see-saw tips, one bucket is emptied and
  // the other bucket begins to fill.
  // The magnet activates a reed switch as the see-saw tips.
  rainCount++;
}

void measureWindGust() {

  // The wind gust speed is the highest speed, recorded over a 6s period,
  // during the reporting period.

  // New highest wind gust?
  if (windGustCount > maxWindGustCount) maxWindGustCount = windGustCount;
  // Zero gust count for next period
  windGustCount = 0;

}

void measureWindDir() {

  // Measure wind direction.
  // The sensor contains a magnet which rotates with the vane.
  // The magnet activates one of 8 reed switches, or sometimes two adjacent reed switches.
  // Each reed switch has a different resistor attached.
  // So there are 16 possible resistances indicating 16 compass points.
  // To measure this resistance, a crude ADC circuit is used.
  // A 0.1uF cap is charged up by a digital output. It is then
  // allowed to discharge through the wind sensor with a 4K7 resistor
  // in parrallel. The discharge time indicates the resistance and so the wind direction.

  pinMode(WIND_DIR_SENSOR, OUTPUT);
  //Charge the cap attached to the wind dir sensor
  digitalWrite(WIND_DIR_SENSOR, HIGH);
  delayMicroseconds(100);
  //Stop charging the cap and measure time taken to discharge
  pinMode(WIND_DIR_SENSOR, INPUT);
  unsigned long readingStart = micros();
  while (digitalRead(WIND_DIR_SENSOR) != LOW); // should take no longer than ~350us
  unsigned long readingNow = micros() - readingStart;

  // Translate discharge time into compass direction
  // Can only be one of 16 values, but there will be some
  // noise, so the reading[] array contains values
  // mid-way between the expected values.
  int i = 0;
  while (i <= 15 && readingNow >= reading[i]) i++;
  if (i <= 15) {
    windDirNow = compass[i];
    // Check if sensor has swept through 0 degrees, moving in either direction
    if (windDirNow - windDirPrev > 180) windDirNow -= 360;
    if (windDirPrev - windDirNow > 180) windDirNow += 360;
    // Update total and count of data points for calculating average
    windDirTot += windDirNow;
    windDirCount++;
    windDirPrev = windDirNow;
  }
  else {
    // The wind direction sensor is disconnected or faulty
    windDirCount = 0;
  }
}

void updateServer() {

  Serial.println();
  Serial.println("Calculating Sensor Values");

  double battLevelNow = analogRead(BATT_LEVEL) / 209.66; // assumes external 180K resistor

  Serial.print("batt=");
  Serial.print(battLevelNow);

  // Calculate average wind direction over the reporting period
  double windDir;
  if (windDirCount > 0) {
    windDir = windDirTot / windDirCount;
    while (windDir >= 360) windDir -= 360;
    while (windDir < 0) windDir += 360;
  }
  else {
    // The wind direction sensor is disconnected or faulty
    windDir = -999;
  }
  Serial.print(" windDir=");
  Serial.print(windDir);

  // Calcualate average wind speed, in Km/hr, over reporting period
  // Note: 1 rotation per second = 2.4 Km/hr
  // 4 changes per rotation, so 1 change per millisecond = 600 Km/hr
  double windSpeed = 600.0 * windSpeedCount / SERVER_UPDATE_PERIOD;
  Serial.print(" windSpeed=");
  Serial.print(windSpeed);

  // Calculate wind gust speed, in Km/hr, over period of 6s
  // Note: 1 rotation per second = 2.4 Km/hr
  // 4 changes per rotation, so 1 change per millisecond = 600 Km/hr
  double windGustSpeed = 600.0 * maxWindGustCount / WIND_GUST_PERIOD;
  Serial.print(" maxWindGust=");
  Serial.print(windGustSpeed);

  // Report rainfall rate in mm/hr
  // One counts = 0.011" of rain
  // One count per millisecond = 0.011 * 25.4 * 1000 * 60 * 60 = 1005840mm/hr
  double rainRate = 1005840.0 * rainCount / SERVER_UPDATE_PERIOD;
  Serial.print(" rainRate=");
  Serial.print(rainRate);

  Serial.println();

  //WiFi.forceSleepWake();

  Serial.println();
  Serial.print("Connecting to ");
  Serial.print(ssid);

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
    Serial.print("WiFi connected, IP address: ");
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

      if (windDir >= 0 && windDir <= 360) {
        sensors += ",WD";
        values += ",";
        values += windDir;
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
      // Read all the lines of the reply from server and print them to Serial
      while (wsClient.available()) {
        String line = wsClient.readStringUntil('\r');
        Serial.print(line);
      }

      Serial.println();
      Serial.print("Closing connection. Total time: ");
      Serial.println( millis() - startTime);

    }

    //WiFi.disconnect();
    //WiFi.forceSleepBegin(30000000);
  }


  // Zero max wind gust & wind & rain counts for next reporting period
  maxWindGustCount = 0;
  windDirTot = 0;
  windDirCount = 0;
  windSpeedCount = 0;
  rainCount = 0;

}

void setup() {

  Serial.begin(115200);

  WiFi.persistent(false);

  // Set schedules for various tasks
  windDirTicker.attach_ms(WIND_DIR_PERIOD, measureWindDir);
  windGustTicker.attach_ms(WIND_GUST_PERIOD, measureWindGust);
  //updateServerTicker.attach_ms(SERVER_UPDATE_PERIOD, updateServer);

  pinMode(WIND_SPEED_SENSOR, INPUT_PULLUP);
  pinMode(RAIN_SENSOR, INPUT_PULLUP);

  attachInterrupt(WIND_SPEED_SENSOR, windSpeedInterrupt, CHANGE);
  attachInterrupt(RAIN_SENSOR, rainInterrupt, FALLING);

  wifi_fpm_set_sleep_type(LIGHT_SLEEP_T); // Enable light sleep mode,
  //WiFi.forceSleepBegin();
}

void loop() {
  updateServer();
  delay(SERVER_UPDATE_PERIOD);
}
