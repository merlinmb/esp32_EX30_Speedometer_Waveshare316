// AT6668 GPS receiver on the Waveshare UART connector.
#include "gps.h"
#include "settings.h"
#include <TinyGPSPlus.h>

GpsState gps;

static HardwareSerial& GPS_SERIAL = Serial1;
static TinyGPSPlus parser;
static constexpr int PORT_PIN_A = 44;  // GPS TX -> ESP32 RX
static constexpr int PORT_PIN_B = 43;  // ESP32 TX -> GPS RX

static uint32_t rmcCount = 0;
static uint32_t rateWindowStart = 0;
static uint32_t lastCharMs = 0;

// ----- CASIC command helpers -----
static void sendCasic(const char* body) {
  // body without '$' and '*'. Checksum = XOR of all chars between them.
  uint8_t cs = 0;
  for (const char* p = body; *p; ++p) cs ^= (uint8_t)*p;
  char buf[96];
  snprintf(buf, sizeof(buf), "$%s*%02X\r\n", body, cs);
  GPS_SERIAL.print(buf);
  GPS_SERIAL.flush();
  delay(60);
}

// Count good NMEA checksums over a short window.
static uint32_t probe(int rx, int tx, uint32_t baud, uint32_t ms) {
  GPS_SERIAL.end();
  GPS_SERIAL.setRxBufferSize(2048);
  GPS_SERIAL.begin(baud, SERIAL_8N1, rx, tx);
  TinyGPSPlus p;
  uint32_t start = millis();
  while (millis() - start < ms) {
    while (GPS_SERIAL.available()) p.encode(GPS_SERIAL.read());
    delay(2);
  }
  return p.passedChecksum();
}

void gpsSetRate(uint8_t hz) {
  // $PCAS02,<fix interval ms>
  uint16_t ms = hz >= 10 ? 100 : (hz >= 5 ? 200 : 1000);
  char body[24];
  snprintf(body, sizeof(body), "PCAS02,%u", ms);
  sendCasic(body);
}

static void configureReceiver() {
  // Only the sentences we need: GGA (sats, HDOP, altitude) and RMC (speed, course, date).
  // Keeps the UART light at 10 Hz. Field order: GGA,GLL,GSA,GSV,RMC,VTG,ZDA,ANT,DHV,LPS,,,UTC,GST
  sendCasic("PCAS03,1,0,0,0,1,0,0,0,0,0,,,0,0");
  // Dynamic platform model: automotive (ignored by firmware that lacks it)
  sendCasic("PCAS11,3");
  gpsSetRate(settings.gpsHz);
}

void gpsBegin() {
  const int pins[2][2] = {{PORT_PIN_A, PORT_PIN_B}, {PORT_PIN_B, PORT_PIN_A}};
  const uint32_t bauds[] = {115200, 9600, 38400};

  for (uint32_t baud : bauds) {
    for (auto& pr : pins) {
      if (probe(pr[0], pr[1], baud, 1300) >= 2) {
        gps.rxPin = pr[0];
        gps.txPin = pr[1];
        gps.baud = baud;
        break;
      }
    }
    if (gps.baud) break;
  }

  if (!gps.baud) {
    // Nothing heard yet (cold module or wiring issue). Default orientation; keep listening.
    gps.rxPin = PORT_PIN_A;
    gps.txPin = PORT_PIN_B;
    gps.baud = 115200;
    GPS_SERIAL.end();
    GPS_SERIAL.setRxBufferSize(2048);
    GPS_SERIAL.begin(gps.baud, SERIAL_8N1, gps.rxPin, gps.txPin);
  }

  if (gps.baud != 115200) {
    // 10 Hz needs more than 9600 baud: switch the receiver to 115200 ($PCAS01,5)
    sendCasic("PCAS01,5");
    delay(100);
    GPS_SERIAL.end();
    GPS_SERIAL.begin(115200, SERIAL_8N1, gps.rxPin, gps.txPin);
    gps.baud = 115200;
  }

  configureReceiver();
  rateWindowStart = millis();
}

bool gpsPoll() {
  bool newSample = false;
  while (GPS_SERIAL.available()) {
    char c = GPS_SERIAL.read();
    lastCharMs = millis();
    if (parser.encode(c)) {
      // A sentence completed.
      if (parser.speed.isUpdated() && parser.location.isValid() && parser.location.age() < 1500) {
        rmcCount++;
        gps.speedMs = parser.speed.mps();
        gps.lat = parser.location.lat();
        gps.lon = parser.location.lng();
        if (parser.course.isValid()) gps.courseDeg = parser.course.deg();
        gps.lastFixMs = millis();
        gps.fix = true;
        newSample = true;
      } else if (parser.speed.isUpdated()) {
        rmcCount++;  // RMC with no fix still counts towards the rate
      }
      if (parser.satellites.isUpdated()) gps.sats = parser.satellites.value();
      if (parser.hdop.isUpdated()) gps.hdop = parser.hdop.hdop();
      if (parser.altitude.isUpdated() && parser.altitude.isValid()) gps.altM = parser.altitude.meters();
      if (parser.date.isValid() && parser.time.isValid() && parser.date.year() > 2020) {
        gps.timeValid = true;
        gps.year = parser.date.year();
        gps.month = parser.date.month();
        gps.day = parser.date.day();
        gps.hour = parser.time.hour();
        gps.minute = parser.time.minute();
        gps.second = parser.time.second();
      }
    }
  }

  uint32_t now = millis();
  gps.connected = (now - lastCharMs) < 2000;
  if (gps.fix && now - gps.lastFixMs > 3000) gps.fix = false;

  if (now - rateWindowStart >= 2000) {
    gps.measuredHz = rmcCount * 1000.0f / (now - rateWindowStart);
    rmcCount = 0;
    rateWindowStart = now;
  }
  return newSample;
}
