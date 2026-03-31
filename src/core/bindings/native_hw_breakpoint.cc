#include "native_hw_breakpoint.h"
#include "native_pointer.h"
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

#ifdef CHROMATIC_WINDOWS
#include <windows.h>
#elif defined(CHROMATIC_DARWIN)
#include <mach/mach.h>
#include <mach/thread_act.h>
#include <pthread.h>
#ifdef CHROMATIC_X64
#include <mach/i386/thread_status.h>
#endif
#elif defined(CHROMATIC_LINUX) || defined(CHROMATIC_ANDROID)
#include <linux/hw_breakpoint.h>
#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace {

uint64_t parseHexAddr(const std::string &s) {
  return std::stoull(s, nullptr, 16);
}

std::string toHexAddr(uint64_t addr) {
  std::ostringstream oss;
  oss << "0x" << std::hex << addr;
  return oss.str();
}

// ─── HW breakpoint type ───

enum class HwBpType { Execute, Write, ReadWrite };

HwBpType parseType(const std::string &s) {
  if (s == "execute")
    return HwBpType::Execute;
  if (s == "write")
    return HwBpType::Write;
  if (s == "readwrite")
    return HwBpType::ReadWrite;
  throw std::runtime_error("Invalid hardware breakpoint type: " + s +
                           ". Use 'execute', 'write', or 'readwrite'");
}

// ─── HW breakpoint entry ───

struct HwBreakpointEntry {
  uint64_t address;
  HwBpType type;
  int size;
  std::function<void(std::string)> onHit;

  // Platform-specific handles
#if defined(CHROMATIC_LINUX) || defined(CHROMATIC_ANDROID)
  int perfFd = -1;
#elif defined(CHROMATIC_DARWIN) && defined(CHROMATIC_X64)
  int slot = -1; // DR0-DR3 slot index
#elif defined(CHROMATIC_WINDOWS)
  int slot = -1;
#endif

  // For execute-type, we need a trampoline
  void *trampolineCode = nullptr;
  void *relocatedCode = nullptr;
  std::vector<uint8_t> originalBytes;

  chromatic::js::internal::ExceptionCallbackId handlerId = 0;
};

static std::mutex g_hwMutex;
static uint64_t g_nextHwId = 1;
static std::unordered_map<uint64_t, HwBreakpointEntry *> g_hwById;
static std::unordered_map<uint64_t, HwBreakpointEntry *> g_hwByAddress;

// ─── Platform max slots ───

static int platformMaxBreakpoints() {
#if defined(CHROMATIC_DARWIN) && defined(CHROMATIC_ARM64)
  return 0; // Not supported on macOS ARM64
#elif defined(CHROMATIC_X64)
  return 4; // DR0-DR3
#elif defined(CHROMATIC_ARM64)
  return 4; // Typical ARM64 HW BP count
#else
  return 0;
#endif
}

// ─── Dispatch (called from trampoline, normal context) ───

extern "C" void chromatic_hwbp_dispatch(void *cpuContext, void *entryPtr) {
  auto *entry = static_cast<HwBreakpointEntry *>(entryPtr);
  if (!entry || !entry->onHit)
    return;
  std::string ctxHex = toHexAddr(reinterpret_cast<uint64_t>(cpuContext));
  try {
    entry->onHit(ctxHex);
  } catch (const std::exception &e) {
    fprintf(stderr, "[chromatic] hw breakpoint onHit threw: %s\n", e.what());
  } catch (...) {
    fprintf(stderr,
            "[chromatic] hw breakpoint onHit threw unknown exception\n");
  }
}

// ─── Platform: set/clear HW breakpoint ───

#if defined(CHROMATIC_LINUX) || defined(CHROMATIC_ANDROID)

