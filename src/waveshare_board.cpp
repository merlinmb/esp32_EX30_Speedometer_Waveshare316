// Waveshare ESP32-S3-LCD-3.16: ST7701S 320x820 RGB panel, set up as in Waveshare's demo
// (Arduino/examples/08_LVGL_V9_Test/lvgl_port.c):
//  - ESP-IDF RGB LCD driver with bounce buffers: the LCD DMA reads from small internal-RAM
//    buffers refilled from the PSRAM frame buffer, so PSRAM load can't make the image jump.
//  - Two frame buffers: each frame is drawn off screen and swapped in at the start of a
//    refresh, so there is no tearing.

#include "waveshare_board.h"
#include <esp_lcd_panel_rgb.h>
#include <esp_lcd_panel_ops.h>
#include <freertos/semphr.h>

static constexpr int LCD_BACKLIGHT_PIN = 6;
static constexpr int LCD_CS_PIN = 0;       // shared with the BOOT key; only used during init
static constexpr int LCD_SCK_PIN = 2;
static constexpr int LCD_MOSI_PIN = 1;
static constexpr int LCD_RESET_PIN = 16;
static constexpr int LCD_HSYNC_PIN = 38;
static constexpr int LCD_VSYNC_PIN = 39;
static constexpr int LCD_DE_PIN = 40;
static constexpr int LCD_PCLK_PIN = 41;
static constexpr int BUTTON_PIN = 0;

// ST7701S init sequence from Waveshare's demo.
// Format: command, parameter count (| DELAY: a delay in ms follows the parameters), parameters.
static constexpr uint8_t DELAY = 0x80;
static const uint8_t INIT_CMDS[] = {
  0xFF, 5, 0x77, 0x01, 0x00, 0x00, 0x13,
  0xEF, 1, 0x08,
  0xFF, 5, 0x77, 0x01, 0x00, 0x00, 0x10,
  0xC0, 2, 0xE5, 0x02,
  0xC1, 2, 0x15, 0x0A,
  0xC2, 2, 0x07, 0x02,
  0xCC, 1, 0x10,
  0xB0, 16, 0x00, 0x08, 0x51, 0x0D, 0xCE, 0x06, 0x00, 0x08, 0x08, 0x24, 0x05, 0xD0, 0x0F, 0x6F, 0x36, 0x1F,
  0xB1, 16, 0x00, 0x10, 0x4F, 0x0C, 0x11, 0x05, 0x00, 0x07, 0x07, 0x18, 0x02, 0xD3, 0x11, 0x6E, 0x34, 0x1F,
  0xFF, 5, 0x77, 0x01, 0x00, 0x00, 0x11,
  0xB0, 1, 0x4D,
  0xB1, 1, 0x37,
  0xB2, 1, 0x87,
  0xB3, 1, 0x80,
  0xB5, 1, 0x4A,
  0xB7, 1, 0x85,
  0xB8, 1, 0x21,
  0xB9, 2, 0x00, 0x13,
  0xC0, 1, 0x09,
  0xC1, 1, 0x78,
  0xC2, 1, 0x78,
  0xD0, 1, 0x88,
  0xE0, 3 | DELAY, 0x80, 0x00, 0x02, 100,
  0xE1, 11, 0x0F, 0xA0, 0x00, 0x00, 0x10, 0xA0, 0x00, 0x00, 0x00, 0x60, 0x60,
  0xE2, 13, 0x30, 0x30, 0x60, 0x60, 0x45, 0xA0, 0x00, 0x00, 0x46, 0xA0, 0x00, 0x00, 0x00,
  0xE3, 4, 0x00, 0x00, 0x33, 0x33,
  0xE4, 2, 0x44, 0x44,
  0xE5, 16, 0x0F, 0x4A, 0xA0, 0xA0, 0x11, 0x4A, 0xA0, 0xA0, 0x13, 0x4A, 0xA0, 0xA0, 0x15, 0x4A, 0xA0, 0xA0,
  0xE6, 4, 0x00, 0x00, 0x33, 0x33,
  0xE7, 2, 0x44, 0x44,
  0xE8, 16, 0x10, 0x4A, 0xA0, 0xA0, 0x12, 0x4A, 0xA0, 0xA0, 0x14, 0x4A, 0xA0, 0xA0, 0x16, 0x4A, 0xA0, 0xA0,
  0xEB, 7, 0x02, 0x00, 0x4E, 0x4E, 0xEE, 0x44, 0x00,
  0xED, 16, 0xFF, 0xFF, 0x04, 0x56, 0x72, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x27, 0x65, 0x40, 0xFF, 0xFF,
  0xEF, 6, 0x08, 0x08, 0x08, 0x40, 0x3F, 0x64,
  0xFF, 5, 0x77, 0x01, 0x00, 0x00, 0x13,
  0xE8, 2, 0x00, 0x0E,
  0xFF, 5, 0x77, 0x01, 0x00, 0x00, 0x00,
  0x11, 0 | DELAY, 120,               // sleep out
  0xFF, 5, 0x77, 0x01, 0x00, 0x00, 0x13,
  0xE8, 2 | DELAY, 0x00, 0x0C, 10,
  0xE8, 2, 0x00, 0x00,
  0xFF, 5, 0x77, 0x01, 0x00, 0x00, 0x00,
  0x3A, 1, 0x55,                      // RGB565
  0x36, 1, 0x00,
  0x35, 1, 0x00,
  0x29, 0 | DELAY, 20,                // display on
};

