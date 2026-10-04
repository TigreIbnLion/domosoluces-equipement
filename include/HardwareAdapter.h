#pragma once
#include <Arduino.h>

namespace domo {
enum class LogicalState { Off, On, Unknown };

enum class CapabilitySemantic { Switch, Level, Temperature, Humidity, Motion, Contact, Alarm, Gas, Water, Rgb, Rfid };
struct CapabilityDescriptor {
  const char* id;
  CapabilitySemantic semantic;
  bool readable;
  bool commandable;
  bool configurable;
};
struct CapabilityValue {
  bool available{false};
  String value;
};

struct Telemetry {
  bool hasCurrentPower{false}; float currentPower{0};
  bool hasEnergyKwh{false}; float energyKwh{0};
  bool hasVoltage{false}; float voltage{0};
  bool hasCurrent{false}; float current{0};
  bool hasPowerFactor{false}; float powerFactor{0};
};

class HardwareAdapter {
 public:
  virtual ~HardwareAdapter() = default;
  virtual bool begin() = 0;
  virtual bool setState(LogicalState target) = 0;
  virtual LogicalState readState() const = 0;
  virtual Telemetry readTelemetry() const = 0;
  virtual size_t capabilityCount() const = 0;
  virtual CapabilityDescriptor capability(size_t index) const = 0;
  virtual bool readCapability(const String& id, CapabilityValue& out) const = 0;
  virtual bool executeCapability(const String& id, const String& command, const String& value, CapabilityValue& confirmed) = 0;
  virtual void loop() = 0;
};
} // namespace domo
