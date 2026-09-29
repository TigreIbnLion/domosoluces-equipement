#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include "HardwareAdapter.h"

namespace domo {
struct CommandResult {
  String id;
  LogicalState state{LogicalState::Unknown};
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

  void remember(const String& id, LogicalState state) {
    if (id.isEmpty()) return;
    CommandResult existing;
    if (find(id, existing)) return;
    items_[cursor_] = {id, state};
    cursor_ = (cursor_ + 1) % Capacity;
    save();
  }

 private:
  Preferences prefs_;
  CommandResult items_[Capacity];
  size_t cursor_{0};

  void load() {
    cursor_ = prefs_.getUChar("cursor", 0) % Capacity;
    for (size_t i = 0; i < Capacity; ++i) {
      const String key = String("c") + i;
      const String raw = prefs_.getString(key.c_str(), "");
      const int split = raw.lastIndexOf('|');
      if (split <= 0) continue;
      items_[i].id = raw.substring(0, split);
      const String state = raw.substring(split + 1);
      items_[i].state = state == "on" ? LogicalState::On :
                        state == "off" ? LogicalState::Off : LogicalState::Unknown;
    }
  }

  void save() {
    prefs_.putUChar("cursor", static_cast<uint8_t>(cursor_));
    const size_t i = (cursor_ + Capacity - 1) % Capacity;
    const char* state = items_[i].state == LogicalState::On ? "on" :
                        items_[i].state == LogicalState::Off ? "off" : "unknown";
    const String key = String("c") + i;
    const String raw = items_[i].id + "|" + state;
    prefs_.putString(key.c_str(), raw);
  }
};
} // namespace domo
