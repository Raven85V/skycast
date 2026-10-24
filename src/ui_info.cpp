#include "ui_info.h"
#include "lgfx_crowpanel.h"
#include "settings.h"
#include "icons.h"
#include "ui.h"
#include "ui_clock.h"
#include "ui_anim.h"
#include "wsensors.h"
#include "sensors.h"
#include "moon.h"
#include <math.h>
#include <string.h>

// Второй экран в духе глянцевой настольной станции: чёрный фон, цветные
// семисегментные показания, шкала давления, компас ветра, дата и фаза луны.

#define RGB16(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

static const uint16_t C_BG   = 0x0000;                 // полностью чёрный фон
static const uint16_t C_LINE = RGB16(34, 42, 54);
static const uint16_t C_DIM  = RGB16(132, 146, 164);
static const uint16_t C_WHITE= RGB16(236, 244, 255);
static const uint16_t C_BLUE = RGB16(74, 200, 255);
static const uint16_t C_GREEN= RGB16(124, 246, 116);
static const uint16_t C_YEL  = RGB16(255, 232, 74);
static const uint16_t C_OR   = RGB16(255, 140, 40);

#define F_J16 (&lgfx::fonts::lgfxJapanGothic_16)
#define F_J20 (&lgfx::fonts::lgfxJapanGothic_20)
#define F_J24 (&lgfx::fonts::lgfxJapanGothic_24)
#define F_J32 (&lgfx::fonts::lgfxJapanGothic_32)
#define F_D24 (&lgfx::fonts::DejaVu24)
#define F_D40 (&lgfx::fonts::DejaVu40)
#define F_D56 (&lgfx::fonts::DejaVu56)

using lgfx::textdatum_t;

// ---------- геометрия ----------
static const int COL1 = 8,   COL1_W = 236;     // прогноз и давление
static const int COL2 = 254, COL2_W = 258;     // часы, дом, улица
static const int COL3 = 522, COL3_W = 270;     // компас, дата, луна
static const int COMP_CX = 676, COMP_CY = 244, COMP_R = 64;

static Weather wx;
static bool    hasWx = false;
static int     lastMinute = -1;

static float   hist[16];
static int     histN = 0;

static const char* WD_FULL[] = {"воскресенье", "понедельник", "вторник", "среда",
                                "четверг", "пятница", "суббота"};
static const char* MONTH_GEN[] = {"января", "февраля", "марта", "апреля", "мая", "июня",
                                  "июля", "августа", "сентября", "октября", "ноября", "декабря"};
static const char* COMPASS[] = {"С", "СВ", "В", "ЮВ", "Ю", "ЮЗ", "З", "СЗ"};
static const char* WIND_SHORT[] = {"С", "СВ", "В", "ЮВ", "Ю", "ЮЗ", "З", "СЗ"};

static void txt(const char* s, int x, int y, const lgfx::IFont* f, uint16_t fg, textdatum_t d) {
  gfx.setFont(f);
  gfx.setTextColor(fg, C_BG);
  gfx.setTextDatum(d);
  gfx.drawString(s, x, y);
}

static void label(const char* s, int x, int y, uint16_t col = C_DIM) {
  txt(s, x, y, F_J16, col, textdatum_t::top_left);
}

