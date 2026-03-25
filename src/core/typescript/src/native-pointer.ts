import { NativeMemory, NativeProcess } from 'chromatic';
import type { NativePointerValue } from './types';

/**
 * NativePointer — represents a native memory address with full 64-bit precision.
 *
 * Uses `BigInt` internally to avoid JavaScript number precision loss above 2^53.
 * Provides a Frida-compatible API for pointer arithmetic, comparison, and
 * direct memory read/write operations.
 */
export class NativePointer {
  private _addr: bigint;

  /**
   * Create a new NativePointer.
   *
   * @param value - Initial address. Accepts another `NativePointer`, a hex
   *   string (with or without `0x` prefix), a `number`, or a `bigint`.
   *   Defaults to `0` (NULL).
   */
  constructor(value: NativePointerValue | bigint | string | number = 0) {
    if (value instanceof NativePointer) {
      this._addr = value._addr;
    } else if (typeof value === 'bigint') {
      this._addr = value;
    } else if (typeof value === 'number') {
      this._addr = BigInt(value);
    } else if (typeof value === 'string') {
      if (value.startsWith('0x') || value.startsWith('0X')) {
        this._addr = BigInt(value);
      } else {
        this._addr = BigInt('0x' + value);
      }
    } else {
      this._addr = BigInt(0);
    }
  }

  // ---- Conversion ----

  /**
   * Return the address as a hex string with `0x` prefix.
   * @param radix - Output radix (default 16). Only `16` and `undefined` produce the `0x` prefix.
   */
  toString(radix?: number): string {
    if (radix === 16 || radix === undefined) {
      return '0x' + this._addr.toString(16);
    }
    return this._addr.toString(radix);
  }

  /** JSON representation (hex string). */
  toJSON(): string {
    return this.toString();
  }

  /** Truncate to a signed 32-bit integer. */
  toInt32(): number {
    return Number(BigInt.asIntN(32, this._addr));
  }

  /** Truncate to an unsigned 32-bit integer. */
  toUInt32(): number {
    return Number(BigInt.asUintN(32, this._addr));
  }

  /** Convert to a JavaScript number (may lose precision for large addresses). */
  toNumber(): number {
    return Number(this._addr);
  }

  /** Internal hex string for passing to C++ */
  private get _hex(): string {
    return '0x' + this._addr.toString(16);
  }

  // ---- Comparison ----

  /** Returns `true` if the pointer is NULL (0). */
  isNull(): boolean {
    return this._addr === 0n;
  }

  /**
   * Test equality with another pointer value.
   * @param other - Value to compare against.
   */
  equals(other: NativePointerValue): boolean {
    return this._addr === new NativePointer(other)._addr;
  }

  /**
   * Compare with another pointer. Returns `-1`, `0`, or `1`.
   * @param other - Value to compare against.
   */
  compare(other: NativePointerValue): number {
    const o = new NativePointer(other)._addr;
    if (this._addr < o) return -1;
    if (this._addr > o) return 1;
    return 0;
  }

  // ---- Arithmetic ----

  /** Add an offset to this pointer and return a new pointer. */
  add(rhs: NativePointerValue | number | bigint): NativePointer {
    return new NativePointer(this._addr + BigInt(typeof rhs === 'number' ? rhs : new NativePointer(rhs as any)._addr));
  }

  /** Subtract an offset from this pointer and return a new pointer. */
  sub(rhs: NativePointerValue | number | bigint): NativePointer {
    return new NativePointer(this._addr - BigInt(typeof rhs === 'number' ? rhs : new NativePointer(rhs as any)._addr));
  }

  /** Bitwise AND. */
  and(rhs: NativePointerValue | number | bigint): NativePointer {
    return new NativePointer(this._addr & new NativePointer(rhs as any)._addr);
  }

  /** Bitwise OR. */
  or(rhs: NativePointerValue | number | bigint): NativePointer {
    return new NativePointer(this._addr | new NativePointer(rhs as any)._addr);
  }

  /** Bitwise XOR. */
  xor(rhs: NativePointerValue | number | bigint): NativePointer {
    return new NativePointer(this._addr ^ new NativePointer(rhs as any)._addr);
  }

  /** Bitwise right shift by `n` bits. */
  shr(n: number): NativePointer {
    return new NativePointer(this._addr >> BigInt(n));
  }

  /** Bitwise left shift by `n` bits. */
  shl(n: number): NativePointer {
    return new NativePointer(this._addr << BigInt(n));
  }

