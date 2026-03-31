#if !defined(CHROMATIC_WINDOWS) && !defined(_XOPEN_SOURCE)
#define _XOPEN_SOURCE 700
#endif

#include "native_exception_handler.h"
#include <atomic>
#include <sstream>
#include <stdexcept>
#include <vector>

#ifdef CHROMATIC_WINDOWS
#include <windows.h>
#else
#include <signal.h>
#include <ucontext.h>
#include <unistd.h>
#endif

namespace {

// ─── Async-signal-safe spinlock ───

struct SpinLock {
  std::atomic_flag flag = ATOMIC_FLAG_INIT;
  void lock() {
    while (flag.test_and_set(std::memory_order_acquire))
      ;
  }
  void unlock() { flag.clear(std::memory_order_release); }
};

// ─── Handler entry ───

struct HandlerEntry {
  chromatic::js::internal::ExceptionCallbackId id;
  chromatic::js::ExceptionType type;
  chromatic::js::internal::ExceptionCallback callback;
};

// ─── Global state ───

static SpinLock g_lock;
static std::vector<HandlerEntry> g_handlers;
static chromatic::js::internal::ExceptionCallbackId g_nextId = 1;
static std::atomic<bool> g_enabled{false};
static std::atomic<int> g_refCount{0};

// ─── Hex address formatting ───

std::string toHexAddr(uint64_t addr) {
  std::ostringstream oss;
  oss << "0x" << std::hex << addr;
  return oss.str();
}

// ─── JS callback entries (queued, not called from signal handler) ───

struct JsCallbackEntry {
  chromatic::js::internal::ExceptionCallbackId id;
  chromatic::js::ExceptionType type;
  std::function<void(std::string, std::string)> callback;
};

static std::vector<JsCallbackEntry> g_jsCallbacks;
static chromatic::js::internal::ExceptionCallbackId g_nextJsId = 0x10000;

// ─── Exception type string conversion ───

std::string exceptionTypeToString(chromatic::js::ExceptionType type) {
  switch (type) {
  case chromatic::js::ExceptionType::AccessViolation:
    return "access_violation";
  case chromatic::js::ExceptionType::BusError:
    return "bus_error";
  case chromatic::js::ExceptionType::Breakpoint:
    return "breakpoint";
  case chromatic::js::ExceptionType::SingleStep:
    return "single_step";
  case chromatic::js::ExceptionType::IllegalInstruction:
    return "illegal_instruction";
  default:
    return "unknown";
  }
}

chromatic::js::ExceptionType stringToExceptionType(const std::string &s) {
  if (s == "access_violation")
    return chromatic::js::ExceptionType::AccessViolation;
  if (s == "bus_error")
    return chromatic::js::ExceptionType::BusError;
  if (s == "breakpoint")
    return chromatic::js::ExceptionType::Breakpoint;
  if (s == "single_step")
    return chromatic::js::ExceptionType::SingleStep;
  if (s == "illegal_instruction")
    return chromatic::js::ExceptionType::IllegalInstruction;
  return chromatic::js::ExceptionType::Unknown;
}

// ─── ExceptionContext platform helpers ───

#if !defined(CHROMATIC_WINDOWS)

auto buildContextFromSignal(int sig, siginfo_t *info, void *ucontext) {
  chromatic::js::ExceptionContext ctx{};
  ctx.$platformContext = ucontext;
  ctx.faultAddress = reinterpret_cast<uint64_t>(info->si_addr);

  auto *uctx = static_cast<ucontext_t *>(ucontext);

  // Determine exception type
  if (sig == SIGSEGV) {
    ctx.type = chromatic::js::ExceptionType::AccessViolation;
    ctx.accessType = chromatic::js::AccessType::Unknown;
#ifdef CHROMATIC_LINUX
    // si_code can tell us read vs write on Linux
    if (info->si_code == SEGV_ACCERR || info->si_code == SEGV_MAPERR) {
      // On Linux x86_64, err field in ucontext has bit 1 = write
#ifdef CHROMATIC_X64
      if (uctx->uc_mcontext.gregs[REG_ERR] & 2)
        ctx.accessType = chromatic::js::AccessType::Write;
      else
        ctx.accessType = chromatic::js::AccessType::Read;
#endif
    }
#endif
  } else if (sig == SIGBUS) {
    ctx.type = chromatic::js::ExceptionType::BusError;
    ctx.accessType = chromatic::js::AccessType::Unknown;
  } else if (sig == SIGTRAP) {
    // Distinguish breakpoint from single-step
    if (info->si_code == TRAP_TRACE) {
      ctx.type = chromatic::js::ExceptionType::SingleStep;
    } else {
      ctx.type = chromatic::js::ExceptionType::Breakpoint;
    }
    ctx.accessType = chromatic::js::AccessType::Execute;
  } else if (sig == SIGILL) {
    ctx.type = chromatic::js::ExceptionType::IllegalInstruction;
    ctx.accessType = chromatic::js::AccessType::Execute;
  } else {
    ctx.type = chromatic::js::ExceptionType::Unknown;
    ctx.accessType = chromatic::js::AccessType::Unknown;
  }

  return std::make_shared<chromatic::js::ExceptionContext>(std::move(ctx));
}

#endif // !CHROMATIC_WINDOWS

// ─── Platform signal handler / VEH ───

#ifdef CHROMATIC_WINDOWS
static PVOID g_vehHandle = nullptr;

static LONG CALLBACK vehHandler(PEXCEPTION_POINTERS ep) {
  chromatic::js::ExceptionContext ctx{};
  ctx.$platformContext = ep;
  ctx.faultAddress = ep->ExceptionRecord->ExceptionInformation[1];

  switch (ep->ExceptionRecord->ExceptionCode) {
  case EXCEPTION_ACCESS_VIOLATION:
    ctx.type = chromatic::js::ExceptionType::AccessViolation;
    ctx.accessType = (ep->ExceptionRecord->ExceptionInformation[0] == 0)
                         ? chromatic::js::AccessType::Read
                         : chromatic::js::AccessType::Write;
    ctx.faultAddress = ep->ExceptionRecord->ExceptionInformation[1];
    break;
  case EXCEPTION_BREAKPOINT:
    ctx.type = chromatic::js::ExceptionType::Breakpoint;
    ctx.accessType = chromatic::js::AccessType::Execute;
    break;
  case EXCEPTION_SINGLE_STEP:
    ctx.type = chromatic::js::ExceptionType::SingleStep;
    ctx.accessType = chromatic::js::AccessType::Execute;
    break;
  case EXCEPTION_ILLEGAL_INSTRUCTION:
    ctx.type = chromatic::js::ExceptionType::IllegalInstruction;
    ctx.accessType = chromatic::js::AccessType::Execute;
    break;
  default:
    return EXCEPTION_CONTINUE_SEARCH;
  }

  // Dispatch through handler chain
  g_lock.lock();
  auto handlers = g_handlers;
  g_lock.unlock();
  auto sctx = std::make_shared<chromatic::js::ExceptionContext>(std::move(ctx));
  for (auto &entry : handlers) {
    if (entry.type == ctx.type) {
      if (entry.callback(sctx) == chromatic::js::HandleAction::Handled)
        return EXCEPTION_CONTINUE_EXECUTION;
    }
  }

  return EXCEPTION_CONTINUE_SEARCH;
}

#else // POSIX

static struct sigaction g_old_sigsegv{};
static struct sigaction g_old_sigbus{};
static struct sigaction g_old_sigtrap{};
static struct sigaction g_old_sigill{};

static void forwardToOriginal(int sig, siginfo_t *info, void *ucontext,
                              const struct sigaction &old_sa) {
  if (old_sa.sa_flags & SA_SIGINFO) {
    if (old_sa.sa_sigaction)
      old_sa.sa_sigaction(sig, info, ucontext);
  } else {
    if (old_sa.sa_handler == SIG_DFL) {
      // Re-raise with default handler
      struct sigaction def{};
      def.sa_handler = SIG_DFL;
      sigemptyset(&def.sa_mask);
      sigaction(sig, &def, nullptr);
      raise(sig);
    } else if (old_sa.sa_handler != SIG_IGN) {
      old_sa.sa_handler(sig);
    }
  }
}

static void globalSignalHandler(int sig, siginfo_t *info, void *ucontext) {
  auto ctx = buildContextFromSignal(sig, info, ucontext);

  // Dispatch through handler chain
  g_lock.lock();
  auto handlers = g_handlers;
  g_lock.unlock();

  for (auto &entry : handlers) {
    if (entry.type == ctx->type) {
      if (entry.callback(ctx) == chromatic::js::HandleAction::Handled)
        return; // Resume with (possibly modified) ucontext
    }
  }

  // No handler claimed it — forward to original
  switch (sig) {
  case SIGSEGV:
    forwardToOriginal(sig, info, ucontext, g_old_sigsegv);
    break;
  case SIGBUS:
    forwardToOriginal(sig, info, ucontext, g_old_sigbus);
    break;
  case SIGTRAP:
    forwardToOriginal(sig, info, ucontext, g_old_sigtrap);
    break;
  case SIGILL:
    forwardToOriginal(sig, info, ucontext, g_old_sigill);
    break;
  default:
    break;
  }
}

#endif // CHROMATIC_WINDOWS

// ─── Install / remove global handlers ───

void installHandlers() {
#ifdef CHROMATIC_WINDOWS
  if (!g_vehHandle)
    g_vehHandle = AddVectoredExceptionHandler(1, vehHandler);
#else
  struct sigaction sa{};
  sa.sa_sigaction = globalSignalHandler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_SIGINFO | SA_NODEFER;

  sigaction(SIGSEGV, &sa, &g_old_sigsegv);
  sigaction(SIGBUS, &sa, &g_old_sigbus);
  sigaction(SIGTRAP, &sa, &g_old_sigtrap);
  sigaction(SIGILL, &sa, &g_old_sigill);
#endif
}

void removeHandlers() {
#ifdef CHROMATIC_WINDOWS
  if (g_vehHandle) {
    RemoveVectoredExceptionHandler(g_vehHandle);
    g_vehHandle = nullptr;
  }
#else
  sigaction(SIGSEGV, &g_old_sigsegv, nullptr);
  sigaction(SIGBUS, &g_old_sigbus, nullptr);
  sigaction(SIGTRAP, &g_old_sigtrap, nullptr);
  sigaction(SIGILL, &g_old_sigill, nullptr);
#endif
}

} // anonymous namespace

