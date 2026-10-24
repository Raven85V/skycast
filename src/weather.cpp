#include "weather.h"
#include <time.h>
#include "config.h"
#include "settings.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <math.h>

int dayKey(time_t t) {
  tm lt;
  localtime_r(&t, &lt);
  return lt.tm_year * 400 + lt.tm_yday;
}

// GET + разбор JSON с фильтром (экономит память). Сертификат сервера не проверяется.
// err - короткий текст для подвала экрана (место там узкое, ~25 символов), подробности идут в Serial.
// fatal = true: повторять запрос бессмысленно (нет связи, ключ не принят, лимит) - второй запрос не делаем.
static bool getJson(const char* path, JsonDocument& doc, JsonDocument& filter, String& err, bool& fatal) {
  fatal = false;
  String url = String("https://api.openweathermap.org/data/2.5/") + path +
               "?lat=" + String(g_set.lat, 6) + "&lon=" + String(g_set.lon, 6) +
               "&units=metric&lang=ru&appid=" + g_set.owmKey;

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(15);

  HTTPClient http;
  http.setConnectTimeout(10000);
  http.setTimeout(10000);
  http.useHTTP10(true);   // без chunked - поток можно парсить напрямую
  int code = -1;
  for (int attempt = 0; attempt < 2; attempt++) {   // повтор при обрыве связи
    if (!http.begin(client, url)) { err = "http.begin"; return false; }
    code = http.GET();
    if (code == HTTP_CODE_OK) break;
    http.end();
    if (code >= 0) break;                            // HTTP-ошибку не повторяем
    delay(400);
  }
  if (code != HTTP_CODE_OK) {
    Serial.printf("OWM %s: HTTP %d (%s)\n", path, code, HTTPClient::errorToString(code).c_str());
    if (code == 401) {
      err = "ключ не принят";
      fatal = true;
      Serial.println("OWM: 401 - ключ неверный, либо ещё не активирован (новый ключ работает через 10 минут - 2 часа после регистрации)");
    } else if (code == 429) {
      err = "лимит запросов";
      fatal = true;
    } else if (code < 0) {
      err = "нет связи с OWM";
    } else {
      err = "HTTP " + String(code);
    }
    http.end();
    return false;
  }
  DeserializationError je =
      deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
  http.end();
  if (je) { err = String("JSON: ") + je.c_str(); return false; }
  return true;
}

static bool fetchCurrent(CurrentWx& c, String& err, bool& fatal) {
  JsonDocument filter;
  filter["main"]["temp"] = true;
  filter["main"]["feels_like"] = true;
  filter["main"]["humidity"] = true;
  filter["main"]["pressure"] = true;
  filter["wind"]["speed"] = true;
  filter["wind"]["deg"] = true;
  filter["clouds"]["all"] = true;
  filter["sys"]["sunrise"] = true;
  filter["sys"]["sunset"] = true;
  filter["weather"][0]["icon"] = true;
  filter["weather"][0]["description"] = true;

  JsonDocument doc;
  if (!getJson("weather", doc, filter, err, fatal)) return false;
  if (doc["main"]["temp"].isNull()) { err = "пустой ответ"; return false; }   // иначе на экране тихо появилось бы 0 градусов

  c.temp         = doc["main"]["temp"] | 0.0f;
  c.feels        = doc["main"]["feels_like"] | c.temp;
  c.humidity     = doc["main"]["humidity"] | 0;
  c.pressureMmHg = (int)lroundf((doc["main"]["pressure"] | 0.0f) * 0.750062f);
  c.wind         = doc["wind"]["speed"] | 0.0f;
  c.windDeg      = doc["wind"]["deg"] | 0;
  c.clouds       = doc["clouds"]["all"] | 0;
  c.sunrise      = (time_t)(doc["sys"]["sunrise"] | 0L);
  c.sunset       = (time_t)(doc["sys"]["sunset"] | 0L);
  strlcpy(c.icon, doc["weather"][0]["icon"] | "01d", sizeof(c.icon));
  strlcpy(c.desc, doc["weather"][0]["description"] | "", sizeof(c.desc));
  return true;
}

