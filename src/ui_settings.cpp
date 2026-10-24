#include "ui_settings.h"
#include "alarm.h"
#include "chime.h"
#include "settings.h"
#include "icons.h"
#include "ui.h"
#include "lgfx_crowpanel.h"
#include <time.h>
#include <math.h>

// ---------- цвета и шрифты ----------
#define RGB16(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))
static const uint16_t C_BG   = 0x0000;
static const uint16_t C_TEXT = RGB16(255, 255, 255);
static const uint16_t C_DIM  = RGB16(170, 180, 196);
static const uint16_t C_LINE = RGB16(58, 70, 88);
static const uint16_t C_ACC  = RGB16(255, 196, 0);
static const uint16_t C_BTN  = RGB16(44, 54, 70);

#define F_J16 (&lgfx::fonts::lgfxJapanGothic_16)
#define F_J20 (&lgfx::fonts::lgfxJapanGothic_20)
#define F_J24 (&lgfx::fonts::lgfxJapanGothic_24)
#define F_D24 (&lgfx::fonts::DejaVu24)
#define F_D40 (&lgfx::fonts::DejaVu40)

using lgfx::textdatum_t;

// ---------- геометрия ----------
static const int ROW_Y0 = 86, ROW_H = 48, BTN_H = 42;
static const int X_MINUS = 470, BTN_W = 84, X_PLUS = 650;
static const int X_VAL = 602;
static const int NPAGES = 8;
static const int TAB_X0 = 24, TAB_STEP = 96, TAB_W = 96, TAB_Y = 8, TAB_H = 50;
static const int BACK_X = 640, BACK_Y = 420, BACK_W = 136, BACK_H = 50;   // Кнопка "Назад"
static const int RST_X  = 576, RST_Y  = 420, RST_W  = 50,  RST_H  = 50;   // Кнопка перезагрузки в виде иконки (квадратная)
static const int LN_Y0 = 80, LN_STEP = 112;
static const uint32_t PALETTE[8] = {0xF0F4FA, 0xFACD5A, 0x78BEEB, 0x78DC8C, 0xFF9646, 0xFF6E64, 0xF082DC, 0xAA96FF};
static const int PAD = 6;
static const uint32_t IDLE_MS = 60000;

// ---------- описание настроек ----------
enum Id {
  ID_BR_DAY, ID_BR_NIGHT, ID_NIGHT_START, ID_NIGHT_END, ID_CLOCK_NIGHT, ID_INFO, ID_CLOCK_FONT, ID_CLOCK_LAYOUT,
  ID_VOL_AL_D, ID_VOL_AL_N, ID_VOL_CH_D, ID_VOL_CH_N, ID_CHIME_D, ID_CHIME_N, ID_VOICE, ID_AUTOSTOP,
  ID_DAYSEL, ID_AL_EN, ID_AL_H, ID_AL_M, ID_AL_TRACK, ID_TEST, ID_TEST_CHIME,
  ID_LN_SPD0, ID_LN_SPD1, ID_LN_SPD2, ID_CLKCOL, ID_TXTFONT
};
enum Kind { K_STEP, K_TOGGLE, K_DAYSEL, K_ACTION, K_INFO, K_LINE, K_ICON, K_CLKCOL };
enum Fmt  { FM_PCT, FM_INT, FM_TIME, FM_2D, FM_3D, FM_VOICE, FM_FONT, FM_LAYOUT, FM_TXTFONT };

struct Ctl {
  Id id; Kind kind; const char* label;
  int vmin, vmax, step; Fmt fmt; bool wrap; int row;
};

