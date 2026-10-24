#include "ui_boot.h"
#include "config.h"
#include "lgfx_crowpanel.h"
#include <math.h>
#include <esp_heap_caps.h>

#define RGB16(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))
#define F_D72 (&lgfx::fonts::DejaVu72)
#define F_D56 (&lgfx::fonts::DejaVu56)
#define F_D40 (&lgfx::fonts::DejaVu40)
#define F_J28 (&lgfx::fonts::lgfxJapanGothic_28)

using lgfx::textdatum_t;

static const int BAND_Y = 100, BAND_H = 280;          // полоса экрана, в которой идёт анимация
static const int GRAD_L_R = 90,  GRAD_L_G = 200, GRAD_L_B = 255;   // левый цвет градиента (голубой)
static const int GRAD_R_R = 250, GRAD_R_G = 205, GRAD_R_B = 90;    // правый цвет (янтарный)

struct Particle {
  float x, y, vx, vy;
  uint8_t r, g, b;
  uint16_t delay;      // кадров до начала разлёта (слева направо)
};

static float easeOutCubic(float t) { float u = 1.0f - t; return 1.0f - u * u * u; }

static void frameWait(uint32_t& next, uint32_t period) {
  uint32_t now = millis();
  if ((int32_t)(next - now) > 0) delay(next - now);
  next = millis() + period;
}

