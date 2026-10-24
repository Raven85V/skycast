#pragma once
#include <time.h>
#include "weather.h"

// Экран часов (по мотивам главного экрана PixelHub 75): крупные сегментные цифры ЧЧ:ММ,
// секунды справа, день недели / число / месяц и бегущая строка с текстами по очереди
// (дата, погода сейчас, прогноз на завтра).

void uiClockOpen();                      // очистить экран и подготовить (потом вызывать uiClockTick)
void uiClockTick(const tm* t);           // вызывать часто (каждый проход loop): сам ограничивает частоту
void uiClockSetWeather(const Weather& w);

// Нарисовать ЧЧ:ММ выбранным стилем часов в прямоугольник высотой h (для второго экрана).
// Возвращает ширину. x, y - левый верхний угол.
int uiClockDrawTime(int x, int y, int h, int hour, int minute, uint16_t col, int second = -1);// новые данные погоды для бегущей строки