static const Ctl PG_SCREEN[] = {
  {ID_BR_DAY,      K_STEP, "Яркость днём",        5, 100, 5,  FM_PCT,  false, 0},
  {ID_BR_NIGHT,    K_STEP, "Яркость ночью",       5, 100, 5,  FM_PCT,  false, 1},
  {ID_NIGHT_START, K_STEP, "Ночь начинается",     0, 1410, 30, FM_TIME, true,  2},
  {ID_NIGHT_END,   K_STEP, "Ночь заканчивается",  0, 1410, 30, FM_TIME, true,  3},
  {ID_CLOCK_NIGHT, K_TOGGLE, "Ночью показывать часы", 0, 1,  1,  FM_INT,  false, 4},
  {ID_CLOCK_FONT,  K_STEP, "Стиль цифр часов",    0, CLOCK_FONT_COUNT - 1, 1, FM_FONT, true, 5},
  {ID_CLOCK_LAYOUT, K_STEP, "Раскладка часов",    0, CLOCK_LAYOUT_COUNT - 1, 1, FM_LAYOUT, true, 6},
};
static const Ctl PG_SOUND[] = {
  {ID_VOL_AL_D, K_STEP,   "Громкость будильника, день",  0, 21, 1, FM_INT, false, 0},
  {ID_VOL_AL_N, K_STEP,   "Громкость будильника, ночь",  0, 21, 1, FM_INT, false, 1},
  {ID_AUTOSTOP, K_STEP,   "Автостоп будильника, мин",    1, 60, 1, FM_INT, false, 2},
};
static const Ctl PG_TIME[] = {
  {ID_CHIME_D,    K_TOGGLE, "Озвучивать время днём",     0, 1,  1, FM_INT,   false, 0},
  {ID_CHIME_N,    K_TOGGLE, "Озвучивать время ночью",    0, 1,  1, FM_INT,   false, 1},
  {ID_VOL_CH_D,   K_STEP,   "Громкость, день",           0, 21, 1, FM_INT,   false, 2},
  {ID_VOL_CH_N,   K_STEP,   "Громкость, ночь",           0, 21, 1, FM_INT,   false, 3},
  {ID_VOICE,      K_STEP,   "Голос",                     0, 3,  1, FM_VOICE, true,  4},
  {ID_TEST_CHIME, K_ACTION, "Проверить озвучку",         0, 0,  0, FM_INT,   false, 5},
};
static const Ctl PG_ALARM[] = {
  {ID_DAYSEL,   K_DAYSEL, "",                         0, 0,   0, FM_INT, false, 0},
  {ID_AL_EN,    K_TOGGLE, "Включён в этот день",      0, 1,   1, FM_INT, false, 1},
  {ID_AL_H,     K_STEP,   "Часы",                     0, 23,  1, FM_2D,  true,  2},
  {ID_AL_M,     K_STEP,   "Минуты",                   0, 59,  1, FM_2D,  true,  3},
  {ID_AL_TRACK, K_STEP,   "Номер мелодии (/alarms/NNN.mp3)", 1, 999, 1, FM_3D, true, 4},
  {ID_TEST,     K_ACTION, "Проверить звук",           0, 0,   0, FM_INT, false, 5},
};

static const Ctl PG_LINES[] = {
  {ID_LN_SPD0, K_LINE, "", 1, 10, 1, FM_INT, false, 0},
  {ID_LN_SPD1, K_LINE, "", 1, 10, 1, FM_INT, false, 1},
  {ID_LN_SPD2, K_LINE, "", 1, 10, 1, FM_INT, false, 2},
};

// Вкладка «Иконки»: одна строка - один набор, c.row = номер набора (см. icons.h)
static const Ctl PG_ICONS[] = {
  {ID_INFO, K_ICON, "", 0, 0, 0, FM_INT, false, 0},
  {ID_INFO, K_ICON, "", 0, 0, 0, FM_INT, false, 1},
  {ID_INFO, K_ICON, "", 0, 0, 0, FM_INT, false, 2},
  {ID_INFO, K_ICON, "", 0, 0, 0, FM_INT, false, 3},
};
static_assert(sizeof(PG_ICONS) / sizeof(Ctl) == ICON_STYLE_COUNT, "PG_ICONS должен совпадать с ICON_STYLE_COUNT");

// Вкладка «Часы»: цвета цифр, даты, дня недели, месяца и секунд (палитра ниже)
// Вкладка «Шрифт»: шрифт бегущей строки
static const Ctl PG_TEXTFONT[] = {
  {ID_TXTFONT, K_STEP, "Шрифт текста", 0, TEXT_FONT_COUNT - 1, 1, FM_TXTFONT, true, 0},
};
static const Ctl PG_CLOCKCOL[] = {
  {ID_CLKCOL, K_CLKCOL, "Цифры часов",  0, 7, 1, FM_INT, false, 0},
  {ID_CLKCOL, K_CLKCOL, "Дата (число)", 0, 7, 1, FM_INT, false, 1},
  {ID_CLKCOL, K_CLKCOL, "День недели",  0, 7, 1, FM_INT, false, 2},
  {ID_CLKCOL, K_CLKCOL, "Месяц",        0, 7, 1, FM_INT, false, 3},
  {ID_CLKCOL, K_CLKCOL, "Секунды",      0, 7, 1, FM_INT, false, 4},
};

