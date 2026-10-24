#include "wsensors.h"
#include "integrations.h"
#include <Preferences.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <math.h>

#define WS_MAX      12      // датчиков (позиций "id/n")
#define WS_KEYLEN   32
#define WS_LABLEN   64
#define WS_BODY_MAX 2048

struct WsEntry {                 // сохраняется в NVS
  char    key[WS_KEYLEN];        // "id/n"
  char    label[WS_LABLEN];      // название в строке (пусто - показывать ключ)
  uint8_t show;                  // показывать в бегущей строке
  uint8_t main;                  // основной (для экранов)
  float   offT, offH, offP;      // поправки
};

struct WsCfg {
  uint32_t magic;
  uint8_t  lineOn;
  uint8_t  speed;
  uint16_t staleMin;             // 0 - не скрывать
  uint32_t rgb;
  char     secret[24];           // ключ передатчика (пусто - без проверки)
  WsEntry  e[WS_MAX];
};
static const uint32_t WS_MAGIC = 0x57530002;

struct WsLive { float t, h, p; uint32_t ms; int rssi; };   // ms == 0 - данных нет

static WsCfg   g_c;
static uint32_t g_homeMask = 0;
static uint8_t  g_src[3] = {0, 0, 0};   // источники для «Дома»: темп, влажн, давл   // бит i = датчик i комнатный («дома»)
static WsLive  g_l[WS_MAX];
static SemaphoreHandle_t g_mux = nullptr;
static uint32_t g_okCount = 0, g_badCount = 0;
static String   g_lastErr;

struct Lock {
  Lock()  { if (g_mux) xSemaphoreTake(g_mux, portMAX_DELAY); }
  ~Lock() { if (g_mux) xSemaphoreGive(g_mux); }
};

static void saveCfg() {
  Preferences p;
  if (!p.begin("wsens", false)) return;
  p.putBytes("cfg", &g_c, sizeof(g_c));
  p.putUInt("homes", g_homeMask);
  p.putUChar("src0", g_src[0]);
  p.putUChar("src1", g_src[1]);
  p.putUChar("src2", g_src[2]);
  p.end();
}

static void defaults() {
  memset(&g_c, 0, sizeof(g_c));
  g_c.magic = WS_MAGIC;
  g_c.lineOn = 1;
  g_c.speed = 5;
  g_c.staleMin = 35;
  g_c.rgb = 0xFFB040;
}

void wsensorsInit() {
  if (!g_mux) g_mux = xSemaphoreCreateMutex();
  memset(g_l, 0, sizeof(g_l));
  defaults();
  {
    Preferences ph;
    if (ph.begin("wsens", true)) {
      g_homeMask = ph.getUInt("homes", 0);
      g_src[0] = ph.getUChar("src0", 0);
      g_src[1] = ph.getUChar("src1", 0);
      g_src[2] = ph.getUChar("src2", 0);
      ph.end();
    }
  }
  Preferences p;
  if (p.begin("wsens", true)) {
    WsCfg tmp;
    if (p.getBytesLength("cfg") == sizeof(tmp) && p.getBytes("cfg", &tmp, sizeof(tmp)) == sizeof(tmp) && tmp.magic == WS_MAGIC) g_c = tmp;
    p.end();
  }
  // 20.10.2026: прежний дефолт 15 -> 35 минут. Поднимаем один раз; метка stale35 не даёт
  // перезаписывать выставленные вручную 15 при следующих запусках.
  bool bump = false;
  Preferences q;
  if (q.begin("wsens", false)) {
    if (!q.isKey("stale35")) {
      if (g_c.staleMin == 15) { g_c.staleMin = 35; bump = true; }
      q.putBool("stale35", true);
    }
    q.end();
  }
  if (bump) saveCfg();
}

static int findIdx(const char* key) {
  for (int i = 0; i < WS_MAX; i++) if (g_c.e[i].key[0] && strcmp(g_c.e[i].key, key) == 0) return i;
  return -1;
}

