#include "ui.h"
#include "config.h"
#include "settings.h"
#include "icons.h"
#include "logo_skycast.h"
#include "wsensors.h"
#include "ui_anim.h"
#include "ui_clock.h"
#include "moon.h"
#include <time.h>
#include "lgfx_crowpanel.h"
#include <esp_heap_caps.h>
#include <math.h>
#include <string.h>

// ---------- цвета (фон чёрный - как у иконок) ----------
#define RGB16(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))
static const uint16_t C_BG    = 0x0000;
static const uint16_t C_TEXT  = RGB16(255, 255, 255);
static const uint16_t C_DIM   = RGB16(170, 180, 196);
static const uint16_t C_LINE  = RGB16(58, 70, 88);
static const uint16_t C_ACC   = RGB16(255, 196, 0);   // тёплый акцент
static const uint16_t C_COLD  = RGB16(66, 196, 255);
static const uint16_t C_ERR   = RGB16(255, 72, 72);
static const uint16_t C_MQTT  = RGB16(255, 40, 30);    // красная точка "температура принята по MQTT"

// ---------- шрифты ----------
// lgfxJapanGothic содержит кириллицу; DejaVu - только цифры/латиница (для температур)
#define F_J16 (&lgfx::fonts::lgfxJapanGothic_16)
#define F_J20 (&lgfx::fonts::lgfxJapanGothic_20)
#define F_J24 (&lgfx::fonts::lgfxJapanGothic_24)
#define F_J28 (&lgfx::fonts::lgfxJapanGothic_28)
#define F_J32 (&lgfx::fonts::lgfxJapanGothic_32)
#define F_D24 (&lgfx::fonts::DejaVu24)
#define F_D40 (&lgfx::fonts::DejaVu40)
#define F_D56 (&lgfx::fonts::DejaVu56)
#define F_D72 (&lgfx::fonts::DejaVu72)

// Жирнее: у шрифтов Japanese Gothic нет жирного начертания, поэтому строка рисуется второй раз со сдвигом на 1 px
// (прозрачным фоном). 0 - обычная толщина. Крупные цифры DejaVu не трогаем: они и так плотные.
#ifndef UI_TEXT_BOLD
#define UI_TEXT_BOLD 1
#endif

using lgfx::textdatum_t;

// ---------- геометрия ----------
static const int CLK_X = 604, CLK_Y = 6, CLK_W = 196, CLK_H = 76;
static const int Y_DIV1 = 86;
static const int Y_DIV2 = 248;
static const int Y_FOOT = 452;
static void drawFootIp();
static void drawHumPresRows();

static lgfx::LGFX_Sprite clk(&gfx);
static int lastSec = -1;
static int lastDay = -1;

static const char* WD_FULL[] = {"Воскресенье", "Понедельник", "Вторник", "Среда",
                                "Четверг", "Пятница", "Суббота"};
static const char* WD_SHORT[] = {"Вс", "Пн", "Вт", "Ср", "Чт", "Пт", "Сб"};
static const char* MONTH_GEN[] = {"января", "февраля", "марта", "апреля", "мая", "июня",
                                  "июля", "августа", "сентября", "октября", "ноября", "декабря"};
static const char* WIND_DIR[] = {"С", "СВ", "В", "ЮВ", "Ю", "ЮЗ", "З", "СЗ"};

// ---------- вспомогательное ----------

// Вывод строки с уже выставленными шрифтом, цветом (с фоном) и выравниванием. Первый проход стирает фон,
// второй (на 1 px правее, прозрачный) утолщает буквы.
static void drawStr(const char* s, int x, int y, uint16_t col, const lgfx::IFont* f) {
  gfx.drawString(s, x, y);
#if UI_TEXT_BOLD
  if (f != F_D24 && f != F_D40 && f != F_D56 && f != F_D72) {
    gfx.setTextColor(col);              // один цвет без фона = прозрачный текст
    gfx.drawString(s, x + 1, y);
    gfx.setTextColor(col, C_BG);
  }
#endif
}

// Текст нужным шрифтом: берём самый крупный из списка, что влезает в maxW
static void fitText(const char* s, int x, int y, int maxW, uint16_t col, textdatum_t datum,
                    const lgfx::IFont* const* fonts, int nFonts) {
  const lgfx::IFont* use = fonts[nFonts - 1];
  for (int i = 0; i < nFonts; i++) {
    gfx.setFont(fonts[i]);
    if (gfx.textWidth(s) <= maxW) { use = fonts[i]; break; }
  }
  gfx.setFont(use);
  gfx.setTextColor(col, C_BG);
  gfx.setTextDatum(datum);
  drawStr(s, x, y, col, use);
}

static void text(const char* s, int x, int y, const lgfx::IFont* f, uint16_t col, textdatum_t datum) {
  gfx.setFont(f);
  gfx.setTextColor(col, C_BG);
  gfx.setTextDatum(datum);
  drawStr(s, x, y, col, f);
}

// Первая русская буква - заглавная (UTF-8)
static void capitalizeRu(char* s) {
  uint8_t a = (uint8_t)s[0], b = (uint8_t)s[1];
  if (a == 0xD0 && b >= 0xB0 && b <= 0xBF) s[1] = (char)(b - 0x20);          // а..п
  else if (a == 0xD1 && b >= 0x80 && b <= 0x8F) { s[0] = (char)0xD0; s[1] = (char)(b + 0x20); }  // р..я
}

static int roundTemp(float t) {
  int v = (int)lroundf(t);
  return v;                     // -0 в int не бывает
}

// Рисует "число°" (знак градуса - кольцо). Возвращает ширину. dry=true - только измерить.
// dec=true - с десятыми ("21.4"), иначе целое ("21"; так рисуется прогноз).
static int drawTemp(int x, int y, float t, const lgfx::IFont* f, uint16_t col, int ringR,
                    const char* unit, bool dry, bool dec = false) {
  char buf[16];
  if (dec) {
    if (fabsf(t) < 0.05f) t = 0;                  // иначе получится "-0.0"
    snprintf(buf, sizeof(buf), "%.1f", t);
  } else {
    snprintf(buf, sizeof(buf), "%d", roundTemp(t));
  }
  gfx.setFont(f);
  int w = gfx.textWidth(buf);
  int ringW = ringR * 2 + 4;
  int uw = 0;
  if (unit) uw = gfx.textWidth(unit) + 2;
  int total = w + ringW + uw;
  if (dry) return total;

  gfx.setTextColor(col, C_BG);
  gfx.setTextDatum(textdatum_t::top_left);
  gfx.drawString(buf, x, y);
  int rx = x + w + ringR + 2;
  int ry = y + ringR + 2 + gfx.fontHeight() / 12;
  gfx.fillCircle(rx, ry, ringR, col);
  gfx.fillCircle(rx, ry, ringR - (ringR > 6 ? 3 : 2), C_BG);
  if (unit) gfx.drawString(unit, x + w + ringW, y);
  return total;
}

