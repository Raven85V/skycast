#include "moon.h"
#include "lgfx_crowpanel.h"
#include <LittleFS.h>
#include <esp_heap_caps.h>
#include <string.h>

static uint16_t* g_data = nullptr;   // все кадры подряд в PSRAM
static int g_count = 0;
static int g_w = 0, g_h = 0;

bool moonInit() {
  if (g_data) return true;
  if (!LittleFS.begin(true)) {
    Serial.println("moon.bin: файловая система не смонтирована - фазы луны из встроенного круга");
    return false;
  }
  File f = LittleFS.open("/moon.bin", "r");
  if (!f) {
    Serial.println("moon.bin нет - фазы луны рисовать нечем");
    return false;
  }
  char magic[4];
  if (f.read((uint8_t*)magic, 4) != 4 || memcmp(magic, "MOON", 4) != 0) {
    Serial.println("moon.bin: неверный формат");
    f.close();
    return false;
  }
  uint16_t cnt = 0, w = 0, h = 0;
  f.read((uint8_t*)&cnt, 2);
  f.read((uint8_t*)&w, 2);
  f.read((uint8_t*)&h, 2);
  const size_t need = (size_t)cnt * w * h * 2;
  if (cnt == 0 || w == 0 || h == 0 || need > 4u * 1024u * 1024u) {
    Serial.println("moon.bin: подозрительные размеры");
    f.close();
    return false;
  }
  g_data = (uint16_t*)heap_caps_malloc(need, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!g_data) {
    Serial.println("moon.bin: нет памяти в PSRAM");
    f.close();
    return false;
  }
  const size_t got = f.read((uint8_t*)g_data, need);
  f.close();
  if (got != need) {
    free(g_data);
    g_data = nullptr;
    Serial.println("moon.bin: файл короче ожидаемого");
    return false;
  }
  g_count = cnt;
  g_w = w;
  g_h = h;
  Serial.printf("moon.bin: %d фаз, %dx%d\n", g_count, g_w, g_h);
  return true;
}

const uint16_t* moonFrame(int idx) {
  if (!g_data || g_count <= 0) return nullptr;
  idx %= g_count;
  if (idx < 0) idx += g_count;
  return g_data + (size_t)idx * g_w * g_h;
}

int moonCount() { return g_count; }
int moonW() { return g_w; }
int moonH() { return g_h; }
