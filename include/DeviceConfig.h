#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include "HardwareAdapter.h"
namespace domo {
struct WifiCredentials { String ssid; String password; bool configured() const { return !ssid.isEmpty(); } };
class DeviceConfig {
 public:
  bool begin() { return prefs_.begin("domo-config", false); }
  WifiCredentials wifi(const char* fallbackSsid="", const char* fallbackPassword="") {
    WifiCredentials c{prefs_.getString("wifi_ssid", ""), prefs_.getString("wifi_pass", "")};
    if (!c.configured() && fallbackSsid && *fallbackSsid) { c.ssid=fallbackSsid; c.password=fallbackPassword ? fallbackPassword : ""; }
    return c;
  }
  bool hasStoredWifi() { return !prefs_.getString("wifi_ssid", "").isEmpty(); }
  bool saveWifi(const String& ssid, const String& password) {
    if (ssid.isEmpty() || ssid.length()>32 || password.length()>63) return false;
    return prefs_.putString("wifi_ssid", ssid)==ssid.length() && prefs_.putString("wifi_pass", password)==password.length();
  }
  bool clearWifi() { return prefs_.remove("wifi_ssid") | prefs_.remove("wifi_pass"); }
  bool saveConfirmedState(LogicalState state) {
    if (state == LogicalState::Unknown) return false;
    return prefs_.putUChar("last_state", state == LogicalState::On ? 1 : 0) == 1;
  }
  LogicalState confirmedState() {
    if (!prefs_.isKey("last_state")) return LogicalState::Unknown;
    return prefs_.getUChar("last_state", 2) == 1 ? LogicalState::On : LogicalState::Off;
  }
 private: Preferences prefs_;
};
}