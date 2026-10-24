#pragma once
#include <Arduino.h>
#include <time.h>

// Озвучивание времени каждый час (схема как в PixelHub 75):
// /<папка голоса>/HHH.mp3, где HHH - час 000..023.
// Голоса (папки на SD): 0 - женский "22", 1 - мужской "23", 2 - куранты "12", 3 - каминные часы "13".
// Днём/ночью включается и громкость задаются отдельно в настройках.

void chimeTick(const tm& now);        // вызывать раз в секунду

// Проверка из настроек: проигрывает файл текущего часа выбранным голосом
void chimeTestToggle(int hour);
bool chimeTestActive();
void chimeTestStop();
