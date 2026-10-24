#pragma once
#include <time.h>
#include "weather.h"

// Второй экран: часы поменьше, погода на сегодня, текущая температура,
// фаза луны и компас с направлением ветра.
void uiInfoOpen();                        // нарисовать экран целиком
void uiInfoTick(const tm* t);             // обновить часы (вызывать часто, сам ограничивает)
void uiInfoSetWeather(const Weather& w);  // новые данные погоды
