// EX30 GPS speedometer for the Waveshare ESP32-S3-LCD-3.16.
// Short press changes design; hold opens settings. In settings, short press changes a
// value, hold advances to the next item, and a 3 s hold saves and exits.
// Stopped for 'Dim screen' minutes -> dims; 'Screen off' minutes later -> display off.
// Movement over ~3 mph for 3 s or the BOOT key wakes it.

#include "waveshare_board.h"
#include <time.h>
#include <sys/time.h>
#include "settings.h"
#include "gps.h"
#include "speed_filter.h"
#include "brightness.h"
#include "telemetry.h"
#include "net.h"
#include "ui.h"

static SpeedFilter filter;
static Brightness brightness;
static UiModel lastModel;
static Overlay overlay = Overlay::None;
static uint32_t overlayUntil = 0;
static char toast[40] = "";
static uint32_t toastUntil = 0;
static int menuIndex = 0;
static uint8_t lastBrightSent = 0;
static uint8_t appliedHz = 0;
static bool timeSet = false;
static bool alerted = false;
static bool tripDirty = false;

static void showToast(const char* msg, uint32_t ms = 1600) {
  strncpy(toast, msg, sizeof(toast) - 1);
  toast[sizeof(toast) - 1] = 0;
  toastUntil = millis() + ms;
}

// ---------------- time ----------------
static void applyTz() {
  setenv("TZ", settings.tz, 1);
  tzset();
}

static time_t utcFromGps() {
  struct tm t = {};
  t.tm_year = gps.year - 1900;
  t.tm_mon = gps.month - 1;
  t.tm_mday = gps.day;
  t.tm_hour = gps.hour;
  t.tm_min = gps.minute;
  t.tm_sec = gps.second;
  // mktime uses local time; work in UTC by temporarily clearing TZ
  setenv("TZ", "UTC0", 1);
  tzset();
  time_t r = mktime(&t);
  applyTz();
  return r;
}

static void syncClock(uint32_t now) {
  static uint32_t lastSync = 0;
  if (!gps.timeValid || !gps.fix) return;
  if (timeSet && now - lastSync < 60000) return;
  lastSync = now;
  time_t g = utcFromGps();
  time_t sys = time(nullptr);
  if (!timeSet || labs((long)(g - sys)) > 2) {
    struct timeval tv = {g, 0};
    settimeofday(&tv, nullptr);
    timeSet = true;
    struct tm u;
    gmtime_r(&g, &u);
  }
}

// ---------------- settings menu ----------------
struct MenuItem { const char* title; void (*value)(char*, size_t); void (*change)(); };

