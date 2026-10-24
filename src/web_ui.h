#pragma once
#include <Arduino.h>

// Веб-интерфейс настроек: одна страница, открывается с телефона или компьютера по IP станции.
// Если Wi-Fi не подключился - станция поднимает точку доступа и captive-портал (как в PixelHub 75).

void webUiBegin();          // запустить веб-сервер (после подключения к Wi-Fi)
void webUiStartPortal();    // точка доступа "CrowPanel-Weather" + портал настройки (адрес 192.168.4.1)
void webUiLoop();           // вызывать в loop() (нужно для DNS портала)
bool webUiPortalActive();
