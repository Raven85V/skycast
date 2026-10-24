#include "telegram.h"
#include "integrations.h"
#include "sensors.h"
#include "settings.h"
#include "wsensors.h"

#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/idf_additions.h>   // xTaskCreatePinnedToCore
#include <math.h>
#include <time.h>

#define TG_NS         "telegram"     // пространство NVS (до 15 символов)
#define TG_QUEUE      6              // сообщений в очереди
#define TG_TEXT_MAX   3500           // лимит Telegram 4096, оставляем запас
#define TG_POLL_MS    2000UL         // как часто спрашивать команды (как в примере FastBot - часто)
#define TG_TASK_STACK 16384          // TLS-рукопожатию нужен запас стека

// ------------------------------------------------------------
// Состояние
// ------------------------------------------------------------
static TelegramConfig    g_cfg;
static SemaphoreHandle_t g_mux   = nullptr;   // настройки, очередь, счётчики
static SemaphoreHandle_t g_wxMux = nullptr;   // последняя погода

static Weather g_wx;
static bool    g_wxSet = false;

static String  g_q[TG_QUEUE];
static bool    g_qMenu[TG_QUEUE];   // к сообщению приложить кнопки управления
static int     g_qHead = 0, g_qCount = 0;

static uint32_t g_sent = 0, g_fail = 0;
static uint32_t g_pollOk = 0, g_pollErr = 0;   // опрос getUpdates: удачных / с ошибкой
static uint32_t g_recvMsg = 0, g_recvCb = 0;   // принято: текстовых команд / нажатий кнопок
static char     g_banner[48] = "";             // что показать на экране станции
static bool     g_bannerNew = false;
static String   g_lastErr;
static String   g_lastSeenChat;   // чат, который писал боту (подсказка при настройке)
static int64_t  g_lastUpdate = 0;

// Диагностика: без неё "ничего не приходит" невозможно отличить от "ничего не отправлялось"
static bool     g_taskOk = false;      // задача создана
static bool     g_needCommands = true; // нужно зарегистрировать список команд в Telegram
static int      g_lastCode = 0;        // последний HTTP-код от Telegram
static String   g_lastResp;            // начало последнего ответа
static String   g_botName;             // @имя бота после проверки getMe
static volatile bool g_checkBot = false;
static uint32_t g_rebootAskedAt = 0;   // когда запрошено подтверждение перезагрузки (millis)

// Мьютекс с автоматическим захватом/освобождением
struct Lock {
  explicit Lock(SemaphoreHandle_t m) : h(m) { if (h) xSemaphoreTake(h, portMAX_DELAY); }
  ~Lock() { if (h) xSemaphoreGive(h); }
  SemaphoreHandle_t h;
};

static void ensureMux() {
  if (!g_mux)   g_mux   = xSemaphoreCreateMutex();
  if (!g_wxMux) g_wxMux = xSemaphoreCreateMutex();
}

// ------------------------------------------------------------
// Настройки (NVS, отдельными ключами - структура может меняться)
// ------------------------------------------------------------
static void sanitizeCfg(TelegramConfig& c) {
  c.token.trim();
  c.chat.trim();
  if (c.statusMin < 0) c.statusMin = 0;
  if (c.statusMin > 1440) c.statusMin = 1440;
  if (c.sensorMin < 0) c.sensorMin = 0;
  if (c.sensorMin > 1440) c.sensorMin = 1440;
  c.sensorKeys.trim();
  if (c.sensorKeys.length() > 500) c.sensorKeys = c.sensorKeys.substring(0, 500);
  if (c.token.length() > 120) c.token = c.token.substring(0, 120);
  if (c.chat.length() > 40) c.chat = c.chat.substring(0, 40);
}

// Есть ли ключ датчика в списке выбранных (ключи разделены переводом строки)
static bool keySelected(const String& list, const String& key) {
  int p = 0;
  while (p <= (int)list.length()) {
    int e = list.indexOf('\n', p);
    if (e < 0) e = list.length();
    if (list.substring(p, e) == key) return true;
    if (e >= (int)list.length()) break;
    p = e + 1;
  }
  return false;
}

static void loadCfg() {
  Preferences p;
  if (!p.begin(TG_NS, true)) return;
  g_cfg.enabled   = p.getBool("en", false);
  g_cfg.onBoot    = p.getBool("boot", true);
  g_cfg.onAlarm   = p.getBool("alarm", true);
  g_cfg.commands  = p.getBool("cmd", true);
  g_cfg.statusMin = p.getInt("stmin", 0);
  g_cfg.token     = p.getString("token", "");
  g_cfg.chat      = p.getString("chat", "");
  g_cfg.sendSensors = p.getBool("sen", false);
  g_cfg.sensorMin   = p.getInt("senmin", 30);
  g_cfg.sensorKeys  = p.getString("senkeys", "");
  p.end();
  sanitizeCfg(g_cfg);
}

static void saveCfg() {
  Preferences p;
  if (!p.begin(TG_NS, false)) return;
  p.putBool("en", g_cfg.enabled);
  p.putBool("boot", g_cfg.onBoot);
  p.putBool("alarm", g_cfg.onAlarm);
  p.putBool("cmd", g_cfg.commands);
  p.putInt("stmin", g_cfg.statusMin);
  p.putString("token", g_cfg.token);
  p.putString("chat", g_cfg.chat);
  p.putBool("sen", g_cfg.sendSensors);
  p.putInt("senmin", g_cfg.sensorMin);
  p.putString("senkeys", g_cfg.sensorKeys);
  p.end();
}

void telegramInit() {
  ensureMux();
  loadCfg();
}

TelegramConfig telegramGetConfig() {
  ensureMux();
  Lock l(g_mux);
  return g_cfg;
}

void telegramSetConfig(const TelegramConfig& in) {
  ensureMux();
  {
    Lock l(g_mux);
    g_cfg = in;
    sanitizeCfg(g_cfg);
  }
  saveCfg();
  Serial.printf("Telegram: %s, чат '%s'\n", g_cfg.enabled ? "включён" : "выключен", g_cfg.chat.c_str());
}

