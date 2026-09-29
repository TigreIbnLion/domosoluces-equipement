#pragma once
#include "HardwareAdapter.h"

#ifndef DOMO_RELAY_PIN
#error "DOMO_RELAY_PIN must be defined for the prototype hardware"
#endif

namespace domo {
class KeyestudioAdapter final : public HardwareAdapter {
 public:
  bool begin() override {
    pinMode(DOMO_RELAY_PIN, OUTPUT);
    digitalWrite(DOMO_RELAY_PIN, LOW);
    state_ = LogicalState::Off;
    return true;
  }
  bool setState(LogicalState target) override {
    if (target == LogicalState::Unknown) return false;
    if (state_ == target) return true; // set_state is intrinsically idempotent
    digitalWrite(DOMO_RELAY_PIN, target == LogicalState::On ? HIGH : LOW);
    state_ = digitalRead(DOMO_RELAY_PIN) == HIGH ? LogicalState::On : LogicalState::Off;
    return state_ == target;
  }
  LogicalState readState() const override { return state_; }
  Telemetry readTelemetry() const override { return {}; } // no fabricated measurements
  void loop() override {}
 private:
  LogicalState state_{LogicalState::Unknown};
};
} // namespace domo
