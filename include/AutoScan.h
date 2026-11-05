#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

// Scheduling and match selection are independent of sensor names and protocols.
// Probe exactly one profile per tick; publish only complete scan results.
class AutoScan {
public:
  static constexpr uint32_t intervalMs = 3000;
  bool enabled = true;
  bool scanning = false;
  std::vector<size_t> matches;
  void restart(uint32_t now) {
    scanning = false; cursor = 0; pending.clear(); due = now;
  }
  void disable() { enabled = false; scanning = false; pending.clear(); }
  void enable(uint32_t now) { enabled = true; restart(now); }
  template<class Probe>
  bool tick(uint32_t now, size_t count, Probe probe) {
    if (!enabled) return false;
    if (!scanning) {
      if (int32_t(now - due) < 0) return false;
      pending.clear(); cursor = 0; scanning = true;
    }
    if (cursor < count) {
      if (probe(cursor)) pending.push_back(cursor);
      ++cursor;
    }
    if (cursor < count) return false;
    matches = pending; scanning = false; due = now + intervalMs;
    if (matches.size() == 1) enabled = false;
    return true;
  }
private:
  size_t cursor = 0;
  uint32_t due = 0;
  std::vector<size_t> pending;
};
