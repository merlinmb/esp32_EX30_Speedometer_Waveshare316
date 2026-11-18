#pragma once
#include <Arduino.h>
#include "settings.h"

enum class Overlay : uint8_t { None, Status, Settings, WifiSetup, Update };

struct UiModel {
  uint8_t design = 0;           // DESIGN_A .. DESIGN_D
  bool night = false;           // design == D (uses the night colours)
  bool gpsConnected = false;
  bool fix = false;
  int speed = 0;                // whole number in display units
  float ringValue = 0;          // continuous value in display units, for the ring/dots
  float ringMax = 120;          // full-scale of the ring
  bool overLimit = false;
  uint8_t sats = 0;
  float hz = 0;
  uint8_t brightPct = 100;
  const char* units = "MPH";
  char clock[8] = "--:--";
  char trip[24] = "";
  char toast[40] = "";          // replaces the bottom line briefly
  int limit = 0;                // alert limit in display units, 0 = off
  // Design C extras
  float hdop = 0;
  char heading[16] = "";        // e.g. "NE 042Â°"
  char tripVal[12] = "", tripUnit[4] = "";
  char maxVal[8] = "", avgVal[8] = "", altVal[8] = "";

  Overlay overlay = Overlay::None;
  // Settings overlay
  char setTitle[32] = "";
  char setValue[40] = "";
  int setIndex = 0, setCount = 0;
  // Status overlay lines
  char lines[8][48] = {};
  int lineCount = 0;
  // Wi-Fi setup overlay
  char apSsid[33] = "";
  char apPass[16] = "";
  char staLine[48] = "";
  // Firmware update overlay
  int otaPct = 0;
  // Head-up display (settings.hud). hudRev changes whenever a HUD setting does.
  bool hud = false;
  bool testGrid = false;
  uint32_t hudRev = 0;
};

void uiBegin();
void uiRender(const UiModel& m);   // draws the whole frame and pushes it to the display
