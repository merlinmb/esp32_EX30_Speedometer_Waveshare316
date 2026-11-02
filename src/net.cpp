#include "net.h"
#include "settings.h"
#include "telemetry.h"
#include "web_page.h"
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <Update.h>
#include <ArduinoJson.h>

volatile bool netSettingsChanged = false;
volatile bool netTripResetRequested = false;
volatile int netOtaProgress = -1;
LiveData liveData;

static WebServer server(80);
static DNSServer dns;
static bool serverStarted = false;
static volatile bool portalWanted = false;
static bool portalActive = false;
static char portalSsid[33];
static char portalPass[16];
static volatile bool reconnectNow = false;
static SemaphoreHandle_t netLock;

// ---------- helpers ----------
static String hex6(uint32_t c) {
  char b[8];
  snprintf(b, sizeof(b), "#%06X", (unsigned)(c & 0xFFFFFF));
  return b;
}
static uint32_t parseHex(const char* s, uint32_t fallback) {
  if (!s) return fallback;
  if (*s == '#') s++;
  if (strlen(s) != 6) return fallback;
  return strtoul(s, nullptr, 16) & 0xFFFFFF;
}

static void coloursToJson(JsonObject o, const ThemeColours& c) {
  o["background"] = hex6(c.background);
  o["speed"] = hex6(c.speed);
  o["unit"] = hex6(c.unit);
  o["ringTrack"] = hex6(c.ringTrack);
  o["ringFill"] = hex6(c.ringFill);
  o["status"] = hex6(c.status);
  o["statusIcon"] = hex6(c.statusIcon);
  o["info"] = hex6(c.info);
  o["alert"] = hex6(c.alert);
  o["tickMajor"] = hex6(c.tickMajor);
  o["tickMinor"] = hex6(c.tickMinor);
  o["tickLabel"] = hex6(c.tickLabel);
  o["limitMark"] = hex6(c.limitMark);
}
static void coloursFromJson(JsonObject o, ThemeColours& c) {
  if (o.isNull()) return;
  c.background = parseHex(o["background"], c.background);
  c.speed = parseHex(o["speed"], c.speed);
  c.unit = parseHex(o["unit"], c.unit);
  c.ringTrack = parseHex(o["ringTrack"], c.ringTrack);
  c.ringFill = parseHex(o["ringFill"], c.ringFill);
  c.status = parseHex(o["status"], c.status);
  c.statusIcon = parseHex(o["statusIcon"], c.statusIcon);
  c.info = parseHex(o["info"], c.info);
  c.alert = parseHex(o["alert"], c.alert);
  c.tickMajor = parseHex(o["tickMajor"], c.tickMajor);
  c.tickMinor = parseHex(o["tickMinor"], c.tickMinor);
  c.tickLabel = parseHex(o["tickLabel"], c.tickLabel);
  c.limitMark = parseHex(o["limitMark"], c.limitMark);
}

static void sendJson(JsonDocument& d) {
  String out;
  serializeJson(d, out);
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", out);
}

static void settingsJson(JsonDocument& d) {
  d["units"] = (int)settings.units;
  d["design"] = settings.design;
  d["autoNight"] = settings.autoNight;
  d["nightBelowDeg"] = settings.nightBelowDeg;
  d["brightMode"] = (int)settings.brightMode;
  d["fixedLevel"] = settings.fixedLevel;
  d["gpsHz"] = settings.gpsHz;
  d["smoothing"] = (int)settings.smoothing;
  d["alertMode"] = (int)settings.alertMode;
  d["alertLimit"] = settings.alertLimit;
  d["idleDimMin"] = settings.idleDimMin;
  d["idleOffMin"] = settings.idleOffMin;
  d["wifiEnabled"] = settings.wifiEnabled;
  d["telemetryEnabled"] = settings.telemetryEnabled;
  d["apiUrl"] = settings.apiUrl;
  d["apiTokenSet"] = settings.apiToken[0] != 0;   // never sent back
  d["sampleSec"] = settings.sampleSec;
  d["uploadSec"] = settings.uploadSec;
  d["deviceName"] = settings.deviceName;
  d["tz"] = settings.tz;
  coloursToJson(d["day"].to<JsonObject>(), settings.day);
  coloursToJson(d["night"].to<JsonObject>(), settings.night);
  JsonArray n = d["networks"].to<JsonArray>();
  for (auto& w : settings.wifi) {
    JsonObject o = n.add<JsonObject>();
    o["ssid"] = w.ssid;
    o["passSet"] = w.pass[0] != 0;   // passwords are never sent back
  }
  d["preferHome"] = settings.preferHome;
  d["otaPassSet"] = settings.otaPass[0] != 0;
}

