#include "native_disassembler.h"
#include "native_process.h"
#include <async_simple/coro/Lazy.h>
#include <capstone/capstone.h>
#include <cstdint>
#include <sstream>
#include <stdexcept>

namespace {

uint64_t parseHexAddr(const std::string &s) {
  return std::stoull(s, nullptr, 16);
}

std::string toHexAddr(uint64_t addr) {
  std::ostringstream oss;
  oss << "0x" << std::hex << addr;
  return oss.str();
}

std::string bytesToHex(const uint8_t *data, size_t len) {
  std::string result;
  static const char hexchars[] = "0123456789abcdef";
  for (size_t i = 0; i < len; i++) {
    if (i > 0)
      result += ' ';
    result.push_back(hexchars[(data[i] >> 4) & 0xF]);
    result.push_back(hexchars[data[i] & 0xF]);
  }
  return result;
}

cs_arch getArch() {
#ifdef CHROMATIC_ARM64
  return CS_ARCH_ARM64;
#else
  return CS_ARCH_X86;
#endif
}

cs_mode getMode() {
#ifdef CHROMATIC_ARM64
  return CS_MODE_ARM;
#else
  return CS_MODE_64;
#endif
}

struct CapstoneHandle {
  csh handle;
  CapstoneHandle() {
    if (cs_open(getArch(), getMode(), &handle) != CS_ERR_OK)
      throw std::runtime_error("Failed to initialize Capstone");
    cs_option(handle, CS_OPT_DETAIL, CS_OPT_ON);
  }
  ~CapstoneHandle() { cs_close(&handle); }
};

thread_local CapstoneHandle cs_handle;

chromatic::js::InstructionInfo insnToInfo(cs_insn *insn) {
  chromatic::js::InstructionInfo info;
  info.mnemonic = insn->mnemonic;
  info.opStr = insn->op_str;
  info.size = static_cast<int>(insn->size);
  info.bytes = bytesToHex(insn->bytes, insn->size);
  info.address = toHexAddr(insn->address);

  if (insn->detail) {
    for (uint8_t i = 0; i < insn->detail->groups_count; i++) {
      info.groups.push_back(static_cast<int>(insn->detail->groups[i]));
    }
    for (uint8_t i = 0; i < insn->detail->regs_read_count; i++) {
      info.regsRead.push_back(static_cast<int>(insn->detail->regs_read[i]));
    }
    for (uint8_t i = 0; i < insn->detail->regs_write_count; i++) {
      info.regsWrite.push_back(static_cast<int>(insn->detail->regs_write[i]));
    }
  }

  return info;
}

/// Extract target address from an instruction (for xref analysis).
/// Returns 0 if no target found.
/// Sets `type` to "call", "branch", or "data".
uint64_t extractTarget(cs_insn *insn, std::string &type) {
  if (!insn->detail)
    return 0;

  bool isBranch = false;
  bool isCall = false;

  for (uint8_t i = 0; i < insn->detail->groups_count; i++) {
    if (insn->detail->groups[i] == CS_GRP_JUMP)
      isBranch = true;
    if (insn->detail->groups[i] == CS_GRP_CALL)
      isCall = true;
  }

#ifdef CHROMATIC_ARM64
  auto &arm64 = insn->detail->arm64;

  // Check for ADRP instruction (page-relative)
  if (insn->id == ARM64_INS_ADRP) {
    for (uint8_t i = 0; i < arm64.op_count; i++) {
      if (arm64.operands[i].type == ARM64_OP_IMM) {
        type = "data";
        return static_cast<uint64_t>(arm64.operands[i].imm);
      }
    }
  }

  // Check for ADR instruction
  if (insn->id == ARM64_INS_ADR) {
    for (uint8_t i = 0; i < arm64.op_count; i++) {
      if (arm64.operands[i].type == ARM64_OP_IMM) {
        type = "data";
        return static_cast<uint64_t>(arm64.operands[i].imm);
      }
    }
  }

  // Check branch/call operands
  for (uint8_t i = 0; i < arm64.op_count; i++) {
    if (arm64.operands[i].type == ARM64_OP_IMM) {
      if (isCall) {
        type = "call";
      } else if (isBranch) {
        type = "branch";
      } else {
        type = "data";
      }
      return static_cast<uint64_t>(arm64.operands[i].imm);
    }
  }
#else
  auto &x86 = insn->detail->x86;

  for (uint8_t i = 0; i < x86.op_count; i++) {
    if (x86.operands[i].type == X86_OP_IMM) {
      if (isCall) {
        type = "call";
      } else if (isBranch) {
        type = "branch";
      } else {
        type = "data";
      }
      return static_cast<uint64_t>(x86.operands[i].imm);
    } else if (x86.operands[i].type == X86_OP_MEM &&
               x86.operands[i].mem.base == X86_REG_RIP) {
      // RIP-relative memory access
      type = isCall ? "call" : (isBranch ? "branch" : "data");
      return insn->address + insn->size + x86.operands[i].mem.disp;
    }
  }
#endif

  return 0;
}

} // namespace

