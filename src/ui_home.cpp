#include "ui_home.h"
#include "lgfx_crowpanel.h"
#include "settings.h"
#include "sensors.h"
#include "wsensors.h"
#include <math.h>
#include <string.h>

// Экран «Дома»: три карточки показателей (температура, влажность, давление),
// источник по каждому задаётся в веб-интерфейсе, ниже - график за период.

#define RGB16(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

static const uint16_t C_BG   = 0x0000;
static const uint16_t C_TEXT = RGB16(255, 255, 255);
static const uint16_t C_DIM  = RGB16(170, 180, 196);
static const uint16_t C_LINE = RGB16(58, 70, 88);
static const uint16_t C_CARD = RGB16(18, 24, 36);
static const uint16_t C_METRIC[3] = {RGB16(255, 176, 64), RGB16(41, 182, 246), RGB16(102, 187, 106)};

#define F_J16 (&lgfx::fonts::lgfxJapanGothic_16)
#define F_J20 (&lgfx::fonts::lgfxJapanGothic_20)
#define F_J24 (&lgfx::fonts::lgfxJapanGothic_24)
#define F_J32 (&lgfx::fonts::lgfxJapanGothic_32)
#define F_D56 (&lgfx::fonts::DejaVu56)

using lgfx::textdatum_t;

// ---------- состояние ----------
static LGFX_Sprite spr(&gfx);
static bool       sprReady = false;
static uint32_t   lastDraw = 0;

enum { M_TEMP = 0, M_HUM = 1, M_PRES = 2 };
static int metric = M_TEMP;

static const char* METRIC_NAME[3]  = {"Температура", "Влажность", "Давление"};
static const char* PERIOD_NAME[3]  = {"3 ч", "6 ч", "12 ч"};
static const int   PERIOD_HOURS[3] = {3, 6, 12};
static int periodIdx = 2;

// ---------- геометрия ----------
static const int CARD_Y = 76, CARD_H = 150, CARD_W = 240, CARD_GAP = 16, CARD_X0 = 24;
static const int GR_X = 60, GR_Y = 278, GR_W = 716, GR_H = 132;

// ---------- помощники ----------
static void text(const char* s, int x, int y, const lgfx::IFont* f, uint16_t col, textdatum_t d) {
  spr.setFont(f);
  spr.setTextColor(col, C_BG);
  spr.setTextDatum(d);
  spr.drawString(s, x, y);
}

static void textCard(const char* s, int x, int y, const lgfx::IFont* f, uint16_t col, textdatum_t d) {
  spr.setFont(f);
  spr.setTextColor(col, C_CARD);
  spr.setTextDatum(d);
  spr.drawString(s, x, y);
}

static void drawMetricIcon(int m, int x, int y, uint16_t col) {
  if (m == M_TEMP) {
    spr.drawRoundRect(x + 6, y + 2, 12, 30, 6, col);
    spr.drawRoundRect(x + 7, y + 3, 10, 28, 5, col);
    spr.fillCircle(x + 12, y + 34, 8, col);
    spr.fillCircle(x + 12, y + 34, 4, C_BG);
    spr.fillRect(x + 10, y + 10, 4, 20, col);
  } else if (m == M_HUM) {
    spr.fillTriangle(x + 12, y + 2, x + 2, y + 22, x + 22, y + 22, col);
    spr.fillCircle(x + 12, y + 24, 10, col);
    spr.fillCircle(x + 9, y + 22, 4, C_BG);
  } else {
    spr.drawCircle(x + 12, y + 18, 16, col);
    spr.drawCircle(x + 12, y + 18, 15, col);
    spr.drawLine(x + 12, y + 18, x + 21, y + 9, col);
    spr.drawLine(x + 12, y + 18, x + 21, y + 10, col);
    spr.fillCircle(x + 12, y + 18, 3, col);
  }
}

static bool homeValue(int m, float& v) {
  float t = NAN, h = NAN, p = NAN;
  if (!wsensorsPick(wsensorsSrc(m), t, h, p)) return false;
  v = (m == 0) ? t : (m == 1) ? h : p;
  return !isnan(v);
}

