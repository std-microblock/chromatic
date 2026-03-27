// Shared test fixture and helpers for Chromatic tests
#pragma once
#include "core/script.h"
#include "core/bindings/native_breakpoint.h"
#include "core/bindings/native_hw_breakpoint.h"
#include <cstdint>
#include <cstdio>
#include <gtest/gtest.h>
#include <string>

#include <async_simple/coro/Lazy.h>
#include "async_simple/coro/SyncAwait.h"

// Shared runtime — created once, used by all tests
inline chromatic::script::runtime *&getRuntime() {
  static chromatic::script::runtime *g_rt = nullptr;
  return g_rt;
}

// Helper: eval JS and return true if no error
inline bool jsEval(const std::string &code) {
  auto &g_rt = getRuntime();
  auto res = g_rt->eval_script(code, "<test>");
  if (!res) {
    std::fprintf(stderr, "JS error: %s\n", res.error().c_str());
    return false;
  } else {
    syncAwait(res.value().await()).as<std::string>();
  }
  return true;
}

// Helper: format a C pointer as hex string for injection into JS
inline std::string ptrHex(void *p) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "0x%llx",
                (unsigned long long)reinterpret_cast<uintptr_t>(p));
  return buf;
}

// ── Signal test skip mechanism ──
extern bool shouldSkipSignalTests();
#define SKIP_SIGNAL()                                                          \
  if (shouldSkipSignalTests())                                                 \
  GTEST_SKIP() << "Signal tests disabled"

// ── C functions for hooking/calling from JS ──

extern "C" int chromatic_test_add(int a, int b);
extern "C" int chromatic_test_mul(int a, int b);
extern "C" int chromatic_test_sub(int a, int b);
extern "C" void chromatic_test_set_global(int v);
extern "C" int chromatic_test_get_global();

// ── Fixture ──

class ChromaticTest : public ::testing::Test {
protected:
  static void SetUpTestSuite() {
    getRuntime() = new chromatic::script::runtime();
    getRuntime()->reset();
  }
  static void TearDownTestSuite() {
    delete getRuntime();
    getRuntime() = nullptr;
  }

  void TearDown() override {
    // Clean up all breakpoints after each test to prevent slot leaks
    // This ensures tests don't interfere with each other even if they
    // throw exceptions before calling bp.remove()
    chromatic::js::NativeSoftwareBreakpoint::removeAll();
    chromatic::js::NativeHardwareBreakpoint::removeAll();
  }
};
