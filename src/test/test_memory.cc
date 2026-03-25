// test_memory.cc — Memory alloc/read/write/copy/protect/scan tests
#include "test_common.h"

TEST_F(ChromaticTest, Memory_AllocReadWriteU32) {
  EXPECT_TRUE(jsEval(R"(
    (() => {
      const p = Memory.alloc(64);
      if (p.isNull()) throw new Error('alloc null');
      p.writeU32(0xCAFEBABE);
      const val = p.readU32();
      if (val !== 0xCAFEBABE) throw new Error('mismatch: 0x' + val.toString(16));
    })()
  )"));
}

TEST_F(ChromaticTest, Memory_Copy) {
  EXPECT_TRUE(jsEval(R"(
    (() => {
      const src = Memory.alloc(16);
      src.writeU32(0x12345678);
      const dst = Memory.alloc(16);
      Memory.copy(dst, src, 4);
      if (dst.readU32() !== 0x12345678) throw new Error('copy mismatch');
    })()
  )"));
}

TEST_F(ChromaticTest, Memory_ReadWriteMultipleTypes) {
  EXPECT_TRUE(jsEval(R"(
    (() => {
      const p = Memory.alloc(64);
      // U8
      p.writeU8(0xFF);
      if (p.readU8() !== 0xFF) throw new Error('U8 mismatch');
      // U16
      p.add(4).writeU16(0x1234);
      if (p.add(4).readU16() !== 0x1234) throw new Error('U16 mismatch');
      // U32
      p.add(8).writeU32(0xDEADBEEF);
      if (p.add(8).readU32() !== 0xDEADBEEF) throw new Error('U32 mismatch');
    })()
  )"));
}

TEST_F(ChromaticTest, Memory_Protect) {
  EXPECT_TRUE(jsEval(R"(
    (() => {
      const p = Memory.alloc(4096);
      Memory.protect(p, 4096, 'r');
      Memory.protect(p, 4096, 'rw');
      p.writeU32(42);
      if (p.readU32() !== 42) throw new Error('protect round-trip failed');
    })()
  )"));
}

TEST_F(ChromaticTest, Memory_Scan) {
  EXPECT_TRUE(jsEval(R"(
    (() => {
      const buf = Memory.alloc(64);
      buf.writeU32(0xDEADBEEF);
      buf.add(4).writeU32(0x00000000);
      buf.add(8).writeU32(0xDEADBEEF);

      const results = Memory.scanSync(buf, 64, 'ef be ad de');
      if (!results || results.length < 2)
        throw new Error('expected at least 2 matches, got ' + (results ? results.length : 0));
    })()
  )"));
}

TEST_F(ChromaticTest, Memory_ScanEmpty) {
  EXPECT_TRUE(jsEval(R"(
    (() => {
      const buf = Memory.alloc(64);
      // Fill with zeros
      for (let i = 0; i < 64; i++) buf.add(i).writeU8(0);

      // Search for a pattern that doesn't exist
      const results = Memory.scanSync(buf, 64, 'ff ff ff ff');
      if (results.length !== 0)
        throw new Error('expected 0 matches, got ' + results.length);
    })()
  )"));
}

TEST_F(ChromaticTest, Memory_ScanWildcard) {
  EXPECT_TRUE(jsEval(R"(
    (() => {
      const buf = Memory.alloc(64);
      buf.writeU32(0xAABBCCDD);
      buf.add(16).writeU32(0xAAFFCCDD);

      // Wildcard in second byte position
      const results = Memory.scanSync(buf, 64, 'dd cc ?? aa');
      if (results.length < 2)
        throw new Error('expected at least 2 wildcard matches, got ' + results.length);
    })()
  )"));
}

TEST_F(ChromaticTest, Memory_ScanModule) {
  EXPECT_TRUE(jsEval(R"(
    (() => {
      // Just verify it doesn't throw — actual content depends on the module
      const mods = Process.enumerateModules();
      if (mods.length === 0) throw new Error('no modules');
      // Scan for a common byte (0x00) in the first module — should find at least one
      const results = Memory.scanModule(mods[0].name, '00 00 00 00');
      // We just check it returns an array (may or may not have matches)
      if (!Array.isArray(results)) throw new Error('not array');
    })()
  )"));
}

TEST_F(ChromaticTest, Memory_ScanAsync) {
  EXPECT_TRUE(jsEval(
      R"(                                                                                
      (async () => {                                                                                      
        const buf = Memory.alloc(64);                                                                     
        buf.writeU32(0xDEADBEEF);                                                                         
        buf.add(8).writeU32(0xDEADBEEF);                                                                  
                                                                                                          
        const results = await Memory.scan(buf, 64, 'ef be ad de');                                        
        if (!results || results.length < 2)                                                               
          throw new Error('async scan: expected at least 2, got ' + (results ? results.length :           
      -0));                                                                                                    
      })()                                                                                                
    )"));
}