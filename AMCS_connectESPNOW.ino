#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <time.h>
#include <EEPROM.h>
#include <esp_now.h>

// Konstanta pesan
#define MSG_A 0
#define MSG_B 1
#define MSG_SP 2

// MAC address penerima
uint8_t peerAddress[] = {0x20, 0x43, 0xA8, 0x65, 0x71, 0x78};

// WiFi
const char* ssid = "Brin-Net";
const char* password = "";

// Sensor dan pin
const int mosfetOnOffPin = 25;
Adafruit_BME280 bme;

// Variabel sensor dan pompa
float temp, humidity;
bool pumpActive = false;
unsigned long pumpStartTime = 0;
unsigned long pumpDuration = 0;
unsigned long lastPoll = 0;
const unsigned long pollInterval = 3000;
const int farmID = 16;

// Dummy data
float waterTemp = 25.3;
float pHValue = 6.8;
float tdsValue = 1200;
float oxygenValue = 7.5;

// Status aktuator
bool mainTankPump = true;
bool PHUpPump = false;
bool PHDownPump = false;
bool mixAPump = false;
bool mixBPump = true;
bool valve = true;
bool mist = false;
bool lv1 = true;

// Setpoint
float thPHUp = 6.5;
float thPHDown = 5.5;
float thTDSUp = 1300;
float thTDSDown = 800;
float tempMin = 15.5;
float tempMax = 27.9;
float humMin = 82.0;
float humMax = 92.0;

// Timer kirim pesan
unsigned long lastMsgA = 0, lastMsgB = 0, lastMsgSP = 0;

// EEPROM Helpers
void loadThresholdsFromEEPROM() {
  EEPROM.get(0, tempMin);
  EEPROM.get(4, tempMax);
  EEPROM.get(8, humMin);
  EEPROM.get(12, humMax);
  if (isnan(tempMin)) tempMin = 15.5;
  if (isnan(tempMax)) tempMax = 27.9;
  if (isnan(humMin)) humMin = 82.0;
  if (isnan(humMax)) humMax = 92.0;
}

void saveThresholdsToEEPROM() {
  EEPROM.put(0, tempMin);
  EEPROM.put(4, tempMax);
  EEPROM.put(8, humMin);
  EEPROM.put(12, humMax);
  EEPROM.commit();
}

// Format waktu
String getFormattedTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return "01/01/1970 00:00:00";
  char buffer[30];
  sprintf(buffer, "%02d/%02d/%04d %02d:%02d:%02d",
          timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900,
          timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
  return String(buffer);
}

// Pompa
bool isPumpOn() {
  return digitalRead(mosfetOnOffPin) == HIGH;
}

void turnPumpOn() {
  digitalWrite(mosfetOnOffPin, HIGH);
  if (!pumpActive) {
    pumpActive = true;
    Serial.printf("[PUMP] ON for %lu seconds\n", pumpDuration / 1000);
  }
}

void turnPumpOff() {
  digitalWrite(mosfetOnOffPin, LOW);
  if (pumpActive) {
    pumpActive = false;
    Serial.println("[PUMP] OFF");
  }
}

// Serial command handler
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
    } else if (input == "GET MAC") {
      Serial.println("MAC Address: " + WiFi.macAddress());
    } else {
      Serial.println("[INFO] Format perintah tidak dikenal");
    }
  }
}

// Versi lengkap kirim ESP-NOW
void kirimPesanESPNow(int tipe, int farmID, const char* waktu, float waterTemp, float suhu, float hum, float ph, float tds, float oxygen,
                      int mainTankPump, int phUpPump, int phDownPump, int mixAPump, int mixBPump, int valve, int mist, int lv1,
                      float suhuMax, float suhuMin, int humMax, int humMin) {
  char payload[250];

  if (tipe == MSG_A) {
    sprintf(payload, "A;%d;%s;%.2f;%.2f;%.2f;%.2f;%.2f;%.2f",
            farmID, waktu, waterTemp, suhu, hum, ph, tds, oxygen);
  } 
  else if (tipe == MSG_B) {
    sprintf(payload, "B;%d;%s;%d;%d;%d;%d;%d;%d;%d;%d",
            farmID, waktu, mainTankPump, phUpPump, phDownPump,
            mixAPump, mixBPump, valve, mist, lv1);
  } 
  else if (tipe == MSG_SP) {
    sprintf(payload, "SP;%d;%s;%.2f;%.2f;%.0f;%.0f;%.2f;%.2f;%.2f;%.2f",
            farmID, waktu, thPHUp, thPHDown, thTDSUp, thTDSDown,
            suhuMax, suhuMin, (float)humMax, (float)humMin);
  }

  esp_err_t result = esp_now_send(peerAddress, (uint8_t*)payload, strlen(payload));
  if (result == ESP_OK) {
    Serial.println("✅ Pesan terkirim: " + String(payload));
  } else {
    Serial.println("❌ Gagal kirim: " + String(payload));
  }
}

// Setup
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

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");
  Serial.println("MAC Address: " + WiFi.macAddress());

  configTime(7 * 3600, 0, "pool.ntp.org", "time.nist.gov");
  delay(2000);

  if (esp_now_init() != ESP_OK) {
    Serial.println("[ERROR] ESP-NOW Init Failed");
    return;
  }

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, peerAddress, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) == ESP_OK) {
    Serial.println("[ESP-NOW] Peer added successfully");
  } else {
    Serial.println("[ERROR] Failed to add peer");
  }

  Serial.println("Setup completed!");
}

// Loop
void loop() {
  unsigned long now = millis();

  if (now - lastPoll >= pollInterval) {
    lastPoll = now;

    temp = bme.readTemperature();
    humidity = bme.readHumidity();

    if (!pumpActive) {
      if (temp < tempMin && humidity >= humMax) pumpDuration = 30000;
      else if (temp < tempMin && humidity < humMin) pumpDuration = 60000;
      else if (temp >= tempMax && humidity < humMin) pumpDuration = 30000;
      else if (temp >= tempMin && temp <= tempMax && humidity >= humMin && humidity <= humMax) pumpDuration = 0;
      else pumpDuration = 30000;

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

  String timestamp = getFormattedTime();
  mist = isPumpOn();

  if (now - lastMsgA >= 5000) {
    lastMsgA = now;
   kirimPesanESPNow(MSG_A, farmID, timestamp.c_str(),
  waterTemp, temp, humidity, pHValue, tdsValue, oxygenValue,
  0, 0, 0, 0, 0, 0, 0, 0,    // actuator status (8)
  0, 0,                      // tempMin, tempMax
  0, 0);                     // humMin, humMax


  if (now - lastMsgB >= 7000) {
    lastMsgB = now;
    kirimPesanESPNow(MSG_B, farmID, timestamp.c_str(), 0, 0, 0, 0, 0, 0,
                     mainTankPump, PHUpPump, PHDownPump, mixAPump, mixBPump,
                     valve, mist, lv1, 0, 0, 0, 0);  // Sensor & SP dummy
  }

  if (now - lastMsgSP >= 10000) {
    lastMsgSP = now;
    kirimPesanESPNow(MSG_SP, farmID, timestamp.c_str(), 0, 0, 0, 0, 0, 0,
                     0, 0, 0, 0, 0, 0, 0, 0,
                     tempMax, tempMin, humMax, humMin);  // Hanya threshold
  }

  handleSerialCommand();
  delay(100);
}
}