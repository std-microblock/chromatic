#include "native_interceptor.h"
#include "internal/code_relocator.h"

#include <cstdint>
#include <cstring>
#include <functional>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace {

// ─── Utilities ───

uint64_t parseHexAddr(const std::string &s) {
  return std::stoull(s, nullptr, 16);
}

std::string toHexAddr(uint64_t addr) {
  std::ostringstream oss;
  oss << "0x" << std::hex << addr;
  return oss.str();
}

// ─── Hook entry ───

struct HookEntry {
  uint64_t target;
  void *trampolineCode; // asmjit-managed trampoline
  void *relocatedCode;  // asmjit-managed relocated original
  std::vector<uint8_t> originalBytes;
  size_t patchSize;
  std::function<void(std::string)> onEnter;
  std::function<void(std::string)> onLeave;
};

std::mutex hookMutex;
uint64_t nextHookId = 1;
std::unordered_map<uint64_t, HookEntry *> hooksById;
std::unordered_map<uint64_t, HookEntry *> hooksByTarget;

// ─── C dispatch functions ───
// Called from trampoline with (cpuContext*, hookEntry*)
// Catches all exceptions to prevent crashes.

extern "C" void chromatic_interceptor_dispatch(void *cpuContext,
                                               void *hookEntryPtr) {
  auto *entry = static_cast<HookEntry *>(hookEntryPtr);
  if (!entry)
    return;

  std::string ctxHex = toHexAddr(reinterpret_cast<uint64_t>(cpuContext));

  if (entry->onEnter) {
    try {
      entry->onEnter(ctxHex);
    } catch (const std::exception &e) {
      fprintf(stderr, "[chromatic] onEnter threw: %s\n", e.what());
    } catch (...) {
      fprintf(stderr, "[chromatic] onEnter threw unknown exception\n");
    }
  }
}

extern "C" void chromatic_interceptor_dispatch_leave(void *cpuContext,
                                                     void *hookEntryPtr) {
  auto *entry = static_cast<HookEntry *>(hookEntryPtr);
  if (!entry)
    return;

  std::string ctxHex = toHexAddr(reinterpret_cast<uint64_t>(cpuContext));

  if (entry->onLeave) {
    try {
      entry->onLeave(ctxHex);
    } catch (const std::exception &e) {
      fprintf(stderr, "[chromatic] onLeave threw: %s\n", e.what());
    } catch (...) {
      fprintf(stderr, "[chromatic] onLeave threw unknown exception\n");
    }
  }
}

} // anonymous namespace

