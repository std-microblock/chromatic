#include "native_breakpoint.h"
#include "internal/code_relocator.h"
#include "native_exception_handler.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
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

// ─── Breakpoint entry ───

struct BreakpointEntry {
  uint64_t address;
  std::vector<uint8_t> originalBytes;
  size_t originalInsnSize;
  void *trampolineCode; // asmjit-managed
  void *relocatedCode;  // asmjit-managed
  std::function<void(std::string)> onHit;
};

static std::mutex g_bpMutex;
static uint64_t g_nextBpId = 1;
static std::unordered_map<uint64_t, BreakpointEntry *> g_bpById;
static std::unordered_map<uint64_t, BreakpointEntry *> g_bpByAddress;
static chromatic::js::internal::ExceptionCallbackId g_sigtrapHandlerId = 0;
static bool g_handlerInstalled = false;

// ─── Breakpoint instruction sizes ───

#ifdef CHROMATIC_ARM64
static constexpr size_t BP_INSN_SIZE = 4; // BRK #1
#else
static constexpr size_t BP_INSN_SIZE = 1; // INT3
#endif

// ─── Dispatch function ───
// Called from trampoline in NORMAL execution context (not from signal).

extern "C" void chromatic_bp_dispatch(void *cpuContext, void *bpEntryPtr) {
  auto *entry = static_cast<BreakpointEntry *>(bpEntryPtr);
  if (!entry || !entry->onHit)
    return;

  std::string ctxHex = toHexAddr(reinterpret_cast<uint64_t>(cpuContext));
  try {
    entry->onHit(ctxHex);
  } catch (const std::exception &e) {
    fprintf(stderr, "[chromatic] breakpoint onHit threw: %s\n", e.what());
  } catch (...) {
    fprintf(stderr, "[chromatic] breakpoint onHit threw unknown exception\n");
  }
}

// ─── SIGTRAP handler (runs in signal context) ───
// Only redirects PC to trampoline. NO JS calls here.

chromatic::js::HandleAction
breakpointSigtrapHandler(std::shared_ptr<chromatic::js::ExceptionContext> ctx) {
  uint64_t bpAddr = ctx->getPc();
#if defined(CHROMATIC_X64) && !defined(CHROMATIC_WINDOWS)
  // INT3 advances RIP by 1, so the actual BP address is pc - 1
  bpAddr -= 1;
#endif
  // ARM64: BRK does not advance PC, so bpAddr == ctx.pc

  // We don't hold the mutex here (signal context). We access g_bpByAddress
  // which is modified only under g_bpMutex from normal context. This is a
  // trade-off: we rely on the map not being modified concurrently during
  // signal delivery on the same thread. This is safe because the thread
  // that hit the BP is stopped.
  auto it = g_bpByAddress.find(bpAddr);
  if (it == g_bpByAddress.end())
    return chromatic::js::HandleAction::NotHandled;

  auto *entry = it->second;

  // Redirect PC to the trampoline
  ctx->setPc(reinterpret_cast<uint64_t>(entry->trampolineCode));
  return chromatic::js::HandleAction::Handled;
}

void ensureHandlerInstalled() {
  if (g_handlerInstalled)
    return;
  chromatic::js::internal::refEnable();
  g_sigtrapHandlerId = chromatic::js::internal::registerHandler(
      chromatic::js::ExceptionType::Breakpoint, breakpointSigtrapHandler);
  g_handlerInstalled = true;
}

void maybeRemoveHandler() {
  if (!g_handlerInstalled || !g_bpByAddress.empty())
    return;
  chromatic::js::internal::unregisterHandler(g_sigtrapHandlerId);
  chromatic::js::internal::refDisable();
  g_handlerInstalled = false;
  g_sigtrapHandlerId = 0;
}

} // anonymous namespace