// Какой цвет правит строка вкладки «Часы»
static uint32_t& clockColorSlot(int row) {
  switch (row) {
    case 1:  return g_dateColor;
    case 2:  return g_weekdayColor;
    case 3:  return g_monthColor;
    case 4:  return g_secColor;
    default: return g_clockColor;
  }
}

struct Page { const char* title; const Ctl* c; int n; };
static const Page PAGES[] = {
  {"Экран",      PG_SCREEN, (int)(sizeof(PG_SCREEN) / sizeof(Ctl))},
  {"Звук",       PG_SOUND,  (int)(sizeof(PG_SOUND) / sizeof(Ctl))},
  {"Время",      PG_TIME,   (int)(sizeof(PG_TIME) / sizeof(Ctl))},
  {"Будильник",  PG_ALARM,  (int)(sizeof(PG_ALARM) / sizeof(Ctl))},
  {"Строки",     PG_LINES,  (int)(sizeof(PG_LINES) / sizeof(Ctl))},
  {"Иконки",     PG_ICONS,  (int)(sizeof(PG_ICONS) / sizeof(Ctl))},
  {"Часы",       PG_CLOCKCOL, (int)(sizeof(PG_CLOCKCOL) / sizeof(Ctl))},
  {"Шрифт",      PG_TEXTFONT, (int)(sizeof(PG_TEXTFONT) / sizeof(Ctl))},
};
static const char* DAY_NAMES[] = {"Пн", "Вт", "Ср", "Чт", "Пт", "Сб", "Вс"};
static const char* VOICE_NAMES[] = {"Женский", "Мужской", "Куранты", "Камин"};

static int      pageIdx = 0;
static int      selDay = 0;
static bool     wasDown = true;
static bool     closeReq = false;
static uint32_t lastActivity = 0;
static int      heldCtl = -1, heldDir = 0;
static uint32_t nextRepeat = 0;

static int wdayOf(int uiDay) { return (uiDay + 1) % 7; }

static int getVal(Id id) {
  int wd = wdayOf(selDay);
  switch (id) {
    case ID_BR_DAY:      return g_set.brDay;
    case ID_BR_NIGHT:    return g_set.brNight;
    case ID_NIGHT_START: return g_set.nightStart;
    case ID_NIGHT_END:   return g_set.nightEnd;
    case ID_CLOCK_NIGHT: return g_set.clockAtNight;
    case ID_CLOCK_FONT:  return g_clockFont;
    case ID_CLOCK_LAYOUT: return g_clockLayout;
    case ID_TXTFONT:     return g_textFont;
    case ID_VOL_AL_D:    return g_set.volAlarmDay;
    case ID_VOL_AL_N:    return g_set.volAlarmNight;
    case ID_VOL_CH_D:    return g_set.volChimeDay;
    case ID_VOL_CH_N:    return g_set.volChimeNight;
    case ID_CHIME_D:     return g_set.chimeDay;
    case ID_CHIME_N:     return g_set.chimeNight;
    case ID_VOICE:       return g_set.chimeVoice;
    case ID_AUTOSTOP:    return g_set.alarmAutoStopMin;
    case ID_AL_EN:       return g_set.alarmEn[wd];
    case ID_AL_H:        return g_set.alarmH[wd];
    case ID_AL_M:        return g_set.alarmM[wd];
    case ID_AL_TRACK:    return g_set.alarmTrack[wd];
    case ID_LN_SPD0:     return g_set.line[0].speed;
    case ID_LN_SPD1:     return g_set.line[1].speed;
    case ID_LN_SPD2:     return g_set.line[2].speed;
    default:             return 0;
  }
}

