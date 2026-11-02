#pragma once
#include <Arduino.h>

// Wi-Fi client (phone hotspot etc.), setup hotspot with captive web page,
// and the same web page on the car's network for changing colours and settings.

void netBegin();                    // starts the network task on core 0
void netStartPortal();              // open the "EX30-Speedo" setup hotspot
void netStopPortal();
bool netPortalActive();
const char* netPortalSsid();
const char* netPortalPass();
bool netConnected();
String netSsid();
String netIp();
int netRssi();

// Set by the web page when settings change; main loop applies and clears it.
extern volatile bool netSettingsChanged;
extern volatile bool netTripResetRequested;
extern volatile int netOtaProgress;   // -1 idle, 0-100 while a firmware update runs

// Live values the web page shows on its status line (written by the main loop).
struct LiveData {
  int speed = 0;
  uint8_t sats = 0;
  bool fix = false;
  float hz = 0;
  float tripDisplay = 0;
  uint8_t brightnessPct = 100;
  bool night = false;
  uint8_t screen = 0;   // 0 on, 1 dimmed, 2 off
};
extern LiveData liveData;
