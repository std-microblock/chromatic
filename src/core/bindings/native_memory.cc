#include "native_memory.h"
#include "native_pointer.h"
#include "native_process.h"
#include <async_simple/coro/Lazy.h>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <vector>

#ifdef CHROMATIC_WINDOWS
#include <windows.h>
#else
#include <setjmp.h>
#include <signal.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

#ifdef CHROMATIC_DARWIN
#include <libkern/OSCacheControl.h>
#include <mach/mach.h>
#endif

namespace {

#ifndef CHROMATIC_WINDOWS
// Safe memory read support using signal handling
thread_local sigjmp_buf safe_read_jmpbuf;
thread_local volatile bool safe_read_active = false;

void safe_read_signal_handler(int /*sig*/) {
  if (safe_read_active) {
    siglongjmp(safe_read_jmpbuf, 1);
  }
}
#endif

int protStringToFlags(const std::string &prot) {
#ifdef CHROMATIC_WINDOWS
  bool r = prot.find('r') != std::string::npos;
  bool w = prot.find('w') != std::string::npos;
  bool x = prot.find('x') != std::string::npos;
  if (r && w && x)
    return PAGE_EXECUTE_READWRITE;
  if (r && x)
    return PAGE_EXECUTE_READ;
  if (r && w)
    return PAGE_READWRITE;
  if (r)
    return PAGE_READONLY;
  if (x)
    return PAGE_EXECUTE;
  return PAGE_NOACCESS;
#else
  int flags = PROT_NONE;
  if (prot.find('r') != std::string::npos)
    flags |= PROT_READ;
  if (prot.find('w') != std::string::npos)
    flags |= PROT_WRITE;
  if (prot.find('x') != std::string::npos)
    flags |= PROT_EXEC;
  return flags;
#endif
}

#ifndef CHROMATIC_WINDOWS
std::string flagsToProtString(int flags) {
  std::string result;
  result += (flags & PROT_READ) ? 'r' : '-';
  result += (flags & PROT_WRITE) ? 'w' : '-';
  result += (flags & PROT_EXEC) ? 'x' : '-';
  return result;
}
#else
std::string flagsToProtString(DWORD flags) {
  switch (flags) {
  case PAGE_EXECUTE_READWRITE:
    return "rwx";
  case PAGE_EXECUTE_READ:
    return "r-x";
  case PAGE_READWRITE:
    return "rw-";
  case PAGE_READONLY:
    return "r--";
  case PAGE_EXECUTE:
    return "--x";
  case PAGE_EXECUTE_WRITECOPY:
    return "rwx";
  case PAGE_WRITECOPY:
    return "rw-";
  default:
    return "---";
  }
}
#endif

// ─── Pattern parser ───────────────────────────────────────────────
struct ParsedPattern {
  std::vector<uint8_t> bytes;
  std::vector<bool> mask; // true = must match, false = wildcard
};

ParsedPattern parsePattern(const std::string &pattern) {
  ParsedPattern p;
  std::istringstream iss(pattern);
  std::string token;
  while (iss >> token) {
    if (token == "??" || token == "?") {
      p.bytes.push_back(0);
      p.mask.push_back(false);
    } else {
      p.bytes.push_back(static_cast<uint8_t>(std::stoi(token, nullptr, 16)));
      p.mask.push_back(true);
    }
  }
  return p;
}

// ─── Boyer-Moore-Horspool with wildcard support ───────────────────
// Build bad-character shift table.  Wildcard positions are treated as
// "match anything", so they must not restrict the shift value.
std::vector<std::shared_ptr<chromatic::js::ScanMatch>>
bmhScan(const uint8_t *haystack, size_t haystackLen, const ParsedPattern &pat) {
  std::vector<std::shared_ptr<chromatic::js::ScanMatch>> results;
  const size_t m = pat.bytes.size();
  if (m == 0 || m > haystackLen)
    return results;

  // Bad-character shift table (classic Horspool)
  // Default shift = pattern length.
  // For each non-wildcard byte at position j (j < m-1):
  //   shift[byte] = min(shift[byte], m - 1 - j)
  // Wildcards at position j: every entry could be that position,
  //   so shift[*] = min(shift[*], m - 1 - j)
  size_t shift[256];
  for (int i = 0; i < 256; i++)
    shift[i] = m;

  for (size_t j = 0; j < m - 1; j++) {
    if (!pat.mask[j]) {
      // Wildcard — any byte could appear here ⇒ reduce all shifts
      size_t s = m - 1 - j;
      for (int c = 0; c < 256; c++) {
        if (shift[c] > s)
          shift[c] = s;
      }
    } else {
      size_t s = m - 1 - j;
      uint8_t b = pat.bytes[j];
      if (shift[b] > s)
        shift[b] = s;
    }
  }

  // If the last position is a wildcard, shift for any mismatch at end
  // position is 1 (safest).
  if (!pat.mask[m - 1]) {
    for (int c = 0; c < 256; c++) {
      if (shift[c] > 1)
        shift[c] = 1;
    }
  }

  // Search
  size_t i = 0;
  while (i + m <= haystackLen) {
    // Compare right to left
    bool match = true;
    for (size_t j = m; j-- > 0;) {
      if (pat.mask[j] && haystack[i + j] != pat.bytes[j]) {
        match = false;
        break;
      }
    }
    if (match) {
      auto matchAddr = reinterpret_cast<uint64_t>(haystack + i);
      results.push_back(std::make_shared<chromatic::js::ScanMatch>(
          chromatic::js::ScanMatch{
              std::make_shared<chromatic::js::NativePointer>(matchAddr),
              static_cast<int>(m)}));
      // Advance by 1 to find overlapping matches
      i += 1;
    } else {
      i += shift[haystack[i + m - 1]];
    }
  }
  return results;
}

} // namespace

