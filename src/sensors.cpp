#include "sensors.h"
#include "pins.h"
#include <math.h>

// Датчик станции (BME280/BMP280) из проекта удалён: показания «Дома» берутся
// только с Wi-Fi датчиков. Здесь оставлены заглушки, чтобы остальной код,
// который спрашивает состояние датчика, продолжал собираться и всегда
// получал «датчика нет».

SensorType detectedSensor = SensorType::NONE;
float homeTemp = NAN;
float homeHumidity = NAN;
float homePressure = NAN;

static HistPoint s_empty = {0, 0, -1, 0};

bool initSensor() {
  Serial.println("Датчик станции не используется (показания берутся с Wi-Fi датчиков)");
  return false;
}

void sensorLoop(uint32_t) {}

bool sensorFresh() { return false; }

int sensorHistCount() { return 0; }

const HistPoint& sensorHistAt(int) { return s_empty; }