namespace chromatic::js {

namespace cr = ::chromatic::internal; // alias for code_relocator namespace

std::string
NativeSoftwareBreakpoint::set(const std::string &addressStr,
                              std::function<void(std::string)> onHit) {
  uint64_t address = parseHexAddr(addressStr);

  std::lock_guard<std::mutex> lock(g_bpMutex);

  if (g_bpByAddress.count(address))
    throw std::runtime_error("Software breakpoint already set at " +
                             addressStr);

  ensureHandlerInstalled();

  auto *entry = new BreakpointEntry();
  entry->address = address;
  entry->onHit = std::move(onHit);

  auto *targetPtr = reinterpret_cast<uint8_t *>(address);

  // 1. Save original byte(s) at the target
  entry->originalBytes.resize(BP_INSN_SIZE);
  std::memcpy(entry->originalBytes.data(), targetPtr, BP_INSN_SIZE);

  // We need to relocate at least BP_INSN_SIZE bytes. On x86_64 that's just
  // 1 byte (the INT3), but we need to relocate the entire first instruction
  // (which may be larger). On ARM64, BRK replaces exactly one 4-byte insn.
  // For the trampoline, we need to relocate enough bytes for the original
  // instruction to be re-executed. We use PATCH_SIZE to be safe (same as
  // Interceptor), but we only OVERWRITE BP_INSN_SIZE bytes.
  // Actually, for software BP we only overwrite BP_INSN_SIZE bytes at the
  // target address. The relocated code must re-execute the original
  // instruction(s) that were at the BP address. We relocate starting from
  // the BP address for at least BP_INSN_SIZE bytes.
  size_t bytesConsumed = 0;
  try {
    entry->relocatedCode =
        cr::buildRelocatedCode(address, BP_INSN_SIZE, bytesConsumed);
    entry->originalInsnSize = bytesConsumed;
  } catch (...) {
    delete entry;
    throw;
  }

  // 2. Build trampoline: save regs → call dispatch → restore → jump to
  //    relocated code
  try {
    entry->trampolineCode = cr::buildTrampoline(
        reinterpret_cast<cr::DispatchFn>(&chromatic_bp_dispatch),
        nullptr, // no onLeave
        entry, reinterpret_cast<uint64_t>(entry->relocatedCode));
  } catch (...) {
    cr::releaseCode(entry->relocatedCode);
    delete entry;
    throw;
  }

  // 3. Write breakpoint instruction over the target
  uint8_t bpInsn[4];
#ifdef CHROMATIC_ARM64
  // BRK #1 = 0xD4200020
  uint32_t brk = 0xD4200020;
  std::memcpy(bpInsn, &brk, 4);
#else
  // INT3 = 0xCC
  bpInsn[0] = 0xCC;
#endif
  cr::makeWritableAndPatch(targetPtr, bpInsn, BP_INSN_SIZE);

  // 4. Register
  uint64_t bpId = g_nextBpId++;
  g_bpById[bpId] = entry;
  g_bpByAddress[address] = entry;

  return toHexAddr(bpId);
}

void NativeSoftwareBreakpoint::remove(const std::string &breakpointIdStr) {
  uint64_t bpId = parseHexAddr(breakpointIdStr);

  std::lock_guard<std::mutex> lock(g_bpMutex);

  auto it = g_bpById.find(bpId);
  if (it == g_bpById.end())
    return;

  auto *entry = it->second;

  // Restore original bytes
  cr::makeWritableAndPatch(reinterpret_cast<void *>(entry->address),
                           entry->originalBytes.data(), BP_INSN_SIZE);

  // Release JIT code
  cr::releaseCode(entry->trampolineCode);
  cr::releaseCode(entry->relocatedCode);

  g_bpByAddress.erase(entry->address);
  g_bpById.erase(it);
  delete entry;

  maybeRemoveHandler();
}

void NativeSoftwareBreakpoint::removeAll() {
  std::lock_guard<std::mutex> lock(g_bpMutex);

  for (auto &[id, entry] : g_bpById) {
    cr::makeWritableAndPatch(reinterpret_cast<void *>(entry->address),
                             entry->originalBytes.data(), BP_INSN_SIZE);
    cr::releaseCode(entry->trampolineCode);
    cr::releaseCode(entry->relocatedCode);
    delete entry;
  }
  g_bpById.clear();
  g_bpByAddress.clear();

  maybeRemoveHandler();
}

} // namespace chromatic::js
