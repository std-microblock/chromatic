import { NativeFFI, NativePointer } from 'chromatic';
import { ptr } from './native-pointer';
import type { NativeType, NativePointerValue } from './types';

/**
 * NativeFunction — call native functions from JavaScript.
 * Compatible with Frida's NativeFunction API.
 *
 * Now passes NativePointer directly to C++ for the address parameter.
 */
export class NativeFunction {
  private _address: NativePointer;
  private _retType: NativeType;
  private _argTypes: NativeType[];
  private _abi: string;

  constructor(
    address: NativePointerValue,
    retType: NativeType,
    argTypes: NativeType[],
    abi: string = 'default'
  ) {
    this._address = ptr(address);
    this._retType = retType;
    this._argTypes = argTypes;
    this._abi = abi;

    // Return a callable proxy
    const self = this;
    const callable = function (...args: any[]) {
      return self._call(...args);
    };
    Object.setPrototypeOf(callable, NativeFunction.prototype);
    (callable as any)._address = this._address;
    (callable as any)._retType = this._retType;
    (callable as any)._argTypes = this._argTypes;
    (callable as any)._abi = this._abi;
    return callable as any;
  }

  private _call(...args: any[]): NativePointer | number | bigint | void {
    // Convert arguments to string representations
    const serializedArgs: string[] = args.map((arg, i) => {
      const type = this._argTypes[i];
      if (type === 'pointer') {
        if (arg instanceof NativePointer) return arg.toString();
        if (typeof arg === 'string') return arg;
        if (typeof arg === 'number') return '0x' + arg.toString(16);
        if (typeof arg === 'bigint') return '0x' + arg.toString(16);
        return '0x0';
      }
      if (typeof arg === 'bigint') return arg.toString();
      return String(arg);
    });

    const result = NativeFFI.callFunction(
      this._address,
      this._retType,
      this._argTypes,
      serializedArgs,
      this._abi
    );

    // Parse result based on return type
    if (this._retType === 'void') return undefined;
    if (this._retType === 'pointer') return new NativePointer(parseInt(result, 16));
    if (this._retType === 'float' || this._retType === 'double') return parseFloat(result);
    if (this._retType === 'int64' || this._retType === 'uint64' ||
        this._retType === 'long' || this._retType === 'ulong') {
      return BigInt(result);
    }
    return parseInt(result, 10);
  }

  get address(): NativePointer {
    return this._address;
  }
}