void telegramSetWeather(const Weather& w) {
  ensureMux();
  Lock l(g_wxMux);
  g_wx = w;
  g_wxSet = true;
}

// ------------------------------------------------------------
// Очередь исходящих
// ------------------------------------------------------------
void telegramSend(const String& text, bool withMenu) {
  ensureMux();
  {
    Lock l(g_mux);
    if (!g_cfg.enabled || g_cfg.token.length() == 0 || g_cfg.chat.length() == 0) return;
  }
  String t = text;
  if (t.length() > TG_TEXT_MAX) t = t.substring(0, TG_TEXT_MAX) + "…";
  Lock l(g_mux);
  if (g_qCount == TG_QUEUE) {                     // очередь полна: выбрасываем самое старое
    g_qHead = (g_qHead + 1) % TG_QUEUE;
    g_qCount--;
  }
  const int slot = (g_qHead + g_qCount) % TG_QUEUE;
  g_q[slot] = t;
  g_qMenu[slot] = withMenu;
  g_qCount++;
}

// ------------------------------------------------------------
// Тексты сообщений
// ------------------------------------------------------------
static String fmtNow() {
  tm t;
  if (!getLocalTime(&t, 0)) return String("--:--");
  char b[40];
  strftime(b, sizeof(b), "%d.%m %H:%M", &t);
  return String(b);
}

static String uptimeText() {
  uint32_t s = millis() / 1000;
  char b[40];
  if (s < 3600)        snprintf(b, sizeof(b), "%u мин", (unsigned)(s / 60));
  else if (s < 86400)  snprintf(b, sizeof(b), "%u ч %u мин", (unsigned)(s / 3600), (unsigned)((s % 3600) / 60));
  else                 snprintf(b, sizeof(b), "%u сут %u ч", (unsigned)(s / 86400), (unsigned)((s % 86400) / 3600));
  return String(b);
}

static String weatherText(bool withForecast) {
  Weather w;
  bool have;
  {
    Lock l(g_wxMux);
    w = g_wx;
    have = g_wxSet;
  }
  if (!have || (!w.hasCur && w.dayCount == 0))
    return "Погода ещё не получена. Проверьте ключ OpenWeatherMap и подключение к Wi-Fi.";

  String o = "Погода, " + String(g_set.city) + ":";
  char b[200];
  if (w.hasCur) {
    const CurrentWx& c = w.cur;
    snprintf(b, sizeof(b), "%+.0f °C, ощущается %+.0f °C",
             (double)c.temp, (double)c.feels);
    o += "\n" + String(b);
    if (c.desc[0]) o += "\n" + String(c.desc);
    snprintf(b, sizeof(b), "Ветер %.1f м/с, влажность %d %%\nДавление %d мм рт. ст.",
             (double)c.wind, c.humidity, c.pressureMmHg);
    o += "\n" + String(b);
  }
  if (withForecast && w.dayCount > 0) {
    static const char* const WD[7] = {"Вс", "Пн", "Вт", "Ср", "Чт", "Пт", "Сб"};
    o += "\n\nПрогноз:";
    for (int i = 0; i < w.dayCount && i < 5; i++) {
      tm lt;
      localtime_r(&w.days[i].ts, &lt);
      snprintf(b, sizeof(b), "\n%s: %+.0f…%+.0f °C",
               WD[lt.tm_wday], (double)w.days[i].tmin, (double)w.days[i].tmax);
      o += b;
    }
  }
  return o;
}

static String sensorsText() {
  String o;
  String wl = wsensorsLineText();
  wl.trim();
  if (wl.length()) o += "\n\nДатчики по Wi-Fi:\n" + wl;
  return o;
}

// Показания выбранных внешних датчиков (по Wi-Fi). Пустая строка - слать нечего.
// Если в настройках не отмечен ни один - отправляются все известные.
static String externalSensorsText() {
  TelegramConfig c = telegramGetConfig();
  int staleMin = wsensorsStaleMin();
  uint32_t staleMs = staleMin > 0 ? (uint32_t)staleMin * 60000UL : 0;

  String out;
  int shown = 0;
  int total = wsensorsCount();
  for (int i = 0; i < total; i++) {
    String key, label;
    if (!wsensorsInfo(i, key, label)) continue;
    // шлём по всем датчикам: фильтр по выбранным ключам больше не применяется

    float t = NAN, h = NAN, p = NAN;
    uint32_t age = 0;
    if (!wsensorsValues(i, t, h, p, age)) continue;
    if (age == 0) continue;                       // данных ещё не было
    if (staleMs && age > staleMs) continue;       // датчик молчит

    char b[160];
    String part = label.length() ? label : key;
    part += ":";
    if (!isnan(t)) { snprintf(b, sizeof(b), " %+.1f °C", (double)t); part += b; }
    if (!isnan(h)) { snprintf(b, sizeof(b), ", %d %%", (int)lroundf(h)); part += b; }
    if (!isnan(p)) { snprintf(b, sizeof(b), ", %d мм рт. ст.", (int)lroundf(p * 0.750062f)); part += b; }

    if (out.length()) out += "\n";
    out += part;
    shown++;
  }
  if (shown == 0) return "";
  return "Датчики • " + fmtNow() + "\n" + out;
}

static String netText() {
  char b[200];
  snprintf(b, sizeof(b), "IP: %s\nWi-Fi: %d дБм\nАптайм: %s\nСвободно памяти: %u КБ",
           WiFi.localIP().toString().c_str(), WiFi.RSSI(), uptimeText().c_str(),
           (unsigned)(ESP.getFreeHeap() / 1024));
  return String(b);
}

