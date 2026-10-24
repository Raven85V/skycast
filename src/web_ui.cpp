#include "web_ui.h"
#include "api_state.h"
#include <LittleFS.h>
#include "logo_png.h"
#include <math.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include "settings.h"
#include "icons.h"
#include "sensors.h"
#include "integrations.h"
#include "ota.h"
#include "telegram.h"
#include "wsensors.h"

static AsyncWebServer server(80);
static DNSServer dnsServer;
static bool portalActive = false;
static bool serverStarted = false;
static const char* AP_SSID = "CrowPanel-Weather";

// ---------- вспомогательное ----------
static String esc(const String& s) {
  String o;
  o.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '&') o += "&amp;";
    else if (c == '<') o += "&lt;";
    else if (c == '>') o += "&gt;";
    else if (c == '"') o += "&quot;";
    else if (c == '\'') o += "&#39;";
    else o += c;
  }
  return o;
}

static String chk(bool b) { return b ? " checked" : ""; }

static String numField(const char* name, int v, int lo, int hi) {
  return String("<input type='number' name='") + name + "' value='" + v + "' min='" + lo + "' max='" + hi + "'>";
}

static String floatField(const char* name, float v, float lo, float hi) {
  char b[16];
  snprintf(b, sizeof(b), "%.1f", v);
  return String("<input type='number' step='0.1' name='") + name + "' value='" + b + "' min='" + (int)lo + "' max='" + (int)hi + "'>";
}

static String timeField(const char* name, int hour, int minute) {
  char b[8];
  snprintf(b, sizeof(b), "%02d:%02d", hour, minute);
  return String("<input type='time' name='") + name + "' value='" + b + "'>";
}

static String row(const char* label, const String& input) {
  return String("<label><span>") + label + "</span>" + input + "</label>";
}

static bool parseHM(const String& v, int& h, int& m) {
  int c = v.indexOf(':');
  if (c < 1) return false;
  h = v.substring(0, c).toInt();
  m = v.substring(c + 1).toInt();
  return h >= 0 && h <= 23 && m >= 0 && m <= 59;
}

static void restartSoon(uint32_t ms) {
  xTaskCreate([](void* p) {
    vTaskDelay(pdMS_TO_TICKS((uint32_t)(uintptr_t)p));
    ESP.restart();
  }, "restart", 2048, (void*)(uintptr_t)ms, 1, nullptr);
}