  /** Bitwise NOT, masked to the native pointer size. */
  not(): NativePointer {
    // Mask to pointer size
    const ptrSize = NativeProcess.getPointerSize();
    const mask = ptrSize === 8 ? 0xFFFFFFFFFFFFFFFFn : 0xFFFFFFFFn;
    return new NativePointer((~this._addr) & mask);
  }

  // ---- Memory Read ----

  /** Read an unsigned 8-bit integer at this address. */
  readU8(): number {
    const hex = NativeMemory.readMemory(this._hex, 1);
    return parseInt(hex, 16);
  }

  /** Read a signed 8-bit integer at this address. */
  readS8(): number {
    const v = this.readU8();
    return v > 127 ? v - 256 : v;
  }

  /** Read an unsigned 16-bit integer (little-endian) at this address. */
  readU16(): number {
    const hex = NativeMemory.readMemory(this._hex, 2);
    // Little-endian: reverse byte pairs
    return parseInt(hex.slice(2, 4) + hex.slice(0, 2), 16);
  }

  /** Read a signed 16-bit integer (little-endian) at this address. */
  readS16(): number {
    const v = this.readU16();
    return v > 32767 ? v - 65536 : v;
  }

  /** Read an unsigned 32-bit integer (little-endian) at this address. */
  readU32(): number {
    const hex = NativeMemory.readMemory(this._hex, 4);
    const b = hex.match(/.{2}/g)!;
    return parseInt(b[3] + b[2] + b[1] + b[0], 16);
  }

  readS32(): number {
    const v = this.readU32();
    return v > 0x7FFFFFFF ? v - 0x100000000 : v;
  }

  readU64(): bigint {
    const hex = NativeMemory.readMemory(this._hex, 8);
    const b = hex.match(/.{2}/g)!;
    return BigInt('0x' + b[7] + b[6] + b[5] + b[4] + b[3] + b[2] + b[1] + b[0]);
  }

  readS64(): bigint {
    const v = this.readU64();
    return v > 0x7FFFFFFFFFFFFFFFn ? v - 0x10000000000000000n : v;
  }

  readFloat(): number {
    const hex = NativeMemory.readMemory(this._hex, 4);
    const b = hex.match(/.{2}/g)!;
    const buf = new ArrayBuffer(4);
    const view = new DataView(buf);
    for (let i = 0; i < 4; i++) view.setUint8(i, parseInt(b[i], 16));
    return view.getFloat32(0, true);
  }

  readDouble(): number {
    const hex = NativeMemory.readMemory(this._hex, 8);
    const b = hex.match(/.{2}/g)!;
    const buf = new ArrayBuffer(8);
    const view = new DataView(buf);
    for (let i = 0; i < 8; i++) view.setUint8(i, parseInt(b[i], 16));
    return view.getFloat64(0, true);
  }

  readPointer(): NativePointer {
    const ptrSize = NativeProcess.getPointerSize();
    if (ptrSize === 8) {
      return new NativePointer(this.readU64());
    }
    return new NativePointer(this.readU32());
  }

  readByteArray(length: number): ArrayBuffer {
    const hex = NativeMemory.readMemory(this._hex, length);
    const buf = new ArrayBuffer(length);
    const view = new Uint8Array(buf);
    for (let i = 0; i < length; i++) {
      view[i] = parseInt(hex.substr(i * 2, 2), 16);
    }
    return buf;
  }

  readCString(maxLength: number = 256): string {
    let result = '';
    for (let i = 0; i < maxLength; i++) {
      const ch = this.add(i).readU8();
      if (ch === 0) break;
      result += String.fromCharCode(ch);
    }
    return result;
  }

  readUtf8String(maxLength: number = 256): string {
    // Read raw bytes, then decode as UTF-8
    const bytes: number[] = [];
    for (let i = 0; i < maxLength; i++) {
      const ch = this.add(i).readU8();
      if (ch === 0) break;
      bytes.push(ch);
    }
    // Simple UTF-8 decode
    let result = '';
    let i = 0;
    while (i < bytes.length) {
      const c = bytes[i];
      if (c < 0x80) {
        result += String.fromCharCode(c);
        i++;
      } else if (c < 0xE0) {
        result += String.fromCharCode(((c & 0x1F) << 6) | (bytes[i + 1] & 0x3F));
        i += 2;
      } else if (c < 0xF0) {
        result += String.fromCharCode(((c & 0x0F) << 12) | ((bytes[i + 1] & 0x3F) << 6) | (bytes[i + 2] & 0x3F));
        i += 3;
      } else {
        const cp = ((c & 0x07) << 18) | ((bytes[i + 1] & 0x3F) << 12) | ((bytes[i + 2] & 0x3F) << 6) | (bytes[i + 3] & 0x3F);
        // Convert to surrogate pair
        const offset = cp - 0x10000;
        result += String.fromCharCode(0xD800 + (offset >> 10), 0xDC00 + (offset & 0x3FF));
        i += 4;
      }
    }
    return result;
  }

