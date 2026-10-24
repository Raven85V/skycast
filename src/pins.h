#pragma once
// Пины звука и SD. Выбор платы - флагом сборки BOARD_SKYCAST (platformio.ini).

#ifndef BL_PWM_HZ
#define BL_PWM_HZ 2000      // частота ШИМ подсветки; на Sunton проверено тестом: плавно до 2000 Гц, выше диапазон схлопывается
#endif

// Sunton ESP32-8048S070 (7"). Экран занимает 1,3-9,14-16,21,39-42,45-48; тач 19/20, подсветка 2.
// SD-слот Sunton разведён и под SPI: CS = D3 = GPIO10, MOSI = CMD = 11, SCK = 12, MISO = D0 = 13.
#define SD_CS_PIN    10
#define SD_MOSI_PIN  11
#define SD_SCK_PIN   12
#define SD_MISO_PIN  13

// I2S -> PCM5102 (пины из рабочих настроек MiniWebRadio; GPIO 0 - BCLK)
#define I2S_BCLK_PIN 0
#define I2S_LRC_PIN  18
#define I2S_DOUT_PIN 17

#define BL_DUTY_MIN  90     // ШИМ подсветки при минимальной яркости (из 255). Выше - нижние ступени ярче; ниже - темнее
#define BL_FULL_PERCENT 95  // с этой яркости (и выше) подсветка включена постоянно без ШИМ - это максимум платы


// Комнатный датчик BME280/BMP280 (I2C) - на разъёме P3, ту же шину, что и тач GT911:
// I2C общая, адреса не конфликтуют (тач 0x5D/0x14, BME/BMP 0x76/0x77, AHT10 0x38).
// Звук (I2S 0/17/18), SD (10-13), дисплей и UART при этом не затронуты.
#define SENSOR_SDA_PIN -1
#define SENSOR_SCL_PIN -1