// ---------- карточка ----------
static void drawCard(int i) {
  const int x = CARD_X0 + i * (CARD_W + CARD_GAP);
  const bool sel = (i == metric);

  spr.fillRoundRect(x, CARD_Y, CARD_W, CARD_H, 14, C_CARD);
  spr.drawRoundRect(x, CARD_Y, CARD_W, CARD_H, 14, sel ? C_METRIC[i] : C_LINE);
  if (sel) spr.drawRoundRect(x + 1, CARD_Y + 1, CARD_W - 2, CARD_H - 2, 13, C_METRIC[i]);

  drawMetricIcon(i, x + 14, CARD_Y + 12, sel ? C_METRIC[i] : C_DIM);

  float v = NAN;
  if (!homeValue(i, v)) {
    text("--", x + 10, CARD_Y + 76, F_J32, C_DIM, textdatum_t::top_left);
    return;
  }

  char buf[16];
  if (i == M_TEMP) {
    snprintf(buf, sizeof(buf), "%.1f", v);
    textCard(buf, x + 10, CARD_Y + 76, F_D56, C_METRIC[i], textdatum_t::top_left);
    const int w = spr.textWidth(buf);
    const int cx = x + 10 + w + 12, cy = CARD_Y + 88;
    spr.drawCircle(cx, cy, 6, C_METRIC[i]);
    spr.drawCircle(cx, cy, 5, C_METRIC[i]);
    textCard("C", cx + 12, CARD_Y + 88, F_J32, C_METRIC[i], textdatum_t::top_left);
  } else if (i == M_HUM) {
    snprintf(buf, sizeof(buf), "%d", (int)lroundf(v));
    textCard(buf, x + 10, CARD_Y + 76, F_D56, C_METRIC[i], textdatum_t::top_left);
    const int w = spr.textWidth(buf);
    textCard("%", x + 10 + w + 10, CARD_Y + 88, F_J32, C_METRIC[i], textdatum_t::top_left);
  } else {
    snprintf(buf, sizeof(buf), "%d", (int)lroundf(v * 0.750062f));
    textCard(buf, x + 10, CARD_Y + 76, F_D56, C_METRIC[i], textdatum_t::top_left);
    const int w = spr.textWidth(buf);
    textCard("мм", x + 10 + w + 10, CARD_Y + 88, F_J24, C_METRIC[i], textdatum_t::top_left);
  }
}


// ---------- история показаний (с выбранного источника) ----------
#define HIST_N 288                 // 24 ч по 5 минут
struct HistPt { uint32_t ms; float v; bool ok; };
static HistPt histBuf[3][HIST_N];
static int    histCnt[3] = {0, 0, 0};
static uint32_t lastSample = 0;

static void sampleHistory() {
  const uint32_t now = millis();
  if (lastSample != 0 && now - lastSample < 60000UL) return;   // раз в минуту
  lastSample = now;
  for (int m = 0; m < 3; m++) {
    float v = NAN;
    const bool ok = homeValue(m, v);
    HistPt& p = histBuf[m][histCnt[m] % HIST_N];
    p.ms = now; p.v = v; p.ok = ok;
    if (histCnt[m] < HIST_N) histCnt[m]++;
  }
}

