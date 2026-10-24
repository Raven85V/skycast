#pragma once

// ====== ЗНАЧЕНИЯ ПО УМОЛЧАНИЮ (первый запуск) ======
// Править необязательно: если Wi-Fi не подключится, станция поднимет точку доступа
// "CrowPanel-Weather" - там всё вводится в браузере и хранится во флеш-памяти.
#define WIFI_SSID     "your-wifi"
#define WIFI_PASS     "your-password"

// Ключ API: https://home.openweathermap.org/api_keys (бесплатного тарифа достаточно)
#define OWM_API_KEY   "your-openweathermap-key"

// Координаты места (по умолчанию Москва) и название на экране
#define OWM_LAT       "55.7558"
#define OWM_LON       "37.6173"
#define CITY_NAME     "Москва"

// Часовой пояс POSIX (Москва = MSK-3) и NTP
#define TZ_INFO       "MSK-3"
#define NTP_SERVER    "pool.ntp.org"

// ШИМ-регулировка подсветки на GPIO 2 (PIN_BL). Если яркость на вашей плате
// не меняется - поставьте 0: подсветка будет просто включена, ползунки яркости ничего не делают.
#ifndef BACKLIGHT_PWM
#define BACKLIGHT_PWM 1
#endif

// Стартовая заставка: название выплывает и рассыпается на пиксели (1 - включена, 0 - выключена)
#define SPLASH_ANIM   1
#define SPLASH_TITLE  "SKYCAST"            // только латиница (крупный шрифт без кириллицы)
#define SPLASH_SUB    "Метеостанция"   // подпись для стиля 0
// Стиль: 0 - название выплывает и рассыпается на пиксели; 1 - название + три строки выплывают по очереди,
// строки осыпаются вниз, название взрывается
#define SPLASH_STYLE  1
#define SPLASH_LINE1  "Метеостанция"
#define SPLASH_LINE3  "Часы"

// ====== Тайминги ======
#define WEATHER_REFRESH_MS  (10UL * 60UL * 1000UL)  // обновление погоды раз в 10 минут
#define RETRY_MS            (2UL * 60UL * 1000UL)   // повтор после ошибки (бережём лимит OWM)
