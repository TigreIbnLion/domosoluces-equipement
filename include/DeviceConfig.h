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
  bool saveRecoveryPolicy(const String& policy, LogicalState safeValue=LogicalState::Off) {
    if(policy!="force_off"&&policy!="restore_last_state"&&policy!="safe_value") return false;
    if(policy=="safe_value"&&safeValue==LogicalState::Unknown) return false;
    if(prefs_.putString("recovery_policy",policy)!=policy.length()) return false;
    if(policy=="safe_value") return prefs_.putUChar("safe_state",safeValue==LogicalState::On?1:0)==1;
    return true;
  }
  String recoveryPolicy() {
    const String p=prefs_.getString("recovery_policy","");
    if(p=="force_off"||p=="restore_last_state"||p=="safe_value") return p;
    if(prefs_.getBool("restore_state",false)) return "restore_last_state";
    return "force_off";
  }
  LogicalState recoverySafeValue() { return prefs_.getUChar("safe_state",0)==1?LogicalState::On:LogicalState::Off; }
  bool saveRecoveryPolicy(bool restoreLastState) { return saveRecoveryPolicy(restoreLastState?"restore_last_state":"force_off"); }
  bool restoreLastStateEnabled(bool fallback=false) { const String p=recoveryPolicy(); return p=="restore_last_state" || (p.isEmpty()&&fallback); }
  LogicalState confirmedState() {
    if (!prefs_.isKey("last_state")) return LogicalState::Unknown;
    return prefs_.getUChar("last_state", 2) == 1 ? LogicalState::On : LogicalState::Off;
  }
 private: Preferences prefs_;
};
}