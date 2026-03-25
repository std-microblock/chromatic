// main.cc — gtest entry point + C function definitions
#include "test_common.h"

// ── C functions for hooking/calling from JS ──
// These are defined here (once) and declared extern in test_common.h

extern "C" int chromatic_test_add(int a, int b) { return a + b; }
extern "C" int chromatic_test_mul(int a, int b) { return a * b; }
extern "C" int chromatic_test_sub(int a, int b) { return a - b; }

static int g_side_effect = 0;
extern "C" void chromatic_test_set_global(int v) { g_side_effect = v; }
extern "C" int chromatic_test_get_global() { return g_side_effect; }

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