static void setVal(Id id, int v) {
  int wd = wdayOf(selDay);
  switch (id) {
    case ID_BR_DAY:      g_set.brDay = v;        brightnessSetPercent(v); break;
    case ID_BR_NIGHT:    g_set.brNight = v;      brightnessSetPercent(v); break;
    case ID_NIGHT_START: g_set.nightStart = v;   break;
    case ID_NIGHT_END:   g_set.nightEnd = v;     break;
    case ID_CLOCK_NIGHT: g_set.clockAtNight = v != 0; break;
    case ID_CLOCK_FONT:  g_clockFont = (uint8_t)v; break;
    case ID_CLOCK_LAYOUT: g_clockLayout = (uint8_t)v; break;
    case ID_TXTFONT:     g_textFont = (uint8_t)v; break;
    case ID_VOL_AL_D:    g_set.volAlarmDay = v;  break;
    case ID_VOL_AL_N:    g_set.volAlarmNight = v; break;
    case ID_VOL_CH_D:    g_set.volChimeDay = v;  break;
    case ID_VOL_CH_N:    g_set.volChimeNight = v; break;
    case ID_CHIME_D:     g_set.chimeDay = v != 0;  break;
    case ID_CHIME_N:     g_set.chimeNight = v != 0; break;
    case ID_VOICE:       g_set.chimeVoice = v;   break;
    case ID_AUTOSTOP:    g_set.alarmAutoStopMin = v; break;
    case ID_AL_EN:       g_set.alarmEn[wd] = v != 0; break;
    case ID_AL_H:        g_set.alarmH[wd] = v;   break;
    case ID_AL_M:        g_set.alarmM[wd] = v;   break;
    case ID_AL_TRACK:    g_set.alarmTrack[wd] = v; break;
    case ID_LN_SPD0:     g_set.line[0].speed = v; break;
    case ID_LN_SPD1:     g_set.line[1].speed = v; break;
    case ID_LN_SPD2:     g_set.line[2].speed = v; break;
    default: break;
  }
}

static void formatVal(const Ctl& c, char* out, size_t n) {
  int v = getVal(c.id);
  switch (c.fmt) {
    case FM_PCT:  snprintf(out, n, "%d %%", v); break;
    case FM_TIME: snprintf(out, n, "%02d:%02d", v / 60, v % 60); break;
    case FM_2D:   snprintf(out, n, "%02d", v); break;
    case FM_3D:   snprintf(out, n, "%03d", v); break;
    case FM_VOICE: snprintf(out, n, "%s", VOICE_NAMES[v < 4 ? v : 0]); break;
    case FM_FONT:  snprintf(out, n, "%s", CLOCK_FONT_NAMES[v < CLOCK_FONT_COUNT ? v : 0]); break;
    case FM_LAYOUT: snprintf(out, n, "%s", CLOCK_LAYOUT_NAMES[v < CLOCK_LAYOUT_COUNT ? v : 0]); break;
    case FM_TXTFONT: snprintf(out, n, "%s", TEXT_FONT_NAMES[v < TEXT_FONT_COUNT ? v : 0]); break;
    default:      snprintf(out, n, "%d", v); break;
  }
}

static void txt(const char* s, int x, int y, const lgfx::IFont* f, uint16_t fg, uint16_t bg, textdatum_t d) {
  gfx.setFont(f);
  gfx.setTextColor(fg, bg);
  gfx.setTextDatum(d);
  gfx.drawString(s, x, y);
}

static void fit(const char* s, int x, int y, int maxW, uint16_t fg, uint16_t bg, textdatum_t d) {
  const lgfx::IFont* fs[] = {F_J24, F_J20, F_J16};
  const lgfx::IFont* use = fs[2];
  for (int i = 0; i < 3; i++) {
    gfx.setFont(fs[i]);
    if (gfx.textWidth(s) <= maxW) { use = fs[i]; break; }
  }
  txt(s, x, y, use, fg, bg, d);
}

static int rowY(int row) { return ROW_Y0 + row * ROW_H; }
// Строки вкладки «Иконки»: высота 84 px, чтобы под строкой поместился предпросмотр 110x64
static int iconRowY(int i) { return 78 + i * 84; }

