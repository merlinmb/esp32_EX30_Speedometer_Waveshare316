#include "settings.h"
#include <Preferences.h>

// Optional: include/secrets.h (not committed) can pre-load the two networks and the
// OTA password. Copy include/secrets_example.h to include/secrets.h and edit it.
#if __has_include("secrets.h")
#include "secrets.h"
#endif
#ifndef WIFI1_SSID
#define WIFI1_SSID ""
#define WIFI1_PASS ""
#endif
#ifndef WIFI2_SSID
#define WIFI2_SSID ""
#define WIFI2_PASS ""
#endif
#ifndef OTA_PASSWORD
#define OTA_PASSWORD "speedo1234"
#endif

Settings settings;

static constexpr uint32_t SETTINGS_MAGIC = 0x53504431;  // "SPD1"
static constexpr uint16_t SETTINGS_VERSION = 5;
static constexpr size_t SETTINGS_V4_SIZE = offsetof(Settings, fixSettleSec);  // v5 only appended fields

struct DayPreset { const char* name; ThemeColours c; };

// Day designs (A, B, C). Order: background, speed, unit, ringTrack, ringFill, status, statusIcon, info, alert,
// tickMajor, tickMinor, tickLabel, limitMark
static const DayPreset DAY_PRESETS[] = {
  {"Teal",   {0x000000, 0xFFFFFF, 0x9AA3AD, 0x1C2229, 0x2EE6C8, 0x9AA3AD, 0x2EE6C8, 0x7C858F, 0xFF5A4E, 0xC9D0D6, 0x5A636C, 0x8E979F, 0xFFB020}},
  {"Ice",    {0x000000, 0xFFFFFF, 0x9AB0C4, 0x18212B, 0x5CC8FF, 0x9AB0C4, 0x5CC8FF, 0x7A8A99, 0xFF5A4E, 0xC4D2DE, 0x52606E, 0x8C9CAB, 0xFFB020}},
  {"Lime",   {0x000000, 0xFFFFFF, 0xA0A89A, 0x1E2419, 0xB6F23A, 0xA0A89A, 0xB6F23A, 0x80887A, 0xFF5A4E, 0xD0D6C9, 0x5E6656, 0x969E8E, 0xFFB020}},
  {"Orange", {0x000000, 0xFFFFFF, 0xADA39A, 0x2A2019, 0xFF8A2A, 0xADA39A, 0xFF8A2A, 0x8F857C, 0xFF4040, 0xD6CFC9, 0x665E58, 0x9E968F, 0xFFD24A}},
  {"Volvo",  {0x000000, 0xF4F6F8, 0x9EA7B3, 0x1A2030, 0x4C8DF2, 0x9EA7B3, 0x4C8DF2, 0x7C8592, 0xFF5A4E, 0xC9D0DA, 0x566070, 0x8E98A6, 0xFFB020}},
  {"Mono",   {0x000000, 0xFFFFFF, 0x9A9A9A, 0x222222, 0xE6E6E6, 0x9A9A9A, 0xE6E6E6, 0x7A7A7A, 0xFF5A4E, 0xCFCFCF, 0x5C5C5C, 0x939393, 0xFFB020}},
};

// Design D (night): warm, low-blue colours.
static const DayPreset NIGHT_PRESETS[] = {
  {"Amber",  {0x000000, 0xE8901C, 0x9A6418, 0x2A1E10, 0xE8901C, 0x8A5A14, 0x8A5A14, 0x6E4A14, 0xFF3B30, 0x9A6418, 0x4A3410, 0x7A5214, 0xFF3B30}},
  {"Red",    {0x000000, 0xD8352A, 0x8A2A22, 0x2A0E0C, 0xD8352A, 0x7A2620, 0x7A2620, 0x5E1E1A, 0xFF6A3D, 0x8A2A22, 0x4A1612, 0x6E221C, 0xFF6A3D}},
  {"Green",  {0x000000, 0x5FD068, 0x3E8A44, 0x0F2412, 0x5FD068, 0x357A3A, 0x357A3A, 0x2A5E2E, 0xFF5A4E, 0x3E8A44, 0x1E4A22, 0x357A3A, 0xFFB020}},
  {"Dim white", {0x000000, 0xB8B8B8, 0x6E6E6E, 0x1A1A1A, 0x9A9A9A, 0x5E5E5E, 0x5E5E5E, 0x4A4A4A, 0xE04A3F, 0x7A7A7A, 0x3A3A3A, 0x5E5E5E, 0xC88A2A}},
};

int dayPresetCount() { return sizeof(DAY_PRESETS) / sizeof(DAY_PRESETS[0]); }
int nightPresetCount() { return sizeof(NIGHT_PRESETS) / sizeof(NIGHT_PRESETS[0]); }
const char* dayPresetName(int i) { return DAY_PRESETS[i % dayPresetCount()].name; }
const char* nightPresetName(int i) { return NIGHT_PRESETS[i % nightPresetCount()].name; }
void applyDayPreset(int i) { settings.dayPreset = i % dayPresetCount(); settings.day = DAY_PRESETS[settings.dayPreset].c; }
void applyNightPreset(int i) { settings.nightPreset = i % nightPresetCount(); settings.night = NIGHT_PRESETS[settings.nightPreset].c; }

void settingsDefaults(Settings& s) {
  s = Settings();
  s.day = DAY_PRESETS[0].c;
  s.night = NIGHT_PRESETS[0].c;
  snprintf(s.deviceName, sizeof(s.deviceName), "ex30-speedo");
  strncpy(s.otaPass, OTA_PASSWORD, sizeof(s.otaPass) - 1);
}

