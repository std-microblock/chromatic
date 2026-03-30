#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace chromatic::js {

// ─── Exception types ───

enum class ExceptionType {
  AccessViolation,    // SIGSEGV / EXCEPTION_ACCESS_VIOLATION
  BusError,           // SIGBUS
  Breakpoint,         // SIGTRAP / EXCEPTION_BREAKPOINT
  SingleStep,         // SIGTRAP (TRAP_TRACE) / EXCEPTION_SINGLE_STEP
  IllegalInstruction, // SIGILL
  Unknown
};

enum class AccessType { Read, Write, Execute, Unknown };
enum class HandleAction { Handled, NotHandled };

// ─── ExceptionContext ───

struct ExceptionContext {
  chromatic::js::ExceptionType type;
  uint64_t faultAddress; // Address that faulted (or PC for breakpoints)
  chromatic::js::AccessType accessType; // For SIGSEGV: read/write/execute
  void *$platformContext; // ucontext_t* (POSIX) or EXCEPTION_POINTERS* (Win)

  /// Get the program counter from the platform context.
  uint64_t getPc() const;
  /// Set the program counter in the platform context (for resumption).
  void setPc(uint64_t newPc);
  /// Enable/disable single-step mode (TF on x86, SS bit on ARM64).
  void setSingleStep(bool enable);
};

// ─── JS-facing binding struct ───

struct NativeExceptionHandler {
  /// Enable the global exception handler (opt-in, idempotent).
  static void enable();

  /// Disable the global exception handler. Restores original handlers.
  static void disable();

  /// Returns true if currently enabled.
  static bool isEnabled();

  /// Register a JS callback for a given exception type.
  /// type:
  /// "access_violation"|"breakpoint"|"single_step"|"bus_error"|"illegal_instruction"
  /// The callback receives (exceptionType: string, faultAddress: hex string).
  /// Returns callbackId (hex) for removal.
  static std::string
  addCallback(const std::string &type,
              std::function<void(std::string, std::string)> callback);

  /// Remove a previously registered callback.
  static void removeCallback(const std::string &callbackId);

  /// Remove all registered callbacks.
  static void removeAllCallbacks();
};

// ─── Internal API (for SoftwareBP, HardwareBP, PageAccess) ───

namespace internal {

using ExceptionCallbackId = uint64_t;
using ExceptionCallback = std::function<chromatic::js::HandleAction(
    std::shared_ptr<ExceptionContext> ctx)>;

/// Register an internal handler for a specific exception type.
/// Returns a unique callback ID for later removal.
ExceptionCallbackId registerHandler(ExceptionType type,
                                    ExceptionCallback callback);

/// Unregister a previously registered internal handler.
void unregisterHandler(ExceptionCallbackId id);

/// Reference-counted enable: increments refcount, installs handlers
/// if not already installed.
void refEnable();

/// Reference-counted disable: decrements refcount, removes handlers
/// when refcount reaches 0.
void refDisable();

} // namespace internal

} // namespace chromatic::js
