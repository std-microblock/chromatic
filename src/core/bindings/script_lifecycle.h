#pragma once
#include <functional>
#include <string>

namespace chromatic::js {

struct ScriptLifecycle {
  /// Register a callback to be called before script dispose/reload.
  /// Returns callbackId (hex) for removal.
  static std::string onDispose(std::function<void()> callback);

  /// Remove a dispose callback by ID.
  static void removeDisposeCallback(const std::string &callbackId);

  /// Remove all dispose callbacks.
  static void removeAllDisposeCallbacks();

  /// [Internal] Call all registered dispose callbacks, then clear them.
  static void _callDisposeCallbacks();
};

} // namespace chromatic::js
