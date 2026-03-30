import { NativeInterceptor, NativeProcess, NativePointer } from 'chromatic';
import { ptr } from '../native-pointer';
import type {
  NativePointerValue,
  InvocationCallbacks,
  InvocationListener,
  InvocationContext,
  InvocationArgs,
  InvocationReturnValue,
} from '../types';

const isArm64 = NativeProcess.architecture === 'arm64';

/**
 * Interceptor — Frida-compatible inline hook API.
 * Now a thin wrapper around C++ NativeInterceptor (trampoline + relocator done in C++).
 */
export const Interceptor = {
  /**
   * Attach inline hook to `target`.
   */
  attach(target: NativePointerValue, callbacks: InvocationCallbacks): InvocationListener {
    const p = ptr(target);

    const onEnter = (cpuContextPtr: string) => {
      if (!callbacks.onEnter) return;

      const ctx: InvocationContext = {
        context: {} as any,
        threadId: 0,
        returnAddress: new NativePointer(0),
      };

      // Build args proxy
      const argsProxy = new Proxy({} as InvocationArgs, {
        get(_target, prop) {
          if (typeof prop === 'string') {
            const idx = parseInt(prop, 10);
            if (!isNaN(idx)) {
              const ctxPtr = new NativePointer(parseInt(cpuContextPtr, 16));
              const ptrSize = NativeProcess.pointerSize;
              if (isArm64) {
                return ctxPtr.add(idx * ptrSize).readPointer();
              } else {
                const regOffsets = [7, 6, 3, 2, 8, 9];
                if (idx < regOffsets.length) {
                  return ctxPtr.add(regOffsets[idx] * ptrSize).readPointer();
                }
                return new NativePointer(0);
              }
            }
          }
          return undefined;
        },
        set(_target, prop, value) {
          if (typeof prop === 'string') {
            const idx = parseInt(prop, 10);
            if (!isNaN(idx)) {
              const ctxPtr = new NativePointer(parseInt(cpuContextPtr, 16));
              const ptrSize = NativeProcess.pointerSize;
              if (isArm64) {
                ctxPtr.add(idx * ptrSize).writePointer(ptr(value));
              } else {
                const regOffsets = [7, 6, 3, 2, 8, 9];
                if (idx < regOffsets.length) {
                  ctxPtr.add(regOffsets[idx] * ptrSize).writePointer(ptr(value));
                }
              }
              return true;
            }
          }
          return false;
        }
      });

      try {
        callbacks.onEnter!.call(ctx, argsProxy);
      } catch (e) {
        // Swallow errors in hook callbacks to avoid crashing the target
      }
    };

    const onLeave = (cpuContextPtr: string) => {
      if (!callbacks.onLeave) return;

      const ctx: InvocationContext = {
        context: {} as any,
        threadId: 0,
        returnAddress: new NativePointer(0),
      };

      const ctxPtr = new NativePointer(parseInt(cpuContextPtr, 16));
      const ptrSize = NativeProcess.pointerSize;

      const retvalPtr = isArm64 ? ctxPtr : ctxPtr.add(14 * ptrSize);
      const retval = retvalPtr.readPointer() as any as InvocationReturnValue;
      retval.replace = (value: NativePointerValue) => {
        retvalPtr.writePointer(ptr(value));
      };

      try {
        callbacks.onLeave!.call(ctx, retval);
      } catch (e) {
        // Swallow
      }
    };

    const hookId = NativeInterceptor.attach(p, onEnter, onLeave);

    return {
      detach() {
        NativeInterceptor.detach(hookId);
      }
    };
  },

  /**
   * Replace target function with `replacement`. Returns a NativePointer
   * to a trampoline that calls the original function.
   */
  replace(target: NativePointerValue, replacement: NativePointerValue): NativePointer {
    return NativeInterceptor.replace(ptr(target), ptr(replacement));
  },

  /**
   * Revert a hook/replace at `target`.
   */
  revert(target: NativePointerValue): void {
    NativeInterceptor.revert(ptr(target));
  },

  /**
   * Detach all active hooks.
   */
  detachAll(): void {
    NativeInterceptor.detachAll();
  },
};