static void drawButton(int x, int y, int w, int h, const char* label, bool big, bool on, uint16_t offText = C_TEXT, uint16_t customBg = 0) {
  uint16_t bg = customBg ? customBg : (on ? C_ACC : C_BTN);
  uint16_t fg = on ? C_BG : offText;
  gfx.fillRoundRect(x, y, w, h, h / 2, bg);
  if (big) txt(label, x + w / 2, y + h / 2, F_D40, fg, bg, textdatum_t::middle_center);
  else     fit(label, x + w / 2, y + h / 2, w - 8, fg, bg, textdatum_t::middle_center);
}

// Отрисовка векторной иконки перезагрузки (стрелка по кругу)
static void drawRebootIcon(int x, int y, int w, int h, uint16_t col) {
  int cx = x + w / 2;
  int cy = y + h / 2;
  int r = 12;

  // Дуга окружности (стрелка)
  for (int a = 30; a < 330; a += 15) {
    float rad = a * 0.0174533f;
    int px = cx + (int)lroundf(cosf(rad) * r);
    int py = cy + (int)lroundf(sinf(rad) * r);
    gfx.fillCircle(px, py, 2, col);
  }

  // Наконечник стрелки в верхней правой части
  gfx.fillTriangle(cx + r, cy - 6, cx + r + 6, cy - 2, cx + r - 2, cy + 4, col);
}

