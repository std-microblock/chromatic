#pragma once
#include "native_pointer.h"
#include <functional>
#include <memory>
#include <string>

namespace chromatic::js {

struct NativeHardwareBreakpoint {
  /// Set a hardware breakpoint/watchpoint.
  /// type: "execute" | "write" | "readwrite"
  /// size: 1, 2, 4, or 8 bytes (for watchpoints; ignored for execute)
  /// onHit: callback receiving cpuContext pointer (hex)
  /// Returns breakpointId (hex).
  static std::string set(std::shared_ptr<NativePointer> address,
                         const std::string &type, int size,
                         std::function<void(std::string)> onHit);

  /// Remove a breakpoint by ID.
  static void remove(const std::string &breakpointId);

  /// Remove all hardware breakpoints.
  static void removeAll();

  /// Max hardware breakpoints the platform supports.
  static int maxBreakpoints();

  /// Currently active count.
  static int activeCount();
};

} // namespace chromatic::js
