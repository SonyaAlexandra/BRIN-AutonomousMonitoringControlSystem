#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <EEPROM.h>
#include <OneWire.h>            // DS18B20
#include <DallasTemperature.h>  // DS18B20
#include "GravityTDS.h"         // TDS Sensor
#include "DFRobot_PH.h"         // PH Sensor
#include <ds3231.h>             // RTC

// Pin dan Sensor 
const int mosfetOnOffPin = 25;
Adafruit_BME280 bme;

// Variabel Sensor dan Pompa 
float temp, humidity;
bool pumpActive = false;
unsigned long pumpStartTime = 0;
unsigned long pumpDuration = 0;
unsigned long lastPoll = 0;
const unsigned long pollInterval = 3000;

//  Farm ID
const int farmID = 16;

// A Sensor
float waterTemp = 0.0;
float pHValue = 0.0;
float tdsValue = 0.0;
float oxygenValue = 0.0;

// === B Aktuator ===
bool mainTankPump = true;
bool PHUpPump = false;
bool PHDownPump = false;
bool mixAPump = false;
bool mixBPump = true;
bool valve = true;
bool mist = false; 
bool lv1 = false; 

// === Threshold Dummy ===
float thPHUp = 6.5;
float thPHDown = 5.5;
float thTDSUp = 1300;
float thTDSDown = 800;
float tempMin = 15.5;
float tempMax = 27.9;
float humMin = 82.0;
float humMax = 92.0;

//  Analog Pin Definitions for Arduino Mega 
#define PHSensorPin A2
#define TdsSensorPin A4
#define DOSensorPin A6
int airTempPin = 34;
int waterTempPin = 32;
int lv1Pin = 38;  // Up Level
int lv2Pin = 40;  // Low Level

// DS18B20
#define ONE_WIRE_BUS waterTempPin
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

// pH Sensor
int buf[10];
DFRobot_PH ph;
float PHVoltage = 0;

// TDS Sensor
GravityTDS gravityTds;
#define VREF 5.0            // 5V reference for Arduino Mega
#define SCOUNT 30
int analogBuffer[SCOUNT];
int analogBufferIndex = 0;

// DO Sensor
#define VREF_DO 5000        // 5V reference for Arduino Mega
#define ADC_RES 1024        // 10-bit ADC for Arduino Mega
#define TWO_POINT_CALIBRATION 0
#define READ_TEMP 28
#define CAL1_V 1900
#define CAL1_T 32
#define CAL2_V 1000
#define CAL2_T 14
const uint16_t DO_Table[41] = {
  14460, 14220, 13820, 13440, 13090, 12740, 12420, 12110, 11810, 11530,
  11260, 11010, 10770, 10530, 10300, 10080, 9860, 9660, 9460, 9270,
  9080, 8900, 8730, 8570, 8410, 8250, 8110, 7960, 7820, 7690,
  7560, 7430, 7300, 7180, 7070, 6950, 6840, 6730, 6630, 6530, 6410
};

//Timer Kirim Pesan
unsigned long lastMsgA = 0, lastMsgB = 0, lastMsgSP = 0;

//For RTC
char timeNow[30];
char TimeStamps[30];
struct ts t;
int secondSendA = 0;
int secondSendB = 0;
int secondSendSP = 0;
int secNow = 0;

// Function Declarations
void loadThresholdsFromEEPROM();
void saveThresholdsToEEPROM();
void getTime();
void getTimeStamps();
String getFormattedTime();
bool isPumpOn();
void turnPumpOn();
void turnPumpOff();
void updateMistStatus();
void handleSerialCommand();
void readWaterTemp();
void readPH();
void readTDS();
void readDO();
int16_t readDOValue(uint32_t voltage_mv, uint8_t temperature_c);
void readLevelSensor();

// RTC simulation (since we don't have WiFi)
unsigned long startTime = 0;

// RTC Functions
void getTime(){
  DS3231_get(&t);
  sprintf(timeNow, "%d/%d/%d %d:%d:%d",t.mday,t.mon,t.year,t.hour,t.min,t.sec);
}

void getTimeStamps() {
  DS3231_get(&t);
  sprintf(TimeStamps, "%d;%d;%d;%d;%d;%d", t.year, t.mon, t.mday, t.hour, t.min, t.sec);
}

// EEPROM Helpers
void loadThresholdsFromEEPROM() {
  EEPROM.get(0, tempMin);
  EEPROM.get(10, tempMax);
  EEPROM.get(20, humMin);
  EEPROM.get(30, humMax);
  if (isnan(tempMin)) tempMin = 15.5;
  if (isnan(tempMax)) tempMax = 27.9;
  if (isnan(humMin)) humMin = 82.0;
  if (isnan(humMax)) humMax = 92.0;
}

void saveThresholdsToEEPROM() {
  EEPROM.put(0, tempMin);
  EEPROM.put(10, tempMax);
  EEPROM.put(20, humMin);
  EEPROM.put(30, humMax);
}