static String helpText() {
  return String("Станция SKYCAST на связи.\n\n"
                "/status — всё сразу: время, погода, датчики, сеть\n"
                "/weather — погода и прогноз\n"
                "/temp — комнатный датчик и датчики по Wi-Fi\n"
                "/sensors — внешние датчики, выбранные для отправки\n"
                "/ip — адрес, Wi-Fi, аптайм, память\n"
                "/reboot — перезагрузить станцию (нужно подтверждение /reboot yes)\n"
                "/help — эта справка");
}

static String statusText() {
  return "SKYCAST • " + fmtNow() + "\n\n" + weatherText(false) + "\n\n" + sensorsText() + "\n\n" + netText() +
         "\nПрошивка: " + String(__DATE__) + " " + String(__TIME__);
}

// ------------------------------------------------------------
// Отправка
// ------------------------------------------------------------
static void notePollErr() { Lock l(g_mux); g_pollErr++; }

static void noteErr(const String& e) {
  Lock l(g_mux);
  g_fail++;
  g_lastErr = e;
  Serial.println("Telegram: " + e);
}

// Одно TLS-соединение на все запросы (keep-alive), как в примере на FastBot:
// меньше рукопожатий - меньше сбоев "connection refused" при одновременной погоде
static WiFiClientSecure g_tls;
static bool g_tlsReady = false;

static WiFiClientSecure& tlsClient() {
  if (!g_tlsReady) {
    g_tls.setInsecure();            // как в weather.cpp: сертификат не проверяем
    g_tls.setTimeout(10);
    g_tls.setHandshakeTimeout(10);
    g_tlsReady = true;
  }
  return g_tls;
}

static String apiUrl(const String& token, const char* method) {
  return String("https://api.telegram.org/bot") + token + "/" + method;
}

// Короткое описание ошибки из ответа Bot API
static String apiDesc(const String& resp) {
  if (!resp.length()) return String("пустой ответ");
  JsonDocument d;
  if (deserializeJson(d, resp)) return resp.substring(0, 80);
  String s = d["description"] | "";
  return s.length() ? s : resp.substring(0, 80);
}

// Кнопки под сообщением (inline_keyboard)
static String menuMarkup() {
  return String("{\"inline_keyboard\":["
                "[{\"text\":\"Статус\",\"callback_data\":\"st\"},{\"text\":\"Погода\",\"callback_data\":\"wx\"}],"
                "[{\"text\":\"Датчики\",\"callback_data\":\"sn\"},{\"text\":\"Сеть и IP\",\"callback_data\":\"ip\"}],"
                "[{\"text\":\"Яркость -10%\",\"callback_data\":\"bl-\"},{\"text\":\"Яркость +10%\",\"callback_data\":\"bl+\"}],"
                "[{\"text\":\"Перезагрузить\",\"callback_data\":\"rb\"}]]}");
}
static String rebootMarkup() {
  return String("{\"inline_keyboard\":[[{\"text\":\"Да, перезагрузить\",\"callback_data\":\"rby\"},"
                "{\"text\":\"Отмена\",\"callback_data\":\"rbn\"}]]}");
}

static bool sendNow(const String& token, const String& chat, const String& text, const String& markup = String()) {
  JsonDocument doc;
  doc["chat_id"] = chat;                       // строкой: подходит и число, и @канал
  doc["text"] = text;
  doc["disable_web_page_preview"] = true;

  String body;
  serializeJson(doc, body);
  // Разметку кнопок добавляем к готовому JSON строкой: так не зависим от того,
  // как ArduinoJson копирует объект из другого документа
  if (markup.length() && body.endsWith("}")) {
    body.remove(body.length() - 1);
    body += ",\"reply_markup\":";
    body += markup;
    body += "}";
  }

  // Отправка с повторами: при занятой сети или одновременном TLS-запросе погоды
  // первая попытка может упасть с "connection refused" (-1), вторая обычно проходит.
  int code = 0;
  String resp;
  for (int attempt = 0; attempt < 3; attempt++) {
    HTTPClient http;
    http.setConnectTimeout(8000);
    http.setTimeout(12000);
    http.setReuse(true);
    if (!http.begin(tlsClient(), apiUrl(token, "sendMessage"))) { noteErr("не удалось начать запрос"); return false; }
    http.addHeader("Content-Type", "application/json");

    code = http.POST(body);
    if (code > 0) resp = http.getString();
    http.end();
    if (code > 0) break;
    Serial.printf("Telegram: попытка %d не удалась (%d), повтор\n", attempt + 1, code);
    vTaskDelay(pdMS_TO_TICKS(400));
  }

  {
    Lock l(g_mux);
    g_lastCode = code;
    g_lastResp = resp.substring(0, 120);
  }

  if (code == 200) {
    {
      Lock l(g_mux);
      g_sent++;
      g_lastErr = "";
    }
    Serial.printf("Telegram: отправлено в чат %s (%u байт)\n", chat.c_str(), (unsigned)text.length());
    return true;
  }
  if (code == 401)      { noteErr("токен не принят (401) — проверьте токен бота"); return false; }
  if (code == 403)      { noteErr("бот заблокирован или не состоит в чате (403)"); return false; }
  if (code == 429)      { noteErr("слишком часто, Telegram просит подождать (429)"); return false; }
  if (code == 400)      { noteErr("запрос отклонён: " + apiDesc(resp) + " (проверьте id чата)"); return false; }
  if (code < 0)         { noteErr("нет связи с api.telegram.org: " + HTTPClient::errorToString(code) +
                                   " (проверьте интернет и блокировку Telegram в сети)"); return false; }
  noteErr("HTTP " + String(code) + ": " + apiDesc(resp));
  return false;
}