static void drawTempCenter(int cx, int y, float t, const lgfx::IFont* f, uint16_t col, int ringR) {
  int w = drawTemp(0, y, t, f, col, ringR, nullptr, true);
  drawTemp(cx - w / 2, y, t, f, col, ringR, nullptr, false);
}

// ---------- Полная заливка и раскраска погодных иконок ----------
// Яркость исходного пикселя RGB565 (0..255)
static inline int rawLum(uint16_t raw) {
  if (!raw) return 0;
  int r5 = (raw >> 11) & 0x1F;
  int g6 = (raw >> 5)  & 0x3F;
  int b5 =  raw        & 0x1F;
  int lum = (r5 * 8 * 77 + g6 * 4 * 150 + b5 * 8 * 29) >> 8;
  return lum > 255 ? 255 : lum;
}

// Буферы для заливки и цветного рендера (в PSRAM)
static uint8_t*  extMask   = nullptr;   // 1 = внешний фон, 0 = фигура или её внутренняя область
static uint16_t* bfsQueue  = nullptr;   // очередь обхода в ширину (до 110*64 = 7040)
static uint8_t*  rgba1x    = nullptr;   // 110 * 64 * 3 (R, G, B) для 1x и билинейного сглаживания 2x
static uint16_t* icon1xBuf = nullptr;
static uint16_t* bigBuf    = nullptr;

// Выбирает базовый цвет (tr, tg, tb) для точки (x, y) в зависимости от кода погоды и того,
// является ли точка контуром (isStroke) или внутренней заливкой (!isStroke)
static void pickIconColor(int x, int y, const char* code, bool isStroke, int& tr, int& tg, int& tb) {
  // По умолчанию - объёмное бело-голубое облако
  tr = 230; tg = 240; tb = 255;

  if (!code || code[0] == 0) {
    // Заставка (weather.h): солнце слева-сверху + облако + капли/снежинка снизу
    if (y >= 45) {
      tr = 60; tg = 185; tb = 255;
    } else if ((y < 23 && x < 60) || (x < 40 && y < 37) || (x < 46 && y < 29)) {
      tr = 255; tg = isStroke ? 205 : 185; tb = 35;
    } else {
      int shade = (y - 16) * 2;
      if (shade < 0) shade = 0;
      tr = 240 - shade; tg = 246 - shade * 2 / 3; tb = 255;
    }
  } else if (strncmp(code, "01d", 3) == 0) {
    // Ясное солнце: яркий золотисто-жёлтый диск внутри, тёплый оранжево-золотой контур и лучи
    float dx = x - 55.0f, dy = y - 32.0f;
    float dist = sqrtf(dx * dx + dy * dy);
    if (!isStroke) {
      int k = (int)(dist * 3.0f);
      if (k > 45) k = 45;
      tr = 255; tg = 235 - k; tb = 80 - k;
    } else {
      tr = 255; tg = dist < 18.0f ? 200 : 175; tb = 30;
    }
  } else if (strncmp(code, "01n", 3) == 0) {
    // Ясная ночь: полностью залитый неоново-голубой полумесяц
    int shade = (y - 8) * 3 / 4;
    if (shade < 0) shade = 0;
    if (!isStroke) {
      tr = 135 - shade / 2; tg = 205 - shade / 3; tb = 255;
    } else {
      tr = 175; tg = 225; tb = 255;
    }
  } else if (strncmp(code, "02d", 3) == 0) {
    // Переменная облачность (день): залитое солнце слева-сверху + залитое белое облако
    bool inSun = (y < 21 && x < 62) || (x < 39 && y < 39) || (x < 45 && y < 30) || (x < 51 && y < 24);
    if (inSun) {
      tr = 255; tg = isStroke ? 200 : 215; tb = isStroke ? 30 : 55;
    } else {
      int shade = (y - 16) * 2;
      if (shade < 0) shade = 0;
      if (shade > 55) shade = 55;
      tr = (isStroke ? 245 : 235) - shade;
      tg = (isStroke ? 250 : 242) - shade * 2 / 3;
      tb = 255;
    }
  } else if (strncmp(code, "02n", 3) == 0) {
    // Переменная облачность (ночь): залитая голубая луна + залитое светлое облако
    bool inMoon = (y < 21 && x < 62) || (x < 39 && y < 39) || (x < 45 && y < 30) || (x < 51 && y < 24);
    if (inMoon) {
      tr = isStroke ? 160 : 120; tg = isStroke ? 215 : 190; tb = 255;
    } else {
      int shade = (y - 16) * 2;
      if (shade < 0) shade = 0;
      if (shade > 60) shade = 60;
      tr = 225 - shade; tg = 235 - shade * 2 / 3; tb = 250;
    }
  } else if (strncmp(code, "03", 2) == 0) {
    // Облачно: полностью залитое бело-голубое облако с объёмом книзу
    int shade = (y - 12) * 3 / 2;
    if (shade < 0) shade = 0;
    if (shade > 55) shade = 55;
    tr = (isStroke ? 245 : 235) - shade;
    tg = (isStroke ? 250 : 243) - shade * 2 / 3;
    tb = 255;
  } else if (strncmp(code, "04", 2) == 0) {
    // Пасмурно: задняя туча темнее (сине-стальная), переднее облако светлое
    bool backCloud = (y < 22) || (x > 60 && y < 36) || (x > 52 && y < 27);
    if (backCloud) {
      tr = isStroke ? 165 : 135; tg = isStroke ? 190 : 160; tb = isStroke ? 225 : 200;
    } else {
      int shade = (y - 20) * 3 / 2;
      if (shade < 0) shade = 0;
      if (shade > 50) shade = 50;
      tr = 235 - shade; tg = 243 - shade * 2 / 3; tb = 255;
    }
  } else if (strncmp(code, "09", 2) == 0) {
    // Ливень: плотная сине-серая дождевая туча + ярко-голубые капли снизу
    if (y >= 42) {
      tr = 50; tg = 180; tb = 255;
    } else {
      int shade = (y - 10) * 3 / 2;
      if (shade < 0) shade = 0;
      if (shade > 50) shade = 50;
      tr = 195 - shade; tg = 215 - shade * 2 / 3; tb = 240 - shade / 3;
    }
  } else if (strncmp(code, "10d", 3) == 0) {
    // Дождь с солнцем: залитое солнце + залитое облако + голубые капли
    if (y >= 43) {
      tr = 50; tg = 180; tb = 255;
    } else if ((y < 20 && x < 60) || (x < 39 && y < 35) || (x < 45 && y < 28) || (x < 50 && y < 23)) {
      tr = 255; tg = isStroke ? 195 : 215; tb = isStroke ? 30 : 55;
    } else {
      int shade = (y - 16) * 3 / 2;
      if (shade < 0) shade = 0;
      if (shade > 50) shade = 50;
      tr = 225 - shade; tg = 235 - shade * 2 / 3; tb = 250;
    }
  } else if (strncmp(code, "10n", 3) == 0) {
    // Ночной дождь: залитая луна + залитое облако + голубые капли
    if (y >= 43) {
      tr = 50; tg = 180; tb = 255;
    } else if ((y < 20 && x < 60) || (x < 39 && y < 35) || (x < 45 && y < 28) || (x < 50 && y < 23)) {
      tr = isStroke ? 160 : 120; tg = isStroke ? 215 : 190; tb = 255;
    } else {
      int shade = (y - 16) * 3 / 2;
      if (shade < 0) shade = 0;
      if (shade > 50) shade = 50;
      tr = 210 - shade; tg = 225 - shade * 2 / 3; tb = 245;
    }
  } else if (strncmp(code, "11", 2) == 0) {
    // Гроза: залитая грозовая туча сверху + залитая ярко-жёлтая молния снизу
    if (y >= 41 || (y >= 36 && x >= 46 && x <= 64)) {
      tr = 255; tg = isStroke ? 210 : 230; tb = isStroke ? 25 : 65;
    } else {
      int shade = (y - 10) * 3 / 2;
      if (shade < 0) shade = 0;
      if (shade > 55) shade = 55;
      tr = 185 - shade; tg = 205 - shade * 2 / 3; tb = 235 - shade / 3;
    }
  } else if (strncmp(code, "13", 2) == 0) {
    // Снег: залитое светлое облако + голубые снежинки
    if (y >= 42) {
      tr = 130; tg = 220; tb = 255;
    } else {
      int shade = (y - 10) * 3 / 2;
      if (shade < 0) shade = 0;
      if (shade > 45) shade = 45;
      tr = 235 - shade; tg = 243 - shade * 2 / 3; tb = 255;
    }
  } else if (strncmp(code, "50", 2) == 0) {
    // Туман: плотные бирюзово-серебристые полосы
    tr = 165; tg = 215; tb = 235;
  }
}

