#pragma once
#include <Arduino.h>

// ---------------------------------------------------------------------------
// User settings, stored in flash (NVS) and editable on the device or from the
// web page. Colours are 0xRRGGBB.
// ---------------------------------------------------------------------------

enum class Units : uint8_t { MPH = 0, KMH = 1 };
enum class Smoothing : uint8_t { Low = 0, Medium = 1, High = 2 };
enum class AlertMode : uint8_t { Off = 0, Visual = 1, VisualReminder = 2 };
// Designs, in the order the touch zones / Key A step through them.
enum Design : uint8_t { DESIGN_A = 0, DESIGN_B1 = 1, DESIGN_B2 = 2, DESIGN_C = 3, DESIGN_D = 4, DESIGN_COUNT = 5 };
const char* designName(uint8_t d);
enum class BrightMode : uint8_t { AutoSun = 0, Fixed = 1 };

// Every colour on one screen design. "day" colours are used by A, B1, B2 and C;
// "night" colours by D.
struct ThemeColours {
  uint32_t background;
  uint32_t speed;       // big speed digits
  uint32_t unit;        // "MPH" label
  uint32_t ringTrack;   // unlit part of the ring / unlit dots
  uint32_t ringFill;    // lit part of the ring / lit dots
  uint32_t status;      // top status line (sats, rate, night level)
  uint32_t statusIcon;  // satellite / moon icon
  uint32_t info;        // bottom line (clock, trip)
  uint32_t alert;       // over-limit colour for digits and ring
  uint32_t tickMajor;   // gauge (B) major ticks
  uint32_t tickMinor;   // gauge (B) minor ticks
  uint32_t tickLabel;   // gauge (B) numbers round the dial
  uint32_t limitMark;   // gauge (B) limit marker and pill outline
};

// Slot 0 = Network 1 (phone hotspot, for travelling), slot 1 = Network 2 (home Wi-Fi)
static constexpr int MAX_WIFI = 2;

struct WifiCred {
  char ssid[33];
  char pass[65];
};

struct Settings {
  Units units = Units::MPH;
  uint8_t gpsHz = 10;               // 1, 5 or 10
  Smoothing smoothing = Smoothing::Medium;
  AlertMode alertMode = AlertMode::Visual;
  uint8_t alertLimit = 0;           // in display units, 0 = off
  BrightMode brightMode = BrightMode::AutoSun;
  uint8_t fixedLevel = 0;           // index into brightness steps (0 = brightest)
  int8_t brightOffset = 0;          // user nudge, steps
  uint8_t idleDimMin = 3;           // stationary this many minutes -> dim (0 = never)
  uint8_t idleOffMin = 1;           // then this many more minutes -> screen off (0 = stay dimmed)
  uint8_t design = DESIGN_A;        // chosen design for daytime
  bool autoNight = true;            // switch to D automatically when the sun is low
  float nightBelowDeg = -3.0f;      // sun elevation below which autoNight switches to D
  uint8_t dayPreset = 0;            // last preset picked on the device (informational)
  uint8_t nightPreset = 0;
  ThemeColours day;
  ThemeColours night;

  // Wi-Fi and telemetry
  bool wifiEnabled = true;
  WifiCred wifi[MAX_WIFI] = {};
  bool preferHome = true;           // when both are in range, use Network 2 (home)
  char otaPass[32] = "";            // password for over-the-air updates (PlatformIO and web upload)
  bool telemetryEnabled = false;
  char apiUrl[160] = "";
  char apiToken[96] = "";
  uint16_t sampleSec = 5;           // how often a point is recorded
  uint16_t uploadSec = 10;          // how often queued points are sent
  char tz[48] = "GMT0BST,M3.5.0/1,M10.5.0";  // UK time incl. BST
  char deviceName[32] = "";

  // Remembered between drives
  float lastLat = 51.40f;           // used for sun position before the first fix
  float lastLon = -1.32f;
  float tripMeters = 0;
  float tripMovingSec = 0;
  float tripMaxMs = 0;
};

extern Settings settings;

void settingsDefaults(Settings& s);
void settingsLoad();
void settingsSave();
void settingsSaveTrip();          // light-weight save of trip + last position

// Colour presets selectable on the device. The web page can set any colour.
int dayPresetCount();
int nightPresetCount();
const char* dayPresetName(int i);
const char* nightPresetName(int i);
void applyDayPreset(int i);
void applyNightPreset(int i);

float unitsFromMs(float ms);      // m/s -> display units
const char* unitsLabel();
