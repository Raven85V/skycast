#include "icons.h"
#include <string.h>

// Наборы иконок погоды (20.10.2026). Каждый .h определяет массивы и tImage в своём namespace,
// поэтому одинаковые имена (day_clearsky и т.д.) не конфликтуют:
//   icons/        - мультяшный (по образцу заказчика)   - рисуется как есть
//   icons_v17/    - цветной, мягкие синие облака        - рисуется как есть
//   icons_flat/   - яркий, плоские заливки              - рисуется как есть
//   icons_outline/- исходный контурный                  - ui.cpp заливает по коду погоды (как раньше)

namespace pack_cartoon {
}

namespace pack_color {
#include "icons_v17/01d.h"
#include "icons_v17/01n.h"
#include "icons_v17/02d.h"
#include "icons_v17/02n.h"
#include "icons_v17/03d.h"
#include "icons_v17/03n.h"
#include "icons_v17/04d.h"
#include "icons_v17/04n.h"
#include "icons_v17/09d.h"
#include "icons_v17/09n.h"
#include "icons_v17/10d.h"
#include "icons_v17/10n.h"
#include "icons_v17/11d.h"
#include "icons_v17/11n.h"
#include "icons_v17/13d.h"
#include "icons_v17/13n.h"
#include "icons_v17/50d.h"
#include "icons_v17/50n.h"
#include "icons_v17/weather.h"
}

namespace pack_bright {
#include "icons_flat/01d.h"
#include "icons_flat/01n.h"
#include "icons_flat/02d.h"
#include "icons_flat/02n.h"
#include "icons_flat/03d.h"
#include "icons_flat/03n.h"
#include "icons_flat/04d.h"
#include "icons_flat/04n.h"
#include "icons_flat/09d.h"
#include "icons_flat/09n.h"
#include "icons_flat/10d.h"
#include "icons_flat/10n.h"
#include "icons_flat/11d.h"
#include "icons_flat/11n.h"
#include "icons_flat/13d.h"
#include "icons_flat/13n.h"
#include "icons_flat/50d.h"
#include "icons_flat/50n.h"
#include "icons_flat/weather.h"
}

namespace pack_outline {
#include "icons_outline/01d.h"
#include "icons_outline/01n.h"
#include "icons_outline/02d.h"
#include "icons_outline/02n.h"
#include "icons_outline/03d.h"
#include "icons_outline/03n.h"
#include "icons_outline/04d.h"
#include "icons_outline/04n.h"
#include "icons_outline/09d.h"
#include "icons_outline/09n.h"
#include "icons_outline/10d.h"
#include "icons_outline/10n.h"
#include "icons_outline/11d.h"
#include "icons_outline/11n.h"
#include "icons_outline/13d.h"
#include "icons_outline/13n.h"
#include "icons_outline/50d.h"
#include "icons_outline/50n.h"
#include "icons_outline/weather.h"
}

const char* const ICON_STYLE_NAMES[ICON_STYLE_COUNT] = {"Пастельный (анимация)", "Цветной", "Яркий", "Контурный"};

struct IconEntry { const char* code; const tImage* img; };
struct IconPack { const IconEntry* list; int n; const tImage* splash; };