// Строит RGB-карту иконки (W x H) в буфере rgba1x.
// raw = true  - иконка уже нарисована в цвете, берём её пиксели как есть (мультяшный, цветной, яркий наборы);
// raw = false - контурная иконка: пустые области внутри фигуры заливаются по коду погоды (как раньше).
static bool buildFilledIcon(const tImage* im, const char* code, bool raw) {
  const int W = im->width, H = im->height, N = W * H;
  if (!extMask)  extMask  = (uint8_t*)heap_caps_malloc(N, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!bfsQueue) bfsQueue = (uint16_t*)heap_caps_malloc(N * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!rgba1x)   rgba1x   = (uint8_t*)heap_caps_malloc(N * 3, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!extMask || !bfsQueue || !rgba1x) return false;

  if (raw) {
    for (int i = 0; i < N; i++) {
      uint16_t v = im->data[i];
      rgba1x[i * 3]     = (uint8_t)(((v >> 11) & 0x1F) * 255 / 31);
      rgba1x[i * 3 + 1] = (uint8_t)(((v >> 5) & 0x3F) * 255 / 63);
      rgba1x[i * 3 + 2] = (uint8_t)((v & 0x1F) * 255 / 31);
    }
    return true;
  }

  memset(extMask, 0, N);
  int head = 0, tail = 0;

  // Порог яркости контура, который не пропускает заливку снаружи внутрь фигуры
  const int WALL_LUM = 28;

  auto tryPush = [&](int x, int y) {
    int idx = y * W + x;
    if (extMask[idx]) return;
    if (rawLum(im->data[idx]) >= WALL_LUM) return;
    extMask[idx] = 1;
    bfsQueue[tail++] = (uint16_t)idx;
  };

  // Запускаем волну с краёв иконки
  for (int x = 0; x < W; x++) { tryPush(x, 0); tryPush(x, H - 1); }
  for (int y = 0; y < H; y++) { tryPush(0, y); tryPush(W - 1, y); }

  while (head < tail) {
    int idx = bfsQueue[head++];
    int x = idx % W, y = idx / W;
    if (x > 0)     tryPush(x - 1, y);
    if (x + 1 < W) tryPush(x + 1, y);
    if (y > 0)     tryPush(x, y - 1);
    if (y + 1 < H) tryPush(x, y + 1);
  }

  // Заполняем итоговые R, G, B для каждого пикселя
  for (int y = 0; y < H; y++) {
    for (int x = 0; x < W; x++) {
      int idx = y * W + x;
      int lum = rawLum(im->data[idx]);
      uint8_t r = 0, g = 0, b = 0;

      if (!extMask[idx]) {
        // Внутренняя область фигуры или сам контур: закрашиваем полностью!
        bool isStroke = (lum >= 140);
        int tr, tg, tb;
        pickIconColor(x, y, code, isStroke, tr, tg, tb);
        // Для плавного перехода от заливки к яркому контуру слегка учитываем яркость контура
        int boost = (lum > 150) ? (lum - 150) / 5 : 0;
        int rr = tr + boost, gg = tg + boost, bb = tb + boost;
        r = (uint8_t)(rr > 255 ? 255 : (rr < 0 ? 0 : rr));
        g = (uint8_t)(gg > 255 ? 255 : (gg < 0 ? 0 : gg));
        b = (uint8_t)(bb > 255 ? 255 : (bb < 0 ? 0 : bb));
      } else if (lum > 0) {
        // Внешние сглаженные края (антиалиасинг контура, лучи, капли, снежинки)
        int tr, tg, tb;
        pickIconColor(x, y, code, true, tr, tg, tb);
        // Усиливаем плотность мелких деталей (лучей, капель, снежинок)
        int effLum = lum + (lum >> 1);
        if (effLum > 255) effLum = 255;
        r = (uint8_t)((tr * effLum) / 255);
        g = (uint8_t)((tg * effLum) / 255);
        b = (uint8_t)((tb * effLum) / 255);
      }

      int p3 = idx * 3;
      rgba1x[p3]     = r;
      rgba1x[p3 + 1] = g;
      rgba1x[p3 + 2] = b;
    }
  }
  return true;
}