// getMe: проверяет токен и заодно узнаёт имя бота. Это первый вопрос при настройке -
// "токен верный, но сообщения не идут" и "токен неверный" лечатся по-разному.
static void checkBotNow(const String& token) {
  WiFiClientSecure cl;
  cl.setInsecure();
  cl.setHandshakeTimeout(10);

  HTTPClient http;
  http.setConnectTimeout(8000);
  http.setTimeout(15000);
  if (!http.begin(cl, apiUrl(token, "getMe"))) { noteErr("getMe: не удалось начать запрос"); return; }

  int code = http.GET();
  String resp;
  if (code > 0) resp = http.getString();
  http.end();

  {
    Lock l(g_mux);
    g_lastCode = code;
    g_lastResp = resp.substring(0, 120);
  }

  if (code != 200) {
    noteErr(code < 0 ? ("getMe: нет связи: " + HTTPClient::errorToString(code))
                     : ("getMe: " + apiDesc(resp)));
    return;
  }
  JsonDocument d;
  if (deserializeJson(d, resp)) { noteErr("getMe: ответ не разобран"); return; }
  String u = d["result"]["username"] | "";
  String nm = d["result"]["first_name"] | "";
  Lock l(g_mux);
  g_botName = u.length() ? ("@" + u) : (nm.length() ? nm : String("без имени"));
  g_lastErr = "";
  Serial.println("Telegram: бот " + g_botName);
}

// ------------------------------------------------------------
// Приём команд
// ------------------------------------------------------------
// id чата числом, либо @имя канала/группы
static bool chatMatches(const String& cfgChat, int64_t id, const String& user) {
  if (!cfgChat.length()) return false;
  if (cfgChat[0] == '@') return user.length() && cfgChat.substring(1).equalsIgnoreCase(user);
  return strtoll(cfgChat.c_str(), nullptr, 10) == (long long)id;
}

static void handleCommand(const String& token, const String& chat, String text) {
  text.trim();
  if (!text.startsWith("/")) return;

  int sp = text.indexOf(' ');
  String cmd = sp < 0 ? text : text.substring(0, sp);
  String arg = sp < 0 ? String() : text.substring(sp + 1);
  arg.trim();
  arg.toLowerCase();
  int at = cmd.indexOf('@');
  if (at > 0) cmd = cmd.substring(0, at);      // /status@MyBot -> /status
  cmd.toLowerCase();

  Serial.println("Telegram: команда " + cmd);
  if (cmd == "/start" || cmd == "/help")        sendNow(token, chat, helpText(), menuMarkup());
  else if (cmd == "/menu")                      sendNow(token, chat, "Меню управления:", menuMarkup());
  else if (cmd == "/status")                    sendNow(token, chat, statusText());
  else if (cmd == "/weather")                   sendNow(token, chat, weatherText(true));
  else if (cmd == "/temp")                      sendNow(token, chat, sensorsText());
  else if (cmd == "/sensors") {
    String s = externalSensorsText();
    sendNow(token, chat, s.length() ? s : String("Нет свежих показаний выбранных датчиков."));
  }
  else if (cmd == "/ip")                        sendNow(token, chat, netText());
  else if (cmd == "/reboot" || cmd == "/restart") {
    if (arg == "yes" || arg == "да") {
      // подтверждение живёт минуту, чтобы случайная команда не перезагрузила станцию
      if (g_rebootAskedAt && (uint32_t)(millis() - g_rebootAskedAt) < 60000UL) {
        g_rebootAskedAt = 0;
        sendNow(token, chat, "Перезагружаюсь. Через полминуты буду на связи.");
        vTaskDelay(pdMS_TO_TICKS(1000));       // дать сообщению уйти
        ESP.restart();
      } else {
        sendNow(token, chat, "Подтверждение устарело. Отправьте /reboot, затем /reboot yes.");
      }
    } else {
      g_rebootAskedAt = millis() ? millis() : 1;
      sendNow(token, chat, "Перезагрузить станцию?\nОтправьте /reboot yes в течение 60 секунд.");
    }
  }
  else                                          sendNow(token, chat, "Не знаю такую команду.\n\n" + helpText());
}

// Ответ на нажатие кнопки: убирает "часики" в клиенте Telegram
static void answerCallback(const String& token, const String& cbId, const String& text) {
  JsonDocument doc;
  doc["callback_query_id"] = cbId;
  if (text.length()) doc["text"] = text;
  String body;
  serializeJson(doc, body);

  HTTPClient http;
  http.setConnectTimeout(8000);
  http.setTimeout(10000);
  http.setReuse(true);
  if (!http.begin(tlsClient(), apiUrl(token, "answerCallbackQuery"))) return;
  http.addHeader("Content-Type", "application/json");
  http.POST(body);
  http.end();
}

// Баннер на экране станции (забирает main.cpp)
static void setBanner(const char* s) {
  Lock l(g_mux);
  strlcpy(g_banner, s, sizeof(g_banner));
  g_bannerNew = true;
}

bool telegramTakeBanner(char* out, size_t n) {
  Lock l(g_mux);
  if (!g_bannerNew) return false;
  g_bannerNew = false;
  strlcpy(out, g_banner, n);
  return true;
}

// Регистрирует список команд в Telegram (кнопка «Меню» в клиенте и подсказка по «/»)
static void setCommandsNow(const String& token) {
  const char* body =
    "{\"commands\":["
    "{\"command\":\"status\",\"description\":\"Статус станции\"},"
    "{\"command\":\"weather\",\"description\":\"Погода и прогноз\"},"
    "{\"command\":\"temp\",\"description\":\"Датчик в комнате\"},"
    "{\"command\":\"sensors\",\"description\":\"Датчики по Wi-Fi\"},"
    "{\"command\":\"menu\",\"description\":\"Меню с кнопками\"},"
    "{\"command\":\"ip\",\"description\":\"Сеть и IP\"},"
    "{\"command\":\"reboot\",\"description\":\"Перезагрузка\"},"
    "{\"command\":\"help\",\"description\":\"Справка\"}"
    "]}";
  HTTPClient http;
  http.setConnectTimeout(8000);
  http.setTimeout(10000);
  http.setReuse(true);
  if (!http.begin(tlsClient(), apiUrl(token, "setMyCommands"))) return;
  http.addHeader("Content-Type", "application/json");
  int code = http.POST((uint8_t*)body, strlen(body));
  http.end();
  Serial.printf("Telegram: список команд %s (%d)\n", code == 200 ? "обновлён" : "не обновлён", code);
}