static void copyStr(char* dst, size_t n, const char* src) {
  if (!src) return;
  strncpy(dst, src, n - 1);
  dst[n - 1] = 0;
}

// ---------- HTTP handlers ----------
static void hRoot() { server.send_P(200, "text/html", WEB_PAGE); }

static void hGetSettings() {
  JsonDocument d;
  settingsJson(d);
  sendJson(d);
}

static void hPostSettings() {
  JsonDocument in;
  if (deserializeJson(in, server.arg("plain"))) { server.send(400, "text/plain", "bad json"); return; }
  if (in["units"].is<int>()) settings.units = (Units)constrain(in["units"].as<int>(), 0, 1);
  if (in["design"].is<int>()) settings.design = constrain(in["design"].as<int>(), 0, DESIGN_COUNT - 1);
  if (in["autoNight"].is<bool>()) settings.autoNight = in["autoNight"];
  if (in["nightBelowDeg"].is<float>()) settings.nightBelowDeg = constrain(in["nightBelowDeg"].as<float>(), -18.0f, 10.0f);
  if (in["brightMode"].is<int>()) settings.brightMode = (BrightMode)constrain(in["brightMode"].as<int>(), 0, 1);
  if (in["fixedLevel"].is<int>()) settings.fixedLevel = constrain(in["fixedLevel"].as<int>(), 0, 4);
  if (in["gpsHz"].is<int>()) { int hz = in["gpsHz"]; settings.gpsHz = hz >= 10 ? 10 : (hz >= 5 ? 5 : 1); }
  if (in["smoothing"].is<int>()) settings.smoothing = (Smoothing)constrain(in["smoothing"].as<int>(), 0, 2);
  if (in["alertMode"].is<int>()) settings.alertMode = (AlertMode)constrain(in["alertMode"].as<int>(), 0, 2);
  if (in["idleDimMin"].is<int>()) settings.idleDimMin = constrain(in["idleDimMin"].as<int>(), 0, 60);
  if (in["idleOffMin"].is<int>()) settings.idleOffMin = constrain(in["idleOffMin"].as<int>(), 0, 60);
  if (in["alertLimit"].is<int>()) settings.alertLimit = constrain(in["alertLimit"].as<int>(), 0, 200);
  if (in["wifiEnabled"].is<bool>()) settings.wifiEnabled = in["wifiEnabled"];
  if (in["preferHome"].is<bool>()) settings.preferHome = in["preferHome"];
  if (in["otaPass"].is<const char*>() && strlen(in["otaPass"]) >= 6) copyStr(settings.otaPass, sizeof(settings.otaPass), in["otaPass"]);
  if (in["telemetryEnabled"].is<bool>()) settings.telemetryEnabled = in["telemetryEnabled"];
  if (in["apiUrl"].is<const char*>()) copyStr(settings.apiUrl, sizeof(settings.apiUrl), in["apiUrl"]);
  if (in["apiToken"].is<const char*>()) copyStr(settings.apiToken, sizeof(settings.apiToken), in["apiToken"]);
  if (in["sampleSec"].is<int>()) settings.sampleSec = constrain(in["sampleSec"].as<int>(), 1, 600);
  if (in["uploadSec"].is<int>()) settings.uploadSec = constrain(in["uploadSec"].as<int>(), 2, 3600);
  if (in["deviceName"].is<const char*>() && strlen(in["deviceName"]) > 0) copyStr(settings.deviceName, sizeof(settings.deviceName), in["deviceName"]);
  if (in["tz"].is<const char*>() && strlen(in["tz"]) > 0) copyStr(settings.tz, sizeof(settings.tz), in["tz"]);
  coloursFromJson(in["day"], settings.day);
  coloursFromJson(in["night"], settings.night);
  settingsSave();
  netSettingsChanged = true;
  hGetSettings();
}