// ---------- восход / закат ----------
// Солнце над горизонтом и две стрелки: вверх - восход (жёлтый), вниз - закат (оранжевый)
static void drawSunArrow(int x, int y, int s, bool up, uint16_t col) {
  gfx.fillArc(x + s / 2, y + s * 3 / 4, s / 3, s / 3 - 1, 180, 360, col);
  gfx.drawFastHLine(x + 2, y + s * 3 / 4, s - 4, col);
  gfx.drawFastHLine(x + s / 5, y + s * 3 / 4 + 5, s * 3 / 5, col);
  gfx.drawFastHLine(x + s / 3, y + s * 3 / 4 + 10, s / 3, col);
  // лучи
  gfx.drawLine(x + s / 2, y + s / 8, x + s / 2, y + s / 5, col);
  gfx.drawLine(x + s / 6, y + s / 4, x + s / 4, y + s / 3, col);
  gfx.drawLine(x + s * 5 / 6, y + s / 4, x + s * 3 / 4, y + s / 3, col);
  // стрелки по бокам
  for (int k = 0; k < 2; k++) {
    const int ax = x + (k == 0 ? s / 5 : s * 4 / 5) - 4;
    const int top = y, bot = y + s / 3 + 2;
    const int stemH = (bot - top) * 3 / 5;
    if (up) {
      gfx.fillRect(ax, top + stemH, 8, bot - top - stemH, col);
      gfx.fillTriangle(ax - 5, top + stemH + 1, ax + 13, top + stemH + 1, ax + 4, top, col);
    } else {
      gfx.fillRect(ax, top, 8, stemH, col);
      gfx.fillTriangle(ax - 5, top + stemH, ax + 13, top + stemH, ax + 4, bot, col);
    }
  }
}

// ---------- компас ----------

// ---------- шкала давления ----------
static void drawPressHistory(int x, int y, int w, int h) {
  if (histN < 3) return;
  float lo = hist[0], hi = hist[0];
  for (int i = 0; i < histN; i++) {
    if (hist[i] < lo) lo = hist[i];
    if (hist[i] > hi) hi = hist[i];
  }
  if (hi - lo < 1.0f) hi = lo + 1.0f;
  const int bw = w / 16 - 3;
  for (int i = 0; i < histN && i < 16; i++) {
    const int si = histN - 1 - i;                         // слева самое старое
    const int bh = 4 + (int)((hist[si] - lo) / (hi - lo) * (h - 6));
    const int bx = x + i * (w / 16);
    gfx.fillRect(bx, y + h - bh, bw, bh, (i == histN - 1) ? C_WHITE : C_LINE);
  }
  gfx.drawFastHLine(x, y + h + 2, w, C_LINE);
}

// Крупное показание: температура слева (при длинном числе шрифт меньше), влажность справа
static void bigReading(int y, float t, float hum, uint16_t col, bool forceBig = false) {
  char b[24];
  snprintf(b, sizeof(b), "%+.1f", t);
  const lgfx::IFont* f = F_D56;
  gfx.setFont(f);
  int tw = gfx.textWidth(b);
  if (tw > COL2_W - 92) { f = F_D40; gfx.setFont(f); tw = gfx.textWidth(b); }
  if (forceBig && f == F_D40 && tw <= COL2_W - 40) { f = F_D56; gfx.setFont(f); tw = gfx.textWidth(b); }
  gfx.setTextColor(col, C_BG);
  gfx.setTextDatum(textdatum_t::top_left);
  gfx.drawString(b, COL2 + 4, y);
  const int ry = y + (f == F_D56 ? 22 : 16);
  gfx.drawCircle(COL2 + tw + 12, ry, 5, col);
  gfx.drawCircle(COL2 + tw + 12, ry, 4, col);
  if (!isnan(hum) && hum > 0) {
    snprintf(b, sizeof(b), "%d%%", (int)lroundf(hum));
    gfx.setFont(F_J32);
    gfx.setTextColor(col, C_BG);
    gfx.setTextDatum(textdatum_t::top_right);
    gfx.drawString(b, COL2 + COL2_W - 6, y + 10);
  }
}

// ---------- часы ----------
static void drawClockArea(const tm* t) {
  gfx.fillRect(COL2, 0, COL2_W, 112, C_BG);
  if (!t) return;
  uiClockDrawTime(COL2 + 30, 10, 84, t->tm_hour, t->tm_min, C_WHITE, -1);  // по центру колонки
}

