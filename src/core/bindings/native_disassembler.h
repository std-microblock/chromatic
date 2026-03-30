#pragma once
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
  std::string bytes;   // hex
  std::string address; // hex
  std::vector<int> groups;
  std::vector<int> regsRead;
  std::vector<int> regsWrite;
};

struct InstructionAnalysis {
  bool isBranch;
  bool isCall;
  bool isRelative;
  std::string target; // hex
  bool isPcRelative;
  int size;
};

/// Result of a cross-reference search.
struct XrefResult {
  std::string address; // hex — the referring instruction's address
  std::string type;    // "call" | "branch" | "data"
  int size;            // instruction size
};

struct NativeDisassembler {
  /// Disassemble one instruction at address.
  static std::shared_ptr<InstructionInfo>
  disassembleOne(const std::string &address);

  /// Disassemble `count` instructions starting at address.
  static std::vector<std::shared_ptr<InstructionInfo>>
  disassemble(const std::string &address, int count);

  /// Analyze instruction for control flow.
  static std::shared_ptr<InstructionAnalysis>
  analyzeInstruction(const std::string &address);

  /// Find all instructions in [rangeStart, rangeStart+rangeSize) that
  /// reference targetAddr (call, branch, or data/PC-relative load).
  static std::vector<std::shared_ptr<XrefResult>>
  findXrefs(const std::string &rangeStart, int rangeSize,
            const std::string &targetAddr);

  /// Find xrefs within a named module.
  static std::vector<std::shared_ptr<XrefResult>>
  findXrefsInModule(const std::string &moduleName,
                    const std::string &targetAddr);

  /// Async variant of findXrefs.
  static async_simple::coro::Lazy<std::vector<std::shared_ptr<XrefResult>>>
  findXrefsAsync(const std::string &rangeStart, int rangeSize,
                 const std::string &targetAddr);

  /// Async variant of findXrefsInModule.
  static async_simple::coro::Lazy<std::vector<std::shared_ptr<XrefResult>>>
  findXrefsInModuleAsync(const std::string &moduleName,
                         const std::string &targetAddr);

  /// Iterate instructions starting at `address` for `count` instructions,
  /// calling `filter` on each. Return only instructions for which filter
  /// returns true.
  static std::vector<std::shared_ptr<InstructionInfo>>
  filterInstructions(const std::string &address, int count,
                     std::function<bool(std::shared_ptr<InstructionInfo>)> filter);

  /// Async variant of filterInstructions.
  static async_simple::coro::Lazy<std::vector<std::shared_ptr<InstructionInfo>>>
  filterInstructionsAsync(
      const std::string &address, int count,
      std::function<bool(std::shared_ptr<InstructionInfo>)> filter);
};
} // namespace chromatic::js
