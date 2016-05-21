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
#include <Wire.h>
#include <AM2320.h>

#define WIND_DIR_SENSOR D5
#define WIND_SPEED_SENSOR D6
#define RAIN_SENSOR D7
#define BATT_LEVEL A0

#define SERVER_UPDATE_PERIOD 900000UL
#define WIND_GUST_PERIOD 6000UL
#define WIND_DIR_PERIOD 250UL
#define SENSOR_UPDATE_PERIOD 5UL

// Use WiFiClient class to create TCP connections
WiFiClient wsClient;

// Use Tickers to schedule various tasks
Ticker windDirTicker;
Ticker windGustTicker;
Ticker sensorTicker;

// AM2320 i2c temp & humidity sensor
AM2320 tempHumidSensor;

const char ssid[]     = "granary";
const char password[] = "sparkym00se";
const char host[]     = "www.databaseconnect.co.uk";

long windDirTot;
long windDirCount;
long windDirPrev;
long windDirNow;
long maxWindGustCount;

long windSpeedCount = 0;
long windGustCount = 0;
long rainCount = 0;

int lastWindSpeedSensor = HIGH;
int lastRainSensor = HIGH;

const int reading[] = { 45,  53,  64, 88, 116, 138, 168, 198, 225, 248, 260, 276, 289, 298, 309,  320};
const int compass[] = {292, 247, 270, 337, 315, 22, 0, 202, 225, 67, 45, 157, 180, 112, 135, 90, -999};
const double e = 2.71828;

void checkSensors() {

  // The wind speed sensor contains a magnet which rotates with the anemometer cups
  // The magnet activates a reed switch which opens and closes twice per rotation
  if (digitalRead(WIND_SPEED_SENSOR) != lastWindSpeedSensor) {
    windSpeedCount++;
    windGustCount++;
    lastWindSpeedSensor = !lastWindSpeedSensor;
  }

  // The rain sensor contains a magnet attached to a see-saw mechanism
  // with buckets on each side. As the see-saw tips, one bucket is emptied and
  // the other bucket begins to fill.
  // The magnet activates a reed switch as the see-saw tips.
  if (digitalRead(RAIN_SENSOR) != lastRainSensor) {
    if (lastRainSensor == HIGH) rainCount++;
    lastRainSensor = !lastRainSensor;
  }

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
  while (i <= 16 && readingNow >= reading[i]) i++;
  if (i <= 15) {
    windDirNow = compass[i];
    // Check if sensor has swept through 0 degrees, moving in either direction
    if (windDirNow - windDirPrev > 180L) windDirNow -= 360L;
    if (windDirPrev - windDirNow > 180L) windDirNow += 360L;
    // Update total and count of data points for calculating average
    windDirTot += windDirNow;
    windDirCount++;
    windDirPrev = windDirNow;
  }
  else {
    // The wind direction sensor is disconnected or faulty
    windDirCount = 0L;
  }

}

void updateServer() {

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
  Serial.print(absoluteHumidityNow);

  // Calculate average wind direction over the reporting period
  double windDir;
  if (windDirCount > 0L) {
    windDir = windDirTot / windDirCount;
    while (windDir >= 360.0) windDir -= 360.0;
    while (windDir < 0.0) windDir += 360.0;
  }
  else {
    // The wind direction sensor is disconnected or faulty
    windDir = -999.9;
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

  // Zero max wind gust & wind & rain counts for next reporting period
  Serial.println("Zeroing counts");
  maxWindGustCount = 0;
  windDirTot = 0;
  windDirCount = 0;
  windSpeedCount = 0;
  rainCount = 0;

}

void setup() {

  Serial.begin(115200);
  Wire.begin();
  Serial.println("Started.");

  WiFi.persistent(false);

  pinMode(WIND_SPEED_SENSOR, INPUT_PULLUP);
  pinMode(RAIN_SENSOR, INPUT_PULLUP);

  wifi_fpm_set_sleep_type(LIGHT_SLEEP_T); // Enable light sleep mode

}

void loop() {

  updateServer();

  Serial.println("Starting modem sleep");
  WiFi.forceSleepBegin();
  delay(100);

  // Set schedules for various tasks
  Serial.println("Starting scheduled tasks");
  windDirTicker.attach_ms(WIND_DIR_PERIOD, measureWindDir);
  windGustTicker.attach_ms(WIND_GUST_PERIOD, measureWindGust);
  sensorTicker.attach_ms(SENSOR_UPDATE_PERIOD, checkSensors);

  Serial.println("Waiting for server update period");
  delay(SERVER_UPDATE_PERIOD);

  Serial.println("Disabling scheduled tasks");
  windDirTicker.detach();
  windGustTicker.detach();
  sensorTicker.detach();

  Serial.println("Waking modem");
  WiFi.forceSleepWake();
  delay(100);

}
