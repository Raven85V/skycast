#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

// Состояние станции в JSON для внешних интеграций (Home Assistant / HACS).
String apiStateJson();
