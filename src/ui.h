#pragma once
#include <time.h>
#include "weather.h"
#include "icons.h"      // tImage (предпросмотр иконок в настройках)

void uiBegin();                                           // подготовка (спрайт часов)
void uiSplash(const char* line1, const char* line2);      // экран загрузки
void uiShowMain();                                        // очистить и нарисовать каркас
void uiTickClock(const tm* t);                            // вызывать часто; nullptr - время не синхронизировано
void uiDrawWeather(const Weather& w);                     // перерисовать блок погоды
void uiFooter(const char* left, bool error);              // строка состояния внизу слева
void uiAlarmShow(int hour, int minute);                   // экран сработавшего будильника

// Большая температура главного экрана с внешнего датчика (MQTT) вместо погоды из интернета.
// Число выводится с десятыми ("21.4"), без округления до целых.
// has = false - показывать температуру из погоды. redraw = true - сразу перерисовать число,
// но только если оно изменилось на экране (вызывать только когда показан главный экран).
void uiSetMainTemp(bool has, float t, bool redraw);

// Влажность (%) и давление (мм рт. ст.) главного экрана с внешнего датчика вместо погоды из интернета.
// На экране обе величины - целые ("45 %", "745 мм"), округление при выводе.
// humHas / presHas = false - для этой строки показывать значение из погоды. Рядом со значением с датчика - красная точка.
// redraw - как у uiSetMainTemp: перерисовать сразу, но только если на экране что-то изменилось.
void uiSetMainHumPres(bool humHas, float hum, bool presHas, float presMmHg, bool redraw);

// IP-адрес станции: всегда показывается справа внизу на главном экране (пустая строка - скрыть)
void uiSetIp(const char* ip);

// Кнопка радио в шапке главного экрана: подсвечена, пока радио играет.
// redraw = true - сразу перерисовать (только если сейчас показан главный экран)

// Предпросмотр иконки набора на экране настроек: тот же путь вывода, что и на главном экране
// (заливка по коду погоды или как есть), 1:1. style - номер набора (icons.h).
void uiPreviewIcon(const tImage* im, int x, int y, const char* code, uint8_t style);

// Короткий баннер поверх текущего экрана (например, "Telegram: погода").
// Рисуется один раз; стереть - перерисовать экран (restoreScreen в main.cpp).
void uiBanner(const char* text);

// Иконка погоды вдвое крупнее (как в карточке на главном экране) - для второго экрана
void uiDrawWeatherIcon2x(const char* code, int x, int y);

// Если код иконки - ясная ночь (01n), рисует текущую фазу луны (128x128) по центру области иконки.
// Возвращает true, если нарисовал (тогда обычную иконку рисовать не нужно).
bool uiDrawPhaseIcon(const char* code, int x, int y);
void uiTickIconAnim();
void uiTickForecastAnim();
const char* uiAnimNameFor(const char* code);
void uiInfoAnimTick();
