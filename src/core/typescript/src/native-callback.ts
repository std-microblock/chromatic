import { NativeFFI, NativePointer } from 'chromatic';
import { ptr } from './native-pointer';
import type { NativeType } from './types';

/**
 * NativeCallback — create native callbacks that dispatch to JavaScript.
 * Compatible with Frida's NativeCallback API.
 *
 * Now uses std::function binding — the JS handler is passed directly to C++.
 * No more __chromaticCallbackDispatch / callbackId mechanism.
 */
export class NativeCallback {
  private _address: NativePointer;

  constructor(
    handler: (...args: any[]) => any,
    retType: NativeType,
    argTypes: NativeType[],
    abi: string = 'default'
  ) {
    // Wrap the user's handler into the form expected by C++:
    //   (args: string[]) => string
    const wrappedHandler = (rawArgs: string[]): string => {
      // Convert raw string args based on types
      const args = rawArgs.map((arg, i) => {
        const type = argTypes[i];
        if (type === 'pointer') return new NativePointer(parseInt(String(arg), 16));
        if (type === 'float' || type === 'double') return Number(arg);
        return Number(arg);
      });

      try {
        const result = handler(...args);

        if (retType === 'void') return '0';
        if (retType === 'pointer') {
          if (result instanceof NativePointer) return result.toString();
          return ptr(result).toString();
        }
        return String(result ?? 0);
      } catch (e) {
        return '0';
      }
    };

    this._address = NativeFFI.createCallback(wrappedHandler, retType, argTypes, abi);
  }

  get address(): NativePointer {
    return this._address;
  }

  destroy(): void {
    NativeFFI.destroyCallback(this._address);
  }
}
