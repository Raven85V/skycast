// Метеостанция на CrowPanel 5.0" (ESP32-S3, 800x480, GT911)
// Погода: OpenWeatherMap (текущая + прогноз на 5 дней), время: NTP.
// Звук: SD + PCM5102 (движок из PixelHub 75). Будильник по дням недели, озвучка времени.
// Экраны: главный (погода) и часы (крупные цифры + бегущая строка).
// Настройки: на экране (шестерёнка в шапке) и в браузере по IP станции.

#include <Arduino.h>
#include <WiFi.h>
#include <math.h>
#include <time.h>
#include "alarm.h"
#include "audio_engine.h"
#include "chime.h"
#include "config.h"
#include "integrations.h"
#include "lgfx_crowpanel.h"
#include "sensors.h"
#include "settings.h"
#include "telegram.h"
#include "ui.h"
#include "ui_boot.h"
#include "ui_clock.h"
#include "ui_info.h"
#include "ui_anim.h"
#include "moon.h"
#include "ui_home.h"
#include "ui_settings.h"
#include "weather.h"
#include "api_state.h"
#include "web_ui.h"
#include "wsensors.h"

LGFX gfx;

enum Mode { MODE_MAIN, MODE_CLOCK, MODE_HOME, MODE_INFO };
static Mode     mode = MODE_MAIN;

static Weather  wx;
static uint32_t nextFetch = 0;
static bool     inSettings = false;
static uint32_t portalStartedAt = 0;

// последнее сообщение подвала - чтобы восстановить после настроек
static char footerMsg[96] = "";
static bool footerErr = false;

// подвал рисуется только на главном экране
static void setFooter(const char* msg, bool err) {
  strlcpy(footerMsg, msg, sizeof(footerMsg));
  footerErr = err;
  if (mode == MODE_MAIN) uiFooter(footerMsg, footerErr);
}

static bool connectWifi(uint32_t timeoutMs) {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.setTxPower(WIFI_POWER_11dBm);   // как WIFI_TX_POWER 10 в MiniWebRadio: меньше просадка питания и мерцание экрана
  WiFi.begin(g_set.wifiSsid, g_set.wifiPass);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < timeoutMs) delay(200);
  return WiFi.status() == WL_CONNECTED;
}

// Температура, влажность и давление главного экрана с внешних датчиков (MQTT или основной датчик по Wi-Fi - что выбрано
// в настройках для каждой величины). Если свежего значения нет, на экране остаётся значение из погоды.
// redraw - сразу обновить на экране то, что изменилось.
static void syncMainValues(bool redraw) {
  // основной датчик по Wi-Fi читаем один раз и только если он кому-то нужен
  float wt = NAN, wh = NAN, wp = NAN;
  bool wOk = false, wRead = false;
  auto wifi = [&]() {
    if (!wRead) { wRead = true; wOk = wsensorsMain(wt, wh, wp); }
    return wOk;
  };

  float t = 0, h = 0, p = 0;
  bool hasT = false, hasH = false, hasP = false;

  if (integrationsMainSource(MAIN_VAL_TEMP) == MAIN_SRC_WIFI) {
    if (wifi() && isfinite(wt)) { t = wt; hasT = true; }   // у датчика может не быть температуры
  } else {
    hasT = integrationsMainMqtt(MAIN_VAL_TEMP, t);
  }

  if (integrationsMainSource(MAIN_VAL_HUM) == MAIN_SRC_WIFI) {
    if (wifi() && isfinite(wh)) { h = wh; hasH = true; }
  } else {
    hasH = integrationsMainMqtt(MAIN_VAL_HUM, h);
  }

  if (integrationsMainSource(MAIN_VAL_PRES) == MAIN_SRC_WIFI) {
    if (wifi() && isfinite(wp)) { p = wp * 0.750062f; hasP = true; }   // датчик Wi-Fi присылает гПа, на экране мм рт. ст.
  } else {
    hasP = integrationsMainMqtt(MAIN_VAL_PRES, p);                      // уже в мм рт. ст.
  }

  uiSetMainTemp(hasT, t, redraw);
  uiSetMainHumPres(hasH, h, hasP, p, redraw);
}

