#pragma once
#include "native_pointer.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
namespace chromatic::js {

struct SegmentInfo {
  uint64_t base;
  uint64_t size;
};

struct ModuleInfo {
  std::string name;
  std::shared_ptr<NativePointer> base;
  int size;
  std::string path;
  std::vector<SegmentInfo>
      segments; // individual mapped regions (not exposed to JS)
};

struct RangeInfo {
  std::shared_ptr<NativePointer> base;
  int size;
  std::string protection;
  std::string filePath; // empty if none
};

struct ExportInfo {
  std::string type;
  std::string name;
  std::shared_ptr<NativePointer> address;
};

struct NativeProcess {
  /// Returns "arm64" or "x64"
  static std::string getArchitecture();

  /// Returns "windows", "linux", "darwin", or "android"
  static std::string getPlatform();

  /// Returns pointer size in bytes (4 or 8)
  static int getPointerSize();

  /// Returns system page size
  static int getPageSize();

  /// Returns current process ID
  static int getProcessId();

  /// Returns current thread ID
  static std::shared_ptr<NativePointer> getCurrentThreadId();

  /// Returns vector of loaded modules
  static std::vector<std::shared_ptr<ModuleInfo>> enumerateModules();

  /// Returns vector of memory ranges matching protection
  static std::vector<std::shared_ptr<RangeInfo>>
  enumerateRanges(const std::string &protection);

  /// Find export address by module and export name.
  static std::shared_ptr<NativePointer>
  findExportByName(const std::string &moduleName,
                   const std::string &exportName);

  /// Find module containing address, or nullptr
  static std::shared_ptr<ModuleInfo>
  findModuleByAddress(std::shared_ptr<NativePointer> address);

  /// Find module by name, or nullptr
  static std::shared_ptr<ModuleInfo>
  findModuleByName(const std::string &name);

  /// Enumerate exports of a module
  static std::vector<std::shared_ptr<ExportInfo>>
  enumerateExports(const std::string &moduleName);
};
} // namespace chromatic::js