static void vUnits(char* b, size_t n) { snprintf(b, n, "%s", settings.units == Units::MPH ? "mph" : "km/h"); }
static void cUnits() { settings.units = settings.units == Units::MPH ? Units::KMH : Units::MPH; settings.alertLimit = 0; }
static void vDesign(char* b, size_t n) { snprintf(b, n, "%s", designName(settings.design)); }
static void cDesign() { settings.design = (settings.design + 1) % DESIGN_COUNT; }
static void vAutoNight(char* b, size_t n) { snprintf(b, n, "%s", settings.autoNight ? "On (D after dark)" : "Off"); }
static void cAutoNight() { settings.autoNight = !settings.autoNight; }
static void vDayCol(char* b, size_t n) { snprintf(b, n, "%s", dayPresetName(settings.dayPreset)); }
static void cDayCol() { applyDayPreset(settings.dayPreset + 1); }
static void vNightCol(char* b, size_t n) { snprintf(b, n, "%s", nightPresetName(settings.nightPreset)); }
static void cNightCol() { applyNightPreset(settings.nightPreset + 1); }
static void vBright(char* b, size_t n) {
  if (settings.brightMode == BrightMode::AutoSun) snprintf(b, n, "Auto by sun");
  else snprintf(b, n, "Fixed %u%%", brightStepPercent(settings.fixedLevel));
}
static void cBright() {
  if (settings.brightMode == BrightMode::AutoSun) { settings.brightMode = BrightMode::Fixed; settings.fixedLevel = 0; }
  else if (settings.fixedLevel < BRIGHT_STEPS - 1) settings.fixedLevel++;
  else settings.brightMode = BrightMode::AutoSun;
  settings.brightOffset = 0;
}
static void vHz(char* b, size_t n) { snprintf(b, n, "%u Hz", settings.gpsHz); }
static void cHz() { settings.gpsHz = settings.gpsHz == 10 ? 1 : (settings.gpsHz == 1 ? 5 : 10); }
static void vSmooth(char* b, size_t n) { const char* v[] = {"Low", "Medium", "High"}; snprintf(b, n, "%s", v[(int)settings.smoothing]); }
static void cSmooth() { settings.smoothing = (Smoothing)(((int)settings.smoothing + 1) % 3); }
static void vAlert(char* b, size_t n) { const char* v[] = {"Off", "Visual", "Visual reminder"}; snprintf(b, n, "%s", v[(int)settings.alertMode]); }
static void cAlert() { settings.alertMode = (AlertMode)(((int)settings.alertMode + 1) % 3); }
static void vLimit(char* b, size_t n) {
  if (!settings.alertLimit) snprintf(b, n, "Off");
  else snprintf(b, n, "%u %s", settings.alertLimit, settings.units == Units::MPH ? "mph" : "km/h");
}
static void cLimit() {
  int lo = settings.units == Units::MPH ? 20 : 30, hi = settings.units == Units::MPH ? 80 : 130;
  if (!settings.alertLimit) settings.alertLimit = lo;
  else if (settings.alertLimit + 10 > hi) settings.alertLimit = 0;
  else settings.alertLimit += 10;
}
static void vWifi(char* b, size_t n) {
  if (!settings.wifiEnabled) snprintf(b, n, "Off");
  else if (netConnected()) snprintf(b, n, "On, %s", netSsid().c_str());
  else snprintf(b, n, "On, not connected");
}
static void cWifi() { settings.wifiEnabled = !settings.wifiEnabled; }
static void vSetup(char* b, size_t n) { snprintf(b, n, "Short press to start"); }
static void cSetup() { netStartPortal(); overlay = Overlay::WifiSetup; }
static void vTele(char* b, size_t n) {
  if (!settings.apiUrl[0]) snprintf(b, n, "Set URL on web page");
  else snprintf(b, n, "%s", settings.telemetryEnabled ? "On" : "Off");
}
static void cTele() { settings.telemetryEnabled = !settings.telemetryEnabled; }
static void vIdleDim(char* b, size_t n) {
  if (!settings.idleDimMin) snprintf(b, n, "Never");
  else snprintf(b, n, "After %u min stopped", settings.idleDimMin);
}
static void cIdleDim() {
  static const uint8_t V[] = {0, 1, 2, 3, 5, 10, 15, 30};
  int i = 0;
  for (int k = 0; k < 8; k++) if (V[k] == settings.idleDimMin) i = k;
  settings.idleDimMin = V[(i + 1) % 8];
}
static void vIdleOff(char* b, size_t n) {
  if (!settings.idleOffMin) snprintf(b, n, "Stay dimmed");
  else snprintf(b, n, "%u min after dimming", settings.idleOffMin);
}
static void cIdleOff() {
  static const uint8_t V[] = {0, 1, 2, 5, 10, 30};
  int i = 0;
  for (int k = 0; k < 6; k++) if (V[k] == settings.idleOffMin) i = k;
  settings.idleOffMin = V[(i + 1) % 6];
}
static void resetTrip();
static void vTrip(char* b, size_t n) { snprintf(b, n, "Short press to reset"); }
static void cTrip() { resetTrip(); showToast("Trip reset"); }

static const MenuItem MENU[] = {
  {"Units", vUnits, cUnits},
  {"Design", vDesign, cDesign},
  {"Auto night", vAutoNight, cAutoNight},
  {"Day colours", vDayCol, cDayCol},
  {"Night colours", vNightCol, cNightCol},
  {"Brightness", vBright, cBright},
  {"Dim screen", vIdleDim, cIdleDim},
  {"Screen off", vIdleOff, cIdleOff},
  {"GPS updates", vHz, cHz},
  {"Smoothing", vSmooth, cSmooth},
  {"Speed alert", vAlert, cAlert},
  {"Alert limit", vLimit, cLimit},
  {"Wi-Fi", vWifi, cWifi},
  {"Wi-Fi setup", vSetup, cSetup},
  {"Send to API", vTele, cTele},
  {"Trip", vTrip, cTrip},
};
static constexpr int MENU_COUNT = sizeof(MENU) / sizeof(MENU[0]);