// Иконка 1:1 (полностью закрашенная). style >= 0 - нарисовать иконку этого набора (предпросмотр).
static void drawIcon2x(const tImage* im, int x, int y, const char* code);

static char g_animCode[4] = "01d";

// имя иконки в наборе icons.bin по коду OpenWeather

static char g_fc0[4] = "";      // иконка первого дня прогноза
static char g_fc[5][4];          // коды иконок всех дней полосы
static int  g_fcN = 0;

// Анимация иконки «Сегодня» в полосе прогноза (первая ячейка)
void uiTickForecastAnim() {
  if (g_iconStyle != 0) return;   // анимация только для пастельного стиля
  static uint32_t lastMs = 0;
  static char lastCode[5][4] = {};
  const uint32_t now = millis();
  if (now - lastMs < 400) return;          // медленнее: 5 иконок сразу
  lastMs = now;
  for (int i = 0; i < g_fcN && i < 5; i++) {
    if (!g_fc[i][0]) continue;
    const char* nm = uiAnimNameFor(g_fc[i]);
    if (uiAnimFrames(nm) <= 0) continue;
    if (strcmp(lastCode[i], g_fc[i]) != 0) {
      strlcpy(lastCode[i], g_fc[i], sizeof(lastCode[i]));
      gfx.fillRect(24 + i * 150, 288, 150, 68, C_BG);
    }
    uiAnimDraw(nm, 24 + i * 150 + 20, 290, 110, 64);
  }
}

const char* uiAnimNameFor(const char* code) {
  if (!code) return "day";
  if (!strcmp(code, "01d")) return "day";
  if (!strcmp(code, "01n")) return "night";
  if (!strcmp(code, "02d")) return "cloudy-day-1";
  if (!strcmp(code, "02n")) return "cloudy-night-1";
  if (!strcmp(code, "03d")) return "cloudy-day-2";
  if (!strcmp(code, "03n")) return "cloudy-night-2";
  if (!strcmp(code, "04d") || !strcmp(code, "04n")) return "cloudy";
  if (!strcmp(code, "09d")) return "rainy-1";
  if (!strcmp(code, "09n")) return "rainy-2";
  if (!strcmp(code, "10d")) return "rainy-3";
  if (!strcmp(code, "10n")) return "rainy-4";
  if (!strcmp(code, "11d") || !strcmp(code, "11n")) return "thunder";
  if (!strcmp(code, "13d")) return "snowy-1";
  if (!strcmp(code, "13n")) return "snowy-2";
  if (!strcmp(code, "50d") || !strcmp(code, "50n")) return "weather";
  return "day";
}

// Анимация иконки погоды на главном экране (вызывать из loop)
void uiTickIconAnim() {
  if (g_iconStyle != 0) return;   // анимация только для пастельного стиля
  static uint32_t lastMs = 0;
  static int lastFrame = -1;
  static char lastCode[4] = "";
  const uint32_t now = millis();
  if (now - lastMs < 160) return;
  lastMs = now;
  const char* nm = uiAnimNameFor(g_animCode);
  const int n = uiAnimFrames(nm);
  if (n <= 0) return;                       // набора нет - остаётся встроенная иконка
  if (strcmp(lastCode, g_animCode) != 0) {   // сменился тип погоды - чистим один раз
    strlcpy(lastCode, g_animCode, sizeof(lastCode));
    gfx.fillRect(24, 100, 240, 144, C_BG);
    lastFrame = -1;
  }
  const int f = (int)((now / 160) % (uint32_t)n);
  if (f == lastFrame) return;
  lastFrame = f;
  uiAnimDraw(nm, 80, 126, 128, 92);   // кадры уже нужного размера - без масштабирования
}

bool uiDrawPhaseIcon(const char* code, int x, int y) {
  if (!code || moonCount() <= 0) return false;
  const bool clear  = (strcmp(code, "01n") == 0);   // ясно, ночь
  const bool partly = (strcmp(code, "02n") == 0);   // облачно с прояснениями, ночь
  if (!clear && !partly) return false;

  static uint16_t* buf = nullptr;
  const int SZ = clear ? 128 : 96;                  // 64x64 удваиваем (плотное облако - меньше)
  if (!buf) buf = (uint16_t*)heap_caps_malloc(128 * 128 * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!buf) return false;

  time_t ts = time(nullptr);
  const double syn = 29.530588853;
  double ph = fmod(((double)ts - 947182440.0) / 86400.0 / syn, 1.0);
  if (ph < 0) ph += 1.0;
  int idx = (int)lroundf((ph + 0.5f) * (float)moonCount()) % moonCount();   // набор начинается с полной луны
  if (idx < 0) idx += moonCount();
  const uint16_t* src = moonFrame(idx);
  if (!src) return false;
  for (int dy = 0; dy < SZ; dy++) {
    const int sy = dy * moonH() / SZ;
    for (int dx = 0; dx < SZ; dx++) buf[dy * SZ + dx] = src[sy * moonW() + dx * moonW() / SZ];
  }

  // место под иконку: 2x-область 220x128; для «с прояснениями» луна выше и левее
  const int bx = x + 110 - SZ / 2 - (partly ? 26 : 0);
  const int by = y + 64 - SZ / 2 - (partly ? 20 : 0);
  bool oldSwap = gfx.getSwapBytes();
  gfx.setSwapBytes(true);
  gfx.pushImage(bx, by, SZ, SZ, buf);
  gfx.setSwapBytes(oldSwap);

  if (partly) {
    // облако поверх нижней правой части луны
    const int cx = x + 128, cy = y + 84;
    const uint16_t cl = RGB16(226, 236, 248), out = RGB16(120, 138, 160);
    gfx.fillCircle(cx - 26, cy + 6, 15, cl);
    gfx.fillCircle(cx - 2, cy - 6, 21, cl);
    gfx.fillCircle(cx + 22, cy + 7, 14, cl);
    gfx.fillRect(cx - 26, cy + 6, 48, 16, cl);
    gfx.drawCircle(cx - 26, cy + 6, 15, out);
    gfx.drawCircle(cx - 2, cy - 6, 21, out);
    gfx.drawCircle(cx + 22, cy + 7, 14, out);
    gfx.drawFastHLine(cx - 26, cy + 22, 49, out);
  }
  return true;
}