static void hPresets() {
  JsonDocument d;
  JsonArray day = d["day"].to<JsonArray>();
  ThemeColours saveDay = settings.day, saveNight = settings.night;
  uint8_t dp = settings.dayPreset, np = settings.nightPreset;
  for (int i = 0; i < dayPresetCount(); i++) {
    applyDayPreset(i);
    JsonObject o = day.add<JsonObject>();
    o["name"] = dayPresetName(i);
    coloursToJson(o["c"].to<JsonObject>(), settings.day);
  }
  JsonArray night = d["night"].to<JsonArray>();
  for (int i = 0; i < nightPresetCount(); i++) {
    applyNightPreset(i);
    JsonObject o = night.add<JsonObject>();
    o["name"] = nightPresetName(i);
    coloursToJson(o["c"].to<JsonObject>(), settings.night);
  }
  settings.day = saveDay; settings.night = saveNight;
  settings.dayPreset = dp; settings.nightPreset = np;
  sendJson(d);
}

static void hScan() {
  JsonDocument d;
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_FAILED) {
    WiFi.scanNetworks(true, false);
    d["scanning"] = true;
  } else if (n == WIFI_SCAN_RUNNING) {
    d["scanning"] = true;
  } else {
    d["scanning"] = false;
    JsonArray a = d["networks"].to<JsonArray>();
    for (int i = 0; i < n; i++) {
      String s = WiFi.SSID(i);
      if (!s.length()) continue;
      bool dup = false;
      for (JsonObject o : a) if (s == o["ssid"].as<const char*>()) { dup = true; break; }
      if (dup) continue;
      JsonObject o = a.add<JsonObject>();
      o["ssid"] = s;
      o["rssi"] = WiFi.RSSI(i);
      o["open"] = WiFi.encryptionType(i) == WIFI_AUTH_OPEN;
    }
    WiFi.scanDelete();   // next call starts a fresh scan
  }
  sendJson(d);
}

// Save a network into slot 0 (Network 1, phone hotspot) or 1 (Network 2, home).
// A blank password keeps the saved one unless "open" is true.
static void hWifiAdd() {
  JsonDocument in;
  if (deserializeJson(in, server.arg("plain")) || !in["ssid"].is<const char*>()) { server.send(400, "text/plain", "bad"); return; }
  int slot = constrain(in["slot"] | 0, 0, MAX_WIFI - 1);
  const char* ssid = in["ssid"];
  const char* pass = in["pass"] | "";
  bool open = in["open"] | false;
  if (!strlen(ssid)) { server.send(400, "text/plain", "name required"); return; }
  bool sameNet = !strcmp(settings.wifi[slot].ssid, ssid);
  copyStr(settings.wifi[slot].ssid, sizeof(settings.wifi[slot].ssid), ssid);
  if (strlen(pass) || open) copyStr(settings.wifi[slot].pass, sizeof(settings.wifi[slot].pass), pass);
  else if (!sameNet) settings.wifi[slot].pass[0] = 0;
  settings.wifiEnabled = true;
  settingsSave();
  reconnectNow = true;
  hGetSettings();
}

static void hWifiForget() {
  JsonDocument in;
  deserializeJson(in, server.arg("plain"));
  int slot = in["slot"] | -1;
  if (slot >= 0 && slot < MAX_WIFI) memset(&settings.wifi[slot], 0, sizeof(WifiCred));
  settingsSave();
  hGetSettings();
}

static void hStatus() {
  JsonDocument d;
  auto t = telemetryStats();
  d["speed"] = liveData.speed;
  d["fix"] = liveData.fix;
  d["sats"] = liveData.sats;
  d["hz"] = liveData.hz;
  d["bright"] = liveData.brightnessPct;
  d["night"] = liveData.night;
  d["screen"] = liveData.screen;
  d["wifi"] = WiFi.status() == WL_CONNECTED;
  d["ssid"] = WiFi.SSID();
  d["ip"] = WiFi.localIP().toString();
  d["rssi"] = WiFi.RSSI();
  d["queued"] = t.queued;
  d["sent"] = t.sent;
  d["http"] = t.lastHttpCode;
  sendJson(d);
}

static void hTripReset() {
  netTripResetRequested = true;
  JsonDocument d;
  d["ok"] = true;
  sendJson(d);
}