// Прогноз на 5 суток с шагом 3 ч -> группируем по локальным датам
static bool fetchForecast(Weather& w, String& err, bool& fatal) {
  JsonDocument filter;
  filter["list"][0]["dt"] = true;
  filter["list"][0]["main"]["temp"] = true;
  filter["list"][0]["weather"][0]["icon"] = true;
  filter["list"][0]["pop"] = true;            // вероятность осадков (0..1)

  JsonDocument doc;
  if (!getJson("forecast", doc, filter, err, fatal)) return false;

  const time_t nowT = time(nullptr);
  struct tm nowTm; localtime_r(&nowT, &nowTm);
  const int todayYday = nowTm.tm_yday;
  int popRest = 0;                      // максимум по оставшимся часам сегодня
  struct Acc { int key; int bestDiff; DayWx d; } acc[5];
  int n = 0;

  for (JsonObject e : doc["list"].as<JsonArray>()) {
    time_t dt = (time_t)(e["dt"] | 0L);
    if (!dt) continue;
    float t = e["main"]["temp"] | 0.0f;
    const char* ic = e["weather"][0]["icon"] | "01d";
  const int popPct = (int)lroundf((e["pop"] | 0.0f) * 100.0f);

    tm lt;
    localtime_r(&dt, &lt);
    int key = lt.tm_year * 400 + lt.tm_yday;

    int idx = -1;
    for (int i = 0; i < n; i++) if (acc[i].key == key) { idx = i; break; }
    if (idx < 0) {
      if (n >= 5) break;                 // записи идут по времени - дальше новые дни
      idx = n++;
      acc[idx].key = key;
      acc[idx].bestDiff = 99;
      acc[idx].d.ts = dt;
      acc[idx].d.tmin = acc[idx].d.tmax = t;
    acc[idx].d.pop = 0;
      strlcpy(acc[idx].d.icon, "01d", sizeof(acc[idx].d.icon));
    }
    DayWx& d = acc[idx].d;
    if (t < d.tmin) d.tmin = t;
    if (t > d.tmax) d.tmax = t;
    if (popPct > d.pop) d.pop = popPct;   // за день - максимум из 3-часовых значений
    if (lt.tm_yday == todayYday && dt >= nowT && popPct > popRest) popRest = popPct;

    // иконка дня - ближайшая к 13:00 запись
    int diff = abs(lt.tm_hour - 13);
    if (diff < acc[idx].bestDiff) {
      acc[idx].bestDiff = diff;
      d.ts = dt;
      strlcpy(d.icon, ic, sizeof(d.icon));
      d.icon[2] = 'd';
    }
  }
  if (n == 0) { err = "пустой прогноз"; return false; }

  w.dayCount = n;
  w.popNow = popRest;
  for (int i = 0; i < n; i++) w.days[i] = acc[i].d;
  return true;
}




// ================= Pirate Weather (формат Dark Sky) =================
static int pwToOwm(const char* icon) {
  if (!icon) return 4;
  if (!strcmp(icon, "clear-day")) return 1;
  if (!strcmp(icon, "clear-night")) return 1;
  if (!strcmp(icon, "partly-cloudy-day")) return 3;
  if (!strcmp(icon, "partly-cloudy-night")) return 3;
  if (!strcmp(icon, "cloudy")) return 4;
  if (!strcmp(icon, "fog")) return 50;
  if (!strcmp(icon, "rain")) return 10;
  if (!strcmp(icon, "snow")) return 13;
  if (!strcmp(icon, "sleet")) return 13;
  if (!strcmp(icon, "thunderstorm")) return 11;
  if (!strcmp(icon, "wind")) return 4;
  return 4;
}