  // ---- Memory Write ----

  private static _toLeHex(value: bigint, bytes: number): string {
    let hex = '';
    for (let i = 0; i < bytes; i++) {
      const byte = Number((value >> BigInt(i * 8)) & 0xFFn);
      hex += byte.toString(16).padStart(2, '0');
    }
    return hex;
  }

  writeU8(value: number): NativePointer {
    NativeMemory.writeMemory(this._hex, value.toString(16).padStart(2, '0'));
    return this;
  }

  writeU16(value: number): NativePointer {
    NativeMemory.writeMemory(this._hex, NativePointer._toLeHex(BigInt(value), 2));
    return this;
  }

  writeU32(value: number): NativePointer {
    NativeMemory.writeMemory(this._hex, NativePointer._toLeHex(BigInt(value), 4));
    return this;
  }

  writeU64(value: bigint | number): NativePointer {
    NativeMemory.writeMemory(this._hex, NativePointer._toLeHex(BigInt(value), 8));
    return this;
  }

  writeS8(value: number): NativePointer { return this.writeU8(value & 0xFF); }
  writeS16(value: number): NativePointer { return this.writeU16(value & 0xFFFF); }
  writeS32(value: number): NativePointer { return this.writeU32(value >>> 0); }
  writeS64(value: bigint | number): NativePointer { return this.writeU64(BigInt(value)); }

  writeFloat(value: number): NativePointer {
    const buf = new ArrayBuffer(4);
    new DataView(buf).setFloat32(0, value, true);
    let hex = '';
    const view = new Uint8Array(buf);
    for (let i = 0; i < 4; i++) hex += view[i].toString(16).padStart(2, '0');
    NativeMemory.writeMemory(this._hex, hex);
    return this;
  }

  writeDouble(value: number): NativePointer {
    const buf = new ArrayBuffer(8);
    new DataView(buf).setFloat64(0, value, true);
    let hex = '';
    const view = new Uint8Array(buf);
    for (let i = 0; i < 8; i++) hex += view[i].toString(16).padStart(2, '0');
    NativeMemory.writeMemory(this._hex, hex);
    return this;
  }

  writePointer(value: NativePointerValue): NativePointer {
    const ptr = new NativePointer(value);
    const ptrSize = NativeProcess.getPointerSize();
    if (ptrSize === 8) {
      return this.writeU64(ptr._addr);
    }
    return this.writeU32(Number(ptr._addr));
  }

  writeByteArray(bytes: ArrayBuffer | number[]): NativePointer {
    let hex = '';
    const arr = bytes instanceof ArrayBuffer ? new Uint8Array(bytes) : bytes;
    for (let i = 0; i < arr.length; i++) {
      hex += arr[i].toString(16).padStart(2, '0');
    }
    NativeMemory.writeMemory(this._hex, hex);
    return this;
  }

  writeUtf8String(str: string): NativePointer {
    const bytes: number[] = [];
    for (let i = 0; i < str.length; i++) {
      let cp = str.codePointAt(i)!;
      if (cp < 0x80) {
        bytes.push(cp);
      } else if (cp < 0x800) {
        bytes.push(0xC0 | (cp >> 6), 0x80 | (cp & 0x3F));
      } else if (cp < 0x10000) {
        bytes.push(0xE0 | (cp >> 12), 0x80 | ((cp >> 6) & 0x3F), 0x80 | (cp & 0x3F));
      } else {
        bytes.push(0xF0 | (cp >> 18), 0x80 | ((cp >> 12) & 0x3F), 0x80 | ((cp >> 6) & 0x3F), 0x80 | (cp & 0x3F));
        i++; // Skip surrogate pair
      }
    }
    bytes.push(0); // Null terminator
    return this.writeByteArray(bytes);
  }

  /** Convenience: create a NativePointer from various values */
  static from(value: NativePointerValue): NativePointer {
    return new NativePointer(value);
  }

  /** Static NULL pointer */
  static readonly NULL = new NativePointer(0);
}

export function ptr(value: NativePointerValue | string | number | bigint): NativePointer {
  return new NativePointer(value);
}

export const NULL = NativePointer.NULL;