namespace chromatic::js {

std::string
NativeInterceptor::attach(const std::string &targetStr,
                          std::function<void(std::string)> onEnter,
                          std::function<void(std::string)> onLeave) {
  uint64_t target = parseHexAddr(targetStr);

  std::lock_guard<std::mutex> lock(hookMutex);

  if (hooksByTarget.count(target))
    throw std::runtime_error("Already hooked at " + targetStr);

  auto *entry = new HookEntry();
  entry->target = target;
  entry->onEnter = std::move(onEnter);
  entry->onLeave = std::move(onLeave);
  entry->patchSize = internal::PATCH_SIZE;

  // 1. Save original bytes
  auto *targetPtr = reinterpret_cast<uint8_t *>(target);
  entry->originalBytes.resize(internal::PATCH_SIZE);
  std::memcpy(entry->originalBytes.data(), targetPtr, internal::PATCH_SIZE);

  // 2. Build relocated code (copies original instructions + jump-back)
  size_t bytesConsumed = 0;
  try {
    entry->relocatedCode = internal::buildRelocatedCode(
        target, internal::PATCH_SIZE, bytesConsumed);
  } catch (...) {
    delete entry;
    throw;
  }

  // 3. Build trampoline (save ctx → dispatch → restore → jump to relocated)
  try {
    entry->trampolineCode = internal::buildTrampoline(
        reinterpret_cast<internal::DispatchFn>(&chromatic_interceptor_dispatch),
        reinterpret_cast<internal::DispatchFn>(
            &chromatic_interceptor_dispatch_leave),
        entry, reinterpret_cast<uint64_t>(entry->relocatedCode));
  } catch (...) {
    internal::releaseCode(entry->relocatedCode);
    delete entry;
    throw;
  }

  // 4. Patch target to jump to trampoline
  uint8_t patchBuf[16];
  internal::generatePatchBytes(
      patchBuf, reinterpret_cast<uint64_t>(entry->trampolineCode));
  internal::makeWritableAndPatch(targetPtr, patchBuf, internal::PATCH_SIZE);

  // 5. Register
  uint64_t hookId = nextHookId++;
  hooksById[hookId] = entry;
  hooksByTarget[target] = entry;

  return toHexAddr(hookId);
}

void NativeInterceptor::detach(const std::string &hookIdStr) {
  uint64_t hookId = parseHexAddr(hookIdStr);

  std::lock_guard<std::mutex> lock(hookMutex);

  auto it = hooksById.find(hookId);
  if (it == hooksById.end())
    return;

  auto *entry = it->second;

  // Restore original bytes
  internal::makeWritableAndPatch(reinterpret_cast<void *>(entry->target),
                                 entry->originalBytes.data(), entry->patchSize);

  // Release asmjit-managed code
  internal::releaseCode(entry->trampolineCode);
  internal::releaseCode(entry->relocatedCode);

  hooksByTarget.erase(entry->target);
  hooksById.erase(it);
  delete entry;
}

void NativeInterceptor::detachAll() {
  std::lock_guard<std::mutex> lock(hookMutex);

  for (auto &[id, entry] : hooksById) {
    internal::makeWritableAndPatch(reinterpret_cast<void *>(entry->target),
                                   entry->originalBytes.data(),
                                   entry->patchSize);
    internal::releaseCode(entry->trampolineCode);
    internal::releaseCode(entry->relocatedCode);
    delete entry;
  }
  hooksById.clear();
  hooksByTarget.clear();
}

std::string NativeInterceptor::replace(const std::string &targetStr,
                                       const std::string &replacementStr) {
  uint64_t target = parseHexAddr(targetStr);
  uint64_t replacement = parseHexAddr(replacementStr);

  std::lock_guard<std::mutex> lock(hookMutex);

  if (hooksByTarget.count(target))
    throw std::runtime_error("Already hooked at " + targetStr);

  auto *entry = new HookEntry();
  entry->target = target;
  entry->patchSize = internal::PATCH_SIZE;
  entry->trampolineCode = nullptr;

  // Save original bytes
  auto *targetPtr = reinterpret_cast<uint8_t *>(target);
  entry->originalBytes.resize(internal::PATCH_SIZE);
  std::memcpy(entry->originalBytes.data(), targetPtr, internal::PATCH_SIZE);

  // Build relocated code (= trampoline to call original)
  size_t bytesConsumed = 0;
  try {
    entry->relocatedCode = internal::buildRelocatedCode(
        target, internal::PATCH_SIZE, bytesConsumed);
  } catch (...) {
    delete entry;
    throw;
  }

  // Patch target to jump to replacement
  uint8_t patchBuf[16];
  internal::generatePatchBytes(patchBuf, replacement);
  internal::makeWritableAndPatch(targetPtr, patchBuf, internal::PATCH_SIZE);

  uint64_t hookId = nextHookId++;
  hooksById[hookId] = entry;
  hooksByTarget[target] = entry;

  return toHexAddr(reinterpret_cast<uint64_t>(entry->relocatedCode));
}

void NativeInterceptor::revert(const std::string &targetStr) {
  uint64_t target = parseHexAddr(targetStr);

  std::lock_guard<std::mutex> lock(hookMutex);

  auto it = hooksByTarget.find(target);
  if (it == hooksByTarget.end())
    return;

  auto *entry = it->second;
  internal::makeWritableAndPatch(reinterpret_cast<void *>(entry->target),
                                 entry->originalBytes.data(), entry->patchSize);

  internal::releaseCode(entry->trampolineCode);
  internal::releaseCode(entry->relocatedCode);

  // Find in hooksById
  for (auto jt = hooksById.begin(); jt != hooksById.end(); ++jt) {
    if (jt->second == entry) {
      hooksById.erase(jt);
      break;
    }
  }
  hooksByTarget.erase(it);
  delete entry;
}

} // namespace chromatic::js
