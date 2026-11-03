# Display: what works, and what breaks it

The Waveshare ESP32-S3-LCD-3.16 has an ST7701S 320x820 RGB panel. It has no memory of its
own: the ESP32-S3 streams every pixel to it continuously (about 52 frames a second) out
of a frame buffer in PSRAM. Anything that stops that stream for more than about 0.2 ms
makes the panel lose sync, and the picture jumps sideways, tears or shakes.

Getting this stable took many attempts. The settings below are the ones that work.
**Don't change them without measuring** (see [Measuring](#measuring)).

## Working setup

| Setting | Value | Where |
|---|---|---|
| Driver | ESP-IDF `esp_lcd` RGB panel, not LovyanGFX `Bus_RGB` | `src/waveshare_board.cpp` |
| Bounce buffers | `10 * 320` px (10 lines) | `boardBegin()` |
| Frame buffers | `num_fbs = 2`, in PSRAM, swapped at the start of a refresh | `boardBegin()`, `boardPresent()` |
| Pixel clock | 18 MHz | `boardBegin()` |
| Timings | HSYNC 30/6/30, VSYNC 20/40/20 (front/pulse/back), default polarities | `boardBegin()` |
| Panel init | Waveshare's ST7701S command table, sent over 3-wire SPI | `INIT_CMDS` |
| Build options | `CONFIG_SPIRAM_XIP_FROM_PSRAM=y`, `CONFIG_LCD_RGB_ISR_IRAM_SAFE=y` | `custom_sdkconfig` in `platformio.ini` |
| Drawing | LovyanGFX sprite drawing straight into the back frame buffer, rotated 1, `rgb565_nonswapped` | `src/ui_waveshare.cpp` |
| Redraws | only the screen regions whose data changed | `uiRender()` |

The driver settings match Waveshare's demo (`Arduino/examples/08_LVGL_V9_Test/lvgl_port.c`)
and [thelastoutpostworkshop/ESP32-S3_3_16_ST7701_movie_player](https://github.com/thelastoutpostworkshop/ESP32-S3_3_16_ST7701_movie_player).
That project also uses 10-line bounce buffers and two frame buffers, at 16 MHz. Neither of
them uses Wi-Fi, so neither needs the build options.

## What breaks it, and why

| Change | What you see | Why |
|---|---|---|
| LovyanGFX `Bus_RGB` / `Panel_ST7701` | black screen, or jumping and tearing | Its stock ST7701 init is for 480x480 panels. It has no bounce buffers, so the LCD reads PSRAM directly, and no double buffering. |
| `setRotation(1)` on the display instead of the sprite | constant jumping | The rotated copy writes one pixel at a time down PSRAM columns, which floods PSRAM. |
| Bounce buffer that isn't 10 lines (20 lines tested) | constant fast left-right shaking, 10-20 px offset | 320x820 must be an even multiple of the bounce buffer: 262400 / (2 x 3200) = 41, but / (2 x 6400) = 20.5. The driver then resizes it to part of a line. |
| Removing the two build options | jumps every few seconds, only while Wi-Fi is connected | Flash and PSRAM share one bus on the S3. Wi-Fi code fetched from flash, plus an LCD interrupt that also runs from flash, delays the refill by 0.15-0.3 ms. A 10-line buffer only covers 0.21 ms. |
| Redrawing the whole frame on every change | jumps while values change | Each redraw writes 525 KB to PSRAM on top of the 36 MB/s the panel reads. |
| Pixel clock 12 MHz | visible flicker | That's a 36 Hz refresh. Lowering the clock doesn't fix the jumps anyway. |
| `WiFi.setSleep(false)` | no change | Modem sleep wasn't the cause. Leave Wi-Fi power saving on. |

## Building

- `custom_sdkconfig` makes PlatformIO rebuild the Arduino core from ESP-IDF. The first
  build, or any build after the build folder is deleted, takes about 7 minutes.
- Build from the IDE or PowerShell, never Git Bash. ESP-IDF's tool installer refuses MSYS
  (`MSys/Mingw is not supported`), and the build then fails with
  `Missing CMake package directory 'None'`.
- Run **one build at a time**. Two builds sharing `.pio/build` delete each other's files
  (`Could Not Find ...tmp`, `sdkconfig.h: No such file`) and can corrupt the CMake cache
  (`CMakeCCompiler.cmake: Parse error`). If that happens, delete
  `.pio/build/waveshare-esp32-s3-lcd-316` and build again.
- The rebuilt core needs these, which are kept in the repo:
  - `default_16MB.csv`: copy of the Arduino core's table. The rebuilt core looks for it in
    the project folder.
  - `custom_component_remove = espressif/esp-modbus`: it fails a static assertion in the
    rebuilt core and isn't used.
  - Generated and committed: `sdkconfig.defaults`, `sdkconfig.waveshare-esp32-s3-lcd-316`,
    `dependencies.lock`. `managed_components/` is downloaded and ignored by git.
- If a build fails with `FileExistsError ... managed_components/...`, delete
  `managed_components/` and build again.

## Measuring

Judging by eye is unreliable and slow. To check a display change, time the panel's
frames: at 18 MHz each one takes (320 + 66) x (820 + 80) / 18 MHz = 19.3 ms. A held-up
refill shows as one frame 150-300 us late followed by one equally early, and that's a
visible jump. Add this temporarily to `src/waveshare_board.cpp`:

```cpp
static volatile int64_t lastFrameUs = 0;
static volatile uint32_t badPeriodUs = 0;
static constexpr uint32_t FRAME_US = 19300;   // recompute if pclk or porches change

// at the top of onFrameComplete():
int64_t now = esp_timer_get_time();
if (lastFrameUs) {
  uint32_t period = (uint32_t)(now - lastFrameUs);
  if ((period > FRAME_US + 150 || period + 150 < FRAME_US) && !badPeriodUs) badPeriodUs = period;
}
lastFrameUs = now;
```

Then print `badPeriodUs` from `loop()` and clear it. Leave the board idle for 90 s with
Wi-Fi connected and count the reports.

| Build | Off-timing frames |
|---|---|
| Precompiled core, 18 MHz | about 10 in 75 s |
| Precompiled core, 16 MHz | about 8 in 75 s |
| Build options on, 18 MHz (current) | 0 in 90 s |
