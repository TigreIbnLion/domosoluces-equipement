#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include "HardwareAdapter.h"

namespace domo {
struct CommandResult {
  String id;
  bool executed{false};
  LogicalState state{LogicalState::Unknown};
  String error;
  bool v2{false};
  String capabilityId;
  String value;
  String origin;
  String errorCode;
};

class RecentCommandCache {
 public:
  static constexpr size_t Capacity = 16;

  bool begin() {
    if (!prefs_.begin("domo-cmd", false)) return false;
    load();
    return true;
  }

  bool find(const String& id, CommandResult& out) const {
    for (const auto& item : items_) {
      if (item.id == id) { out = item; return true; }
    }
    return false;
  }

  bool remember(const String& id, bool executed, LogicalState state, const String& error = "",
                bool v2=false, const String& capabilityId="", const String& value="",
                const String& origin="", const String& errorCode="") {
    if (id.isEmpty()) return false;
    CommandResult existing;
    if (find(id, existing)) return true;

    const size_t slot = cursor_;
    CommandResult candidate;
    candidate.id = id;
    candidate.executed = executed;
    candidate.state = state;
    candidate.error = error; candidate.v2=v2; candidate.capabilityId=capabilityId;
    candidate.value=value; candidate.origin=origin; candidate.errorCode=errorCode;

    if (!save(slot, candidate)) return false;

    items_[slot] = candidate;
    cursor_ = (cursor_ + 1) % Capacity;
    return prefs_.putUChar("cursor", static_cast<uint8_t>(cursor_)) == sizeof(uint8_t);
  }

 private:
  Preferences prefs_;
  CommandResult items_[Capacity];
  size_t cursor_{0};

  static LogicalState parseState(const String& value) {
    if (value == "on") return LogicalState::On;
    if (value == "off") return LogicalState::Off;
    return LogicalState::Unknown;
  }

  static const char* stateName(LogicalState state) {
    if (state == LogicalState::On) return "on";
    if (state == LogicalState::Off) return "off";
    return "unknown";
  }

  void load() {
    cursor_ = prefs_.getUChar("cursor", 0) % Capacity;
    for (size_t i = 0; i < Capacity; ++i) {
      const String key = String("c") + i;
      // Missing slots are normal on first boot. Avoid Preferences emitting an
      // error for every empty cache entry.
      if (!prefs_.isKey(key.c_str())) continue;
      const String raw = prefs_.getString(key.c_str(), "");
      if (raw.isEmpty()) continue;

      // V2: id|executed(1/0)|state|error
      int p1 = raw.indexOf('|');
      int p2 = p1 < 0 ? -1 : raw.indexOf('|', p1 + 1);
      int p3 = p2 < 0 ? -1 : raw.indexOf('|', p2 + 1);
      if (p1 > 0 && p2 > p1 && p3 > p2) {
        items_[i].id = raw.substring(0, p1);
        items_[i].executed = raw.substring(p1 + 1, p2) == "1";
        items_[i].state = parseState(raw.substring(p2 + 1, p3));
        const String tail=raw.substring(p3 + 1);
        const int q1=tail.indexOf('|'), q2=q1<0?-1:tail.indexOf('|',q1+1), q3=q2<0?-1:tail.indexOf('|',q2+1), q4=q3<0?-1:tail.indexOf('|',q3+1), q5=q4<0?-1:tail.indexOf('|',q4+1);
        if(q1>=0&&q2>q1&&q3>q2&&q4>q3&&q5>q4){
          items_[i].error=tail.substring(0,q1); items_[i].v2=tail.substring(q1+1,q2)=="1";
          items_[i].capabilityId=tail.substring(q2+1,q3); items_[i].value=tail.substring(q3+1,q4);
          items_[i].origin=tail.substring(q4+1,q5); items_[i].errorCode=tail.substring(q5+1);
        } else items_[i].error=tail;
        continue;
      }

      // Backward compatibility with V1 cache: id|state = successful execution.
      const int legacy = raw.lastIndexOf('|');
      if (legacy > 0) {
        items_[i].id = raw.substring(0, legacy);
        items_[i].executed = true;
        items_[i].state = parseState(raw.substring(legacy + 1));
        items_[i].error = "";
      }
    }
  }

  bool save(size_t slot, const CommandResult& item) {
    const String key = String("c") + slot;
    const String raw = item.id + "|" + (item.executed ? "1" : "0") +
                       "|" + stateName(item.state) + "|" + item.error + "|" +
                       (item.v2 ? "1" : "0") + "|" + item.capabilityId + "|" + item.value +
                       "|" + item.origin + "|" + item.errorCode;
    const size_t written = prefs_.putString(key.c_str(), raw);
    if (written != raw.length()) return false;
    return prefs_.getString(key.c_str(), "") == raw;
  }
};
} // namespace domo
