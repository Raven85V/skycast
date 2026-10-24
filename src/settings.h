#pragma once
#include <Arduino.h>
#include <time.h>
#include <stdlib.h>
#include "config.h"   // значения по умолчанию для первого запуска

// Бегущая строка на экране часов: у каждой строки свои включение, скорость и цвет.
// Индексы: 0 - дата, 1 - погода сейчас, 2 - завтра (место для строк датчиков - увеличить счётчик).
#define CLOCK_LINE_COUNT 3
struct LineCfg { bool en; uint8_t speed; uint32_t rgb; };   // speed 1..10, rgb 0xRRGGBB
static const char* const CLOCK_LINE_NAMES[CLOCK_LINE_COUNT] = {"Дата", "Погода сейчас", "Завтра"};

// Все настройки с дневным и ночным значением. Хранятся во флеш (NVS).
// Будильники по дням недели, индекс = tm_wday (0 = воскресенье ... 6 = суббота),
// как в PixelHub 75.
struct Settings {
  uint8_t  ver = 7;

  // сеть и погода (правятся через веб-интерфейс; из config.h берутся только значения по умолчанию)
  char     wifiSsid[33] = WIFI_SSID;
  char     wifiPass[65] = WIFI_PASS;
  char     owmKey[41]   = OWM_API_KEY;
  uint8_t  wxSrc = 0;            // 0 - OpenWeatherMap, 1 - Pirate Weather
  char     pwKey[41]   = "";    // ключ Pirate Weather
  char     city[32]     = CITY_NAME;
  float    lat = (float)atof(OWM_LAT);
  float    lon = (float)atof(OWM_LON);
  int8_t   tzHours = 3;          // часовой пояс, часы от UTC (без перехода на летнее время)

  // поправки к показаниям комнатного датчика (как в PixelHub 75)
  float    tempOff = 0;       // °C
  float    humOff = 0;        // %
  float    presOffMm = 0;     // мм рт.ст.

  // экран, % (минимум 5, чтобы экран не погас совсем)
  uint8_t  brDay = 80;
  uint8_t  brNight = 15;
  bool     clockAtNight = true;  // ночью показывать экран часов
  LineCfg  line[CLOCK_LINE_COUNT] = {{true, 5, 0xF0F4FA}, {true, 5, 0xFACD5A}, {true, 5, 0x78BEEB}};

  // ночное окно, минуты от полуночи (может переходить через полночь)
  uint16_t nightStart = 23 * 60;
  uint16_t nightEnd   = 7 * 60;

  // громкость 0..21
  uint8_t  volAlarmDay = 15;
  uint8_t  volAlarmNight = 8;
  uint8_t  volChimeDay = 12;
  uint8_t  volChimeNight = 6;

  // озвучивание времени каждый час: днём / ночью
  bool     chimeDay = true;
  bool     chimeNight = false;
  uint8_t  chimeVoice = 0;      // 0 женский, 1 мужской, 2 куранты, 3 каминные часы

  // будильники по дням
  uint8_t  alarmAutoStopMin = 5;                       // через сколько минут сам замолчит
  bool     alarmEn[7]    = {false, false, false, false, false, false, false};
  uint8_t  alarmH[7]     = {7, 7, 7, 7, 7, 7, 7};
  uint8_t  alarmM[7]     = {0, 0, 0, 0, 0, 0, 0};
  uint16_t alarmTrack[7] = {1, 1, 1, 1, 1, 1, 1};      // /alarms/NNN.mp3
};

extern Settings g_set;

// Стиль цифр на экране часов. Хранится отдельным ключом NVS (структура Settings не менялась,
// чтобы заливка новой прошивки не стёрла сохранённые Wi-Fi и остальные настройки).
#define CLOCK_FONT_COUNT 7
// Показываем просто номера стилей: 1 - скруглённый, 2 - LED шестигранный, 3 - тонкий, 4 - блок,
// 5 - точечный, 6 - западный (каллиграфия, clock_western.h), 7 - перекидные карточки (clock_flip.h)
static const char* const CLOCK_FONT_NAMES[CLOCK_FONT_COUNT] = {"Стиль 1", "Стиль 2", "Стиль 3", "Стиль 4", "Стиль 5", "Стиль 6", "Стиль 7"};
extern uint8_t g_clockFont;   // 0..CLOCK_FONT_COUNT-1

// Шрифт декоративного текста (бегущая строка): 0 - встроенный, 1..3 - наборы из clock_marquee.h
#define TEXT_FONT_COUNT 3
static const char* const TEXT_FONT_NAMES[TEXT_FONT_COUNT] = {"Стандартный", "С засечками", "Контурный"};
extern uint8_t g_textFont;

// Цвета экрана часов (0xRRGGBB), у каждого свой ключ NVS.
extern uint32_t g_clockColor;     // цифры часов
extern uint32_t g_dateColor;      // дата (число)
extern uint32_t g_weekdayColor;   // день недели
extern uint32_t g_monthColor;     // месяц
extern uint32_t g_secColor;       // секунды
#define CLOCK_COLOR_DEFAULT 0xF0F4FA
#define DATE_COLOR_DEFAULT  0xF0F4FA
#define WD_COLOR_DEFAULT    0xA8B4C6
#define MONTH_COLOR_DEFAULT 0xFFC400
#define SEC_COLOR_DEFAULT   0xF0F4FA

// Раскладка экрана часов: 0 - как была (день недели/число/месяц справа), 1 - дата сверху,
// секунды внизу справа, над ними день недели (как в PixelHub)
#define CLOCK_LAYOUT_COUNT 2
static const char* const CLOCK_LAYOUT_NAMES[CLOCK_LAYOUT_COUNT] = {"1", "2"};
extern uint8_t g_clockLayout;

void settingsSanitize(Settings& s);   // привести значения к допустимым
void settingsLoad();
void settingsSave();

// Ночь ли в данную минуту суток (0..1439) по настройкам
bool isNightMinutes(int minuteOfDay);
// Ночь ли сейчас (если время не синхронизировано - считается днём)
bool nightNow();

// Подсветка
void brightnessInit();
void brightnessSetPercent(int percent);   // немедленно, независимо от времени суток
void brightnessAuto();                    // по текущему времени: день или ночь