// Нажатие кнопки в меню
static void handleCallback(const String& token, const String& chat, const String& cbId, const String& data) {
  Serial.println("Telegram: кнопка " + data);
  if (data == "st") {
    setBanner("Telegram: статус");
    answerCallback(token, cbId, "Статус");
    sendNow(token, chat, statusText(), menuMarkup());
  } else if (data == "wx") {
    setBanner("Telegram: погода");
    answerCallback(token, cbId, "Погода");
    sendNow(token, chat, weatherText(true), menuMarkup());
  } else if (data == "sn") {
    setBanner("Telegram: датчики");
    answerCallback(token, cbId, "Датчики");
    String s = externalSensorsText();
    sendNow(token, chat, s.length() ? s : String("Нет свежих показаний выбранных датчиков."), menuMarkup());
  } else if (data == "ip") {
    setBanner("Telegram: сеть и IP");
    answerCallback(token, cbId, "Сеть");
    sendNow(token, chat, netText(), menuMarkup());
  } else if (data == "bl+" || data == "bl-") {
    setBanner(data == "bl+" ? "Telegram: яркость +10%" : "Telegram: яркость -10%");
    int v = (int)g_set.brDay + (data == "bl+" ? 10 : -10);
    if (v < 5) v = 5;
    if (v > 100) v = 100;
    g_set.brDay = (uint8_t)v;
    brightnessSetPercent(v);
    settingsSave();
    char b[64];
    snprintf(b, sizeof(b), "Яркость днём: %d %%", v);
    answerCallback(token, cbId, b);
    sendNow(token, chat, String(b) + "\n(ночью действует отдельная яркость)", menuMarkup());
  } else if (data == "rb") {
    setBanner("Telegram: перезагрузка?");
    answerCallback(token, cbId, "Подтвердите");
    sendNow(token, chat, "Перезагрузить станцию?", rebootMarkup());
  } else if (data == "rby") {
    // как кнопка «Перезагрузить станцию» в веб-интерфейсе: сообщение, пауза, перезапуск
    setBanner("Перезагрузка...");
    answerCallback(token, cbId, "Перезагружаюсь");
    sendNow(token, chat, "Перезагружаюсь. Через полминуты буду на связи.");
    vTaskDelay(pdMS_TO_TICKS(1200));
    ESP.restart();
  } else if (data == "rbn") {
    answerCallback(token, cbId, "Отменено");
  } else {
    answerCallback(token, cbId, "Не понял кнопку");
  }
}

static void pollUpdates(const String& token, const String& chat, bool allowCommands) {
  String url = apiUrl(token, "getUpdates") + "?timeout=0&limit=10&offset=" +
               String((long long)g_lastUpdate + 1);

  HTTPClient http;
  http.setConnectTimeout(8000);
  http.setTimeout(12000);
  http.setReuse(true);
  if (!http.begin(tlsClient(), url)) { noteErr("getUpdates: не удалось начать запрос"); notePollErr(); return; }

  int code = http.GET();
  String resp;
  if (code > 0) resp = http.getString();
  http.end();

  if (code != 200) {
    String d = apiDesc(resp);
    // 409 - у бота стоит вебхук, getUpdates тогда запрещён: сообщаем один раз и надолго
    if (d.indexOf("webhook") >= 0) noteErr("у бота включён вебхук — команды принимать нельзя, отключите его в @BotFather");
    else noteErr("getUpdates: " + d);
    notePollErr();
    return;
  }

  JsonDocument doc;
  if (deserializeJson(doc, resp)) { noteErr("ответ getUpdates не разобран"); return; }
  if (!(doc["ok"] | false)) { noteErr("Telegram: " + apiDesc(resp)); notePollErr(); return; }
  { Lock l(g_mux); g_pollOk++; }

  for (JsonObject m : doc["result"].as<JsonArray>()) {
    int64_t up = m["update_id"].as<int64_t>();
    if (up > g_lastUpdate) g_lastUpdate = up;

    JsonObject cb = m["callback_query"].as<JsonObject>();
    if (!cb.isNull()) {                         // нажатие кнопки в меню
      int64_t cid = cb["message"]["chat"]["id"].as<int64_t>();
      String  from = cb["message"]["chat"]["username"] | "";
      if (!chatMatches(chat, cid, from)) continue;
      { Lock l(g_mux); g_recvCb++; }
      handleCallback(token, chat, cb["id"] | "", cb["data"] | "");
      continue;
    }

    JsonObject msg = m["message"].as<JsonObject>();
    if (msg.isNull()) continue;                 // правки сообщений и прочие типы пропускаем

    int64_t cid = msg["chat"]["id"].as<int64_t>();
    String  from = msg["chat"]["username"] | "";

    if (!chatMatches(chat, cid, from)) {
      // Подсказка при первой настройке: чужому не отвечаем, но id запоминаем для страницы
      Lock l(g_mux);
      g_lastSeenChat = String((long long)cid) + (from.length() ? " @" + from : String(""));
      continue;
    }
    { Lock l(g_mux); g_recvMsg++; }
    if (allowCommands) handleCommand(token, chat, msg["text"] | "");
  }
}