static bool fetchPirateWeather(Weather& w, String& err) {
  if (!g_set.pwKey[0]) { err = "нет ключа Pirate Weather"; return false; }
  char url[360];
  snprintf(url, sizeof(url),
           "https://api.pirateweather.net/forecast/%s/%.4f,%.4f?units=si&lang=ru&exclude=minutely,alerts",
           g_set.pwKey, (double)g_set.lat, (double)g_set.lon);

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setConnectTimeout(10000);
  http.setTimeout(15000);
  int code = -1;
  for (int a = 0; a < 2; a++) {
    if (!http.begin(client, url)) { err = "http.begin"; return false; }
    code = http.GET();
    if (code == HTTP_CODE_OK) break;
    http.end();
    if (code > 0) break;
    delay(400);
  }
  if (code != HTTP_CODE_OK) {
    http.end();
    err = (code < 0) ? "нет связи с Pirate Weather" : (String("HTTP ") + code);
    return false;
  }

  DynamicJsonDocument doc(16384);
  DeserializationError je = deserializeJson(doc, http.getStream());
  http.end();
  if (je) { err = String("JSON: ") + je.c_str(); return false; }

  JsonObject c = doc["currently"];
  if (c.isNull()) { err = "нет блока currently"; return false; }

  const char* icon = c["icon"] | "cloudy";
  w.cur.temp     = c["temperature"] | 0.0f;
  w.cur.feels    = c["apparentTemperature"] | w.cur.temp;
  w.cur.humidity = (int)lroundf((c["humidity"] | 0.0f) * 100.0f);
  w.cur.pressureMmHg = (int)lroundf((c["pressure"] | 1013.0f) * 0.750062f);
  w.cur.wind     = c["windSpeed"] | 0.0f;
  w.cur.windDeg  = (int)lroundf(c["windBearing"] | 0.0f);
  w.cur.clouds   = (int)lroundf((c["cloudCover"] | 0.0f) * 100.0f);
  {
    const bool night = strstr(icon, "night") != nullptr;
    snprintf(w.cur.icon, sizeof(w.cur.icon), "%02d%c", pwToOwm(icon), night ? 'n' : 'd');
    strlcpy(w.cur.desc, c["summary"] | "Погода", sizeof(w.cur.desc));
  }

  {
    const time_t nowT = time(nullptr);
    struct tm nowTm; localtime_r(&nowT, &nowTm);
    int popRest = 0;
    for (JsonObject h : doc["hourly"]["data"].as<JsonArray>()) {
      const time_t ht = h["time"] | 0;
      if (ht < nowT) continue;
      struct tm htm; localtime_r(&ht, &htm);
      if (htm.tm_yday != nowTm.tm_yday) continue;
      const int pp = (int)lroundf((h["precipProbability"] | 0.0f) * 100.0f);
      if (pp > popRest) popRest = pp;
    }
    w.popNow = popRest;
  }
  JsonArray days = doc["daily"]["data"];
  w.dayCount = 0;
  for (int i = 0; i < 5 && i < (int)days.size(); i++) {
    JsonObject d = days[i];
    DayWx& out = w.days[w.dayCount];
    out.ts   = d["time"] | (time_t)(w.updated + i * 86400);
    out.tmax = d["temperatureMax"] | 0.0f;
    out.tmin = d["temperatureMin"] | 0.0f;
    out.pop  = (int)lroundf((d["precipProbability"] | 0.0f) * 100.0f);
    snprintf(out.icon, sizeof(out.icon), "%02d" "d", pwToOwm(d["icon"] | "cloudy"));
    if (i == 0) {
      if (d["sunriseTime"].is<time_t>()) w.cur.sunrise = d["sunriseTime"].as<time_t>();
      if (d["sunsetTime"].is<time_t>())  w.cur.sunset  = d["sunsetTime"].as<time_t>();
    }
    w.dayCount++;
  }

  {
    char line[96]; int p = 0;
    p += snprintf(line + p, sizeof(line) - p, "Осадки по дням (Pirate):");
    for (int i = 0; i < w.dayCount && p < (int)sizeof(line) - 8; i++)
      p += snprintf(line + p, sizeof(line) - p, " %d%%", w.days[i].pop);
    Serial.println(line);
  }
  w.hasCur = true;
  w.updated = time(nullptr);
  err = "";
  return true;
}


