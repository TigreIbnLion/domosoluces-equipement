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
  size_t capabilityCount() const override { return state_ == LogicalState::Unknown ? 0 : 1; }
  CapabilityDescriptor capability(size_t index) const override {
    if (index != 0 || state_ == LogicalState::Unknown) return {nullptr, CapabilitySemantic::Switch, false, false, false};
    return {"switch", CapabilitySemantic::Switch, true, true, false};
  }
  bool readCapability(const String& id, CapabilityValue& out) const override {
    if (id != "switch" || state_ == LogicalState::Unknown) return false;
    out.available=true; out.value=state_ == LogicalState::On ? "on" : "off"; return true;
  }
  bool executeCapability(const String& id, const String& command, const String& value, CapabilityValue& confirmed) override {
    if (id != "switch" || command != "set_state" || (value != "on" && value != "off")) return false;
    const auto target=value == "on" ? LogicalState::On : LogicalState::Off;
    if (!setState(target)) return false;
    return readCapability(id, confirmed);
  }
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