static void bootClassic(const char* title, const char* subtitle) {
  // самый крупный шрифт, в который влезает название
  const lgfx::IFont* fs[] = {F_D72, F_D56, F_D40};
  const lgfx::IFont* f = fs[2];
  for (int i = 0; i < 3; i++) {
    gfx.setFont(fs[i]);
    if (gfx.textWidth(title) <= 740) { f = fs[i]; break; }
  }
  gfx.setFont(f);
  const int tw = gfx.textWidth(title) + 8;
  const int th = gfx.fontHeight() + 4;

  // 1) маска текста (8 бит, белое на чёрном)
  lgfx::LGFX_Sprite mk(&gfx);
  mk.setPsram(true);
  mk.setColorDepth(8);
  if (!mk.createSprite(tw, th)) return;
  mk.fillSprite(0);
  mk.setFont(f);
  mk.setTextColor(0xFFFF, 0x0000);
  mk.setTextDatum(textdatum_t::top_left);
  mk.drawString(title, 4, 2);
  const uint8_t* mb = (const uint8_t*)mk.getBuffer();

  // 2) цветное название с горизонтальным градиентом (яркость - по покрытию маски)
  lgfx::LGFX_Sprite cs(&gfx);
  cs.setPsram(true);
  cs.setColorDepth(16);
  if (!cs.createSprite(tw, th)) return;
  cs.fillSprite(0);

  int cnt = 0;
  for (int y = 0; y < th; y++) {
    for (int x = 0; x < tw; x++) {
      uint8_t v = mb[y * tw + x];
      if (!v) continue;
      int cov = ((v >> 5) & 7) * 255 / 7;                       // 0..255
      float t = (float)x / (float)tw;
      int r = (int)((GRAD_L_R + (GRAD_R_R - GRAD_L_R) * t) * cov / 255);
      int g = (int)((GRAD_L_G + (GRAD_R_G - GRAD_L_G) * t) * cov / 255);
      int b = (int)((GRAD_L_B + (GRAD_R_B - GRAD_L_B) * t) * cov / 255);
      cs.drawPixel(x, y, RGB16(r, g, b));
      if (cov > 90 && ((x & 1) == 0) && ((y & 1) == 0)) cnt++;
    }
  }

  // 3) частицы для распада: каждая 2-я точка по обеим осям из ярких пикселей
  Particle* pt = (Particle*)heap_caps_malloc(sizeof(Particle) * (cnt > 0 ? cnt : 1), MALLOC_CAP_SPIRAM);
  int np = 0;
  const int tx = (800 - tw) / 2;
  const int ty = 70;
  if (pt) {
    for (int y = 0; y < th; y++) {
      for (int x = 0; x < tw; x++) {
        uint8_t v = mb[y * tw + x];
        if (!v) continue;
        int cov = ((v >> 5) & 7) * 255 / 7;
        if (cov <= 90 || (x & 1) || (y & 1) || np >= cnt) continue;
        float t = (float)x / (float)tw;
        Particle& p = pt[np++];
        p.x = tx + x;
        p.y = ty + y;
        p.r = (uint8_t)(GRAD_L_R + (GRAD_R_R - GRAD_L_R) * t);
        p.g = (uint8_t)(GRAD_L_G + (GRAD_R_G - GRAD_L_G) * t);
        p.b = (uint8_t)(GRAD_L_B + (GRAD_R_B - GRAD_L_B) * t);
        p.vx = (random(-30, 31) / 10.0f) + ((p.x - 400.0f) / 160.0f);
        p.vy = random(-45, 8) / 10.0f;
        p.delay = (uint16_t)(t * 22.0f + random(0, 6));
      }
    }
  }

  // полоса анимации
  lgfx::LGFX_Sprite band(&gfx);
  band.setPsram(true);
  band.setColorDepth(16);
  if (!band.createSprite(800, BAND_H)) { if (pt) heap_caps_free(pt); return; }
  band.fillSprite(0);
  gfx.fillRect(0, BAND_Y, 800, BAND_H, 0);

  const int subY = ty + th + 14;
  auto drawSubtitle = [&](float a) {
    if (!subtitle || a <= 0.02f) return;
    int r = (int)(160 * a), g = (int)(170 * a), b = (int)(185 * a);
    band.setFont(F_J28);
    band.setTextColor(RGB16(r, g, b), 0x0000);
    band.setTextDatum(textdatum_t::top_center);
    band.drawString(subtitle, 400, subY);
  };

  // 4) выплывает снизу
  uint32_t next = millis();
  const uint32_t riseMs = 1100;
  uint32_t t0 = millis();
  for (;;) {
    uint32_t el = millis() - t0;
    float k = el >= riseMs ? 1.0f : easeOutCubic((float)el / riseMs);
    int y = (int)(BAND_H + (ty - BAND_H) * k);
    band.fillSprite(0);
    cs.pushSprite(&band, tx, y);
    band.pushSprite(0, BAND_Y);
    if (el >= riseMs) break;
    frameWait(next, 33);
  }

  // 5) подпись проявляется и пауза
  t0 = millis();
  for (;;) {
    uint32_t el = millis() - t0;
    float a = el >= 600 ? 1.0f : (float)el / 600.0f;
    band.fillSprite(0);
    cs.pushSprite(&band, tx, ty);
    drawSubtitle(a);
    band.pushSprite(0, BAND_Y);
    if (el >= 1300) break;
    frameWait(next, 33);
  }

  // 6) распад на пиксели
  const int FR = 52;
  for (int fr = 0; fr < FR; fr++) {
    band.fillSprite(0);
    drawSubtitle(1.0f - fr / 14.0f);
    for (int i = 0; i < np; i++) {
      Particle& p = pt[i];
      if (fr >= p.delay) {
        p.x += p.vx;
        p.y += p.vy;
        p.vy += 0.09f;
      }
      float life = 1.0f;
      if (fr >= p.delay) life = 1.0f - (float)(fr - p.delay) / (float)(FR - p.delay);
      if (life <= 0.02f) continue;
      int px = (int)p.x, py = (int)p.y - 0;
      if (px < 0 || px > 797 || py < 0 || py > BAND_H - 3) continue;
      band.fillRect(px, py, 2, 2, RGB16((int)(p.r * life), (int)(p.g * life), (int)(p.b * life)));
    }
    band.pushSprite(0, BAND_Y);
    frameWait(next, 33);
  }

  gfx.fillRect(0, BAND_Y, 800, BAND_H, 0);
  if (pt) heap_caps_free(pt);
}


// =====================================================================
// СЦЕНАРИЙ 2: название выплывает снизу, за ним по очереди три строки;
// потом строки осыпаются пикселями вниз (с последней), а название взрывается.
// =====================================================================
#ifndef SPLASH_LINE1
#define SPLASH_LINE1 "Метеостанция"
#endif
#ifndef SPLASH_LINE3
#define SPLASH_LINE3 "Часы"
#endif

#define F_J32B (&lgfx::fonts::lgfxJapanGothic_32)

struct Layer {
  lgfx::LGFX_Sprite* cs = nullptr;
  int w = 0, h = 0, x = 0, y = 0;     // x, y - конечное положение на экране
  Particle* pt = nullptr;
  int np = 0;
};