// ---------- график ----------
static void drawGraph() {
  // рамка и линии сетки
  for (int g = 0; g <= 3; g++) {
    const int y = GR_Y + g * GR_H / 3;
    spr.drawFastHLine(GR_X, y, GR_W, C_LINE);
  }
  text("-12 ч", GR_X + 6, GR_Y + GR_H + 8, F_J16, C_DIM, textdatum_t::top_left);
  text("-8 ч", GR_X + GR_W / 3 + 6, GR_Y + GR_H + 8, F_J16, C_DIM, textdatum_t::top_left);
  text("-4 ч", GR_X + 2 * GR_W / 3 + 6, GR_Y + GR_H + 8, F_J16, C_DIM, textdatum_t::top_left);
  text("сейчас", GR_X + GR_W - 6, GR_Y + GR_H + 8, F_J16, C_DIM, textdatum_t::top_right);

  char title[64];
  snprintf(title, sizeof(title), "%s за %d ч", METRIC_NAME[metric], PERIOD_HOURS[periodIdx]);
  text(title, 24, GR_Y - 26, F_J20, C_DIM, textdatum_t::top_left);

  // точки за выбранный период
  const int m = metric;
  const int cnt = histCnt[m];
  const uint32_t now = millis();
  const uint32_t span = (uint32_t)PERIOD_HOURS[periodIdx] * 3600UL * 1000UL;
  float lo = 1e9f, hi = -1e9f;
  int used = 0;
  for (int i = 0; i < cnt; i++) {
    const int idx = (histCnt[m] - 1 - i + HIST_N * 2) % HIST_N;
    const HistPt& p = histBuf[m][idx];
    if (!p.ok) continue;
    if (p.ms == 0 || (now - p.ms) > span) continue;
    if (p.v < lo) lo = p.v;
    if (p.v > hi) hi = p.v;
    used++;
  }
  if (used < 2) {
    text("нет данных", GR_X + GR_W / 2, GR_Y + GR_H / 2, F_J20, C_DIM, textdatum_t::middle_center);
    return;
  }
  if (hi - lo < 0.5f) { hi = lo + 0.5f; }            // чтобы линия не липла к краю

  // подписи шкалы значений
  char vb[16];
  gfx.setFont(F_J16);
  snprintf(vb, sizeof(vb), "%.1f", hi);
  text(vb, GR_X - 8, GR_Y - 4, F_J16, C_DIM, textdatum_t::top_right);
  snprintf(vb, sizeof(vb), "%.1f", lo);
  text(vb, GR_X - 8, GR_Y + GR_H - 8, F_J16, C_DIM, textdatum_t::top_right);

  int px = -1, py = 0;
  for (int i = 0; i < cnt; i++) {
    const int idx = (histCnt[m] - 1 - i + HIST_N * 2) % HIST_N;
    const HistPt& p = histBuf[m][idx];
    if (!p.ok || p.ms == 0) continue;
    const uint32_t ago = now - p.ms;
    if (ago > span) continue;
    const int x = GR_X + GR_W - (int)((uint64_t)ago * (uint64_t)GR_W / span);
    const int y = GR_Y + GR_H - (int)((p.v - lo) / (hi - lo) * (float)GR_H);
    if (px >= 0) spr.drawLine(px, py, x, y, C_METRIC[m]);
    px = x; py = y;
  }
  if (px >= 0) spr.fillCircle(px, py, 3, C_METRIC[m]);
}

// ---------- экран ----------
static void drawAll() {
  if (!sprReady) return;
  spr.fillScreen(C_BG);

  text("Дома", 24, 10, F_J32, C_TEXT, textdatum_t::top_left);
  text("< назад", 776, 22, F_J24, C_DIM, textdatum_t::top_right);
  spr.drawFastHLine(24, 62, 752, C_LINE);

  for (int i = 0; i < 3; i++) drawCard(i);
  drawGraph();

  text("Касание карточки - показатель, графика - период", 400, 456, F_J16, C_LINE, textdatum_t::top_center);
  spr.pushSprite(0, 0);
  lastDraw = millis();
}

void uiHomeOpen() {
  if (!sprReady) {
    spr.setPsram(true);
    spr.setColorDepth(16);
    sprReady = spr.createSprite(800, 480);
    if (!sprReady) return;
  }
  drawAll();
}

void uiHomeTick(const tm*) {
  if (!sprReady) return;
  sampleHistory();
  if (millis() - lastDraw >= 5000) drawAll();
}

bool uiHomeTouch(int x, int y) {
  if (!sprReady) return true;
  if (y < 66) return true;
  if (y >= CARD_Y && y < CARD_Y + CARD_H) {
    for (int i = 0; i < 3; i++) {
      const int cx = CARD_X0 + i * (CARD_W + CARD_GAP);
      if (x >= cx && x < cx + CARD_W) { metric = i; drawAll(); return false; }
    }
  } else if (y >= GR_Y - 30) {
    periodIdx = (periodIdx + 1) % 3;
    drawAll();
  }
  return false;
}

void uiHomeClose() {
  if (sprReady) { spr.deleteSprite(); sprReady = false; }
}
