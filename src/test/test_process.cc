// test_process.cc — Process/Module tests
#include "test_common.h"

TEST_F(ChromaticTest, Process_Arch) {
  EXPECT_TRUE(jsEval(R"(
    (() => {
      const a = Process.arch;
      if (a !== 'arm64' && a !== 'x64') throw new Error('bad arch: ' + a);
    })()
  )"));
}

TEST_F(ChromaticTest, Process_Platform) {
  EXPECT_TRUE(jsEval(R"(
    (() => {
      const p = Process.platform;
      if (!['windows','linux','darwin','android'].includes(p))
        throw new Error('bad platform: ' + p);
    })()
  )"));
}

TEST_F(ChromaticTest, Process_PointerSize) {
  EXPECT_TRUE(jsEval(R"(
    (() => {
      const s = Process.pointerSize;
      if (s !== 4 && s !== 8) throw new Error('bad: ' + s);
    })()
  )"));
}

TEST_F(ChromaticTest, Process_PageSize) {
  EXPECT_TRUE(jsEval(R"(
    (() => {
      if (Process.pageSize < 4096) throw new Error('bad pageSize');
    })()
  )"));
}

TEST_F(ChromaticTest, Process_EnumerateRanges) {
  EXPECT_TRUE(jsEval(R"(
    (() => {
      const ranges = Process.enumerateRanges('r--');
      if (!Array.isArray(ranges)) throw new Error('not array');
      if (ranges.length === 0) throw new Error('no ranges');
      const r = ranges[0];
      if (typeof r.base !== 'object') throw new Error('base not ptr');
      if (typeof r.size !== 'number') throw new Error('no size');
      if (typeof r.protection !== 'string') throw new Error('no protection');
    })()
  )"));
}

TEST_F(ChromaticTest, Module_EnumerateModules) {
  EXPECT_TRUE(jsEval(R"(
    (() => {
      const mods = Process.enumerateModules();
      if (mods.length === 0) throw new Error('no modules');
      const m = mods[0];
      if (typeof m.name !== 'string') throw new Error('no name');
      if (typeof m.base !== 'object') throw new Error('base not ptr');
      if (typeof m.size !== 'number') throw new Error('no size');
      if (typeof m.path !== 'string') throw new Error('no path');
    })()
  )"));
}

TEST_F(ChromaticTest, Module_FindExportByName) {
  EXPECT_TRUE(jsEval(R"(
    (() => {
      const addr = Module.findExportByName(null, 'malloc');
      if (!addr || addr.isNull()) throw new Error('malloc not found');
    })()
  )"));
}

TEST_F(ChromaticTest, Module_EnumerateExports) {
  EXPECT_TRUE(jsEval(R"(
    (() => {
      const mods = Process.enumerateModules();
      let found = false;
      for (const m of mods) {
        const exports = Module.enumerateExports(m.name);
        if (exports.length > 0) {
          const ex = exports[0];
          if (typeof ex.name !== 'string') throw new Error('no name');
          if (typeof ex.address !== 'object') throw new Error('no address');
          found = true;
          break;
        }
      }
      if (!found) throw new Error('no module with exports');
    })()
  )"));
}

TEST_F(ChromaticTest, Module_FindModuleByAddress) {
  EXPECT_TRUE(jsEval(R"(
    (() => {
      const addr = Module.findExportByName(null, 'malloc');
      const mod = Process.findModuleByAddress(addr);
      if (!mod) throw new Error('findModuleByAddress returned null');
      if (typeof mod.name !== 'string') throw new Error('no name');
    })()
  )"));
}

TEST_F(ChromaticTest, Module_Load) {
  EXPECT_TRUE(jsEval(R"(
    (() => {
      const mods = Process.enumerateModules();
      if (mods.length === 0) throw new Error('no modules');
      const m = Module.load(mods[0].name);
      if (!m) throw new Error('Module.load returned null');
      if (m.size <= 0) throw new Error('bad size');
    })()
  )"));
}
