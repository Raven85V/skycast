#include "chime.h"
#include "alarm.h"
#include "audio_engine.h"
#include "settings.h"
#include <SD.h>

// Порядок совпадает с настройкой g_set.chimeVoice
static const char* VOICE_FOLDERS[4] = {"22", "23", "12", "13"};

static bool testing = false;

static String pathFor(int hour) {
  int v = g_set.chimeVoice < 4 ? g_set.chimeVoice : 0;
  char b[24];
  snprintf(b, sizeof(b), "/%s/%03d.mp3", VOICE_FOLDERS[v], hour);
  return String(b);
}

static bool play(int hour, bool night) {
  String p = pathFor(hour);
  if (!SD.exists(p)) {
    Serial.println("Озвучка времени: файл не найден на SD: " + p);
    return false;
  }
  setAudioVolume(night ? g_set.volChimeNight : g_set.volChimeDay);
  playAudioFile(p);
  return true;
}

void chimeTick(const tm& t) {
  // Окно в первые 30 секунд часа (а не ровно секунда 0): если loop на мгновение
  // занят, например запросом погоды, озвучка всё равно сработает. Один раз за час.
  static int lastKey = -1;
  if (t.tm_min != 0 || t.tm_sec >= 30) return;
  int key = t.tm_yday * 24 + t.tm_hour;
  if (key == lastKey) return;
  lastKey = key;

  bool night = nightNow();
  if (!(night ? g_set.chimeNight : g_set.chimeDay)) return;
  if (alarmIsActive()) return;               // не перебиваем звонящий будильник

  if (testing) { stopAudio(); testing = false; }
  if (play(t.tm_hour, night)) {
    Serial.printf("Озвучка времени: час %d, голос %d, ночь=%d\n", t.tm_hour, g_set.chimeVoice, night);
  }
}

// ---------- проверка из настроек ----------
void chimeTestToggle(int hour) {
  if (alarmIsActive()) return;
  if (testing) {
    stopAudio();
    testing = false;
    return;
  }
  if (play(hour, nightNow())) testing = true;
}

bool chimeTestActive() {
  if (testing && !isAudioPlaying()) testing = false;   // файл доиграл сам
  return testing;
}

void chimeTestStop() {
  if (!testing) return;
  stopAudio();
  testing = false;
}
