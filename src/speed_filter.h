#pragma once
#include <Arduino.h>

// Turns raw GPS Doppler speed samples into a calm but honest display value.
//  - adaptive smoothing: steady speeds are smoothed, real acceleration passes through quickly
//  - standstill clamp: shows 0 when the car is still (GPS speed tiny and IMU quiet), and
//    needs a sustained speed before leaving 0
//  - integer display with hysteresis so the number does not flicker between two values
class SpeedFilter {
 public:
  void addSample(float speedMs, uint32_t nowMs, bool imuStill);  // call on every GPS speed sample
  void tick(uint32_t nowMs);                                       // call every frame
  void reset();

  float filteredMs() const { return _filtered; }   // smoothed GPS speed
  float displayMs() const { return _display; }     // frame-rate interpolated, for the ring
  int displayInt(float unitsPerMs);                 // whole number to show, with hysteresis

  void setLevel(uint8_t level) { _level = level; }  // 0 low, 1 medium, 2 high smoothing
  // Leaving standstill needs the speed above startMs for holdMs without a break.
  void setStartGate(float startMs, uint32_t holdMs) { _startMs = startMs; _startHoldMs = holdMs; }

 private:
  float _filtered = 0;
  float _display = 0;
  int _shown = 0;
  uint32_t _lastSampleMs = 0;
  uint32_t _lastTickMs = 0;
  uint8_t _level = 1;
  bool _primed = false;
  float _startMs = 1.0f;         // ~2.2 mph
  uint32_t _startHoldMs = 1000;
  bool _moving = false;          // left standstill
  uint32_t _startSinceMs = 0;    // when the speed first rose above the start threshold
};

// Tracks whether the car is stationary from the BMI270 accelerometer.
class StillDetector {
 public:
  void update(float ax, float ay, float az, uint32_t nowMs);
  bool still() const { return _still; }
  bool available = false;

 private:
  float _mean = 1.0f, _var = 0;
  bool _still = false;
  uint32_t _quietSince = 0;
};
