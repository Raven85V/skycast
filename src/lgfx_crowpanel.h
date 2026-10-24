#pragma once
// Конфигурация дисплея. Выбор платы - флагом сборки (см. platformio.ini):
//   по умолчанию        - Elecrow CrowPanel 5.0" (из рабочего тестового кода)
//   -DBOARD_SKYCAST    - Sunton ESP32-8048S070, 7" 800x480 (ESP32-S3 N16R8, GT911);
//                         пины и тайминги взяты из рабочих настроек MiniWebRadio
#include <Arduino.h>
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>

#define PIN_SDA 19
#define PIN_SCL 20
#define PIN_BL   2        // подсветка (на обеих платах GPIO 2)

#ifdef BOARD_SKYCAST
  #define PIN_TOUCH_RST 38        // сброс GT911 на Sunton (в MiniWebRadio этот же пин значится как IR)
  #define TOUCH_I2C_ADDR 0x5D     // если тач молчит - попробуйте 0x14
  #ifndef LCD_PCLK_HZ
  #define LCD_PCLK_HZ   8600000   // как в MiniWebRadio; можно задать флагом -DLCD_PCLK_HZ=12000000
  #endif
  #define LCD_ROTATION  0         // 0 или 2 - если картинка вверх ногами
#else
  #define PIN_TOUCH_RST -1
  #define TOUCH_I2C_ADDR 0x14
  #define LCD_PCLK_HZ   15000000
  #define LCD_ROTATION  0
#endif

class LGFX : public lgfx::LGFX_Device {
public:
  lgfx::Bus_RGB     _bus;
  lgfx::Panel_RGB   _panel;
  lgfx::Touch_GT911 _touch;

  LGFX() {
    {
      auto cfg = _panel.config();
      cfg.memory_width  = 800;
      cfg.memory_height = 480;
      cfg.panel_width   = 800;
      cfg.panel_height  = 480;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      _panel.config(cfg);
    }
    {
      auto cfg = _panel.config_detail();
      cfg.use_psram = 1;
      _panel.config_detail(cfg);
    }
    {
      auto cfg = _bus.config();
      cfg.panel = &_panel;
#ifdef BOARD_SKYCAST
      // порядок: d0..d4 = B0..B4, d5..d10 = G0..G5, d11..d15 = R0..R4
      cfg.pin_d0  = GPIO_NUM_15;  // B0
      cfg.pin_d1  = GPIO_NUM_7;   // B1
      cfg.pin_d2  = GPIO_NUM_6;   // B2
      cfg.pin_d3  = GPIO_NUM_5;   // B3
      cfg.pin_d4  = GPIO_NUM_4;   // B4
      cfg.pin_d5  = GPIO_NUM_9;   // G0
      cfg.pin_d6  = GPIO_NUM_46;  // G1
      cfg.pin_d7  = GPIO_NUM_3;   // G2
      cfg.pin_d8  = GPIO_NUM_8;   // G3
      cfg.pin_d9  = GPIO_NUM_16;  // G4
      cfg.pin_d10 = GPIO_NUM_1;   // G5
      cfg.pin_d11 = GPIO_NUM_14;  // R0
      cfg.pin_d12 = GPIO_NUM_21;  // R1
      cfg.pin_d13 = GPIO_NUM_47;  // R2
      cfg.pin_d14 = GPIO_NUM_48;  // R3
      cfg.pin_d15 = GPIO_NUM_45;  // R4
      cfg.pin_henable = GPIO_NUM_41;  // DE
      cfg.pin_vsync   = GPIO_NUM_40;
      cfg.pin_hsync   = GPIO_NUM_39;
      cfg.pin_pclk    = GPIO_NUM_42;
      cfg.freq_write  = LCD_PCLK_HZ;
      cfg.hsync_polarity    = 0;
      cfg.hsync_front_porch = 10;
      cfg.hsync_pulse_width = 40;
      cfg.hsync_back_porch  = 16;
      cfg.vsync_polarity    = 0;
      cfg.vsync_front_porch = 22;
      cfg.vsync_pulse_width = 20;
      cfg.vsync_back_porch  = 10;
#else
      cfg.pin_d0  = GPIO_NUM_8;   // B0
      cfg.pin_d1  = GPIO_NUM_3;   // B1
      cfg.pin_d2  = GPIO_NUM_46;  // B2
      cfg.pin_d3  = GPIO_NUM_9;   // B3
      cfg.pin_d4  = GPIO_NUM_1;   // B4
      cfg.pin_d5  = GPIO_NUM_5;   // G0
      cfg.pin_d6  = GPIO_NUM_6;   // G1
      cfg.pin_d7  = GPIO_NUM_7;   // G2
      cfg.pin_d8  = GPIO_NUM_15;  // G3
      cfg.pin_d9  = GPIO_NUM_16;  // G4
      cfg.pin_d10 = GPIO_NUM_4;   // G5
      cfg.pin_d11 = GPIO_NUM_45;  // R0
      cfg.pin_d12 = GPIO_NUM_48;  // R1
      cfg.pin_d13 = GPIO_NUM_47;  // R2
      cfg.pin_d14 = GPIO_NUM_21;  // R3
      cfg.pin_d15 = GPIO_NUM_14;  // R4
      cfg.pin_henable = GPIO_NUM_40;
      cfg.pin_vsync   = GPIO_NUM_41;
      cfg.pin_hsync   = GPIO_NUM_39;
      cfg.pin_pclk    = GPIO_NUM_0;
      cfg.freq_write  = LCD_PCLK_HZ;
      cfg.hsync_polarity    = 0;
      cfg.hsync_front_porch = 40;
      cfg.hsync_pulse_width = 48;
      cfg.hsync_back_porch  = 40;
      cfg.vsync_polarity    = 0;
      cfg.vsync_front_porch = 1;
      cfg.vsync_pulse_width = 31;
      cfg.vsync_back_porch  = 13;
#endif
      cfg.pclk_active_neg   = 1;
      cfg.de_idle_high      = 0;
      cfg.pclk_idle_high    = 0;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _touch.config();
      cfg.x_min = 0;
      cfg.x_max = 799;
      cfg.y_min = 0;
      cfg.y_max = 479;
      cfg.pin_int = -1;
      cfg.pin_rst = PIN_TOUCH_RST;
      cfg.bus_shared = false;
      cfg.offset_rotation = 0;
      cfg.i2c_port = 1;
      cfg.i2c_addr = TOUCH_I2C_ADDR;
      cfg.pin_sda = PIN_SDA;
      cfg.pin_scl = PIN_SCL;
      cfg.freq = 400000;
      _touch.config(cfg);
      _panel.setTouch(&_touch);
    }
    setPanel(&_panel);
  }
};

extern LGFX gfx;   // определён в main.cpp