static int lineY(int i) { return LN_Y0 + i * LN_STEP; }
static uint16_t rgb16(uint32_t c) { return RGB16((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF); }

static void drawTabIcon(int idx, int cx, int cy, uint16_t fg, uint16_t bg) {
  switch (idx) {
    case 0: {
      gfx.fillCircle(cx, cy, 7, fg);
      for (int i = 0; i < 8; i++) {
        float a = i * 0.785398f;
        int x1 = cx + (int)lroundf(cosf(a) * 11);
        int y1 = cy + (int)lroundf(sinf(a) * 11);
        int x2 = cx + (int)lroundf(cosf(a) * 15);
        int y2 = cy + (int)lroundf(sinf(a) * 15);
        gfx.drawLine(x1, y1, x2, y2, fg);
      }
      break;
    }
    case 1: {
      int sx = cx - 4;
      gfx.fillRect(sx - 10, cy - 5, 6, 10, fg);
      gfx.fillTriangle(sx - 4, cy - 5, sx + 5, cy - 13, sx + 5, cy + 13, fg);
      gfx.fillTriangle(sx - 4, cy - 5, sx - 4, cy + 5, sx + 5, cy + 13, fg);
      for (int k = 0; k < 2; k++) {
        gfx.fillRoundRect(sx + 10 + k * 6, cy - 5 - k * 4, 3, 10 + k * 8, 1, fg);
      }
      break;
    }
    case 2: {
      gfx.fillCircle(cx, cy, 15, fg);
      gfx.fillCircle(cx, cy, 12, bg);
      gfx.fillRect(cx - 1, cy - 9, 3, 10, fg);
      gfx.fillRect(cx, cy - 1, 8, 3, fg);
      break;
    }
    case 3: {
      gfx.fillCircle(cx - 10, cy - 11, 5, fg);
      gfx.fillCircle(cx + 10, cy - 11, 5, fg);
      gfx.fillCircle(cx, cy + 2, 13, fg);
      gfx.fillCircle(cx, cy + 2, 10, bg);
      gfx.fillRect(cx - 1, cy - 5, 3, 8, fg);
      gfx.fillRect(cx, cy + 1, 6, 3, fg);
      break;
    }
    case 4: {
      for (int r = -1; r <= 1; r++) {
        int ry = cy + r * 8;
        gfx.fillCircle(cx - 14, ry, 2, fg);
        gfx.fillRoundRect(cx - 8, ry - 2, 24, 4, 2, fg);
      }
      break;
    }
    case 5: {   // иконки: рамка картинки с солнцем
      gfx.drawRoundRect(cx - 17, cy - 12, 34, 24, 4, fg);
      gfx.fillCircle(cx - 8, cy - 4, 4, fg);
      gfx.fillTriangle(cx - 17, cy + 12, cx - 5, cy + 12, cx - 17, cy + 1, fg);
      break;
    }
    case 7: {   // шрифт: буква А
      gfx.fillTriangle(cx - 13, cy + 13, cx + 13, cy + 13, cx, cy - 13, fg);
      gfx.fillTriangle(cx - 7, cy + 13, cx + 7, cy + 13, cx, cy - 3, bg);
      gfx.fillRect(cx - 9, cy + 5, 18, 3, fg);
      break;
    }
    case 6: {   // часы: палитра
      gfx.drawCircle(cx, cy, 15, fg);
      gfx.drawCircle(cx, cy, 14, fg);
      gfx.fillCircle(cx - 5, cy - 5, 3, fg);
      gfx.fillCircle(cx + 5, cy - 4, 3, fg);
      gfx.fillCircle(cx - 1, cy + 6, 3, fg);
      break;
    }
  }
}

static void drawCtl(const Ctl& c) {
  int y = (c.kind == K_LINE) ? lineY(c.row) : (c.kind == K_ICON ? iconRowY(c.row) : rowY(c.row));
  int clearH = (c.kind == K_LINE) ? 102 : (c.kind == K_ICON ? 80 : BTN_H + 8);
  gfx.fillRect(0, y - 4, 800, clearH, C_BG);

  switch (c.kind) {
    case K_STEP: {
      fit(c.label, 24, y + BTN_H / 2, X_MINUS - 40, C_TEXT, C_BG, textdatum_t::middle_left);
      drawButton(X_MINUS, y, BTN_W, BTN_H, "-", true, false);
      char v[16];
      formatVal(c, v, sizeof(v));
      if (c.fmt == FM_VOICE || c.fmt == FM_FONT || c.fmt == FM_LAYOUT || c.fmt == FM_TXTFONT) fit(v, X_VAL, y + BTN_H / 2, 92, C_ACC, C_BG, textdatum_t::middle_center);
      else                   txt(v, X_VAL, y + BTN_H / 2, F_D24, C_ACC, C_BG, textdatum_t::middle_center);
      drawButton(X_PLUS, y, BTN_W, BTN_H, "+", true, false);
      break;
    }
    case K_TOGGLE: {
      fit(c.label, 24, y + BTN_H / 2, X_MINUS - 40, C_TEXT, C_BG, textdatum_t::middle_left);
      bool on = getVal(c.id) != 0;
      drawButton(X_MINUS, y, X_PLUS + BTN_W - X_MINUS, BTN_H, on ? "ВКЛ" : "ВЫКЛ", false, on);
      break;
    }
    case K_DAYSEL: {
      for (int i = 0; i < 7; i++) {
        bool sel = (i == selDay);
        bool en = g_set.alarmEn[wdayOf(i)];
        drawButton(24 + i * 108, y, 98, BTN_H, DAY_NAMES[i], false, sel, en ? C_ACC : C_DIM);
      }
      break;
    }
    case K_ACTION: {
      fit(c.label, 24, y + BTN_H / 2, X_MINUS - 40, C_TEXT, C_BG, textdatum_t::middle_left);
      bool playing = (c.id == ID_TEST_CHIME) ? chimeTestActive() : alarmTestActive();
      drawButton(X_MINUS, y, X_PLUS + BTN_W - X_MINUS, BTN_H, playing ? "СТОП" : "ИГРАТЬ", false, playing);
      break;
    }
    case K_LINE: {
      const LineCfg& L = g_set.line[c.row];
      fit(CLOCK_LINE_NAMES[c.row], 24, y + BTN_H / 2, 150, C_TEXT, C_BG, textdatum_t::middle_left);
      drawButton(190, y, 96, BTN_H, L.en ? "ВКЛ" : "ВЫКЛ", false, L.en);
      drawButton(330, y, 76, BTN_H, "-", true, false);
      char v[8];
      snprintf(v, sizeof(v), "%d", L.speed);
      txt(v, 447, y + BTN_H / 2, F_D24, C_ACC, C_BG, textdatum_t::middle_center);
      drawButton(498, y, 76, BTN_H, "+", true, false);
      txt("скорость", 590, y + BTN_H / 2, F_J16, C_DIM, C_BG, textdatum_t::middle_left);
      for (int k = 0; k < 8; k++) {
        int sx = 24 + k * 92;
        gfx.fillRoundRect(sx, y + 56, 84, 38, 19, rgb16(PALETTE[k]));
        if (L.rgb == PALETTE[k]) gfx.drawRoundRect(sx - 3, y + 53, 90, 44, 22, C_TEXT);
      }
      break;
    }
    case K_ICON: {
      const int i = c.row;
      const bool sel = (g_iconStyle == i);
      if (sel) gfx.drawRoundRect(16, y - 6, 768, 76, 38, C_ACC);
      const tImage* im = iconByStyle((uint8_t)i, "02d");
      if (im) uiPreviewIcon(im, 34, y + 3, "02d", (uint8_t)i);
      fit(ICON_STYLE_NAMES[i], 168, y + 32, 420, sel ? C_ACC : C_TEXT, C_BG, textdatum_t::middle_left);
      gfx.fillCircle(742, y + 32, 14, sel ? C_ACC : C_BTN);
      if (sel) gfx.fillCircle(742, y + 32, 6, C_BG);
      break;
    }
    case K_CLKCOL: {
      fit(c.label, 24, y + BTN_H / 2, 240, C_TEXT, C_BG, textdatum_t::middle_left);
      const uint32_t cur = clockColorSlot(c.row) & 0xFFFFFF;
      for (int k = 0; k < 8; k++) {
        int sx = 280 + k * 62;
        gfx.fillRoundRect(sx, y + 9, 56, 30, 15, rgb16(PALETTE[k]));
        if (cur == PALETTE[k]) gfx.drawRect(sx - 2, y + 7, 60, 34, C_TEXT);
      }
      break;
    }
    case K_INFO: break;
  }
}

static void drawPage() {
  gfx.fillScreen(C_BG);

  for (int i = 0; i < NPAGES; i++) {
    bool act = (i == pageIdx);
    int x = TAB_X0 + i * TAB_STEP;
    uint16_t bg = act ? C_ACC : C_BTN;
    uint16_t fg = act ? C_BG : C_TEXT;
    gfx.fillRoundRect(x, TAB_Y, TAB_W, TAB_H, TAB_H / 2, bg);
    drawTabIcon(i, x + TAB_W / 2, TAB_Y + TAB_H / 2, fg, bg);
  }
  
  // Кнопка "Назад"
  gfx.fillRoundRect(BACK_X, BACK_Y, BACK_W, BACK_H, BACK_H / 2, C_BTN);
  fit("Назад", BACK_X + BACK_W / 2, BACK_Y + BACK_H / 2, BACK_W - 12, C_TEXT, C_BTN, textdatum_t::middle_center);

  // Кнопка перезагрузки в виде иконки слева от "Назад"
  uint16_t rstBg = RGB16(80, 25, 25);
  gfx.fillRoundRect(RST_X, RST_Y, RST_W, RST_H, RST_H / 2, rstBg);
  drawRebootIcon(RST_X, RST_Y, RST_W, RST_H, C_TEXT);

  gfx.drawFastHLine(24, 70, 752, C_LINE);

  const Page& p = PAGES[pageIdx];
  for (int i = 0; i < p.n; i++) drawCtl(p.c[i]);
}

static bool inRect(int px, int py, int x, int y, int w, int h) {
  return px >= x - PAD && px < x + w + PAD && py >= y - PAD && py < y + h + PAD;
}

static void stepCtl(const Ctl& c, int dir) {
  int v = getVal(c.id) + dir * c.step;
  if (v > c.vmax) v = c.wrap ? c.vmin : c.vmax;
  if (v < c.vmin) v = c.wrap ? c.vmax : c.vmin;
  setVal(c.id, v);
  drawCtl(c);
}

static void handlePress(int x, int y) {
  if (y < 66) {
    for (int i = 0; i < NPAGES; i++) {
      if (inRect(x, y, TAB_X0 + i * TAB_STEP, TAB_Y, TAB_W, TAB_H)) {
        if (i != pageIdx) {
          pageIdx = i;
          alarmTestStop();
          chimeTestStop();
          brightnessAuto();
          drawPage();
        }
        return;
      }
    }
    return;
  }
  
  if (inRect(x, y, BACK_X, BACK_Y, BACK_W, BACK_H)) { closeReq = true; return; }

  // Нажатие на иконку перезагрузки
  if (inRect(x, y, RST_X, RST_Y, RST_W, RST_H)) {
    gfx.fillScreen(C_BG);
    fit("Перезагрузка...", 400, 240, 600, C_ACC, C_BG, textdatum_t::middle_center);
    delay(800);
    ESP.restart();
    return;
  }

  const Page& p = PAGES[pageIdx];
  for (int i = 0; i < p.n; i++) {
    const Ctl& c = p.c[i];
    int ry = rowY(c.row);
    if (c.kind == K_STEP) {
      if (inRect(x, y, X_MINUS, ry, BTN_W, BTN_H)) { stepCtl(c, -1); heldCtl = i; heldDir = -1; return; }
      if (inRect(x, y, X_PLUS, ry, BTN_W, BTN_H))  { stepCtl(c, +1); heldCtl = i; heldDir = +1; return; }
    } else if (c.kind == K_TOGGLE) {
      if (inRect(x, y, X_MINUS, ry, X_PLUS + BTN_W - X_MINUS, BTN_H)) {
        setVal(c.id, getVal(c.id) ? 0 : 1);
        drawCtl(c);
        if (c.id == ID_AL_EN) drawCtl(p.c[0]);
        return;
      }
    } else if (c.kind == K_DAYSEL) {
      for (int d = 0; d < 7; d++) {
        if (inRect(x, y, 24 + d * 108, ry, 98, BTN_H)) {
          if (d != selDay) { selDay = d; alarmTestStop(); chimeTestStop(); drawPage(); }
          return;
        }
      }
    } else if (c.kind == K_LINE) {
      int li = c.row, ly = lineY(li);
      if (inRect(x, y, 190, ly, 96, BTN_H)) { g_set.line[li].en = !g_set.line[li].en; drawCtl(c); return; }
      if (inRect(x, y, 330, ly, 76, BTN_H)) { stepCtl(c, -1); heldCtl = i; heldDir = -1; return; }
      if (inRect(x, y, 498, ly, 76, BTN_H)) { stepCtl(c, +1); heldCtl = i; heldDir = +1; return; }
      for (int k = 0; k < 8; k++) {
        if (inRect(x, y, 24 + k * 92, ly + 56, 84, 38)) { g_set.line[li].rgb = PALETTE[k]; drawCtl(c); return; }
      }
    } else if (c.kind == K_ICON) {
      if (inRect(x, y, 16, iconRowY(c.row) - 6, 768, 76)) {
        iconsSetStyle((uint8_t)c.row);
        drawPage();
        return;
      }
    } else if (c.kind == K_CLKCOL) {
      for (int k = 0; k < 8; k++) {
        if (inRect(x, y, 280 + k * 62, ry + 9, 56, 30)) {
          clockColorSlot(c.row) = PALETTE[k];
          drawCtl(c);
          return;
        }
      }
    } else if (c.kind == K_ACTION) {
      if (inRect(x, y, X_MINUS, ry, X_PLUS + BTN_W - X_MINUS, BTN_H)) {
        if (c.id == ID_TEST_CHIME) {
          tm tt;
          chimeTestToggle(getLocalTime(&tt, 0) ? tt.tm_hour : 12);
        } else {
          alarmTestToggle(wdayOf(selDay));
        }
        drawCtl(c);
        return;
      }
    }
  }
}

void uiSettingsOpen() {
  pageIdx = 0;
  closeReq = false;
  wasDown = true;
  heldCtl = -1;
  lastActivity = millis();
  drawPage();
}

bool uiSettingsLoop() {
  uint32_t now = millis();
  int32_t x, y;
  bool down = gfx.getTouch(&x, &y);

  if (down) {
    lastActivity = now;
    if (!wasDown) {
      handlePress(x, y);
      nextRepeat = now + 450;
    } else if (heldCtl >= 0 && now >= nextRepeat) {
      stepCtl(PAGES[pageIdx].c[heldCtl], heldDir);
      nextRepeat = now + 110;
    }
  } else {
    heldCtl = -1;
  }
  wasDown = down;

  static bool lastAlarmTest = false, lastChimeTest = false;
  bool ta = alarmTestActive(), tc = chimeTestActive();
  if (pageIdx == 3 && ta != lastAlarmTest) drawCtl(PG_ALARM[5]);
  if (pageIdx == 2 && tc != lastChimeTest) drawCtl(PG_TIME[5]);
  lastAlarmTest = ta;
  lastChimeTest = tc;

  if (closeReq || now - lastActivity > IDLE_MS) {
    alarmTestStop();
    chimeTestStop();
    return false;
  }
  return true;
}