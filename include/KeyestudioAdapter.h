#pragma once
#include "HardwareAdapter.h"

#ifndef DOMO_RELAY_PIN
#define DOMO_RELAY_PIN -1
#endif
#ifndef DOMO_RELAY_ACTIVE_HIGH
#define DOMO_RELAY_ACTIVE_HIGH -1
#endif

namespace domo {
class KeyestudioAdapter final : public HardwareAdapter {
 public:
  bool begin() override {
    if (DOMO_RELAY_PIN < 0 || (DOMO_RELAY_ACTIVE_HIGH != 0 && DOMO_RELAY_ACTIVE_HIGH != 1)) {
      state_ = LogicalState::Unknown;
      return false;
    }
    pinMode(DOMO_RELAY_PIN, OUTPUT);
    writeState(LogicalState::Off);
    state_ = readPinState();
    return state_ == LogicalState::Off;
  }
  bool setState(LogicalState target) override {
    if (target == LogicalState::Unknown || DOMO_RELAY_PIN < 0) return false;
    if (state_ == target) return true;
    writeState(target);
    state_ = readPinState();
    return state_ == target;
  }
  LogicalState readState() const override { return state_; }
  Telemetry readTelemetry() const override { return {}; }
  void loop() override {}
 private:
  static int electricalLevel(LogicalState target) {
    const bool active = target == LogicalState::On;
    const bool high = DOMO_RELAY_ACTIVE_HIGH ? active : !active;
    return high ? HIGH : LOW;
  }
  static LogicalState readPinState() {
    const bool high = digitalRead(DOMO_RELAY_PIN) == HIGH;
    const bool active = DOMO_RELAY_ACTIVE_HIGH ? high : !high;
    return active ? LogicalState::On : LogicalState::Off;
  }
  static void writeState(LogicalState target) {
    digitalWrite(DOMO_RELAY_PIN, electricalLevel(target));
  }
  LogicalState state_{LogicalState::Unknown};
};
} // namespace domo