namespace chromatic::js {

std::vector<uint8_t> NativeMemory::readMemory(std::shared_ptr<NativePointer> address,
                                               int size) {
  auto addr = reinterpret_cast<const uint8_t *>(address->value());
  return std::vector<uint8_t>(addr, addr + size);
}

std::vector<uint8_t> NativeMemory::safeReadMemory(std::shared_ptr<NativePointer> address,
                                                   int size) {
#ifdef CHROMATIC_WINDOWS
  auto addr = reinterpret_cast<const uint8_t *>(address->value());

  auto doRead = [&]() -> bool {
    __try {
      return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
      return false;
    }
  };

  if (doRead()) {
    return std::vector<uint8_t>(addr, addr + size);
  }
  return {};
#else
  auto addr = reinterpret_cast<const uint8_t *>(address->value());

  struct sigaction sa, old_sa;
  sa.sa_handler = safe_read_signal_handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;

  sigaction(SIGSEGV, &sa, &old_sa);
  sigaction(SIGBUS, &sa, nullptr);

  safe_read_active = true;
  std::vector<uint8_t> result;

  if (sigsetjmp(safe_read_jmpbuf, 1) == 0) {
    result.assign(addr, addr + size);
  }
  // On fault, result stays empty

  safe_read_active = false;
  sigaction(SIGSEGV, &old_sa, nullptr);
  sigaction(SIGBUS, &old_sa, nullptr);

  return result;
#endif
}

void NativeMemory::writeMemory(std::shared_ptr<NativePointer> address,
                               std::vector<uint8_t> data) {
  auto addr = reinterpret_cast<uint8_t *>(address->value());
  std::memcpy(addr, data.data(), data.size());
}

std::shared_ptr<NativePointer> NativeMemory::allocateMemory(int size) {
#ifdef CHROMATIC_WINDOWS
  void *mem = VirtualAlloc(nullptr, static_cast<SIZE_T>(size),
                           MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
  if (!mem)
    throw std::runtime_error("VirtualAlloc failed");
#else
  int prot = PROT_READ | PROT_WRITE;
  int flags = MAP_PRIVATE | MAP_ANONYMOUS;
  void *mem = mmap(nullptr, static_cast<size_t>(size), prot, flags, -1, 0);
  if (mem == MAP_FAILED)
    throw std::runtime_error("mmap failed");
#endif
  return std::make_shared<NativePointer>(reinterpret_cast<uint64_t>(mem));
}

void NativeMemory::freeMemory(std::shared_ptr<NativePointer> address, int size) {
  auto addr = reinterpret_cast<void *>(address->value());
#ifdef CHROMATIC_WINDOWS
  (void)size;
  VirtualFree(addr, 0, MEM_RELEASE);
#else
  munmap(addr, static_cast<size_t>(size));
#endif
}

std::string NativeMemory::protectMemory(std::shared_ptr<NativePointer> address,
                                        int size,
                                        const std::string &protection) {
  auto addr = reinterpret_cast<void *>(address->value());
  int newProt = protStringToFlags(protection);

#ifdef CHROMATIC_WINDOWS
  DWORD oldProt;
  if (!VirtualProtect(addr, static_cast<SIZE_T>(size),
                      static_cast<DWORD>(newProt), &oldProt))
    throw std::runtime_error("VirtualProtect failed");
  return flagsToProtString(oldProt);
#else
  // POSIX doesn't provide a way to query old protection easily,
  // so we return "---" as a placeholder. The TS layer can use
  // enumerateRanges to find old protection if needed.
  if (mprotect(addr, static_cast<size_t>(size), newProt) != 0)
    throw std::runtime_error("mprotect failed");
  return "---";
#endif
}

void NativeMemory::patchCode(std::shared_ptr<NativePointer> address,
                             std::vector<uint8_t> bytes) {
  auto addr = reinterpret_cast<uint8_t *>(address->value());
  size_t len = bytes.size();

#ifdef CHROMATIC_WINDOWS
  DWORD oldProt;
  VirtualProtect(addr, len, PAGE_EXECUTE_READWRITE, &oldProt);
  std::memcpy(addr, bytes.data(), len);
  FlushInstructionCache(GetCurrentProcess(), addr, len);
  VirtualProtect(addr, len, oldProt, &oldProt);
#elif defined(CHROMATIC_DARWIN)
  // macOS: use vm_protect with VM_PROT_COPY for signed pages
  size_t pageSize = static_cast<size_t>(sysconf(_SC_PAGESIZE));
  uintptr_t pageStart = reinterpret_cast<uintptr_t>(addr) & ~(pageSize - 1);
  size_t totalSize = (reinterpret_cast<uintptr_t>(addr) + len) - pageStart;

  vm_protect(mach_task_self(), static_cast<vm_address_t>(pageStart), totalSize,
             FALSE, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY);
  std::memcpy(addr, bytes.data(), len);
  vm_protect(mach_task_self(), static_cast<vm_address_t>(pageStart), totalSize,
             FALSE, VM_PROT_READ | VM_PROT_EXECUTE);
  sys_icache_invalidate(addr, len);
#else
  // Linux/Android
  size_t pageSize = static_cast<size_t>(sysconf(_SC_PAGESIZE));
  uintptr_t pageStart = reinterpret_cast<uintptr_t>(addr) & ~(pageSize - 1);
  size_t totalSize = (reinterpret_cast<uintptr_t>(addr) + len) - pageStart;
  mprotect(reinterpret_cast<void *>(pageStart), totalSize,
           PROT_READ | PROT_WRITE);
  std::memcpy(addr, bytes.data(), len);
  mprotect(reinterpret_cast<void *>(pageStart), totalSize,
           PROT_READ | PROT_EXEC);
#ifdef CHROMATIC_ARM64
  __builtin___clear_cache(reinterpret_cast<char *>(addr),
                          reinterpret_cast<char *>(addr + len));
#endif
#endif
}

void NativeMemory::flushIcache(std::shared_ptr<NativePointer> address, int size) {
  auto addr = reinterpret_cast<void *>(address->value());
#ifdef CHROMATIC_WINDOWS
  FlushInstructionCache(GetCurrentProcess(), addr, static_cast<SIZE_T>(size));
#elif defined(CHROMATIC_ARM64)
  __builtin___clear_cache(
      reinterpret_cast<char *>(addr),
      reinterpret_cast<char *>(reinterpret_cast<uintptr_t>(addr) + size));
#else
  (void)addr;
  (void)size;
  // x64 typically doesn't need explicit icache flush
#endif
}

void NativeMemory::copyMemory(std::shared_ptr<NativePointer> dst,
                              std::shared_ptr<NativePointer> src, int size) {
  auto dstAddr = reinterpret_cast<void *>(dst->value());
  auto srcAddr = reinterpret_cast<const void *>(src->value());
  std::memcpy(dstAddr, srcAddr, static_cast<size_t>(size));
}

// ─── scanMemory — Boyer-Moore-Horspool with wildcards ─────────────
std::vector<std::shared_ptr<ScanMatch>>
NativeMemory::scanMemory(std::shared_ptr<NativePointer> address, int size,
                         const std::string &pattern) {
  auto addr = reinterpret_cast<const uint8_t *>(address->value());
  auto pat = parsePattern(pattern);
  return bmhScan(addr, static_cast<size_t>(size), pat);
}

// ─── scanModule — scan each mapped segment of the module ──────────
std::vector<std::shared_ptr<ScanMatch>>
NativeMemory::scanModule(const std::string &moduleName,
                         const std::string &pattern) {
  auto mod = NativeProcess::findModuleByName(moduleName);
  if (!mod)
    throw std::runtime_error("Module not found: " + moduleName);

  auto pat = parsePattern(pattern);
  std::vector<std::shared_ptr<ScanMatch>> results;

  if (mod->segments.empty()) {
    // No segment info (Windows / macOS) — the full range is contiguous
    auto addr = reinterpret_cast<const uint8_t *>(mod->base->value());
    return bmhScan(addr, static_cast<size_t>(mod->size), pat);
  }

  // Scan each individually mapped segment to avoid unmapped gaps
  for (const auto &seg : mod->segments) {
    auto hits = bmhScan(reinterpret_cast<const uint8_t *>(seg.base),
                        static_cast<size_t>(seg.size), pat);
    results.insert(results.end(), hits.begin(), hits.end());
  }
  return results;
}

// ─── Async variants — co_return makes Lazy<T> → JS Promise ───────
async_simple::coro::Lazy<std::vector<std::shared_ptr<ScanMatch>>>
NativeMemory::scanMemoryAsync(std::shared_ptr<NativePointer> address, int size,
                              const std::string &pattern) {
  co_return scanMemory(address, size, pattern);
}

async_simple::coro::Lazy<std::vector<std::shared_ptr<ScanMatch>>>
NativeMemory::scanModuleAsync(const std::string &moduleName,
                              const std::string &pattern) {
  co_return scanModule(moduleName, pattern);
}

} // namespace chromatic::js
