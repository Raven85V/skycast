#include "ui_anim.h"
#include "lgfx_crowpanel.h"
#include <LittleFS.h>

#define ANIM_MAX 32
struct AnimEntry { char name[16]; uint16_t frames; uint32_t off; };

static uint16_t* g_buf = nullptr;      // весь файл в PSRAM
static uint32_t  g_len = 0;
static AnimEntry g_e[ANIM_MAX];
static int       g_n = 0;
static uint16_t  g_w = 0, g_h = 0;

bool uiAnimReady() { return g_n > 0 && g_buf != nullptr; }

int uiAnimW() { return g_w; }
int uiAnimH() { return g_h; }

bool uiAnimInit() {
  if (!LittleFS.begin(true)) { Serial.println("LittleFS: не смонтировалась"); return false; }
  File f = LittleFS.open("/icons.bin", "r");
  if (!f) { Serial.println("icons.bin нет - работаем на встроенных иконках"); return false; }
  g_len = f.size();
  g_buf = (uint16_t*)heap_caps_malloc(g_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!g_buf) { Serial.println("icons.bin: нет памяти в PSRAM"); f.close(); return false; }
  const size_t rd = f.read((uint8_t*)g_buf, g_len);
  f.close();
  if (rd != g_len) { free(g_buf); g_buf = nullptr; return false; }

  const uint8_t* p = (const uint8_t*)g_buf;
  if (memcmp(p, "ICN1", 4) != 0) { Serial.println("icons.bin: плохой заголовок"); return false; }
  const uint16_t cnt = *(const uint16_t*)(p + 4);
  g_w = *(const uint16_t*)(p + 6);
  g_h = *(const uint16_t*)(p + 8);
  uint32_t off = 10;
  g_n = 0;
  for (int i = 0; i < cnt && g_n < ANIM_MAX; i++) {
    AnimEntry& e = g_e[g_n];
    memcpy(e.name, p + off, 16); e.name[15] = 0;
    e.frames = *(const uint16_t*)(p + off + 16);
    e.off = off + 18;
    off = e.off + (uint32_t)e.frames * g_w * g_h * 2;
    if (off > g_len) break;
    g_n++;
  }
  Serial.printf("icons.bin: %d icons, %dx%d, %u KB\n", g_n, g_w, g_h, (unsigned)(g_len / 1024));
  return g_n > 0;
}

int uiAnimFrames(const char* name) {
  if (!g_buf || !name) return 0;
  for (int i = 0; i < g_n; i++) if (strcmp(g_e[i].name, name) == 0) return g_e[i].frames;
  return 0;
}

const uint16_t* uiAnimFrame(const char* name, int idx) {
  if (!g_buf || !name) return nullptr;
  for (int i = 0; i < g_n; i++) {
    if (strcmp(g_e[i].name, name) != 0) continue;
    if (idx < 0 || idx >= g_e[i].frames) return nullptr;
    const uint32_t o = g_e[i].off + (uint32_t)idx * g_w * g_h * 2;
    return (const uint16_t*)((const uint8_t*)g_buf + o);
  }
  return nullptr;
}

void uiAnimDraw(const char* name, int x, int y, int w, int h) {
  const int n = uiAnimFrames(name);
  if (n <= 0) return;
  const int f = (int)((millis() / 160) % (uint32_t)n);
  const uint16_t* fr = uiAnimFrame(name, f);
  if (!fr) return;
  const int fw = g_w, fh = g_h;
  bool oldSwap = gfx.getSwapBytes();
  gfx.setSwapBytes(true);
  if (w == fw && h == fh) {
    gfx.pushImage(x, y, w, h, fr);
  } else {
    static uint16_t* buf = nullptr;
    static size_t cap = 0;
    const size_t need = (size_t)w * h * 2;
    if (cap < need) {
      if (buf) free(buf);
      buf = (uint16_t*)heap_caps_malloc(need, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
      cap = buf ? need : 0;
    }
    if (!buf) { gfx.setSwapBytes(oldSwap); return; }
    for (int dy = 0; dy < h; dy++) {
      const int sy = dy * fh / h;
      for (int dx = 0; dx < w; dx++) buf[dy * w + dx] = fr[sy * fw + dx * fw / w];
    }
    gfx.pushImage(x, y, w, h, buf);
  }
  gfx.setSwapBytes(oldSwap);
}
