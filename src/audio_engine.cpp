#include "audio_engine.h"
#include <SPI.h>
#include <SD.h>
#include "pins.h"

// ============================================================
// ОБЪЕДИНЁННЫЙ движок на schreibfaul1/ESP32-audioI2S.
// Раньше здесь был самописный WAV-парсер поверх встроенного ESP_I2S,
// потому что эта библиотека не собиралась (не было C++20 в build_flags -
// см. platformio.ini). Проверено на реальном железе (см. заметки
// проекта): собирается и стабильно работает часами на ESP32-S3
// (интернет-поток, смена треков, утечек памяти нет).
//
// ВАЖНО: библиотека требует PSRAM и работает только на многоядерных
// чипах (ESP32-S3/P4) - у нас ESP32-S3 N16R8 с Octal PSRAM, подходит.
// ============================================================

Audio audio;

// Отдельный SPI-объект для SD-карты (аппаратный FSPI/SPI2) - как в
// рабочем тесте на плате информера. Пины задаются в pins.h.
static SPIClass sdSPI(FSPI);

static bool sdOk = false;
static AudioEventCallback externalCallback = nullptr;

// Коллбэк библиотеки (актуальный API с версии 3.4.2+) - логируем в
// Serial для отладки И пробрасываем событие дальше, если radio.cpp
// (или кто-то ещё) подписался через setAudioEventCallback().
static void onAudioInfo(Audio::msg_t m) {
  Serial.printf("[audio:%s] %s\n", m.s, m.msg);
  if (externalCallback) externalCallback(m);
}

void setAudioEventCallback(AudioEventCallback cb) {
  externalCallback = cb;
}

bool initAudio() {
  // Подтяжка MISO к 3.3V - без неё линия может "плавать", и карта не
  // отвечает на первую команду (в логе: sdWait Wait Failed /
  // GO_IDLE_STATE failed). В рабочем тесте эта строка была.
  pinMode(SD_MISO_PIN, INPUT_PULLUP);

  // Порядок аргументов: SCK, MISO, MOSI, CS
  sdSPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);

  if (!SD.begin(SD_CS_PIN, sdSPI, 4000000)) {
    Serial.println("❌ SD-карта не найдена (будильник работать не будет, радио - будет)");
    sdOk = false;
  } else {
    Serial.println("✅ SD-карта найдена");
    sdOk = true;
  }

  Audio::audio_info_callback = onAudioInfo;

  audio.setPinout(I2S_BCLK_PIN, I2S_LRC_PIN, I2S_DOUT_PIN);
  audio.setVolume(15); // 0...21, стартовая громкость по умолчанию

  return sdOk;
}

void playAudioFile(const String &path) {
  if (!sdOk) {
    Serial.println("❌ Нет SD-карты - звук будильника не воспроизвести: " + path);
    return;
  }
  // connecttoFS сама останавливает то, что играло раньше (файл или поток).
  if (!audio.connecttoFS(SD, path.c_str())) {
    Serial.println("❌ Не удалось открыть/распознать файл на SD: " + path);
  } else {
    Serial.println("🔊 Играю с SD: " + path);
  }
}

bool connectToStream(const String &url) {
  bool ok = audio.connecttohost(url.c_str());
  if (!ok) {
    Serial.println("❌ Не удалось подключиться к потоку: " + url);
  }
  return ok;
}

void setAudioVolume(uint8_t volume) {
  audio.setVolume(volume); // библиотека сама ограничивает диапазоном 0..21
}

void stopAudio() {
  audio.stopSong();
}

bool isAudioPlaying() {
  return audio.isRunning();
}

void audioLoop() {
  audio.loop();
}