namespace chromatic::js {

InstructionInfo NativeDisassembler::disassembleOne(const std::string &address) {
  uint64_t addr = parseHexAddr(address);
  if (addr == 0)
    return InstructionInfo{"", "", 0, "", "0x0", {}, {}, {}};
  auto code = reinterpret_cast<const uint8_t *>(addr);

  cs_insn *insn;
  size_t count = cs_disasm(cs_handle.handle, code, 16, addr, 1, &insn);
  if (count == 0)
    return InstructionInfo{"", "", 0, "", toHexAddr(addr), {}, {}, {}};

  auto result = insnToInfo(&insn[0]);
  cs_free(insn, count);
  return result;
}

std::vector<InstructionInfo>
NativeDisassembler::disassemble(const std::string &address, int count) {
  uint64_t addr = parseHexAddr(address);
  if (addr == 0 || count <= 0)
    return {};
  auto code = reinterpret_cast<const uint8_t *>(addr);

  cs_insn *insn;
  size_t maxBytes = static_cast<size_t>(count) * 16;
  size_t disasmCount =
      cs_disasm(cs_handle.handle, code, maxBytes, addr, count, &insn);

  std::vector<InstructionInfo> result;
  for (size_t i = 0; i < disasmCount; i++) {
    result.push_back(insnToInfo(&insn[i]));
  }

  if (disasmCount > 0)
    cs_free(insn, disasmCount);
  return result;
}

InstructionAnalysis
NativeDisassembler::analyzeInstruction(const std::string &address) {
  uint64_t addr = parseHexAddr(address);
  if (addr == 0)
    return InstructionAnalysis{false, false, false, "0x0", false, 0};
  auto code = reinterpret_cast<const uint8_t *>(addr);

  cs_insn *insn;
  size_t count = cs_disasm(cs_handle.handle, code, 16, addr, 1, &insn);
  if (count == 0)
    return InstructionAnalysis{false, false, false, "0x0", false, 0};

  bool isBranch = false;
  bool isCall = false;
  bool isRelative = false;
  bool isPcRelative = false;
  uint64_t target = 0;

  if (insn->detail) {
    for (uint8_t i = 0; i < insn->detail->groups_count; i++) {
      if (insn->detail->groups[i] == CS_GRP_JUMP)
        isBranch = true;
      if (insn->detail->groups[i] == CS_GRP_CALL)
        isCall = true;
      if (insn->detail->groups[i] == CS_GRP_BRANCH_RELATIVE)
        isRelative = true;
    }

#ifdef CHROMATIC_ARM64
    auto &arm64 = insn->detail->arm64;
    for (uint8_t i = 0; i < arm64.op_count; i++) {
      if (arm64.operands[i].type == ARM64_OP_IMM) {
        target = static_cast<uint64_t>(arm64.operands[i].imm);
        isPcRelative = true;
        break;
      }
    }
#else
    auto &x86 = insn->detail->x86;
    for (uint8_t i = 0; i < x86.op_count; i++) {
      if (x86.operands[i].type == X86_OP_IMM) {
        target = static_cast<uint64_t>(x86.operands[i].imm);
        isPcRelative = true;
        break;
      } else if (x86.operands[i].type == X86_OP_MEM &&
                 x86.operands[i].mem.base == X86_REG_RIP) {
        target = addr + insn->size + x86.operands[i].mem.disp;
        isPcRelative = true;
        break;
      }
    }
#endif
  }

  int size = insn->size;
  cs_free(insn, count);

  return InstructionAnalysis{isBranch,          isCall,       isRelative,
                             toHexAddr(target), isPcRelative, size};
}

// ─── findXrefs — scan range for instructions referencing target ────
std::vector<XrefResult>
NativeDisassembler::findXrefs(const std::string &rangeStart, int rangeSize,
                              const std::string &targetAddr) {
  std::vector<XrefResult> results;
  uint64_t start = parseHexAddr(rangeStart);
  uint64_t target = parseHexAddr(targetAddr);
  if (start == 0 || target == 0 || rangeSize <= 0)
    return results;
  auto code = reinterpret_cast<const uint8_t *>(start);
  size_t remaining = static_cast<size_t>(rangeSize);
  size_t offset = 0;

  while (offset < remaining) {
    cs_insn *insn;
    size_t maxBytes = remaining - offset;
    if (maxBytes > 16)
      maxBytes = 16; // decode one instruction at a time

    size_t count = cs_disasm(cs_handle.handle, code + offset, maxBytes,
                             start + offset, 1, &insn);
    if (count == 0) {
      // Skip one byte if disassembly fails
#ifdef CHROMATIC_ARM64
      offset += 4; // ARM64 instructions are always 4 bytes
#else
      offset += 1;
#endif
      continue;
    }

    std::string xrefType;
    uint64_t insnTarget = extractTarget(insn, xrefType);

    if (insnTarget == target) {
      results.push_back(
          {toHexAddr(insn->address), xrefType, static_cast<int>(insn->size)});
    }

    offset += insn->size;
    cs_free(insn, count);
  }

  return results;
}

// ─── findXrefsInModule — look up module, delegate ──────────────────
std::vector<XrefResult>
NativeDisassembler::findXrefsInModule(const std::string &moduleName,
                                      const std::string &targetAddr) {
  auto mod = NativeProcess::findModuleByName(moduleName);
  if (!mod)
    throw std::runtime_error("Module not found: " + moduleName);
  return findXrefs(mod->base, mod->size, targetAddr);
}

// ─── findXrefs async variants ─────────────────────────────────────
async_simple::coro::Lazy<std::vector<XrefResult>>
NativeDisassembler::findXrefsAsync(const std::string &rangeStart, int rangeSize,
                                   const std::string &targetAddr) {
  co_return findXrefs(rangeStart, rangeSize, targetAddr);
}

async_simple::coro::Lazy<std::vector<XrefResult>>
NativeDisassembler::findXrefsInModuleAsync(const std::string &moduleName,
                                           const std::string &targetAddr) {
  co_return findXrefsInModule(moduleName, targetAddr);
}

// ─── filterInstructions — iterate + JS callback filter ─────────────
std::vector<InstructionInfo> NativeDisassembler::filterInstructions(
    const std::string &address, int count,
    std::function<bool(InstructionInfo)> filter) {
  uint64_t addr = parseHexAddr(address);
  if (addr == 0 || count <= 0)
    return {};
  auto code = reinterpret_cast<const uint8_t *>(addr);

  std::vector<InstructionInfo> results;
  size_t offset = 0;
  int decoded = 0;

  while (decoded < count) {
    cs_insn *insn;
    size_t maxBytes = 16;
    size_t disasmCount = cs_disasm(cs_handle.handle, code + offset, maxBytes,
                                   addr + offset, 1, &insn);
    if (disasmCount == 0)
      break;

    auto info = insnToInfo(&insn[0]);
    offset += insn->size;
    cs_free(insn, disasmCount);
    decoded++;

    if (filter(info)) {
      results.push_back(std::move(info));
    }
  }

  return results;
}

// ─── filterInstructions async variant ──────────────────────────────
async_simple::coro::Lazy<std::vector<InstructionInfo>>
NativeDisassembler::filterInstructionsAsync(
    const std::string &address, int count,
    std::function<bool(InstructionInfo)> filter) {
  co_return filterInstructions(address, count, std::move(filter));
}

} // namespace chromatic::js