static int addIdx(const char* key) {
  for (int i = 0; i < WS_MAX; i++) if (!g_c.e[i].key[0]) {
    memset(&g_c.e[i], 0, sizeof(WsEntry));
    strlcpy(g_c.e[i].key, key, WS_KEYLEN);
    g_c.e[i].show = 1;
    g_l[i] = WsLive{NAN, NAN, NAN, 0, 0};
    return i;
  }
  return -1;
}

// Принять одно показание. true - принято.
static bool ingest(const String& id, const String& name, float t, float h, float p, int rssi) {
  String key = id + "/" + name;
  if (id.length() == 0 || name.length() == 0 || key.length() >= WS_KEYLEN) { g_lastErr = "плохой id/n"; return false; }
  if (!isnan(t) && (t < -80 || t > 100)) t = NAN;       // явный мусор отбрасываем
  if (!isnan(h) && (h < 0 || h > 100)) h = NAN;
  if (!isnan(p) && (p < 300 || p > 1100)) p = NAN;
  if (isnan(t) && isnan(h) && isnan(p)) { g_lastErr = "нет значений в " + key; return false; }
  Lock l;
  int i = findIdx(key.c_str());
  bool created = false;
  if (i < 0) {
    i = addIdx(key.c_str());
    if (i < 0) { g_lastErr = "слишком много датчиков (максимум " + String(WS_MAX) + ")"; return false; }
    created = true;
  }
  g_l[i].t = isnan(t) ? NAN : t + g_c.e[i].offT;
  g_l[i].h = isnan(h) ? NAN : constrain(h + g_c.e[i].offH, 0.0f, 100.0f);
  g_l[i].p = isnan(p) ? NAN : p + g_c.e[i].offP;
  g_l[i].ms = millis() ? millis() : 1;
  g_l[i].rssi = rssi;
  if (created) saveCfg();
  return true;
}

static bool secretOk(const String& k) {
  Lock l;
  return g_c.secret[0] == 0 || k == g_c.secret;
}

// копия строки с обрезкой по границе UTF-8 символа
static void copyUtf8(char* dst, const char* src, size_t n) {
  strlcpy(dst, src, n);
  size_t l = strlen(dst);
  if (l + 1 >= n && l > 0) {                      // возможно обрезали: убрать оборванный символ
    size_t i = l;
    while (i > 0 && (((uint8_t)dst[i - 1]) & 0xC0) == 0x80) i--;
    if (i > 0 && (((uint8_t)dst[i - 1]) & 0x80)) {
      uint8_t lead = (uint8_t)dst[i - 1];
      size_t need = (lead >= 0xF0) ? 4 : (lead >= 0xE0) ? 3 : 2;
      if (l - (i - 1) < need) dst[i - 1] = 0;
    }
  }
}

static float jf(JsonVariantConst v) { return v.isNull() ? NAN : v.as<float>(); }

// ---------------- строка для бегущей строки ----------------
String wsensorsLineText() {
  Lock l;
  if (!g_c.lineOn) return "";
  uint32_t now = millis();
  uint32_t stale = (uint32_t)g_c.staleMin * 60000UL;
  String out;
  for (int i = 0; i < WS_MAX; i++) {
    if (!g_c.e[i].key[0] || !g_c.e[i].show || g_l[i].ms == 0) continue;
    if (stale && now - g_l[i].ms > stale) continue;
    char b[96];
    String part = g_c.e[i].label[0] ? String(g_c.e[i].label) : String(g_c.e[i].key);
    if (!isnan(g_l[i].t)) { snprintf(b, sizeof(b), " %+.1f°C", (double)g_l[i].t); part += b; }
    if (!isnan(g_l[i].h)) { snprintf(b, sizeof(b), " %d%%", (int)lroundf(g_l[i].h)); part += b; }
    if (!isnan(g_l[i].p)) { snprintf(b, sizeof(b), " %d мм рт.ст.", (int)lroundf(g_l[i].p * 0.750062f)); part += b; }
    if (out.length()) out += ",  ";
    out += part;
  }
  if (out.length()) out += "   ";
  return out;
}

