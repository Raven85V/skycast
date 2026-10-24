#include "alarm.h"
#include "audio_engine.h"
#include "settings.h"
#include <SD.h>

static const char* ALARM_FOLDER = "/alarms";
static const uint32_t RAMP_SECONDS = 15;     // за сколько секунд звук доходит до заданной громкости

static bool     active = false;
static int      curDay = 0;
static uint32_t startedAt = 0;
static bool     testing = false;

static uint8_t targetVolume() {
  return nightNow() ? g_set.volAlarmNight : g_set.volAlarmDay;
}

static String pathFor(int track) {
  char b[32];
  snprintf(b, sizeof(b), "%s/%03d.mp3", ALARM_FOLDER, track);
  return String(b);
}

// Одна ошибка в лог, без повторов
static bool startSound(int track, uint8_t startVol) {
  String p = pathFor(track);
  if (!SD.exists(p)) {
    Serial.println("Будильник: файл не найден на SD: " + p);
    return false;
  }
  setAudioVolume(startVol);
  playAudioFile(p);
  return true;
}

// Громкость с плавным нарастанием: через RAMP_SECONDS достигает цели
static uint8_t rampedVolume(uint32_t elapsedSec) {
  uint8_t tgt = targetVolume();
  if (elapsedSec >= RAMP_SECONDS || tgt <= 1) return tgt;
  uint32_t v = (uint32_t)tgt * (elapsedSec + 1) / (RAMP_SECONDS + 1);
  return v < 1 ? 1 : (uint8_t)v;
}

void alarmTick(const tm& t) {
  static int lastTriggerKey = -1;      // защита от повторного запуска в ту же минуту

  if (!active) {
    int wd = t.tm_wday;
    int key = t.tm_yday * 1440 + t.tm_hour * 60 + t.tm_min;
    if (g_set.alarmEn[wd] && t.tm_hour == g_set.alarmH[wd] && t.tm_min == g_set.alarmM[wd] &&
        key != lastTriggerKey) {
      lastTriggerKey = key;
      Serial.printf("Будильник сработал (день %d, мелодия %d)\n", wd, g_set.alarmTrack[wd]);
      if (testing) { stopAudio(); testing = false; }
      if (startSound(g_set.alarmTrack[wd], rampedVolume(0))) {
        curDay = wd;
        active = true;
        startedAt = millis();
      }
    }
    return;
  }

  uint32_t elapsedMs = millis() - startedAt;
  if (elapsedMs >= (uint32_t)g_set.alarmAutoStopMin * 60000UL) {
    alarmStop();
    Serial.println("Будильник: автостоп по таймауту");
    return;
  }
  setAudioVolume(rampedVolume(elapsedMs / 1000));
  if (!isAudioPlaying()) {
    // мелодия доиграла, время автостопа не вышло - запускаем снова
    if (!startSound(g_set.alarmTrack[curDay], rampedVolume(elapsedMs / 1000))) alarmStop();
  }
}

bool alarmIsActive() { return active; }

void alarmStop() {
  if (!active) return;
  stopAudio();
  active = false;
}

void alarmInfo(int& hour, int& minute) {
  hour = g_set.alarmH[curDay];
  minute = g_set.alarmM[curDay];
}

// ---------- проверка звука из настроек ----------
void alarmTestToggle(int wday) {
  if (active) return;
  if (testing) {
    stopAudio();
    testing = false;
    return;
  }
  if (startSound(g_set.alarmTrack[wday], targetVolume())) testing = true;
}

bool alarmTestActive() {
  if (testing && !isAudioPlaying()) testing = false;   // файл доиграл сам
  return testing;
}

void alarmTestStop() {
  if (!testing) return;
  stopAudio();
  testing = false;
}