// ------------------------------------------------------------
// Фоновая задача
// ------------------------------------------------------------
static void telegramTask(void*) {
  uint32_t lastPoll = 0, lastStatus = millis(), lastSensors = millis();
  uint32_t pollEvery = TG_POLL_MS;          // растёт при ошибках связи, чтобы не долбить сеть

  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(1000));
    if (WiFi.status() != WL_CONNECTED) continue;

    TelegramConfig c;
    {
      Lock l(g_mux);
      c = g_cfg;
    }
    // чат может быть ещё не задан: тогда опрашиваем команды, чтобы узнать id
    // (подсказка показывается на странице настроек), но ничего не отправляем
    if (!c.enabled || c.token.length() == 0) continue;

    // проверка бота по кнопке (getMe): работает и без чата
    if (g_checkBot) {
      g_checkBot = false;
      checkBotNow(c.token);
      continue;
    }

    // 1) одно сообщение из очереди (по одному за проход, чтобы не держать задачу подолгу)
    String msg;
    bool have = false, withMenu = false;
    {
      Lock l(g_mux);
      if (g_qCount > 0 && c.chat.length() > 0) {
        msg = g_q[g_qHead];
        withMenu = g_qMenu[g_qHead];
        g_qHead = (g_qHead + 1) % TG_QUEUE;
        g_qCount--;
        have = true;
      }
    }
    if (have) {
      sendNow(c.token, c.chat, msg, withMenu ? menuMarkup() : String());
      continue;
    }

    uint32_t now = millis();

    // 1.5) один раз регистрируем список команд в Telegram
    if (g_needCommands) {
      g_needCommands = false;
      setCommandsNow(c.token);
      continue;
    }

    // 2) опрос Telegram: кнопки (callback_query) работают всегда, текстовые команды - по галочке
    if ((c.chat.length() > 0 || c.commands) && (uint32_t)(now - lastPoll) >= pollEvery) {
      lastPoll = now;
      uint32_t failsBefore;
      { Lock l(g_mux); failsBefore = g_fail; }
      pollUpdates(c.token, c.chat, c.commands);
      uint32_t failsAfter;
      { Lock l(g_mux); failsAfter = g_fail; }
      if (failsAfter > failsBefore) {                       // связи нет - ждём дольше
        pollEvery = pollEvery < 20000UL ? pollEvery * 2 : 20000UL;
        if (pollEvery > 20000UL) pollEvery = 20000UL;
      } else {
        pollEvery = TG_POLL_MS;
      }
      continue;
    }

    // 3) периодическая сводка
    if (c.statusMin > 0 && (uint32_t)(now - lastStatus) >= (uint32_t)c.statusMin * 60000UL) {
      lastStatus = now;
      telegramSend(statusText());
    }

    // 4) показания внешних датчиков
    if (c.sendSensors && c.sensorMin > 0 &&
        (uint32_t)(now - lastSensors) >= (uint32_t)c.sensorMin * 60000UL) {
      lastSensors = now;
      String rep = externalSensorsText();
      if (rep.length()) telegramSend(rep);
    }
  }
}

void telegramStart() {
  ensureMux();
  BaseType_t ok = xTaskCreatePinnedToCore(telegramTask, "telegram", TG_TASK_STACK, nullptr, 1, nullptr, 0);
  {
    Lock l(g_mux);
    g_taskOk = (ok == pdPASS);
  }
  Serial.printf("Задача Telegram: %s\n", ok == pdPASS ? "запущена" : "НЕ СОЗДАНА (мало памяти)");
}

// ------------------------------------------------------------
// Уведомления
// ------------------------------------------------------------
void telegramNotifyBoot(const char* ip, int rssi) {
  ensureMux();
  {
    Lock l(g_mux);
    if (!g_cfg.enabled || !g_cfg.onBoot) return;
  }
  char b[220];
  snprintf(b, sizeof(b), "Станция включена.\nIP: %s\nWi-Fi: %d дБм\nВремя: %s",
           ip ? ip : "—", rssi, fmtNow().c_str());
  telegramSend(b);
}

void telegramNotifyAlarm(int hour, int minute) {
  ensureMux();
  {
    Lock l(g_mux);
    if (!g_cfg.enabled || !g_cfg.onAlarm) return;
  }
  char b[120];
  snprintf(b, sizeof(b), "Будильник %02d:%02d — сработал.", hour, minute);
  telegramSend(b);
}

String telegramStatus() {
  ensureMux();
  Lock l(g_mux);
  String s;
  s += g_taskOk ? "Задача работает." : "Задача НЕ запущена (не хватило памяти при старте).";
  s += " Отправлено: " + String(g_sent) + ", ошибок: " + String(g_fail);
  s += " Опрос команд: удачно " + String(g_pollOk) + ", ошибок " + String(g_pollErr) + ".";
  s += " Принято: команд " + String(g_recvMsg) + ", нажатий кнопок " + String(g_recvCb) + ".";
  if (g_qCount > 0) s += ", в очереди: " + String(g_qCount);
  if (g_botName.length()) s += ". Бот: " + g_botName;
  if (g_lastCode) s += ". Последний ответ: HTTP " + String(g_lastCode);
  if (g_lastResp.length()) s += " " + g_lastResp;
  if (g_lastErr.length()) s += ". Ошибка: " + g_lastErr;
  if (g_lastSeenChat.length()) s += ". Писал боту чат: " + g_lastSeenChat;
  return s;
}

// ------------------------------------------------------------
// Веб-интерфейс
// ------------------------------------------------------------
static String esc(const String& s) {
  String o;
  o.reserve(s.length() + 8);
  for (unsigned i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '&') o += "&amp;";
    else if (c == '<') o += "&lt;";
    else if (c == '>') o += "&gt;";
    else if (c == '"') o += "&quot;";
    else o += c;
  }
  return o;
}

static String chkBox(const char* id, const char* name, bool on, const char* label) {
  return String("<div class=\"cb\"><input type=\"checkbox\" id=\"") + id + "\" name=\"" + name + "\"" +
         (on ? " checked" : "") + "><label for=\"" + id + "\" style=\"margin:0\">" + label + "</label></div>";
}