// ---------------- trip ----------------
static void resetTrip() {
  settings.tripMeters = 0;
  settings.tripMovingSec = 0;
  settings.tripMaxMs = 0;
  tripDirty = true;
}

static void applySettingsChanges() {
  filter.setLevel((uint8_t)settings.smoothing);
  if (appliedHz != settings.gpsHz) {
    Serial.printf("[Settings] GPS update rate: %u Hz\n", settings.gpsHz);
    gpsSetRate(settings.gpsHz);
    appliedHz = settings.gpsHz;
  }
  applyTz();
}

// ---------------- inactivity: dim, then screen off ----------------
// GPS jitter while parked never resets the timer. The BOOT key wakes a dark screen.
enum class Screen : uint8_t { On, Dimmed, Off };
static Screen screen = Screen::On;
static uint32_t lastActivityMs = 0;
static uint32_t movingSince = 0;
static float dimFactor = 1.0f;

static constexpr float MOVING_MS = 1.34f;          // ~3 mph
static constexpr uint32_t MOVING_CONFIRM_MS = 3000;

static void wakeScreen(uint32_t now) {
  lastActivityMs = now;
  if (screen == Screen::Off) {
    boardSetBrightness(lastBrightSent ? lastBrightSent : 255);
    memset(&lastModel, 0xFF, sizeof(lastModel));   // force a full redraw
  }
  screen = Screen::On;
}

static void updateIdle(uint32_t now, float speedMs, bool busy) {
  // Real movement: above the threshold continuously for a few seconds
  if (speedMs > MOVING_MS) {
    if (!movingSince) movingSince = now;
    if (now - movingSince >= MOVING_CONFIRM_MS) wakeScreen(now);
  } else {
    movingSince = 0;
  }
  if (busy || settings.idleDimMin == 0) { if (screen != Screen::On) wakeScreen(now); else lastActivityMs = now; }

  uint32_t idle = now - lastActivityMs;
  uint32_t dimAt = (uint32_t)settings.idleDimMin * 60000UL;
  uint32_t offAt = dimAt + (uint32_t)settings.idleOffMin * 60000UL;
  if (screen == Screen::On && settings.idleDimMin && idle >= dimAt) screen = Screen::Dimmed;
  if (screen == Screen::Dimmed && settings.idleOffMin && idle >= offAt) {
    screen = Screen::Off;
    boardSetBrightness(0);
  }

  // Ease the dim level so it fades rather than jumps
  static uint32_t lastMs = 0;
  float dt = lastMs ? (now - lastMs) / 1000.0f : 0;
  lastMs = now;
  float target = screen == Screen::On ? 1.0f : 0.25f;
  float k = target < dimFactor ? dt / 2.0f : dt / 0.3f;   // dim over ~2 s, brighten in ~0.3 s
  dimFactor += (target - dimFactor) * fminf(1.0f, k);
  if (fabsf(target - dimFactor) < 0.01f) dimFactor = target;
}

// Returns true when this loop's input should be ignored (it was used to wake the screen).
static bool buttonDown = false;
static bool buttonLong = false;
static bool buttonVeryLong = false;
static bool swallowButton = false;
static uint32_t buttonPressedAt = 0;

enum ButtonEvent : uint8_t { ButtonNone = 0, ButtonShort = 1, ButtonLong = 2, ButtonVeryLong = 4 };

static uint8_t pollButton(uint32_t now) {
  uint8_t event = ButtonNone;
  bool down = boardButtonPressed();
  if (down && !buttonDown) {
    buttonPressedAt = now;
    buttonLong = buttonVeryLong = false;
    if (screen != Screen::On) { wakeScreen(now); swallowButton = true; }
  }
  if (down) {
    lastActivityMs = now;
    uint32_t held = now - buttonPressedAt;
    if (!buttonLong && held >= 1000) { buttonLong = true; if (!swallowButton) event |= ButtonLong; }
    if (!buttonVeryLong && held >= 3000) { buttonVeryLong = true; if (!swallowButton) event |= ButtonVeryLong; }
  } else if (buttonDown && !buttonLong && !swallowButton) {
    event |= ButtonShort;
  }
  if (!down) swallowButton = false;
  buttonDown = down;
  return event;
}

