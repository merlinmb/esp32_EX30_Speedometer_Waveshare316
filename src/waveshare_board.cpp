#define LGFX_USE_V1

#include "waveshare_board.h"
#include <Arduino.h>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>

static constexpr int LCD_WIDTH = 320;
static constexpr int LCD_HEIGHT = 820;
static constexpr int LCD_BACKLIGHT_PIN = 6;
static constexpr int LCD_CS_PIN = 0;
static constexpr int LCD_SCK_PIN = 2;
static constexpr int LCD_MOSI_PIN = 1;
static constexpr int LCD_RESET_PIN = 16;
static constexpr int LCD_HSYNC_PIN = 38;
static constexpr int LCD_VSYNC_PIN = 39;
static constexpr int LCD_DE_PIN = 40;
static constexpr int LCD_PCLK_PIN = 41;

WaveshareDisplay::WaveshareDisplay() {
  auto panelConfig = panel_.config();
  panelConfig.pin_cs = LCD_CS_PIN;
  panelConfig.pin_rst = LCD_RESET_PIN;
  panelConfig.memory_width = LCD_WIDTH;
  panelConfig.memory_height = LCD_HEIGHT;
  panelConfig.panel_width = LCD_WIDTH;
  panelConfig.panel_height = LCD_HEIGHT;
  panelConfig.offset_x = 0;
  panelConfig.offset_y = 0;
  panelConfig.rgb_order = true;
  panel_.config(panelConfig);

  auto detailConfig = panel_.config_detail();
  detailConfig.pin_cs = LCD_CS_PIN;
  detailConfig.pin_sclk = LCD_SCK_PIN;
  detailConfig.pin_mosi = LCD_MOSI_PIN;
  detailConfig.use_psram = 2;
  panel_.config_detail(detailConfig);

  auto busConfig = bus_.config();
  busConfig.panel = &panel_;
  busConfig.pin_d0 = 21;
  busConfig.pin_d1 = 5;
  busConfig.pin_d2 = 45;
  busConfig.pin_d3 = 48;
  busConfig.pin_d4 = 47;
  busConfig.pin_d5 = 14;
  busConfig.pin_d6 = 13;
  busConfig.pin_d7 = 12;
  busConfig.pin_d8 = 11;
  busConfig.pin_d9 = 10;
  busConfig.pin_d10 = 9;
  busConfig.pin_d11 = 17;
  busConfig.pin_d12 = 46;
  busConfig.pin_d13 = 3;
  busConfig.pin_d14 = 8;
  busConfig.pin_d15 = 18;
  busConfig.pin_henable = LCD_DE_PIN;
  busConfig.pin_vsync = LCD_VSYNC_PIN;
  busConfig.pin_hsync = LCD_HSYNC_PIN;
  busConfig.pin_pclk = LCD_PCLK_PIN;
  busConfig.freq_write = 18000000;
  busConfig.hsync_polarity = 0;
  busConfig.hsync_front_porch = 30;
  busConfig.hsync_pulse_width = 6;
  busConfig.hsync_back_porch = 30;
  busConfig.vsync_polarity = 0;
  busConfig.vsync_front_porch = 20;
  busConfig.vsync_pulse_width = 40;
  busConfig.vsync_back_porch = 20;
  busConfig.pclk_idle_high = 1;
  bus_.config(busConfig);
  panel_.setBus(&bus_);
  setPanel(&panel_);
}

WaveshareDisplay boardDisplay;

void boardBegin() {
  ledcAttach(LCD_BACKLIGHT_PIN, 50000, 8);
  boardSetBrightness(255);
  boardDisplay.init();
  boardDisplay.setRotation(1);
  boardDisplay.setColorDepth(16);
  pinMode(0, INPUT_PULLUP);
}

void boardSetBrightness(uint8_t brightness) {
  ledcWrite(LCD_BACKLIGHT_PIN, 255 - brightness);
}

bool boardButtonPressed() {
  return digitalRead(0) == LOW;
}