// Перерисовать текущий экран целиком (после настроек, будильника, смены режима)
static void restoreScreen() {
  if (mode == MODE_HOME) {
    uiHomeOpen();
  } else if (mode == MODE_CLOCK) {
    uiClockOpen();
    if (wx.hasCur || wx.dayCount) uiClockSetWeather(wx);
  } else if (mode == MODE_INFO) {
    uiInfoSetWeather(wx);
    uiInfoOpen();
  } else {
    syncMainValues(false);   // значения с датчиков обновить до отрисовки каркаса
    uiShowMain();
    if (wx.hasCur || wx.dayCount) uiDrawWeather(wx);
    if (footerMsg[0]) uiFooter(footerMsg, footerErr);
  }
}

static void setMode(Mode m) {
  if (m == mode) return;
  if (mode == MODE_HOME) uiHomeClose();
  mode = m;
  restoreScreen();
}


// ---------------------------------------------------------------------------
// JSON-состояние станции для внешних интеграций (GET /api/state)
// ---------------------------------------------------------------------------
String apiStateJson() {
  JsonDocument d;
  d["station"] = "SKYCAST";
  d["version"] = "1.0.0";
  d["uptime_s"] = (uint32_t)(millis() / 1000UL);

  JsonObject w = d["weather"].to<JsonObject>();
  w["has"] = wx.hasCur;
  if (wx.hasCur) {
    w["temp"] = wx.cur.temp;
    w["feels"] = wx.cur.feels;
    w["humidity"] = wx.cur.humidity;
    w["pressure_mmhg"] = wx.cur.pressureMmHg;
    w["wind_ms"] = wx.cur.wind;
    w["wind_deg"] = wx.cur.windDeg;
    w["clouds"] = wx.cur.clouds;
    w["icon"] = wx.cur.icon;
    w["desc"] = wx.cur.desc;
    w["sunrise"] = (long)wx.cur.sunrise;
    w["sunset"] = (long)wx.cur.sunset;
  }
  w["pop_now"] = wx.popNow;
  w["updated"] = (long)wx.updated;
  JsonArray days = w["days"].to<JsonArray>();
  for (int i = 0; i < wx.dayCount; i++) {
    JsonObject o = days.add<JsonObject>();
    o["ts"] = (long)wx.days[i].ts;
    o["tmin"] = wx.days[i].tmin;
    o["tmax"] = wx.days[i].tmax;
    o["pop"] = wx.days[i].pop;
    o["icon"] = wx.days[i].icon;
  }

  JsonObject s = d["sensors"].to<JsonObject>();
  float t, h, p;
  t = h = p = NAN;
  if (wsensorsMain(t, h, p)) {
    JsonObject m = s["main"].to<JsonObject>();
    if (!isnan(t)) m["temp"] = t;
    if (!isnan(h)) m["humidity"] = h;
    if (!isnan(p)) m["pressure_hpa"] = p;
  }
  t = h = p = NAN;
  if (wsensorsHome(t, h, p)) {
    JsonObject m = s["home"].to<JsonObject>();
    if (!isnan(t)) m["temp"] = t;
    if (!isnan(h)) m["humidity"] = h;
    if (!isnan(p)) m["pressure_hpa"] = p;
  }
  JsonArray list = s["list"].to<JsonArray>();
  for (int i = 0; i < wsensorsCount(); i++) {
    String key, label;
    if (!wsensorsInfo(i, key, label)) continue;
    float st, sh, sp;
    uint32_t age = 0;
    if (!wsensorsValues(i, st, sh, sp, age)) continue;
    JsonObject o = list.add<JsonObject>();
    o["id"] = key;
    o["label"] = label;
    if (!isnan(st)) o["temp"] = st;
    if (!isnan(sh)) o["humidity"] = sh;
    if (!isnan(sp)) o["pressure_hpa"] = sp;
    o["age_s"] = age / 1000UL;
  }

  String out;
  serializeJson(d, out);
  return out;
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.printf("\n=== CrowPanel weather station ===\nPSRAM: %u\n", ESP.getPsramSize());

  settingsLoad();
  wsensorsInit();         // датчики по Wi-Fi (настройки из NVS)
  loadIntegrationsConfig();   // MQTT / Народмон / пароль веб-настроек (NVS, свой раздел)
  telegramInit();             // токен бота и чат (NVS, свой раздел)
  initSensor();
  uiAnimInit();
  moonInit();          // анимированные иконки из LittleFS (если залит набор)           // BME280/BMP280 (если заданы пины в pins.h)
  initAudio();            // SD + PCM5102; без SD будильник молчит, остальное работает

  gfx.init();
  gfx.setRotation(LCD_ROTATION);
  brightnessInit();       // ШИМ подсветки - после инициализации дисплея
  uiBegin();
#if SPLASH_ANIM
  uiBootAnim(SPLASH_TITLE, SPLASH_SUB);
#endif

  uiSplash("Подключение к Wi-Fi", g_set.wifiSsid);
  bool wifiOk = connectWifi(20000);
  Serial.printf("WiFi: %s\n", wifiOk ? WiFi.localIP().toString().c_str() : "нет подключения");

  if (!wifiOk) {
    // Не подключились (первый запуск, сменили роутер, роутер ещё грузится):
    // поднимаем точку доступа с настройкой. Если за 3 минуты ничего не сохранили - перезапуск и новая попытка.
    uiSplash("Нет подключения к Wi-Fi", "Подключитесь к сети CrowPanel-Weather и откройте 192.168.4.1");
    webUiStartPortal();
    portalStartedAt = millis();
    return;
  }

  uiSetIp(WiFi.localIP().toString().c_str());
  uiSplash("Синхронизация времени", WiFi.localIP().toString().c_str());
  char tz[16];
  snprintf(tz, sizeof(tz), "UTC%d", -(int)g_set.tzHours);   // POSIX: знак обратный (UTC-3 = +3 часа)
  configTzTime(tz, NTP_SERVER, "time.google.com");
  tm t;
  getLocalTime(&t, 8000);   // ждём до 8 секунд

  webUiBegin();
  startIntegrationsTask();    // MQTT и Народмон работают в своей задаче и не тормозят экран
  telegramStart();            // уведомления и команды бота - тоже в своей задаче
  telegramNotifyBoot(WiFi.localIP().toString().c_str(), WiFi.RSSI());
  brightnessAuto();
  mode = (g_set.clockAtNight && nightNow()) ? MODE_CLOCK : MODE_MAIN;
  restoreScreen();
}

