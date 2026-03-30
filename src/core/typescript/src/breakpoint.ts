import { NativeSoftwareBreakpoint, NativeHardwareBreakpoint } from 'chromatic';
import { ptr } from './native-pointer';
import type { NativePointerValue } from './types';

export interface BreakpointListener {
  remove(): void;
}

/**
 * SoftwareBreakpoint — set INT3 (x86) or BRK (ARM64) breakpoints.
 *
 * When the breakpoint is hit, the callback fires in normal execution
 * context (not from a signal handler), and the original instruction
 * is re-executed transparently.
 */
export const SoftwareBreakpoint = {
  /**
   * Set a software breakpoint at the given address.
   *
   * @param target - Address to set the breakpoint at.
   * @param onHit - Callback invoked when the breakpoint is hit.
   * @returns A listener with a `remove()` method.
   */
  set(target: NativePointerValue, onHit: () => void): BreakpointListener {
    const id = NativeSoftwareBreakpoint.set(ptr(target), (_cpuCtx: string) => {
      try {
        onHit();
      } catch (e) {
        // Swallow
      }
    });
    return {
      remove() {
        NativeSoftwareBreakpoint.remove(id);
      }
    };
  },

  /**
   * Remove all software breakpoints.
   */
  removeAll(): void {
    NativeSoftwareBreakpoint.removeAll();
  },
};

/**
 * HardwareBreakpoint — use CPU debug registers for breakpoints/watchpoints.
 *
 * Supports execute breakpoints and data watchpoints (write/readwrite).
 * Limited to 4 slots on x86_64. Not supported on macOS ARM64.
 */
export const HardwareBreakpoint = {
  /**
   * Set a hardware breakpoint or watchpoint.
   *
   * @param target - Address to watch.
   * @param type - "execute" | "write" | "readwrite"
   * @param size - Watchpoint size in bytes (1, 2, 4, or 8). Ignored for execute.
   * @param onHit - Callback invoked when the breakpoint/watchpoint fires.
   * @returns A listener with a `remove()` method.
   */
  set(target: NativePointerValue, type: string, size: number, onHit: () => void): BreakpointListener {
    const id = NativeHardwareBreakpoint.set(ptr(target), type, size, (_cpuCtx: string) => {
      try {
        onHit();
      } catch (e) {
        // Swallow
      }
    });
    return {
      remove() {
        NativeHardwareBreakpoint.remove(id);
      }
    };
  },

  /**
   * Remove all hardware breakpoints.
   */
  removeAll(): void {
    NativeHardwareBreakpoint.removeAll();
  },

  /**
   * Maximum number of hardware breakpoints supported by the platform.
   */
  get maxBreakpoints(): number {
    return NativeHardwareBreakpoint.maxBreakpoints();
  },

  /**
   * Number of currently active hardware breakpoints.
   */
  get activeCount(): number {
    return NativeHardwareBreakpoint.activeCount();
  },
};
