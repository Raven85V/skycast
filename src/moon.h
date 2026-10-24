#pragma once
#include <stdint.h>

// Фазы луны теперь лежат в файловой системе (data/moon.bin), а не во флеше:
// так экономится ~512 КБ под код. Формат файла: "MOON", u16 count, u16 w, u16 h, кадры RGB565.

// Загрузить набор (монтирует LittleFS, читает в PSRAM). true - успешно.
bool moonInit();

// Данные кадра по индексу; nullptr если набора нет.
const uint16_t* moonFrame(int idx);

int moonCount();
int moonW();
int moonH();
