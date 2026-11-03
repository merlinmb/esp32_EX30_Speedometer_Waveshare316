#include "ui.h"
#include "waveshare_board.h"
#include "settings.h"
#include "fonts_vlw.h"
#include <LovyanGFX.hpp>

static constexpr int WIDTH = 820;
static constexpr int HEIGHT = 320;
static constexpr int SPEED_X = 260;
static constexpr int SPEED_BASE = 234;   // baseline of the speed digits (they rise 214 px above it)
static constexpr int SIDE_X = 540;       // left edge of the right-hand column

static lgfx::LGFX_Sprite canvas;
static lgfx::PointerWrapper wSpeed, wSpeedM, wLabel, wSmall, wTitle;
static lgfx::VLWfont fSpeed, fSpeedM, fLabel, fSmall, fTitle;

static uint16_t color565(uint32_t color) {
  return lgfx::color565((color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF);
}

static const ThemeColours& activeTheme(const UiModel& model) {
  return model.night ? settings.night : settings.day;
}

static void drawText(const char* text, int x, int y, const lgfx::VLWfont* font,
                     uint16_t color, textdatum_t datum = textdatum_t::middle_left) {
  canvas.setFont(font);
  canvas.setTextDatum(datum);
  canvas.setTextColor(color);
  canvas.drawString(text, x, y);
}

static void drawValue(int x, int y, const char* label, const char* value,
                      const char* unit, const ThemeColours& theme) {
  drawText(label, x, y, &fSmall, color565(theme.status));
  char text[28];
  snprintf(text, sizeof(text), "%s %s", value, unit);
  drawText(text, x, y + 34, &fTitle, color565(theme.speed));
}

static void drawDriving(const UiModel& model) {
  const ThemeColours& theme = activeTheme(model);
  const uint16_t background = color565(theme.background);
  const uint16_t accent = color565(model.overLimit ? theme.alert : theme.ringFill);
  canvas.fillScreen(background);

  char status[48];
  if (!model.gpsConnected) snprintf(status, sizeof(status), "NO GPS  |  %s", model.clock);
  else if (!model.fix) snprintf(status, sizeof(status), "SEARCHING  |  %u SATS  |  %s", model.sats, model.clock);
  else snprintf(status, sizeof(status), "GPS FIX  |  %u SATS  |  %.0f HZ  |  %s", model.sats, model.hz, model.clock);
  drawText(status, SIDE_X, 26, &fSmall, color565(theme.status));
  canvas.drawFastHLine(SIDE_X, 48, WIDTH - 20 - SIDE_X, color565(theme.ringTrack));

  char speed[12];
  if (model.fix && model.speed > 0) snprintf(speed, sizeof(speed), "%d", model.speed);
  else strcpy(speed, "--");   // no fix, or below the standstill cut-off
  drawText(speed, SPEED_X, SPEED_BASE, &fSpeedM,
           model.fix ? color565(model.overLimit ? theme.alert : theme.speed) : color565(theme.ringTrack),
           textdatum_t::baseline_center);
  drawText(model.units, SPEED_X, 258, &fLabel, color565(theme.unit), textdatum_t::middle_center);

  if (model.limit > 0) {
    int meterWidth = 470;
    canvas.drawRoundRect(25, 277, meterWidth, 10, 5, color565(theme.ringTrack));
    if (model.fix && model.ringMax > 0) {
      int fill = constrain((int)(meterWidth * model.ringValue / model.ringMax), 0, meterWidth);
      if (fill) canvas.fillRoundRect(25, 277, fill, 10, 5, accent);
    }
    char limit[28];
    snprintf(limit, sizeof(limit), "LIMIT %d %s", model.limit, model.units);
    drawText(limit, 25, 304, &fSmall, color565(theme.info));
  }
  if (model.toast[0]) drawText(model.toast, 495, 304, &fSmall, accent, textdatum_t::middle_right);

  drawValue(SIDE_X, 86, "TRIP", model.tripVal, model.tripUnit, theme);
  drawValue(676, 86, "MAX", model.maxVal, model.units, theme);
  drawValue(SIDE_X, 180, "AVERAGE", model.avgVal, model.units, theme);
  drawValue(676, 180, "ALTITUDE", model.altVal, "m", theme);
  drawText(model.heading, SIDE_X, 270, &fSmall, color565(theme.info));
  char bright[20];
  snprintf(bright, sizeof(bright), "LIGHT %u%%", model.brightPct);
  drawText(bright, 790, 270, &fSmall, color565(theme.info), textdatum_t::middle_right);
}

static void drawSettings(const UiModel& model) {
  const ThemeColours& theme = activeTheme(model);
  canvas.fillScreen(color565(theme.background));
  char header[40];
  snprintf(header, sizeof(header), "SETTINGS  %d / %d", model.setIndex + 1, model.setCount);
  drawText(header, 40, 42, &fSmall, color565(theme.status));
  drawText(model.setTitle, WIDTH / 2, 132, &fTitle, color565(theme.speed), textdatum_t::middle_center);
  drawText(model.setValue, WIDTH / 2, 194, &fLabel, color565(theme.ringFill), textdatum_t::middle_center);
  drawText("Short: change    Hold: next    Hold 3 seconds: save", WIDTH / 2, 274,
           &fSmall, color565(theme.info), textdatum_t::middle_center);
}

static void drawStatus(const UiModel& model) {
  const ThemeColours& theme = activeTheme(model);
  canvas.fillScreen(color565(theme.background));
  drawText("STATUS", 32, 30, &fLabel, color565(theme.ringFill));
  for (int i = 0; i < model.lineCount; ++i)
    drawText(model.lines[i], 36, 76 + i * 30, &fSmall, color565(theme.speed));
}

static void drawWifiSetup(const UiModel& model) {
  const ThemeColours& theme = activeTheme(model);
  canvas.fillScreen(color565(theme.background));
  drawText("WI-FI SETUP", 32, 32, &fLabel, color565(theme.ringFill));
  char qr[96];
  snprintf(qr, sizeof(qr), "WIFI:T:WPA;S:%s;P:%s;;", model.apSsid, model.apPass);
  canvas.fillRoundRect(44, 64, 190, 190, 8, TFT_WHITE);
  canvas.qrcode(qr, 54, 74, 170, 3);
  drawText(model.apSsid, 274, 102, &fTitle, color565(theme.speed));
  char password[40];
  snprintf(password, sizeof(password), "Password: %s", model.apPass);
  drawText(password, 274, 150, &fSmall, color565(theme.info));
  drawText("http://192.168.4.1", 274, 194, &fSmall, color565(theme.ringFill));
  drawText(model.staLine, 274, 236, &fSmall, color565(theme.status));
  drawText("Hold 3 seconds to close", 274, 278, &fSmall, color565(theme.info));
}

static void drawUpdate(const UiModel& model) {
  const ThemeColours& theme = activeTheme(model);
  canvas.fillScreen(color565(theme.background));
  drawText("UPDATING FIRMWARE", WIDTH / 2, 88, &fLabel,
           color565(theme.status), textdatum_t::middle_center);
  char percent[12];
  snprintf(percent, sizeof(percent), "%d%%", model.otaPct);
  drawText(percent, WIDTH / 2, 176, &fSpeed, color565(theme.speed), textdatum_t::middle_center);
  canvas.drawRoundRect(150, 238, 520, 14, 7, color565(theme.ringTrack));
  canvas.fillRoundRect(150, 238, 520 * constrain(model.otaPct, 0, 100) / 100, 14, 7,
                       color565(theme.ringFill));
  drawText("Keep power connected", WIDTH / 2, 286, &fSmall,
           color565(theme.info), textdatum_t::middle_center);
}

void uiBegin() {
  // Draws straight into the panel's frame buffers (see uiRender). They are in the panel's
  // native portrait layout, so the canvas is rotated to draw in landscape.
  canvas.setColorDepth(lgfx::color_depth_t::rgb565_nonswapped);
  wSpeed.set(font_speed, sizeof(font_speed));
  wSpeedM.set(font_speed_m, sizeof(font_speed_m));
  wLabel.set(font_label, sizeof(font_label));
  wSmall.set(font_small, sizeof(font_small));
  wTitle.set(font_title, sizeof(font_title));
  fSpeed.loadFont(&wSpeed);
  fSpeedM.loadFont(&wSpeedM);
  fLabel.loadFont(&wLabel);
  fSmall.loadFont(&wSmall);
  fTitle.loadFont(&wTitle);
}

// ---------------- partial updates ----------------
// The panel streams the frame buffer out of PSRAM continuously. Redrawing the whole frame
// on every change adds so much PSRAM traffic that the stream falls behind and the image
// shakes. Like LVGL, only the areas whose data changed are redrawn. The regions don't cut
// through any element that changes.
struct Region { int16_t x, y, w, h; };
static constexpr Region R_FULL   = {0, 0, WIDTH, HEIGHT};
static constexpr Region R_STATUS = {520, 0, WIDTH - 520, 48};     // status line (rule at y 48 is static)
static constexpr Region R_SPEED  = {36, 12, 448, 230};            // speed digits (up to 3 x 138 px wide)
static constexpr Region R_METER  = {0, 272, 520, 20};             // speed bar
static constexpr Region R_FOOTER = {0, 292, 520, 28};             // limit and toast
static constexpr Region R_VALUES = {520, 49, WIDTH - 520, 207};   // trip / max / average / altitude
static constexpr Region R_INFO   = {520, 256, WIDTH - 520, 64};   // heading and light
static constexpr int MAX_DIRTY = 6;

static UiModel shown;
static ThemeColours shownTheme;
static bool shownValid = false;
static Region lastDirty[MAX_DIRTY];
static int lastDirtyCount = 0;

static bool needsFullRedraw(const UiModel& m) {
  return !shownValid || m.overlay != Overlay::None || shown.overlay != Overlay::None ||
         m.night != shown.night || strcmp(m.units, shown.units) ||
         memcmp(&activeTheme(m), &shownTheme, sizeof(ThemeColours));
}

// Copies a region between frame buffers. With the canvas rotated 3 (USB connector on the
// right), logical (x, y) is stored at native column y, row LCD_HEIGHT - 1 - x, so each
// logical column is one native row span.
static void copyRegion(uint16_t* dst, const uint16_t* src, const Region& r) {
  int nx = r.y;
  for (int row = LCD_HEIGHT - (r.x + r.w); row < LCD_HEIGHT - r.x; row++)
    memcpy(dst + row * LCD_WIDTH + nx, src + row * LCD_WIDTH + nx, r.h * sizeof(uint16_t));
}

void uiRender(const UiModel& model) {
  uint16_t* back = boardBackBuffer();
  // The back buffer still holds the frame before last: bring the areas that changed last
  // time up to date from the frame on screen, then draw only what changed now.
  for (int i = 0; i < lastDirtyCount; i++) copyRegion(back, boardFrontBuffer(), lastDirty[i]);
  canvas.setBuffer(back, LCD_WIDTH, LCD_HEIGHT);
  canvas.setRotation(3);

  Region dirty[MAX_DIRTY];
  int n = 0;
  if (needsFullRedraw(model)) {
    switch (model.overlay) {
      case Overlay::Settings: drawSettings(model); break;
      case Overlay::Status: drawStatus(model); break;
      case Overlay::WifiSetup: drawWifiSetup(model); break;
      case Overlay::Update: drawUpdate(model); break;
      default: drawDriving(model); break;
    }
    dirty[n++] = R_FULL;
  } else {
    const UiModel& m = model;
    const UiModel& o = shown;
    auto redraw = [&](bool changed, const Region& r) {
      if (!changed) return;
      canvas.setClipRect(r.x, r.y, r.w, r.h);
      drawDriving(model);
      canvas.clearClipRect();
      dirty[n++] = r;
    };
    redraw(m.gpsConnected != o.gpsConnected || m.fix != o.fix || m.sats != o.sats ||
           m.hz != o.hz || strcmp(m.clock, o.clock), R_STATUS);
    redraw(m.fix != o.fix || m.speed != o.speed || m.overLimit != o.overLimit, R_SPEED);
    redraw(m.fix != o.fix || m.ringValue != o.ringValue || m.ringMax != o.ringMax ||
           m.overLimit != o.overLimit || m.limit != o.limit, R_METER);
    redraw(m.limit != o.limit || strcmp(m.toast, o.toast) || m.overLimit != o.overLimit, R_FOOTER);
    redraw(strcmp(m.tripVal, o.tripVal) || strcmp(m.tripUnit, o.tripUnit) ||
           strcmp(m.maxVal, o.maxVal) || strcmp(m.avgVal, o.avgVal) || strcmp(m.altVal, o.altVal),
           R_VALUES);
    redraw(strcmp(m.heading, o.heading) || m.brightPct != o.brightPct, R_INFO);
  }
  if (n) boardPresent();

  memcpy(lastDirty, dirty, sizeof(Region) * n);
  lastDirtyCount = n;
  shown = model;
  shownTheme = activeTheme(model);
  shownValid = true;
}
