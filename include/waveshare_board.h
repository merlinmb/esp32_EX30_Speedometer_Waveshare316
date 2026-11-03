#pragma once

#include <Arduino.h>

// Native panel size (portrait). The UI draws rotated, in landscape.
static constexpr int LCD_WIDTH = 320;
static constexpr int LCD_HEIGHT = 820;

void boardBegin();
// Frame buffer (LCD_WIDTH x LCD_HEIGHT, RGB565) to draw the next frame into. It is not
// on screen; waits if the panel is still scanning it after the last boardPresent().
uint16_t* boardBackBuffer();
// Frame buffer currently on screen (read-only use: copying unchanged areas).
const uint16_t* boardFrontBuffer();
// Shows the back buffer from the start of the next panel refresh (no tearing).
void boardPresent();
void boardSetBrightness(uint8_t brightness);
bool boardButtonPressed();