// Иконка погоды вдвое крупнее - для второго экрана (как на главном)
void uiDrawWeatherIcon2x(const char* code, int x, int y) {
  const tImage* im = iconByCode(code);
  if (!im) return;
  drawIcon2x(im, x, y, code);
}

static void drawIcon1x(const tImage* im, int x, int y, const char* code = nullptr, int style = -1) {
  const int W = im->width, H = im->height;
  const uint8_t st = (style >= 0) ? (uint8_t)style : g_iconStyle;
  if (!icon1xBuf) icon1xBuf = (uint16_t*)heap_caps_malloc(W * H * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!icon1xBuf || !buildFilledIcon(im, code, !iconsStylePainted(st))) {
    bool oldSwap = gfx.getSwapBytes();
    gfx.setSwapBytes(true);
    gfx.pushImage(x, y, W, H, im->data);
    gfx.setSwapBytes(oldSwap);
    return;
  }
  for (int i = 0; i < W * H; i++) {
    icon1xBuf[i] = RGB16(rgba1x[i * 3], rgba1x[i * 3 + 1], rgba1x[i * 3 + 2]);
  }
  bool oldSwap = gfx.getSwapBytes();
  gfx.setSwapBytes(true);
  gfx.pushImage(x, y, W, H, icon1xBuf);
  gfx.setSwapBytes(oldSwap);
}

// Иконка x2 с полной заливкой и билинейным сглаживанием (буфер в PSRAM)
static void drawIcon2x(const tImage* im, int x, int y, const char* code = nullptr) {
  const int W = im->width, H = im->height, DW = W * 2, DH = H * 2;
  if (!bigBuf) bigBuf = (uint16_t*)heap_caps_malloc(DW * DH * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!bigBuf || !buildFilledIcon(im, code, !iconsStylePainted(g_iconStyle))) { drawIcon1x(im, x, y, code); return; }

  for (int dy = 0; dy < DH; dy++) {
    float sy = (dy + 0.5f) * 0.5f - 0.5f;
    int y0 = (int)floorf(sy);
    float fy = sy - y0;
    int ya = y0 < 0 ? 0 : y0;
    int yb = y0 + 1 > H - 1 ? H - 1 : y0 + 1;
    for (int dx = 0; dx < DW; dx++) {
      float sx = (dx + 0.5f) * 0.5f - 0.5f;
      int x0 = (int)floorf(sx);
      float fx = sx - x0;
      int xa = x0 < 0 ? 0 : x0;
      int xb = x0 + 1 > W - 1 ? W - 1 : x0 + 1;

      const uint8_t* p00 = &rgba1x[(ya * W + xa) * 3];
      const uint8_t* p10 = &rgba1x[(ya * W + xb) * 3];
      const uint8_t* p01 = &rgba1x[(yb * W + xa) * 3];
      const uint8_t* p11 = &rgba1x[(yb * W + xb) * 3];

      float w00 = (1 - fx) * (1 - fy), w10 = fx * (1 - fy), w01 = (1 - fx) * fy, w11 = fx * fy;

      int r = (int)(p00[0] * w00 + p10[0] * w10 + p01[0] * w01 + p11[0] * w11 + 0.5f);
      int g = (int)(p00[1] * w00 + p10[1] * w10 + p01[1] * w01 + p11[1] * w11 + 0.5f);
      int b = (int)(p00[2] * w00 + p10[2] * w10 + p01[2] * w01 + p11[2] * w11 + 0.5f);
      bigBuf[dy * DW + dx] = RGB16(r, g, b);
    }
  }
  bool oldSwap = gfx.getSwapBytes();
  gfx.setSwapBytes(true);
  gfx.pushImage(x, y, DW, DH, bigBuf);
  gfx.setSwapBytes(oldSwap);
}

// Предпросмотр иконки набора на экране настроек: тот же путь вывода, что и на главном экране
void uiPreviewIcon(const tImage* im, int x, int y, const char* code, uint8_t style) {
  drawIcon1x(im, x, y, code, (int)style);
}

// ---------- шапка: город и дата ----------
// Значок шестерёнки: нажатие в шапке слева открывает настройки
static void drawHomeBlock();

static void drawGear(int cx, int cy, uint16_t col) {
  for (int i = 0; i < 8; i++) {
    float a = i * 0.785398f;
    gfx.fillCircle(cx + (int)lroundf(cosf(a) * 14), cy + (int)lroundf(sinf(a) * 14), 3, col);
  }
  gfx.fillCircle(cx, cy, 11, col);
  gfx.fillCircle(cx, cy, 5, C_BG);
}

static void drawHeaderLeft(const tm& t) {
  gfx.fillRect(0, 0, CLK_X - 4, Y_DIV1 - 2, C_BG);
  text(g_set.city, 24, 8, F_J32, C_TEXT, textdatum_t::top_left);
  char buf[64];
  snprintf(buf, sizeof(buf), "%s, %d %s", WD_FULL[t.tm_wday], t.tm_mday, MONTH_GEN[t.tm_mon]);
  const lgfx::IFont* fs[] = {F_J24, F_J20, F_J16};
  fitText(buf, 24, 50, 335, C_DIM, textdatum_t::top_left, fs, 3);
  // радио убрано из проекта - кнопка не рисуется
  drawHomeBlock();
  drawGear(CLK_X - 24, 43, C_DIM);
}

