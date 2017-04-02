/*
 * Weather Station Slave
 * PaulRB
 * March 2016
 * For ATTiny85 @1MHz, BOD disabled
 * ATTiny core used: https://github.com/SpenceKonde/ATTinyCore
 * Current consumption ~0.5mA
 */

#include <avr/sleep.h>

#define WIND_DIR_SENSOR   A3
#define RAIN_SENSOR  4
#define WIND_SPEED_SENSOR   1
#define SENSOR_ENABLE  2
#define SERIAL_OUT 0

#define WIND_GUST_PERIOD 6000UL
#define COMPASS_DIRECTIONS 16

//long windDirTot = 0;
//long windDirCount = 0;
long windDirTot[COMPASS_DIRECTIONS];
unsigned int maxWindGustCount = 0;
unsigned int windSpeedCount = 0;
unsigned int rainCount = 0;

//long windDirPrev = 0;
unsigned int windGustCount = 0;
byte windSpeedEdgeCount = 0;
int prevWindSpeedSensor = LOW;
int prevRainSensor = LOW;

unsigned long lastWindGustTime;
unsigned long lastReportTime;

const int reading[COMPASS_DIRECTIONS] = {147, 172, 208, 281, 368, 437, 531, 624, 710, 780, 817, 870, 909, 938, 970, 1023};
const int compass[COMPASS_DIRECTIONS] = {112,  67,  90, 157, 135, 202, 180,  22,  45, 247, 225, 337,   0, 292, 315,  270};

void setup() {

  // Enable various power-savings
  bitSet(PRR, PRTIM1); // power-off Timer 1
  bitSet(PRR, PRADC);  // power-off ADC
  bitSet(PRR, PRUSI);  // disable USI
  bitClear(ADCSRA, ADEN); // Disable ADC
  set_sleep_mode(SLEEP_MODE_IDLE);

  //pinMode(SERIAL_OUT, INPUT_PULLUP); // not needed, Wemos D3 has built-in pull-up
}

void loop() {

  unsigned long timeNow = millis();

  // Check for request from master
  if (digitalRead(SERIAL_OUT) == LOW) {

    // Time the length of the low pulse
    unsigned long startTime = millis();
    while (digitalRead(SERIAL_OUT) == LOW);
    unsigned long endTime = millis();

    // Was the low pulse long enough?
    if (endTime - startTime >= 8) {

      //Check that the line is high & stable
      startTime = endTime;
      endTime += 10;
      while (digitalRead(SERIAL_OUT) == HIGH && endTime >= millis());

      // Has line been high & stable ?
      if (millis() > endTime) {

        // Find most common wind direction over the reporting period
        long maxWindDirTot = 0;
        byte maxWindDirIndex = 0;
        for (byte i = 0; i < COMPASS_DIRECTIONS; i++) {
          if (windDirTot[i] > maxWindDirTot) {
            maxWindDirTot = windDirTot[i];
            maxWindDirIndex = i;
          }
          //Reset for next reporting period
          windDirTot[i] = 0;
        }
        int windDir = compass[maxWindDirIndex];

        // Calculate average wind direction over the reporting period
        //int windDir;
        //if (windDirCount > 0) windDir = windDirTot / windDirCount; else windDir = 0;
        //while (windDir >= 360) windDir -= 360;
        //while (windDir < 0) windDir += 360;
  
        // Send all sensor data to master
        pinMode(SERIAL_OUT, OUTPUT);
        Serial.begin(9600);
        Serial.write(highByte(windDir));
        Serial.write(lowByte(windDir));
        Serial.write(highByte(windSpeedCount));
        Serial.write(lowByte(windSpeedCount));
        Serial.write(highByte(maxWindGustCount));
        Serial.write(lowByte(maxWindGustCount));
        Serial.write(highByte(rainCount));
        Serial.write(lowByte(rainCount));
        Serial.flush();
        Serial.end();
        pinMode(SERIAL_OUT, INPUT); // INPUT_PULLUP not needed because Wemos D3 has built-in pullup
  
        // Zero counters for next reporting period
        //windDirTot = 0;
        //windDirCount = 0;
        windSpeedCount = 0;
        maxWindGustCount = 0;
        rainCount = 0;
      }
    }
  }

  // Check Wind speed sensor
  pinMode(WIND_SPEED_SENSOR, INPUT_PULLUP);
  int currWindSpeedSensor = digitalRead(WIND_SPEED_SENSOR);
  pinMode(WIND_SPEED_SENSOR, INPUT);
  if (currWindSpeedSensor != prevWindSpeedSensor) {
    windSpeedEdgeCount++;
    if (windSpeedEdgeCount > 1) {
      windSpeedEdgeCount = 0;
      windSpeedCount++;
    }
    windGustCount++;
    prevWindSpeedSensor = currWindSpeedSensor;

    // Now check the wind direction sensor
    // Enable sensor only to take reading. Reduces current
    bitClear(PRR, PRADC); // power-on ADC
    bitSet(ADCSRA, ADEN); // Enable ADC
    pinMode(SENSOR_ENABLE, OUTPUT);
    digitalWrite(SENSOR_ENABLE, HIGH);
    int readingNow = analogRead(WIND_DIR_SENSOR);
    pinMode(SENSOR_ENABLE, INPUT);
    bitClear(ADCSRA, ADEN); // Disable ADC
    bitSet(PRR, PRADC);  // power-off ADC
    // Translate analog reading into compass direction
    // Can be one of 16 values, but there could be some
    // analog noise, so the reading[] array contains values
    // mid-way between the expected readings.
    byte i;
    for (i = 0; i < COMPASS_DIRECTIONS && readingNow >= reading[i]; i++);
    windDirTot[i]++;
    //int windDirNow = compass[i];
    // Check if sensor has swept through 0 degrees, moving in either direction
    //if (windDirNow - windDirPrev > 180) windDirNow -= 360;
    //if (windDirPrev - windDirNow > 180) windDirNow += 360;
    // Update total and count of data points for calculating average
    //windDirTot += windDirNow;
    //windDirCount++;
    //windDirPrev = windDirNow;
  }

  // Check rain sensor
  pinMode(RAIN_SENSOR, INPUT_PULLUP);
  int currRainSensor = digitalRead(RAIN_SENSOR);
  pinMode(RAIN_SENSOR, INPUT);
  if (currRainSensor != prevRainSensor) {
    if (prevRainSensor == HIGH) rainCount++;
    prevRainSensor = currRainSensor;
  }

  // Time to check for wind gust?
  unsigned long windGustPeriod = timeNow - lastWindGustTime;
  if (windGustPeriod >= WIND_GUST_PERIOD) {
    lastWindGustTime += WIND_GUST_PERIOD;
    // New highest wind gust?
    if (windGustCount > maxWindGustCount) maxWindGustCount = windGustCount;
    // Zero gust count for next period
    windGustCount = 0;
  }

  // Go to sleep until next 1ms timer interrupt
  sleep_enable();
  sleep_mode();
  sleep_disable();

}