static String buildPage(const String& notice, bool isErr) {
  TelegramConfig c = telegramGetConfig();
  String st = telegramStatus();

  String h;
  h.reserve(8000);
  h += "<!DOCTYPE html><html lang=\"ru\"><head><meta charset=\"utf-8\"><link rel=\"icon\" type=\"image/png\" href=\"/logo.png\">"
       "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
       "<title>Telegram</title><style>"
       "body{font-family:sans-serif;background:#111;color:#eee;margin:0;padding:12px}.wrap{max-width:600px;margin:0 auto}h1{font-size:20px}"
       "a{color:#5ac8fa}.card{border:1px solid #333;border-radius:10px;margin:14px 0;padding:10px 14px;background:#1a1a1a}"
       ".t{color:#fcd05a;font-weight:bold;margin:6px 0}label{display:block;margin:8px 0 3px}"
       "input[type=text],input[type=password],input[type=number]{width:100%;background:#222;color:#eee;border:1px solid #444;"
       "border-radius:6px;padding:8px;font-size:15px;box-sizing:border-box}"
       ".cb{display:flex;align-items:center;gap:10px;margin:8px 0}input[type=checkbox]{width:22px;height:22px}"
       "button{width:100%;padding:14px;border:0;border-radius:8px;background:#0ac;color:#fff;font-size:17px;margin:10px 0}"
       "button.alt{background:#333}"
       ".hint{color:#999;font-size:13px}.ok{background:#1d4d2b;border-radius:8px;padding:10px;margin:10px 0}"
       ".err{background:#5a1d1d;border-radius:8px;padding:10px;margin:10px 0}"
       "code{background:#222;padding:2px 5px;border-radius:4px}</style></head><body><div class=\"wrap\">"
       "<p><a href=\"/\">Главная</a> &nbsp; <a href=\"/settings/integrations\">Интеграции</a> &nbsp; "
       "<a href=\"/settings/sensors\">Датчики</a></p><h1><img src='/logo.png' alt='' style='height:34px;vertical-align:middle;margin-right:10px;border-radius:6px'>Telegram</h1>";
  if (notice.length()) h += String("<div class=\"") + (isErr ? "err" : "ok") + "\">" + notice + "</div>";

  h += "<form method=\"POST\" action=\"/settings/telegram\">";
  h += "<div class=\"card\"><div class=\"t\">Бот</div>";
  h += chkBox("enabled", "enabled", c.enabled, "Включить Telegram");
  h += "<label>Токен бота (от @BotFather)</label><input type=\"text\" name=\"token\" maxlength=\"119\" value=\"" +
       esc(c.token) + "\" placeholder=\"123456789:AAH...\" autocomplete=\"off\">";
  h += "<label>ID чата</label><input type=\"text\" name=\"chat\" maxlength=\"39\" value=\"" + esc(c.chat) +
       "\" placeholder=\"123456789 или -1001234567890\">";
  h += "<p class=\"hint\">Токен: @BotFather → /newbot. ID чата: напишите @userinfobot или своему боту — "
       "он покажет id. Группам id начинается с минуса.</p></div>";

  h += "<div class=\"card\"><div class=\"t\">Что присылать</div>";
  h += chkBox("boot", "boot", c.onBoot, "Сообщение при запуске станции (IP-адрес)");
  h += chkBox("alarm", "alarm", c.onAlarm, "Сработал будильник");
  h += chkBox("cmd", "cmd", c.commands, "Отвечать на команды (/status, /weather, /temp, /ip)");
  h += "<label>Периодическая сводка, минут (0 — не присылать)</label>"
       "<input type=\"number\" name=\"stmin\" min=\"0\" max=\"1440\" value=\"" + String(c.statusMin) + "\">";
  h += "</div>";

  // --- внешние датчики (по Wi-Fi) ---
  h += "<div class=\"card\"><div class=\"t\">Внешние датчики (по Wi-Fi)</div>";
  h += chkBox("sen", "sen", c.sendSensors, "Присылать показания датчиков");
  h += "<label>Как часто, минут (0 — выключено)</label>"
       "<input type=\"number\" name=\"senmin\" min=\"0\" max=\"1440\" value=\"" + String(c.sensorMin) + "\">";
  {
    int wsTotal = wsensorsCount();
    if (wsTotal == 0) {
      h += "<p class=\"hint\">Датчиков пока нет — они появятся здесь после первого приёма "
           "на странице «Датчики по Wi-Fi».</p>";
    } else {
      h += "<p class=\"hint\">С каких датчиков присылать. Если не отмечен ни один — присылаются все.</p>";
      for (int i = 0; i < wsTotal; i++) {
        String key, label;
        if (!wsensorsInfo(i, key, label)) continue;
        float t = NAN, hh = NAN, p = NAN;
        uint32_t age = 0;
        wsensorsValues(i, t, hh, p, age);

        String val = "нет данных";
        if (age > 0) {
          char b[96];
          String v;
          if (!isnan(t))  { snprintf(b, sizeof(b), "%+.1f°", (double)t); v += b; }
          if (!isnan(hh)) { snprintf(b, sizeof(b), " %d%%", (int)lroundf(hh)); v += b; }
          if (!isnan(p))  { snprintf(b, sizeof(b), " %d мм", (int)lroundf(p * 0.750062f)); v += b; }
          if (age < 90000UL) v += " • только что";
          else v += " • " + String(age / 60000UL) + " мин назад";
          val = v;
        }
        String num = String(i);
        String title = label.length() ? label : key;
        h += "<input type=\"hidden\" name=\"key" + num + "\" value=\"" + esc(key) + "\">";
        h += "<div class=\"cb\"><input type=\"checkbox\" id=\"sg" + num + "\" name=\"sg" + num + "\"" +
             (keySelected(c.sensorKeys, key) ? " checked" : "") +
             "><label for=\"sg" + num + "\" style=\"margin:0\">" + esc(title) +
             " <span class=\"hint\">" + esc(val) + "</span></label></div>";
      }
    }
  }
  h += "</div>";

  h += "<button type=\"submit\" name=\"action\" value=\"save\">Сохранить</button>";
  h += "<button type=\"submit\" name=\"action\" value=\"test\" class=\"alt\">Сохранить и прислать проверочное сообщение</button>";
  h += "<button type=\"submit\" name=\"action\" value=\"stest\" class=\"alt\">Прислать показания датчиков сейчас</button>";
  h += "<button type=\"submit\" name=\"action\" value=\"menu\" class=\"alt\">Прислать меню с кнопками</button>";
  h += "<button type=\"submit\" name=\"action\" value=\"cmds\" class=\"alt\">Обновить список команд в боте</button>";
  h += "<button type=\"submit\" name=\"action\" value=\"check\" class=\"alt\">Проверить бота (токен и связь)</button>";
  h += "</form>";

  h += "<div class=\"card\"><div class=\"t\">Состояние</div><p class=\"hint\">" + esc(st) + "</p>";
  if (!c.enabled) h += "<p class=\"hint\">Выключено.</p>";
  else if (c.token.length() == 0 || c.chat.length() == 0)
    h += "<p class=\"hint\">Заполните токен и ID чата — без них отправка не работает.</p>";
  else h += "<p class=\"hint\">Станция опрашивает команды раз в 5 секунд.</p>";
  h += "<p class=\"hint\">Проверочное сообщение уходит в очередь: если ошибка — она появится здесь "
       "через несколько секунд после обновления страницы.</p></div>";
  h += "</div><script>(function(){var cs=document.querySelectorAll('.card,fieldset');for(var i=0;i<cs.length;i++){var c=cs[i];if(!c.querySelector('input,select,textarea'))continue;var b=document.createElement('button');b.type='submit';b.textContent='Сохранить';b.style.cssText='width:auto;margin-top:10px;padding:5px 12px;font-size:12px;border-radius:6px;border:0;background:#2b6cb0;color:#fff;cursor:pointer';b.onclick=function(ev){var fr=this.closest('form');if(fr&&!fr.id){ev.preventDefault();fr.submit();}};c.appendChild(b);}})();</script></body></html>";
  return h;
}