// История давления из архива Open-Meteo: без ключа, по координатам из настроек.
int weatherFetchPressureHistory(float* out, int maxN) {
  if (!out || maxN <= 0) return 0;

  char path[300];
  snprintf(path, sizeof(path),
           "/v1/forecast?latitude=%.4f&longitude=%.4f"
           "&hourly=pressure_msl&past_days=1&forecast_days=1&timezone=auto",
           (double)g_set.lat, (double)g_set.lon);

  // HTTPS, а при обрыве рукопожатия - обычный HTTP: сервер отдаёт те же данные.
  for (int secure = 1; secure >= 0; secure--) {
    char url[340];
    snprintf(url, sizeof(url), "%s://api.open-meteo.com%s", secure ? "https" : "http", path);

    int code = -1;
    String payload;

    if (secure) {
      WiFiClientSecure client;
      client.setInsecure();
      client.setTimeout(15);
      HTTPClient http;
      http.setConnectTimeout(10000);
      http.setTimeout(15000);
      http.setReuse(false);
      for (int attempt = 0; attempt < 2; attempt++) {
        if (!http.begin(client, url)) break;
        code = http.GET();
        if (code == HTTP_CODE_OK) { payload = http.getString(); http.end(); break; }
        Serial.printf("История: HTTPS попытка %d, код %d (%s)\n", attempt + 1, code, http.errorToString(code).c_str());
        http.end();
        delay(400);
      }
    } else {
      WiFiClient client;
      HTTPClient http;
      http.setConnectTimeout(10000);
      http.setTimeout(15000);
      http.setReuse(false);
      for (int attempt = 0; attempt < 2; attempt++) {
        if (!http.begin(client, url)) break;
        code = http.GET();
        if (code == HTTP_CODE_OK) { payload = http.getString(); http.end(); break; }
        Serial.printf("История: HTTP попытка %d, код %d\n", attempt + 1, code);
        http.end();
        delay(400);
      }
    }

    if (code != HTTP_CODE_OK) continue;
    if (payload.length() < 20) {
      Serial.printf("История: %s, пустое тело (%u байт)\n", secure ? "HTTPS" : "HTTP", (unsigned)payload.length());
      continue;
    }

    DynamicJsonDocument doc(16384);
    DeserializationError je = deserializeJson(doc, payload);
    if (je) {
      Serial.printf("История: JSON ошибка %s (тело %u байт, начало: %.40s)\n",
                    je.c_str(), (unsigned)payload.length(), payload.c_str());
      continue;
    }

    JsonArray arr = doc["hourly"]["pressure_msl"];
    const int n = (int)arr.size();
    int filled = 0;
    for (int i = n - 1; i >= 0 && filled < maxN; i--) {
      if (arr[i].is<float>() || arr[i].is<int>()) {
        out[filled++] = arr[i].as<float>() * 0.750062f;   // гПа -> мм рт. ст.
      }
    }
      return filled;
  }

  Serial.println("История: не удалось получить ни по HTTPS, ни по HTTP");
  return 0;
}

bool weatherUpdate(Weather& w, String& err) {
  if (g_set.wxSrc == 1) { String e; bool ok = fetchPirateWeather(w, e); err = e; return ok; }
  String e1, e2;
  bool fatal = false;
  bool okCur = fetchCurrent(w.cur, e1, fatal);
  if (okCur) w.hasCur = true;
  // нет связи / ключ не принят / лимит: второй запрос только зря заблокирует экран ещё на несколько секунд
  bool okFc = fatal ? false : fetchForecast(w, e2, fatal);
  err = e1.length() ? e1 : e2;
  if (okCur || okFc) w.updated = time(nullptr);
  return okCur || okFc;
}