// RTC timestamp function
String getFormattedTime() {
  DS3231_get(&t);
  char buffer[30];
  sprintf(buffer, "%02d/%02d/%04d %02d:%02d:%02d",
          t.mday, t.mon, t.year, t.hour, t.min, t.sec);
  return String(buffer);
}

//  Pompa 
bool isPumpOn() {
  return digitalRead(mosfetOnOffPin) == HIGH;
}

void turnPumpOn() {
  digitalWrite(mosfetOnOffPin, HIGH);
  if (!pumpActive) {
    pumpActive = true;
    mist = true;  // Update status mist saat pompa ON
    Serial.print("[PUMP] ON for ");
    Serial.print(pumpDuration / 1000);
    Serial.println(" seconds");
  }
}

void turnPumpOff() {
  digitalWrite(mosfetOnOffPin, LOW);
  if (pumpActive) {
    pumpActive = false;
    mist = false;  // Update status mist saat pompa OFF
    Serial.println("[PUMP] OFF");
  }
}

// update status mist
void updateMistStatus() {
  mist = isPumpOn();
}

//Serial Command Handler
void handleSerialCommand() {
  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    input.toUpperCase();

    if (input.startsWith("SET TEMP MIN ")) {
      float value = input.substring(13).toFloat();
      if (value > 0 && value < tempMax) {
        tempMin = value;
        saveThresholdsToEEPROM();
        Serial.print("[THRESHOLD] tempMin updated to ");
        Serial.println(tempMin);
      }
    } else if (input.startsWith("SET TEMP MAX ")) {
      float value = input.substring(13).toFloat();
      if (value > tempMin && value < 100) {
        tempMax = value;
        saveThresholdsToEEPROM();
        Serial.print("[THRESHOLD] tempMax updated to ");
        Serial.println(tempMax);
      }
    } else if (input.startsWith("SET HUM MIN ")) {
      float value = input.substring(12).toFloat();
      if (value > 0 && value < humMax) {
        humMin = value;
        saveThresholdsToEEPROM();
        Serial.print("[THRESHOLD] humMin updated to ");
        Serial.println(humMin);
      }
    } else if (input.startsWith("SET HUM MAX ")) {
      float value = input.substring(12).toFloat();
      if (value > humMin && value < 100) {
        humMax = value;
        saveThresholdsToEEPROM();
        Serial.print("[THRESHOLD] humMax updated to ");
        Serial.println(humMax);
      }
    } else {
      Serial.println("[INFO] Format perintah tidak dikenal");
    }
  }
}

// Sensor Reading Functions 
void readWaterTemp() {
  sensors.requestTemperatures();
  waterTemp = sensors.getTempCByIndex(0);
  if (waterTemp == DEVICE_DISCONNECTED_C) {
    waterTemp = 25.0; // Default value if sensor disconnected
  }
}

void readPH() {
  for (int i = 0; i < 10; i++) {
    buf[i] = analogRead(PHSensorPin);
    delay(10);
  }
  int avgValue = 0;
  for (int i = 2; i < 8; i++) {
    avgValue += buf[i];
  }
  PHVoltage = (float)avgValue * 5.0 / 1024.0 / 6;  // For Arduino Mega ADC
  pHValue = ph.readPH(PHVoltage, waterTemp);
  // Alternative calibration formula (uncomment if needed):
  // pHValue = (-0.0128 * PHVoltage) + 25.511;
}

void readTDS() {
  gravityTds.setTemperature(waterTemp);
  gravityTds.update();
  tdsValue = gravityTds.getTdsValue();
}

// Alternative TDS reading method (commented out)
/*
void readTDS() {
  int tdsADC = analogRead(TdsSensorPin);
  float tdsRaw = (float(tdsADC) / 1024.0) * 2000.0;  // For Arduino Mega
  tdsValue = (0.8081 * tdsRaw) + 39.094;  // Kalibrasi ID 10
}
*/

void readDO() {
  int adc = analogRead(DOSensorPin);
  uint16_t voltage_mv = adc * 5000 / 1024;  // Convert to mV for Arduino Mega
  int tempIdx = constrain((int)waterTemp, 0, 40);
  oxygenValue = (float)readDOValue(voltage_mv, tempIdx) / 1000.0;  // Convert to mg/L
}

int16_t readDOValue(uint32_t voltage_mv, uint8_t temperature_c) {
#if TWO_POINT_CALIBRATION == 0
  uint16_t V_saturation = (uint32_t)CAL1_V + (uint32_t)35 * temperature_c - (uint32_t)CAL1_T * 35;
  return (voltage_mv * DO_Table[temperature_c] / V_saturation);
#else
  uint16_t V_saturation = ((int16_t)temperature_c - CAL2_T) * ((uint16_t)CAL1_V - CAL2_V) / ((uint8_t)CAL1_T - CAL2_T) + CAL2_V;
  return (voltage_mv * DO_Table[temperature_c] / V_saturation);
#endif
}

