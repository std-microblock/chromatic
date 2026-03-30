#pragma once
#include <functional>
#include <string>

namespace chromatic::js {

struct NativeSoftwareBreakpoint {
  /// Set a software breakpoint at address (hex).
  /// Writes INT3 (x86) or BRK #1 (ARM64).
  /// onHit receives cpuContext pointer (hex) when triggered.
  /// Returns breakpointId (hex).
  static std::string set(const std::string &address,
                         std::function<void(std::string)> onHit);

  /// Remove a breakpoint by ID. Restores original byte(s).
  static void remove(const std::string &breakpointId);

  /// Remove all software breakpoints.
  static void removeAll();
};

} // namespace chromatic::js
