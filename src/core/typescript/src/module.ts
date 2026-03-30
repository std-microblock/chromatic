import { NativeProcess } from 'chromatic';
import { NativePointer } from './native-pointer';
import { Memory } from './memory';
import { Instruction } from './instruction';
import type { ModuleInfo, ExportInfo, NativePointerValue, ScanMatch, XrefResult } from './types';

/**
 * Module — Frida-compatible module operations.
 *
 * Represents a loaded native module (shared library / executable image).
 * Provides both instance methods and static class methods for module
 * enumeration, export lookup, memory scanning, and xref searching.
 */
export class Module {
  /** Module short name (e.g. "libfoo.dylib"). */
  name: string;
  /** Base address of the module in memory (hex string). */
  base: string;
  /** Size of the module image in bytes. */
  size: number;
  /** Full filesystem path. */
  path: string;

  constructor(info: ModuleInfo) {
    this.name = info.name;
    this.base = info.base;
    this.size = info.size;
    this.path = info.path;
  }

  /**
   * Find an export by name within this module.
   * @param exportName - Symbol name to look up.
   * @returns NativePointer to the export, or `null` if not found.
   */
  findExportByName(exportName: string): NativePointer | null {
    return Module.findExportByName(this.name, exportName);
  }

  /**
   * Enumerate all exports of this module.
   * @returns Array of `ExportInfo` objects.
   */
  enumerateExports(): ExportInfo[] {
    return Module.enumerateExports(this.name);
  }

  /**
   * Scan this module's memory for a byte pattern (synchronous).
   * @param pattern - Space-separated hex bytes; use "??" for wildcards.
   * @returns Array of {@link ScanMatch} objects.
   */
  scan(pattern: string): ScanMatch[] {
    return Memory.scanModule(this.name, pattern);
  }

  /**
   * Scan this module's memory for a byte pattern (asynchronous).
   * @param pattern - Space-separated hex bytes; use "??" for wildcards.
   * @returns Promise resolving to array of {@link ScanMatch} objects.
   */
  async scanAsync(pattern: string): Promise<ScanMatch[]> {
    return Memory.scanModuleAsync(this.name, pattern);
  }

  /**
   * Find cross-references to `targetAddr` within this module.
   * @param targetAddr - Address being referenced.
   * @returns Array of {@link XrefResult} objects.
   */
  findXrefs(targetAddr: NativePointerValue): XrefResult[] {
    return Instruction.findXrefsInModule(this.name, targetAddr);
  }

  /**
   * Find cross-references to `targetAddr` within this module (async).
   * @param targetAddr - Address being referenced.
   * @returns Promise resolving to array of {@link XrefResult} objects.
   */
  async findXrefsAsync(targetAddr: NativePointerValue): Promise<XrefResult[]> {
    return Instruction.findXrefsInModuleAsync(this.name, targetAddr);
  }

  // ---- Static methods ----

  /**
   * Find an export by name. If moduleName is null, searches all modules.
   * @param moduleName - Module name, or `null` to search globally.
   * @param exportName - Symbol name to look up.
   * @returns NativePointer to the export, or `null` if not found.
   */
  static findExportByName(moduleName: string | null, exportName: string): NativePointer | null {
    const addr = NativeProcess.findExportByName(moduleName || '', exportName);
    if (addr === '0x0') return null;
    return new NativePointer(addr);
  }

  /**
   * Find the base address of a module by name.
   * @param moduleName - Module name to look up.
   * @returns NativePointer to the module base, or `null` if not found.
   */
  static findBaseAddress(moduleName: string): NativePointer | null {
    const m = NativeProcess.findModuleByName(moduleName);
    if (!m) return null;
    return new NativePointer(m.base);
  }

  /**
   * Enumerate all loaded modules in the current process.
   * @returns Array of `Module` instances.
   */
  static enumerateModules(): Module[] {
    const modules = NativeProcess.enumerateModules();
    return modules.map(m => new Module(m as ModuleInfo));
  }

  /**
   * Enumerate exports of a specific module by name.
   * @param moduleName - Module name.
   * @returns Array of `ExportInfo` objects.
   */
  static enumerateExports(moduleName: string): ExportInfo[] {
    return NativeProcess.enumerateExports(moduleName) as ExportInfo[];
  }

  /**
   * Load (find) a module by name.
   * @param moduleName - Module name to find.
   * @returns `Module` instance, or `null` if not found.
   */
  static load(moduleName: string): Module | null {
    const m = NativeProcess.findModuleByName(moduleName);
    if (!m) return null;
    return new Module(m as ModuleInfo);
  }
}