// ---------- страница ----------
static String buildPage(const String& notice) {
  const Settings& s = g_set;
  String h;
  h.reserve(10000);
  h += F("<!DOCTYPE html><html lang='ru'><head><meta charset='utf-8'><link rel='icon' type='image/png' href='/logo.png'>"
         "<meta name='viewport' content='width=device-width,initial-scale=1'>"
         "<title>SKYCAST</title><style>"
         "body{font-family:sans-serif;background:#111;color:#eee;margin:0;padding:12px}"
         ".w{max-width:560px;margin:0 auto}h1{font-size:20px}"
         "fieldset{border:1px solid #333;border-radius:10px;margin:14px 0;padding:10px 14px;background:#1a1a1a}"
         "legend{color:#fcd05a;padding:0 6px}"
         "label{display:flex;justify-content:space-between;align-items:center;gap:10px;margin:8px 0}"
         "label span{flex:1}input,select{background:#222;color:#eee;border:1px solid #444;border-radius:6px;padding:8px;font-size:15px;max-width:55%;box-sizing:border-box}"
         "input[type=checkbox]{width:22px;height:22px}input[type=number]{width:90px}"
         "button{width:100%;padding:14px;border:0;border-radius:8px;background:#0ac;color:#fff;font-size:17px;margin:10px 0}"
         ".ok{background:#1d4d2b;border-radius:8px;padding:10px;margin:10px 0}.hint{color:#999;font-size:13px}"
         "table{width:100%;border-collapse:collapse}td{padding:4px 2px}"
         ".tiles{display:grid;grid-template-columns:repeat(auto-fit,minmax(160px,1fr));gap:8px;margin:12px 0}"
         ".tile{display:block;background:#1b2230;border:1px solid #333;border-radius:12px;padding:12px;text-decoration:none;color:#eee}"
         ".tile b{display:block;font-size:15px;color:#fff}.tile span{font-size:12px;color:#9aa7b8}"
         "</style></head><body><div class='w'><h1><img src='/logo.png' alt='' style='height:44px;vertical-align:middle;margin-right:12px;border-radius:8px'>SKYCAST</h1>");
  h += F("<div class='tiles'>"
         "<a class='tile' href='/settings/integrations'><b>Интеграции</b><span>MQTT, Народмон, пароль</span></a>"
         "<a class='tile' href='/settings/receive'><b>Приём данных</b><span>в бегущую строку</span></a>"
         "<a class='tile' href='/settings/sensors'><b>Датчики по Wi-Fi</b><span>названия и поправки</span></a>"
         "<a class='tile' href='/settings/telegram'><b>Telegram</b><span>уведомления и команды</span></a>"
         "<a class='tile' href='/update'><b>Обновление</b><span>прошивка по Wi-Fi</span></a>"
         "</div>");
  if (notice.length()) h += "<div class='ok'>" + notice + "</div>";
  h += F("<form method='POST' action='/save'>");

  // --- экран ---
  h += F("<fieldset><legend>Экран и ночной режим</legend>");
  h += row("Яркость днём, %", numField("brDay", s.brDay, 5, 100));
  h += row("Яркость ночью, %", numField("brNight", s.brNight, 5, 100));
  h += row("Ночь начинается", timeField("nightStart", s.nightStart / 60, s.nightStart % 60));
  h += row("Ночь заканчивается", timeField("nightEnd", s.nightEnd / 60, s.nightEnd % 60));
  h += row("Ночью показывать экран часов", "<input type='checkbox' name='clockNight'" + chk(s.clockAtNight) + ">");
  {
    String fs = "<select name='clkFont'>";
    for (int i = 0; i < CLOCK_FONT_COUNT; i++) fs += String("<option value='") + i + "'" + (g_clockFont == i ? " selected" : "") + ">" + CLOCK_FONT_NAMES[i] + "</option>";
    fs += "</select>";
    h += row("Стиль цифр часов", fs);
    {
      String tf = "<select name='textFont'>";
      for (int i = 0; i < TEXT_FONT_COUNT; i++) tf += String("<option value='") + i + "'" + (g_textFont == i ? " selected" : "") + ">" + TEXT_FONT_NAMES[i] + "</option>";
      tf += "</select>";
      h += row("Шрифт текста (бегущая строка)", tf);
    }
    {
      const char* nm[5] = {"Цвет цифр часов", "Цвет даты", "Цвет дня недели", "Цвет месяца", "Цвет секунд"};
      const char* key[5] = {"clkCol", "dCol", "wdCol", "mCol", "sCol"};
      uint32_t* val[5] = {&g_clockColor, &g_dateColor, &g_weekdayColor, &g_monthColor, &g_secColor};
      for (int i = 0; i < 5; i++) {
        char hex[8];
        snprintf(hex, sizeof(hex), "#%06X", (unsigned)(*val[i] & 0xFFFFFF));
        h += row(nm[i], String("<input type='color' name='") + key[i] + "' value='" + hex + "'>");
      }
    }
    String ls = "<select name='clkLay'>";
    for (int i = 0; i < CLOCK_LAYOUT_COUNT; i++) ls += String("<option value='") + i + "'" + (g_clockLayout == i ? " selected" : "") + ">" + CLOCK_LAYOUT_NAMES[i] + "</option>";
    ls += "</select>";
    h += row("Раскладка экрана часов", ls);
    String is = "<select name='iconStyle'>";
    for (int i = 0; i < ICON_STYLE_COUNT; i++) is += String("<option value='") + i + "'" + (g_iconStyle == i ? " selected" : "") + ">" + ICON_STYLE_NAMES[i] + "</option>";
    is += "</select>";
    h += row("Иконки погоды", is);
  }
  h += F("</fieldset>");

  // --- бегущая строка на экране часов ---
  h += F("<fieldset><legend>Бегущая строка (экран часов)</legend><table>"
         "<tr><td></td><td>вкл</td><td>скорость 1-10</td><td>цвет</td></tr>");
  for (int i = 0; i < CLOCK_LINE_COUNT; i++) {
    char hex[8];
    snprintf(hex, sizeof(hex), "#%06X", (unsigned)(s.line[i].rgb & 0xFFFFFF));
    h += String("<tr><td>") + CLOCK_LINE_NAMES[i] + "</td><td><input type='checkbox' name='lnE" + i + "'" + chk(s.line[i].en) + "></td><td>";
    h += numField((String("lnS") + i).c_str(), s.line[i].speed, 1, 10);
    h += String("</td><td><input type='color' name='lnC") + i + "' value='" + hex + "'></td></tr>";
  }
  h += F("</table></fieldset>");

  // --- комнатный датчик ---
  h += F("<fieldset><legend>Датчик в комнате</legend>");
  {
    const char* st = detectedSensor == SensorType::BME280 ? "BME280 (температура, влажность, давление)"
                   : detectedSensor == SensorType::BMP280 ? "BMP280 (температура, давление, без влажности)"
                   : "не найден (проверьте пины в pins.h и подключение)";
    h += String("<p class='hint'>Датчик: ") + st + "</p>";
    if (sensorFresh()) {
      char b[128];
      snprintf(b, sizeof(b), "<p class='hint'>Сейчас: %.1f &deg;C, %d %%, %d мм рт.ст.</p>", homeTemp,
               (int)lroundf(homeHumidity), (int)lroundf(homePressure * 0.750062f));
      h += b;
    }
  }
  h += row("Поправка температуры, &deg;C", floatField("tOff", s.tempOff, -10, 10));
  h += row("Поправка влажности, %", floatField("hOff", s.humOff, -20, 20));
  h += row("Поправка давления, мм рт.ст.", floatField("pOff", s.presOffMm, -30, 30));
  h += F("</fieldset>");

  // --- звук ---
  h += F("<fieldset><legend>Звук</legend>");
  h += row("Громкость будильника, день (0-21)", numField("volAlarmDay", s.volAlarmDay, 0, 21));
  h += row("Громкость будильника, ночь (0-21)", numField("volAlarmNight", s.volAlarmNight, 0, 21));
  h += row("Автостоп будильника, мин", numField("autoStop", s.alarmAutoStopMin, 1, 60));
  h += F("</fieldset>");

  // --- озвучка времени ---
  h += F("<fieldset><legend>Озвучивание времени</legend>");
  h += row("Озвучивать днём", "<input type='checkbox' name='chimeDay'" + chk(s.chimeDay) + ">");
  h += row("Озвучивать ночью", "<input type='checkbox' name='chimeNight'" + chk(s.chimeNight) + ">");
  h += row("Громкость, день (0-21)", numField("volChimeDay", s.volChimeDay, 0, 21));
  h += row("Громкость, ночь (0-21)", numField("volChimeNight", s.volChimeNight, 0, 21));
  static const char* VOICES[4] = {"Женский", "Мужской", "Куранты", "Каминные часы"};
  String sel = "<select name='voice'>";
  for (int i = 0; i < 4; i++) sel += String("<option value='") + i + "'" + (s.chimeVoice == i ? " selected" : "") + ">" + VOICES[i] + "</option>";
  sel += "</select>";
  h += row("Голос", sel);
  h += F("</fieldset>");

  // --- будильники по дням ---
  h += F("<fieldset><legend>Будильники</legend><table>");
  static const char* DAYS[7] = {"Воскресенье", "Понедельник", "Вторник", "Среда", "Четверг", "Пятница", "Суббота"};
  static const int ORDER[7] = {1, 2, 3, 4, 5, 6, 0};   // Пн..Вс, индекс = tm_wday
  for (int k = 0; k < 7; k++) {
    int d = ORDER[k];
    h += "<tr><td>" + String(DAYS[d]) + "</td><td><input type='checkbox' name='en" + d + "'" + chk(s.alarmEn[d]) + "></td><td>";
    h += timeField((String("tm") + d).c_str(), s.alarmH[d], s.alarmM[d]);
    h += "</td><td>";
    h += numField((String("tr") + d).c_str(), s.alarmTrack[d], 1, 999);
    h += "</td></tr>";
  }
  h += F("</table><p class='hint'>Колонки: день, вкл, время, номер мелодии (/alarms/NNN.mp3 на SD-карте).</p></fieldset>");

  // --- сеть и погода ---
  h += F("<fieldset><legend>Wi-Fi и погода</legend>");
  h += row("Сеть Wi-Fi (SSID)", "<input type='text' name='ssid' value='" + esc(s.wifiSsid) + "'>");
  h += row("Пароль Wi-Fi", "<input type='password' name='pass' placeholder='не менять'>");
  h += row("Ключ OpenWeatherMap", "<input type='password' name='owm' placeholder='не менять'>");
h += row("Источник погоды", "<select name='wxsrc'>" +
    String("<option value='0'") + (s.wxSrc == 0 ? " selected" : "") + ">OpenWeatherMap</option>" +
    String("<option value='1'") + (s.wxSrc == 1 ? " selected" : "") + ">Pirate Weather (ключ)</option>" +
    "</select>");
  h += row("Ключ Pirate Weather", "<input type='password' name='pwkey' placeholder='не менять'>");
h += row("Город (подпись)", "<input type='text' name='city' value='" + esc(s.city) + "'>");
  h += row("Широта", "<input type='text' name='lat' value='" + String(s.lat, 4) + "'>");
  h += row("Долгота", "<input type='text' name='lon' value='" + String(s.lon, 4) + "'>");
  h += row("Часовой пояс (часы от UTC)", numField("tz", s.tzHours, -12, 14));
  h += F("<p class='hint'>Пустые поля пароля и ключа оставляют старые значения. "
         "После смены сети или часового пояса станция перезагрузится.</p></fieldset>");

  h += F("</form>"
         "<form method='POST' action='/reboot' onsubmit=\"return confirm('Перезагрузить станцию?');\">"
         "<button type='submit' style='background:#7a2626'>Перезагрузить станцию</button></form>"

         "<p class='hint'>Пароль на настройки задаётся на странице «Интеграции».</p></div><script>(function(){var cs=document.querySelectorAll('.card,fieldset');for(var i=0;i<cs.length;i++){var c=cs[i];if(!c.querySelector('input,select,textarea'))continue;var b=document.createElement('button');b.type='submit';b.textContent='Сохранить';b.style.cssText='width:auto;margin-top:10px;padding:5px 12px;font-size:12px;border-radius:6px;border:0;background:#2b6cb0;color:#fff;cursor:pointer';b.onclick=function(ev){var fr=this.closest('form');if(fr&&!fr.id){ev.preventDefault();fr.submit();}};c.appendChild(b);}})();</script></body></html>");
  return h;
}

