#include "ui_clock.h"
#include "lgfx_crowpanel.h"
#include "settings.h"
#include "integrations.h"
#include "wsensors.h"
#include "clock_western.h"   // цифры стиля «Западный» (стиль 6)
#include "clock_marquee.h"   // глифы бегущей строки в том же стиле
#include "clock_flip.h"      // цифры-«перекидные карточки» (стиль 7)
#include <esp_heap_caps.h>
#include <math.h>

// ---------- цвета и шрифты ----------
#define RGB16(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))
static const uint16_t C_BG      = 0x0000;
static const uint16_t C_TEXT    = RGB16(255, 255, 255);
static const uint16_t C_DIM     = RGB16(170, 180, 196);
static const uint16_t C_ACC     = RGB16(255, 196, 0);
static const uint16_t C_COLD    = RGB16(66, 196, 255);
static const uint16_t C_DIG     = RGB16(235, 245, 255);   // цифры часов
static const uint16_t C_DIG_OFF = RGB16(30, 38, 52);      // погашенные сегменты (еле видны)
static const uint16_t C_SEC     = RGB16(255, 196, 0);    // секунды
static const uint16_t C_SEC_OFF = RGB16(40, 36, 16);
static const uint16_t C_WEST_SHADOW = RGB16(54, 70, 96);    // тень объёмных цифр
static const uint16_t C_WEST_RIM    = RGB16(150, 168, 190); // светлая обводка вокруг тени

#define F_J16 (&lgfx::fonts::lgfxJapanGothic_16)
#define F_J20 (&lgfx::fonts::lgfxJapanGothic_20)
#define F_J24 (&lgfx::fonts::lgfxJapanGothic_24)
#define F_J32 (&lgfx::fonts::lgfxJapanGothic_32)
#define F_J40 (&lgfx::fonts::lgfxJapanGothic_40)
#define F_SCROLL F_J40      // шрифт бегущей строки (самый крупный из имеющихся с кириллицей)
#define SCROLL_SCALE 1.4f   // увеличение шрифта бегущей строки (1.0 - как было; больше 1.5 не влезет в полосу 68 пикс)
#define F_D72 (&lgfx::fonts::DejaVu72)

using lgfx::textdatum_t;

// ---------- геометрия ----------
static int HY = 80;                                                         // верх основных цифр (зависит от раскладки)
static const int HW = 125, HH = 250, HT = 26, HG = 18, HCW = 50;           // основные цифры
static const int HX = 24;
static int SY = 80;                                                         // верх секунд (зависит от раскладки)
static const int SW = 46, SH = 92, ST = 11;                                 // секунды
static const int SX0 = 664, SX1 = 720;
// Раскладка 1 ("Дата сверху"): дата по центру вверху, часы ниже, секунды по нижней линии часов, над ними день недели
static const int L1_HY = 104;
static const int L1_DATE_CY = 50;                                           // центр строки даты по вертикали
static const int L1_WD_GAP = 8;                                             // зазор между днём недели и секундами
static const int DATE_X = 640, DATE_Y = 190, DATE_W = 150, DATE_H = 170;    // блок дня недели и даты
static const int SCR_Y = 404, SCR_H = 68;                                   // бегущая строка
static const float SCROLL_PX_PER_MS = 0.14f;                                // 140 пикселей в секунду

static const char* WD_SHORT[] = {"ВС", "ПН", "ВТ", "СР", "ЧТ", "ПТ", "СБ"};
static const char* WD_ABBR[]  = {"Вс", "Пн", "Вт", "Ср", "Чт", "Пт", "Сб"};     // для раскладки 1
static const char* WD_FULL[]  = {"воскресенье", "понедельник", "вторник", "среда",
                                 "четверг", "пятница", "суббота"};
static const char* MONTH_GEN[] = {"января", "февраля", "марта", "апреля", "мая", "июня",
                                  "июля", "августа", "сентября", "октября", "ноября", "декабря"};
static const char* WIND_DIR[] = {"С", "СВ", "В", "ЮВ", "Ю", "ЮЗ", "З", "СЗ"};
// Полные названия направлений - для бегущей строки (порядок тот же: 0 = север)
static const char* WIND_FULL[] = {"северный", "северо-восточный", "восточный", "юго-восточный",
                                  "южный", "юго-западный", "западный", "северо-западный"};

// сегменты: бит 0 = a (верх), 1 = b (верхний правый), 2 = c (нижний правый),
// 3 = d (низ), 4 = e (нижний левый), 5 = f (верхний левый), 6 = g (середина)
static const uint8_t SEG_MASK[10] = {
  0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F
};

// ---------- состояние ----------
static int  shownDigit[6];     // 0..3 - ЧЧММ, 4..5 - секунды; -2 = ещё не рисовали
static int  shownColon = -1;
static int  shownDay = -1;
static uint8_t shownFont = 255;   // стиль цифр, которым нарисован экран
static uint8_t shownLayout = 255; // раскладка, по которой нарисован экран
static uint32_t shownColor = 0;   // цвет цифр, которым нарисован экран
static uint8_t shownTextFont = 255; // шрифт текста, которым нарисована дата
static int  colonX = 0;
static int  digX[4];

static lgfx::LGFX_Sprite scr(&gfx);
static bool scrReady = false;

static Weather wxCopy;
static bool    haveWx = false;

#define MAX_LINES 6
#define RX_LINE_BASE 100                 // lineId >= 100: строки приёма (0 MQTT, 1 Народмон, 2 датчики Wi-Fi), цвет и скорость - из integrations
static char     lines[MAX_LINES][300];
static uint8_t  lineId[MAX_LINES];   // индекс строки в настройках g_set.line[] или RX_LINE_BASE + источник
static int      nLines = 0, curLine = 0;
static float    scrollOff = 800;
static uint32_t lastFrame = 0;

