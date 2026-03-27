// Chromatic — Frida-like instrumentation framework
// Main entry point: registers all Frida-compatible APIs on globalThis

import { console } from 'chromatic';
import { NativePointer, ptr, NULL } from './native-pointer';
import { Int64, UInt64 } from './int64';
import { Memory } from './memory';
import { Process } from './process';
import { Module } from './module';
import { Instruction } from './instruction';
import { NativeFunction } from './native-function';
import { NativeCallback } from './native-callback';
import { hexdump } from './hexdump';
import { Interceptor } from './interceptor/index';
import { ExceptionHandler } from './exception-handler';
import { SoftwareBreakpoint, HardwareBreakpoint } from './breakpoint';
import { MemoryAccessMonitor } from './memory-access-monitor';
import { Script } from './script-lifecycle';

// Register globals (Frida-compatible)
const g = globalThis as any;

// Core classes
g.NativePointer = NativePointer;
g.Int64 = Int64;
g.UInt64 = UInt64;
g.NativeFunction = NativeFunction;
g.NativeCallback = NativeCallback;

// Singletons / namespaces
g.Memory = Memory;
g.Process = Process;
g.Module = Module;
g.Instruction = Instruction;
g.Interceptor = Interceptor;
g.ExceptionHandler = ExceptionHandler;
g.SoftwareBreakpoint = SoftwareBreakpoint;
g.HardwareBreakpoint = HardwareBreakpoint;
g.MemoryAccessMonitor = MemoryAccessMonitor;

// Script lifecycle
g.Script = Script;

// Utility functions
g.ptr = ptr;
g.NULL = NULL;
g.hexdump = hexdump;
g.console = console;