// ---------- обработчики ----------
static void handleRoot(AsyncWebServerRequest* r) {
  if (!portalActive && !integrationsAuth(r)) return;
  String notice = r->hasParam("ok") ? "Сохранено" : "";
  r->send(200, "text/html; charset=utf-8", buildPage(notice));
}

static void handleSave(AsyncWebServerRequest* r) {
  if (!portalActive && !integrationsAuth(r)) return;
  auto has = [&](const char* n) { return r->hasParam(n, true); };
  auto P = [&](const char* n) -> String { return has(n) ? r->getParam(n, true)->value() : String(); };
  auto I = [&](const char* n, int cur, int lo, int hi) -> int {
    if (!has(n) || P(n).length() == 0) return cur;
    int v = P(n).toInt();
    return v < lo ? lo : (v > hi ? hi : v);
  };

  Settings s = g_set;
  bool needRestart = false;

  s.brDay = I("brDay", s.brDay, 5, 100);
  s.brNight = I("brNight", s.brNight, 5, 100);
  s.clockAtNight = has("clockNight");
  g_clockFont = (uint8_t)I("clkFont", g_clockFont, 0, CLOCK_FONT_COUNT - 1);
  g_clockLayout = (uint8_t)I("clkLay", g_clockLayout, 0, CLOCK_LAYOUT_COUNT - 1);
  iconsSetStyle((uint8_t)I("iconStyle", g_iconStyle, 0, ICON_STYLE_COUNT - 1));
  g_textFont = (uint8_t)I("textFont", g_textFont, 0, TEXT_FONT_COUNT - 1);
  {
    const char* key[5] = {"clkCol", "dCol", "wdCol", "mCol", "sCol"};
    uint32_t* val[5] = {&g_clockColor, &g_dateColor, &g_weekdayColor, &g_monthColor, &g_secColor};
    for (int i = 0; i < 5; i++) {
      String cc = P(key[i]);
      if (cc.length() == 7 && cc[0] == '#') *val[i] = (uint32_t)strtoul(cc.c_str() + 1, nullptr, 16) & 0xFFFFFF;
    }
  }
  int hh, mm;
  if (parseHM(P("nightStart"), hh, mm)) s.nightStart = hh * 60 + mm;
  if (parseHM(P("nightEnd"), hh, mm)) s.nightEnd = hh * 60 + mm;

  // число из формы: на русской клавиатуре вводят запятую ("59,437"), а toFloat() читает только до неё
  auto NUM = [&](const char* n) -> float { String v = P(n); v.trim(); v.replace(",", "."); return v.toFloat(); };
  if (P("tOff").length()) s.tempOff = NUM("tOff");
  if (P("hOff").length()) s.humOff = NUM("hOff");
  if (P("pOff").length()) s.presOffMm = NUM("pOff");

  s.volAlarmDay = I("volAlarmDay", s.volAlarmDay, 0, 21);
  s.volAlarmNight = I("volAlarmNight", s.volAlarmNight, 0, 21);
  s.alarmAutoStopMin = I("autoStop", s.alarmAutoStopMin, 1, 60);

  s.chimeDay = has("chimeDay");
  s.chimeNight = has("chimeNight");
  s.volChimeDay = I("volChimeDay", s.volChimeDay, 0, 21);
  s.volChimeNight = I("volChimeNight", s.volChimeNight, 0, 21);
  s.chimeVoice = I("voice", s.chimeVoice, 0, 3);

  for (int d = 0; d < 7; d++) {
    String e = String("en") + d, t = String("tm") + d, k = String("tr") + d;
    s.alarmEn[d] = has(e.c_str());
    if (parseHM(P(t.c_str()), hh, mm)) { s.alarmH[d] = hh; s.alarmM[d] = mm; }
    s.alarmTrack[d] = I(k.c_str(), s.alarmTrack[d], 1, 999);
  }

  for (int i = 0; i < CLOCK_LINE_COUNT; i++) {
    String e = String("lnE") + i, sp = String("lnS") + i, co = String("lnC") + i;
    s.line[i].en = has(e.c_str());
    s.line[i].speed = I(sp.c_str(), s.line[i].speed, 1, 10);
    String c = P(co.c_str());
    if (c.length() == 7 && c[0] == '#') s.line[i].rgb = (uint32_t)strtoul(c.c_str() + 1, nullptr, 16);
  }

  String ssid = P("ssid");
  ssid.trim();
  if (ssid.length() && ssid != s.wifiSsid) { strlcpy(s.wifiSsid, ssid.c_str(), sizeof(s.wifiSsid)); needRestart = true; }
  String pass = P("pass");
  if (pass.length()) { strlcpy(s.wifiPass, pass.c_str(), sizeof(s.wifiPass)); needRestart = true; }
  String owm = P("owm");
  owm.trim();
  if (owm.length()) strlcpy(s.owmKey, owm.c_str(), sizeof(s.owmKey));
  if (P("wxsrc").length()) s.wxSrc = (uint8_t)I("wxsrc", s.wxSrc, 0, 1);
  if (P("pwkey").length()) strlcpy(s.pwKey, P("pwkey").c_str(), sizeof(s.pwKey));
  String city = P("city");
  city.trim();
  if (city.length()) strlcpy(s.city, city.c_str(), sizeof(s.city));
  if (P("lat").length()) s.lat = NUM("lat");
  if (P("lon").length()) s.lon = NUM("lon");
  int tz = I("tz", s.tzHours, -12, 14);
  if (tz != s.tzHours) { s.tzHours = tz; needRestart = true; }

  settingsSanitize(s);
  g_set = s;
  settingsSave();
  brightnessAuto();

  if (needRestart || portalActive) {
    r->send(200, "text/html; charset=utf-8",
            "<!DOCTYPE html><html lang='ru'><head><meta charset='utf-8'>"
            "<meta name='viewport' content='width=device-width,initial-scale=1'></head>"
            "<body style='font-family:sans-serif;background:#111;color:#eee;padding:24px'>"
            "<h2>Сохранено</h2><p>Станция перезагружается и подключается к сети. "
            "Через полминуты откройте её адрес снова.</p><script>(function(){var cs=document.querySelectorAll('.card,fieldset');for(var i=0;i<cs.length;i++){var c=cs[i];if(!c.querySelector('input,select,textarea'))continue;var b=document.createElement('button');b.type='submit';b.textContent='Сохранить';b.style.cssText='width:auto;margin-top:10px;padding:5px 12px;font-size:12px;border-radius:6px;border:0;background:#2b6cb0;color:#fff;cursor:pointer';b.onclick=function(ev){var fr=this.closest('form');if(fr&&!fr.id){ev.preventDefault();fr.submit();}};c.appendChild(b);}})();</script></body></html>");
    restartSoon(1500);
  } else {
    r->redirect("/?ok=1");
  }
}

