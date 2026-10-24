#include <Arduino.h>
#include <Wire.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <Adafruit_BMP280.h>

#if defined(ESP8266)
  #include <ESP8266WiFi.h>
  #include <ESP8266HTTPClient.h>
#elif defined(ESP32)
  #include <WiFi.h>
  #include <HTTPClient.h>
#endif

// ==================== НАСТРОЙКИ ПОЛЬЗОВАТЕЛЯ ====================

// Секреты (Wi-Fi, адрес станции, ключ) лежат в src/secrets.h и не попадают
// в репозиторий. Если файла нет — используются значения-заглушки.
#if __has_include("secrets.h")
  #include "secrets.h"
#else
  #define WIFI_SSID    "your-wifi"
  #define WIFI_PASS    "your-password"
  #define STATION_HOST "192.168.1.100"
  #define STATION_PORT 80
  #define STATION_KEY  ""
#endif

// Идентификатор этого узла (латиницей без пробелов)
#define NODE_ID           "ulica"

// Интервал отправки данных (в секундах)
#define SEND_INTERVAL_SEC 1800

// Режим глубокого сна (0 = постоянно включен, 1 = Deep Sleep)
#define USE_DEEP_SLEEP    1

// -------------------- Пины подключения --------------------
#if defined(ESP8266)
  // Wemos D1 mini / NodeMCU: D2 = GPIO4 (SDA), D1 = GPIO5 (SCL), D4 = GPIO2 (1-Wire)
  #define PIN_I2C_SDA     4
  #define PIN_I2C_SCL     5
  #define PIN_ONEWIRE     2
#elif defined(ESP32)
  // Классический ESP32: 21 (SDA), 22 (SCL), 4 (1-Wire)
  #define PIN_I2C_SDA     21
  #define PIN_I2C_SCL     22
  #define PIN_ONEWIRE     4
#endif

// ================================================================

enum I2cType { I2C_NONE = 0, I2C_BME280, I2C_BMP280 };

struct I2cSlot {
  uint8_t addr;
  const char* suffix;
  I2cType type;
  Adafruit_BME280 bme;
  Adafruit_BMP280 bmp;
};

static I2cSlot i2cSlots[2] = {
  { 0x76, "76", I2C_NONE, Adafruit_BME280(), Adafruit_BMP280() },
  { 0x77, "77", I2C_NONE, Adafruit_BME280(), Adafruit_BMP280() }
};

static OneWire* ow = nullptr;
static DallasTemperature* ds = nullptr;
static int dsCount = 0;

static uint8_t readChipId(uint8_t addr) {
  Wire.beginTransmission(addr);
  Wire.write(0xD0);
  if (Wire.endTransmission() != 0) return 0;
  if (Wire.requestFrom(addr, (uint8_t)1) != 1) return 0;
  return Wire.read();
}

static void detectAll() {
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);

  for (int i = 0; i < 2; i++) {
    uint8_t addr = i2cSlots[i].addr;
    uint8_t id = readChipId(addr);
    if (id == 0x60 && i2cSlots[i].bme.begin(addr, &Wire)) {
      i2cSlots[i].type = I2C_BME280;
      i2cSlots[i].bme.setSampling(
        Adafruit_BME280::MODE_FORCED,
        Adafruit_BME280::SAMPLING_X1,
        Adafruit_BME280::SAMPLING_X1,
        Adafruit_BME280::SAMPLING_X1,
        Adafruit_BME280::FILTER_OFF
      );
      Serial.printf("✅ Найден BME280 на 0x%02X\n", addr);
    } else if ((id == 0x58 || id == 0x56 || id == 0x57) && i2cSlots[i].bmp.begin(addr)) {
      i2cSlots[i].type = I2C_BMP280;
      i2cSlots[i].bmp.setSampling(
        Adafruit_BMP280::MODE_FORCED,
        Adafruit_BMP280::SAMPLING_X1,
        Adafruit_BMP280::SAMPLING_X1,
        Adafruit_BMP280::FILTER_OFF
      );
      Serial.printf("✅ Найден BMP280 на 0x%02X\n", addr);
    }
  }

  if (!ow) {
    ow = new OneWire(PIN_ONEWIRE);
    ds = new DallasTemperature(ow);
    ds->begin();
  }
  int total = ds->getDeviceCount();
  Serial.printf("На шине 1-Wire найдено устройств: %d\n", total);
  dsCount = total > 8 ? 8 : total;
}