static int linuxSetHwBp(uint64_t addr, HwBpType type, int bpSize) {
  struct perf_event_attr pe{};
  pe.type = PERF_TYPE_BREAKPOINT;
  pe.size = sizeof(pe);

  switch (type) {
  case HwBpType::Execute:
    pe.bp_type = HW_BREAKPOINT_X;
    pe.bp_len = sizeof(long);
    break;
  case HwBpType::Write:
    pe.bp_type = HW_BREAKPOINT_W;
    pe.bp_len = bpSize;
    break;
  case HwBpType::ReadWrite:
    pe.bp_type = HW_BREAKPOINT_RW;
    pe.bp_len = bpSize;
    break;
  }

  pe.bp_addr = addr;
  pe.disabled = 1;
  pe.exclude_kernel = 1;
  pe.exclude_hv = 1;
  pe.sigtrap = 1; // Deliver as SIGTRAP

  int fd = static_cast<int>(
      syscall(SYS_perf_event_open, &pe, 0, -1, -1, PERF_FLAG_FD_CLOEXEC));
  if (fd < 0)
    throw std::runtime_error("perf_event_open failed for hardware breakpoint");

  ioctl(fd, PERF_EVENT_IOC_ENABLE, 0);
  return fd;
}

static void linuxClearHwBp(int fd) {
  if (fd >= 0) {
    ioctl(fd, PERF_EVENT_IOC_DISABLE, 0);
    close(fd);
  }
}

#elif defined(CHROMATIC_DARWIN) && defined(CHROMATIC_X64)

// macOS x86_64: manipulate debug registers via Mach thread API
static int g_usedSlots = 0; // bitmask of used DR0-DR3

static int findFreeSlot() {
  for (int i = 0; i < 4; i++) {
    if (!(g_usedSlots & (1 << i)))
      return i;
  }
  return -1;
}

static void darwinSetDebugReg(int slot, uint64_t addr, HwBpType type,
                              int bpSize) {
  thread_t thread = pthread_mach_thread_np(pthread_self());
  x86_debug_state64_t dbg{};
  mach_msg_type_number_t count = x86_DEBUG_STATE64_COUNT;
  thread_get_state(thread, x86_DEBUG_STATE64,
                   reinterpret_cast<thread_state_t>(&dbg), &count);

  // Set address register
  switch (slot) {
  case 0:
    dbg.__dr0 = addr;
    break;
  case 1:
    dbg.__dr1 = addr;
    break;
  case 2:
    dbg.__dr2 = addr;
    break;
  case 3:
    dbg.__dr3 = addr;
    break;
  }

  // DR7 control bits for this slot:
  // Bits [2*slot]: local enable
  // Bits [16+4*slot..17+4*slot]: type (00=execute, 01=write, 11=rw)
  // Bits [18+4*slot..19+4*slot]: len (00=1, 01=2, 11=4 on x86, 10=8)
  uint64_t dr7 = dbg.__dr7;

  // Enable local breakpoint
  dr7 |= (1ULL << (slot * 2));

  // Set type
  uint64_t typeVal = 0;
  switch (type) {
  case HwBpType::Execute:
    typeVal = 0;
    break;
  case HwBpType::Write:
    typeVal = 1;
    break;
  case HwBpType::ReadWrite:
    typeVal = 3;
    break;
  }
  dr7 &= ~(3ULL << (16 + slot * 4));
  dr7 |= (typeVal << (16 + slot * 4));

  // Set length
  uint64_t lenVal = 0;
  switch (bpSize) {
  case 1:
    lenVal = 0;
    break;
  case 2:
    lenVal = 1;
    break;
  case 4:
    lenVal = 3;
    break;
  case 8:
    lenVal = 2;
    break;
  default:
    lenVal = 0;
    break;
  }
  dr7 &= ~(3ULL << (18 + slot * 4));
  dr7 |= (lenVal << (18 + slot * 4));

  dbg.__dr7 = dr7;
  thread_set_state(thread, x86_DEBUG_STATE64,
                   reinterpret_cast<thread_state_t>(&dbg),
                   x86_DEBUG_STATE64_COUNT);
}

static void darwinClearDebugReg(int slot) {
  thread_t thread = pthread_mach_thread_np(pthread_self());
  x86_debug_state64_t dbg{};
  mach_msg_type_number_t count = x86_DEBUG_STATE64_COUNT;
  thread_get_state(thread, x86_DEBUG_STATE64,
                   reinterpret_cast<thread_state_t>(&dbg), &count);

  // Disable the slot
  dbg.__dr7 &= ~(1ULL << (slot * 2));
  // Clear address
  switch (slot) {
  case 0:
    dbg.__dr0 = 0;
    break;
  case 1:
    dbg.__dr1 = 0;
    break;
  case 2:
    dbg.__dr2 = 0;
    break;
  case 3:
    dbg.__dr3 = 0;
    break;
  }

  thread_set_state(thread, x86_DEBUG_STATE64,
                   reinterpret_cast<thread_state_t>(&dbg),
                   x86_DEBUG_STATE64_COUNT);
  g_usedSlots &= ~(1 << slot);
}

