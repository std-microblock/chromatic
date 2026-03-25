import { NativeMemory } from 'chromatic';
import { NativePointer } from './native-pointer';
import type { NativePointerValue, ScanMatch } from './types';

/**
 * Memory — Frida-compatible memory operations.
 *
 * Provides allocation, read/write, protection changes, pattern scanning,
 * and code patching utilities for native memory.
 */
export const Memory = {
  /**
   * Allocate `size` bytes of memory with read-write permissions.
   *
   * @param size - Number of bytes to allocate.
   * @returns A {@link NativePointer} to the allocated region.
   */
  alloc(size: number): NativePointer {
    const addr = NativeMemory.allocateMemory(size);
    return new NativePointer(addr);
  },

  /**
   * Allocate memory and write a null-terminated UTF-8 string into it.
   *
   * @param str - The string to encode and write.
   * @returns A {@link NativePointer} to the allocated UTF-8 C-string.
   */
  allocUtf8String(str: string): NativePointer {
    const encoded: number[] = [];
    for (let i = 0; i < str.length; i++) {
      let cp = str.codePointAt(i)!;
      if (cp < 0x80) {
        encoded.push(cp);
      } else if (cp < 0x800) {
        encoded.push(0xC0 | (cp >> 6), 0x80 | (cp & 0x3F));
      } else if (cp < 0x10000) {
        encoded.push(0xE0 | (cp >> 12), 0x80 | ((cp >> 6) & 0x3F), 0x80 | (cp & 0x3F));
      } else {
        encoded.push(0xF0 | (cp >> 18), 0x80 | ((cp >> 12) & 0x3F), 0x80 | ((cp >> 6) & 0x3F), 0x80 | (cp & 0x3F));
        i++;
      }
    }
    encoded.push(0); // null terminator

    const ptr = Memory.alloc(encoded.length);
    ptr.writeByteArray(encoded);
    return ptr;
  },

  /**
   * Change memory protection for a region.
   *
   * @param address - Start address of the region.
   * @param size    - Size of the region in bytes.
   * @param protection - Protection string, e.g. `"rwx"`, `"r--"`, `"rw-"`.
   * @returns `true` if the protection was changed successfully.
   */
  protect(address: NativePointerValue, size: number, protection: string): boolean {
    try {
      const ptr = new NativePointer(address);
      NativeMemory.protectMemory(ptr.toString(), size, protection);
      return true;
    } catch {
      return false;
    }
  },

  /**
   * Copy `size` bytes from `src` to `dst`.
   *
   * @param dst  - Destination address.
   * @param src  - Source address.
   * @param size - Number of bytes to copy.
   */
  copy(dst: NativePointerValue, src: NativePointerValue, size: number): void {
    const d = new NativePointer(dst);
    const s = new NativePointer(src);
    NativeMemory.copyMemory(d.toString(), s.toString(), size);
  },

  /**
   * Synchronous memory scan using Boyer-Moore-Horspool with wildcard support.
   *
   * Pattern format: space-separated hex bytes, `??` for wildcard.
   * Example: `"48 8b ?? 00"`.
   *
   * @param address - Start address of the region to scan.
   * @param size    - Size of the region in bytes.
   * @param pattern - Pattern string with hex bytes and `??` wildcards.
   * @returns Array of {@link ScanMatch} objects for each match found.
   */
  scanSync(address: NativePointerValue, size: number, pattern: string): ScanMatch[] {
    const ptr = new NativePointer(address);
    const results = NativeMemory.scanMemory(ptr.toString(), size, pattern);
    return results.map(r => ({
      address: new NativePointer(r.address),
      size: r.size
    }));
  },

  /**
   * Asynchronous memory scan — returns a Promise that resolves to matches.
   *
   * Uses the same Boyer-Moore-Horspool algorithm as {@link scanSync}, but
   * the C++ coroutine is converted to a JS Promise via breeze-js.
   *
   * @param address - Start address of the region to scan.
   * @param size    - Size of the region in bytes.
   * @param pattern - Pattern string with hex bytes and `??` wildcards.
   * @returns Promise resolving to an array of {@link ScanMatch}.
   */
  async scan(address: NativePointerValue, size: number, pattern: string): Promise<ScanMatch[]> {
    const ptr = new NativePointer(address);
    const results = await NativeMemory.scanMemoryAsync(ptr.toString(), size, pattern);
    return results.map(r => ({
      address: new NativePointer(r.address),
      size: r.size
    }));
  },

  /**
   * Synchronous pattern scan within a named module.
   *
   * Internally looks up the module's base address and size, then delegates
   * to the BMH scan engine.
   *
   * @param moduleName - Name of the loaded module (e.g. `"libfoo.dylib"`).
   * @param pattern    - Pattern string with hex bytes and `??` wildcards.
   * @returns Array of {@link ScanMatch} objects.
   */
  scanModule(moduleName: string, pattern: string): ScanMatch[] {
    const results = NativeMemory.scanModule(moduleName, pattern);
    return results.map(r => ({
      address: new NativePointer(r.address),
      size: r.size
    }));
  },

  /**
   * Asynchronous pattern scan within a named module.
   *
   * @param moduleName - Name of the loaded module.
   * @param pattern    - Pattern string with hex bytes and `??` wildcards.
   * @returns Promise resolving to an array of {@link ScanMatch}.
   */
  async scanModuleAsync(moduleName: string, pattern: string): Promise<ScanMatch[]> {
    const results = await NativeMemory.scanModuleAsync(moduleName, pattern);
    return results.map(r => ({
      address: new NativePointer(r.address),
      size: r.size
    }));
  },

  /**
   * Patch code at `address` with new bytes.
   *
   * Handles memory protection changes and instruction cache flushing
   * automatically. The `apply` callback receives a writable copy of
   * the original bytes; modifications are then written back to the
   * original address.
   *
   * @param address - Address of the code to patch.
   * @param size    - Size of the region in bytes.
   * @param apply   - Callback that modifies the writable buffer.
   */
  patchCode(address: NativePointerValue, size: number, apply: (code: NativePointer) => void): void {
    const ptr = new NativePointer(address);
    // Allocate temporary writable buffer
    const buf = Memory.alloc(size);
    // Copy original code
    Memory.copy(buf, ptr, size);
    // Let user modify the buffer
    apply(buf);
    // Patch the hex
    const hex = NativeMemory.readMemory(buf.toString(), size);
    NativeMemory.patchCode(ptr.toString(), hex);
  },

  /**
   * Free previously allocated memory.
   *
   * @param address - Address of the allocation.
   * @param size    - Size of the allocation in bytes.
   */
  free(address: NativePointerValue, size: number): void {
    const ptr = new NativePointer(address);
    NativeMemory.freeMemory(ptr.toString(), size);
  }
};
