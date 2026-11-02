#pragma once

#include <LovyanGFX.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>

class WaveshareDisplay : public lgfx::LGFX_Device {
 public:
  WaveshareDisplay();

 private:
  lgfx::Panel_ST7701 panel_;
  lgfx::Bus_RGB bus_;
};

extern WaveshareDisplay boardDisplay;

void boardBegin();
void boardSetBrightness(uint8_t brightness);
bool boardButtonPressed();