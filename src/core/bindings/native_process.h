#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
namespace chromatic::js {

struct SegmentInfo {
  uint64_t base;
  uint64_t size;
};

struct ModuleInfo {
  std::string name;
  std::string base; // hex address
  int size;
  std::string path;
  std::vector<SegmentInfo>
      segments; // individual mapped regions (not exposed to JS)
};

struct RangeInfo {
  std::string base; // hex address
  int size;
  std::string protection;
  std::string filePath; // empty if none
};

struct ExportInfo {
  std::string type;
  std::string name;
  std::string address; // hex address
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

  /// Returns current thread ID as hex string
  static std::string getCurrentThreadId();

  /// Returns vector of loaded modules
  static std::vector<ModuleInfo> enumerateModules();

  /// Returns vector of memory ranges matching protection
  static std::vector<RangeInfo> enumerateRanges(const std::string &protection);

  /// Find export address by module and export name. Returns hex address or
  /// "0x0".
  static std::string findExportByName(const std::string &moduleName,
                                      const std::string &exportName);

  /// Find module containing address, or nullopt
  static std::optional<ModuleInfo>
  findModuleByAddress(const std::string &address);

  /// Find module by name, or nullopt
  static std::optional<ModuleInfo> findModuleByName(const std::string &name);

  /// Enumerate exports of a module
  static std::vector<ExportInfo>
  enumerateExports(const std::string &moduleName);
};
} // namespace chromatic::js
