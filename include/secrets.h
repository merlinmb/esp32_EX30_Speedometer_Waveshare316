// Copy this file to include/secrets.h and fill it in. secrets.h is not committed to git.
// These are only used to fill empty slots on first start (or after "forget"); after that
// the networks can be changed from the web page or the USB serial console.
#pragma once

// Network 1 - phone hotspot (used when travelling). Must match the hotspot name exactly,
// including the apostrophe type: iPhones use a curly one, e.g. "Mark\xE2\x80\x99s iPhone".
#define WIFI1_SSID "MGB"
#define WIFI1_PASS "1llusion_mb"

// Network 2 - home Wi-Fi (used for OTA updates and the settings page at home)
#define WIFI2_SSID "merlinhome"
#define WIFI2_PASS "n00n00head"

// Password for over-the-air updates (PlatformIO "ota" environment and http://ex30-speedo.local/update)
#define OTA_PASSWORD "1llusion_mb"