// ---------------- design selection ----------------
// During the day the chosen design (settings.design) is shown. With Auto night on, D is
// shown after dark; stepping designs at night is a temporary choice that lasts until
// the next change between day and night.
static int8_t nightPick = -1;          // design picked while auto night is active, -1 = D
static bool lastNightState = false;

static bool autoNightActive() { return settings.autoNight && brightness.nightDesign(); }

static uint8_t currentDesign() {
  if (autoNightActive()) return nightPick >= 0 ? nightPick : DESIGN_D;
  return settings.design;
}

static void stepDesign(int dir) {
  uint8_t d = (currentDesign() + DESIGN_COUNT + dir) % DESIGN_COUNT;
  if (autoNightActive()) nightPick = d;
  else { settings.design = d; tripDirty = true; }
  char b[40];
  snprintf(b, sizeof(b), "Design %s", designName(d));
  showToast(b, 1200);
}

static void handleButton(uint8_t event) {
  if (overlay == Overlay::WifiSetup) {
    if (event & ButtonVeryLong) { netStopPortal(); overlay = Overlay::None; }
    return;
  }
  if (overlay == Overlay::Settings) {
    if (event & ButtonVeryLong) {
      settingsSave();
      applySettingsChanges();
      overlay = Overlay::None;
      showToast("Settings saved");
    } else if (event & ButtonLong) {
      menuIndex = (menuIndex + 1) % MENU_COUNT;
    } else if (event & ButtonShort) {
      MENU[menuIndex].change();
      applySettingsChanges();
    }
    return;
  }
  if (event & ButtonLong) {
    overlay = Overlay::Settings;
    menuIndex = 0;
  } else if (event & ButtonShort) {
    stepDesign(+1);
  }
}

// ---------------- USB serial console ----------------
// A fallback for entering Wi-Fi details from a PC (115200 baud, any serial monitor):
//   wifi1 "Phone hotspot name" "password"     Network 1 (phone hotspot)
//   wifi2 "Home network" "password"           Network 2 (home)
//   clear1 / clear2   status   ota "new-password"   reboot   help
static int splitArgs(char* line, char* argv[], int max) {
  int n = 0;
  char* p = line;
  while (*p && n < max) {
    while (*p == ' ' || *p == '\t') p++;
    if (!*p) break;
    if (*p == '"') {
      argv[n++] = ++p;
      while (*p && *p != '"') p++;
    } else {
      argv[n++] = p;
      while (*p && *p != ' ' && *p != '\t') p++;
    }
    if (*p) *p++ = 0;
  }
  return n;
}

static void serialConsole() {
  static char buf[200];
  static size_t len = 0;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r') continue;
    if (c != '\n') { if (len < sizeof(buf) - 1) buf[len++] = c; continue; }
    buf[len] = 0;
    len = 0;
    char* argv[4];
    int argc = splitArgs(buf, argv, 4);
    if (!argc) continue;
    String cmd = argv[0];
    cmd.toLowerCase();
    if ((cmd == "wifi1" || cmd == "wifi2") && argc >= 2) {
      int slot = cmd == "wifi1" ? 0 : 1;
      strncpy(settings.wifi[slot].ssid, argv[1], sizeof(settings.wifi[slot].ssid) - 1);
      settings.wifi[slot].ssid[sizeof(settings.wifi[slot].ssid) - 1] = 0;
      strncpy(settings.wifi[slot].pass, argc >= 3 ? argv[2] : "", sizeof(settings.wifi[slot].pass) - 1);
      settings.wifi[slot].pass[sizeof(settings.wifi[slot].pass) - 1] = 0;
      settings.wifiEnabled = true;
      settingsSave();
      Serial.printf("Network %d saved: \"%s\"\n", slot + 1, settings.wifi[slot].ssid);
    } else if (cmd == "clear1" || cmd == "clear2") {
      int slot = cmd == "clear1" ? 0 : 1;
      memset(&settings.wifi[slot], 0, sizeof(WifiCred));
      settingsSave();
      Serial.printf("Network %d cleared\n", slot + 1);
    } else if (cmd == "ota" && argc >= 2 && strlen(argv[1]) >= 6) {
      strncpy(settings.otaPass, argv[1], sizeof(settings.otaPass) - 1);
      settingsSave();
      Serial.println("OTA password saved (takes effect after reboot)");
    } else if (cmd == "status") {
      for (int i = 0; i < MAX_WIFI; i++)
        Serial.printf("Network %d: \"%s\" %s\n", i + 1, settings.wifi[i].ssid, settings.wifi[i].pass[0] ? "(password set)" : "(no password)");
      Serial.printf("Wi-Fi: %s %s %s\n", netConnected() ? "connected to" : "not connected",
                    netConnected() ? netSsid().c_str() : "", netConnected() ? netIp().c_str() : "");
      Serial.printf("Hostname: %s.local\n", settings.deviceName);
      Serial.printf("GPS: %s, %u sats, %.1f Hz, RX G%d @ %lu\n", gps.fix ? "fix" : "no fix", gps.sats, gps.measuredHz, gps.rxPin, (unsigned long)gps.baud);
    } else if (cmd == "reboot") {
      Serial.println("Restarting");
      delay(200);
      ESP.restart();
    } else {
      Serial.println("Commands: wifi1 \"name\" \"pass\" | wifi2 \"name\" \"pass\" | clear1 | clear2 | ota \"pass\" | status | reboot");
    }
  }
}

