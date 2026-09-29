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

  void remember(const String& id, bool executed, LogicalState state, const String& error = "") {
    if (id.isEmpty()) return;
    CommandResult existing;
    if (find(id, existing)) return;
    items_[cursor_].id = id;
    items_[cursor_].executed = executed;
    items_[cursor_].state = state;
    items_[cursor_].error = error;
    cursor_ = (cursor_ + 1) % Capacity;
    saveLast();
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
        items_[i].error = raw.substring(p3 + 1);
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

  void saveLast() {
    prefs_.putUChar("cursor", static_cast<uint8_t>(cursor_));
    const size_t i = (cursor_ + Capacity - 1) % Capacity;
    const String key = String("c") + i;
    const String raw = items_[i].id + "|" + (items_[i].executed ? "1" : "0") +
                       "|" + stateName(items_[i].state) + "|" + items_[i].error;
    prefs_.putString(key.c_str(), raw);
  }
};
} // namespace domo