#elif defined(CHROMATIC_WINDOWS)

static int g_usedSlots = 0;

static int findFreeSlot() {
  for (int i = 0; i < 4; i++) {
    if (!(g_usedSlots & (1 << i)))
      return i;
  }
  return -1;
}

static void winSetDebugReg(int slot, uint64_t addr, HwBpType type, int bpSize) {
  CONTEXT ctx{};
  ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
  HANDLE thread = GetCurrentThread();
  GetThreadContext(thread, &ctx);

  switch (slot) {
  case 0:
    ctx.Dr0 = addr;
    break;
  case 1:
    ctx.Dr1 = addr;
    break;
  case 2:
    ctx.Dr2 = addr;
    break;
  case 3:
    ctx.Dr3 = addr;
    break;
  }

  DWORD64 dr7 = ctx.Dr7;
  dr7 |= (1ULL << (slot * 2));

  DWORD64 typeVal = 0;
  switch (type) {
  case HwBpType::Execute:
    typeVal = 0;
    break;
  case HwBpType::Write:
    typeVal = 1;
    break;
  case HwBpType::ReadWrite:
    typeVal = 3;
    break;
  }
  dr7 &= ~(3ULL << (16 + slot * 4));
  dr7 |= (typeVal << (16 + slot * 4));

  DWORD64 lenVal = 0;
  switch (bpSize) {
  case 1:
    lenVal = 0;
    break;
  case 2:
    lenVal = 1;
    break;
  case 4:
    lenVal = 3;
    break;
  case 8:
    lenVal = 2;
    break;
  }
  dr7 &= ~(3ULL << (18 + slot * 4));
  dr7 |= (lenVal << (18 + slot * 4));

  ctx.Dr7 = dr7;
  SetThreadContext(thread, &ctx);
}

static void winClearDebugReg(int slot) {
  CONTEXT ctx{};
  ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
  HANDLE thread = GetCurrentThread();
  GetThreadContext(thread, &ctx);

  ctx.Dr7 &= ~(1ULL << (slot * 2));
  switch (slot) {
  case 0:
    ctx.Dr0 = 0;
    break;
  case 1:
    ctx.Dr1 = 0;
    break;
  case 2:
    ctx.Dr2 = 0;
    break;
  case 3:
    ctx.Dr3 = 0;
    break;
  }

  SetThreadContext(thread, &ctx);
  g_usedSlots &= ~(1 << slot);
}

#endif

// ─── SIGTRAP handler for HW breakpoints ───

