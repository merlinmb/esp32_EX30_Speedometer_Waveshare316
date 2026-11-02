#include "ui.h"
#include "waveshare_board.h"
#include "settings.h"
#include "fonts_vlw.h"

static constexpr int WIDTH = 820;
static constexpr int HEIGHT = 320;
static constexpr int SPEED_X = 275;
static constexpr int SPEED_Y = 150;

static lgfx::LGFX_Sprite canvas(&boardDisplay);
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
  drawText(status, 28, 26, &fSmall, color565(theme.status));
  drawText(model.design == DESIGN_C ? "INFO" : designName(model.design), 790, 26,
           &fSmall, color565(theme.info), textdatum_t::middle_right);
  canvas.drawFastHLine(28, 48, 764, color565(theme.ringTrack));

  char speed[12];
  if (model.fix) snprintf(speed, sizeof(speed), "%d", model.speed);
  else strcpy(speed, "--");
  drawText(speed, SPEED_X, SPEED_Y, &fSpeedM,
           model.fix ? color565(model.overLimit ? theme.alert : theme.speed) : color565(theme.ringTrack),
           textdatum_t::middle_center);
  drawText(model.units, SPEED_X, 230, &fLabel, color565(theme.unit), textdatum_t::middle_center);

  int meterWidth = 470;
  canvas.drawRoundRect(38, 272, meterWidth, 10, 5, color565(theme.ringTrack));
  if (model.fix && model.ringMax > 0) {
    int fill = constrain((int)(meterWidth * model.ringValue / model.ringMax), 0, meterWidth);
    if (fill) canvas.fillRoundRect(38, 272, fill, 10, 5, accent);
  }
  if (model.limit > 0) {
    char limit[28];
    snprintf(limit, sizeof(limit), "LIMIT %d %s", model.limit, model.units);
    drawText(limit, 38, 300, &fSmall, color565(theme.info));
  }
  if (model.toast[0]) drawText(model.toast, 505, 300, &fSmall, accent, textdatum_t::middle_right);

  canvas.drawFastVLine(530, 66, 228, color565(theme.ringTrack));
  drawValue(565, 86, "TRIP", model.tripVal, model.tripUnit, theme);
  drawValue(700, 86, "MAX", model.maxVal, model.units, theme);
  drawValue(565, 180, "AVERAGE", model.avgVal, model.units, theme);
  drawValue(700, 180, "ALTITUDE", model.altVal, "m", theme);
  drawText(model.heading, 565, 270, &fSmall, color565(theme.info));
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
  canvas.setColorDepth(16);
  canvas.setPsram(true);
  canvas.createSprite(WIDTH, HEIGHT);
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

void uiRender(const UiModel& model) {
  switch (model.overlay) {
    case Overlay::Settings: drawSettings(model); break;
    case Overlay::Status: drawStatus(model); break;
    case Overlay::WifiSetup: drawWifiSetup(model); break;
    case Overlay::Update: drawUpdate(model); break;
    default: drawDriving(model); break;
  }
  canvas.pushSprite(0, 0);
}