static void freeLayer(Layer& L) {
  if (L.cs) { L.cs->deleteSprite(); delete L.cs; L.cs = nullptr; }
  if (L.pt) { heap_caps_free(L.pt); L.pt = nullptr; }
  L.np = 0;
}

// Рисует надпись в цветной спрайт (градиент слева направо) и готовит частицы (блоки 2x2).
static bool makeLayer(Layer& L, const char* text, const lgfx::IFont* f, int y,
                      int r0, int g0, int b0, int r1, int g1, int b1) {
  gfx.setFont(f);
  const int w = gfx.textWidth(text) + 8;
  const int h = gfx.fontHeight() + 4;

  lgfx::LGFX_Sprite mk(&gfx);
  mk.setPsram(true);
  mk.setColorDepth(8);
  if (!mk.createSprite(w, h)) return false;
  mk.fillSprite(0);
  mk.setFont(f);
  mk.setTextColor(0xFFFF, 0x0000);
  mk.setTextDatum(textdatum_t::top_left);
  mk.drawString(text, 4, 2);
  const uint8_t* mb = (const uint8_t*)mk.getBuffer();

  L.cs = new lgfx::LGFX_Sprite(&gfx);
  L.cs->setPsram(true);
  L.cs->setColorDepth(16);
  if (!L.cs->createSprite(w, h)) { freeLayer(L); return false; }
  L.cs->fillSprite(0);
  L.w = w; L.h = h; L.y = y; L.x = (800 - w) / 2;

  int cnt = 0;
  for (int yy = 0; yy < h; yy++) {
    for (int xx = 0; xx < w; xx++) {
      uint8_t v = mb[yy * w + xx];
      if (!v) continue;
      int cov = ((v >> 5) & 7) * 255 / 7;
      float t = (float)xx / (float)w;
      int r = (int)((r0 + (r1 - r0) * t) * cov / 255);
      int g = (int)((g0 + (g1 - g0) * t) * cov / 255);
      int b = (int)((b0 + (b1 - b0) * t) * cov / 255);
      L.cs->drawPixel(xx, yy, RGB16(r, g, b));
      if (cov > 60 && !(xx & 1) && !(yy & 1)) cnt++;
    }
  }
  L.pt = (Particle*)heap_caps_malloc(sizeof(Particle) * (cnt > 0 ? cnt : 1), MALLOC_CAP_SPIRAM);
  if (!L.pt) { freeLayer(L); return false; }
  for (int yy = 0; yy < h; yy += 2) {
    for (int xx = 0; xx < w; xx += 2) {
      uint8_t v = mb[yy * w + xx];
      if (!v) continue;
      int cov = ((v >> 5) & 7) * 255 / 7;
      if (cov <= 60 || L.np >= cnt) continue;
      float t = (float)xx / (float)w;
      Particle& p = L.pt[L.np++];
      p.x = L.x + xx;
      p.y = y + yy;
      p.vx = p.vy = 0;
      p.r = (uint8_t)((r0 + (r1 - r0) * t) * cov / 255);
      p.g = (uint8_t)((g0 + (g1 - g0) * t) * cov / 255);
      p.b = (uint8_t)((b0 + (b1 - b0) * t) * cov / 255);
      p.delay = 0;
    }
  }
  return true;
}

