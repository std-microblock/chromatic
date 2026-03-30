#pragma once
#include "native_pointer.h"
#include <functional>
#include <memory>
#include <string>

namespace chromatic::js {
struct NativeInterceptor {
  /// Attach an inline hook to `target`.
  /// onEnter/onLeave receive the cpuContext pointer as a hex string.
  /// Returns a hookId string used for detaching.
  static std::string attach(std::shared_ptr<NativePointer> target,
                            std::function<void(std::string)> onEnter,
                            std::function<void(std::string)> onLeave);

  /// Detach a hook by hookId.
  static void detach(const std::string &hookId);

  /// Detach all active hooks.
  static void detachAll();

  /// Replace target function with replacement.
  /// Returns trampoline address to call the original function.
  static std::shared_ptr<NativePointer>
  replace(std::shared_ptr<NativePointer> target,
          std::shared_ptr<NativePointer> replacement);

  /// Revert a replacement.
  static void revert(std::shared_ptr<NativePointer> target);
};
} // namespace chromatic::js