void uiInfoOpen() {
  gfx.fillScreen(C_BG);

  gfx.drawFastVLine(COL1 + COL1_W + 4, 8, 464, C_LINE);
  gfx.drawFastVLine(COL2 + COL2_W + 4, 8, 464, C_LINE);

  tm now;
  const bool ok = getLocalTime(&now, 0);
  lastMinute = ok ? now.tm_min : -1;

  // ---- прогноз: одна иконка, как на главном экране (вдвое крупнее) ----
  {
    const char* wcode = (hasWx && wx.hasCur) ? wx.cur.icon : "01d";
    if (g_iconStyle != 0 || uiAnimFrames(uiAnimNameFor(wcode)) == 0) {   // кадры - рисует аниматор
      if (!uiDrawPhaseIcon(wcode, COL1 + 2, 26)) uiDrawWeatherIcon2x(wcode, COL1 + 2, 26);
    }
  }
  if (hasWx && wx.dayCount > 0) {
    char b[32];
    snprintf(b, sizeof(b), "%+.0f ... %+.0f", wx.days[0].tmin, wx.days[0].tmax);
    txt(b, COL1 + 2, 156, F_D40, C_WHITE, textdatum_t::top_left);
  }

  // ---- давление только в мм ----
  label("ДАВЛЕНИЕ", COL1, 204);
  {
    int pressMm = 0;
    if (detectedSensor != SensorType::NONE && sensorFresh())
      pressMm = (int)lroundf(homePressure * 0.750062f);   // с датчика станции, гПа -> мм
    else if (hasWx && wx.hasCur)
      pressMm = wx.cur.pressureMmHg;                      // нет датчика - из погоды
    if (pressMm > 0) {
    char b[24];
    snprintf(b, sizeof(b), "%d", pressMm);
    gfx.setFont(F_D40);
    gfx.setTextColor(C_WHITE, C_BG);
    gfx.setTextDatum(textdatum_t::top_left);
    gfx.drawString(b, COL1, 224);
    const int tw = gfx.textWidth(b);
    txt("мм", COL1 + tw + 8, 242, F_J20, C_DIM, textdatum_t::top_left);
    }
  }
  label("ИСТОРИЯ", COL1, 280);
  drawPressHistory(COL1, 300, COL1_W, 52);
  if (hasWx && wx.dayCount > 0) {
    label("ВЕРОЯТНОСТЬ", COL1, 366);
    char pb[16];
    snprintf(pb, sizeof(pb), "%d", wx.popNow ? wx.popNow : (wx.dayCount ? wx.days[0].pop : 0));
    gfx.setFont(F_D40);
    gfx.setTextColor(C_BLUE, C_BG);
    gfx.setTextDatum(textdatum_t::top_left);
    gfx.drawString(pb, COL1, 408);
    const int pw = gfx.textWidth(pb);
    txt("%", COL1 + pw + 8, 402, F_J20, C_BLUE, textdatum_t::top_left);
  }

  // ---- часы ----
  drawClockArea(ok ? &now : nullptr);

  // ---- дом ----
  gfx.drawFastHLine(COL2, 118, COL2_W, C_LINE);
  label("ДОМ", COL2 + 4, 126, C_DIM);
  float ht = NAN, hh = NAN, hp = NAN;
  {
    float t0 = NAN, h0 = NAN, p0 = NAN;
    // температура и влажность - из тех же источников, что на экране «Дома»
    if (wsensorsPick(wsensorsSrc(0), t0, h0, p0)) ht = t0;
    float t1 = NAN, h1 = NAN, p1 = NAN;
    if (wsensorsPick(wsensorsSrc(1), t1, h1, p1)) hh = h1;
    if (isnan(ht) || isnan(hh)) {   // нет выбранных - берём «дома», затем «основной»
      float a = NAN, b = NAN, c = NAN;
      if (wsensorsHome(a, b, c) || wsensorsMain(a, b, c)) {
        if (isnan(ht)) ht = a;
        if (isnan(hh)) hh = b;
      }
    }
  }
  if (isnan(ht) && detectedSensor != SensorType::NONE && sensorFresh()) {
    ht = homeTemp;              // датчик самой станции
    hh = homeHumidity;
  }
  if (!isnan(ht)) {
    bigReading(150, ht, hh, C_BLUE, true);   // размер числа как у «УЛИЦЫ»
  } else {
    txt("нет данных", COL2 + 4, 164, F_J20, C_DIM, textdatum_t::top_left);
  }

  // ---- улица ----
  gfx.drawFastHLine(COL2, 250, COL2_W, C_LINE);
  label("УЛИЦА", COL2 + 4, 258, C_DIM);
  if (hasWx && wx.hasCur) {
    { float wt = NAN, wh = NAN, wp = NAN;
      const bool okM = wsensorsMain(wt, wh, wp);   // основной датчик Wi-Fi
      if (okM && !isnan(wt))
        bigReading(282, wt, isnan(wh) ? (float)wx.cur.humidity : wh, C_GREEN);
      else
        bigReading(282, wx.cur.temp, (float)wx.cur.humidity, C_GREEN); }   // нет датчика - из интернета
    char b[48];
    snprintf(b, sizeof(b), "ощущается %+.1f", wx.cur.feels);
    txt(b, COL2 + 4, 360, F_J20, C_DIM, textdatum_t::top_left);
  } else {
    txt("нет данных", COL2 + 4, 294, F_J20, C_DIM, textdatum_t::top_left);
  }
  gfx.drawFastHLine(COL2, 394, COL2_W, C_LINE);
  if (hasWx && wx.hasCur) {
    char b[64];
    snprintf(b, sizeof(b), "облачность %d %%", wx.cur.clouds);
    txt(b, COL2 + 4, 404, F_J20, C_DIM, textdatum_t::top_left);
    snprintf(b, sizeof(b), "ветер %.1f м/с %s", wx.cur.wind, WIND_SHORT[((wx.cur.windDeg + 22) / 45) % 8]);
    txt(b, COL2 + 4, 434, F_J16, C_DIM, textdatum_t::top_left);
  }

  // ---- компас ----
  // большая фаза луны вместо компаса ветра
  {
    const time_t ts2 = ok ? mktime(&now) : time(nullptr);
    const double syn2 = 29.530588853;
    double ph2 = fmod(((double)ts2 - 947182440.0) / 86400.0 / syn2, 1.0);
    if (ph2 < 0) ph2 += 1.0;
    const int SZ = 264;                       // крупный размер
    static uint16_t* mbuf = nullptr;
    if (!mbuf) mbuf = (uint16_t*)heap_caps_malloc(SZ * SZ * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (mbuf && moonCount() > 0) {
      int idx2 = (int)lroundf((ph2 + 0.5f) * (float)moonCount()) % moonCount();   // набор начинается с полной луны
      if (idx2 < 0) idx2 += moonCount();
      const uint16_t* src2 = moonFrame(idx2);
      for (int y2 = 0; y2 < SZ; y2++) {
        const int sy2 = y2 * moonH() / SZ;
        for (int x2 = 0; x2 < SZ; x2++) mbuf[y2 * SZ + x2] = src2[sy2 * moonW() + x2 * moonW() / SZ];
      }
      bool oldSwap = gfx.getSwapBytes();
      gfx.setSwapBytes(true);
      gfx.pushImage(COL3 + COL3_W / 2 - SZ / 2, 108, SZ, SZ, mbuf);
      gfx.setSwapBytes(oldSwap);
    }
    char pb2[16];
    snprintf(pb2, sizeof(pb2), "%d %%", (int)lroundf((1.0f - cosf((float)ph2 * 2.0f * (float)M_PI)) * 50.0f));
    txt(pb2, COL3 + COL3_W / 2, 108 + SZ + 2, F_J20, C_DIM, textdatum_t::top_center);
  }

  // ---- дата: число крупно, месяц рядом, день недели полностью ----
  if (ok) {
    char b[16];
    snprintf(b, sizeof(b), "%d", now.tm_mday);
    gfx.setFont(F_D56);
    gfx.setTextColor(C_YEL, C_BG);
    gfx.setTextDatum(textdatum_t::top_left);
    gfx.drawString(b, COL3 + 2, 18);
    const int nw = gfx.textWidth(b);
    gfx.setFont(F_J32);
    const int mw = gfx.textWidth(MONTH_GEN[now.tm_mon]);
    const bool small = (nw + 12 + mw) > (COL3_W - 6);
    txt(MONTH_GEN[now.tm_mon], COL3 + nw + 12, small ? 34 : 30,
        small ? F_J24 : F_J32, C_YEL, textdatum_t::top_left);
    txt(WD_FULL[now.tm_wday], COL3 + 2, 82, F_J24, C_DIM, textdatum_t::top_left);
  }

  // ---- луна и рядом восход/закат ----
  {
    const time_t ts = ok ? mktime(&now) : time(nullptr);
    const double syn = 29.530588853;
    double ph = fmod(((double)ts - 947182440.0) / 86400.0 / syn, 1.0);
    if (ph < 0) ph += 1.0;
    // восход и закат с иконками
    if (hasWx && wx.hasCur && wx.cur.sunrise && wx.cur.sunset) {
      tm a, s;
      localtime_r(&wx.cur.sunrise, &a);
      localtime_r(&wx.cur.sunset, &s);
      char t1[16], t2[16];
      snprintf(t1, sizeof(t1), "%02d:%02d", a.tm_hour, a.tm_min);
      snprintf(t2, sizeof(t2), "%02d:%02d", s.tm_hour, s.tm_min);
      // восход и закат - в одну линию
      drawSunArrow(COL3 + 12, 412, 62, true, C_YEL);
      txt(t1, COL3 + 74, 430, F_J20, C_WHITE, textdatum_t::top_left);
      drawSunArrow(COL3 + 132, 412, 62, false, C_OR);
      txt(t2, COL3 + 194, 430, F_J20, C_WHITE, textdatum_t::top_left);
    }
  }
}

void uiInfoTick(const tm* t) {
  if (!t) return;
  if (t->tm_min == lastMinute) return;
  lastMinute = t->tm_min;
  drawClockArea(t);
}

void uiInfoSetWeather(const Weather& w) {
  wx = w;
  hasWx = w.hasCur || w.dayCount > 0;

  // истории ещё нет - берём готовую из интернета (Open-Meteo, без ключа)
  static uint32_t lastHistTry = 0;
  if (histN < 8 && (uint32_t)(millis() - lastHistTry) > 60000UL) {
    lastHistTry = millis();
    const int got = weatherFetchPressureHistory(hist, 16);
    if (got > histN) histN = got;
    Serial.printf("История: получено %d точек\n", got);
  }
  if (w.hasCur) {
    const float p = (float)w.cur.pressureMmHg;
    if (histN == 0 || fabsf(p - hist[0]) > 0.4f) {
      for (int i = 15; i > 0; i--) hist[i] = hist[i - 1];   // hist[0] - самое свежее
      hist[0] = p;
      if (histN < 16) histN++;
    }
  }
}


// Анимация иконки прогноза на втором экране (вызывать из loop в режиме INFO)
void uiInfoAnimTick() {
  if (g_iconStyle != 0) return;   // анимация только для пастельного стиля
  static uint32_t lastMs = 0;
  static char lastCode[4] = "";
  const uint32_t now = millis();
  if (now - lastMs < 160) return;
  lastMs = now;
  const char* code = (hasWx && wx.hasCur) ? wx.cur.icon : "01d";
  const char* nm = uiAnimNameFor(code);
  if (uiAnimFrames(nm) <= 0) return;          // нет кадров - остаётся статичная
  if (strcmp(lastCode, code) != 0) {
    strlcpy(lastCode, code, sizeof(lastCode));
    gfx.fillRect(COL1 + 2, 26, 240, 116, C_BG);   // не затирать строку +2...+3 под иконкой
  }
  uiAnimDraw(nm, COL1 + 2 + 36, 26 + 18, 128, 92);
}