static void bootSequence(const char* title) {
  const char* lines[2] = {SPLASH_LINE1, SPLASH_LINE3};   // без «Погода»

  // шрифт названия: самый крупный из влезающих
  const lgfx::IFont* fs[] = {F_D72, F_D56, F_D40};
  const lgfx::IFont* tf = fs[2];
  for (int i = 0; i < 3; i++) {
    gfx.setFont(fs[i]);
    if (gfx.textWidth(title) <= 740) { tf = fs[i]; break; }
  }
  gfx.setFont(tf);
  const int th = gfx.fontHeight() + 4;
  gfx.setFont(F_J32B);
  const int lh = gfx.fontHeight() + 4;
  const int pitch = lh + 10;
  int total = th + 18 + 2 * pitch - 10;
  int y0 = (480 - total) / 2;
  if (y0 < 8) y0 = 8;

  Layer L[3];
  bool ok = makeLayer(L[0], title, tf, y0, 90, 200, 255, 250, 205, 90);
  static const int C[3][6] = {{110, 190, 240, 150, 215, 255},    // Метеостанция - голубой
                              {120, 220, 150, 190, 245, 170},    // Погода - зелёный
                              {250, 205, 90, 255, 160, 110}};    // Часы - янтарный
  for (int i = 0; i < 2 && ok; i++)
    ok = makeLayer(L[i + 1], lines[i], F_J32B, y0 + th + 18 + i * pitch, C[i][0], C[i][1], C[i][2], C[i][3], C[i][4], C[i][5]);

  lgfx::LGFX_Sprite band(&gfx);
  band.setPsram(true);
  band.setColorDepth(16);
  if (ok) ok = band.createSprite(800, 480);
  if (!ok) { for (int i = 0; i < 3; i++) freeLayer(L[i]); return; }

  // начальные скорости: строки (1..3) осыпаются вниз, название (0) взрывается
  const float cx = L[0].x + L[0].w / 2.0f, cy = L[0].y + L[0].h / 2.0f;
  for (int i = 0; i < 3; i++) {
    for (int k = 0; k < L[i].np; k++) {
      Particle& p = L[i].pt[k];
      if (i == 0) {
        float a = random(0, 6283) / 1000.0f;
        float sp = random(30, 150) / 10.0f;
        p.vx = cosf(a) * sp + (p.x - cx) / 90.0f;
        p.vy = sinf(a) * sp + (p.y - cy) / 90.0f;
        p.delay = (uint16_t)random(0, 5);
      } else {
        p.vx = random(-8, 9) / 10.0f;
        p.vy = random(0, 16) / 10.0f;
        float t = (p.x - L[i].x) / (float)L[i].w;
        p.delay = (uint16_t)(t * 10.0f + random(0, 8));
      }
    }
  }

  gfx.fillScreen(0);
  uint32_t next = millis();

  // 1) выплывание: название, затем Метеостанция, Веб-радио, Часы
  const uint32_t startMs[3] = {0, 1000, 1500};
  const uint32_t durMs[3]   = {1050, 650, 650};
  const uint32_t riseTotal = 2000 + 650;
  uint32_t t0 = millis();
  for (;;) {
    uint32_t el = millis() - t0;
    band.fillSprite(0);
    for (int i = 0; i < 3; i++) {
      if (el < startMs[i]) continue;
      float pr = el >= startMs[i] + durMs[i] ? 1.0f : (float)(el - startMs[i]) / durMs[i];
      int y = (int)(480 + (L[i].y - 480) * easeOutCubic(pr));
      L[i].cs->pushSprite(&band, L[i].x, y);
    }
    band.pushSprite(0, 0);
    if (el >= riseTotal) break;
    frameWait(next, 33);
  }
  delay(800);
  next = millis();

  // 2) осыпание строк (с последней) и взрыв названия
  const int startFr[3] = {40, 24, 12};
  const int EXP_FR = 46;
  const int totalFr = 92;
  for (int fr = 0; fr < totalFr; fr++) {
    band.fillSprite(0);
    for (int i = 0; i < 3; i++) {
      if (fr < startFr[i]) { L[i].cs->pushSprite(&band, L[i].x, L[i].y); continue; }
      const int f = fr - startFr[i];
      for (int k = 0; k < L[i].np; k++) {
        Particle& p = L[i].pt[k];
        float life = 1.0f;
        if (f >= p.delay) {
          if (i == 0) {
            p.x += p.vx; p.y += p.vy;
            p.vx *= 0.985f; p.vy *= 0.985f;
            life = 1.0f - (float)(f - p.delay) / (float)EXP_FR;
            if (life <= 0.03f) continue;
          } else {
            p.vy += 0.55f;
            p.x += p.vx; p.y += p.vy;
          }
        }
        int px = (int)p.x, py = (int)p.y;
        if (px < 0 || px > 797 || py < 0 || py > 477) continue;
        band.fillRect(px, py, 2, 2, RGB16((int)(p.r * life), (int)(p.g * life), (int)(p.b * life)));
      }
    }
    band.pushSprite(0, 0);
    frameWait(next, 33);
  }

  gfx.fillScreen(0);
  for (int i = 0; i < 3; i++) freeLayer(L[i]);
}

void uiBootAnim(const char* title, const char* subtitle) {
#if SPLASH_STYLE == 1
  (void)subtitle;
  bootSequence(title);
#else
  bootClassic(title, subtitle);
#endif
}
