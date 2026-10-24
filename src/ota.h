#pragma once
class AsyncWebServer;

// Обновление прошивки по Wi-Fi: страница /update, загрузка файла firmware.bin из браузера.
// Работает только если задан пароль на веб-настройки (страница «Интеграции»).
// Нужна таблица разделов с двумя слотами (partitions_16MB.csv), поэтому первый раз - прошивка по USB.
void otaRegisterWeb(AsyncWebServer& srv);