// Значок дома + показания «Дома» (слева от шестерёнки)
static void drawHomeBlock() {
  float t = NAN, h = NAN, pv = NAN;
  if (!wsensorsPick(wsensorsSrc(0), t, h, pv)) return;   // нет данных - блок не рисуем
  const int x = CLK_X - 190, y = 16;
  // домик
  const uint16_t c = C_DIM;
  gfx.drawLine(x, y + 16, x + 14, y + 4, c);
  gfx.drawLine(x + 14, y + 4, x + 28, y + 16, c);
  gfx.drawRect(x + 5, y + 16, 18, 16, c);
  gfx.fillRect(x + 12, y + 22, 5, 10, c);

  char b[32];
  if (!isnan(t)) {
    snprintf(b, sizeof(b), "%+.1f", t);
    gfx.setFont(F_J24);
    gfx.setTextColor(C_TEXT, C_BG);
    gfx.setTextDatum(textdatum_t::top_left);
    gfx.drawString(b, x + 36, y + 2);
    const int w = gfx.textWidth(b);
    gfx.drawCircle(x + 36 + w + 8, y + 12, 4, C_ACC);
    gfx.drawCircle(x + 36 + w + 8, y + 12, 3, C_ACC);
  }
  if (!isnan(h)) {
    snprintf(b, sizeof(b), "%d%%", (int)lroundf(h));
    gfx.setFont(F_J20);
    gfx.setTextColor(C_DIM, C_BG);
    gfx.setTextDatum(textdatum_t::top_left);
    gfx.drawString(b, x + 36, y + 30);
  }
}

// Часы в шапке - тем же стилем, что и на экране часов (uiClockDrawTime рисует прямо на экран).
// Спрайт больше не используем: стили 1-7 рисуют векторно/глифами, в спрайт их так просто не положить.
static void drawClock(const tm* t, bool colonOn) {
  (void)colonOn;
  gfx.fillRect(CLK_X, CLK_Y, CLK_W, CLK_H, C_BG);
  if (!t) return;
  uiClockDrawTime(CLK_X + 12, CLK_Y + 6, 64, t->tm_hour, t->tm_min, C_TEXT, -1);
}

// ---------- большая температура: из погоды или с внешнего датчика (MQTT) ----------
static bool  haveWx = false;    // текущая погода получена
static float wxTemp = 0;        // температура из погоды
static bool  extHas = false;    // есть температура с внешнего датчика
static float extTemp = 0;
static const int BT_X = 268, BT_Y = 96, BT_W = 240, BT_H = 86;   // ниже, с y = 182, идёт описание погоды

// Красная точка "температура приходит по MQTT" (диаметр 10)
static const int MQTT_DOT = 10;
static void drawMqttDot(int x, int y, uint16_t col) {
  gfx.fillCircle(x + MQTT_DOT / 2, y + MQTT_DOT / 2, MQTT_DOT / 2, col);
}

static void drawBigTemp() {
  float t = extHas ? extTemp : wxTemp;
  // D56, а не D72: с десятыми ("-21.4") число крупным шрифтом не влезает в BT_W (240 px)
  int w = drawTemp(BT_X, BT_Y, t, F_D56, t < 0 ? C_COLD : C_TEXT, 9, "C", false, true);
  if (extHas) {   // число с MQTT-датчика - справа от температуры красная точка
    int ix = BT_X + w + 8;
    if (ix > BT_X + BT_W - MQTT_DOT) ix = BT_X + BT_W - MQTT_DOT;   // не выходить за область (правее - колонка подробностей)
    drawMqttDot(ix, BT_Y + 6, C_MQTT);
  }
}

// Перерисовка одного числа: сначала стираем область, а рисуем строго внутри неё (описание не заденем)
static void redrawBigTemp() {
  gfx.setClipRect(BT_X, BT_Y, BT_W, BT_H);
  gfx.fillRect(BT_X, BT_Y, BT_W, BT_H, C_BG);
  if (extHas || haveWx) drawBigTemp();
  gfx.clearClipRect();
}

// Десятые доли - для сравнения "изменилось ли число на экране"
static int temp10(float t) { return (int)lroundf(t * 10.0f); }

void uiSetMainTemp(bool has, float t, bool redraw) {
  // на экране десятые: перерисовываем, только если изменилось число (0.1 градуса) или источник
  bool same = (has == extHas) && (!has || temp10(t) == temp10(extTemp));
  extHas = has;
  extTemp = has ? t : 0;
  if (!same && redraw) redrawBigTemp();
}

// ---------- влажность и давление: две верхние строки правой колонки ----------
static int  wxHum = 0, wxPres = 0;                 // из погоды (давление в мм рт. ст.)
static bool  extHumHas = false, extPresHas = false; // есть значение с внешнего датчика
static float extHum = 0, extPres = 0;               // с датчика; на экране - целые (45 %, 745 мм)
static const int HP_X = 516, HP_Y0 = 98, HP_ROW = 36, HP_W = 268;

static void redrawHumPresRows() {
  gfx.setClipRect(HP_X, HP_Y0 - 2, HP_W, HP_ROW * 2);
  gfx.fillRect(HP_X, HP_Y0 - 2, HP_W, HP_ROW * 2, C_BG);
  drawHumPresRows();
  gfx.clearClipRect();
}

void uiSetMainHumPres(bool humHas, float hum, bool presHas, float presMmHg, bool redraw) {
  bool same = (humHas == extHumHas) && (presHas == extPresHas) &&
              (!humHas || (int)lroundf(hum) == (int)lroundf(extHum)) &&
              (!presHas || (int)lroundf(presMmHg) == (int)lroundf(extPres));
  extHumHas = humHas;   extHum = humHas ? hum : 0;
  extPresHas = presHas; extPres = presHas ? presMmHg : 0;
  if (!same && redraw) redrawHumPresRows();
}

// ---------- публичное ----------
void uiBegin() {
  clk.setColorDepth(16);
  clk.createSprite(CLK_W, CLK_H);
}

void uiSplash(const char* line1, const char* line2) {
  gfx.fillScreen(C_BG);
  {   // вместо значков погоды - логотип SKYCAST
    bool oldSwap = gfx.getSwapBytes();
    gfx.setSwapBytes(true);
    gfx.pushImage(400 - LOGO_SKYCAST_W / 2, 70, LOGO_SKYCAST_W, LOGO_SKYCAST_H, LOGO_SKYCAST);
    gfx.setSwapBytes(oldSwap);
  }
  const lgfx::IFont* f1[] = {F_J28, F_J24, F_J20, F_J16};
  const lgfx::IFont* f2[] = {F_J20, F_J16};
  fitText(line1, 400, 270, 760, C_TEXT, textdatum_t::top_center, f1, 4);
  if (line2) fitText(line2, 400, 316, 760, C_DIM, textdatum_t::top_center, f2, 2);
}

