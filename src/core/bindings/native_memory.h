#pragma once
#include <memory>
#include <string>
#include <vector>

namespace async_simple::coro {
template <typename T> class Lazy;
}

namespace chromatic::js {

/// Result of a pattern scan match.
struct ScanMatch {
  std::string address; // hex
  int size;
};

struct NativeMemory {
  /// Read `size` bytes from `address` (hex string), return hex-encoded data
  static std::string readMemory(const std::string &address, int size);

  /// Like readMemory but returns empty string on access fault instead of
  /// crashing
  static std::string safeReadMemory(const std::string &address, int size);

  /// Write hex-encoded `hexData` to `address`
  static void writeMemory(const std::string &address,
                          const std::string &hexData);

  /// Allocate `size` bytes of RWX memory, return address as hex string
  static std::string allocateMemory(int size);

  /// Free previously allocated memory at `address` of given `size`
  static void freeMemory(const std::string &address, int size);

  /// Change memory protection. `protection` is like "rwx"/"r-x"/etc.
  /// Returns old protection string.
  static std::string protectMemory(const std::string &address, int size,
                                   const std::string &protection);

  /// Write bytes + flush instruction cache (for code patching)
  static void patchCode(const std::string &address,
                        const std::string &hexBytes);

  /// Flush instruction cache for region
  static void flushIcache(const std::string &address, int size);

  /// Copy `size` bytes from `src` to `dst`
  static void copyMemory(const std::string &dst, const std::string &src,
                         int size);

  /// Scan memory region for pattern (e.g. "48 8b ?? 00") using
  /// Boyer-Moore-Horspool with wildcard support.
  /// Returns vector of ScanMatch with address + pattern size.
  static std::vector<std::shared_ptr<ScanMatch>>
  scanMemory(const std::string &address, int size,
             const std::string &pattern);

  /// Scan within a named module for pattern.
  /// Internally looks up module base+size, then delegates to scanMemory.
  static std::vector<std::shared_ptr<ScanMatch>>
  scanModule(const std::string &moduleName, const std::string &pattern);

  /// Async variant of scanMemory — returns Lazy<T> (→ JS Promise).
  static async_simple::coro::Lazy<std::vector<std::shared_ptr<ScanMatch>>>
  scanMemoryAsync(const std::string &address, int size,
                  const std::string &pattern);

  /// Async variant of scanModule — returns Lazy<T> (→ JS Promise).
  static async_simple::coro::Lazy<std::vector<std::shared_ptr<ScanMatch>>>
  scanModuleAsync(const std::string &moduleName, const std::string &pattern);
};
} // namespace chromatic::js
