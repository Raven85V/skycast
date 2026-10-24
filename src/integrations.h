#pragma once
// ============================================================
// ИНТЕГРАЦИИ: MQTT (простой брокер / Home Assistant) и
// Народный мониторинг (narodmon.ru).
// Всё работает в отдельной FreeRTOS-задаче - loop() не блокируется,
// даже если брокер или сервер недоступны.
// Настройки хранятся в NVS (Preferences, пространство "integr"),
// поэтому НЕ стираются заливкой LittleFS и не требуют правок storage.cpp.
// ============================================================
#include <Arduino.h>

class AsyncWebServer;   // полностью объявлен в ESPAsyncWebServer.h

// Сколько значений можно принимать от каждого источника (MQTT / Народмон)
#define RX_ITEMS 4
// Источник значений главного экрана (mainTempSrc / mainHumSrc / mainPresSrc):
// 0 = интернет, 1..RX_ITEMS = MQTT-значение N, MAIN_SRC_WIFI = основной датчик по Wi-Fi (галочка на странице /settings/sensors)
#define MAIN_SRC_WIFI (RX_ITEMS + 1)
// Что именно (параметр kind в integrationsMainSource / integrationsMainMqtt)
#define MAIN_VAL_TEMP 0
#define MAIN_VAL_HUM  1
#define MAIN_VAL_PRES 2

struct IntegrationsConfig {
  // --- MQTT ---
  bool mqttEnabled = false;
  int mqttMode = 0;            // 0 = простой брокер (свои топики), 1 = Home Assistant (автообнаружение)
  String mqttHost = "";
  int mqttPort = 1883;
  String mqttUser = "";
  String mqttPass = "";
  String mqttBase = "skycast";   // корневой топик
  int mqttInterval = 60;        // секунд между отправками (мин. 10)
  bool mqttHome = true;         // домашний датчик
  bool mqttOutdoor = false;     // уличного датчика в метеостанции нет (поле оставлено для совместимости)
  bool mqttWeather = true;      // текущая погода из интернета
  bool mqttStatus = true;       // состояние устройства (RSSI, аптайм, память, IP)
  // --- Народный мониторинг ---
  bool nmEnabled = false;
  String nmName = "SKYCAST"; // имя устройства на narodmon.ru
  bool nmHome = true;
  bool nmOutdoor = false;
  // --- Пароль веб-настроек (логин admin; пусто = без защиты) ---
  String webPass = "";

  // --- ПРИЁМ по MQTT (подписка на чужие топики, вывод в бегущую строку) ---
  // Работает поверх подключения к брокеру: MQTT должен быть включён выше.
  bool mqttRxEnabled = false;
  String mqttRxTopic[RX_ITEMS];   // полный топик, например home/garage/temp
  String mqttRxLabel[RX_ITEMS];   // подпись в строке, например "Гараж"
  String mqttRxUnit[RX_ITEMS];    // единица, например "C" или "%"
  String mqttRxTextTopic = "";    // топик с готовым текстом (показывается как есть)
  int mqttRxStaleMin = 35;        // через сколько минут без новых данных значение пропадает (0 = не скрывать)
  int mainTempSrc = 0;            // источник температуры главного экрана (0 = интернет, 1..RX_ITEMS = MQTT-значение N, MAIN_SRC_WIFI = датчик по Wi-Fi)
  int mainHumSrc = 0;             // источник влажности главного экрана (те же значения)
  int mainPresSrc = 0;            // источник давления главного экрана (те же значения)
  int mainPresUnit = 0;           // единица давления в MQTT-значении: 0 = гПа, 1 = мм рт. ст.
  bool mqttRxLineOn = true;       // показывать строку "MQTT" в бегущей строке
  String mqttRxColor = "#40C0FF";
  int mqttRxSpeed = 5;            // 1..10

