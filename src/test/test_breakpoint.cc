// test_breakpoint.cc — Software breakpoint and hardware breakpoint tests
#include "test_common.h"

// ════════════════════════════════════════════════════════════════════════
// Software Breakpoint Tests
// ════════════════════════════════════════════════════════════════════════

TEST_F(ChromaticTest, Signal_SoftwareBreakpoint_Basic) {
  SKIP_SIGNAL();
  std::string code = R"(
    (() => {
      let hitCount = 0;
      const target = ptr(')" +
                     ptrHex((void *)&chromatic_test_add) + R"(');

      const bp = SoftwareBreakpoint.set(target, () => {
        hitCount++;
      });

      const fn = new NativeFunction(target, 'int', ['int', 'int']);
      fn(1, 2);
      if (hitCount !== 1) throw new Error('expected 1 hit, got ' + hitCount);

      fn(3, 4);
      if (hitCount !== 2) throw new Error('expected 2 hits, got ' + hitCount);

      bp.remove();

      fn(5, 6);
      if (hitCount !== 2) throw new Error('still firing after remove: ' + hitCount);
    })()
  )";
  EXPECT_TRUE(jsEval(code));
}

TEST_F(ChromaticTest, Signal_SoftwareBreakpoint_OriginalReturnValue) {
  SKIP_SIGNAL();
  std::string code = R"(
    (() => {
      let entered = false;
      const target = ptr(')" +
                     ptrHex((void *)&chromatic_test_add) + R"(');

      const bp = SoftwareBreakpoint.set(target, () => {
        entered = true;
      });

      const fn = new NativeFunction(target, 'int', ['int', 'int']);
      const result = fn(100, 200);
      if (result !== 300) throw new Error('expected 300, got ' + result);
      if (!entered) throw new Error('breakpoint not hit');

      bp.remove();
    })()
  )";
  EXPECT_TRUE(jsEval(code));
}

