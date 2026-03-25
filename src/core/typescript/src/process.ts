import { NativeProcess as NP } from 'chromatic';
import { NativePointer } from './native-pointer';
import type { ModuleInfo, RangeInfo } from './types';

/**
 * Process — provides information about the current process.
 *
 * Frida-compatible API for querying process architecture, platform,
 * memory layout, and loaded modules.
 */
export const Process = {
  /** CPU architecture: `"arm64"` or `"x64"`. */
  get arch(): string {
    return NP.getArchitecture();
  },

  /** Operating system: `"darwin"`, `"linux"`, `"windows"`, or `"android"`. */
  get platform(): string {
    return NP.getPlatform();
  },

  /** Native pointer size in bytes (4 or 8). */
  get pointerSize(): number {
    return NP.getPointerSize();
  },

  /** Virtual memory page size in bytes (typically 4096 or 16384). */
  get pageSize(): number {
    return NP.getPageSize();
  },

  /** Process ID (PID) of the current process. */
  get id(): number {
    return NP.getProcessId();
  },

  /**
   * Get the OS thread ID of the calling thread.
   * @returns Thread ID as a number.
   */
  getCurrentThreadId(): number {
    const hex = NP.getCurrentThreadId();
    return Number(BigInt(hex));
  },

  /**
   * Enumerate all loaded modules (shared libraries and the main executable).
   * @returns Array of {@link ModuleInfo} objects.
   */
  enumerateModules(): ModuleInfo[] {
    const modules = NP.enumerateModules();
    return modules.map(m => ({
      name: m.name,
      base: new NativePointer(m.base),
      size: m.size,
      path: m.path
    }));
  },

  /**
   * Enumerate memory ranges matching the given protection filter.
   *
   * @param protection - Protection string to match, e.g. `"r--"`, `"rw-"`, `"r-x"`.
   *   A character means "must have this permission"; `-` means "don't care".
   * @returns Array of {@link RangeInfo} objects describing each matching range.
   */
  enumerateRanges(protection: string): RangeInfo[] {
    const ranges = NP.enumerateRanges(protection);
    return ranges.map(r => ({
      base: new NativePointer(r.base),
      size: r.size,
      protection: r.protection,
      file: r.filePath ? { path: r.filePath } : undefined
    }));
  },

  /**
   * Find the module that contains the given address.
   *
   * @param address - A memory address to look up.
   * @returns {@link ModuleInfo} if found, or `null`.
   */
  findModuleByAddress(address: NativePointer | string): ModuleInfo | null {
    const ptr = new NativePointer(address);
    const m = NP.findModuleByAddress(ptr.toString());
    if (!m) return null;
    return {
      name: m.name,
      base: new NativePointer(m.base),
      size: m.size,
      path: m.path
    };
  },

  /**
   * Find a loaded module by its short name.
   *
   * @param name - Module name (e.g. `"libSystem.B.dylib"`).
   * @returns {@link ModuleInfo} if found, or `null`.
   */
  findModuleByName(name: string): ModuleInfo | null {
    const m = NP.findModuleByName(name);
    if (!m) return null;
    return {
      name: m.name,
      base: new NativePointer(m.base),
      size: m.size,
      path: m.path
    };
  }
};