static uint16_t rgb16(uint32_t c) { return RGB16((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF); }

// Цвет цифр часов (настройка) и его затемнённый вариант для подписей
static uint16_t clockCol() { return rgb16(g_clockColor); }
static uint16_t dateCol()  { return rgb16(g_dateColor); }
static uint16_t wdCol()    { return rgb16(g_weekdayColor); }
static uint16_t monthCol() { return rgb16(g_monthColor); }
static uint16_t secCol()   { return rgb16(g_secColor); }
static uint16_t clockColDim(int pct) {
  uint16_t c = clockCol();
  int r = ((c >> 11) & 0x1F) * pct / 100;
  int g = ((c >> 5) & 0x3F) * pct / 100;
  int b = (c & 0x1F) * pct / 100;
  return (uint16_t)((r << 11) | (g << 5) | b);
}
// скорость 1..10 -> пикселей в миллисекунду (40..310 пикс/с)
static float speedPxPerMs(int s) {
  if (s < 1) s = 1;
  if (s > 10) s = 10;
  return (40.0f + (s - 1) * 30.0f) / 1000.0f;
}

// ---------- стили цифр (выбор: g_clockFont) ----------
// 0 скруглённые сегменты, 1 шестигранные LED, 2 тонкие, 3 блоки, 4 точечная матрица 5x7

// один сегмент: горизонтальный, если w >= h
static void seg(int x, int y, int w, int h, uint16_t c) {
  const bool horiz = w >= h;
  switch (g_clockFont) {
    case 1: {   // шестигранник: прямоугольник и два треугольника по краям
      if (horiz) {
        int k = h / 2;
        gfx.fillRect(x + k, y, w - 2 * k, h, c);
        gfx.fillTriangle(x, y + k, x + k, y, x + k, y + h - 1, c);
        gfx.fillTriangle(x + w - 1, y + k, x + w - k - 1, y, x + w - k - 1, y + h - 1, c);
      } else {
        int k = w / 2;
        gfx.fillRect(x, y + k, w, h - 2 * k, c);
        gfx.fillTriangle(x + k, y, x, y + k, x + w - 1, y + k, c);
        gfx.fillTriangle(x + k, y + h - 1, x, y + h - k - 1, x + w - 1, y + h - k - 1, c);
      }
      break;
    }
    case 2: {   // тонкий: та же ось, толщина около 40%
      if (horiz) { int nh = h * 2 / 5 < 3 ? 3 : h * 2 / 5; y += (h - nh) / 2; h = nh; }
      else       { int nw = w * 2 / 5 < 3 ? 3 : w * 2 / 5; x += (w - nw) / 2; w = nw; }
      gfx.fillRoundRect(x, y, w, h, (w < h ? w : h) / 2, c);
      break;
    }
    case 3:     // блок: почти прямые углы
      gfx.fillRoundRect(x, y, w, h, 3, c);
      break;
    default: {  // 0: скруглённый (таблетка)
      int r = (w < h ? w : h) / 2;
      gfx.fillRoundRect(x, y, w, h, r, c);
    }
  }
}

// ---------- стиль 6: каллиграфические цифры и дата (clock_western.h) ----------
static uint16_t* westBuf = nullptr;   // общий буфер под глиф в PSRAM (до 125x250)

// Глиф хранится по 2 бита на точку: 0 - фон, 1 - фигура. fitW > 0 - сжать по ширине.
static void drawGlyph(const ClockGlyph& g, int x, int y, uint16_t col, int fitW = 0) {
  if (!westBuf) westBuf = (uint16_t*)heap_caps_malloc(125 * 250 * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!westBuf || !g.data || !g.w || !g.h) return;
  const int dw = (fitW > 0 && fitW < (int)g.w) ? fitW : (int)g.w;
  const int dh = (int)g.h * dw / (int)g.w;
  const uint8_t* src = g.data;
  int n = 0;
  for (int dy = 0; dy < dh; dy++) {
    const int sy = dy * g.h / dh;
    for (int dx = 0; dx < dw; dx++) {
      const int idx = sy * g.w + (dx * g.w / dw);
      const uint8_t v = (src[idx >> 2] >> ((idx & 3) * 2)) & 3;
      westBuf[n++] = (v == 1) ? col : C_BG;
    }
  }
  bool oldSwap = gfx.getSwapBytes();
  gfx.setSwapBytes(true);
  gfx.pushImage(x, y, dw, dh, westBuf);
  gfx.setSwapBytes(oldSwap);
}

static void drawWestDigit(int x, int y, int w, int h, int val, uint16_t face) {
  if (val < 0 || val > 9) { gfx.fillRect(x, y, w, h, C_BG); return; }
  drawGlyph((w >= 100) ? CLOCK_TBIG[val] : CLOCK_TSMALL[val], x, y, face);
}

// Ширина строки из цифр таблицы tbl (с зазором gap между ними)
static int digitsWidth(const char* s, const ClockGlyph* tbl, int gap) {
  int w = 0;
  for (const char* p = s; *p; p++) w += tbl[*p - '0'].w + gap;
  return w > 0 ? w - gap : 0;
}
static void drawDigits(const char* s, const ClockGlyph* tbl, int x, int y, int gap, uint16_t col) {
  for (const char* p = s; *p; p++) {
    const ClockGlyph& g = tbl[*p - '0'];
    drawGlyph(g, x, y, col);
    x += g.w + gap;
  }
}

// Стиль 7: перекидная карточка - светлая плашка, тёмная цифра и перекладина
static void drawFlipDigit(int x, int y, int w, int h, int val) {
  if (val < 0 || val > 9) { gfx.fillRect(x, y, w, h, C_BG); return; }
  if (!westBuf) westBuf = (uint16_t*)heap_caps_malloc(125 * 250 * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!westBuf) return;
  const FlipGlyph& g = (w >= 100) ? FLIP_BIG[val] : FLIP_SMALL[val];
  const uint16_t card = RGB16(232, 232, 232), ink = RGB16(20, 20, 24);
  const int n = g.w * g.h;
  for (int i = 0; i < n; i++) {
    const uint8_t v = (g.data[i >> 2] >> ((i & 3) * 2)) & 3;
    westBuf[i] = (v == 1) ? card : (v == 2) ? ink : C_BG;
  }
  bool oldSwap = gfx.getSwapBytes();
  gfx.setSwapBytes(true);
  gfx.pushImage(x, y, g.w, g.h, westBuf);
  gfx.setSwapBytes(oldSwap);
}
// точечная матрица 5 столбцов x 7 строк
static const uint8_t DOT57[10][7] = {
  {0b01110, 0b10001, 0b10011, 0b10101, 0b11001, 0b10001, 0b01110},
  {0b00100, 0b01100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110},
  {0b01110, 0b10001, 0b00001, 0b00010, 0b00100, 0b01000, 0b11111},
  {0b11110, 0b00001, 0b00001, 0b01110, 0b00001, 0b00001, 0b11110},
  {0b00010, 0b00110, 0b01010, 0b10010, 0b11111, 0b00010, 0b00010},
  {0b11111, 0b10000, 0b11110, 0b00001, 0b00001, 0b10001, 0b01110},
  {0b00110, 0b01000, 0b10000, 0b11110, 0b10001, 0b10001, 0b01110},
  {0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b01000, 0b01000},
  {0b01110, 0b10001, 0b10001, 0b01110, 0b10001, 0b10001, 0b01110},
  {0b01110, 0b10001, 0b10001, 0b01111, 0b00001, 0b00010, 0b01100},
};

static void drawDigitDots(int x, int y, int w, int h, int val, uint16_t on, uint16_t off) {
  const int cw = w / 5, ch = h / 7;
  const int r = ((cw < ch ? cw : ch) * 42) / 100;
  const int x0 = x + (w - 5 * cw) / 2;
  const bool ok = (val >= 0 && val <= 9);
  for (int row = 0; row < 7; row++) {
    for (int col = 0; col < 5; col++) {
      bool lit = ok && ((DOT57[val][row] >> (4 - col)) & 1);
      gfx.fillCircle(x0 + col * cw + cw / 2, y + row * ch + ch / 2, r, lit ? on : off);
    }
  }
}

// val < 0 - все сегменты погашены
static void drawDigit(int x, int y, int w, int h, int t, int val, uint16_t on, uint16_t off) {
  if (g_clockFont == 5) { drawWestDigit(x, y, w, h, val, on); return; }
  if (g_clockFont == 6) { drawFlipDigit(x, y, w, h, val); return; }
  if (g_clockFont == 4) { drawDigitDots(x, y, w, h, val, on, off); return; }
  uint8_t m = (val >= 0 && val <= 9) ? SEG_MASK[val] : 0;
  const int g = 3;                       // зазор между сегментами
  const int hl = w - t - 2 * g;          // длина горизонтального сегмента
  const int vl = h / 2 - t / 2 - 2 * g;  // длина вертикального сегмента
  const int hx = x + t / 2 + g;
  const int yMid = y + h / 2;

  seg(hx, y,               hl, t, (m & 0x01) ? on : off);       // a
  seg(hx, yMid - t / 2,    hl, t, (m & 0x40) ? on : off);       // g
  seg(hx, y + h - t,       hl, t, (m & 0x08) ? on : off);       // d
  seg(x + w - t, y + t / 2 + g, t, vl, (m & 0x02) ? on : off);  // b
  seg(x,         y + t / 2 + g, t, vl, (m & 0x20) ? on : off);  // f
  seg(x + w - t, yMid + g,      t, vl, (m & 0x04) ? on : off);  // c
  seg(x,         yMid + g,      t, vl, (m & 0x10) ? on : off);  // e
}

static void drawDigitIfChanged(int idx, int val) {
  if (shownDigit[idx] == val) return;
  shownDigit[idx] = val;
  if (idx < 4) drawDigit(digX[idx], HY, HW, HH, HT, val, clockCol(), C_DIG_OFF);
  else         drawDigit(idx == 4 ? SX0 : SX1, SY, SW, SH, ST, val, secCol(), C_SEC_OFF);
}

static void drawColon(bool on) {
  int c = on ? 1 : 0;
  if (shownColon == c) return;
  shownColon = c;
  uint16_t col = on ? clockCol() : C_DIG_OFF;
  const int cx = colonX + HCW / 2;
  const int ys[2] = {HY + HH / 3, HY + 2 * HH / 3};
  if (g_clockFont == 5) {                        // западный стиль: точки с тенью
    int d = HT + 4;
    for (int i = 0; i < 2; i++) {
      gfx.fillCircle(cx + 4, ys[i] + 5, d / 2, C_WEST_SHADOW);
      gfx.fillCircle(cx, ys[i], d / 2, col);
    }
    return;
  }
  for (int i = 0; i < 2; i++) {
    int d = HT;                                   // диаметр / сторона точки
    if (g_clockFont == 2) d = HT * 2 / 3;         // тонкий стиль - точки поменьше
    if (g_clockFont == 4) d = HT * 4 / 5;
    if (g_clockFont == 1 || g_clockFont == 3) gfx.fillRect(cx - d / 2, ys[i] - d / 2, d, d, col);
    else gfx.fillCircle(cx, ys[i], d / 2, col);
  }
}

// ---------- раскладка ----------
static void applyLayout() {
  if (g_clockLayout == 1) { HY = L1_HY; SY = HY + HH - SH; }   // секунды - по нижней линии часов
  else                    { HY = 80;    SY = 80; }
  shownLayout = g_clockLayout;
}


// ---- число дня в стиле выбранных часов (для обоих раскладок) ----
static int dayNumW(int day, int h) {
  char b[4];
  snprintf(b, sizeof(b), "%d", day);
  const int n = (int)strlen(b);
  if (g_clockFont == 5) {                        // стиль 6: глифы Gabriola
    const ClockGlyph* tbl = (h >= 60) ? CLOCK_DAYBIG : CLOCK_DAYDIG;
    return digitsWidth(b, tbl, 6);
  }
  if (g_clockFont == 6) return n * FLIP_SMALL[0].w + (n - 1) * 4;   // стиль 7: карточки
  const int dw = h / 2;                          // сегментные стили 1-5
  return n * dw + (n - 1) * (h / 10);
}

static void drawDayNum(int x, int y, int h, int day, uint16_t col) {
  char b[4];
  snprintf(b, sizeof(b), "%d", day);
  if (g_clockFont == 5) {                        // стиль 6
    const ClockGlyph* tbl = (h >= 60) ? CLOCK_DAYBIG : CLOCK_DAYDIG;
    int xx = x;
    for (const char* p = b; *p; p++) {
      const ClockGlyph& g = tbl[*p - '0'];
      drawGlyph(g, xx, y, col);
      xx += g.w + 6;
    }
    return;
  }
  if (g_clockFont == 6) {                        // стиль 7: перекидные карточки
    int xx = x;
    for (const char* p = b; *p; p++) { drawFlipDigit(xx, y, FLIP_SMALL[0].w, FLIP_SMALL[0].h, *p - '0'); xx += FLIP_SMALL[0].w + 4; }
    return;
  }
  const int dw = h / 2, gap = h / 10;            // сегменты и точки
  const int t = dw * 21 / 100 < 4 ? 4 : dw * 21 / 100;
  int xx = x;
  for (const char* p = b; *p; p++) { drawDigit(xx, y, dw, h, t, *p - '0', col, C_DIG_OFF); xx += dw + gap; }
}
static void drawMaskText(const char* s, int x, int y, int targetH, uint16_t col);
static int  maskTextW(const char* s, int targetH);

// раскладка 1: дата вверху ("18 октября") и день недели над секундами
static void drawDateTop(const tm& t) {
  gfx.fillRect(0, 0, 800, HY - 2, C_BG);
  const int nh = (g_clockFont == 6) ? FLIP_SMALL[0].h : 46;   // высота числа в стиле часов
  const int ny = L1_DATE_CY - nh / 2;
  char num[4];
  snprintf(num, sizeof(num), "%d", t.tm_mday);
  const int wnum = dayNumW(t.tm_mday, nh);
  const int mh = 46;
  int wmon;
  if (g_clockFont == 5) {
    wmon = CLOCK_MONTHS[t.tm_mon].w;
  } else if (g_textFont >= 1) {
    wmon = maskTextW(MONTH_GEN[t.tm_mon], mh);
  } else {
    gfx.setFont(F_J40);
    gfx.setTextSize(1.15f);
    wmon = gfx.textWidth(MONTH_GEN[t.tm_mon]);
  }
  const int gap = 10;
  int x = 400 - (wnum + gap + wmon) / 2;

  if (g_clockFont == 5) {                       // стиль 6: глифы Gabriola
    drawDayNum(x, ny, nh, t.tm_mday, dateCol());
    drawGlyph(CLOCK_MONTHS[t.tm_mon], x + wnum + gap, ny, monthCol());
  } else {
    drawDayNum(x, ny, nh, t.tm_mday, dateCol());
    if (g_textFont >= 1) {
      drawMaskText(MONTH_GEN[t.tm_mon], x + wnum + gap, L1_DATE_CY - mh / 2, mh, monthCol());
    } else {
      gfx.setFont(F_J40);
      gfx.setTextSize(1.15f);
      gfx.setTextColor(monthCol(), C_BG);
      gfx.setTextDatum(textdatum_t::middle_left);
      gfx.drawString(MONTH_GEN[t.tm_mon], x + wnum + gap, L1_DATE_CY);
      gfx.setTextSize(1.0f);
    }
  }

  // день недели: блок над секундами, ширина и высота как у цифр секунд
  const int bx = SX0, bw = SX1 + SW - SX0, bh = SH;
  int by = SY - L1_WD_GAP - SH;
  // стиль 7: день недели на прежнем месте (был поднят на 10 px, теперь опущен обратно)
  gfx.fillRect(bx - 4, by, bw + 8, bh, C_BG);
  if (g_clockFont == 5) {
    const ClockGlyph& wd = CLOCK_WDAYS[t.tm_wday];
    drawGlyph(wd, bx + bw / 2 - wd.w / 2, by + (bh - wd.h) / 2, wdCol(), bw);
    return;
  }
  if (g_textFont >= 1) {
    const int th = 40;
    drawMaskText(WD_ABBR[t.tm_wday], bx + (bw - maskTextW(WD_ABBR[t.tm_wday], th)) / 2,
                 by + (bh - th) / 2, th, wdCol());
    return;
  }
  if (g_clockFont == 6) {                       // стиль 7: день недели как в «Классике» - Font J32
    gfx.setFont(F_J32);
    gfx.setTextColor(wdCol(), C_BG);
    gfx.setTextDatum(textdatum_t::middle_center);
    gfx.drawString(WD_ABBR[t.tm_wday], bx + bw / 2, by + bh / 2);
    return;
  }
  gfx.setFont(F_J40);
  float sc = 2.3f;
  for (; sc > 0.8f; sc -= 0.1f) {
    gfx.setTextSize(sc);
    if (gfx.textWidth(WD_ABBR[t.tm_wday]) <= bw && 40.0f * sc <= bh) break;
  }
  gfx.setTextColor(wdCol(), C_BG);
  gfx.setTextDatum(textdatum_t::middle_center);
  gfx.drawString(WD_ABBR[t.tm_wday], bx + bw / 2, by + bh / 2);
  gfx.setTextSize(1.0f);
}

// ---------- день недели / число / месяц (раскладка 0) ----------
static void drawDateBlock(const tm& t) {
  if (g_clockLayout == 1) { drawDateTop(t); return; }
  gfx.fillRect(DATE_X, DATE_Y - 8, DATE_W, DATE_H + 8, C_BG);
  const int cx = DATE_X + DATE_W / 2;
  const int wdY = DATE_Y - ((g_clockFont == 6) ? 5 : 0);   // стиль 7: день недели на 5 px выше

  // день недели
  if (g_clockFont == 5) {
    const ClockGlyph& wd = CLOCK_WDAYS[t.tm_wday];
    drawGlyph(wd, cx - wd.w / 2, wdY, wdCol(), DATE_W - 8);
  } else if (g_textFont >= 1) {
    drawMaskText(WD_SHORT[t.tm_wday], cx - maskTextW(WD_SHORT[t.tm_wday], 40) / 2, wdY, 40, wdCol());
  } else {
    gfx.setFont(F_J32);
    gfx.setTextColor(wdCol(), C_BG);
    gfx.setTextDatum(textdatum_t::top_center);
    gfx.drawString(WD_SHORT[t.tm_wday], cx, wdY);
  }

  // число - стилем выбранных часов
  {
    const int h = (g_clockFont == 6) ? FLIP_SMALL[0].h : 76;
    const int y = (g_clockFont == 6) ? DATE_Y + 30 : DATE_Y + 40;
    drawDayNum(cx - dayNumW(t.tm_mday, h) / 2, y, h, t.tm_mday, dateCol());
  }

  // месяц
  if (g_clockFont == 5) {
    const ClockGlyph& mon = CLOCK_MONTHS[t.tm_mon];
    drawGlyph(mon, cx - mon.w / 2, DATE_Y + 128, monthCol(), DATE_W - 4);
  } else if (g_textFont >= 1) {
    drawMaskText(MONTH_GEN[t.tm_mon], cx - maskTextW(MONTH_GEN[t.tm_mon], 46) / 2, DATE_Y + 128, 46, monthCol());
  } else {
    const lgfx::IFont* fs[] = {F_J24, F_J20, F_J16};
    const lgfx::IFont* use = fs[2];
    for (int i = 0; i < 3; i++) {
      gfx.setFont(fs[i]);
      if (gfx.textWidth(MONTH_GEN[t.tm_mon]) <= DATE_W - 4) { use = fs[i]; break; }
    }
    gfx.setFont(use);
    gfx.setTextColor(monthCol(), C_BG);
    gfx.setTextDatum(textdatum_t::top_center);
    gfx.drawString(MONTH_GEN[t.tm_mon], cx, DATE_Y + 128);
  }
}
// ---------- бегущая строка ----------
static int rt(float v) { return (int)lroundf(v); }

// ---------- бегущая строка в стиле 6: свой шрифт из clock_marquee.h ----------
// Коды в MG_CODES отсортированы - ищем двоичным поиском
static int mgFind(uint16_t code) {
  int lo = 0, hi = MG_COUNT - 1;
  while (lo <= hi) {
    int mid = (lo + hi) >> 1;
    if (MG_CODES[mid] == code) return mid;
    if (MG_CODES[mid] < code) lo = mid + 1; else hi = mid - 1;
  }
  return -1;
}

static uint16_t mgUtf8(const char*& p) {
  const uint8_t* s = (const uint8_t*)p;
  uint16_t c = *s++;
  if (c < 0x80) { p = (const char*)s; return c; }
  if ((c & 0xE0) == 0xC0) {
    if (*s) c = (uint16_t)(((c & 0x1F) << 6) | (*s++ & 0x3F));
  } else if ((c & 0xF0) == 0xE0) {
    c = (uint16_t)((c & 0x0F) << 12);
    if (*s) c |= (uint16_t)((*s++ & 0x3F) << 6);
    if (*s) c |= (uint16_t)(*s++ & 0x3F);
  }
  p = (const char*)s;
  return c;
}

// Текущий набор глифов бегущей строки (0 - встроенный шрифт, рисуем им)
static const MgGlyph* mgSet() {
  const int i = (g_textFont >= 1 && g_textFont <= 2) ? g_textFont - 1 : 0;
  return MG_SETS[i];
}

static int marqueeWidth(const char* s) {
  int w = 0;
  const char* p = s;
  while (*p) {
    const uint16_t c = mgUtf8(p);
    const int i = mgFind(c);
    w += (i >= 0) ? mgSet()[i].adv : 12;
  }
  return w;
}

// Рисуем глиф прямо в буфер спрайта бегущей строки (он 16-битный, RGB565)
static void blitGlyph(uint16_t* buf, int stride, const MgGlyph& g, int x, int y, uint16_t col) {
  for (int dy = 0; dy < g.h; dy++) {
    const int py = y + dy;
    if (py < 0 || py >= SCR_H) continue;
    uint16_t* row = buf + py * stride;
    for (int dx = 0; dx < g.w; dx++) {
      const int px = x + dx;
      if (px < 0 || px >= 800) continue;
      const int idx = dy * g.w + dx;
      const uint8_t v = (g.data[idx >> 2] >> ((idx & 3) * 2)) & 3;
      row[px] = v ? col : C_BG;
    }
  }
}

static void marqueeDraw(const char* s, int x, int y, uint16_t col) {
  uint16_t* buf = (uint16_t*)scr.getBuffer();
  if (!buf) return;
  const int stride = scr.width();
  int pen = x;
  const char* p = s;
  while (*p) {
    const uint16_t c = mgUtf8(p);
    const int i = mgFind(c);
    if (i < 0) { pen += 12; continue; }
    const MgGlyph& g = mgSet()[i];
    if (g.data && g.w && g.h && pen + g.xo < 800 && pen + g.xo + (int)g.w > 0) {
      blitGlyph(buf, stride, g, pen + g.xo, y + g.yo, col);
    }
    pen += g.adv;
  }
}


// Текст выбранным декоративным шрифтом (набор из clock_marquee.h) с масштабом по высоте
static int maskTextW(const char* s, int targetH) {
  const MgGlyph* set = mgSet();
  int w = 0;
  const char* p = s;
  while (*p) {
    const uint16_t c = mgUtf8(p);
    const int i = mgFind(c);
    w += (i >= 0) ? (int)set[i].adv * targetH / MG_LINE_H : 10 * targetH / MG_LINE_H;
  }
  return w;
}

static void drawMaskText(const char* s, int x, int y, int targetH, uint16_t col) {
  if (g_textFont < 1 || g_textFont > 2) return;
  static uint16_t* buf = nullptr;
  if (!buf) buf = (uint16_t*)heap_caps_malloc(80 * 80 * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!buf) return;
  const MgGlyph* set = mgSet();
  bool oldSwap = gfx.getSwapBytes();
  gfx.setSwapBytes(true);
  int pen = x;
  const char* p = s;
  while (*p) {
    const uint16_t c = mgUtf8(p);
    const int i = mgFind(c);
    if (i < 0) { pen += 10 * targetH / MG_LINE_H; continue; }
    const MgGlyph& g = set[i];
    const int dw = (int)g.w * targetH / MG_LINE_H;
    const int dh = (int)g.h * targetH / MG_LINE_H;
    if (g.data && dw > 0 && dh > 0 && dw <= 80 && dh <= 80) {
      for (int dy = 0; dy < dh; dy++) {
        const int sy = dy * g.h / dh;
        for (int dx = 0; dx < dw; dx++) {
          const int idx = sy * g.w + (dx * g.w / dw);
          const uint8_t v = (g.data[idx >> 2] >> ((idx & 3) * 2)) & 3;
          buf[dy * dw + dx] = v ? col : C_BG;
        }
      }
      gfx.pushImage(pen + (int)g.xo * targetH / MG_LINE_H, y + (int)g.yo * targetH / MG_LINE_H, dw, dh, buf);
    }
    pen += (int)g.adv * targetH / MG_LINE_H;
  }
  gfx.setSwapBytes(oldSwap);
}
static void buildLines() {
  nLines = 0;
  time_t now = time(nullptr);
  tm t;
  localtime_r(&now, &t);

  // 0) дата
  if (g_set.line[0].en) {
    snprintf(lines[nLines], sizeof(lines[0]), "Сегодня %s, %d %s %d года.",
             WD_FULL[t.tm_wday], t.tm_mday, MONTH_GEN[t.tm_mon], t.tm_year + 1900);
    lineId[nLines++] = 0;
  }

  // 1) погода сейчас - полное описание: температура, состояние, «ощущается», ветер,
  // влажность, давление, облачность, сегодняшние минимум/максимум, восход и закат
  if (g_set.line[1].en && haveWx && wxCopy.hasCur) {
    const CurrentWx& c = wxCopy.cur;
    int n = snprintf(lines[nLines], sizeof(lines[0]),
                     "Сейчас %+.1f°C, %s. Ощущается как %+.1f°C. Ветер %.1f м/с %s, влажность %d%%, давление %d мм рт.ст., облачность %d%%.",
                     c.temp, c.desc, c.feels, c.wind, WIND_FULL[((c.windDeg + 22) / 45) % 8],
                     c.humidity, c.pressureMmHg, c.clouds);
    if (n > 0 && n < (int)sizeof(lines[0]) && wxCopy.dayCount > 0) {
      const DayWx& d0 = wxCopy.days[0];
      n += snprintf(lines[nLines] + n, sizeof(lines[0]) - n, " Сегодня от %+.1f°C до %+.1f°C.",
                    d0.tmin, d0.tmax);
    }
    if (n > 0 && n < (int)sizeof(lines[0]) && c.sunrise) {
      tm a, b;
      localtime_r(&c.sunrise, &a);
      localtime_r(&c.sunset, &b);
      snprintf(lines[nLines] + n, sizeof(lines[0]) - n, " Восход %02d:%02d, закат %02d:%02d.",
               a.tm_hour, a.tm_min, b.tm_hour, b.tm_min);
    }
    lineId[nLines++] = 1;
  }

  // 2) завтра
  if (g_set.line[2].en && haveWx && wxCopy.dayCount >= 2) {
    const DayWx& d = wxCopy.days[1];
    snprintf(lines[nLines], sizeof(lines[0]), "Завтра от %+d°C до %+d°C.", rt(d.tmin), rt(d.tmax));
    lineId[nLines++] = 2;
  }

  // 3, 4) принятые данные: MQTT и Народмон (пусто, если выключено или нет свежих значений)
  for (int w = 0; w < 3; w++) {
    String txt = (w == 2) ? wsensorsLineText() : integrationsRxLineText(w);
    if (txt.length() && nLines < MAX_LINES) {
      strlcpy(lines[nLines], txt.c_str(), sizeof(lines[0]));
      lineId[nLines++] = RX_LINE_BASE + w;
    }
  }
}

static void scrollFrame(uint32_t now) {
  if (!scrReady) return;
  uint32_t dt = now - lastFrame;
  if (dt < 33) return;               // ~30 кадров в секунду
  if (dt > 120) dt = 120;
  lastFrame = now;

  scr.fillSprite(C_BG);
  if (nLines == 0) { scr.pushSprite(0, SCR_Y); return; }   // все строки выключены
  if (curLine >= nLines) curLine = 0;

  uint32_t lineRgb;
  int lineSpeed;
  if (lineId[curLine] >= RX_LINE_BASE) {                    // строки приёма
    int w = lineId[curLine] - RX_LINE_BASE;
    lineRgb = (w == 2) ? wsensorsLineColor() : integrationsRxLineColor(w);
    lineSpeed = (w == 2) ? wsensorsLineSpeed() : integrationsRxLineSpeed(w);
  } else {                                                  // цвет и скорость - живые настройки строки
    lineRgb = g_set.line[lineId[curLine]].rgb;
    lineSpeed = g_set.line[lineId[curLine]].speed;
  }
  const bool styled = (g_textFont >= 1 && g_textFont <= 2);   // выбран декоративный шрифт
  const uint16_t col = rgb16(lineRgb);
  int w;
  if (styled) {
    w = marqueeWidth(lines[curLine]);
  } else {
    scr.setFont(F_SCROLL);
    scr.setTextSize(SCROLL_SCALE);
    scr.setTextColor(col, C_BG);
    scr.setTextDatum(textdatum_t::top_left);
    w = scr.textWidth(lines[curLine]);
  }
  scrollOff -= dt * speedPxPerMs(lineSpeed);
  if (scrollOff < -w) {
    curLine = (curLine + 1) % nLines;
    scrollOff = 800;
    if (curLine == 0) buildLines();  // новый круг - свежие тексты и погода
  }
  if (styled) marqueeDraw(lines[curLine], (int)scrollOff, (SCR_H - MG_LINE_H) / 2, col);
  else        scr.drawString(lines[curLine], (int)scrollOff, (int)((SCR_H - 40 * SCROLL_SCALE) / 2));   // по центру полосы
  scr.pushSprite(0, SCR_Y);
}

// ---------- публичное ----------
void uiClockSetWeather(const Weather& w) {
  wxCopy = w;
  haveWx = true;
  buildLines();
  if (curLine >= nLines) curLine = 0;
}

void uiClockOpen() {
  gfx.fillScreen(C_BG);

  // позиции цифр ЧЧ:ММ
  digX[0] = HX;
  digX[1] = digX[0] + HW + HG;
  colonX  = digX[1] + HW + HG;
  digX[2] = colonX + HCW + HG;
  digX[3] = digX[2] + HW + HG;

  for (int i = 0; i < 6; i++) shownDigit[i] = -2;
  shownColon = -1;
  shownDay = -1;
  shownFont = g_clockFont;
  shownColor = g_clockColor;
  shownTextFont = g_textFont;
  applyLayout();

  if (!scrReady) {
    scr.setPsram(true);        // спрайт 800x68x16 бит = 106 КБ: во внутренней памяти его не выделить, строка тогда не рисуется
    scr.setColorDepth(16);
    scrReady = scr.createSprite(800, SCR_H) != nullptr;
    Serial.printf("Бегущая строка: спрайт %s\n", scrReady ? "создан" : "НЕ СОЗДАН (не хватило памяти)");
  }
  curLine = 0;
  scrollOff = 800;
  lastFrame = millis();
  buildLines();
}

void uiClockTick(const tm* t) {
  if (shownFont != g_clockFont || shownLayout != g_clockLayout || shownColor != g_clockColor
      || shownTextFont != g_textFont) {
    applyLayout();
    gfx.fillRect(0, 0, 800, SCR_Y, C_BG);
    for (int i = 0; i < 6; i++) shownDigit[i] = -2;
    shownColon = -1;
    shownDay = -1;
    shownFont = g_clockFont;
    shownColor = g_clockColor;
    shownTextFont = g_textFont;
  }
  if (t) {
    drawDigitIfChanged(0, t->tm_hour / 10);
    drawDigitIfChanged(1, t->tm_hour % 10);
    drawDigitIfChanged(2, t->tm_min / 10);
    drawDigitIfChanged(3, t->tm_min % 10);
    drawDigitIfChanged(4, t->tm_sec / 10);
    drawDigitIfChanged(5, t->tm_sec % 10);
    drawColon((t->tm_sec & 1) == 0);
    if (t->tm_yday != shownDay) {
      shownDay = t->tm_yday;
      drawDateBlock(*t);
      buildLines();                       // сменилась дата - обновить первую строку
      if (curLine >= nLines) curLine = 0;
    }
  } else {
    for (int i = 0; i < 6; i++) drawDigitIfChanged(i, -1);
    drawColon(true);
  }
  scrollFrame(millis());
}

// ---------- отрисовка времени для второго экрана: тем же стилем, но меньшего размера ----------
// Глиф стиля 6/7 с масштабом по высоте (для остальных стилей работает drawDigit)
static void drawGlyphH(const ClockGlyph& g, int x, int y, int targetH, uint16_t col) {
  if (!g.data || !g.w || !g.h) return;
  if (!westBuf) westBuf = (uint16_t*)heap_caps_malloc(125 * 250 * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!westBuf) return;
  const int dh = targetH;
  const int dw = (int)g.w * dh / (int)g.h;
  if (dw <= 0 || dh <= 0 || dw > 250 || dh > 250) return;
  for (int dy = 0; dy < dh; dy++) {
    const int sy = dy * g.h / dh;
    for (int dx = 0; dx < dw; dx++) {
      const int idx = sy * g.w + (dx * g.w / dw);
      const uint8_t v = (g.data[idx >> 2] >> ((idx & 3) * 2)) & 3;
      westBuf[dy * dw + dx] = (v == 1) ? col : C_BG;
    }
  }
  bool oldSwap = gfx.getSwapBytes();
  gfx.setSwapBytes(true);
  gfx.pushImage(x, y, dw, dh, westBuf);
  gfx.setSwapBytes(oldSwap);
}

static void drawFlipH(const FlipGlyph& g, int x, int y, int targetH) {
  if (!g.data || !g.w || !g.h) return;
  if (!westBuf) westBuf = (uint16_t*)heap_caps_malloc(125 * 250 * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!westBuf) return;
  const int dh = targetH;
  const int dw = (int)g.w * dh / (int)g.h;
  if (dw <= 0 || dh <= 0 || dw > 250 || dh > 250) return;
  for (int dy = 0; dy < dh; dy++) {
    const int sy = dy * g.h / dh;
    for (int dx = 0; dx < dw; dx++) {
      const int idx = sy * g.w + (dx * g.w / dw);
      const uint8_t v = (g.data[idx >> 2] >> ((idx & 3) * 2)) & 3;
      westBuf[dy * dw + dx] = (v == 1) ? RGB16(232, 232, 232) : (v == 2) ? RGB16(20, 20, 24) : C_BG;
    }
  }
  bool oldSwap = gfx.getSwapBytes();
  gfx.setSwapBytes(true);
  gfx.pushImage(x, y, dw, dh, westBuf);
  gfx.setSwapBytes(oldSwap);
}

int uiClockDrawTime(int x, int y, int h, int hour, int minute, uint16_t col, int second) {
  const int d[4] = { hour / 10, hour % 10, minute / 10, minute % 10 };
  const int gap = h / 12;
  const int dw = h / 2;
  const int t = dw * 21 / 100 < 5 ? 5 : dw * 21 / 100;
  const int sh = h * 42 / 100;
  const int sdw = sh / 2;
  const int st2 = sdw * 21 / 100 < 3 ? 3 : sdw * 21 / 100;
  int xx = x;

  auto glyphW = [&](const ClockGlyph& g) { return (int)g.w * h / (int)g.h; };
  auto flipW  = [&](const FlipGlyph& g)  { return (int)g.w * h / (int)g.h; };

  for (int i = 0; i < 4; i++) {
    if (i == 2) {                                   // двоеточие между часами и минутами
      const int cw = h / 8;
      const int r = (g_clockFont == 6) ? h / 18 : (g_clockFont == 5 ? h / 16 : t / 2 + 1);
      const int cxx = (g_clockFont == 6) ? xx + h / 24 : xx + cw / 2 - 2;
      gfx.fillCircle(cxx, y + h / 3, r, col);
      gfx.fillCircle(cxx, y + 2 * h / 3, r, col);
      xx += cw;
    }
    if (g_clockFont == 5) {                         // стиль 6: глифы Gabriola
      drawGlyphH(CLOCK_TBIG[d[i]], xx, y, h, col);
      xx += glyphW(CLOCK_TBIG[d[i]]) + gap;
    } else if (g_clockFont == 6) {                  // стиль 7: перекидные карточки
      drawFlipH(FLIP_SMALL[d[i]], xx, y, h);
      xx += flipW(FLIP_SMALL[d[i]]) + gap;
    } else {                                        // стили 0-4: сегменты и точки
      drawDigit(xx, y, dw, h, t, d[i], col, C_DIG_OFF);
      xx += dw + gap;
    }
  }

  if (second >= 0 && second <= 59) {                // секунды мельче, справа от минут
    xx += h / 14;
    const int sy = y + h - sh;
    if (g_clockFont == 5) {
      drawGlyphH(CLOCK_TBIG[second / 10], xx, sy, sh, col);
      xx += (int)CLOCK_TBIG[second / 10].w * sh / (int)CLOCK_TBIG[second / 10].h + gap / 2;
      drawGlyphH(CLOCK_TBIG[second % 10], xx, sy, sh, col);
      xx += (int)CLOCK_TBIG[second % 10].w * sh / (int)CLOCK_TBIG[second % 10].h;
    } else if (g_clockFont == 6) {
      drawFlipH(FLIP_SMALL[second / 10], xx, sy, sh);
      xx += (int)FLIP_SMALL[second / 10].w * sh / (int)FLIP_SMALL[second / 10].h + gap / 2;
      drawFlipH(FLIP_SMALL[second % 10], xx, sy, sh);
      xx += (int)FLIP_SMALL[second % 10].w * sh / (int)FLIP_SMALL[second % 10].h;
    } else {
      drawDigit(xx, sy, sdw, sh, st2, second / 10, col, C_DIG_OFF);
      xx += sdw + sh / 12;
      drawDigit(xx, sy, sdw, sh, st2, second % 10, col, C_DIG_OFF);
      xx += sdw;
    }
  }
  return xx - x;
}