chromatic::js::HandleAction
hwBpSigtrapHandler(std::shared_ptr<chromatic::js::ExceptionContext> ctx) {
  // For execute breakpoints, check if PC matches a breakpoint address
  auto it = g_hwByAddress.find(ctx->getPc());
  if (it != g_hwByAddress.end()) {
    auto *entry = it->second;
    if (entry->type == HwBpType::Execute && entry->trampolineCode) {
      // Redirect to trampoline
      ctx->setPc(reinterpret_cast<uint64_t>(entry->trampolineCode));
      return chromatic::js::HandleAction::Handled;
    }
  }

  // For data watchpoints, we need to check DR6 to find which slot triggered
#if defined(CHROMATIC_DARWIN) && defined(CHROMATIC_X64)
  if (ctx->$platformContext) {
    // On macOS, debug registers are not in ucontext - need to read via Mach API
    thread_t thread = pthread_mach_thread_np(pthread_self());
    x86_debug_state64_t dbg{};
    mach_msg_type_number_t count = x86_DEBUG_STATE64_COUNT;
    kern_return_t kr =
        thread_get_state(thread, x86_DEBUG_STATE64,
                         reinterpret_cast<thread_state_t>(&dbg), &count);
    if (kr == KERN_SUCCESS) {
      uint64_t dr6 = dbg.__dr6;

      // Check each slot (DR0-DR3)
      for (int slot = 0; slot < 4; slot++) {
        if (dr6 & (1ULL << slot)) {
          // This slot triggered - find the entry
          for (auto &[addr, entry] : g_hwByAddress) {
            if (entry->slot == slot) {
              if (entry->onHit) {
                std::string ctxHex = toHexAddr(ctx->getPc());
                try {
                  entry->onHit(ctxHex);
                } catch (...) {
                }
              }
              return chromatic::js::HandleAction::Handled;
            }
          }
        }
      }
    }
  }
#elif defined(CHROMATIC_WINDOWS)
  if (ctx->$platformContext) {
    auto *ep = static_cast<PEXCEPTION_POINTERS>(ctx->$platformContext);
    uint64_t dr6 = ep->ContextRecord->Dr6;

    // Check each slot (DR0-DR3)
    for (int slot = 0; slot < 4; slot++) {
      if (dr6 & (1ULL << slot)) {
        // This slot triggered - find the entry
        for (auto &[addr, entry] : g_hwByAddress) {
          if (entry->slot == slot) {
            if (entry->onHit) {
              std::string ctxHex = toHexAddr(ctx->getPc());
              try {
                entry->onHit(ctxHex);
              } catch (...) {
              }
            }
            return chromatic::js::HandleAction::Handled;
          }
        }
      }
    }
  }
#endif

  return chromatic::js::HandleAction::NotHandled;
}

static chromatic::js::internal::ExceptionCallbackId g_hwHandlerId = 0;
static bool g_hwHandlerInstalled = false;

void ensureHwHandlerInstalled() {
  if (g_hwHandlerInstalled)
    return;
  chromatic::js::internal::refEnable();
  g_hwHandlerId = chromatic::js::internal::registerHandler(
      chromatic::js::ExceptionType::Breakpoint, hwBpSigtrapHandler);
  // Also register for SingleStep (HW debug exceptions on x86)
  chromatic::js::internal::registerHandler(
      chromatic::js::ExceptionType::SingleStep, hwBpSigtrapHandler);
  g_hwHandlerInstalled = true;
}

void maybeRemoveHwHandler() {
  if (!g_hwHandlerInstalled || !g_hwByAddress.empty())
    return;
  chromatic::js::internal::unregisterHandler(g_hwHandlerId);
  chromatic::js::internal::refDisable();
  g_hwHandlerInstalled = false;
  g_hwHandlerId = 0;
}

} // anonymous namespace