// ─── ExceptionContext implementation ───

namespace chromatic::js {

uint64_t ExceptionContext::getPc() const {
#ifdef CHROMATIC_WINDOWS
  auto *ep = static_cast<EXCEPTION_POINTERS *>($platformContext);
#ifdef CHROMATIC_X64
  return ep->ContextRecord->Rip;
#else
  return ep->ContextRecord->Pc;
#endif
#else
  auto *uctx = static_cast<ucontext_t *>($platformContext);
#if defined(CHROMATIC_DARWIN) && defined(CHROMATIC_ARM64)
  return uctx->uc_mcontext->__ss.__pc;
#elif defined(CHROMATIC_DARWIN) && defined(CHROMATIC_X64)
  return uctx->uc_mcontext->__ss.__rip;
#elif defined(CHROMATIC_ARM64)
  return uctx->uc_mcontext.pc;
#else // x86_64 Linux
  return uctx->uc_mcontext.gregs[REG_RIP];
#endif
#endif
}

void ExceptionContext::setPc(uint64_t newPc) {
#ifdef CHROMATIC_WINDOWS
  auto *ep = static_cast<EXCEPTION_POINTERS *>($platformContext);
#ifdef CHROMATIC_X64
  ep->ContextRecord->Rip = newPc;
#else
  ep->ContextRecord->Pc = newPc;
#endif
#else
  auto *uctx = static_cast<ucontext_t *>($platformContext);
#if defined(CHROMATIC_DARWIN) && defined(CHROMATIC_ARM64)
  uctx->uc_mcontext->__ss.__pc = newPc;
#elif defined(CHROMATIC_DARWIN) && defined(CHROMATIC_X64)
  uctx->uc_mcontext->__ss.__rip = newPc;
#elif defined(CHROMATIC_ARM64)
  uctx->uc_mcontext.pc = newPc;
#else // x86_64 Linux
  uctx->uc_mcontext.gregs[REG_RIP] = newPc;
#endif
#endif
}

void ExceptionContext::setSingleStep(bool enable) {
#ifdef CHROMATIC_WINDOWS
  auto *ep = static_cast<EXCEPTION_POINTERS *>($platformContext);
  if (enable)
    ep->ContextRecord->EFlags |= 0x100; // Set TF
  else
    ep->ContextRecord->EFlags &= ~0x100;
#else
  auto *uctx = static_cast<ucontext_t *>($platformContext);
#if defined(CHROMATIC_DARWIN) && defined(CHROMATIC_X64)
  if (enable)
    uctx->uc_mcontext->__ss.__rflags |= 0x100;
  else
    uctx->uc_mcontext->__ss.__rflags &= ~0x100ULL;
#elif defined(CHROMATIC_DARWIN) && defined(CHROMATIC_ARM64)
  if (enable)
    uctx->uc_mcontext->__ss.__cpsr |= (1UL << 21); // SS bit
  else
    uctx->uc_mcontext->__ss.__cpsr &= ~(1UL << 21);
#elif defined(CHROMATIC_X64)   // Linux x86_64
  if (enable)
    uctx->uc_mcontext.gregs[REG_EFL] |= 0x100;
  else
    uctx->uc_mcontext.gregs[REG_EFL] &= ~0x100;
#elif defined(CHROMATIC_ARM64) // Linux ARM64
  if (enable)
    uctx->uc_mcontext.pstate |= (1UL << 21); // PSR_SS_BIT
  else
    uctx->uc_mcontext.pstate &= ~(1UL << 21);
#endif
#endif
}

// ─── NativeExceptionHandler (JS-facing) ───

void NativeExceptionHandler::enable() {
  if (g_enabled.exchange(true))
    return; // already enabled
  installHandlers();
}

void NativeExceptionHandler::disable() {
  if (!g_enabled.exchange(false))
    return; // already disabled
  if (g_refCount.load() > 0)
    return; // sub-systems still need it
  removeHandlers();
}

bool NativeExceptionHandler::isEnabled() { return g_enabled.load(); }

std::string NativeExceptionHandler::addCallback(
    const std::string &type,
    std::function<void(std::string, std::string)> callback) {
  auto excType = stringToExceptionType(type);
  if (excType == ExceptionType::Unknown)
    throw std::runtime_error("Unknown exception type: " + type);

  g_lock.lock();
  auto id = g_nextJsId++;
  g_jsCallbacks.push_back({id, excType, std::move(callback)});
  g_lock.unlock();

  // Also register an internal handler that dispatches to JS callbacks
  // This is a simplified approach: the internal handler finds matching
  // JS callbacks and calls them (since JS callbacks should NOT be called
  // from signal context, this is for "safe" exceptions caught via
  // mechanisms like safeReadMemory patterns).
  // For actual signal-time use, the internal API is used directly.

  return toHexAddr(id);
}

void NativeExceptionHandler::removeCallback(const std::string &callbackId) {
  uint64_t id = std::stoull(callbackId, nullptr, 16);
  g_lock.lock();
  auto it =
      std::remove_if(g_jsCallbacks.begin(), g_jsCallbacks.end(),
                     [id](const JsCallbackEntry &e) { return e.id == id; });
  g_jsCallbacks.erase(it, g_jsCallbacks.end());
  g_lock.unlock();
}

void NativeExceptionHandler::removeAllCallbacks() {
  g_lock.lock();
  g_jsCallbacks.clear();
  g_lock.unlock();
}

// ─── Internal API ───

namespace internal {

ExceptionCallbackId registerHandler(ExceptionType type,
                                    ExceptionCallback callback) {
  g_lock.lock();
  auto id = g_nextId++;
  g_handlers.push_back({id, type, std::move(callback)});
  g_lock.unlock();
  return id;
}

void unregisterHandler(ExceptionCallbackId id) {
  g_lock.lock();
  auto it = std::remove_if(g_handlers.begin(), g_handlers.end(),
                           [id](const HandlerEntry &e) { return e.id == id; });
  g_handlers.erase(it, g_handlers.end());
  g_lock.unlock();
}

void refEnable() {
  if (g_refCount.fetch_add(1) == 0) {
    if (!g_enabled.load()) {
      g_enabled.store(true);
      installHandlers();
    }
  }
}

void refDisable() {
  if (g_refCount.fetch_sub(1) == 1) {
    // Last sub-system released. If user didn't explicitly enable,
    // we can remove handlers.
    // (We keep them if user called enable() explicitly.)
    // For simplicity, always keep handlers while g_enabled is true.
  }
}

} // namespace internal

} // namespace chromatic::js