  // --- ПРИЁМ с Народного мониторинга (чтение чужих/своих датчиков по API) ---
  bool nmRxEnabled = false;
  String nmApiKey = "";           // ключ приложения (narodmon.ru, профиль -> мои приложения)
  String nmRxId[RX_ITEMS];        // ID датчика (число)
  String nmRxLabel[RX_ITEMS];
  String nmRxUnit[RX_ITEMS];
  int nmRxInterval = 10;          // минут между запросами (не меньше 5)
  int nmRxStaleMin = 60;          // через сколько минут без успешного запроса значения пропадают (0 = не скрывать)
  bool nmRxLineOn = true;
  String nmRxColor = "#80FF80";
  int nmRxSpeed = 5;
};

// Вызвать в setup() сразу после loadConfig()
void loadIntegrationsConfig();
// Вызвать в setup() после startWeatherTask() (т.е. когда Wi-Fi уже есть)
void startIntegrationsTask();
// Вызывать раз в секунду из loop()
void integrationsUpdateSnapshot();
// После каждого успешного обновления погоды (давление в гПа, ветер в м/с)
void integrationsSetWeather(float tempC, float humPct, float pressHpa, float windMs);

// Расширенные данные погоды для Home Assistant: подробные сущности (ощущается, облачность,
// направление ветра, минимум/максимум дня, восход/закат, описание и код иконки).
void integrationsSetWeatherEx(float feels, int clouds, int windDeg, float tmin, float tmax,
                              long sunrise, long sunset, const char* desc, const char* icon);

// Команды из Home Assistant (приходят по MQTT в <base>/cmd/...): забирает main.cpp
#define IG_CMD_NONE    0
#define IG_CMD_REBOOT  1
#define IG_CMD_SCREEN  2
#define IG_CMD_BRIGHT  3
#define IG_CMD_HOME    4
#define IG_CMD_CLOCK   5
// arg - значение для IG_CMD_BRIGHT (яркость в процентах), можно nullptr
int integrationsTakeCommand(int* arg = nullptr);

// Для веб-интерфейса
IntegrationsConfig getIntegrationsConfig();
void setIntegrationsConfig(const IntegrationsConfig &c);   // применяет и сохраняет в NVS
String integrationsMqttStatus();
String integrationsNarodmonStatus();
String integrationsMac();   // MAC без двоеточий, заглавными - для регистрации на narodmon.ru

// ---------- ПРИЁМ: строки для бегущей строки ----------
// which: 0 = MQTT, 1 = Народный мониторинг.
// Текст пустой, если строка выключена или нет свежих данных - drawScrollText() такие пропускает.
String integrationsRxLineText(int which);
uint32_t integrationsRxLineColor(int which);   // 0xRRGGBB
int integrationsRxLineSpeed(int which);        // 1..10
// Счётчик меняется, когда принятые данные реально изменились (по нему main.cpp пересобирает строки)
uint32_t integrationsRxVersion();
// Какой источник выбран для kind (MAIN_VAL_TEMP / MAIN_VAL_HUM / MAIN_VAL_PRES): 0, 1..RX_ITEMS или MAIN_SRC_WIFI
int integrationsMainSource(int kind);
// Значение для главного экрана из MQTT, если для kind выбрано MQTT-значение: °C, % или мм рт. ст. (давление пересчитано по единице из настроек).
// true - есть свежее числовое значение в разумных пределах; false - выбран не MQTT / нет данных / устарело / не число.
bool integrationsMainMqtt(int kind, float &v);
String integrationsMqttRxStatus();
String integrationsNmRxStatus();
// Регистрирует страницы /settings/integrations и /settings/receive (вызвать до server.begin())
void integrationsRegisterWeb(AsyncWebServer &srv);
// Проверка пароля веб-настроек: false - запрос пароля уже отправлен, обработчик должен выйти
class AsyncWebServerRequest;
bool integrationsAuth(AsyncWebServerRequest *req);
bool integrationsPasswordSet();                          // пароль веб-настроек задан
bool integrationsCheckAuth(AsyncWebServerRequest *req);  // верный ли пароль в запросе (ответ не отправляет)
