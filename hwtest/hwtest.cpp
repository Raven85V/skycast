// Тест Sunton 7": (А) подбор частоты ШИМ подсветки, (Б) мерцание экрана при подключении к Wi-Fi.
// Сборка: pio run -e skycasttest -t upload ; монитор порта 115200.
// Wi-Fi берётся из флеш-памяти (сохранён основной прошивкой), иначе из config.h.
// Частота пикселей: -DLCD_PCLK_HZ=12000000 в build_flags окружения skycasttest.
#include <Arduino.h>
#include <WiFi.h>
#include "lgfx_crowpanel.h"
#include "config.h"

LGFX gfx;

static void title(const char* l1, const char* l2 = "") {
  gfx.fillRect(0, 0, 800, 120, 0xFFFFFF);
  gfx.setTextColor(0x000000, 0xFFFFFF);
  gfx.setTextSize(4);
  gfx.setCursor(20, 15);  gfx.print(l1);
  gfx.setTextSize(3);
  gfx.setCursor(20, 70);  gfx.print(l2);
}

// Неподвижная картинка (вертикальные полосы + рамка): любой сдвиг/дрожание/полосы при Wi-Fi хорошо видны.
// Живость показывает только маленький квадрат в углу, рисуется редко (разрыва кадра нет).
static void drawStatic() {
  for (int x = 0; x < 800; x += 40) gfx.fillRect(x, 130, 20, 350, 0x000000);
  for (int x = 20; x < 800; x += 40) gfx.fillRect(x, 130, 20, 350, 0xFFFFFF);
  gfx.drawRect(0, 130, 800, 350, 0xFF0000);
  gfx.drawRect(1, 131, 798, 348, 0xFF0000);
}
static void animateFor(uint32_t ms, const char* l1, const char* l2) {
  uint32_t t0 = millis();
  title(l1, l2);
  drawStatic();
  bool on = false;
  while (millis() - t0 < ms) {
    on = !on;
    gfx.fillRect(740, 20, 40, 40, on ? 0x00AA00 : 0xFFFFFF);
    delay(500);
  }
}

// ---------- А: ШИМ подсветки ----------
static void pwmSweep() {
  static const uint32_t freqs[] = {200, 500, 1000, 2000, 5000, 10000, 20000, 40000};
  gfx.fillScreen(0xFFFFFF);
  for (uint32_t f : freqs) {
    bool ok = ledcAttach(PIN_BL, f, 8);
    Serial.printf("ШИМ %u Гц: attach=%d\n", (unsigned)f, (int)ok);
    char b[40]; snprintf(b, sizeof b, "PWM %u Hz", (unsigned)f);
    title(b, "brightness 100 -> 10 -> 100 %");
    for (int k = 0; k < 2; k++) {
      for (int d = 255; d >= 25; d -= 5) { ledcWrite(PIN_BL, d); delay(25); }
      for (int d = 25; d <= 255; d += 5) { ledcWrite(PIN_BL, d); delay(25); }
    }
    ledcDetach(PIN_BL);
    pinMode(PIN_BL, OUTPUT);
    digitalWrite(PIN_BL, HIGH);
    delay(500);
  }
}

// ---------- Б: мерцание при Wi-Fi ----------
static void wifiFlickerTest() {
  char b[80];
  Serial.printf("Wi-Fi тест, PCLK %u Гц\n", (unsigned)LCD_PCLK_HZ);
  snprintf(b, sizeof b, "PCLK %.1f MHz", LCD_PCLK_HZ / 1e6);
  animateFor(6000, "Wi-Fi OFF (reference)", b);

  WiFi.mode(WIFI_STA);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);       // максимум: худший случай для питания
  WiFi.begin();                              // сохранённые данные
  delay(200);
  if (WiFi.SSID().length() == 0) {
    Serial.println("В флеш нет Wi-Fi, беру из config.h");
    WiFi.begin(WIFI_SSID, WIFI_PASS);
  }
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 45000) {
    snprintf(b, sizeof b, "%u s, status %d, TX 19.5", (unsigned)((millis() - t0) / 1000), (int)WiFi.status());
    animateFor(1000, "Connecting to Wi-Fi...", b);
  }
  Serial.printf("Wi-Fi: статус %d за %u мс, RSSI %d\n", (int)WiFi.status(), (unsigned)(millis() - t0), (int)WiFi.RSSI());

  struct { wifi_power_t v; const char* n; } lv[] = {
    {WIFI_POWER_19_5dBm, "19.5"}, {WIFI_POWER_15dBm, "15"}, {WIFI_POWER_11dBm, "11"}, {WIFI_POWER_8_5dBm, "8.5"}};
  for (auto& l : lv) {
    WiFi.setTxPower(l.v);
    snprintf(b, sizeof b, "TX %s dBm, RSSI %d", l.n, (int)WiFi.RSSI());
    Serial.println(b);
    animateFor(10000, "Wi-Fi connected: flicker?", b);   // в сети есть трафик от автопереподключений/пинга
  }
  WiFi.disconnect(true, false);   // false - не стирать сохранённые данные
  WiFi.mode(WIFI_OFF);
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("HWTEST start");
  pinMode(PIN_BL, OUTPUT);
  digitalWrite(PIN_BL, HIGH);
  Serial.printf("PSRAM: %u байт, свободно %u\n", (unsigned)ESP.getPsramSize(), (unsigned)ESP.getFreePsram());
  bool ok = gfx.init();
  Serial.printf("gfx.init() = %d, %dx%d\n", (int)ok, (int)gfx.width(), (int)gfx.height());
  gfx.setRotation(LCD_ROTATION);
  gfx.fillScreen(0xFFFFFF);
}

void loop() {
  pwmSweep();
  wifiFlickerTest();
  title("Test done", "repeat in 5 s");
  delay(5000);
}
