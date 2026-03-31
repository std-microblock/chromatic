// test_cmodule.cc — CModule tests (compile C at runtime with TCC)
#include "test_common.h"

// ── Basic compile + call ─────────────────────────────────────────────
TEST_F(ChromaticTest, CModule_CompileAndCall) {
  EXPECT_TRUE(jsEval(R"(
    (() => {
      const cm = new CModule(`
        int add(int a, int b) { return a + b; }
      `);
      const fn = new NativeFunction(cm.add, 'int', ['int', 'int']);
      const r = fn(3, 4);
      if (r !== 7) throw new Error('expected 7, got ' + r);
      cm.dispose();
    })()
  )"));
}

// ── Multiple functions ───────────────────────────────────────────────
TEST_F(ChromaticTest, CModule_MultipleFunctions) {
  EXPECT_TRUE(jsEval(R"(
    (() => {
      const cm = new CModule(`
        int add(int a, int b) { return a + b; }
        int mul(int a, int b) { return a * b; }
        int square(int x) { return x * x; }
      `);
      const add = new NativeFunction(cm.add, 'int', ['int', 'int']);
      const mul = new NativeFunction(cm.mul, 'int', ['int', 'int']);
      const square = new NativeFunction(cm.square, 'int', ['int']);
      if (add(10, 20) !== 30) throw new Error('add failed');
      if (mul(5, 6) !== 30) throw new Error('mul failed');
      if (square(7) !== 49) throw new Error('square failed');
      cm.dispose();
    })()
  )"));
}

// ── Symbols (extern injection) ───────────────────────────────────────
TEST_F(ChromaticTest, CModule_ExternSymbols) {
  EXPECT_TRUE(jsEval(R"(
    (() => {
      const buf = Memory.alloc(4);
      buf.writeS32(0);

      // The symbol address IS the address of counter[], so C code
      // accesses the buffer directly.
      const cm = new CModule(`
        extern int counter[];
        void increment(void) { counter[0]++; }
        int read_counter(void) { return counter[0]; }
      `, { counter: buf });

      const increment = new NativeFunction(cm.increment, 'void', []);
      const read_counter = new NativeFunction(cm.read_counter, 'int', []);

      increment();
      increment();
      increment();
      const val = read_counter();
      if (val !== 3) throw new Error('expected 3, got ' + val);

      // Also verify via direct memory read
      const memVal = buf.readS32();
      if (memVal !== 3) throw new Error('mem expected 3, got ' + memVal);

      cm.dispose();
    })()
  )"));
}

// ── init() auto-call ─────────────────────────────────────────────────
TEST_F(ChromaticTest, CModule_InitFunction) {
  EXPECT_TRUE(jsEval(R"(
    (() => {
      const buf = Memory.alloc(4);
      buf.writeS32(0);

      const cm = new CModule(`
        extern int flag[];
        void init(void) { flag[0] = 42; }
        int get_flag(void) { return flag[0]; }
      `, { flag: buf });

      // init() should have been called automatically
      const val = buf.readS32();
      if (val !== 42) throw new Error('init not called, flag=' + val);
      cm.dispose();
    })()
  )"));
}

// ── finalize() auto-call on dispose ──────────────────────────────────
TEST_F(ChromaticTest, CModule_FinalizeOnDispose) {
  EXPECT_TRUE(jsEval(R"(
    (() => {
      const buf = Memory.alloc(4);
      buf.writeS32(0);

      const cm = new CModule(`
        extern int flag[];
        void finalize(void) { flag[0] = 99; }
      `, { flag: buf });

      // Before dispose: should be 0 (no finalize yet)
      if (buf.readS32() !== 0) throw new Error('premature finalize');

      cm.dispose();

      // After dispose: finalize should have been called
      const val = buf.readS32();
      if (val !== 99) throw new Error('finalize not called, flag=' + val);
    })()
  )"));
}

// ── Compilation error throws ─────────────────────────────────────────
TEST_F(ChromaticTest, CModule_CompileError) {
  EXPECT_TRUE(jsEval(R"(
    (() => {
      let threw = false;
      try {
        const cm = new CModule(`
          this is not valid C code !!!
        `);
      } catch (e) {
        threw = true;
      }
      if (!threw) throw new Error('expected compile error to throw');
    })()
  )"));
}

// ── listSymbols ──────────────────────────────────────────────────────
TEST_F(ChromaticTest, CModule_ListSymbols) {
  EXPECT_TRUE(jsEval(R"(
    (() => {
      const cm = new CModule(`
        int foo(void) { return 1; }
        int bar(void) { return 2; }
      `);

      // The symbol properties should be set on cm
      if (!cm.foo) throw new Error('missing foo');
      if (!cm.bar) throw new Error('missing bar');
      if (cm.foo.isNull()) throw new Error('foo is null');
      if (cm.bar.isNull()) throw new Error('bar is null');

      // Call them
      const foo = new NativeFunction(cm.foo, 'int', []);
      const bar = new NativeFunction(cm.bar, 'int', []);
      if (foo() !== 1) throw new Error('foo() != 1');
      if (bar() !== 2) throw new Error('bar() != 2');

      cm.dispose();
    })()
  )"));
}

// ── Using standard types from preamble ───────────────────────────────
TEST_F(ChromaticTest, CModule_StdTypes) {
  EXPECT_TRUE(jsEval(R"(
    (() => {
      const cm = new CModule(`
        int test_types(void) {
          uint8_t a = 255;
          int32_t b = -100;
          uint64_t c = 1000000;
          size_t d = sizeof(void*);
          return (int)(a + b + (int)c + (int)d);
        }
      `);
      const fn = new NativeFunction(cm.test_types, 'int', []);
      const r = fn();
      // 255 + (-100) + 1000000 + 8 = 1000163 (on 64-bit)
      if (r !== 1000163) throw new Error('expected 1000163, got ' + r);
      cm.dispose();
    })()
  )"));
}

// ── String operations from preamble ──────────────────────────────────
TEST_F(ChromaticTest, CModule_StringOps) {
  EXPECT_TRUE(jsEval(R"(
    (() => {
      const buf = Memory.alloc(256);

      const cm = new CModule(`
        extern char buffer[];
        void write_hello(void) {
          const char *src = "Hello, CModule!";
          int i = 0;
          while (src[i]) { buffer[i] = src[i]; i++; }
          buffer[i] = 0;
        }
        int get_length(void) {
          int len = 0;
          while (buffer[len]) len++;
          return len;
        }
      `, { buffer: buf });

      const write_hello = new NativeFunction(cm.write_hello, 'void', []);
      const get_length = new NativeFunction(cm.get_length, 'int', []);

      write_hello();
      const len = get_length();
      if (len !== 15) throw new Error('expected 15, got ' + len);

      const str = buf.readUtf8String(256);
      if (str !== 'Hello, CModule!') throw new Error('got: ' + str);

      cm.dispose();
    })()
  )"));
}
