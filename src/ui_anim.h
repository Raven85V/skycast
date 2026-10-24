#pragma once
#include <Arduino.h>

// Загрузчик анимированных иконок из LittleFS (/icons.bin) в PSRAM.
// Формат: "ICN1", uint16 count, uint16 w, uint16 h, далее записи:
// char name[16], uint16 frames, кадры RGB565 w*h.
bool uiAnimInit();                                  // смонтировать FS и загрузить набор
int  uiAnimFrames(const char* name);                // сколько кадров у иконки (0 - нет)
const uint16_t* uiAnimFrame(const char* name, int idx);
int  uiAnimW(); int uiAnimH();
bool uiAnimReady();   // набор загружен?
void uiAnimDraw(const char* name, int x, int y, int w, int h);   // кадр по времени
