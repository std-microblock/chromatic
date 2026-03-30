#include "script_lifecycle.h"
#include <cstdint>
#include <fmt/core.h>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace {

static std::mutex g_mutex;
static uint64_t g_nextId = 1;
static std::unordered_map<uint64_t, std::function<void()>> g_callbacks;

std::string toHexId(uint64_t id) {
  std::ostringstream oss;
  oss << "0x" << std::hex << id;
  return oss.str();
}

} // namespace

namespace chromatic::js {

std::string ScriptLifecycle::onDispose(std::function<void()> callback) {
  std::lock_guard lock(g_mutex);
  auto id = g_nextId++;
  g_callbacks[id] = std::move(callback);
  return toHexId(id);
}

void ScriptLifecycle::removeDisposeCallback(const std::string &callbackId) {
  uint64_t id = std::stoull(callbackId, nullptr, 16);
  std::lock_guard lock(g_mutex);
  g_callbacks.erase(id);
}

void ScriptLifecycle::removeAllDisposeCallbacks() {
  std::lock_guard lock(g_mutex);
  g_callbacks.clear();
}

void ScriptLifecycle::_callDisposeCallbacks() {
  // Take a snapshot of callbacks under lock, then call them outside lock
  std::vector<std::function<void()>> callbacks;
  {
    std::lock_guard lock(g_mutex);
    callbacks.reserve(g_callbacks.size());
    for (auto &[id, cb] : g_callbacks) {
      callbacks.push_back(cb);
    }
  }

  for (auto &cb : callbacks) {
    try {
      cb();
    } catch (const std::exception &e) {
      fmt::print(stderr, "Exception in dispose callback: {}\n", e.what());
    } catch (...) {
      fmt::print(stderr, "Unknown exception in dispose callback\n");
    }
  }

  // Clear all callbacks after calling
  removeAllDisposeCallbacks();
}

} // namespace chromatic::js
