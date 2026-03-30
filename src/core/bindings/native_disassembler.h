#pragma once
#include "native_pointer.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace async_simple::coro {
template <typename T> class Lazy;
}

namespace chromatic::js {

struct InstructionInfo {
  std::string mnemonic;
  std::string opStr;
  int size;
  std::string bytes;                    // hex
  std::shared_ptr<NativePointer> address;
  std::vector<int> groups;
  std::vector<int> regsRead;
  std::vector<int> regsWrite;
};

struct InstructionAnalysis {
  bool isBranch;
  bool isCall;
  bool isRelative;
  std::shared_ptr<NativePointer> target;
  bool isPcRelative;
  int size;
};

/// Result of a cross-reference search.
struct XrefResult {
  std::shared_ptr<NativePointer> address;
  std::string type; // "call" | "branch" | "data"
  int size;         // instruction size
};

struct NativeDisassembler {
  /// Disassemble one instruction at address.
  static std::shared_ptr<InstructionInfo>
  disassembleOne(std::shared_ptr<NativePointer> address);

  /// Disassemble `count` instructions starting at address.
  static std::vector<std::shared_ptr<InstructionInfo>>
  disassemble(std::shared_ptr<NativePointer> address, int count);

  /// Analyze instruction for control flow.
  static std::shared_ptr<InstructionAnalysis>
  analyzeInstruction(std::shared_ptr<NativePointer> address);

  /// Find all instructions in [rangeStart, rangeStart+rangeSize) that
  /// reference targetAddr (call, branch, or data/PC-relative load).
  static std::vector<std::shared_ptr<XrefResult>>
  findXrefs(std::shared_ptr<NativePointer> rangeStart, int rangeSize,
            std::shared_ptr<NativePointer> targetAddr);

  /// Find xrefs within a named module.
  static std::vector<std::shared_ptr<XrefResult>>
  findXrefsInModule(const std::string &moduleName,
                    std::shared_ptr<NativePointer> targetAddr);

  /// Async variant of findXrefs.
  static async_simple::coro::Lazy<std::vector<std::shared_ptr<XrefResult>>>
  findXrefsAsync(std::shared_ptr<NativePointer> rangeStart, int rangeSize,
                 std::shared_ptr<NativePointer> targetAddr);

  /// Async variant of findXrefsInModule.
  static async_simple::coro::Lazy<std::vector<std::shared_ptr<XrefResult>>>
  findXrefsInModuleAsync(const std::string &moduleName,
                         std::shared_ptr<NativePointer> targetAddr);

  /// Iterate instructions starting at `address` for `count` instructions,
  /// calling `filter` on each. Return only instructions for which filter
  /// returns true.
  static std::vector<std::shared_ptr<InstructionInfo>>
  filterInstructions(
      std::shared_ptr<NativePointer> address, int count,
      std::function<bool(std::shared_ptr<InstructionInfo>)> filter);

  /// Async variant of filterInstructions.
  static async_simple::coro::Lazy<
      std::vector<std::shared_ptr<InstructionInfo>>>
  filterInstructionsAsync(
      std::shared_ptr<NativePointer> address, int count,
      std::function<bool(std::shared_ptr<InstructionInfo>)> filter);
};
} // namespace chromatic::js
