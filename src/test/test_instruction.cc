// test_instruction.cc — Instruction parse + xref + filter tests
#include "test_common.h"

TEST_F(ChromaticTest, Instruction_Parse) {
  EXPECT_TRUE(jsEval(R"(
    (() => {
      const addr = Module.findExportByName(null, 'malloc');
      const insn = Instruction.parse(addr);

      if (!insn.mnemonic) throw new Error('no mnemonic');
      if (insn.size <= 0) throw new Error('bad size');
    })()
  )"));
}

TEST_F(ChromaticTest, Instruction_ParseMultiple) {
  EXPECT_TRUE(jsEval(R"(
    (() => {
      const addr = Module.findExportByName(null, 'malloc');
      const insn1 = Instruction.parse(addr);
      const insn2 = Instruction.parse(addr.add(insn1.size));
      if (!insn1.mnemonic || !insn2.mnemonic)
        throw new Error('parse multiple failed');
      if (insn1.address.equals(insn2.address))
        throw new Error('same address');
    })()
  )"));
}

TEST_F(ChromaticTest, Instruction_Disassemble) {
  EXPECT_TRUE(jsEval(R"(
    (() => {
      const addr = Module.findExportByName(null, 'malloc');
      const insns = Instruction.disassemble(addr, 5);
      if (insns.length !== 5) throw new Error('expected 5, got ' + insns.length);
      for (const insn of insns) {
        if (!insn.mnemonic) throw new Error('empty mnemonic');
        if (insn.size <= 0) throw new Error('bad size');
      }
    })()
  )"));
}

TEST_F(ChromaticTest, Instruction_Analyze) {
  EXPECT_TRUE(jsEval(R"(
    (() => {
      const addr = Module.findExportByName(null, 'malloc');
      const analysis = Instruction.analyze(addr);
      // Just verify the shape — specific values depend on the instruction
      if (typeof analysis.isBranch !== 'boolean') throw new Error('no isBranch');
      if (typeof analysis.isCall !== 'boolean') throw new Error('no isCall');
      if (typeof analysis.size !== 'number') throw new Error('no size');
      if (analysis.size <= 0) throw new Error('bad analysis size');
    })()
  )"));
}

TEST_F(ChromaticTest, Instruction_FilterInstructions) {
  EXPECT_TRUE(jsEval(R"(
    (() => {
      const addr = Module.findExportByName(null, 'malloc');
      // Filter for instructions with size > 0 (should be all of them)
      const all = Instruction.filterInstructions(addr, 10, (insn) => insn.size > 0);
      if (all.length !== 10) throw new Error('expected 10, got ' + all.length);

      // Filter for a specific mnemonic — we don't know what it is,
      // but at least verify the callback is invoked and filtering works
      const none = Instruction.filterInstructions(addr, 10, (insn) => false);
      if (none.length !== 0) throw new Error('expected 0, got ' + none.length);
    })()
  )"));
}

TEST_F(ChromaticTest, Instruction_FilterInstructionsAsync) {
  EXPECT_TRUE(jsEval(
      R"(                                                                                 
          (async () => {                                                                                       
            const addr = Module.findExportByName(null, 'malloc');                                              
            const results = await Instruction.filterInstructionsAsync(addr, 10, (insn) => insn.size >          
         -0);                                                                                                      
            if (results.length !== 10) throw new Error('expected 10, got ' + results.length);                  
          })()                                                                                                 
        )"));
}

TEST_F(ChromaticTest, Instruction_FindXrefs) {
  // This test verifies findXrefs doesn't crash and returns an array.
  // Finding actual xrefs depends on the binary layout, so we just
  // test the API contract.
  EXPECT_TRUE(jsEval(R"(
    (() => {
      const addr = Module.findExportByName(null, 'malloc');
      // Search a small range around malloc for references — may or may not find any
      const results = Instruction.findXrefs(addr, 256, addr);
      if (!Array.isArray(results)) throw new Error('not array');
      // If we got results, verify their shape
      for (const r of results) {
        if (typeof r.type !== 'string') throw new Error('no type');
        if (typeof r.size !== 'number') throw new Error('no size');
      }
    })()
  )"));
}