void telegramRegisterWeb(AsyncWebServer& srv) {
  srv.on("/settings/telegram", HTTP_GET, [](AsyncWebServerRequest* r) {
    if (!integrationsAuth(r)) return;
    bool err = r->hasParam("err") || r->hasParam("notask");
    String notice;
    if (r->hasParam("notask"))
      notice = "Задача Telegram не запущена: станция не подключилась к Wi-Fi и работает как точка доступа. "
               "Сначала настройте сеть, после перезапуска Telegram заработает.";
    else if (r->hasParam("nodata"))
      notice = "Свежих показаний нет: либо не отмечен ни один датчик и датчиков вообще нет, "
               "либо они молчат дольше времени «убирать значение» на странице «Датчики по Wi-Fi».";
    else if (err) notice = "Не отправлено: включите Telegram и заполните токен с ID чата.";
    else if (r->hasParam("check")) notice = "Проверяю бота — обновите страницу через пару секунд, результат будет в «Состоянии».";
    else if (r->hasParam("stest")) notice = "Показания датчиков отправлены в очередь — проверьте чат.";
    else if (r->hasParam("test")) notice = "Проверочное сообщение в очереди — проверьте чат.";
    else if (r->hasParam("ok")) notice = "Сохранено";
    r->send(200, "text/html; charset=utf-8", buildPage(notice, err));
  });

  srv.on("/settings/telegram", HTTP_POST, [](AsyncWebServerRequest* r) {
    if (!integrationsAuth(r)) return;
    auto P = [&](const char* n) -> String {
      return r->hasParam(n, true) ? r->getParam(n, true)->value() : String();
    };

    TelegramConfig c = telegramGetConfig();
    c.enabled   = r->hasParam("enabled", true);
    c.onBoot    = r->hasParam("boot", true);
    c.onAlarm   = r->hasParam("alarm", true);
    c.commands  = r->hasParam("cmd", true);
    c.statusMin = P("stmin").toInt();
    c.sendSensors = r->hasParam("sen", true);
    c.sensorMin = P("senmin").toInt();
    {
      // список выбранных датчиков собираем заново из формы (ключи приходят скрытыми полями)
      String keys;
      for (int i = 0; i < 32; i++) {
        String kn = String("key") + i;
        if (!r->hasParam(kn.c_str(), true)) continue;
        String sn = String("sg") + i;
        if (!r->hasParam(sn.c_str(), true)) continue;
        String k = r->getParam(kn.c_str(), true)->value();
        k.trim();
        if (!k.length()) continue;
        if (keys.length()) keys += "\n";
        keys += k;
      }
      c.sensorKeys = keys;
    }
    c.token     = P("token");
    c.chat      = P("chat");
    telegramSetConfig(c);

    String action = P("action");

    bool running;
    {
      Lock l(g_mux);
      running = g_taskOk;
    }
    if (!running && (action == "test" || action == "check")) {
      // Станция не дошла до запуска задач: обычно это режим настройки Wi-Fi (нет сети)
      r->redirect("/settings/telegram?notask=1");
      return;
    }

    if (action == "check") {
      g_checkBot = true;                       // задачу разбудит ближайший тик
      r->redirect("/settings/telegram?check=1");
      return;
    }
    bool can = c.enabled && c.token.length() > 0 && c.chat.length() > 0;

    if (action == "cmds") {
    g_needCommands = true;
    r->redirect("/settings/telegram?ok=1");
    return;
  }
  if (action == "menu") {
    if (can) telegramSend("Меню управления станцией:", true);
    r->redirect(can ? "/settings/telegram?ok=1&menu=1" : "/settings/telegram?err=1");
    return;
  }
  if (action == "test") {
      if (can) telegramSend("Проверка связи: станция SKYCAST на связи.\nКнопки ниже - управление станцией.", true);
      r->redirect(can ? "/settings/telegram?ok=1&test=1" : "/settings/telegram?err=1");
      return;
    }
    if (action == "stest") {
      String rep = can ? externalSensorsText() : String();
      if (rep.length()) {
        telegramSend(rep);
        r->redirect("/settings/telegram?ok=1&stest=1");
      } else {
        r->redirect("/settings/telegram?nodata=1");
      }
      return;
    }
    r->redirect("/settings/telegram?ok=1");
  });
}