uint32_t wsensorsLineColor() { Lock l; return g_c.rgb; }
int wsensorsLineSpeed() { Lock l; return g_c.speed < 1 ? 5 : (g_c.speed > 10 ? 10 : g_c.speed); }

int wsensorsSrc(int metric) {
  Lock l;
  return (metric >= 0 && metric < 3) ? g_src[metric] : 0;
}

// sel: 0 = авто (первый со свежими данными), N = N-й известный датчик
bool wsensorsPick(int sel, float& t, float& h, float& p) {
  Lock l;
  const uint32_t now = millis(), stale = (uint32_t)g_c.staleMin * 60000UL;
  int seen = 0;
  for (int i = 0; i < WS_MAX; i++) {
    if (!g_c.e[i].key[0]) continue;
    if (g_l[i].ms == 0) continue;
    if (stale && now - g_l[i].ms > stale) continue;
    seen++;
    if (sel > 0 && seen != sel) continue;
    t = g_l[i].t; h = g_l[i].h; p = g_l[i].p;
    return true;
  }
  return false;
}

bool wsensorsHome(float& t, float& h, float& p) {
  Lock l;
  uint32_t now = millis(), stale = (uint32_t)g_c.staleMin * 60000UL;
  for (int i = 0; i < WS_MAX; i++) {
    if (!g_c.e[i].key[0] || !(g_homeMask & (1u << i)) || g_l[i].ms == 0) continue;
    if (stale && now - g_l[i].ms > stale) continue;
    t = g_l[i].t; h = g_l[i].h; p = g_l[i].p;
    return true;
  }
  return false;
}

bool wsensorsMain(float& t, float& h, float& p) {
  Lock l;
  uint32_t now = millis(), stale = (uint32_t)g_c.staleMin * 60000UL;
  for (int i = 0; i < WS_MAX; i++) {
    if (!g_c.e[i].key[0] || !g_c.e[i].main || g_l[i].ms == 0) continue;
    if (stale && now - g_l[i].ms > stale) continue;
    t = g_l[i].t; h = g_l[i].h; p = g_l[i].p;
    return true;
  }
  return false;
}

// ---------------- перечисление (плотные индексы 0..count-1) ----------------
int wsensorsCount() {
  Lock l;
  int n = 0;
  for (int i = 0; i < WS_MAX; i++) if (g_c.e[i].key[0]) n++;
  return n;
}

bool wsensorsInfo(int idx, String& key, String& label) {
  Lock l;
  int n = 0;
  for (int i = 0; i < WS_MAX; i++) {
    if (!g_c.e[i].key[0]) continue;
    if (n++ == idx) { key = g_c.e[i].key; label = g_c.e[i].label; return true; }
  }
  return false;
}

bool wsensorsValues(int idx, float& t, float& h, float& p, uint32_t& ageMs) {
  Lock l;
  int n = 0;
  for (int i = 0; i < WS_MAX; i++) {
    if (!g_c.e[i].key[0]) continue;
    if (n++ == idx) {
      t = g_l[i].t; h = g_l[i].h; p = g_l[i].p;
      ageMs = g_l[i].ms == 0 ? 0 : (uint32_t)(millis() - g_l[i].ms);
      return true;
    }
  }
  return false;
}

int wsensorsStaleMin() {
  Lock l;
  return g_c.staleMin;
}

// ---------------- веб ----------------
static String esc(const String& s) {
  String o;
  for (unsigned i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '&') o += "&amp;"; else if (c == '<') o += "&lt;"; else if (c == '>') o += "&gt;"; else if (c == '"') o += "&quot;"; else o += c;
  }
  return o;
}