// 3-wire SPI, 9 bits per byte: D/C bit (0 = command, 1 = parameter), then MSB first.
// Mode 0: SCL idles low, the panel samples on the rising edge.
static void spiWrite9(bool isData, uint8_t value) {
  uint16_t bits = (isData ? 0x100 : 0) | value;
  for (int i = 8; i >= 0; i--) {
    digitalWrite(LCD_MOSI_PIN, (bits >> i) & 1);
    delayMicroseconds(1);
    digitalWrite(LCD_SCK_PIN, HIGH);
    delayMicroseconds(1);
    digitalWrite(LCD_SCK_PIN, LOW);
  }
}

static void sendInitCommands() {
  pinMode(LCD_CS_PIN, OUTPUT);
  pinMode(LCD_SCK_PIN, OUTPUT);
  pinMode(LCD_MOSI_PIN, OUTPUT);
  pinMode(LCD_RESET_PIN, OUTPUT);
  digitalWrite(LCD_CS_PIN, HIGH);
  digitalWrite(LCD_SCK_PIN, LOW);

  digitalWrite(LCD_RESET_PIN, LOW);
  delay(10);
  digitalWrite(LCD_RESET_PIN, HIGH);
  delay(120);

  size_t i = 0;
  while (i < sizeof(INIT_CMDS)) {
    uint8_t cmd = INIT_CMDS[i++];
    uint8_t count = INIT_CMDS[i] & ~DELAY;
    bool hasDelay = INIT_CMDS[i++] & DELAY;
    digitalWrite(LCD_CS_PIN, LOW);
    spiWrite9(false, cmd);
    for (uint8_t n = 0; n < count; n++) spiWrite9(true, INIT_CMDS[i++]);
    digitalWrite(LCD_CS_PIN, HIGH);
    if (hasDelay) delay(INIT_CMDS[i++]);
  }
}

static esp_lcd_panel_handle_t panel = nullptr;
static uint16_t* frameBuffers[2] = {};
static int backIndex = 1;                    // frame buffer 0 is on screen at start
static SemaphoreHandle_t refreshStarted = nullptr;
static bool swapPending = false;

