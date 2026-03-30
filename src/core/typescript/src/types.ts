// Shared types for the Chromatic instrumentation framework

import type { NativePointer } from 'chromatic';

export type NativePointerValue = NativePointer | string | number | bigint;

export type NativeType =
  | 'void'
  | 'pointer'
  | 'int' | 'uint'
  | 'int8' | 'uint8'
  | 'int16' | 'uint16'
  | 'int32' | 'uint32'
  | 'int64' | 'uint64'
  | 'long' | 'ulong'
  | 'char' | 'uchar'
  | 'short' | 'ushort'
  | 'float' | 'double'
  | 'bool';

export interface CpuContext {
  [reg: string]: NativePointer;
}

export interface Arm64CpuContext extends CpuContext {
  x0: NativePointer; x1: NativePointer; x2: NativePointer; x3: NativePointer;
  x4: NativePointer; x5: NativePointer; x6: NativePointer; x7: NativePointer;
  x8: NativePointer; x9: NativePointer; x10: NativePointer; x11: NativePointer;
  x12: NativePointer; x13: NativePointer; x14: NativePointer; x15: NativePointer;
  x16: NativePointer; x17: NativePointer; x18: NativePointer; x19: NativePointer;
  x20: NativePointer; x21: NativePointer; x22: NativePointer; x23: NativePointer;
  x24: NativePointer; x25: NativePointer; x26: NativePointer; x27: NativePointer;
  x28: NativePointer;
  fp: NativePointer;
  lr: NativePointer;
  sp: NativePointer;
  pc: NativePointer;
}

export interface X64CpuContext extends CpuContext {
  rax: NativePointer; rbx: NativePointer; rcx: NativePointer; rdx: NativePointer;
  rsi: NativePointer; rdi: NativePointer; rbp: NativePointer; rsp: NativePointer;
  r8: NativePointer; r9: NativePointer; r10: NativePointer; r11: NativePointer;
  r12: NativePointer; r13: NativePointer; r14: NativePointer; r15: NativePointer;
  rip: NativePointer;
}

export interface ModuleInfo {
  name: string;
  /** NativePointer to module base address. */
  base: NativePointer;
  size: number;
  path: string;
}

export interface RangeInfo {
  /** NativePointer to range base. */
  base: NativePointer;
  size: number;
  protection: string;
  filePath: string;
}

export interface ExportInfo {
  type: string;
  name: string;
  /** NativePointer to export address. */
  address: NativePointer;
}

export interface InstructionInfo {
  /** NativePointer to instruction address. */
  address: NativePointer;
  mnemonic: string;
  opStr: string;
  size: number;
  /** Hex-encoded bytes. */
  bytes: string;
  groups: number[];
  regsRead: number[];
  regsWrite: number[];
}

/** Result of a memory pattern scan match. */
export interface ScanMatch {
  /** NativePointer to match address. */
  address: NativePointer;
  /** Size of the matched pattern in bytes. */
  size: number;
}

/** Result of a cross-reference (xref) search. */
export interface XrefResult {
  /** NativePointer to the referring instruction's address. */
  address: NativePointer;
  /** Type of reference: "call", "branch", or "data". */
  type: string;
  /** Size of the referring instruction in bytes. */
  size: number;
}

/** Result of analyzing an instruction for control flow. */
export interface InstructionAnalysis {
  isBranch: boolean;
  isCall: boolean;
  isRelative: boolean;
  /** NativePointer to the branch/call target. */
  target: NativePointer;
  isPcRelative: boolean;
  size: number;
}

export interface InvocationArgs {
  [index: number]: NativePointer;
}

export interface InvocationReturnValue extends NativePointer {
  replace(value: NativePointerValue): void;
}

export interface InvocationContext {
  context: CpuContext;
  threadId: number;
  returnAddress: NativePointer;
}

export interface InvocationCallbacks {
  onEnter?: (this: InvocationContext, args: InvocationArgs) => void;
  onLeave?: (this: InvocationContext, retval: InvocationReturnValue) => void;
}

export interface InvocationListener {
  detach(): void;
}