void uiShowMain() {
  gfx.fillScreen(C_BG);
  gfx.drawFastHLine(24, Y_DIV1, 752, C_LINE);
  gfx.drawFastHLine(24, Y_DIV2, 752, C_LINE);
  drawFootIp();
  { tm t; if (getLocalTime(&t, 0)) { drawHeaderLeft(t); drawClock(&t, true); } }  // шапка и часы сразу
  if (extHas) drawBigTemp();   // значения с датчиков показываем и без погоды
  if (extHumHas || extPresHas) drawHumPresRows();
  lastSec = -1;
  lastDay = -1;
}

void uiTickClock(const tm* t) {
  if (!t) {
    if (lastSec != -2) { lastSec = -2; drawClock(nullptr, true); }
    return;
  }
  if (t->tm_sec == lastSec) return;
  lastSec = t->tm_sec;
  if (t->tm_yday != lastDay) { lastDay = t->tm_yday; drawHeaderLeft(*t); }
  static int lastMin = -1;
  if (t->tm_min != lastMin) { lastMin = t->tm_min; drawClock(t, true); }
}

static void drawRow(int y, const char* label, const char* value) {
  text(label, 520, y + 6, F_J16, C_DIM, textdatum_t::top_left);
  text(value, 776, y, F_J24, C_TEXT, textdatum_t::top_right);
}

// Влажность и давление: с внешнего датчика (красная точка слева от значения) или из погоды.
// Рисует только то, что есть; стирание области - на вызывающем.
static void drawHumPresRows() {
  char v[40];
  for (int k = 0; k < 2; k++) {
    bool ext = (k == 0) ? extHumHas : extPresHas;
    if (!ext && !haveWx) continue;
    int y = HP_Y0 + k * HP_ROW;
    if (k == 0) {
      snprintf(v, sizeof(v), "%d %%", ext ? (int)lroundf(extHum) : wxHum);
    } else {
      snprintf(v, sizeof(v), "%d мм", ext ? (int)lroundf(extPres) : wxPres);
    }
    drawRow(y, k == 0 ? "Влажность" : "Давление", v);
    if (ext) {
      gfx.setFont(F_J24);
      gfx.fillCircle(776 - gfx.textWidth(v) - 12, y + 13, 4, C_MQTT);
    }
  }
}

// Описание погоды. Короткое ("Ясно", "Пасмурно") - одной строкой крупно. Длинное ("Переменная облачность") - в две
// строки по пробелу, самым крупным шрифтом, в который обе строки влезают. Возвращает y, с которого можно рисовать
// следующую строку ("Ощущается"): 218 для одной строки, ниже для двух. descH - высота шрифта описания.
static int drawDesc(const char* d, int x, int y0, int maxW, uint16_t col, int* descH) {
  static const lgfx::IFont* one[] = {F_J28, F_J24};
  for (const lgfx::IFont* f : one) {
    gfx.setFont(f);
    if (gfx.textWidth(d) <= maxW) { *descH = gfx.fontHeight(); text(d, x, y0, f, col, textdatum_t::top_left); return 218; }
  }
  static const lgfx::IFont* two[] = {F_J24, F_J20, F_J16};
  const int n = (int)strlen(d);
  for (const lgfx::IFont* f : two) {
    gfx.setFont(f);
    char a[64], b[64];
    int best = -1, bestW = 1 << 30;
    for (int i = 1; i < n - 1 && n < (int)sizeof(a); i++) {   // пробел - всегда границей символа UTF-8
      if (d[i] != ' ') continue;
      memcpy(a, d, i); a[i] = 0;
      strlcpy(b, d + i + 1, sizeof(b));
      int wa = gfx.textWidth(a), wb = gfx.textWidth(b);
      int w = wa > wb ? wa : wb;
      if (w < bestW) { bestW = w; best = i; }            // делим так, чтобы длинная строка была как можно короче
    }
    if (best > 0 && bestW <= maxW) {
      memcpy(a, d, best); a[best] = 0;
      strlcpy(b, d + best + 1, sizeof(b));
      int h = gfx.fontHeight();
      *descH = h;
      text(a, x, y0, f, col, textdatum_t::top_left);
      text(b, x, y0 + h, f, col, textdatum_t::top_left);
      int fy = y0 + 2 * h + 1;
      return fy < 218 ? 218 : fy;
    }
  }
  // одно слово или очень длинный текст: одна строка, шрифт подбирается по ширине
  static const lgfx::IFont* fit[] = {F_J20, F_J16};
  fitText(d, x, y0, maxW, col, textdatum_t::top_left, fit, 2);
  *descH = gfx.fontHeight();
  return 218;
}

static void drawCurrent(const CurrentWx& c) {
  // иконка x2 (с заливкой по коду погоды) и большая температура
  strlcpy(g_animCode, c.icon, sizeof(g_animCode));
  gfx.fillRect(24, 100, 240, 144, C_BG);          // область иконки - под анимацию
  if (g_iconStyle != 0 || uiAnimFrames(uiAnimNameFor(c.icon)) == 0) {   // нет кадров/другой стиль - статичная
    if (!uiDrawPhaseIcon(c.icon, 34, 108)) drawIcon2x(iconByCode(c.icon), 34, 108, c.icon);
  }
  drawBigTemp();   // из погоды (wxTemp) или с датчика MQTT, если он включён

  // описание и "ощущается"
  char d[64];
  strlcpy(d, c.desc, sizeof(d));
  capitalizeRu(d);
  int descH = 28;
  int feelsY = drawDesc(d, 268, 182, 236, C_ACC, &descH);   // одна строка или две; возвращает, где начинается "Ощущается"

  char fl[48];
  snprintf(fl, sizeof(fl), "Ощущается %.1f", c.feels);
  const lgfx::IFont* ff[] = {F_J24, F_J20, F_J16};
  gfx.setFont(F_J16);
  // ширину определяет подобранный шрифт; кольцо градуса дорисуем следом
  const lgfx::IFont* use = ff[2];
  for (int i = 0; i < 3; i++) {   // шрифт: влезает по ширине и по высоте (ниже - разделитель Y_DIV2)
    gfx.setFont(ff[i]);
    int fhh = gfx.fontHeight();
    // влезает по ширине и до разделителя, и не крупнее описания (но не мельче J20, чтобы читалось)
    if (gfx.textWidth(fl) + 14 <= 236 && feelsY + fhh <= Y_DIV2 - 1 && fhh <= (descH > 20 ? descH : 20)) { use = ff[i]; break; }
  }
  gfx.setFont(use);
  gfx.setTextColor(C_DIM, C_BG);
  gfx.setTextDatum(textdatum_t::top_left);
  drawStr(fl, 268, feelsY, C_DIM, use);
  int fw = gfx.textWidth(fl);
  int fh = gfx.fontHeight();
  gfx.fillCircle(268 + fw + 6, feelsY + fh / 5, 3, C_DIM);
  gfx.fillCircle(268 + fw + 6, feelsY + fh / 5, 1, C_BG);

  // правая колонка: подробности
  drawHumPresRows();   // влажность и давление: из погоды (wxHum, wxPres) или с датчика
  char v[40];
  int y = HP_Y0 + HP_ROW * 2;
  snprintf(v, sizeof(v), "%.1f м/с %s", c.wind, WIND_DIR[((c.windDeg + 22) / 45) % 8]);
  drawRow(y, "Ветер", v);                  y += 36;
  snprintf(v, sizeof(v), "%d %%", c.clouds);
  drawRow(y, "Облачность", v);
}