void readLevelSensor() {
  lv1 = digitalRead(lv1Pin);
}

// Setup 
void setup() {
  Serial.begin(115200);
  while (!Serial);

  // Initialize EEPROM (Arduino Mega has built-in EEPROM)
  loadThresholdsFromEEPROM();

  // Initialize sensors
  sensors.begin();              // DS18B20
  ph.begin();                  // PH
  gravityTds.setPin(TdsSensorPin);
  gravityTds.setAref(VREF);
  gravityTds.setAdcRange(1023);     // Arduino Mega uses 10-bit ADC (0-1023)
  gravityTds.begin();

  // Initialize pins
  pinMode(mosfetOnOffPin, OUTPUT);
  pinMode(lv1Pin, INPUT_PULLUP);  // Set level sensor pin as input with pullup
  pinMode(lv2Pin, INPUT_PULLUP);  // Set level sensor pin as input with pullup
  digitalWrite(mosfetOnOffPin, LOW);

  // Initialize BME280
  if (!bme.begin(0x76)) {
    Serial.println("[ERROR] BME280 sensor not found!");
    while (1) delay(1);
  }

  // Initialize RTC
  DS3231_init(DS3231_CONTROL_INTCN);
  
  // Set start time for timestamp simulation
  startTime = millis();

  Serial.println("[INFO] System initialized successfully");
}

  // === Loop ===
void loop() {
  unsigned long now = millis();
  handleSerialCommand();
  
  // Get current second for RTC timing
  DS3231_get(&t);
  secNow = t.sec;
  
  // Sensor dan Pompa
  if (now - lastPoll >= pollInterval) {
    lastPoll = now;

    temp = bme.readTemperature();
    humidity = bme.readHumidity();
    readWaterTemp();
    readPH();
    readTDS();
    readDO();
    readLevelSensor();

    if (isnan(temp) || isnan(humidity)) {
      Serial.println("[ERROR] Failed to read BME280 sensor");
      return;
    }

    if (!pumpActive) {
      if (temp < tempMin && humidity >= humMax) {
        pumpDuration = 30000;
      } else if (temp < tempMin && humidity < humMin) {
        pumpDuration = 60000;
      } else if (temp >= tempMax && humidity < humMin) {
        pumpDuration = 30000;
      } else if (temp >= tempMin && temp <= tempMax && humidity >= humMin && humidity <= humMax) {
        pumpDuration = 0;
      } else {
        pumpDuration = 30000;
      }

      if (pumpDuration > 0) {
        pumpStartTime = now;
        turnPumpOn();
      } else {
        turnPumpOff();
      }
    }
  }

  if (pumpActive && (now - pumpStartTime >= pumpDuration)) {
    turnPumpOff();
  }
  
  // Update mist status setiap loop untuk memastikan sinkronisasi
  updateMistStatus();

  // Get timestamp
  String timestamp = getFormattedTime();

  // PESAN A - Send every 5 seconds based on RTC
  if (secNow != secondSendA && secNow % 5 == 0) {
    secondSendA = secNow;
    String pesanA = "A;" + String(farmID) + ";" + timestamp + ";" +
                    String(waterTemp, 2) + ";" +
                    String(temp, 2) + ";" +
                    String(humidity, 2) + ";" +
                    String(pHValue, 2) + ";" +
                    String(tdsValue, 2) + ";" +
                    String(oxygenValue, 2);
    Serial.println(pesanA);
  }

  // PESAN B - Send every 5 seconds based on RTC (offset by 1 second)
  if (secNow != secondSendB && (secNow + 1) % 5 == 0) {
    secondSendB = secNow;
    String pesanB = "B;" + String(farmID) + ";" + timestamp + ";" +
                    String(mainTankPump) + ";" +
                    String(PHUpPump) + ";" +
                    String(PHDownPump) + ";" +
                    String(mixAPump) + ";" +
                    String(mixBPump) + ";" +
                    String(valve) + ";" +
                    String(mist) + ";" +
                    String(lv1);
    Serial.println(pesanB);
  }

  // PESAN SP - Send every 5 seconds based on RTC (offset by 2 seconds)
  if (secNow != secondSendSP && (secNow + 2) % 5 == 0) {
    secondSendSP = secNow;
    String pesanSP = "SP;" + String(farmID) + ";" + timestamp + ";" +
                     String(thPHUp, 2) + ";" + String(thPHDown, 2) + ";" +
                     String(thTDSUp) + ";" + String(thTDSDown) + ";" +
                     String(tempMax, 2) + ";" + String(tempMin, 2) + ";" +
                     String(humMax, 2) + ";" + String(humMin, 2);
    Serial.println(pesanSP);
  }
}