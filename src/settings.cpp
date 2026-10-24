#include "settings.h"
#include "config.h"
#include "lgfx_crowpanel.h"   // PIN_BL
#include "pins.h"              // BL_DUTY_MIN
#include <Preferences.h>
#include "icons.h"            // наборы иконок (значение хранится отдельным ключом NVS)

Settings g_set;
uint8_t g_clockFont = 0;
uint8_t g_clockLayout = 1;   // по умолчанию - дата сверху
uint8_t g_iconStyle = 0;     // набор иконок: 0 - мультяшный (см. icons.h)
uint32_t g_clockColor = CLOCK_COLOR_DEFAULT;   // цвет цифр часов
uint32_t g_dateColor = DATE_COLOR_DEFAULT;     // число
uint32_t g_weekdayColor = WD_COLOR_DEFAULT;    // день недели
uint32_t g_monthColor = MONTH_COLOR_DEFAULT;   // месяц
uint32_t g_secColor = SEC_COLOR_DEFAULT;       // секунды
uint8_t g_textFont = 0;      // шрифт бегущей строки

static const char* NVS_NS  = "wxstation";
static const char* NVS_KEY = "set";

static uint8_t clampU8(int v, int lo, int hi) { return (uint8_t)(v < lo ? lo : (v > hi ? hi : v)); }

void settingsSanitize(Settings& s) {
  s.wifiSsid[sizeof(s.wifiSsid) - 1] = 0;
  s.wifiPass[sizeof(s.wifiPass) - 1] = 0;
  s.owmKey[sizeof(s.owmKey) - 1] = 0;
  s.city[sizeof(s.city) - 1] = 0;
  if (s.tzHours < -12 || s.tzHours > 14) s.tzHours = 3;
  if (!(s.lat >= -90 && s.lat <= 90)) s.lat = 55.7558f;
  if (!(s.lon >= -180 && s.lon <= 180)) s.lon = 37.6173f;
  if (!(s.tempOff >= -10 && s.tempOff <= 10)) s.tempOff = 0;
  if (!(s.humOff >= -20 && s.humOff <= 20)) s.humOff = 0;
  if (!(s.presOffMm >= -30 && s.presOffMm <= 30)) s.presOffMm = 0;
  if (s.wxSrc > 1) s.wxSrc = 0;                    // 0 - OWM, 1 - Pirate Weather
  if (s.wxSrc == 1 && !s.pwKey[0]) s.wxSrc = 0;     // без ключа Pirate не имеет смысла
  s.brDay        = clampU8(s.brDay, 5, 100);
  s.brNight      = clampU8(s.brNight, 5, 100);
  if (s.nightStart > 1439) s.nightStart = 23 * 60;
  if (s.nightEnd > 1439)   s.nightEnd = 7 * 60;
  s.volAlarmDay   = clampU8(s.volAlarmDay, 0, 21);
  s.volAlarmNight = clampU8(s.volAlarmNight, 0, 21);
  s.volChimeDay   = clampU8(s.volChimeDay, 0, 21);
  s.volChimeNight = clampU8(s.volChimeNight, 0, 21);
  for (int i = 0; i < CLOCK_LINE_COUNT; i++) {
    s.line[i].speed = clampU8(s.line[i].speed, 1, 10);
    s.line[i].rgb &= 0xFFFFFF;
  }
  s.chimeVoice = clampU8(s.chimeVoice, 0, 3);
  s.alarmAutoStopMin = clampU8(s.alarmAutoStopMin, 1, 60);
  for (int d = 0; d < 7; d++) {
    s.alarmH[d] = clampU8(s.alarmH[d], 0, 23);
    s.alarmM[d] = clampU8(s.alarmM[d], 0, 59);
    if (s.alarmTrack[d] < 1 || s.alarmTrack[d] > 999) s.alarmTrack[d] = 1;
  }
}

