#pragma once
#include "native_pointer.h"
#include <memory>
#include <string>
#include <vector>

namespace async_simple::coro {
template <typename T> class Lazy;
}

namespace chromatic::js {

/// Result of a pattern scan match.
struct ScanMatch {
  std::shared_ptr<NativePointer> address;
  int size;
};

struct NativeMemory {
  /// Read `size` bytes from `address`, return as byte vector (→ JS ArrayBuffer)
  static std::vector<uint8_t> readMemory(std::shared_ptr<NativePointer> address,
                                         int size);

  /// Like readMemory but returns empty vector on access fault instead of
  /// crashing
  static std::vector<uint8_t>
  safeReadMemory(std::shared_ptr<NativePointer> address, int size);

  /// Write bytes to `address`
  static void writeMemory(std::shared_ptr<NativePointer> address,
                          std::vector<uint8_t> data);

  /// Allocate `size` bytes of RWX memory, return address
  static std::shared_ptr<NativePointer> allocateMemory(int size);

  /// Free previously allocated memory at `address` of given `size`
  static void freeMemory(std::shared_ptr<NativePointer> address, int size);

  /// Change memory protection. `protection` is like "rwx"/"r-x"/etc.
  /// Returns old protection string.
  static std::string protectMemory(std::shared_ptr<NativePointer> address,
                                   int size, const std::string &protection);

  /// Write bytes + flush instruction cache (for code patching)
  static void patchCode(std::shared_ptr<NativePointer> address,
                        std::vector<uint8_t> bytes);

  /// Flush instruction cache for region
  static void flushIcache(std::shared_ptr<NativePointer> address, int size);

  /// Copy `size` bytes from `src` to `dst`
  static void copyMemory(std::shared_ptr<NativePointer> dst,
                         std::shared_ptr<NativePointer> src, int size);

  /// Scan memory region for pattern (e.g. "48 8b ?? 00") using
  /// Boyer-Moore-Horspool with wildcard support.
  /// Returns vector of ScanMatch with address + pattern size.
  static std::vector<std::shared_ptr<ScanMatch>>
  scanMemory(std::shared_ptr<NativePointer> address, int size,
             const std::string &pattern);

  /// Scan within a named module for pattern.
  /// Internally looks up module base+size, then delegates to scanMemory.
  static std::vector<std::shared_ptr<ScanMatch>>
  scanModule(const std::string &moduleName, const std::string &pattern);

  /// Async variant of scanMemory — returns Lazy<T> (→ JS Promise).
  static async_simple::coro::Lazy<std::vector<std::shared_ptr<ScanMatch>>>
  scanMemoryAsync(std::shared_ptr<NativePointer> address, int size,
                  const std::string &pattern);

  /// Async variant of scanModule — returns Lazy<T> (→ JS Promise).
  static async_simple::coro::Lazy<std::vector<std::shared_ptr<ScanMatch>>>
  scanModuleAsync(const std::string &moduleName, const std::string &pattern);
};
} // namespace chromatic::js