// Fill empty network slots from secrets.h, if it exists.
static void seedFromSecrets() {
  if (!settings.wifi[0].ssid[0] && strlen(WIFI1_SSID)) {
    strncpy(settings.wifi[0].ssid, WIFI1_SSID, sizeof(settings.wifi[0].ssid) - 1);
    strncpy(settings.wifi[0].pass, WIFI1_PASS, sizeof(settings.wifi[0].pass) - 1);
  }
  if (!settings.wifi[1].ssid[0] && strlen(WIFI2_SSID)) {
    strncpy(settings.wifi[1].ssid, WIFI2_SSID, sizeof(settings.wifi[1].ssid) - 1);
    strncpy(settings.wifi[1].pass, WIFI2_PASS, sizeof(settings.wifi[1].pass) - 1);
  }
}

void settingsLoad() {
  settingsDefaults(settings);
  Preferences p;
  if (!p.begin("speedo", true)) return;
  uint32_t magic = p.getUInt("magic", 0);
  uint16_t ver = p.getUShort("ver", 0);
  size_t len = p.getBytesLength("cfg");
  if (magic == SETTINGS_MAGIC && ver == SETTINGS_VERSION && len == sizeof(Settings)) {
    p.getBytes("cfg", &settings, sizeof(Settings));
  } else if (magic == SETTINGS_MAGIC && ver == 4 && len == SETTINGS_V4_SIZE) {
    // Version 4: everything up to the trip; the newer fields keep their defaults
    p.getBytes("cfg", &settings, len);
  }
  p.end();
  // Sanity checks in case of a partly written blob
  if (settings.design >= DESIGN_COUNT) settings.design = DESIGN_A;
  if (settings.gpsHz != 1 && settings.gpsHz != 5 && settings.gpsHz != 10) settings.gpsHz = 10;
  if (settings.idleDimMin > 60) settings.idleDimMin = 3;
  if (settings.idleOffMin > 60) settings.idleOffMin = 1;
  if (settings.sampleSec < 1) settings.sampleSec = 5;
  if (settings.uploadSec < 2) settings.uploadSec = 10;
  if (settings.fixSettleSec > 30) settings.fixSettleSec = 5;
  if (!(settings.startSpeedMs >= 0.6f && settings.startSpeedMs <= 3.0f)) settings.startSpeedMs = 1.0f;
  if (settings.startHoldMs > 5000) settings.startHoldMs = 1000;
  if (settings.rotation != 0 && settings.rotation != 2) settings.rotation = 0;
  if (settings.speedOffset < -10 || settings.speedOffset > 10) settings.speedOffset = 0;
  hudSanitise(settings.hud);
  settings.apiUrl[sizeof(settings.apiUrl) - 1] = 0;
  settings.apiToken[sizeof(settings.apiToken) - 1] = 0;
  settings.tz[sizeof(settings.tz) - 1] = 0;
  if (!settings.deviceName[0]) snprintf(settings.deviceName, sizeof(settings.deviceName), "ex30-speedo");
  settings.otaPass[sizeof(settings.otaPass) - 1] = 0;
  if (!settings.otaPass[0]) strncpy(settings.otaPass, OTA_PASSWORD, sizeof(settings.otaPass) - 1);
  for (auto& w : settings.wifi) { w.ssid[sizeof(w.ssid) - 1] = 0; w.pass[sizeof(w.pass) - 1] = 0; }
  seedFromSecrets();
}

static float clampf(float v, float lo, float hi, float fallback) {
  if (!(v >= lo && v <= hi)) return isnan(v) ? fallback : (v < lo ? lo : hi);
  return v;
}

void hudSanitise(HudSettings& h) {
  if ((uint8_t)h.gauge > 2) h.gauge = HudGauge::Bar;
  if (h.digitSize > 1) h.digitSize = 1;
  if (h.gaugeMax > 300) h.gaugeMax = 0;
  h.colour &= 0xFFFFFF;
  h.nightColour &= 0xFFFFFF;
  h.alertColour &= 0xFFFFFF;
  h.scaleX = clampf(h.scaleX, 0.5f, 1.5f, 1.0f);
  h.scaleY = clampf(h.scaleY, 0.5f, 1.5f, 1.0f);
  h.keystone = clampf(h.keystone, -0.5f, 0.5f, 0.0f);
  h.taper = clampf(h.taper, -0.5f, 0.5f, 0.0f);
  h.bow = clampf(h.bow, -0.5f, 0.5f, 0.0f);
  h.skew = clampf(h.skew, -0.5f, 0.5f, 0.0f);
  h.rotateDeg = clampf(h.rotateDeg, -15.0f, 15.0f, 0.0f);
  h.offsetX = constrain(h.offsetX, -300, 300);
  h.offsetY = constrain(h.offsetY, -150, 150);
}

void settingsSave() {
  Preferences p;
  if (!p.begin("speedo", false)) return;
  p.putUInt("magic", SETTINGS_MAGIC);
  p.putUShort("ver", SETTINGS_VERSION);
  p.putBytes("cfg", &settings, sizeof(Settings));
  p.end();
}

void settingsSaveTrip() { settingsSave(); }

const char* designName(uint8_t d) {
  static const char* N[DESIGN_COUNT] = {"A Clean", "B Gauge", "B Gauge solid", "C Info", "D Night"};
  return N[d < DESIGN_COUNT ? d : 0];
}

float unitsFromMs(float ms) { return settings.units == Units::MPH ? ms * 2.236936f : ms * 3.6f; }
const char* unitsLabel() { return settings.units == Units::MPH ? "MPH" : "KM/H"; }
