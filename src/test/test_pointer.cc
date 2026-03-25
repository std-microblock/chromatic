// test_pointer.cc — NativePointer tests
#include "test_common.h"

TEST_F(ChromaticTest, NativePointer_Construction) {
  EXPECT_TRUE(jsEval(R"(
    (() => {
      const p = new NativePointer(0x1234);
      if (p.toString() !== '0x1234') throw new Error('toString: ' + p.toString());
    })()
  )"));
}

TEST_F(ChromaticTest, NativePointer_FromHexString) {
  EXPECT_TRUE(jsEval(R"(
    (() => {
      const p = new NativePointer('0xdeadbeef');
      if (p.toUInt32() !== 0xdeadbeef) throw new Error('toUInt32: ' + p.toUInt32());
    })()
  )"));
}

TEST_F(ChromaticTest, NativePointer_IsNull) {
  EXPECT_TRUE(jsEval(R"(
    (() => {
      if (!new NativePointer(0).isNull()) throw new Error('0 should be null');
      if (new NativePointer(1).isNull()) throw new Error('1 should not be null');
    })()
  )"));
}

TEST_F(ChromaticTest, NativePointer_Arithmetic) {
  EXPECT_TRUE(jsEval(R"(
    (() => {
      const sum = new NativePointer(100).add(50);
      if (!sum.equals(new NativePointer(150))) throw new Error('add');
      const diff = new NativePointer(200).sub(50);
      if (!diff.equals(new NativePointer(150))) throw new Error('sub');
    })()
  )"));
}

TEST_F(ChromaticTest, NativePointer_Bitwise) {
  EXPECT_TRUE(jsEval(R"(
    (() => {
      const a = new NativePointer(0xFF00);
      const b = new NativePointer(0x0FF0);
      const andR = a.and(b);
      if (andR.toUInt32() !== 0x0F00) throw new Error('and: 0x' + andR.toUInt32().toString(16));
      const orR = a.or(b);
      if (orR.toUInt32() !== 0xFFF0) throw new Error('or: 0x' + orR.toUInt32().toString(16));
      const xorR = a.xor(b);
      if (xorR.toUInt32() !== 0xF0F0) throw new Error('xor: 0x' + xorR.toUInt32().toString(16));
    })()
  )"));
}

TEST_F(ChromaticTest, NativePointer_Compare) {
  EXPECT_TRUE(jsEval(R"(
    (() => {
      const a = new NativePointer(100);
      const b = new NativePointer(200);
      if (a.compare(b) !== -1) throw new Error('a < b');
      if (b.compare(a) !== 1) throw new Error('b > a');
      if (a.compare(new NativePointer(100)) !== 0) throw new Error('a == a');
    })()
  )"));
}
