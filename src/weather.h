#pragma once
#include <Arduino.h>
#include <time.h>

struct CurrentWx {
  float  temp = 0, feels = 0, wind = 0;
  int    humidity = 0;
  int    pressureMmHg = 0;
  int    windDeg = 0;
  int    clouds = 0;
  char   icon[4] = "01d";
  char   desc[64] = "";
  time_t sunrise = 0, sunset = 0;
};

struct DayWx {
  time_t ts = 0;          // момент внутри этого дня (для названия дня)
  float  tmin = 0, tmax = 0;
  int    pop = 0;         // вероятность осадков, %
  char   icon[4] = "01d"; // всегда дневной вариант
};

struct Weather {
  bool      hasCur = false;
  CurrentWx cur;
  int       dayCount = 0;
  DayWx     days[5];
  time_t    updated = 0;
  int       popNow = 0;   // максимум вероятности осадков по оставшимся часам сегодня
};

// Обновляет погоду. true, если получена хотя бы текущая погода или прогноз.
// В err - краткое описание проблемы (если что-то не получилось).
bool weatherUpdate(Weather& w, String& err);

// История давления за последние часы из архива Open-Meteo (без ключа).
// out[0] - самое свежее значение, дальше в прошлое. Возвращает сколько заполнено.
int weatherFetchPressureHistory(float* out, int maxN);

// Ключ суток по локальному времени (для сравнения дат)
int dayKey(time_t t);
