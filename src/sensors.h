#pragma once
#include <Arduino.h>

// Комнатный датчик на I2C: BME280 (темп + влажность + давление) или BMP280 (без влажности).
// Логика как в PixelHub 75: автоопределение, поправки к показаниям. Плюс история для графиков.
enum class SensorType { NONE, BME280, BMP280 };

extern SensorType detectedSensor;
extern float homeTemp;        // °C (с поправкой)
extern float homeHumidity;    // % (с поправкой, 0..100); у BMP280 не обновляется
extern float homePressure;    // гПа (с поправкой)

bool initSensor();                 // один раз в setup(); false - датчика нет (или пины не заданы в pins.h)
void sensorLoop(uint32_t nowMs);   // вызывать из loop(): сама читает датчик и пополняет историю
bool sensorFresh();                // были ли хоть раз успешные показания

// История: точка раз в 5 минут, хранится сутки (288 точек), только в памяти.
struct HistPoint {
  uint32_t t;        // секунды с момента включения (0 - точки нет)
  int16_t  temp10;   // °C * 10
  int16_t  hum10;    // % * 10 (-1 - нет влажности)
  int16_t  pres10;   // мм рт.ст. * 10
};
#define HIST_SIZE        288
#define HIST_INTERVAL_S  300
int sensorHistCount();                    // сколько точек накоплено
const HistPoint& sensorHistAt(int i);     // i = 0 - самая старая
