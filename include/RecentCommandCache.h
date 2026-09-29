#pragma once
#include <Arduino.h>

namespace domo {
class RecentCommandCache {
 public:
  static constexpr size_t Capacity = 16;
  bool contains(const String& id) const {
    for (const auto& item : ids_) if (item == id) return true;
    return false;
  }
  void remember(const String& id) {
    if (id.isEmpty() || contains(id)) return;
    ids_[cursor_] = id;
    cursor_ = (cursor_ + 1) % Capacity;
  }
 private:
  String ids_[Capacity];
  size_t cursor_{0};
};
} // namespace domo