static void hNotFound() {
  // Captive portal: send any unknown host/path to the settings page.
  if (portalActive) {
    server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
    server.send(302, "text/plain", "");
  } else {
    server.send(404, "text/plain", "Not found");
  }
}

// ---------- firmware upload from the browser ----------
static const char UPDATE_PAGE[] = R"HTML(<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Firmware update</title>
<style>body{background:#0b0e11;color:#e8ecef;font:15px system-ui,sans-serif;max-width:560px;margin:0 auto;padding:20px}
button{background:#2ee6c8;color:#001a16;border:0;border-radius:10px;padding:10px 16px;font-weight:600}</style></head>
<body><h1>Firmware update</h1><p>Choose <b>.pio/build/waveshare-esp32-s3-lcd-316/firmware.bin</b> from the project folder.</p>
<form method="POST" action="/update" enctype="multipart/form-data"><input type="file" name="firmware" accept=".bin"><br><br>
<button>Upload and restart</button></form><p><a href="/" style="color:#2ee6c8">Back to settings</a></p></body></html>)HTML";

static bool otaAuth() {
  if (server.authenticate("admin", settings.otaPass)) return true;
  server.requestAuthentication(BASIC_AUTH, "EX30 Speedometer", "OTA password required (user: admin)");
  return false;
}

static void hUpdatePage() {
  if (!otaAuth()) return;
  server.send_P(200, "text/html", UPDATE_PAGE);
}

static bool uploadOk = false;

static void hUpdateDone() {
  if (!otaAuth()) return;
  bool ok = uploadOk && !Update.hasError();
  server.sendHeader("Connection", "close");
  server.send(200, "text/html", ok ? "<p style='font:16px sans-serif'>Update done, restarting...</p>"
                                   : "<p style='font:16px sans-serif'>Update failed. Check the file and try again.</p>");
  if (ok) { delay(800); ESP.restart(); }
  netOtaProgress = -1;
}

static void hUpdateUpload() {
  HTTPUpload& up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    if (!server.authenticate("admin", settings.otaPass)) return;
    uploadOk = false;
    netOtaProgress = 0;
    Update.begin(UPDATE_SIZE_UNKNOWN);
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (netOtaProgress < 0) return;
    Update.write(up.buf, up.currentSize);
    // size unknown up front: show bytes received against a typical ~1.5 MB image
    netOtaProgress = min<int>(99, (int)(Update.progress() / 15000));
  } else if (up.status == UPLOAD_FILE_END) {
    if (netOtaProgress < 0) return;
    uploadOk = Update.end(true);
    netOtaProgress = uploadOk ? 100 : -1;
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
    netOtaProgress = -1;
  }
}

// ---------- PlatformIO / ArduinoOTA ----------
static bool otaStarted = false;
static void startArduinoOta() {
  if (otaStarted) return;
  ArduinoOTA.setHostname(settings.deviceName);   // also starts mDNS: <name>.local
  ArduinoOTA.setPassword(settings.otaPass);
  ArduinoOTA.onStart([]() { netOtaProgress = 0; });
  ArduinoOTA.onProgress([](unsigned int done, unsigned int total) { netOtaProgress = total ? (int)(done * 100ULL / total) : 0; });
  ArduinoOTA.onEnd([]() { netOtaProgress = 100; });
  ArduinoOTA.onError([](ota_error_t) { netOtaProgress = -1; });
  ArduinoOTA.begin();
  MDNS.addService("http", "tcp", 80);
  otaStarted = true;
}

static void startServer() {
  if (serverStarted) return;
  server.on("/", HTTP_GET, hRoot);
  server.on("/api/settings", HTTP_GET, hGetSettings);
  server.on("/api/settings", HTTP_POST, hPostSettings);
  server.on("/api/presets", HTTP_GET, hPresets);
  server.on("/api/scan", HTTP_GET, hScan);
  server.on("/api/wifi", HTTP_POST, hWifiAdd);
  server.on("/api/wifi/forget", HTTP_POST, hWifiForget);
  server.on("/api/status", HTTP_GET, hStatus);
  server.on("/api/trip/reset", HTTP_POST, hTripReset);
  server.on("/update", HTTP_GET, hUpdatePage);
  server.on("/update", HTTP_POST, hUpdateDone, hUpdateUpload);
  // Common captive-portal probes from phones
  server.on("/generate_204", HTTP_GET, hRoot);
  server.on("/hotspot-detect.html", HTTP_GET, hRoot);
  server.on("/connecttest.txt", HTTP_GET, hRoot);
  server.onNotFound(hNotFound);
  server.begin();
  serverStarted = true;
}

