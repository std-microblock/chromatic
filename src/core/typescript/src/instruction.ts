import { NativeDisassembler } from 'chromatic';
import { NativePointer } from './native-pointer';
import type { NativePointerValue, InstructionInfo, XrefResult } from './types';

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
    const ptr = new NativePointer(target);
    const raw = NativeDisassembler.disassembleOne(ptr.toString());
    return {
      address: new NativePointer(raw.address),
      mnemonic: raw.mnemonic || '',
      opStr: raw.opStr || '',
      size: raw.size || 0,
      bytes: raw.bytes || '',
      groups: raw.groups || [],
      regsRead: raw.regsRead || [],
      regsWrite: raw.regsWrite || []
    };
  },

  /**
   * Disassemble multiple consecutive instructions.
   *
   * @param target - Start address.
   * @param count  - Number of instructions to disassemble.
   * @returns Array of {@link InstructionInfo}.
   */
  disassemble(target: NativePointerValue, count: number): InstructionInfo[] {
    const ptr = new NativePointer(target);
    const rawArr = NativeDisassembler.disassemble(ptr.toString(), count);
    return rawArr.map(r => ({
      address: new NativePointer(r.address),
      mnemonic: r.mnemonic || '',
      opStr: r.opStr || '',
      size: r.size || 0,
      bytes: r.bytes || '',
      groups: r.groups || [],
      regsRead: r.regsRead || [],
      regsWrite: r.regsWrite || []
    }));
  },

  /**
   * Analyze a single instruction for control-flow properties.
   *
   * @param target - Address of the instruction.
   * @returns Object describing branch/call behavior and target address.
   */
  analyze(target: NativePointerValue): {
    isBranch: boolean;
    isCall: boolean;
    isRelative: boolean;
    target: NativePointer;
    isPcRelative: boolean;
    size: number;
  } {
    const ptr = new NativePointer(target);
    const raw = NativeDisassembler.analyzeInstruction(ptr.toString());
    return {
      isBranch: raw.isBranch,
      isCall: raw.isCall,
      isRelative: raw.isRelative,
      target: new NativePointer(raw.target),
      isPcRelative: raw.isPcRelative,
      size: raw.size
    };
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
    const start = new NativePointer(rangeStart);
    const target = new NativePointer(targetAddr);
    const results = NativeDisassembler.findXrefs(start.toString(), rangeSize, target.toString());
    return results.map(r => ({
      address: new NativePointer(r.address),
      type: r.type,
      size: r.size
    }));
  },

  /**
   * Find all cross-references to `targetAddr` within a named module.
   *
   * @param moduleName - Name of the module to search.
   * @param targetAddr - The address being referenced.
   * @returns Array of {@link XrefResult}.
   */
  findXrefsInModule(moduleName: string, targetAddr: NativePointerValue): XrefResult[] {
    const target = new NativePointer(targetAddr);
    const results = NativeDisassembler.findXrefsInModule(moduleName, target.toString());
    return results.map(r => ({
      address: new NativePointer(r.address),
      type: r.type,
      size: r.size
    }));
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
    const start = new NativePointer(rangeStart);
    const target = new NativePointer(targetAddr);
    const results = await NativeDisassembler.findXrefsAsync(start.toString(), rangeSize, target.toString());
    return results.map(r => ({
      address: new NativePointer(r.address),
      type: r.type,
      size: r.size
    }));
  },

  /**
   * Async variant of {@link findXrefsInModule}.
   *
   * @param moduleName - Name of the module to search.
   * @param targetAddr - The address being referenced.
   * @returns Promise resolving to an array of {@link XrefResult}.
   */
  async findXrefsInModuleAsync(moduleName: string, targetAddr: NativePointerValue): Promise<XrefResult[]> {
    const target = new NativePointer(targetAddr);
    const results = await NativeDisassembler.findXrefsInModuleAsync(moduleName, target.toString());
    return results.map(r => ({
      address: new NativePointer(r.address),
      type: r.type,
      size: r.size
    }));
  },

  /**
   * Iterate `count` instructions starting at `address` and return only
   * those for which the `filter` callback returns `true`.
   *
   * The callback receives an {@link InstructionInfo} (with raw C++ strings
   * for `address`) and should return a boolean.
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
    const ptr = new NativePointer(address);
    // Pass a thin wrapper to C++: the C++ binding passes InstructionInfo
    // with raw string address; we convert in the result mapping.
    const results = NativeDisassembler.filterInstructions(ptr.toString(), count, (raw) => {
      return filter({
        address: new NativePointer(raw.address),
        mnemonic: raw.mnemonic || '',
        opStr: raw.opStr || '',
        size: raw.size || 0,
        bytes: raw.bytes || '',
        groups: raw.groups || [],
        regsRead: raw.regsRead || [],
        regsWrite: raw.regsWrite || []
      });
    });
    return results.map(r => ({
      address: new NativePointer(r.address),
      mnemonic: r.mnemonic || '',
      opStr: r.opStr || '',
      size: r.size || 0,
      bytes: r.bytes || '',
      groups: r.groups || [],
      regsRead: r.regsRead || [],
      regsWrite: r.regsWrite || []
    }));
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
    const ptr = new NativePointer(address);
    const results = await NativeDisassembler.filterInstructionsAsync(ptr.toString(), count, (raw) => {
      return filter({
        address: new NativePointer(raw.address),
        mnemonic: raw.mnemonic || '',
        opStr: raw.opStr || '',
        size: raw.size || 0,
        bytes: raw.bytes || '',
        groups: raw.groups || [],
        regsRead: raw.regsRead || [],
        regsWrite: raw.regsWrite || []
      });
    });
    return results.map(r => ({
      address: new NativePointer(r.address),
      mnemonic: r.mnemonic || '',
      opStr: r.opStr || '',
      size: r.size || 0,
      bytes: r.bytes || '',
      groups: r.groups || [],
      regsRead: r.regsRead || [],
      regsWrite: r.regsWrite || []
    }));
  }
};