// Перезагрузка станции по кнопке в веб-интерфейсе
static void handleReboot(AsyncWebServerRequest* r) {
  if (!portalActive && !integrationsAuth(r)) return;
  r->send(200, "text/html; charset=utf-8",
          "<!DOCTYPE html><html lang='ru'><head><meta charset='utf-8'>"
          "<meta name='viewport' content='width=device-width,initial-scale=1'></head>"
          "<body style='font-family:sans-serif;background:#111;color:#eee;padding:24px'>"
          "<h2>Перезагрузка</h2><p>Станция перезагружается. Через полминуты откройте её адрес снова.</p>"
          "<script>(function(){var cs=document.querySelectorAll('.card,fieldset');for(var i=0;i<cs.length;i++){var c=cs[i];if(!c.querySelector('input,select,textarea'))continue;var b=document.createElement('button');b.type='submit';b.textContent='Сохранить';b.style.cssText='width:auto;margin-top:10px;padding:5px 12px;font-size:12px;border-radius:6px;border:0;background:#2b6cb0;color:#fff;cursor:pointer';b.onclick=function(ev){var fr=this.closest('form');if(fr&&!fr.id){ev.preventDefault();fr.submit();}};c.appendChild(b);}})();</script></body></html>");
  restartSoon(1200);
}