static bool connectWiFi(uint32_t timeoutMs = 15000) {
  if (WiFi.status() == WL_CONNECTED) return true;
  Serial.printf("Подключение к Wi-Fi '%s'...", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < timeoutMs) {
    delay(250);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf(" OK! IP: %s, RSSI: %d dBm\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
    return true;
  }
  Serial.println(" ОШИБКА подключения!");
  return false;
}

static int buildJson(String& out) {
  String sensorsArray = "";
  int count = 0;

  for (int i = 0; i < 2; i++) {
    if (i2cSlots[i].type == I2C_BME280) {
      i2cSlots[i].bme.takeForcedMeasurement();
      float t = i2cSlots[i].bme.readTemperature();
      float h = i2cSlots[i].bme.readHumidity();
      float p = i2cSlots[i].bme.readPressure() / 100.0f;

      if (!isnan(t) && t > -80.0f && t < 100.0f) {
        if (count > 0) sensorsArray += ",";
        char buf[128];
        snprintf(buf, sizeof(buf), "{\"n\":\"bme%s\",\"t\":%.2f,\"h\":%.1f,\"p\":%.1f}",
                 i2cSlots[i].suffix, t, h, p);
        sensorsArray += buf;
        count++;
      }
    } else if (i2cSlots[i].type == I2C_BMP280) {
      i2cSlots[i].bmp.takeForcedMeasurement();
      float t = i2cSlots[i].bmp.readTemperature();
      float p = i2cSlots[i].bmp.readPressure() / 100.0f;

      if (!isnan(t) && t > -80.0f && t < 100.0f) {
        if (count > 0) sensorsArray += ",";
        char buf[128];
        snprintf(buf, sizeof(buf), "{\"n\":\"bmp%s\",\"t\":%.2f,\"p\":%.1f}",
                 i2cSlots[i].suffix, t, p);
        sensorsArray += buf;
        count++;
      }
    }
  }

  if (dsCount > 0 && ds) {
    ds->requestTemperatures();
    DeviceAddress addr;
    for (int i = 0; i < dsCount; i++) {
      if (ds->getAddress(addr, i)) {
        float t = ds->getTempC(addr);
        if (t != DEVICE_DISCONNECTED_C && fabs(t - 85.0f) > 0.05f && t > -80.0f && t < 110.0f) {
          if (count > 0) sensorsArray += ",";
          char buf[64];
          snprintf(buf, sizeof(buf), "{\"n\":\"ds%d\",\"t\":%.2f}", i + 1, t);
          sensorsArray += buf;
          count++;
        }
      }
    }
  }

  if (count == 0) return 0;

  out = String("{\"id\":\"") + NODE_ID + "\",";
  if (strlen(STATION_KEY) > 0) {
    out += "\"k\":\"" + String(STATION_KEY) + "\",";
  }
  out += "\"rssi\":" + String(WiFi.RSSI()) + ",\"s\":[" + sensorsArray + "]}";
  return count;
}

static void readAndSend() {
  if (!connectWiFi()) return;

  String payload;
  if (buildJson(payload) == 0) {
    Serial.println("⚠️ Нет валидных данных ни с одного датчика.");
    return;
  }

  String url = "http://" + String(STATION_HOST) + ":" + String(STATION_PORT) + "/api/sensor";
  Serial.println("Отправка: " + payload);

  WiFiClient client;
  HTTPClient http;
  http.setTimeout(5000);

  if (http.begin(client, url)) {
    http.addHeader("Content-Type", "application/json");
    int code = http.POST(payload);
    String resp = http.getString();
    Serial.printf("Ответ станции: HTTP %d (%s)\n", code, resp.c_str());
    http.end();
  } else {
    Serial.println("❌ Не удалось открыть HTTP-соединение со станцией");
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.printf("\nSKYCAST Sensor, id=%s\n", NODE_ID);

  detectAll();
  readAndSend();

#if USE_DEEP_SLEEP
  Serial.printf("Уход в Deep Sleep на %d сек...\n", SEND_INTERVAL_SEC);
  ESP.deepSleep((uint64_t)SEND_INTERVAL_SEC * 1000000ULL);
#endif
}

void loop() {
#if !USE_DEEP_SLEEP
  static uint32_t last = 0;
  static bool firstRun = true;
  uint32_t now = millis();
  
  if (firstRun || now - last >= (uint32_t)SEND_INTERVAL_SEC * 1000UL) {
    last = now;
    firstRun = false;
    readAndSend();
  }
  delay(50);
#endif
}