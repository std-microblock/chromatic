#include "code_relocator.h"
#include "../native_disassembler.h"
#include "../native_pointer.h"

#ifdef CHROMATIC_ARM64
#include <asmjit/a64.h>
#else
#include <asmjit/x86.h>
#endif
#include <asmjit/core.h>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>

#ifdef CHROMATIC_WINDOWS
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

#ifdef CHROMATIC_DARWIN
#include <libkern/OSCacheControl.h>
#include <mach/mach.h>
#endif

namespace {

inline std::shared_ptr<chromatic::js::NativePointer> makePtr(uint64_t addr) {
  return std::make_shared<chromatic::js::NativePointer>(addr);
}

} // anonymous namespace

namespace chromatic::internal {

// ─── Shared JitRuntime ───

static asmjit::JitRuntime g_jitRuntime;

asmjit::JitRuntime &jitRuntime() { return g_jitRuntime; }

// ─── Patch writing ───

void makeWritableAndPatch(void *addr, const uint8_t *data, size_t len) {
#ifdef CHROMATIC_WINDOWS
  DWORD oldProt;
  VirtualProtect(addr, len, PAGE_EXECUTE_READWRITE, &oldProt);
  std::memcpy(addr, data, len);
  FlushInstructionCache(GetCurrentProcess(), addr, len);
  VirtualProtect(addr, len, oldProt, &oldProt);
#elif defined(CHROMATIC_DARWIN)
  size_t pageSize = static_cast<size_t>(sysconf(_SC_PAGESIZE));
  uintptr_t pageStart = reinterpret_cast<uintptr_t>(addr) & ~(pageSize - 1);
  size_t totalSize = (reinterpret_cast<uintptr_t>(addr) + len) - pageStart;

  kern_return_t kr =
      vm_protect(mach_task_self(), static_cast<vm_address_t>(pageStart),
                 totalSize, FALSE, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY);
  if (kr != KERN_SUCCESS)
    throw std::runtime_error("vm_protect(RW|COPY) failed: " +
                             std::to_string(kr));

  std::memcpy(addr, data, len);

  kr = vm_protect(mach_task_self(), static_cast<vm_address_t>(pageStart),
                  totalSize, FALSE, VM_PROT_READ | VM_PROT_EXECUTE);
  if (kr != KERN_SUCCESS)
    throw std::runtime_error("vm_protect(RX) failed: " + std::to_string(kr));

  sys_icache_invalidate(addr, len);
#else
  // Linux/Android
  size_t pageSize = static_cast<size_t>(sysconf(_SC_PAGESIZE));
  uintptr_t pageStart = reinterpret_cast<uintptr_t>(addr) & ~(pageSize - 1);
  size_t totalSize = (reinterpret_cast<uintptr_t>(addr) + len) - pageStart;
  if (mprotect(reinterpret_cast<void *>(pageStart), totalSize,
               PROT_READ | PROT_WRITE) != 0)
    throw std::runtime_error("mprotect(RW) failed");
  std::memcpy(addr, data, len);
  if (mprotect(reinterpret_cast<void *>(pageStart), totalSize,
               PROT_READ | PROT_EXEC) != 0)
    throw std::runtime_error("mprotect(RX) failed");
#ifdef CHROMATIC_ARM64
  __builtin___clear_cache(reinterpret_cast<char *>(addr),
                          reinterpret_cast<char *>(addr) + len);
#endif
#endif
}

// ─── Patch bytes ───

void generatePatchBytes(uint8_t *buf, uint64_t jumpTarget) {
#ifdef CHROMATIC_ARM64
  // LDR X16, #8
  uint32_t ldr = 0x58000050;
  std::memcpy(buf + 0, &ldr, 4);
  // BR X16
  uint32_t br = 0xD61F0200;
  std::memcpy(buf + 4, &br, 4);
  // .quad jumpTarget
  std::memcpy(buf + 8, &jumpTarget, 8);
#else
  // FF 25 00 00 00 00 = JMP [RIP+0]
  buf[0] = 0xFF;
  buf[1] = 0x25;
  uint32_t zero = 0;
  std::memcpy(buf + 2, &zero, 4);
  std::memcpy(buf + 6, &jumpTarget, 8);
#endif
}

// ─── Build relocated code ───

void *buildRelocatedCode(uint64_t source, size_t minBytes,
                         size_t &bytesConsumed) {
  using namespace asmjit;
  CodeHolder code;
  code.init(g_jitRuntime.environment(), g_jitRuntime.cpuFeatures());

  auto *srcPtr = reinterpret_cast<const uint8_t *>(source);
  size_t srcOffset = 0;

#ifdef CHROMATIC_ARM64
  a64::Assembler a(&code);

  while (srcOffset < minBytes) {
    auto addrPtr = makePtr(source + srcOffset);
    auto insn = chromatic::js::NativeDisassembler::disassembleOne(addrPtr);
    int insnSize = insn->size;
    if (insnSize == 0)
      throw std::runtime_error("Cannot disassemble at " + addrPtr->toString());

    auto analysis = chromatic::js::NativeDisassembler::analyzeInstruction(addrPtr);

    if (analysis->isPcRelative) {
      uint64_t target = analysis->target->value();
      std::string mnemonic = insn->mnemonic;

      if (mnemonic == "b") {
        a.mov(a64::x(16), target);
        a.br(a64::x(16));
      } else if (mnemonic == "bl") {
        a.mov(a64::x(16), target);
        a.blr(a64::x(16));
      } else if (mnemonic == "adr" || mnemonic == "adrp") {
        uint32_t rawInsn;
        std::memcpy(&rawInsn, srcPtr + srcOffset, 4);
        uint32_t rd = rawInsn & 0x1F;
        a.mov(a64::x(rd), target);
      } else {
        a.embed(srcPtr + srcOffset, insnSize);
      }
    } else {
      a.embed(srcPtr + srcOffset, insnSize);
    }

    srcOffset += insnSize;
  }

  // Jump back to original code after the patch area
  uint64_t jumpBackTarget = source + srcOffset;
  a.mov(a64::x(16), jumpBackTarget);
  a.br(a64::x(16));

#else // x86_64
  x86::Assembler a(&code);

  while (srcOffset < minBytes) {
    auto addrPtr = makePtr(source + srcOffset);
    auto insn = chromatic::js::NativeDisassembler::disassembleOne(addrPtr);
    int insnSize = insn->size;
    if (insnSize == 0)
      throw std::runtime_error("Cannot disassemble at " + addrPtr->toString());

    auto analysis = chromatic::js::NativeDisassembler::analyzeInstruction(addrPtr);

    if (analysis->isPcRelative) {
      uint64_t target = analysis->target->value();
      uint8_t firstByte = srcPtr[srcOffset];

      if (firstByte == 0xE9) {
        // JMP rel32 → absolute jmp
        a.jmp(x86::ptr(x86::rip));
        a.embedUInt64(target);
      } else if (firstByte == 0xE8) {
        // CALL rel32 → absolute call
        a.call(x86::ptr(x86::rip, 2));
        a.jmp(a.newLabel());
        auto skipLabel = a.newLabel();
        a.embedUInt64(target);
        a.bind(skipLabel);
      } else if (analysis->isBranch && !analysis->isCall) {
        // Conditional branch
        uint8_t cc = 0;
        if (firstByte >= 0x70 && firstByte <= 0x7F) {
          cc = firstByte - 0x70;
        } else if (firstByte == 0x0F) {
          cc = srcPtr[srcOffset + 1] - 0x80;
        }
        auto skipLabel = a.newLabel();
        a.embedUInt8(0x0F);
        a.embedUInt8(static_cast<uint8_t>(0x80 + (cc ^ 1)));
        a.embedUInt32(14);
        a.jmp(x86::ptr(x86::rip));
        a.embedUInt64(target);
        a.bind(skipLabel);
      } else {
        a.embed(srcPtr + srcOffset, insnSize);
      }
    } else {
      a.embed(srcPtr + srcOffset, insnSize);
    }

    srcOffset += insnSize;
  }

  // Jump back to original code
  uint64_t jumpBackTarget = source + srcOffset;
  a.jmp(x86::ptr(x86::rip));
  a.embedUInt64(jumpBackTarget);
#endif

  bytesConsumed = srcOffset;

  void *result = nullptr;
  asmjit::Error err = g_jitRuntime.add(&result, &code);
  if (err != asmjit::kErrorOk || !result)
    throw std::runtime_error("asmjit: failed to build relocated code");
  return result;
}

// ─── Build trampoline ───

void *buildTrampoline(DispatchFn onEnterFn, DispatchFn onLeaveFn,
                      void *userData, uint64_t relocatedAddr) {
  using namespace asmjit;
  CodeHolder code;
  code.init(g_jitRuntime.environment(), g_jitRuntime.cpuFeatures());

#ifdef CHROMATIC_ARM64
  a64::Assembler a(&code);

  // Save all general-purpose registers to stack
  // x0-x30, sp snapshot, nzcv = 33*8 = 264 bytes, round to 272 (0x110)
  constexpr int FRAME_SIZE = 0x110;
  a.sub(a64::sp, a64::sp, FRAME_SIZE);

  // STP pairs x0-x27
  for (int i = 0; i < 28; i += 2) {
    a.stp(a64::x(i), a64::x(i + 1), a64::Mem(a64::sp, i * 8));
  }
  // STP x28, x29
  a.stp(a64::x(28), a64::x(29), a64::Mem(a64::sp, 28 * 8));
  // STR x30 (LR)
  a.str(a64::x(30), a64::Mem(a64::sp, 30 * 8));
  // Save SP snapshot
  a.add(a64::x(16), a64::sp, FRAME_SIZE);
  a.str(a64::x(16), a64::Mem(a64::sp, 31 * 8));
  // Save NZCV
  a.mrs(a64::x(16), Imm(0xDA10));
  a.str(a64::x(16), a64::Mem(a64::sp, 32 * 8));

  // Call onEnter dispatch: x0 = sp (context ptr), x1 = userData
  a.mov(a64::x(0), a64::sp);
  a.mov(a64::x(1), reinterpret_cast<uint64_t>(userData));
  a.mov(a64::x(16), reinterpret_cast<uint64_t>(onEnterFn));
  a.blr(a64::x(16));

  // Call onLeave dispatch (if provided)
  if (onLeaveFn) {
    a.mov(a64::x(0), a64::sp);
    a.mov(a64::x(1), reinterpret_cast<uint64_t>(userData));
    a.mov(a64::x(16), reinterpret_cast<uint64_t>(onLeaveFn));
    a.blr(a64::x(16));
  }

  // Restore NZCV
  a.ldr(a64::x(16), a64::Mem(a64::sp, 32 * 8));
  a.msr(Imm(0xDA10), a64::x(16));

  // Restore x0-x27
  for (int i = 0; i < 28; i += 2) {
    a.ldp(a64::x(i), a64::x(i + 1), a64::Mem(a64::sp, i * 8));
  }
  // LDP x28, x29
  a.ldp(a64::x(28), a64::x(29), a64::Mem(a64::sp, 28 * 8));
  // LDR x30
  a.ldr(a64::x(30), a64::Mem(a64::sp, 30 * 8));
  // Restore SP
  a.add(a64::sp, a64::sp, FRAME_SIZE);

  // Jump to relocated code
  a.mov(a64::x(16), relocatedAddr);
  a.br(a64::x(16));

#else // x86_64
  x86::Assembler a(&code);

  // Save all general-purpose registers
  a.pushfq();
  a.push(x86::rax);
  a.push(x86::rcx);
  a.push(x86::rdx);
  a.push(x86::rbx);
  a.push(x86::rbp);
  a.push(x86::rsi);
  a.push(x86::rdi);
  a.push(x86::r8);
  a.push(x86::r9);
  a.push(x86::r10);
  a.push(x86::r11);
  a.push(x86::r12);
  a.push(x86::r13);
  a.push(x86::r14);
  a.push(x86::r15);

  // Function entry preserves the caller's stack layout because we arrive via
  // an inline jump / exception resume instead of a real call.
#ifdef CHROMATIC_WINDOWS
  // Microsoft x64 ABI: rcx = arg0, rdx = arg1, plus 32-byte shadow space.
  a.mov(x86::rcx, x86::rsp);
  a.mov(x86::rdx, reinterpret_cast<uint64_t>(userData));
  // Entry RSP is 8-byte misaligned relative to a normal call frame, so
  // reserve shadow space plus an extra 8 bytes to restore 16-byte alignment.
  a.sub(x86::rsp, 0x28);
#else
  // System V ABI: rdi = arg0, rsi = arg1.
  a.mov(x86::rdi, x86::rsp);
  a.mov(x86::rsi, reinterpret_cast<uint64_t>(userData));
  a.sub(x86::rsp, 0x8);
#endif

  // Call onEnter dispatch
  a.mov(x86::rax, reinterpret_cast<uint64_t>(onEnterFn));
  a.call(x86::rax);

  // Call onLeave dispatch (if provided)
  if (onLeaveFn) {
#ifdef CHROMATIC_WINDOWS
    a.mov(x86::rcx, x86::rsp);
    a.add(x86::rcx, 0x28); // point back to the saved register snapshot
    a.mov(x86::rdx, reinterpret_cast<uint64_t>(userData));
#else
    a.mov(x86::rdi, x86::rsp);
    a.add(x86::rdi, 0x8); // point back to saved regs
    a.mov(x86::rsi, reinterpret_cast<uint64_t>(userData));
#endif
    a.mov(x86::rax, reinterpret_cast<uint64_t>(onLeaveFn));
    a.call(x86::rax);
  }

  // Undo ABI-specific stack reservation.
#ifdef CHROMATIC_WINDOWS
  a.add(x86::rsp, 0x28);
#else
  a.add(x86::rsp, 0x8);
#endif

  // Restore all registers
  a.pop(x86::r15);
  a.pop(x86::r14);
  a.pop(x86::r13);
  a.pop(x86::r12);
  a.pop(x86::r11);
  a.pop(x86::r10);
  a.pop(x86::r9);
  a.pop(x86::r8);
  a.pop(x86::rdi);
  a.pop(x86::rsi);
  a.pop(x86::rbp);
  a.pop(x86::rbx);
  a.pop(x86::rdx);
  a.pop(x86::rcx);
  a.pop(x86::rax);
  a.popfq();

  // Jump to relocated code (absolute indirect jump)
  a.jmp(x86::ptr(x86::rip));
  a.embedUInt64(relocatedAddr);
#endif

  void *result = nullptr;
  asmjit::Error err = g_jitRuntime.add(&result, &code);
  if (err != asmjit::kErrorOk || !result)
    throw std::runtime_error("asmjit: failed to build trampoline");
  return result;
}

// ─── Release code ───

void releaseCode(void *code) {
  if (code)
    g_jitRuntime.release(code);
}

} // namespace chromatic::internal