namespace chromatic::js {

namespace cr = ::chromatic::internal;

std::string
NativeHardwareBreakpoint::set(std::shared_ptr<NativePointer> address,
                              const std::string &typeStr, int size,
                              std::function<void(std::string)> onHit) {

#if defined(CHROMATIC_DARWIN) && defined(CHROMATIC_ARM64)
  throw std::runtime_error("Hardware breakpoints not supported on macOS ARM64. "
                           "Use SoftwareBreakpoint instead.");
#else
  auto type = parseType(typeStr);

  std::lock_guard<std::mutex> lock(g_hwMutex);

  if (g_hwByAddress.count(address->value()))
    throw std::runtime_error("Hardware breakpoint already set at " +
                             address->toString());

  int maxBp = platformMaxBreakpoints();
  if (static_cast<int>(g_hwByAddress.size()) >= maxBp)
    throw std::runtime_error("Maximum hardware breakpoints reached (" +
                             std::to_string(maxBp) + ")");

  ensureHwHandlerInstalled();

  auto *entry = new HwBreakpointEntry();
  entry->address = address->value();
  entry->type = type;
  entry->size = size;
  entry->onHit = std::move(onHit);

  // For execute-type HW breakpoints, build a trampoline so the original
  // instruction can be re-executed
  if (type == HwBpType::Execute) {
    size_t bytesConsumed = 0;
    try {
      entry->relocatedCode = cr::buildRelocatedCode(address->value(), 1, bytesConsumed);
    } catch (...) {
      delete entry;
      throw;
    }
    try {
      entry->trampolineCode = cr::buildTrampoline(
          reinterpret_cast<cr::DispatchFn>(&chromatic_hwbp_dispatch), nullptr,
          entry, reinterpret_cast<uint64_t>(entry->relocatedCode));
    } catch (...) {
      cr::releaseCode(entry->relocatedCode);
      delete entry;
      throw;
    }
  }

  // Set the HW breakpoint using platform API
#if defined(CHROMATIC_LINUX) || defined(CHROMATIC_ANDROID)
  try {
    entry->perfFd = linuxSetHwBp(address->value(), type, size);
  } catch (...) {
    if (entry->trampolineCode)
      cr::releaseCode(entry->trampolineCode);
    if (entry->relocatedCode)
      cr::releaseCode(entry->relocatedCode);
    delete entry;
    throw;
  }
#elif defined(CHROMATIC_DARWIN) && defined(CHROMATIC_X64)
  int slot = findFreeSlot();
  if (slot < 0) {
    if (entry->trampolineCode)
      cr::releaseCode(entry->trampolineCode);
    if (entry->relocatedCode)
      cr::releaseCode(entry->relocatedCode);
    delete entry;
    throw std::runtime_error("No free hardware breakpoint slots");
  }
  entry->slot = slot;
  g_usedSlots |= (1 << slot);
  darwinSetDebugReg(slot, address->value(), type, size);
#elif defined(CHROMATIC_WINDOWS)
  int slot = findFreeSlot();
  if (slot < 0) {
    if (entry->trampolineCode)
      cr::releaseCode(entry->trampolineCode);
    if (entry->relocatedCode)
      cr::releaseCode(entry->relocatedCode);
    delete entry;
    throw std::runtime_error("No free hardware breakpoint slots");
  }
  entry->slot = slot;
  g_usedSlots |= (1 << slot);
  winSetDebugReg(slot, address->value(), type, size);
#endif

  uint64_t bpId = g_nextHwId++;
  g_hwById[bpId] = entry;
  g_hwByAddress[address->value()] = entry;

  return toHexAddr(bpId);
#endif // !DARWIN ARM64
}

void NativeHardwareBreakpoint::remove(const std::string &breakpointIdStr) {
  uint64_t bpId = parseHexAddr(breakpointIdStr);

  std::lock_guard<std::mutex> lock(g_hwMutex);

  auto it = g_hwById.find(bpId);
  if (it == g_hwById.end())
    return;

  auto *entry = it->second;

#if defined(CHROMATIC_LINUX) || defined(CHROMATIC_ANDROID)
  linuxClearHwBp(entry->perfFd);
#elif defined(CHROMATIC_DARWIN) && defined(CHROMATIC_X64)
  darwinClearDebugReg(entry->slot);
#elif defined(CHROMATIC_WINDOWS)
  winClearDebugReg(entry->slot);
#endif

  if (entry->trampolineCode)
    cr::releaseCode(entry->trampolineCode);
  if (entry->relocatedCode)
    cr::releaseCode(entry->relocatedCode);

  g_hwByAddress.erase(entry->address);
  g_hwById.erase(it);
  delete entry;

  maybeRemoveHwHandler();
}

void NativeHardwareBreakpoint::removeAll() {
  std::lock_guard<std::mutex> lock(g_hwMutex);

  for (auto &[id, entry] : g_hwById) {
#if defined(CHROMATIC_LINUX) || defined(CHROMATIC_ANDROID)
    linuxClearHwBp(entry->perfFd);
#elif defined(CHROMATIC_DARWIN) && defined(CHROMATIC_X64)
    darwinClearDebugReg(entry->slot);
#elif defined(CHROMATIC_WINDOWS)
    winClearDebugReg(entry->slot);
#endif
    if (entry->trampolineCode)
      cr::releaseCode(entry->trampolineCode);
    if (entry->relocatedCode)
      cr::releaseCode(entry->relocatedCode);
    delete entry;
  }
  g_hwById.clear();
  g_hwByAddress.clear();

  maybeRemoveHwHandler();
}

int NativeHardwareBreakpoint::maxBreakpoints() {
  return platformMaxBreakpoints();
}

int NativeHardwareBreakpoint::activeCount() {
  std::lock_guard<std::mutex> lock(g_hwMutex);
  return static_cast<int>(g_hwById.size());
}

} // namespace chromatic::js