// ---------------- setup / loop ----------------
void setup() {
  Serial.begin(115200);
  Serial.println("\n[Boot] EX30 speedometer starting");
  boardBegin();
  Serial.println("[Boot] Display initialized");

  settingsLoad();
  applyTz();
  Serial.printf("[Boot] Settings loaded: %s, GPS %u Hz, Wi-Fi %s\n",
                settings.units == Units::MPH ? "mph" : "km/h", settings.gpsHz,
                settings.wifiEnabled ? "enabled" : "disabled");

  uiBegin();
  UiModel splash;
  memset(&splash, 0, sizeof(splash));
  splash.units = unitsLabel();
  strcpy(splash.clock, "--:--");
  strcpy(splash.toast, "Starting GPS...");
  uiRender(splash);

  Serial.println("[Boot] Detecting GPS receiver...");
  gpsBegin();
  Serial.printf("[Boot] GPS listening on RX GPIO%d at %lu baud\n", gps.rxPin, (unsigned long)gps.baud);
  appliedHz = settings.gpsHz;
  filter.setLevel((uint8_t)settings.smoothing);

  telemetryBegin();
  netBegin();
  Serial.println("[Boot] Network and telemetry tasks started");
  Serial.println("[Boot] Send 'status' for details; type 'help' for serial commands");
  memset(&lastModel, 0xFF, sizeof(lastModel));
  lastActivityMs = millis();
}

