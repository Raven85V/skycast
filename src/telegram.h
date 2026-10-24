#pragma once
// ============================================================
// TELEGRAM: уведомления в чат и команды боту.
//
// Станция сама пишет в чат (события) и отвечает на команды:
//   /status  - всё сразу: время, погода, датчики, сеть
//   /weather - погода сейчас и прогноз на дни
//   /temp    - комнатный датчик и датчики по Wi-Fi
//   /ip      - адрес, Wi-Fi, аптайм, память
//   /help    - справка
//
// Работает в отдельной FreeRTOS-задаче, поэтому медленный Telegram
// не блокирует экран, звук и MQTT. Настройки - в NVS (пространство "telegram"),
// отдельно от Settings, ключи часов и раскладки.
//
// Токен бота берётся у @BotFather (/newbot), id чата - @userinfobot
// или из ответа бота после первого сообщения ему.
// ============================================================
#include <Arduino.h>
#include "weather.h"

class AsyncWebServer;   // полностью объявлен в ESPAsyncWebServer.h

struct TelegramConfig {
  bool enabled  = false;   // включить интеграцию целиком
  bool onBoot   = true;    // сообщение при запуске станции
  bool onAlarm  = true;    // сообщение при срабатывании будильника
  bool commands = true;    // отвечать на команды боту
  int  statusMin = 0;      // периодическая сводка, минут (0 - не присылать)
  String token = "";       // токен бота вида 123456789:AA...
  String chat  = "";       // id чата, например 123456789 или -1001234567890

  // Внешние датчики (те, что шлют показания по Wi-Fi, см. wsensors.h)
  bool   sendSensors = false;  // присылать их показания
  int    sensorMin = 30;       // как часто, минут (0 - выключено)
  String sensorKeys = "";      // выбранные датчики: ключи "id/n" через перевод строки (пусто - все)
};

// setup(): загрузить настройки из NVS
void telegramInit();
// setup(), после появления Wi-Fi: запустить задачу
void telegramStart();
// до server.begin(): страница /settings/telegram
void telegramRegisterWeb(AsyncWebServer& srv);

TelegramConfig telegramGetConfig();
void telegramSetConfig(const TelegramConfig& c);   // применяет и сохраняет в NVS

// После каждого успешного обновления погоды - чтобы /weather и сводка были свежими
void telegramSetWeather(const Weather& w);

// Поставить сообщение в очередь (отправляется из своей задачи). Ничего не делает,
// если интеграция выключена или не заданы токен с чатом.
void telegramSend(const String& text, bool withMenu = false);   // withMenu - приложить кнопки управления

// Баннер на экране станции после нажатия кнопки в боте: main.cpp забирает строку и рисует её
bool telegramTakeBanner(char* out, size_t n);

void telegramNotifyBoot(const char* ip, int rssi);
void telegramNotifyAlarm(int hour, int minute);

String telegramStatus();   // строка состояния для страницы настроек
