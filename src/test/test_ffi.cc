// test_ffi.cc — NativeFunction + NativeCallback tests
#include "test_common.h"

TEST_F(ChromaticTest, NativeFunction_Call) {
  std::string code = R"(
    (() => {
      const fn = new NativeFunction(ptr(')" +
                     ptrHex((void *)&chromatic_test_add) +
                     R"('), 'int', ['int', 'int']);
      const r = fn(3, 4);
      if (r !== 7) throw new Error('expected 7, got ' + r);
    })()
  )";
  EXPECT_TRUE(jsEval(code));
}

TEST_F(ChromaticTest, NativeFunction_CallMultiple) {
  std::string code =
      R"(
    (() => {
      const add = new NativeFunction(ptr(')" +
      ptrHex((void *)&chromatic_test_add) + R"('), 'int', ['int', 'int']);
      const mul = new NativeFunction(ptr(')" +
      ptrHex((void *)&chromatic_test_mul) + R"('), 'int', ['int', 'int']);
      if (add(10, 20) !== 30) throw new Error('add');
      if (mul(5, 6) !== 30) throw new Error('mul');
    })()
  )";
  EXPECT_TRUE(jsEval(code));
}

TEST_F(ChromaticTest, NativeCallback_CreateAndCall) {
  EXPECT_TRUE(jsEval(R"(
    (() => {
      const cb = new NativeCallback(function(a, b) {
        return a + b;
      }, 'int', ['int', 'int']);
      if (cb.address.isNull()) throw new Error('null addr');
      const fn = new NativeFunction(cb.address, 'int', ['int', 'int']);
      const r = fn(10, 20);
      if (r !== 30) throw new Error('expected 30, got ' + r);
      cb.destroy();
    })()
  )"));
}

TEST_F(ChromaticTest, NativeCallback_ThrowDoesNotCrash) {
  EXPECT_TRUE(jsEval(R"(
    (() => {
      const cb = new NativeCallback(function(a, b) {
        throw new Error('intentional throw in callback');
      }, 'int', ['int', 'int']);
      const fn = new NativeFunction(cb.address, 'int', ['int', 'int']);
      // Should not crash, just return 0 or default
      const r = fn(1, 2);
      cb.destroy();
    })()
  )"));
}

TEST_F(ChromaticTest, Hexdump) {
  EXPECT_TRUE(jsEval(R"(
    (() => {
      const buf = Memory.alloc(32);
      for (let i = 0; i < 32; i++) buf.add(i).writeU8(i);
      const dump = hexdump(buf, { length: 32 });
      if (!dump || dump.length === 0) throw new Error('empty');
    })()
  )"));
}