// ---------- публичное ----------
void webUiBegin() {
  if (serverStarted) return;
  auto serveLogo = [](AsyncWebServerRequest* r) {
    // логотип отдаём из прошивки: файловая система занята иконками и луной
    AsyncWebServerResponse* res = r->beginResponse_P(200, "image/png", LOGO_PNG, LOGO_PNG_LEN);
    res->addHeader("Cache-Control", "public, max-age=86400");
    r->send(res);
  };
  server.on("/api/state", HTTP_GET, [](AsyncWebServerRequest* r) {
    r->send(200, "application/json", apiStateJson());
  });
  server.on("/logo.png", HTTP_GET, serveLogo);
  server.on("/favicon.ico", HTTP_GET, serveLogo);   // часть браузеров просит его напрямую
  server.on("/favicon.png", HTTP_GET, serveLogo);
  server.on("/", HTTP_GET, handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/reboot", HTTP_POST, handleReboot);
  integrationsRegisterWeb(server);   // /settings/integrations и /settings/receive
  wsensorsRegisterWeb(server);       // /api/sensor и /settings/sensors (датчики по Wi-Fi)
  telegramRegisterWeb(server);       // /settings/telegram (бот: уведомления и команды)
  otaRegisterWeb(server);            // /update (обновление прошивки по Wi-Fi, только с паролем)
  server.begin();
  serverStarted = true;
  Serial.println("Веб-интерфейс запущен");
}

void webUiStartPortal() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID);
  IPAddress ip = WiFi.softAPIP();
  Serial.print("Точка доступа настройки, IP: ");
  Serial.println(ip);
  dnsServer.start(53, "*", ip);          // все адреса ведут сюда - телефон сам предложит "Войти в сеть"
  server.onNotFound(handleRoot);         // проверки captive-портала iOS/Android получают страницу настройки
  webUiBegin();
  portalActive = true;
}

void webUiLoop() {
  if (portalActive) dnsServer.processNextRequest();
}

bool webUiPortalActive() { return portalActive; }
