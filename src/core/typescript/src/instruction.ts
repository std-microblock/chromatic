import { NativeDisassembler, NativePointer } from 'chromatic';
import { ptr } from './native-pointer';
import type { NativePointerValue, InstructionInfo, XrefResult, InstructionAnalysis } from './types';

/**
 * Instruction — disassemble, analyze, and search native instructions.
 *
 * Backed by Capstone disassembly engine via C++ bindings.
 */
export const Instruction = {
  /**
   * Parse a single instruction at the given address.
   *
   * @param target - Address of the instruction to disassemble.
   * @returns Parsed {@link InstructionInfo} with mnemonic, operands, bytes, etc.
   */
  parse(target: NativePointerValue): InstructionInfo {
    return NativeDisassembler.disassembleOne(ptr(target)) as InstructionInfo;
  },

  /**
   * Disassemble multiple consecutive instructions.
   *
   * @param target - Start address.
   * @param count  - Number of instructions to disassemble.
   * @returns Array of {@link InstructionInfo}.
   */
  disassemble(target: NativePointerValue, count: number): InstructionInfo[] {
    return NativeDisassembler.disassemble(ptr(target), count) as InstructionInfo[];
  },

  /**
   * Analyze a single instruction for control-flow properties.
   *
   * @param target - Address of the instruction.
   * @returns Object describing branch/call behavior and target address.
   */
  analyze(target: NativePointerValue): InstructionAnalysis {
    return NativeDisassembler.analyzeInstruction(ptr(target)) as InstructionAnalysis;
  },

  /**
   * Find all cross-references to `targetAddr` within a memory range.
   *
   * Scans every instruction in `[rangeStart, rangeStart + rangeSize)` and
   * returns those whose operand resolves to `targetAddr` (call, branch,
   * or PC-relative data reference).
   *
   * @param rangeStart - Start address of the search range.
   * @param rangeSize  - Size of the search range in bytes.
   * @param targetAddr - The address being referenced.
   * @returns Array of {@link XrefResult}.
   */
  findXrefs(rangeStart: NativePointerValue, rangeSize: number, targetAddr: NativePointerValue): XrefResult[] {
    return NativeDisassembler.findXrefs(ptr(rangeStart), rangeSize, ptr(targetAddr)) as XrefResult[];
  },

  /**
   * Find all cross-references to `targetAddr` within a named module.
   *
   * @param moduleName - Name of the module to search.
   * @param targetAddr - The address being referenced.
   * @returns Array of {@link XrefResult}.
   */
  findXrefsInModule(moduleName: string, targetAddr: NativePointerValue): XrefResult[] {
    return NativeDisassembler.findXrefsInModule(moduleName, ptr(targetAddr)) as XrefResult[];
  },

  /**
   * Async variant of {@link findXrefs}.
   *
   * @param rangeStart - Start address of the search range.
   * @param rangeSize  - Size of the search range in bytes.
   * @param targetAddr - The address being referenced.
   * @returns Promise resolving to an array of {@link XrefResult}.
   */
  async findXrefsAsync(rangeStart: NativePointerValue, rangeSize: number, targetAddr: NativePointerValue): Promise<XrefResult[]> {
    return await NativeDisassembler.findXrefsAsync(ptr(rangeStart), rangeSize, ptr(targetAddr)) as XrefResult[];
  },

  /**
   * Async variant of {@link findXrefsInModule}.
   *
   * @param moduleName - Name of the module to search.
   * @param targetAddr - The address being referenced.
   * @returns Promise resolving to an array of {@link XrefResult}.
   */
  async findXrefsInModuleAsync(moduleName: string, targetAddr: NativePointerValue): Promise<XrefResult[]> {
    return await NativeDisassembler.findXrefsInModuleAsync(moduleName, ptr(targetAddr)) as XrefResult[];
  },

  /**
   * Iterate `count` instructions starting at `address` and return only
   * those for which the `filter` callback returns `true`.
   *
   * The callback receives an {@link InstructionInfo} (C++ class instance
   * with NativePointer address field).
   *
   * @param address - Start address.
   * @param count   - Number of instructions to iterate.
   * @param filter  - Predicate function receiving each instruction.
   * @returns Array of matching {@link InstructionInfo}.
   *
   * @example
   * ```ts
   * // Find all `ret` instructions in the first 100 instructions of malloc
   * const addr = Module.findExportByName(null, 'malloc');
   * const rets = Instruction.filterInstructions(addr, 100, insn => insn.mnemonic === 'ret');
   * ```
   */
  filterInstructions(address: NativePointerValue, count: number, filter: (insn: InstructionInfo) => boolean): InstructionInfo[] {
    return NativeDisassembler.filterInstructions(ptr(address), count, filter as any) as InstructionInfo[];
  },

  /**
   * Async variant of {@link filterInstructions}.
   *
   * @param address - Start address.
   * @param count   - Number of instructions to iterate.
   * @param filter  - Predicate function receiving each instruction.
   * @returns Promise resolving to an array of matching {@link InstructionInfo}.
   */
  async filterInstructionsAsync(address: NativePointerValue, count: number, filter: (insn: InstructionInfo) => boolean): Promise<InstructionInfo[]> {
    return await NativeDisassembler.filterInstructionsAsync(ptr(address), count, filter as any) as InstructionInfo[];
  }
};
