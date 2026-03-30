#pragma once
#include "native_pointer.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace chromatic::js {

struct MemoryAccessDetails {
  std::shared_ptr<NativePointer> address;  // exact access address
  std::shared_ptr<NativePointer> pageBase; // page-aligned base
  std::string operation;                   // "read" | "write" | "execute"
  int rangeIndex;                          // which watched range triggered
};

struct NativeMemoryAccessMonitor {
  /// Enable monitoring on memory ranges (one-shot per range).
  /// Removes permissions via mprotect, catches SIGSEGV on first access.
  /// When a watched range is accessed, permissions are permanently restored
  /// and the access is recorded.
  /// onAccess fires with (address, pageBase, operation, rangeIndex).
  /// Returns monitorId (hex).
  static std::string
  enable(const std::vector<std::shared_ptr<NativePointer>> &addresses,
         const std::vector<int> &sizes,
         std::function<void(std::shared_ptr<NativePointer>,
                            std::shared_ptr<NativePointer>, std::string, int)>
             onAccess);

  /// Disable monitoring for a specific monitor.
  static void disable(const std::string &monitorId);

  /// Disable all active monitors.
  static void disableAll();

  /// Drain pending access events, calling the registered callbacks.
  /// Returns the number of events drained.
  /// This is called from JS context (safe for JS callbacks).
  static int drainPending();
};

} // namespace chromatic::js