static String hex6(uint32_t rgb) { char b[8]; snprintf(b, sizeof(b), "#%06X", (unsigned)(rgb & 0xFFFFFF)); return b; }

static String buildPage(const String& notice) {
  String h;
  h.reserve(8000);
  h += "<!DOCTYPE html><html lang=\"ru\"><head><meta charset=\"utf-8\"><link rel=\"icon\" type=\"image/png\" href=\"/logo.png\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
       "<title>Датчики по Wi-Fi</title><style>"
       "body{font-family:sans-serif;background:#111;color:#eee;margin:0;padding:12px}.wrap{max-width:600px;margin:0 auto}h1{font-size:20px}"
       "a{color:#5ac8fa}.card{border:1px solid #333;border-radius:10px;margin:14px 0;padding:10px 14px;background:#1a1a1a}"
       ".t{color:#fcd05a;font-weight:bold;margin:6px 0}label{display:block;margin:8px 0 3px}"
       "input[type=text],input[type=password],input[type=number]{width:100%;background:#222;color:#eee;border:1px solid #444;border-radius:6px;padding:8px;font-size:15px;box-sizing:border-box}"
       "input[type=color]{height:36px;width:80px}.row{display:flex;gap:8px}.row>div{flex:1}"
       ".cb{display:flex;align-items:center;gap:10px;margin:8px 0}input[type=checkbox]{width:22px;height:22px}"
       "button{width:100%;padding:14px;border:0;border-radius:8px;background:#0ac;color:#fff;font-size:17px;margin:10px 0}"
       ".hint{color:#999;font-size:13px}.ok{background:#1d4d2b;border-radius:8px;padding:10px;margin:10px 0}.old{color:#f88}"
       "code{background:#222;padding:2px 5px;border-radius:4px}</style></head><body><div class=\"wrap\">"
       "<p><a href=\"/\">Главная</a> &nbsp; <a href=\"/settings/integrations\">Интеграции</a> &nbsp; <a href=\"/settings/receive\">Приём данных</a></p>"
       "<h1><img src='/logo.png' alt='' style='height:34px;vertical-align:middle;margin-right:10px;border-radius:6px'>Датчики по Wi-Fi</h1>";
  if (notice.length()) h += "<div class=\"ok\">" + notice + "</div>";
  Lock l;
  h += "<form method=\"POST\" action=\"/settings/sensors\">";
  h += "<div class=\"card\"><div class=\"t\">Экран «Дома»: откуда брать показания</div>";
  {
    const char* nm3[3] = {"Температура", "Влажность", "Давление"};
    const char* fld[3] = {"htsrc", "hhsrc", "hpsrc"};
    for (int m = 0; m < 3; m++) {
      h += String("<label>") + nm3[m] + "</label><select name=\"" + fld[m] + "\">";
      h += String("<option value=\"0\"") + (g_src[m] == 0 ? " selected" : "") + ">авто (любой со свежими данными)</option>";
      int n = 0;
      for (int i = 0; i < WS_MAX; i++) {
        if (!g_c.e[i].key[0]) continue;
        n++;
        const String nm = g_c.e[i].label[0] ? String(g_c.e[i].label) : String(g_c.e[i].key);
        h += String("<option value=\"") + String(n) + "\"" + (g_src[m] == n ? " selected" : "") + ">" + esc(nm) + "</option>";
      }
      h += "</select>";
    }
  }
  h += "</div>";

  h += "<div class=\"cb\"><input type=\"radio\" name=\"homesel\" value=\"0\"" +
       String(g_homeMask == 0 ? " checked" : "") +
       "><label style=\"margin:0\">Дома: датчик станции</label></div>";
  h += "<div class=\"card\"><div class=\"t\">Бегущая строка</div>"
       "<div class=\"cb\"><input type=\"checkbox\" id=\"lineOn\" name=\"lineOn\"";
  h += g_c.lineOn ? " checked" : "";
  h += "><label for=\"lineOn\" style=\"margin:0\">Показывать показания датчиков</label></div>"
       "<label>Цвет текста</label><input type=\"color\" name=\"rgb\" value=\"" + hex6(g_c.rgb) + "\">"
       "<label>Скорость (1..10)</label><input type=\"number\" name=\"speed\" min=\"1\" max=\"10\" value=\"" + String(g_c.speed) + "\">"
       "<label>Убирать значение, если датчик молчит (минут, 0 - не убирать)</label><input type=\"number\" name=\"stale\" min=\"0\" max=\"1440\" value=\"" + String(g_c.staleMin) + "\">"
       "<label>Ключ передатчика (пусто - без проверки)</label><input type=\"text\" name=\"secret\" maxlength=\"23\" value=\"" + esc(g_c.secret) + "\">"
       "<p class=\"hint\">Тот же ключ укажите в прошивке датчика (параметр <code>STATION_KEY</code>).</p></div>";

  int n = 0;
  uint32_t now = millis();
  for (int i = 0; i < WS_MAX; i++) {
    if (!g_c.e[i].key[0]) continue;
    n++;
    String pfx = "e" + String(i) + "_";
    String st;
    if (g_l[i].ms == 0) st = "<span class=\"old\">данных нет</span>";
    else {
      uint32_t age = (now - g_l[i].ms) / 1000;
      char b[96]; String v;
      if (!isnan(g_l[i].t)) { snprintf(b, sizeof(b), "%+.1f&deg;C ", (double)g_l[i].t); v += b; }
      if (!isnan(g_l[i].h)) { snprintf(b, sizeof(b), "%d%% ", (int)lroundf(g_l[i].h)); v += b; }
      if (!isnan(g_l[i].p)) { snprintf(b, sizeof(b), "%d мм (%.1f гПа) ", (int)lroundf(g_l[i].p * 0.750062f), (double)g_l[i].p); v += b; }
      String ageS = age < 90 ? String(age) + " с назад" : String(age / 60) + " мин назад";
      st = v + "<span class=\"hint\">" + ageS + (g_l[i].rssi ? ", Wi-Fi " + String(g_l[i].rssi) + " дБм" : String("")) + "</span>";
    }
    // Название - первым полем карточки: оно подставляется в бегущую строку, в бота и в списки
    const String title = g_c.e[i].label[0] ? String(g_c.e[i].label) : String(g_c.e[i].key);
    h += "<div class=\"card\"><div class=\"t\">" + esc(title) + "</div>";
    if (g_c.e[i].label[0]) h += "<div class=\"hint\">ключ: <code>" + esc(g_c.e[i].key) + "</code></div>";
    h += "<div>" + st + "</div>";
    h += "<label>Название датчика</label><input type=\"text\" name=\"" + pfx + "label\" maxlength=\"63\" value=\"" +
         esc(g_c.e[i].label) + "\" placeholder=\"например Улица или Балкон\">"
         "<p class=\"hint\">Это имя показывается в бегущей строке, в сообщениях бота и в списке датчиков.</p>";
    h += "<div class=\"cb\"><input type=\"checkbox\" id=\"" + pfx + "show\" name=\"" + pfx + "show\"" + String(g_c.e[i].show ? " checked" : "") +
         "><label for=\"" + pfx + "show\" style=\"margin:0\">Показывать в бегущей строке</label></div>";
    h += "<div class=\"cb\"><input type=\"checkbox\" id=\"" + pfx + "main\" name=\"" + pfx + "main\"" + String(g_c.e[i].main ? " checked" : "") +
         "><label for=\"" + pfx + "main\" style=\"margin:0\">Основной датчик (для экранов)</label></div>";
    h += "<div class=\"cb\"><input type=\"radio\" name=\"homesel\" value=\"" +
         String(i + 1) + "\"" + String((g_homeMask == (1u << i)) ? " checked" : "") +
         "><label style=\"margin:0\">Показывать на экране «Дома»</label></div>";
    h += "<div class=\"row\"><div><label>Поправка &deg;C</label><input type=\"number\" step=\"0.1\" name=\"" + pfx + "ot\" value=\"" + String(g_c.e[i].offT, 1) + "\"></div>"
         "<div><label>Поправка %</label><input type=\"number\" step=\"0.1\" name=\"" + pfx + "oh\" value=\"" + String(g_c.e[i].offH, 1) + "\"></div>"
         "<div><label>Поправка гПа</label><input type=\"number\" step=\"0.1\" name=\"" + pfx + "op\" value=\"" + String(g_c.e[i].offP, 1) + "\"></div></div>";
    h += "<div class=\"cb\"><input type=\"checkbox\" id=\"" + pfx + "del\" name=\"" + pfx + "del\"><label for=\"" + pfx + "del\" style=\"margin:0\">Удалить из списка</label></div></div>";
  }
  if (n == 0) h += "<div class=\"card\"><p>Датчиков пока нет. Они появятся здесь сами, как только первый датчик пришлёт данные.</p></div>";
  h += "</form>";
  h += "<div class=\"card\"><div class=\"t\">Как подключить датчик</div>"
       "<p class=\"hint\">Прошивка датчика сама шлёт данные на адрес станции. Принято: " + String(g_okCount) + ", отклонено: " + String(g_badCount) +
       (g_lastErr.length() ? ", последняя ошибка: " + esc(g_lastErr) : String("")) + ".<br>"
       "Для проверки из браузера: <code>/api/sensor?id=test&amp;n=bme1&amp;t=21.5&amp;h=45&amp;p=1013</code></p></div>";
  h += "</div><script>(function(){var cs=document.querySelectorAll('.card,fieldset');for(var i=0;i<cs.length;i++){var c=cs[i];if(!c.querySelector('input,select,textarea'))continue;var b=document.createElement('button');b.type='submit';b.textContent='Сохранить';b.style.cssText='width:auto;margin-top:10px;padding:5px 12px;font-size:12px;border-radius:6px;border:0;background:#2b6cb0;color:#fff;cursor:pointer';b.onclick=function(ev){var fr=this.closest('form');if(fr&&!fr.id){ev.preventDefault();fr.submit();}};c.appendChild(b);}})();</script></body></html>";
  return h;
}

