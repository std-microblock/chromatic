import { NativeMemoryAccessMonitor, NativePointer } from 'chromatic';
import { ptr } from './native-pointer';
import type { NativePointerValue } from './types';

export interface MemoryRange {
  address: NativePointerValue;
  size: number;
}

export interface MemoryAccessInfo {
  address: NativePointer;
  pageBase: NativePointer;
  operation: string;
  rangeIndex: number;
}

export interface MemoryAccessMonitorHandle {
  disable(): void;
}

/**
 * MemoryAccessMonitor — detect reads/writes to memory regions (one-shot per range).
 *
 * Uses mprotect to catch memory accesses to watched pages.
 * When an access is detected, the callback fires with details about
 * which address was accessed and whether it was a read or write.
 *
 * Each range fires at most once (one-shot). After the callback fires,
 * the range stays accessible and no further events are generated for it.
 *
 * Call `drain()` to flush pending access events and fire their callbacks.
 * This is also called automatically by `disable()` and `disableAll()`.
 */
export const MemoryAccessMonitor = {
  /**
   * Enable monitoring on one or more memory ranges.
   *
   * @param ranges - Array of `{address, size}` ranges to watch.
   * @param onAccess - Callback fired when any watched range is accessed.
   * @returns A handle with a `disable()` method.
   */
  enable(ranges: MemoryRange[], onAccess: (details: MemoryAccessInfo) => void): MemoryAccessMonitorHandle {
    const addresses = ranges.map(r => ptr(r.address));
    const sizes = ranges.map(r => r.size);
    const id = NativeMemoryAccessMonitor.enable(addresses, sizes,
      (addr: NativePointer, pageBase: NativePointer, op: string, rangeIdx: number) => {
        try {
          onAccess({
            address: addr,
            pageBase: pageBase,
            operation: op,
            rangeIndex: rangeIdx,
          });
        } catch (e) {
          // Swallow
        }
      });
    return {
      disable() {
        NativeMemoryAccessMonitor.disable(id);
      }
    };
  },

  /**
   * Drain pending access events and fire their callbacks.
   * Returns the number of events drained.
   */
  drain(): number {
    return NativeMemoryAccessMonitor.drainPending();
  },

  /**
   * Disable all active monitors.
   */
  disableAll(): void {
    NativeMemoryAccessMonitor.disableAll();
  },
};