static const IconEntry kColor[] = {
  {"01d", &pack_color::day_clearsky},
  {"01n", &pack_color::night_clearsky},
  {"02d", &pack_color::day_fewclouds},
  {"02n", &pack_color::night_fewclouds},
  {"03d", &pack_color::day_scatteredclouds},
  {"03n", &pack_color::night_scatteredclouds},
  {"04d", &pack_color::day_brokenclouds},
  {"04n", &pack_color::night_brokenclouds},
  {"09d", &pack_color::day_rain},
  {"09n", &pack_color::night_rain},
  {"10d", &pack_color::day_showerrain},
  {"10n", &pack_color::night_showerrain},
  {"11d", &pack_color::day_thunderstorm},
  {"11n", &pack_color::night_thunderstorm},
  {"13d", &pack_color::day_snow},
  {"13n", &pack_color::night_snow},
  {"50d", &pack_color::day_mist},
  {"50n", &pack_color::night_mist},
};
static const IconEntry kBright[] = {
  {"01d", &pack_bright::day_clearsky},
  {"01n", &pack_bright::night_clearsky},
  {"02d", &pack_bright::day_fewclouds},
  {"02n", &pack_bright::night_fewclouds},
  {"03d", &pack_bright::day_scatteredclouds},
  {"03n", &pack_bright::night_scatteredclouds},
  {"04d", &pack_bright::day_brokenclouds},
  {"04n", &pack_bright::night_brokenclouds},
  {"09d", &pack_bright::day_rain},
  {"09n", &pack_bright::night_rain},
  {"10d", &pack_bright::day_showerrain},
  {"10n", &pack_bright::night_showerrain},
  {"11d", &pack_bright::day_thunderstorm},
  {"11n", &pack_bright::night_thunderstorm},
  {"13d", &pack_bright::day_snow},
  {"13n", &pack_bright::night_snow},
  {"50d", &pack_bright::day_mist},
  {"50n", &pack_bright::night_mist},
};
static const IconEntry kOutline[] = {
  {"01d", &pack_outline::day_clearsky},
  {"01n", &pack_outline::night_clearsky},
  {"02d", &pack_outline::day_fewclouds},
  {"02n", &pack_outline::night_fewclouds},
  {"03d", &pack_outline::day_scatteredclouds},
  {"03n", &pack_outline::night_scatteredclouds},
  {"04d", &pack_outline::day_brokenclouds},
  {"04n", &pack_outline::night_brokenclouds},
  {"09d", &pack_outline::day_rain},
  {"09n", &pack_outline::night_rain},
  {"10d", &pack_outline::day_showerrain},
  {"10n", &pack_outline::night_showerrain},
  {"11d", &pack_outline::day_thunderstorm},
  {"11n", &pack_outline::night_thunderstorm},
  {"13d", &pack_outline::day_snow},
  {"13n", &pack_outline::night_snow},
  {"50d", &pack_outline::day_mist},
  {"50n", &pack_outline::night_mist},
};

static const IconPack kPacks[ICON_STYLE_COUNT] = {
  { kColor, (int)(sizeof(kColor) / sizeof(IconEntry)), &pack_color::weather },
  { kColor, (int)(sizeof(kColor) / sizeof(IconEntry)), &pack_color::weather },
  { kBright, (int)(sizeof(kBright) / sizeof(IconEntry)), &pack_bright::weather },
  { kOutline, (int)(sizeof(kOutline) / sizeof(IconEntry)), &pack_outline::weather },
};

// Контурный набор (индекс 3) - единственный, который рисуется не как есть:
// ui.cpp заливает его по коду погоды (так станция выглядела до появления цветных наборов).
bool iconsStylePainted(uint8_t style) { return style != 3; }

const tImage* iconByStyle(uint8_t style, const char* code) {
  if (style >= ICON_STYLE_COUNT) style = 0;
  const IconPack& p = kPacks[style];
  if (code) {
    for (int i = 0; i < p.n; i++) {
      if (strncmp(code, p.list[i].code, 3) == 0) return p.list[i].img;
    }
  }
  return p.splash;
}

const tImage* iconByCode(const char* code) { return iconByStyle(g_iconStyle, code); }
const tImage* iconSplash() { return iconByStyle(g_iconStyle, nullptr); }
const tImage* iconSplashStyle(uint8_t style) { return iconByStyle(style, nullptr); }

static bool styleDirty = false;

void iconsSetStyle(uint8_t style) {
  if (style >= ICON_STYLE_COUNT) style = 0;
  if (style == g_iconStyle) return;
  g_iconStyle = style;
  styleDirty = true;
}

bool iconsStyleDirty() { bool d = styleDirty; styleDirty = false; return d; }
