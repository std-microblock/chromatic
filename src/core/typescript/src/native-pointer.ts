// Re-export NativePointer from C++ bindings.
// The class is now fully implemented in native code.
import { NativePointer } from 'chromatic';

export { NativePointer };

export type NativePointerValue = NativePointer | string | number | bigint;

export function ptr(value: NativePointerValue | string | number | bigint): NativePointer {
  if (value instanceof NativePointer) return value;
  if (typeof value === 'number') return new NativePointer(value);
  if (typeof value === 'bigint') return new NativePointer(Number(value));
  if (typeof value === 'string') {
    if (value.startsWith('0x') || value.startsWith('0X')) {
      return new NativePointer(parseInt(value, 16));
    }
    return new NativePointer(parseInt(value, 16));
  }
  return new NativePointer(0);
}

export const NULL = new NativePointer(0);
