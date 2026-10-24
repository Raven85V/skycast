#pragma once
#include <stdint.h>

// Структура из заголовка lcd-image-converter (в самих .h она закомментирована)
typedef struct {
  const uint16_t *data;   // RGB565
  uint16_t width;
  uint16_t height;
  uint8_t  dataSize;
} tImage;

// ---------- наборы иконок погоды ----------
// Все наборы лежат во флеш константными массивами и переключаются настройкой:
// страница настроек в веб-интерфейсе и экран «Настройки» -> вкладка «Иконки».
#define ICON_STYLE_COUNT 4
extern const char* const ICON_STYLE_NAMES[ICON_STYLE_COUNT];   // Мультяшный, Цветной, Яркий, Контурный

extern uint8_t g_iconStyle;      // 0..ICON_STYLE_COUNT-1; хранится в NVS (settings.cpp)

// Сменить набор. Экран перерисует main.cpp по флагу iconsStyleDirty().
void iconsSetStyle(uint8_t style);
// true один раз после iconsSetStyle (main перерисовывает текущий экран)
bool iconsStyleDirty();

// true - иконки набора уже нарисованы в цвете и выводятся как есть;
// false - контурные, их заливает ui.cpp по коду погоды (так станция выглядела раньше).
bool iconsStylePainted(uint8_t style);

// Иконка по коду OpenWeatherMap ("01d", "10n", ...) из текущего набора.
const tImage* iconByCode(const char* code);
// Иконка из конкретного набора - для предпросмотра в настройках.
const tImage* iconByStyle(uint8_t style, const char* code);
// Общая иконка (заставка / запасная).
const tImage* iconSplash();
const tImage* iconSplashStyle(uint8_t style);
