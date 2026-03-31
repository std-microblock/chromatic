#pragma once
#include "native_pointer.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace chromatic::js {

class NativeCModule : public std::enable_shared_from_this<NativeCModule> {
  struct Impl;
  std::unique_ptr<Impl> $impl;

public:
  /// Compile C source code with TCC (internal compiler).
  /// code: full C source (preamble + user code, concatenated by TS layer)
  /// symbolNames/symbolAddresses: parallel arrays for extern symbol injection
  ///   (e.g. Memory.alloc'd buffers or NativeCallback addresses)
  /// Throws on compilation or relocation error.
  /// Automatically calls void init(void) if defined.
  NativeCModule(const std::string &code,
                const std::vector<std::string> &symbolNames,
                const std::vector<uint64_t> &symbolAddresses);

  /// Default constructor (for binding system; creates empty/disposed module).
  NativeCModule();

  ~NativeCModule();

  // Move OK, no copy
  NativeCModule(NativeCModule &&) noexcept;
  NativeCModule &operator=(NativeCModule &&) noexcept;
  NativeCModule(const NativeCModule &) = delete;
  NativeCModule &operator=(const NativeCModule &) = delete;

  /// Get the address of a named exported symbol. Returns null pointer if not
  /// found.
  std::shared_ptr<NativePointer> getSymbol(const std::string &name);

  /// Get all exported global symbol names.
  std::vector<std::string> listSymbols();

  /// Eagerly unmap the module from memory.
  /// Calls void finalize(void) if defined, then frees the TCC state.
  void dispose();
};

} // namespace chromatic::js
