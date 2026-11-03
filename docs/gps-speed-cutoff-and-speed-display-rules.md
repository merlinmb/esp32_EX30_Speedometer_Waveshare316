---
name: gps-speed-cutoff-and-speed-display-rules
description: "GPS standstill cut-off rules (settle window, start hold, no HDOP gate) and speed-screen layout rules from the Waveshare EX30 speedometer, to port to the round M5GPSStopWatch display"
metadata:
  node_type: memory
  type: project
  originSessionId: 8379b2ae-6fdb-4dbc-b8ce-f255e3fe8d07
  modified: 2026-11-03T13:53:05.164Z
---

Rules worked out on the Waveshare 3.16 speedometer (2026-11-03). The user wants them ported to the round-display project at E:\Development\Arduino\esp32_EX30_Speedometer_M5GPSStopWatch. Both projects share the same SpeedFilter design (speed_filter.cpp).

**Speed cut-off rules** (Waveshare repo: src/speed_filter.cpp and loop() in src/main.cpp)
- Symptom: at rest, right after the fix arrived (7 sats), the device showed 3 mph. That also pushed trip max to 7 mph and average to 3 mph.
- Existing clamp: below 0.6 m/s (~1.3 mph) counts as 0. The 1.4 m/s clamp that needs an IMU never fires, because addSample is always called with imuStill = false.
- Added: speed is fed as 0 for **5 s after a fix is gained** (FIX_SETTLE_MS; fixSinceMs resets when gps.fix goes false).
- Added: to leave standstill, speed must stay **above 1.0 m/s (~2.2 mph) for 1 s continuously** (a _moving flag plus _startSinceMs). It drops back to standstill below 0.6 m/s, and reset() clears both. This costs about 1 s of lag when pulling away.
- **Do NOT gate speed on HDOP.** An HDOP <= 2.5 gate blocked every speed reading during a 7 mph run (it showed 0 the whole time). It was removed.
- Trip, max and average are computed from the filtered speed, so these gates protect them too.
- The display shows "--" instead of 0 whenever the integer speed is 0 (below the cut-off). It is dimmed when there is no fix and in the normal speed colour when stationary.
- The 5 s serial [Status] line prints HDOP and the raw speed, so the thresholds can be tuned from a log.
- Verification so far: a Python port of the filter (still noise stays 0, 3 mph blips stay 0, a 7 mph run shows 7 within ~1 s, a 3 mph walk shows 3 within ~1.5 s). The thresholds are estimates. After removing the HDOP gate, it has not been field-tested yet.

**Speed screen layout rules**
- Speed is the primary function: make the digits as large as the screen allows. On the Waveshare, the 150 px font became 300 px (digits ~218 px tall on a 320 px screen).
- Keep the status info (fix, sats, Hz, clock) out of the speed area. On the Waveshare it moved to the right-hand column above the stats.
- No divider lines or borders around the speed.
- Hide the speed/limit bar **and its outline** when no speed limit is set. Redraw that area when the limit changes.
- LovyanGFX/M5GFX copies each VLW glyph onto the stack (alloca w*h). The 300 px digit is 26.9 KB, so the loop stack went from 24 KB to 36 KB. Size the stack as the largest glyph plus ~4 KB, or the board reboots on the first digit after a fix.
- Fonts are regenerated with tools/make_vlw.py (needs freetype-py; use a scratch venv).

**How to apply:** when porting, copy the filter and loop gating as-is. Re-check the "--" logic and the glyph/stack sizing against the round screen's own fonts. Treat the thresholds as tunable from the serial log.
