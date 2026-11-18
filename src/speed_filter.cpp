#include "speed_filter.h"
#include <math.h>

static constexpr float STOP_THRESHOLD_MS = 0.6f;      // ~1.3 mph: below this treat as stopped
static constexpr float STOP_IMU_THRESHOLD_MS = 1.4f;  // ~3 mph: below this AND IMU still -> 0
static constexpr float HYSTERESIS = 0.60f;            // how far past a whole number before changing

void SpeedFilter::reset() {
  _filtered = _display = 0;
  _shown = 0;
  _primed = false;
  _moving = false;
  _startSinceMs = 0;
}

void SpeedFilter::addSample(float v, uint32_t now, bool imuStill) {
  if (v < 0 || isnan(v)) v = 0;
  if (v < STOP_THRESHOLD_MS || (imuStill && v < STOP_IMU_THRESHOLD_MS)) v = 0;
  // At rest, GPS speed noise comes in short bursts of 1-3 mph. Only leave standstill once
  // the speed has stayed above the start threshold for a while.
  if (!_moving) {
    if (v < _startMs) _startSinceMs = 0;
    else if (!_startSinceMs) _startSinceMs = now;
    if (_startSinceMs && now - _startSinceMs >= _startHoldMs) _moving = true;
    else v = 0;
  } else if (v == 0) {
    _moving = false;
  }

  if (!_primed) {
    _filtered = v;
    _primed = true;
    _lastSampleMs = now;
    return;
  }
  float dt = (now - _lastSampleMs) / 1000.0f;
  _lastSampleMs = now;
  if (dt <= 0 || dt > 2.0f) { _filtered = v; return; }

  // Base time constant by smoothing level. The receiver is already Doppler based and
  // accurate, so these are short: the aim is to remove jitter, not to add lag.
  // Low passes the receiver's speed straight through; the integer hysteresis still stops flicker.
  static const float TAU[3] = {0.0f, 0.15f, 0.40f};
  float tau = TAU[_level > 2 ? 2 : _level];
  // Adaptive: a big difference means real acceleration, so follow it faster.
  float err = fabsf(v - _filtered);
  tau = tau / (1.0f + err / 0.75f);
  float alpha = dt / (tau + dt);
  _filtered += alpha * (v - _filtered);
  if (v == 0 && _filtered < 0.3f) _filtered = 0;
}

void SpeedFilter::tick(uint32_t now) {
  float dt = (now - _lastTickMs) / 1000.0f;
  _lastTickMs = now;
  if (dt <= 0 || dt > 0.5f) { _display = _filtered; return; }
  // Gentle frame-rate easing between GPS samples so the ring glides.
  const float tau = 0.12f;
  _display += (dt / (tau + dt)) * (_filtered - _display);
  if (fabsf(_display - _filtered) < 0.005f) _display = _filtered;
}

int SpeedFilter::displayInt(float unitsPerMs) {
  float u = _filtered * unitsPerMs;
  if (u < 0.5f) { _shown = 0; return 0; }
  // Round up, as a speedometer should never under-read: rounding u + 0.5 to nearest is ceil(u).
  // Only move to a new integer once we are clearly past it.
  float r = u + 0.5f;
  if (r > _shown + HYSTERESIS || r < _shown - HYSTERESIS) _shown = (int)lroundf(r);
  return _shown;
}

void StillDetector::update(float ax, float ay, float az, uint32_t now) {
  available = true;
  float mag = sqrtf(ax * ax + ay * ay + az * az);
  // Exponential mean and variance of |a| (in g)
  const float k = 0.05f;
  float d = mag - _mean;
  _mean += k * d;
  _var = (1 - k) * (_var + k * d * d);
  bool quiet = sqrtf(_var) < 0.012f;
  if (!quiet) { _quietSince = 0; _still = false; return; }
  if (_quietSince == 0) _quietSince = now;
  _still = (now - _quietSince) > 1500;  // quiet for 1.5 s
}