static void dayLabel(time_t ts, char* out, size_t n) {
  time_t now = time(nullptr);
  int k = dayKey(ts);
  if (k == dayKey(now)) { strlcpy(out, "Сегодня", n); return; }
  if (k == dayKey(now + 86400)) { strlcpy(out, "Завтра", n); return; }
  tm lt;
  localtime_r(&ts, &lt);
  snprintf(out, n, "%s, %d", WD_SHORT[lt.tm_wday], lt.tm_mday);
}

static void drawForecast(const Weather& w) {
  const int colW = 752 / 5;                       // 150
  for (int i = 0; i < w.dayCount && i < 5; i++) {
    const DayWx& d = w.days[i];
    int x0 = 24 + i * colW;
    int cx = x0 + colW / 2;

    char lb[24];
    dayLabel(d.ts, lb, sizeof(lb));
    const lgfx::IFont* fl[] = {F_J24, F_J20, F_J16};
    fitText(lb, cx, 258, colW - 8, C_DIM, textdatum_t::top_center, fl, 3);

    const tImage* ic = iconByCode(d.icon);
    if (i < 5) { strlcpy(g_fc[i], d.icon, sizeof(g_fc[i])); g_fcN = i + 1; }
    if (g_iconStyle != 0 || uiAnimFrames(uiAnimNameFor(d.icon)) == 0)
      drawIcon1x(ic, cx - ic->width / 2, 290, d.icon);

    drawTempCenter(cx, 362, d.tmax, F_D40, d.tmax < 0 ? C_COLD : C_TEXT, 5);
    drawTempCenter(cx, 410, d.tmin, F_D24, C_DIM, 3);

    if (i > 0) gfx.drawFastVLine(x0 - 1, 262, 180, C_LINE);
  }
}

void uiDrawWeather(const Weather& w) {
  // область между шапкой и подвалом
  gfx.fillRect(0, Y_DIV1 + 1, 800, Y_DIV2 - Y_DIV1 - 1, C_BG);
  gfx.fillRect(0, Y_DIV2 + 1, 800, Y_FOOT - Y_DIV2 - 1, C_BG);

  haveWx = w.hasCur;
  if (w.hasCur) { wxTemp = w.cur.temp; wxHum = w.cur.humidity; wxPres = w.cur.pressureMmHg; }
  if (w.hasCur) drawCurrent(w.cur);
  else {   // погоды нет, но значения с датчиков показываем
    if (extHas) drawBigTemp();
    drawHumPresRows();
  }
  if (w.dayCount) drawForecast(w);

  // восход / закат - справа в подвале
  gfx.fillRect(470, Y_FOOT, 330, 480 - Y_FOOT, C_BG);
  if (w.hasCur && w.cur.sunrise) {
    tm a, b;
    localtime_r(&w.cur.sunrise, &a);
    localtime_r(&w.cur.sunset, &b);
    char s[64];
    snprintf(s, sizeof(s), "Восход %02d:%02d   Закат %02d:%02d", a.tm_hour, a.tm_min, b.tm_hour, b.tm_min);
    text(s, 776, Y_FOOT + 4, F_J16, C_DIM, textdatum_t::top_right);
  }
}


static char footIp[24] = "";

static void drawFootIp() {
  gfx.fillRect(300, Y_FOOT, 170, 480 - Y_FOOT, C_BG);
  if (footIp[0]) text(footIp, 310, Y_FOOT + 4, F_J16, C_DIM, textdatum_t::top_left);
}

void uiSetIp(const char* ip) {
  strlcpy(footIp, ip ? ip : "", sizeof(footIp));
  drawFootIp();
}

void uiFooter(const char* left, bool error) {
  gfx.fillRect(0, Y_FOOT, 300, 480 - Y_FOOT, C_BG);
  text(left, 24, Y_FOOT + 4, F_J16, error ? C_ERR : C_DIM, textdatum_t::top_left);
}

// Экран сработавшего будильника: время крупно, касание выключает
void uiAlarmShow(int hour, int minute) {
  gfx.fillScreen(C_BG);
  text("Будильник", 400, 60, F_J32, C_ACC, textdatum_t::top_center);
  char tb[8];
  snprintf(tb, sizeof(tb), "%02d:%02d", hour, minute);
  text(tb, 400, 140, F_D72, C_TEXT, textdatum_t::top_center);
  const lgfx::IFont* fh[] = {F_J28, F_J24, F_J20, F_J16};
  fitText("Коснитесь экрана, чтобы выключить", 400, 300, 720, C_DIM, textdatum_t::top_center, fh, 4);
}
// Баннер поверх текущего экрана: тёмная плашка с рамкой внизу по центру
void uiBanner(const char* text) {
  if (!text || !text[0]) return;
  const int w = 420, h = 54, x = (800 - w) / 2, y = 352;
  gfx.fillRoundRect(x, y, w, h, 12, RGB16(18, 24, 34));
  gfx.drawRoundRect(x, y, w, h, 12, C_ACC);
  const lgfx::IFont* fs[] = {F_J24, F_J20, F_J16};
  const lgfx::IFont* use = fs[2];
  for (int i = 0; i < 3; i++) {
    gfx.setFont(fs[i]);
    if (gfx.textWidth(text) <= w - 28) { use = fs[i]; break; }
  }
  gfx.setFont(use);
  gfx.setTextColor(C_TEXT, RGB16(18, 24, 34));
  gfx.setTextDatum(textdatum_t::middle_center);
  gfx.drawString(text, x + w / 2, y + h / 2);
}