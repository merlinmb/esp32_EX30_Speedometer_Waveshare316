# EX30 GPS Speedometer

Firmware for the **Waveshare ESP32-S3-LCD-3.16** with an AT6668 GPS receiver. The board has a 320 x 820 ST7701 RGB display, 16 MB flash, 8 MB PSRAM, and no touch panel. The UI runs in landscape at 820 x 320.

Features include GPS speed and trip statistics, day/night brightness, configurable colours, Wi-Fi settings, OTA updates, and optional location telemetry to a user-provided API. GPS time sets the system clock; this build has no RTC or battery-status hardware.

## Build and Upload

1. Open this folder in VS Code with the PlatformIO IDE extension installed.
2. Select the `waveshare-esp32-s3-lcd-316` environment and build or upload.
3. Use the `ota` environment for network updates when the device and PC share a Wi-Fi network.
4. The serial monitor runs at 115200 baud.

The OTA upload file is `.pio/build/waveshare-esp32-s3-lcd-316/firmware.bin`. Platform and library versions are specified in `platformio.ini`.

## Hardware

Connect the GPS receiver to the board's UART connector: GPS TX to ESP32 RX GPIO44, GPS RX to ESP32 TX GPIO43, and connect power and ground as marked on the board and receiver. Firmware probes both UART directions and common receiver baud rates, then configures the AT6668 for GGA/RMC output and the selected 1, 5, or 10 Hz update rate.

The LCD uses the ST7701 RGB panel driver. Backlight brightness is controlled on GPIO6. The BOOT key on GPIO0 is configured after LCD initialization because that pin is also used during panel startup.

## Controls

- Short press BOOT on the driving screen: cycle the display selection.
- Hold BOOT for 1 second: open settings.
- In settings, short press changes the current value; hold for 1 second advances to the next item.
- Hold BOOT for 3 seconds in settings to save and exit.
- In Wi-Fi setup, hold BOOT for 3 seconds to close the setup portal.

The first press while the display is dimmed or off only wakes it. The screen dims after the configured stopped interval and can turn off later; driving or pressing BOOT wakes it. GPS, Wi-Fi, and telemetry continue while the backlight is off.

Settings include units, display selection, day/night colours, automatic or fixed brightness, screen timeout, GPS rate, smoothing, visual speed alerts, Wi-Fi, and telemetry. Speed alerts are displayed on screen; this board build has no vibration motor or speaker.

## Wi-Fi and Settings

Once connected, open `http://ex30-speedo.local` or the device's IP address. The settings page configures Wi-Fi, colours, units, GPS rate, speed alerts, API telemetry, time zone, and OTA password. Wi-Fi setup can also be started from the device settings menu.

For a USB serial console at 115200 baud, use commands such as:

```text
wifi1 "Phone hotspot" "password"
wifi2 "Home Wi-Fi" "password"
status
```

Other commands include `clear1`, `clear2`, `ota "new-password"`, and `reboot`.

## OTA and Secrets

Open `http://ex30-speedo.local/update` (user `admin`) to upload firmware in a browser, or select the `ota` PlatformIO environment. The default OTA password is `speedo1234`; change it in the settings page. The upload password and host can be overridden in `secrets.ini`.

`include/secrets.h` and `secrets.ini` are ignored by git. Example templates are provided for preloading Wi-Fi credentials and OTA settings.

## Remote API

When telemetry is enabled, GPS points are queued in PSRAM and sent to the configured endpoint. Requests use JSON and optionally include a bearer token. See `tools/test_api_server.py` and the web settings page for configuration details.

## Project Layout

```text
platformio.ini          board, flash/PSRAM settings and libraries
src/main.cpp            main loop, BOOT key, settings, trip and time
src/waveshare_board.cpp ST7701 RGB panel, backlight and button setup
src/ui_waveshare.cpp    landscape display renderer
src/gps.*               AT6668 UART detection/configuration and NMEA parsing
src/brightness.*        sun position and brightness steps
src/net.*, web_page.h   Wi-Fi, settings page and OTA updates
src/telemetry.*         queued point upload
src/settings.*          saved settings and colour presets
```

## Hardware Validation

The firmware build has been verified, but a physical display/GPS test has not yet been performed. Confirm panel orientation and colours, backlight polarity, UART GPS fix, and receiver update rate on the target board.