void loop() {
  uint32_t now = millis();

  if (gpsPoll()) {
    static uint32_t lastSampleMs = 0;
    filter.addSample(gps.speedMs, now, false);
    float dt = lastSampleMs ? (now - lastSampleMs) / 1000.0f : 0;
    lastSampleMs = now;
    float v = filter.filteredMs();
    if (dt > 0 && dt < 2.0f && v > 0.5f) {
      settings.tripMeters += v * dt;
      settings.tripMovingSec += dt;
      if (v > settings.tripMaxMs) settings.tripMaxMs = v;
      tripDirty = true;
    }
  }
  if (!gps.fix) filter.reset();
  filter.tick(now);
  syncClock(now);

  serialConsole();
  if (netSettingsChanged) { netSettingsChanged = false; applySettingsChanges(); }
  if (netTripResetRequested) { netTripResetRequested = false; resetTrip(); showToast("Trip reset"); }

  float unitsPerMs = unitsFromMs(1.0f);
  int speedInt = filter.displayInt(unitsPerMs);
  float speedUnits = filter.filteredMs() * unitsPerMs;

  static uint32_t lastDebug = 0;
  if (now - lastDebug >= 5000) {
    lastDebug = now;
    Serial.printf("[Status] GPS %s, fix %s, sats %u, %.1f Hz, speed %.1f %s | Wi-Fi %s | screen %s | clock %s\n",
                  gps.connected ? "data" : "silent", gps.fix ? "yes" : "no", gps.sats,
                  gps.measuredHz, speedUnits, unitsLabel(),
                  netConnected() ? netIp().c_str() : "disconnected",
                  screen == Screen::On ? "on" : (screen == Screen::Dimmed ? "dimmed" : "off"),
                  timeSet ? "synced" : "waiting");
  }

  bool busy = overlay == Overlay::Settings || overlay == Overlay::WifiSetup || netOtaProgress >= 0;
  updateIdle(now, filter.filteredMs(), busy);
  handleButton(pollButton(now));

  // --- brightness / design ---
  time_t utc = time(nullptr);
  float lat = gps.fix ? gps.lat : settings.lastLat;
  float lon = gps.fix ? gps.lon : settings.lastLon;
  brightness.update(utc, timeSet, lat, lon, now);
  uint8_t wantLevel = (uint8_t)max(4.0f, brightness.level() * dimFactor);
  if (screen != Screen::Off && wantLevel != lastBrightSent) {
    lastBrightSent = wantLevel;
    boardSetBrightness(lastBrightSent);
  }
  if (brightness.nightDesign() != lastNightState) { lastNightState = brightness.nightDesign(); nightPick = -1; }
  uint8_t design = currentDesign();
  bool night = design == DESIGN_D;

  // --- speed alert ---
  bool over = settings.alertLimit && gps.fix && speedInt > settings.alertLimit;
  if (over && !alerted && settings.alertMode != AlertMode::Off) {
    alerted = true;
    showToast("Speed alert limit reached", 2200);
  }
  if (!over && speedInt < (int)settings.alertLimit - 1) alerted = false;

  // --- periodic jobs ---
  static uint32_t lastSave = 0, lastSample = 0;
  if (gps.fix) { settings.lastLat = gps.lat; settings.lastLon = gps.lon; }
  if (tripDirty && now - lastSave > 120000) { lastSave = now; tripDirty = false; settingsSaveTrip(); }

  if (settings.telemetryEnabled && gps.fix && timeSet && now - lastSample >= (uint32_t)settings.sampleSec * 1000) {
    lastSample = now;
    TelemetryPoint p;
    p.utc = (uint32_t)utc;
    p.lat = gps.lat;
    p.lon = gps.lon;
    p.speedMs = filter.filteredMs();
    p.courseDeg = gps.courseDeg;
    p.altM = gps.altM;
    p.hdop = gps.hdop;
    p.sats = gps.sats;
    telemetryRecord(p);
  }

  liveData.speed = speedInt;
  liveData.sats = gps.sats;
  liveData.fix = gps.fix;
  liveData.hz = gps.measuredHz;
  liveData.brightnessPct = brightness.percent();
  liveData.night = night;
  liveData.screen = (uint8_t)screen;

  if (overlay == Overlay::Status && now > overlayUntil) overlay = Overlay::None;

  // --- build the frame ---
  if (screen == Screen::Off) { delay(20); return; }   // nothing to draw; GPS, Wi-Fi and API keep running
  static uint32_t lastFrame = 0;
  if (now - lastFrame < 33) { delay(1); return; }  // ~30 fps cap
  lastFrame = now;

  UiModel m;
  memset(&m, 0, sizeof(m));
  m.design = design;
  m.night = night;
  m.limit = settings.alertLimit;
  m.hdop = gps.hdop;
  if (filter.filteredMs() > 1.5f) {
    static const char* CARD[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    int c = (int)((gps.courseDeg + 22.5f) / 45.0f) % 8;
    snprintf(m.heading, sizeof(m.heading), "%s %03d\xC2\xB0", CARD[c], (int)lroundf(gps.courseDeg) % 360);
  } else {
    strcpy(m.heading, "--");
  }
  m.gpsConnected = gps.connected;
  m.fix = gps.fix;
  m.speed = speedInt;
  m.ringValue = roundf(filter.displayMs() * unitsPerMs * 4) / 4;   // quarter-unit steps: smooth but not every frame
  m.ringMax = settings.units == Units::MPH ? 120 : 200;
  m.overLimit = over;
  m.sats = gps.sats;
  m.hz = roundf(gps.measuredHz);
  m.brightPct = brightness.percent();
  m.units = unitsLabel();
  if (timeSet) {
    time_t lt = time(nullptr);
    struct tm tmv;
    localtime_r(&lt, &tmv);
    strftime(m.clock, sizeof(m.clock), "%H:%M", &tmv);
  } else {
    strcpy(m.clock, "--:--");
  }
  float tripU = settings.units == Units::MPH ? settings.tripMeters / 1609.344f : settings.tripMeters / 1000.0f;
  snprintf(m.trip, sizeof(m.trip), tripU < 100 ? "%.1f %s" : "%.0f %s", tripU, settings.units == Units::MPH ? "mi" : "km");
  snprintf(m.tripVal, sizeof(m.tripVal), tripU < 100 ? "%.1f" : "%.0f", tripU);
  strcpy(m.tripUnit, settings.units == Units::MPH ? "mi" : "km");
  snprintf(m.maxVal, sizeof(m.maxVal), "%.0f", settings.tripMaxMs * unitsPerMs);
  snprintf(m.avgVal, sizeof(m.avgVal), "%.0f", settings.tripMovingSec > 1 ? settings.tripMeters / settings.tripMovingSec * unitsPerMs : 0.0f);
  if (gps.fix) snprintf(m.altVal, sizeof(m.altVal), "%.0f", gps.altM); else strcpy(m.altVal, "--");
  if (now < toastUntil) {
    strncpy(m.toast, toast, sizeof(m.toast) - 1);
  }
  // Brightness nudge feedback
  static int8_t lastOffset = settings.brightOffset;
  if (settings.brightOffset != lastOffset) {
    lastOffset = settings.brightOffset;
    char b[40];
    if (settings.brightOffset == 0) snprintf(b, sizeof(b), "Brightness auto %u%%", brightness.percent());
    else snprintf(b, sizeof(b), "Brightness %u%% (%+d)", brightness.percent(), settings.brightOffset);
    showToast(b);
    strncpy(m.toast, toast, sizeof(m.toast) - 1);
  }

  m.overlay = overlay;
  if (netOtaProgress >= 0) { m.overlay = Overlay::Update; m.otaPct = netOtaProgress; }
  if (overlay == Overlay::Settings) {
    m.setIndex = menuIndex;
    m.setCount = MENU_COUNT;
    strncpy(m.setTitle, MENU[menuIndex].title, sizeof(m.setTitle) - 1);
    MENU[menuIndex].value(m.setValue, sizeof(m.setValue));
  } else if (overlay == Overlay::Status) {
    auto t = telemetryStats();
    int i = 0;
    snprintf(m.lines[i++], 48, gps.fix ? "GPS fix \xC2\xB7 %u sats \xC2\xB7 HDOP %.1f" : "GPS no fix \xC2\xB7 %u sats \xC2\xB7 HDOP %.1f", gps.sats, gps.hdop);
    snprintf(m.lines[i++], 48, "%.1f Hz \xC2\xB7 RX G%d \xC2\xB7 %lu baud", gps.measuredHz, gps.rxPin, (unsigned long)gps.baud);
    if (netConnected()) {
      String ss = netSsid();
      int slot = ss == settings.wifi[1].ssid ? 2 : 1;
      snprintf(m.lines[i++], 48, "Wi-Fi %d: %s", slot, ss.c_str());
    }
    else snprintf(m.lines[i++], 48, "Wi-Fi %s", settings.wifiEnabled ? "searching" : "off");
    if (netConnected()) snprintf(m.lines[i++], 48, "http://%s", netIp().c_str());
    snprintf(m.lines[i++], 48, "API %s \xC2\xB7 queued %lu \xC2\xB7 HTTP %d", settings.telemetryEnabled ? "on" : "off", (unsigned long)t.queued, t.lastHttpCode);
    snprintf(m.lines[i++], 48, "Sun %.1f\xC2\xB0 \xC2\xB7 %s %u%%", brightness.sunDeg(), brightStepName(brightness.step()), brightness.percent());
    float maxU = settings.tripMaxMs * unitsPerMs;
    float avgU = settings.tripMovingSec > 1 ? settings.tripMeters / settings.tripMovingSec * unitsPerMs : 0;
    snprintf(m.lines[i++], 48, "Trip %s \xC2\xB7 max %.0f \xC2\xB7 avg %.0f", m.trip, maxU, avgU);
    m.lineCount = i;
  } else if (overlay == Overlay::WifiSetup) {
    strncpy(m.apSsid, netPortalActive() ? netPortalSsid() : "Starting...", sizeof(m.apSsid) - 1);
    strncpy(m.apPass, netPortalActive() ? netPortalPass() : "", sizeof(m.apPass) - 1);
    if (netConnected()) snprintf(m.staLine, sizeof(m.staLine), "Connected to %s", netSsid().c_str());
    else snprintf(m.staLine, sizeof(m.staLine), "Not connected yet");
  }

  if (memcmp(&m, &lastModel, sizeof(m)) != 0) {
    uiRender(m);
    lastModel = m;
  }
}