static float fparam(AsyncWebServerRequest* r, const String& n, float def) {
  return r->hasParam(n, true) ? r->getParam(n, true)->value().toFloat() : def;
}

void wsensorsRegisterWeb(AsyncWebServer& srv) {
  // ---- приём: GET (простой датчик) ----
  srv.on("/api/sensor", HTTP_GET, [](AsyncWebServerRequest* r) {
    auto q = [&](const char* n) -> String { return r->hasParam(n) ? r->getParam(n)->value() : String(""); };
    if (!secretOk(q("k"))) { g_badCount++; g_lastErr = "неверный ключ"; r->send(403, "text/plain", "bad key"); return; }
    auto num = [&](const char* n) -> float { return r->hasParam(n) ? r->getParam(n)->value().toFloat() : NAN; };
    bool ok = ingest(q("id"), q("n"), num("t"), num("h"), num("p"), q("rssi").toInt());
    if (ok) g_okCount++; else g_badCount++;
    r->send(ok ? 200 : 400, "text/plain", ok ? "ok" : "bad request");
  });

  // ---- приём: POST с JSON (несколько датчиков за раз) ----
  srv.on("/api/sensor", HTTP_POST,
    [](AsyncWebServerRequest* r) {
      char* body = (char*)r->_tempObject;
      if (!body) { g_badCount++; g_lastErr = "пустой запрос"; r->send(400, "text/plain", "empty"); return; }
      JsonDocument d;
      if (deserializeJson(d, body)) { g_badCount++; g_lastErr = "не JSON"; r->send(400, "text/plain", "bad json"); return; }
      if (!secretOk(d["k"] | "")) { g_badCount++; g_lastErr = "неверный ключ"; r->send(403, "text/plain", "bad key"); return; }
      String id = d["id"] | "";
      int rssi = d["rssi"] | 0;
      int good = 0, bad = 0;
      for (JsonVariantConst s : d["s"].as<JsonArrayConst>()) {
        if (ingest(id, String(s["n"] | ""), jf(s["t"]), jf(s["h"]), jf(s["p"]), rssi)) good++; else bad++;
      }
      g_okCount += good; g_badCount += bad;
      r->send(good ? 200 : 400, "text/plain", good ? "ok" : "bad request");
    },
    nullptr,
    [](AsyncWebServerRequest* r, uint8_t* data, size_t len, size_t index, size_t total) {
      if (total == 0 || total > WS_BODY_MAX) return;
      if (index == 0) r->_tempObject = calloc(total + 1, 1);   // освобождается самим сервером
      if (r->_tempObject) memcpy((uint8_t*)r->_tempObject + index, data, len);
    });

  // ---- страница настроек ----
  srv.on("/settings/sensors", HTTP_GET, [](AsyncWebServerRequest* r) {
    if (!integrationsAuth(r)) return;
    r->send(200, "text/html; charset=utf-8", buildPage(r->hasParam("ok") ? "Сохранено" : ""));
  });
  srv.on("/settings/sensors", HTTP_POST, [](AsyncWebServerRequest* r) {
    if (!integrationsAuth(r)) return;
    {
      Lock l;
      g_c.lineOn = r->hasParam("lineOn", true);
      if (r->hasParam("rgb", true)) {
        String c = r->getParam("rgb", true)->value();
        if (c.length() == 7 && c[0] == '#') g_c.rgb = strtoul(c.c_str() + 1, nullptr, 16);
      }
      g_c.speed = constrain((int)fparam(r, "speed", g_c.speed), 1, 10);
      g_c.staleMin = constrain((int)fparam(r, "stale", g_c.staleMin), 0, 1440);
      if (r->hasParam("secret", true)) strlcpy(g_c.secret, r->getParam("secret", true)->value().c_str(), sizeof(g_c.secret));
      for (int i = 0; i < WS_MAX; i++) {
        if (!g_c.e[i].key[0]) continue;
        String p = "e" + String(i) + "_";
        if (r->hasParam(p + "del", true)) { memset(&g_c.e[i], 0, sizeof(WsEntry)); g_l[i] = WsLive{NAN, NAN, NAN, 0, 0}; continue; }
        if (r->hasParam(p + "label", true)) copyUtf8(g_c.e[i].label, r->getParam(p + "label", true)->value().c_str(), WS_LABLEN);
        g_c.e[i].show = r->hasParam(p + "show", true);
        g_c.e[i].main = r->hasParam(p + "main", true);
        g_c.e[i].offT = fparam(r, p + "ot", g_c.e[i].offT);
        g_c.e[i].offH = fparam(r, p + "oh", g_c.e[i].offH);
        g_c.e[i].offP = fparam(r, p + "op", g_c.e[i].offP);
      }
      if (r->hasParam("homesel", true)) {
        const int v = r->getParam("homesel", true)->value().toInt();
        g_homeMask = (v > 0 && v <= WS_MAX) ? (1u << (v - 1)) : 0;
      }
      if (r->hasParam("htsrc", true)) g_src[0] = (uint8_t)r->getParam("htsrc", true)->value().toInt();
      if (r->hasParam("hhsrc", true)) g_src[1] = (uint8_t)r->getParam("hhsrc", true)->value().toInt();
      if (r->hasParam("hpsrc", true)) g_src[2] = (uint8_t)r->getParam("hpsrc", true)->value().toInt();
      saveCfg();
    }
    r->redirect("/settings/sensors?ok=1");
  });
}