// Called when the driver has sent a whole frame; the next refresh starts from the newly
// presented frame buffer, so the previous one is free to draw into.
static bool IRAM_ATTR onFrameComplete(esp_lcd_panel_handle_t, const esp_lcd_rgb_panel_event_data_t*, void*) {
  BaseType_t woken = pdFALSE;
  xSemaphoreGiveFromISR(refreshStarted, &woken);
  return woken == pdTRUE;
}

void boardBegin() {
  ledcAttach(LCD_BACKLIGHT_PIN, 50000, 8);
  boardSetBrightness(0);
  sendInitCommands();

  esp_lcd_rgb_panel_config_t config = {};
  config.clk_src = LCD_CLK_SRC_DEFAULT;
  config.dma_burst_size = 64;
  config.bounce_buffer_size_px = 20 * LCD_WIDTH;   // Waveshare uses 10 lines; 20 gives more slack
  config.num_fbs = 2;
  config.data_width = 16;
  config.bits_per_pixel = 16;
  config.de_gpio_num = LCD_DE_PIN;
  config.pclk_gpio_num = LCD_PCLK_PIN;
  config.vsync_gpio_num = LCD_VSYNC_PIN;
  config.hsync_gpio_num = LCD_HSYNC_PIN;
  config.disp_gpio_num = -1;
  config.flags.fb_in_psram = 1;
  const int dataPins[16] = {21, 5, 45, 48, 47,          // B0-B4
                            14, 13, 12, 11, 10, 9,      // G0-G5
                            17, 46, 3, 8, 18};          // R0-R4
  for (int i = 0; i < 16; i++) config.data_gpio_nums[i] = dataPins[i];
  config.timings.pclk_hz = 18 * 1000 * 1000;
  config.timings.h_res = LCD_WIDTH;
  config.timings.v_res = LCD_HEIGHT;
  config.timings.hsync_back_porch = 30;
  config.timings.hsync_front_porch = 30;
  config.timings.hsync_pulse_width = 6;
  config.timings.vsync_back_porch = 20;
  config.timings.vsync_front_porch = 20;
  config.timings.vsync_pulse_width = 40;

  refreshStarted = xSemaphoreCreateBinary();
  ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&config, &panel));
  esp_lcd_rgb_panel_event_callbacks_t callbacks = {};
  callbacks.on_frame_buf_complete = onFrameComplete;
  ESP_ERROR_CHECK(esp_lcd_rgb_panel_register_event_callbacks(panel, &callbacks, nullptr));
  ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
  ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
  ESP_ERROR_CHECK(esp_lcd_rgb_panel_get_frame_buffer(panel, 2, (void**)&frameBuffers[0], (void**)&frameBuffers[1]));
  memset(frameBuffers[0], 0, LCD_WIDTH * LCD_HEIGHT * 2);
  memset(frameBuffers[1], 0, LCD_WIDTH * LCD_HEIGHT * 2);

  pinMode(BUTTON_PIN, INPUT_PULLUP);   // LCD chip select is no longer needed
  boardSetBrightness(255);
}

uint16_t* boardBackBuffer() {
  if (swapPending) {
    // Wait (at most a frame, ~20 ms) until the panel has moved on to the new frame
    xSemaphoreTake(refreshStarted, pdMS_TO_TICKS(100));
    swapPending = false;
  }
  return frameBuffers[backIndex];
}

void boardPresent() {
  // Passing one of the driver's own frame buffers switches to it at the next refresh;
  // no copy is made.
  esp_lcd_panel_draw_bitmap(panel, 0, 0, LCD_WIDTH, LCD_HEIGHT, frameBuffers[backIndex]);
  xSemaphoreTake(refreshStarted, 0);   // drop a signal from before the switch
  swapPending = true;
  backIndex ^= 1;
}

void boardSetBrightness(uint8_t brightness) {
  ledcWrite(LCD_BACKLIGHT_PIN, 255 - brightness);
}

bool boardButtonPressed() {
  return digitalRead(BUTTON_PIN) == LOW;
}