void loop() {
  uint32_t now = millis();

  // ---------- режим настройки Wi-Fi (точка доступа) ----------
  if (webUiPortalActive()) {
    webUiLoop();
    if (now - portalStartedAt > 180000UL) ESP.restart();
    delay(5);
    return;
  }

  audioLoop();   // КАЖДУЮ итерацию, иначе щелчки и заикания
  sensorLoop(now);

  // набор иконок сменили в веб-интерфейсе или в настройках - перерисовать текущий экран
  if (iconsStyleDirty() && !inSettings) restoreScreen();

  // ---------- экран настроек ----------
  static uint32_t lastTouch = 0;
  if (inSettings) {
    if (!uiSettingsLoop()) {
      inSettings = false;
      settingsSave();
      brightnessAuto();
      restoreScreen();
      lastTouch = millis();          // касание "Назад" не должно сразу вызвать обновление
    }
    delay(2);
    return;
  }

  // ---------- команды из Home Assistant (MQTT) ----------
  {
    int arg = 0;
    const int cmd = integrationsTakeCommand(&arg);
    if (cmd == IG_CMD_BRIGHT) {
      if (arg < 5) arg = 5;
      if (arg > 100) arg = 100;
      g_set.brDay = (uint8_t)arg;
      brightnessSetPercent(arg);
      settingsSave();
      setFooter("Яркость из Home Assistant", false);
    } else if (cmd == IG_CMD_SCREEN) {
      setMode(mode == MODE_INFO ? MODE_MAIN : MODE_INFO);
    } else if (cmd == IG_CMD_HOME) {
      setMode(MODE_HOME);
    } else if (cmd == IG_CMD_CLOCK) {
      setMode(MODE_CLOCK);
    } else if (cmd == IG_CMD_REBOOT) {
      setFooter("Перезагрузка по команде Home Assistant", false);
      delay(700);
      ESP.restart();
    }
  }
  // ---------- баннер от Telegram: нажали кнопку в боте ----------
  static uint32_t bannerUntil = 0;
  {
    char tb[48];
    if (telegramTakeBanner(tb, sizeof(tb))) {
      uiBanner(tb);                    // плашка поверх текущего экрана
      bannerUntil = millis() + 3000;
    } else if (bannerUntil && (int32_t)(millis() - bannerUntil) >= 0) {
      bannerUntil = 0;
      restoreScreen();                 // перерисовать экран, чтобы плашка исчезла
      lastTouch = millis();
    }
  }
  // ---------- раз в секунду: день/ночь, будильник, озвучка ----------
  static uint32_t lastSecond = 0;
  static int lastNight = -1;
  if (now - lastSecond >= 1000) {
    lastSecond = now;
    integrationsUpdateSnapshot();   // свежие показания для MQTT / Народмона
    int n = nightNow() ? 1 : 0;
    if (n != lastNight) {
      lastNight = n;
      brightnessAuto();
      // ночью - экран часов, утром - обратно на главный (будильник не трогаем)
      if (g_set.clockAtNight && !alarmIsActive()) setMode(n ? MODE_CLOCK : MODE_MAIN);
    }

    tm t;
    if (getLocalTime(&t, 0)) {
      alarmTick(t);
      chimeTick(t);
    }
  }

  // ---------- сработавший будильник: показываем экран, касание выключает ----------
  static bool alarmShown = false;
  bool alarmNow = alarmIsActive();
  if (alarmNow != alarmShown) {
    alarmShown = alarmNow;
    if (alarmNow) {
      int h, m;
      alarmInfo(h, m);
      uiAlarmShow(h, m);
      brightnessSetPercent(g_set.brDay);   // разбудить - экран на дневную яркость
      telegramNotifyAlarm(h, m);
    } else {
      brightnessAuto();
      restoreScreen();
      lastTouch = millis();
    }
  }
  if (alarmNow) {
    int32_t ax, ay;
    if (gfx.getTouch(&ax, &ay) && now - lastTouch > 800) {
      lastTouch = now;
      alarmStop();
    }
    delay(2);
    return;
  }

  // диагностика набора иконок (один раз)
  static bool animMsg = false;
  if (!animMsg) {
    animMsg = true;
    Serial.printf("\nИконки: %s\n", uiAnimReady() ? "набор загружен" : "НАБОРА НЕТ - статичные");
    if (!uiAnimReady()) setFooter("Набор иконок не загружен - статика", true);
  }

  // ---------- срок годности погоды ----------
  static uint32_t weatherExpire = 0;
  if (weatherExpire == 0 && wx.updated) weatherExpire = millis();
  if (wx.updated && (uint32_t)(now - weatherExpire) >= 3UL * 3600UL * 1000UL) {
    // данные старше 3 часов - не показываем устаревшие цифры
    if (wx.hasCur) {
      wx.hasCur = false;
      Serial.println("Погода устарела (>3 ч) - показываем без данных");
      restoreScreen();
    }
    weatherExpire = now;
  }
  if (wx.updated == 0) weatherExpire = now;

  // ---------- часы на экране ----------
  if (mode == MODE_HOME) {
    uiHomeTick(nullptr);
  } else if (mode == MODE_CLOCK) {
    tm t;
    uiClockTick(getLocalTime(&t, 0) ? &t : nullptr);   // сам ограничивает частоту
  } else if (mode == MODE_INFO) {
    tm t;
    uiInfoTick(getLocalTime(&t, 0) ? &t : nullptr);
    uiInfoAnimTick();                      // анимация иконки прогноза
  } else {
    static uint32_t lastClock = 0;
    if (mode == MODE_MAIN) { uiTickIconAnim(); uiTickForecastAnim(); }
    if (now - lastClock >= 200) {
      lastClock = now;
      tm t;
      uiTickClock(getLocalTime(&t, 0) ? &t : nullptr);
    }
    static uint32_t lastTemp = 0;
    if (now - lastTemp >= 1000) {   // температура, влажность, давление с датчиков (на экране меняется только то, что изменилось)
      lastTemp = now;
      syncMainValues(true);
    }
  }

  // ---------- касание ----------
  int32_t tx, ty;
  if (gfx.getTouch(&tx, &ty) && now - lastTouch > 800u) {
    lastTouch = now;
    if (false) {
    } else if (mode == MODE_HOME) {
      if (uiHomeTouch(tx, ty)) setMode(MODE_MAIN);   // шапка - назад, остальное - показатель/период
    } else if (mode == MODE_CLOCK) {
      setMode(MODE_MAIN);                      // касание в любом месте - обратно на главный экран
    } else if (mode == MODE_INFO) {
      setMode(MODE_MAIN);                      // касание в любом месте - обратно на главный экран
    } else if (ty >= 440) {
      setMode(MODE_HOME);                      // касание подвала - экран "Дома" (датчик и графики)
    } else if (ty < 96 && tx >= 396 && tx < 556) {
    setMode(MODE_HOME);                      // показания «Дома» - экран «Дома»
  } else if (ty < 96 && tx >= 556 && tx < 604) {
      uiSettingsOpen();                        // шестерёнка в шапке слева
      inSettings = true;
      return;
    } else if (ty < 96 && tx >= 604) {
      setMode(MODE_CLOCK);                     // касание часов в шапке справа - экран часов
    } else if (tx < 250 && ty >= 90 && ty < 250) {
      setMode(MODE_INFO);                      // касание иконки погоды - второй экран
    } else {
      nextFetch = now;                         // остальное - обновить погоду сейчас
    }
  }

  // ---------- погода ----------
  // Запрос блокирует loop на несколько секунд - пока что-то играет, откладываем,
  // иначе звук будет заикаться.
  // при радио погоду всё равно обновляем (иначе она не обновлялась бы никогда); буфер аудио переживает паузу
  if ((int32_t)(now - nextFetch) >= 0 && !isAudioPlaying()) {
    if (WiFi.status() != WL_CONNECTED) {
      setFooter("Нет подключения к Wi-Fi", true);
      nextFetch = now + 5000;
    } else {
      if (mode == MODE_MAIN) uiFooter("Обновление...", false);
      String err;
      bool ok = weatherUpdate(wx, err);
      Serial.printf("Погода: %s%s%s, дней %d, есть текущая %d\n",
                    ok ? "ok" : "ошибка",
                    err.length() ? " - " : "",
                    err.length() ? err.c_str() : "",
                    wx.dayCount, (int)wx.hasCur);
      if (ok) {
        if (wx.hasCur) {
          integrationsSetWeather(wx.cur.temp, (float)wx.cur.humidity, wx.cur.pressureMmHg * 1.33322f, wx.cur.wind);
          // подробные сущности для Home Assistant
          integrationsSetWeatherEx(wx.cur.feels, wx.cur.clouds, wx.cur.windDeg,
                                   wx.dayCount ? wx.days[0].tmin : 0.0f,
                                   wx.dayCount ? wx.days[0].tmax : 0.0f,
                                   (long)wx.cur.sunrise, (long)wx.cur.sunset,
                                   wx.cur.desc, wx.cur.icon);
        }
        telegramSetWeather(wx);
        if (mode == MODE_MAIN) uiDrawWeather(wx);
        else if (mode == MODE_CLOCK) uiClockSetWeather(wx);
        else if (mode == MODE_INFO) { uiInfoSetWeather(wx); uiInfoOpen(); }
      }

      char msg[96];
      if (err.length()) {
        snprintf(msg, sizeof(msg), "Ошибка: %s", err.c_str());
        setFooter(msg, true);
        nextFetch = millis() + RETRY_MS;
      } else {
        tm t;
        time_t u = wx.updated;
        localtime_r(&u, &t);
        snprintf(msg, sizeof(msg), "Обновлено %02d:%02d", t.tm_hour, t.tm_min);
        setFooter(msg, false);
        nextFetch = millis() + WEATHER_REFRESH_MS;
      }
    }
  }

  delay(2);
}
