#include "ota.h"
#include "integrations.h"
#include <Update.h>
#include <ESPAsyncWebServer.h>

static bool otaFailed = false;

static const char* PAGE_HEAD =
  "<!DOCTYPE html><html lang=\"ru\"><head><meta charset=\"utf-8\"><link rel=\"icon\" type=\"image/png\" href=\"/logo.png\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
  "<title>Обновление прошивки</title><style>body{font-family:sans-serif;background:#111;color:#eee;padding:16px;max-width:560px;margin:0 auto}"
  "a{color:#5ac8fa}button{width:100%;padding:14px;border:0;border-radius:8px;background:#0ac;color:#fff;font-size:17px;margin:14px 0}"
  "input[type=file]{width:100%;margin:10px 0}.hint{color:#999;font-size:13px}.err{background:#5a1d1d;border-radius:8px;padding:10px}"
  ".ok{background:#1d4d2b;border-radius:8px;padding:10px}</style></head><body><p><a href=\"/\">Главная</a></p><h1><img src='/logo.png' alt='' style='height:34px;vertical-align:middle;margin-right:10px;border-radius:6px'>Обновление прошивки</h1>";

static void restartSoon(uint32_t ms) {
  xTaskCreate([](void* p) {
    vTaskDelay(pdMS_TO_TICKS((uint32_t)(uintptr_t)p));
    ESP.restart();
  }, "ota_restart", 2048, (void*)(uintptr_t)ms, 1, nullptr);
}

// куски файла приходят сюда; обработчик живёт в сетевой задаче, на экран и звук это не влияет
static void onUpload(AsyncWebServerRequest* req, const String& filename, size_t index, uint8_t* data, size_t len, bool final) {
  if (!integrationsPasswordSet() || !integrationsCheckAuth(req)) return;   // без пароля и без входа - ничего не пишем
  if (index == 0) {
    otaFailed = false;
    Serial.printf("OTA: приём файла %s\n", filename.c_str());
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
      Serial.println("OTA: Update.begin не удался (нужны два слота прошивки в таблице разделов)");
      otaFailed = true;
    }
  }
  if (!otaFailed && Update.isRunning()) {
    if (Update.write(data, len) != len) {
      Serial.println("OTA: ошибка записи");
      otaFailed = true;
    }
  }
  if (final && !otaFailed) {
    if (!Update.end(true)) {
      Serial.println("OTA: Update.end не удался (файл не является прошивкой для этой платы?)");
      otaFailed = true;
    }
  }
}

void otaRegisterWeb(AsyncWebServer& srv) {
  srv.on("/update", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (!integrationsAuth(req)) return;
    String h = PAGE_HEAD;
    if (!integrationsPasswordSet()) {
      h += "<div class=\"err\">Сначала задайте пароль на странице «Интеграции»: без пароля обновление по Wi-Fi выключено, "
           "чтобы никто в сети не мог загрузить чужую прошивку.</div><script>(function(){var cs=document.querySelectorAll('.card,fieldset');for(var i=0;i<cs.length;i++){var c=cs[i];if(!c.querySelector('input,select,textarea'))continue;var b=document.createElement('button');b.type='submit';b.textContent='Сохранить';b.style.cssText='width:auto;margin-top:10px;padding:5px 12px;font-size:12px;border-radius:6px;border:0;background:#2b6cb0;color:#fff;cursor:pointer';b.onclick=function(ev){var fr=this.closest('form');if(fr&&!fr.id){ev.preventDefault();fr.submit();}};c.appendChild(b);}})();</script></body></html>";
    } else {
      h += "<form method=\"POST\" action=\"/update\" enctype=\"multipart/form-data\">"
           "<p>Выберите файл <b>firmware.bin</b> (после сборки он лежит в <code>.pio/build/skycast/</code>). "
           "После загрузки станция сама перезагрузится.</p>"
           "<input type=\"file\" name=\"firmware\" accept=\".bin\"><button type=\"submit\">Загрузить и обновить</button></form>"
           "<p class=\"hint\">Не выключайте питание, пока идёт загрузка. Настройки сохраняются.</p><script>(function(){var cs=document.querySelectorAll('.card,fieldset');for(var i=0;i<cs.length;i++){var c=cs[i];if(!c.querySelector('input,select,textarea'))continue;var b=document.createElement('button');b.type='submit';b.textContent='Сохранить';b.style.cssText='width:auto;margin-top:10px;padding:5px 12px;font-size:12px;border-radius:6px;border:0;background:#2b6cb0;color:#fff;cursor:pointer';b.onclick=function(ev){var fr=this.closest('form');if(fr&&!fr.id){ev.preventDefault();fr.submit();}};c.appendChild(b);}})();</script></body></html>";
    }
    req->send(200, "text/html; charset=utf-8", h);
  });
  srv.on("/update", HTTP_POST,
    [](AsyncWebServerRequest* req) {
      if (!integrationsPasswordSet()) { req->send(403, "text/plain; charset=utf-8", "Обновление выключено: задайте пароль"); return; }
      if (!integrationsCheckAuth(req)) { req->requestAuthentication(); return; }
      String h = PAGE_HEAD;
      if (otaFailed || Update.hasError()) {
        h += "<div class=\"err\">Не удалось обновить. Проверьте, что это firmware.bin для этой платы, и попробуйте ещё раз. "
             "Подробности - в мониторе порта.</div><p><a href=\"/update\">Назад</a></p><script>(function(){var cs=document.querySelectorAll('.card,fieldset');for(var i=0;i<cs.length;i++){var c=cs[i];if(!c.querySelector('input,select,textarea'))continue;var b=document.createElement('button');b.type='submit';b.textContent='Сохранить';b.style.cssText='width:auto;margin-top:10px;padding:5px 12px;font-size:12px;border-radius:6px;border:0;background:#2b6cb0;color:#fff;cursor:pointer';b.onclick=function(ev){var fr=this.closest('form');if(fr&&!fr.id){ev.preventDefault();fr.submit();}};c.appendChild(b);}})();</script></body></html>";
        req->send(200, "text/html; charset=utf-8", h);
      } else {
        h += "<div class=\"ok\">Готово. Станция перезагружается, через полминуты откройте её адрес снова.</div><script>(function(){var cs=document.querySelectorAll('.card,fieldset');for(var i=0;i<cs.length;i++){var c=cs[i];if(!c.querySelector('input,select,textarea'))continue;var b=document.createElement('button');b.type='submit';b.textContent='Сохранить';b.style.cssText='width:auto;margin-top:10px;padding:5px 12px;font-size:12px;border-radius:6px;border:0;background:#2b6cb0;color:#fff;cursor:pointer';b.onclick=function(ev){var fr=this.closest('form');if(fr&&!fr.id){ev.preventDefault();fr.submit();}};c.appendChild(b);}})();</script></body></html>";
        req->send(200, "text/html; charset=utf-8", h);
        restartSoon(1500);
      }
    },
    onUpload);
}