void settingsLoad() {
  Settings def;
  g_set = def;
  Preferences p;
  if (!p.begin(NVS_NS, true)) return;
  g_clockFont = p.getUChar("clkfont", 0);
  if (g_clockFont >= CLOCK_FONT_COUNT) g_clockFont = 0;
  g_clockLayout = p.getUChar("clklay", 1);
  if (g_clockLayout >= CLOCK_LAYOUT_COUNT) g_clockLayout = 1;
  g_iconStyle = p.getUChar("icons", 0);
  if (g_iconStyle >= ICON_STYLE_COUNT) g_iconStyle = 0;
  g_clockColor = p.getUInt("clkcol", CLOCK_COLOR_DEFAULT) & 0xFFFFFF;
  g_dateColor = p.getUInt("dcol", DATE_COLOR_DEFAULT) & 0xFFFFFF;
  g_weekdayColor = p.getUInt("wdcol", WD_COLOR_DEFAULT) & 0xFFFFFF;
  g_monthColor = p.getUInt("mcol", MONTH_COLOR_DEFAULT) & 0xFFFFFF;
  g_secColor = p.getUInt("scol", SEC_COLOR_DEFAULT) & 0xFFFFFF;
  g_textFont = p.getUChar("txtfont", 0);
  if (g_textFont >= TEXT_FONT_COUNT) g_textFont = 0;
  if (p.getBytesLength(NVS_KEY) == sizeof(Settings)) {
    Settings tmp;
    p.getBytes(NVS_KEY, &tmp, sizeof(tmp));
    if (tmp.ver == def.ver) { settingsSanitize(tmp); g_set = tmp; }
  }
  p.end();
}

void settingsSave() {
  Preferences p;
  if (!p.begin(NVS_NS, false)) return;
  p.putBytes(NVS_KEY, &g_set, sizeof(g_set));
  p.putUChar("clkfont", g_clockFont);
  p.putUChar("clklay", g_clockLayout);
  p.putUChar("icons", g_iconStyle);
  p.putUInt("clkcol", g_clockColor);
  p.putUInt("dcol", g_dateColor);
  p.putUInt("wdcol", g_weekdayColor);
  p.putUInt("mcol", g_monthColor);
  p.putUInt("scol", g_secColor);
  p.putUChar("txtfont", g_textFont);
  p.end();
}

bool isNightMinutes(int m) {
  int s = g_set.nightStart, e = g_set.nightEnd;
  if (s == e) return false;                 // окно нулевой длины - ночи нет
  if (s < e) return m >= s && m < e;
  return m >= s || m < e;                   // через полночь
}

bool nightNow() {
  tm t;
  if (!getLocalTime(&t, 0)) return false;
  return isNightMinutes(t.tm_hour * 60 + t.tm_min);
}

// ---------- подсветка ----------
#if BACKLIGHT_PWM
static bool blPwmOn = false;   // пин сейчас отдан ШИМ (иначе просто HIGH)
#endif

void brightnessInit() {
  pinMode(PIN_BL, OUTPUT);
  brightnessSetPercent(g_set.brDay);
}

void brightnessSetPercent(int p) {
  if (p < 1) p = 1;
  if (p > 100) p = 100;
#if BACKLIGHT_PWM
  // Максимум: пин постоянно HIGH, без ШИМ. Любой ШИМ, даже 99%, на этой плате заметно темнее,
  // потому что драйвер подсветки при каждом коротком провале заново "разгоняется".
  if (p >= BL_FULL_PERCENT) {
    if (blPwmOn) { ledcDetach(PIN_BL); blPwmOn = false; }
    pinMode(PIN_BL, OUTPUT);
    digitalWrite(PIN_BL, HIGH);
    return;
  }
  if (!blPwmOn) {
    if (!ledcAttach(PIN_BL, BL_PWM_HZ, 8)) {
      Serial.println("ledcAttach для подсветки не удался - подсветка без ШИМ");
      pinMode(PIN_BL, OUTPUT);
      digitalWrite(PIN_BL, HIGH);
      return;
    }
    blPwmOn = true;
  }
  // линейно от BL_DUTY_MIN (при 1%) до 255 (при BL_FULL_PERCENT): нижние ступени теперь заметно ярче прежних
  int duty = BL_DUTY_MIN + (int)((255 - BL_DUTY_MIN) * (float)(p - 1) / (float)(BL_FULL_PERCENT - 1) + 0.5f);
  if (duty > 254) duty = 254;
  ledcWrite(PIN_BL, duty);
#else
  digitalWrite(PIN_BL, HIGH);
#endif
}

void brightnessAuto() {
  brightnessSetPercent(nightNow() ? g_set.brNight : g_set.brDay);
}