TEST_F(ChromaticTest, Signal_SoftwareBreakpoint_MultipleBPs) {
  SKIP_SIGNAL();
  std::string code = R"(
    (() => {
      let addCount = 0;
      let mulCount = 0;

      const addTarget = ptr(')" +
                     ptrHex((void *)&chromatic_test_add) + R"(');
      const mulTarget = ptr(')" +
                     ptrHex((void *)&chromatic_test_mul) + R"(');

      const bp1 = SoftwareBreakpoint.set(addTarget, () => { addCount++; });
      const bp2 = SoftwareBreakpoint.set(mulTarget, () => { mulCount++; });

      const addFn = new NativeFunction(addTarget, 'int', ['int', 'int']);
      const mulFn = new NativeFunction(mulTarget, 'int', ['int', 'int']);

      addFn(1, 2);
      mulFn(3, 4);
      addFn(5, 6);

      if (addCount !== 2) throw new Error('addCount=' + addCount);
      if (mulCount !== 1) throw new Error('mulCount=' + mulCount);

      bp1.remove();
      bp2.remove();

      addFn(7, 8);
      mulFn(9, 10);
      if (addCount !== 2) throw new Error('addCount after remove=' + addCount);
      if (mulCount !== 1) throw new Error('mulCount after remove=' + mulCount);
    })()
  )";
  EXPECT_TRUE(jsEval(code));
}

TEST_F(ChromaticTest, Signal_SoftwareBreakpoint_RemoveAll) {
  SKIP_SIGNAL();
  std::string code = R"(
    (() => {
      let count = 0;
      const target = ptr(')" +
                     ptrHex((void *)&chromatic_test_sub) + R"(');

      SoftwareBreakpoint.set(target, () => { count++; });

      const fn = new NativeFunction(target, 'int', ['int', 'int']);
      fn(10, 3);
      if (count !== 1) throw new Error('expected 1, got ' + count);

      SoftwareBreakpoint.removeAll();

      fn(10, 3);
      if (count !== 1) throw new Error('still firing after removeAll: ' + count);
    })()
  )";
  EXPECT_TRUE(jsEval(code));
}

TEST_F(ChromaticTest, Signal_SoftwareBreakpoint_ResetAfterRemove) {
  SKIP_SIGNAL();
  std::string code = R"(
    (() => {
      let count = 0;
      const target = ptr(')" +
                     ptrHex((void *)&chromatic_test_sub) + R"(');
      const fn = new NativeFunction(target, 'int', ['int', 'int']);

      const bp1 = SoftwareBreakpoint.set(target, () => { count++; });
      fn(10, 3);
      if (count !== 1) throw new Error('first attach count=' + count);

      bp1.remove();
      fn(10, 3);
      if (count !== 1) throw new Error('after remove count=' + count);

      const bp2 = SoftwareBreakpoint.set(target, () => { count += 10; });
      fn(10, 3);
      if (count !== 11) throw new Error('re-set count=' + count);
      bp2.remove();
    })()
  )";
  EXPECT_TRUE(jsEval(code));
}

TEST_F(ChromaticTest, Signal_SoftwareBreakpoint_DuplicateSetThrows) {
  SKIP_SIGNAL();
  std::string code = R"(
    (() => {
      const target = ptr(')" +
                     ptrHex((void *)&chromatic_test_add) + R"(');

      const bp = SoftwareBreakpoint.set(target, () => {});

      let threw = false;
      try {
        SoftwareBreakpoint.set(target, () => {});
      } catch (e) {
        threw = true;
      }
      bp.remove();
      if (!threw) throw new Error('expected error when setting duplicate BP');
    })()
  )";
  EXPECT_TRUE(jsEval(code));
}

TEST_F(ChromaticTest, Signal_SoftwareBreakpoint_AllThreeFunctions) {
  SKIP_SIGNAL();
  // Verify BP works correctly on all three test functions simultaneously
  std::string code = R"(
    (() => {
      let addHit = 0, mulHit = 0, subHit = 0;

      const addTarget = ptr(')" +
                     ptrHex((void *)&chromatic_test_add) + R"(');
      const mulTarget = ptr(')" +
                     ptrHex((void *)&chromatic_test_mul) + R"(');
      const subTarget = ptr(')" +
                     ptrHex((void *)&chromatic_test_sub) + R"(');

      const bp1 = SoftwareBreakpoint.set(addTarget, () => { addHit++; });
      const bp2 = SoftwareBreakpoint.set(mulTarget, () => { mulHit++; });
      const bp3 = SoftwareBreakpoint.set(subTarget, () => { subHit++; });

      const addFn = new NativeFunction(addTarget, 'int', ['int', 'int']);
      const mulFn = new NativeFunction(mulTarget, 'int', ['int', 'int']);
      const subFn = new NativeFunction(subTarget, 'int', ['int', 'int']);

      // Call each and verify return values are preserved
      if (addFn(10, 20) !== 30) throw new Error('add broken');
      if (mulFn(5, 6) !== 30) throw new Error('mul broken');
      if (subFn(100, 40) !== 60) throw new Error('sub broken');

      if (addHit !== 1) throw new Error('addHit=' + addHit);
      if (mulHit !== 1) throw new Error('mulHit=' + mulHit);
      if (subHit !== 1) throw new Error('subHit=' + subHit);

      SoftwareBreakpoint.removeAll();

      // After removeAll, all functions work without triggering
      if (addFn(1, 1) !== 2) throw new Error('add after removeAll');
      if (mulFn(2, 3) !== 6) throw new Error('mul after removeAll');
      if (subFn(9, 4) !== 5) throw new Error('sub after removeAll');

      if (addHit !== 1 || mulHit !== 1 || subHit !== 1)
        throw new Error('counts changed after removeAll');
    })()
  )";
  EXPECT_TRUE(jsEval(code));
}

TEST_F(ChromaticTest, Signal_SoftwareBreakpoint_RapidHits) {
  SKIP_SIGNAL();
  // Verify BP can handle many rapid invocations
  std::string code = R"(
    (() => {
      let hitCount = 0;
      const target = ptr(')" +
                     ptrHex((void *)&chromatic_test_add) + R"(');
      const fn = new NativeFunction(target, 'int', ['int', 'int']);

      const bp = SoftwareBreakpoint.set(target, () => { hitCount++; });

      const N = 100;
      for (let i = 0; i < N; i++) {
        const result = fn(i, 1);
        if (result !== i + 1)
          throw new Error('iteration ' + i + ': expected ' + (i+1) + ', got ' + result);
      }

      if (hitCount !== N)
        throw new Error('expected ' + N + ' hits, got ' + hitCount);

      bp.remove();
    })()
  )";
  EXPECT_TRUE(jsEval(code));
}

TEST_F(ChromaticTest, Signal_SoftwareBreakpoint_RemoveIdempotent) {
  SKIP_SIGNAL();
  // Calling remove() multiple times should not crash
  std::string code = R"(
    (() => {
      const target = ptr(')" +
                     ptrHex((void *)&chromatic_test_add) + R"(');
      const bp = SoftwareBreakpoint.set(target, () => {});
      bp.remove();
      bp.remove(); // second remove — should be a no-op
      bp.remove(); // third remove — still no-op
    })()
  )";
  EXPECT_TRUE(jsEval(code));
}

// ════════════════════════════════════════════════════════════════════════
// Hardware Breakpoint Tests
// ════════════════════════════════════════════════════════════════════════

TEST_F(ChromaticTest, Signal_HardwareBreakpoint_MaxSlots) {
  SKIP_SIGNAL();
  EXPECT_TRUE(jsEval(R"(
    (() => {
      const max = HardwareBreakpoint.maxBreakpoints;
      // On macOS ARM64, max is 0 (not supported)
      // On x86_64, max should be 4
      // On Linux ARM64, max should be 4
      if (typeof max !== 'number') throw new Error('maxBreakpoints not a number');
      if (max < 0) throw new Error('negative maxBreakpoints');
    })()
  )"));
}

TEST_F(ChromaticTest, Signal_HardwareBreakpoint_ActiveCount) {
  SKIP_SIGNAL();
  EXPECT_TRUE(jsEval(R"(
    (() => {
      const count = HardwareBreakpoint.activeCount;
      if (count !== 0) throw new Error('expected 0 active, got ' + count);
    })()
  )"));
}

TEST_F(ChromaticTest, Signal_HardwareBreakpoint_UnsupportedPlatformThrows) {
  SKIP_SIGNAL();
  // On platforms where maxBreakpoints is 0 (macOS ARM64), set() should throw
  EXPECT_TRUE(jsEval(R"(
    (() => {
      if (HardwareBreakpoint.maxBreakpoints > 0) return; // skip on supported platforms

      const buf = Memory.alloc(8);
      let threw = false;
      try {
        HardwareBreakpoint.set(buf, 'write', 4, () => {});
      } catch (e) {
        threw = true;
      }
      if (!threw) throw new Error('expected error on unsupported platform');
    })()
  )"));
}

TEST_F(ChromaticTest, Signal_HardwareBreakpoint_ExecuteHit) {
  SKIP_SIGNAL();
  std::string code = R"(
    (() => {
      if (HardwareBreakpoint.maxBreakpoints === 0) return; // skip unsupported

      let hitCount = 0;
      const target = ptr(')" +
                     ptrHex((void *)&chromatic_test_mul) + R"(');

      const bp = HardwareBreakpoint.set(target, 'execute', 1, () => {
        hitCount++;
      });

      if (HardwareBreakpoint.activeCount !== 1)
        throw new Error('activeCount should be 1, got ' + HardwareBreakpoint.activeCount);

      const fn = new NativeFunction(target, 'int', ['int', 'int']);
      const result = fn(7, 8);

      if (hitCount !== 1) throw new Error('expected 1 hit, got ' + hitCount);
      if (result !== 56) throw new Error('expected 56, got ' + result);

      bp.remove();

      if (HardwareBreakpoint.activeCount !== 0)
        throw new Error('activeCount should be 0 after remove');

      // After remove, function still works and BP doesn't fire
      const result2 = fn(3, 3);
      if (result2 !== 9) throw new Error('expected 9, got ' + result2);
      if (hitCount !== 1) throw new Error('hitCount changed after remove: ' + hitCount);
    })()
  )";
  EXPECT_TRUE(jsEval(code));
}

TEST_F(ChromaticTest, Signal_HardwareBreakpoint_WriteWatchpoint) {
  SKIP_SIGNAL();
  EXPECT_TRUE(jsEval(R"(
    (() => {
      if (HardwareBreakpoint.maxBreakpoints === 0) return; // skip unsupported

      let hitCount = 0;
      const buf = Memory.alloc(8);
      buf.writeU32(0);

      const bp = HardwareBreakpoint.set(buf, 'write', 4, () => {
        hitCount++;
      });

      if (HardwareBreakpoint.activeCount !== 1)
        throw new Error('activeCount should be 1');

      // Write to the watched address — should trigger watchpoint
      buf.writeU32(0xDEAD);

      if (hitCount < 1) throw new Error('expected hit on write, got ' + hitCount);

      bp.remove();
    })()
  )"));
}

TEST_F(ChromaticTest, Signal_HardwareBreakpoint_ReadWriteWatchpoint) {
  SKIP_SIGNAL();
  EXPECT_TRUE(jsEval(R"(
    (() => {
      if (HardwareBreakpoint.maxBreakpoints === 0) return; // skip unsupported

      let hitCount = 0;
      const buf = Memory.alloc(8);
      buf.writeU32(42);

      const bp = HardwareBreakpoint.set(buf, 'readwrite', 4, () => {
        hitCount++;
      });

      // Write to the watched address
      buf.writeU32(99);

      if (hitCount < 1) throw new Error('expected hit on rw access, got ' + hitCount);

      bp.remove();
    })()
  )"));
}

TEST_F(ChromaticTest, Signal_HardwareBreakpoint_ActiveCountTracking) {
  SKIP_SIGNAL();
  EXPECT_TRUE(jsEval(R"(
    (() => {
      if (HardwareBreakpoint.maxBreakpoints === 0) return;

      if (HardwareBreakpoint.activeCount !== 0)
        throw new Error('initial activeCount=' + HardwareBreakpoint.activeCount);

      const buf1 = Memory.alloc(8);
      const buf2 = Memory.alloc(8);

      const bp1 = HardwareBreakpoint.set(buf1, 'write', 4, () => {});
      if (HardwareBreakpoint.activeCount !== 1)
        throw new Error('after set 1: ' + HardwareBreakpoint.activeCount);

      const bp2 = HardwareBreakpoint.set(buf2, 'write', 4, () => {});
      if (HardwareBreakpoint.activeCount !== 2)
        throw new Error('after set 2: ' + HardwareBreakpoint.activeCount);

      bp1.remove();
      if (HardwareBreakpoint.activeCount !== 1)
        throw new Error('after remove 1: ' + HardwareBreakpoint.activeCount);

      bp2.remove();
      if (HardwareBreakpoint.activeCount !== 0)
        throw new Error('after remove 2: ' + HardwareBreakpoint.activeCount);
    })()
  )"));
}

TEST_F(ChromaticTest, Signal_HardwareBreakpoint_RemoveAll) {
  SKIP_SIGNAL();
  EXPECT_TRUE(jsEval(R"(
    (() => {
      if (HardwareBreakpoint.maxBreakpoints === 0) return;

      const buf1 = Memory.alloc(8);
      const buf2 = Memory.alloc(8);

      HardwareBreakpoint.set(buf1, 'write', 4, () => {});
      HardwareBreakpoint.set(buf2, 'write', 4, () => {});

      if (HardwareBreakpoint.activeCount !== 2)
        throw new Error('expected 2 active');

      HardwareBreakpoint.removeAll();

      if (HardwareBreakpoint.activeCount !== 0)
        throw new Error('expected 0 after removeAll, got ' + HardwareBreakpoint.activeCount);
    })()
  )"));
}

TEST_F(ChromaticTest, Signal_HardwareBreakpoint_DuplicateAddressThrows) {
  SKIP_SIGNAL();
  EXPECT_TRUE(jsEval(R"(
    (() => {
      if (HardwareBreakpoint.maxBreakpoints === 0) return;

      const buf = Memory.alloc(8);
      const bp = HardwareBreakpoint.set(buf, 'write', 4, () => {});

      let threw = false;
      try {
        HardwareBreakpoint.set(buf, 'write', 4, () => {});
      } catch (e) {
        threw = true;
      }
      bp.remove();
      if (!threw) throw new Error('expected error on duplicate address');
    })()
  )"));
}

TEST_F(ChromaticTest, Signal_HardwareBreakpoint_OverflowThrows) {
  SKIP_SIGNAL();
  EXPECT_TRUE(jsEval(R"(
    (() => {
      const max = HardwareBreakpoint.maxBreakpoints;
      if (max === 0) return;

      const bps = [];
      const bufs = [];
      // Fill all slots
      for (let i = 0; i < max; i++) {
        const buf = Memory.alloc(8);
        bufs.push(buf);
        bps.push(HardwareBreakpoint.set(buf, 'write', 4, () => {}));
      }

      if (HardwareBreakpoint.activeCount !== max)
        throw new Error('expected ' + max + ' active');

      // One more should throw
      let threw = false;
      try {
        const extra = Memory.alloc(8);
        HardwareBreakpoint.set(extra, 'write', 4, () => {});
      } catch (e) {
        threw = true;
      }
      if (!threw) throw new Error('expected overflow error');

      // Clean up
      HardwareBreakpoint.removeAll();
    })()
  )"));
}

TEST_F(ChromaticTest, Signal_HardwareBreakpoint_InvalidTypeThrows) {
  SKIP_SIGNAL();
  EXPECT_TRUE(jsEval(R"(
    (() => {
      if (HardwareBreakpoint.maxBreakpoints === 0) return;

      const buf = Memory.alloc(8);
      let threw = false;
      try {
        HardwareBreakpoint.set(buf, 'invalid_type', 4, () => {});
      } catch (e) {
        threw = true;
      }
      if (!threw) throw new Error('expected error on invalid type');
    })()
  )"));
}

TEST_F(ChromaticTest, Signal_HardwareBreakpoint_RemoveIdempotent) {
  SKIP_SIGNAL();
  EXPECT_TRUE(jsEval(R"(
    (() => {
      if (HardwareBreakpoint.maxBreakpoints === 0) return;

      const buf = Memory.alloc(8);
      const bp = HardwareBreakpoint.set(buf, 'write', 4, () => {});
      bp.remove();
      bp.remove(); // should not crash
      bp.remove(); // should not crash
    })()
  )"));
}

TEST_F(ChromaticTest, Signal_HardwareBreakpoint_ReuseAfterRemove) {
  SKIP_SIGNAL();
  EXPECT_TRUE(jsEval(R"(
    (() => {
      if (HardwareBreakpoint.maxBreakpoints === 0) return;

      const buf = Memory.alloc(8);

      // Set, remove, set again on same address — should reuse slot
      const bp1 = HardwareBreakpoint.set(buf, 'write', 4, () => {});
      bp1.remove();

      const bp2 = HardwareBreakpoint.set(buf, 'write', 4, () => {});
      if (HardwareBreakpoint.activeCount !== 1)
        throw new Error('expected 1 active after re-set');
      bp2.remove();
    })()
  )"));
}

TEST_F(ChromaticTest, Signal_HardwareBreakpoint_ExecuteReturnValuePreserved) {
  SKIP_SIGNAL();
  std::string code = R"(
    (() => {
      if (HardwareBreakpoint.maxBreakpoints === 0) return;

      const target = ptr(')" +
                     ptrHex((void *)&chromatic_test_add) + R"(');
      let entered = false;

      const bp = HardwareBreakpoint.set(target, 'execute', 1, () => {
        entered = true;
      });

      const fn = new NativeFunction(target, 'int', ['int', 'int']);
      const result = fn(123, 456);

      if (!entered) throw new Error('breakpoint not hit');
      if (result !== 579) throw new Error('expected 579, got ' + result);

      bp.remove();
    })()
  )";
  EXPECT_TRUE(jsEval(code));
}

TEST_F(ChromaticTest, Signal_HardwareBreakpoint_MultipleExecuteSlots) {
  SKIP_SIGNAL();
  std::string code = R"(
    (() => {
      if (HardwareBreakpoint.maxBreakpoints < 2) return;

      let addHit = 0, mulHit = 0;
      const addTarget = ptr(')" +
                     ptrHex((void *)&chromatic_test_add) + R"(');
      const mulTarget = ptr(')" +
                     ptrHex((void *)&chromatic_test_mul) + R"(');

      const bp1 = HardwareBreakpoint.set(addTarget, 'execute', 1, () => { addHit++; });
      const bp2 = HardwareBreakpoint.set(mulTarget, 'execute', 1, () => { mulHit++; });

      const addFn = new NativeFunction(addTarget, 'int', ['int', 'int']);
      const mulFn = new NativeFunction(mulTarget, 'int', ['int', 'int']);

      if (addFn(2, 3) !== 5) throw new Error('add broken');
      if (mulFn(4, 5) !== 20) throw new Error('mul broken');

      if (addHit !== 1) throw new Error('addHit=' + addHit);
      if (mulHit !== 1) throw new Error('mulHit=' + mulHit);

      bp1.remove();
      bp2.remove();
    })()
  )";
  EXPECT_TRUE(jsEval(code));
}
