#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <time.h>
#include <EEPROM.h>

// === WiFi ===
const char* ssid = "Sonya alexa";
const char* password = "sonya123";

// Pin dan Sensor 
const int mosfetOnOffPin = 25;
Adafruit_BME280 bme;

// === Variabel Sensor dan Pompa ===
float temp, humidity;
bool pumpActive = false;
unsigned long pumpStartTime = 0;
unsigned long pumpDuration = 0;
unsigned long lastPoll = 0;
const unsigned long pollInterval = 3000;

// === Farm ID ===
const int farmID = 16;

// === Threshold ===
float tempMin = 15.5, tempMax = 27.9;
float humMin = 82.0, humMax = 92.0;

// === Dummy Data Sensor Lain ===
float waterTemp = 25.3;
float pHValue = 6.8;
float tdsValue = 1200;
float oxygenValue = 7.5;

// === Status Aktuator Dummy ===
bool mainTankPump = true;
bool PHUpPump = false;
bool PHDownPump = false;
bool mixAPump = false;
bool mixBPump = true;
bool valve = true;
bool mist = false;  // Akan diupdate berdasarkan status pompa
bool lv1 = true;

// === Threshold Dummy ===
float thPHUp = 6.5;
float thPHDown = 5.5;
float thTDSUp = 1300;
float thTDSDown = 800;
float thWTempUp = 28.0;
float thWTempDown = 22.0;
float thHumUp = 90.0;
float thHumDown = 60.0;

// === Timer Kirim Pesan ===
unsigned long lastMsgA = 0, lastMsgB = 0, lastMsgSP = 0;

// === EEPROM Helpers ===
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
  EEPROM.commit();
}

// === Waktu ===
String getFormattedTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return "01/01/1970 00:00:00";
  char buffer[30];
  sprintf(buffer, "%02d/%02d/%04d %02d:%02d:%02d",
          timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900,
          timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
  return String(buffer);
}

// === Pompa ===
bool isPumpOn() {
  return digitalRead(mosfetOnOffPin) == HIGH;
}

void turnPumpOn() {
  digitalWrite(mosfetOnOffPin, HIGH);
  if (!pumpActive) {
    pumpActive = true;
    mist = true;  // Update status mist saat pompa ON
    Serial.printf("[PUMP] ON for %lu seconds\n", pumpDuration / 1000);
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

// === Update Mist Status ===
void updateMistStatus() {
  // Synchronize mist status dengan actual MOSFET status
  mist = isPumpOn();
}

// === Serial Command Handler ===
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
        Serial.printf("[THRESHOLD] tempMin updated to %.2f\n", tempMin);
      }
    } else if (input.startsWith("SET TEMP MAX ")) {
      float value = input.substring(13).toFloat();
      if (value > tempMin && value < 100) {
        tempMax = value;
        saveThresholdsToEEPROM();
        Serial.printf("[THRESHOLD] tempMax updated to %.2f\n", tempMax);
      }
    } else if (input.startsWith("SET HUM MIN ")) {
      float value = input.substring(12).toFloat();
      if (value > 0 && value < humMax) {
        humMin = value;
        saveThresholdsToEEPROM();
        Serial.printf("[THRESHOLD] humMin updated to %.2f\n", humMin);
      }
    } else if (input.startsWith("SET HUM MAX ")) {
      float value = input.substring(12).toFloat();
      if (value > humMin && value < 100) {
        humMax = value;
        saveThresholdsToEEPROM();
        Serial.printf("[THRESHOLD] humMax updated to %.2f\n", humMax);
      }
    } else {
      Serial.println("[INFO] Format perintah tidak dikenal");
    }
  }
}

// === Setup ===
void setup() {
  Serial.begin(115200);
  while (!Serial);

  EEPROM.begin(64);
  loadThresholdsFromEEPROM();

  pinMode(mosfetOnOffPin, OUTPUT);
  digitalWrite(mosfetOnOffPin, LOW);

  if (!bme.begin(0x76)) {
    Serial.println("[ERROR] BME280 sensor not found!");
    while (1) delay(1);
  }

  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");
  Serial.println(WiFi.localIP());

  configTime(7 * 3600, 0, "pool.ntp.org", "time.nist.gov");
}

// === Loop ===
void loop() {
  unsigned long now = millis();

  handleSerialCommand();

  // Sensor dan Pompa
  if (now - lastPoll >= pollInterval) {
    lastPoll = now;

    temp = bme.readTemperature();
    humidity = bme.readHumidity();

    if (isnan(temp) || isnan(humidity)) {
      Serial.println("[ERROR] Failed to read sensor");
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

  // === PESAN A ===
  if (now - lastMsgA >= 5000) {
    lastMsgA = now;
    String pesanA = "A;" + String(farmID) + ";" + timestamp + ";" +
                    String(waterTemp, 2) + ";" +
                    String(temp, 2) + ";" +
                    String(humidity, 2) + ";" +
                    String(pHValue, 2) + ";" +
                    String(tdsValue, 2) + ";" +
                    String(oxygenValue, 2);
    Serial.println(pesanA);
  }

  // === PESAN B ===
  if (now - lastMsgB >= 5000) {
    lastMsgB = now;
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

  // === PESAN SP ===
  if (now - lastMsgSP >= 5000) {
    lastMsgSP = now;
    String pesanSP = "SP;" + String(farmID) + ";" + timestamp + ";" +
                     String(thPHUp, 2) + ";" + String(thPHDown, 2) + ";" +
                     String(thTDSUp) + ";" + String(thTDSDown) + ";" +
                     String(thWTempUp, 2) + ";" + String(thWTempDown, 2) + ";" +
                     String(thHumUp, 2) + ";" + String(thHumDown, 2);
    Serial.println(pesanSP);
  }
}