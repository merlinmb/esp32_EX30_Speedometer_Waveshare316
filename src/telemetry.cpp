// Queues GPS points and posts them in batches to a remote HTTP(S) API whenever
// Wi-Fi is up. Points recorded while offline are kept (up to QUEUE_CAP) and sent later.
#include "telemetry.h"
#include "settings.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>

static uint32_t QUEUE_CAP = 4000;             // ~5.5 hours at one point every 5 s (PSRAM)
static constexpr uint32_t BATCH_MAX = 50;

static TelemetryPoint* ring = nullptr;
static uint32_t head = 0, count = 0;          // head = index of oldest
static SemaphoreHandle_t lock;
static TelemetryStats stats;

void telemetryRecord(const TelemetryPoint& p) {
  if (!ring) return;
  xSemaphoreTake(lock, portMAX_DELAY);
  if (count == QUEUE_CAP) {                   // full: drop the oldest
    head = (head + 1) % QUEUE_CAP;
    count--;
    stats.dropped++;
  }
  ring[(head + count) % QUEUE_CAP] = p;
  count++;
  stats.queued = count;
  xSemaphoreGive(lock);
}

TelemetryStats telemetryStats() {
  xSemaphoreTake(lock, portMAX_DELAY);
  TelemetryStats s = stats;
  xSemaphoreGive(lock);
  return s;
}

static void isoTime(uint32_t utc, char* out, size_t n) {
  time_t t = utc;
  struct tm tmv;
  gmtime_r(&t, &tmv);
  strftime(out, n, "%Y-%m-%dT%H:%M:%SZ", &tmv);
}

static int postBatch(uint32_t n) {
  JsonDocument doc;
  doc["device"] = settings.deviceName;
  JsonArray arr = doc["points"].to<JsonArray>();
  xSemaphoreTake(lock, portMAX_DELAY);
  for (uint32_t i = 0; i < n; i++) {
    const TelemetryPoint& p = ring[(head + i) % QUEUE_CAP];
    JsonObject o = arr.add<JsonObject>();
    char ts[24];
    isoTime(p.utc, ts, sizeof(ts));
    o["t"] = ts;
    o["lat"] = serialized(String(p.lat, 6));
    o["lon"] = serialized(String(p.lon, 6));
    o["speed_mph"] = serialized(String(p.speedMs * 2.236936f, 1));
    o["speed_kmh"] = serialized(String(p.speedMs * 3.6f, 1));
    o["heading"] = serialized(String(p.courseDeg, 0));
    o["alt_m"] = serialized(String(p.altM, 0));
    o["sats"] = p.sats;
    o["hdop"] = serialized(String(p.hdop, 1));
  }
  xSemaphoreGive(lock);

  String body;
  serializeJson(doc, body);

  HTTPClient http;
  bool https = strncmp(settings.apiUrl, "https://", 8) == 0;
  WiFiClientSecure secure;
  WiFiClient plain;
  bool ok;
  if (https) {
    secure.setInsecure();  // no certificate pinning; see README
    ok = http.begin(secure, settings.apiUrl);
  } else {
    ok = http.begin(plain, settings.apiUrl);
  }
  if (!ok) return -1;
  http.setTimeout(8000);
  http.addHeader("Content-Type", "application/json");
  if (settings.apiToken[0]) http.addHeader("Authorization", String("Bearer ") + settings.apiToken);
  int code = http.POST(body);
  http.end();
  return code;
}

static void uploadTask(void*) {
  uint32_t backoffMs = 0;
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(500));
    if (!settings.telemetryEnabled || !settings.apiUrl[0]) continue;
    if (WiFi.status() != WL_CONNECTED) continue;
    if (millis() - stats.lastSendMs < (uint32_t)settings.uploadSec * 1000 + backoffMs) continue;

    xSemaphoreTake(lock, portMAX_DELAY);
    uint32_t n = count < BATCH_MAX ? count : BATCH_MAX;
    xSemaphoreGive(lock);
    if (n == 0) continue;

    Serial.printf("[API] Uploading %lu telemetry points\n", (unsigned long)n);
    int code = postBatch(n);
    xSemaphoreTake(lock, portMAX_DELAY);
    stats.lastHttpCode = code;
    stats.lastSendMs = millis();
    if (code >= 200 && code < 300) {
      // Remove what was sent (new points may have been added behind it meanwhile).
      uint32_t drop = n < count ? n : count;
      head = (head + drop) % QUEUE_CAP;
      count -= drop;
      stats.sent += drop;
      backoffMs = 0;
    } else {
      backoffMs = backoffMs ? min<uint32_t>(backoffMs * 2, 120000) : 5000;
    }
    stats.queued = count;
    xSemaphoreGive(lock);
    Serial.printf("[API] Upload result HTTP %d, queue %lu, sent %lu\n",
                  code, (unsigned long)stats.queued, (unsigned long)stats.sent);
  }
}

void telemetryBegin() {
  lock = xSemaphoreCreateMutex();
  ring = (TelemetryPoint*)heap_caps_malloc(sizeof(TelemetryPoint) * QUEUE_CAP, MALLOC_CAP_SPIRAM);
  if (!ring) {
    QUEUE_CAP = 300;
    ring = (TelemetryPoint*)malloc(sizeof(TelemetryPoint) * QUEUE_CAP);
  }
  Serial.printf("[API] Telemetry queue %s (%lu points)\n", ring ? "ready" : "unavailable", (unsigned long)QUEUE_CAP);
  xTaskCreatePinnedToCore(uploadTask, "telemetry", 12288, nullptr, 1, nullptr, 0);
}
