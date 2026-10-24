#pragma once
// ============================================================
// ДАТЧИКИ ПО Wi-Fi: станция принимает показания от своих датчиков (ESP8266/ESP32).
// Датчик шлёт HTTP-запрос на станцию:
//   POST /api/sensor   тело JSON: {"id":"balkon","k":"ключ","s":[{"n":"bme1","t":12.3,"h":65,"p":1013.2},{"n":"ds1","t":11.8}]}
//   GET  /api/sensor?id=balkon&n=bme1&t=12.3&h=65&p=1013.2&k=ключ
// t - градусы C, h - влажность %, p - давление гПа. Любое поле можно не передавать.
// Каждый датчик получает ключ "id/n" (например balkon/bme1), его название и поправки задаются на странице /settings/sensors.
// Настройки хранятся в NVS (пространство "wsens"), значения - в памяти.
// ============================================================
#include <Arduino.h>

class AsyncWebServer;

void wsensorsInit();                              // в setup(): загрузка настроек из NVS
void wsensorsRegisterWeb(AsyncWebServer& srv);    // до server.begin(): /api/sensor и /settings/sensors

// Бегущая строка (пустая строка - нечего показывать)
String   wsensorsLineText();
uint32_t wsensorsLineColor();   // 0xRRGGBB
int      wsensorsLineSpeed();   // 1..10

// Первое свежее значение датчика с галочкой "основной": для будущих экранов
bool wsensorsMain(float& tempC, float& humPct, float& pressHpa);   // что-то из этого может быть NAN
bool wsensorsHome(float& tempC, float& humPct, float& pressHpa);   // датчик с галочкой «в доме»

// Выбор источника для экрана «Дома» по каждому показателю:
//   metric: 0 температура, 1 влажность, 2 давление
//   sel: 0 = авто (первый датчик со свежими данными), 1..N = номер датчика (1-based)
int  wsensorsSrc(int metric);
bool wsensorsPick(int sel, float& tempC, float& humPct, float& pressHpa);

// ---------- перечисление датчиков (для выбора в других настройках, например в Telegram) ----------
int  wsensorsCount();                                              // сколько датчиков известно
bool wsensorsInfo(int idx, String& key, String& label);            // idx = 0..count-1, без пропусков
// Показания датчика: чего нет - NAN. ageMs = 0, если данных ещё не было.
bool wsensorsValues(int idx, float& tempC, float& humPct, float& pressHpa, uint32_t& ageMs);
int  wsensorsStaleMin();                                           // через сколько минут значение считается старым