// ---------- connection management ----------
// Pick Network 2 (home) if it is in range and preferred, otherwise the strongest saved one.
static bool tryConnectBest() {
  bool any = false;
  for (auto& w : settings.wifi) if (w.ssid[0]) any = true;
  if (!any) return false;

  int n = WiFi.scanNetworks(false, false);
  int rssi[MAX_WIFI];
  for (int k = 0; k < MAX_WIFI; k++) rssi[k] = -1000;
  for (int i = 0; i < n; i++) {
    String s = WiFi.SSID(i);
    for (int k = 0; k < MAX_WIFI; k++)
      if (settings.wifi[k].ssid[0] && s == settings.wifi[k].ssid && WiFi.RSSI(i) > rssi[k]) rssi[k] = WiFi.RSSI(i);
  }
  WiFi.scanDelete();

  int best = -1;
  if (settings.preferHome && rssi[1] > -1000) best = 1;
  else for (int k = 0; k < MAX_WIFI; k++) if (rssi[k] > -1000 && (best < 0 || rssi[k] > rssi[best])) best = k;
  if (best < 0) return false;

  WiFi.begin(settings.wifi[best].ssid, settings.wifi[best].pass);
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 12000) {
    if (serverStarted) server.handleClient();
    if (portalActive) dns.processNextRequest();
    vTaskDelay(pdMS_TO_TICKS(50));
  }
  return WiFi.status() == WL_CONNECTED;
}

static void doStartPortal() {
  uint32_t r = esp_random();
  snprintf(portalSsid, sizeof(portalSsid), "EX30-Speedo-%04X", (unsigned)(ESP.getEfuseMac() >> 32) & 0xFFFF);
  snprintf(portalPass, sizeof(portalPass), "%08lu", (unsigned long)(r % 100000000UL));
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(portalSsid, portalPass);
  delay(100);
  dns.start(53, "*", WiFi.softAPIP());
  portalActive = true;
  startServer();
}

static void doStopPortal() {
  dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  portalActive = false;
}

static void netTask(void*) {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(settings.deviceName);
  WiFi.setAutoReconnect(true);
  uint32_t lastAttempt = 0;

  for (;;) {
    if (portalWanted && !portalActive) doStartPortal();
    if (!portalWanted && portalActive) doStopPortal();

    bool want = settings.wifiEnabled || portalActive;
    if (!want && WiFi.status() == WL_CONNECTED) WiFi.disconnect(true);

    if (want && WiFi.status() != WL_CONNECTED) {
      // While the setup hotspot is open, only try after new details are saved, so the
      // phone on the hotspot is not dropped by a channel change.
      bool due = portalActive ? false : (lastAttempt == 0 || millis() - lastAttempt > 15000);
      if (reconnectNow || due) {
        reconnectNow = false;
        lastAttempt = millis();
        tryConnectBest();
      }
    }

    if (WiFi.status() == WL_CONNECTED) {
      startServer();      // settings page also reachable on the phone/home network
      startArduinoOta();  // PlatformIO uploads over Wi-Fi
      ArduinoOTA.handle();
    }

    if (portalActive) dns.processNextRequest();
    if (serverStarted) server.handleClient();
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

void netBegin() {
  netLock = xSemaphoreCreateMutex();
  xTaskCreatePinnedToCore(netTask, "net", 10240, nullptr, 1, nullptr, 0);
}

void netStartPortal() { portalWanted = true; }
void netStopPortal() { portalWanted = false; }
bool netPortalActive() { return portalActive; }
const char* netPortalSsid() { return portalSsid; }
const char* netPortalPass() { return portalPass; }
bool netConnected() { return WiFi.status() == WL_CONNECTED; }
String netSsid() { return WiFi.SSID(); }
String netIp() { return WiFi.localIP().toString(); }
int netRssi() { return WiFi.RSSI(); }
