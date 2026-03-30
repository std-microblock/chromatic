// test_script_lifecycle.cc — ScriptLifecycle (on_dispose) tests
#include "core/bindings/script_lifecycle.h"
#include "test_common.h"

TEST_F(ChromaticTest, ScriptLifecycle_OnDisposeCalledOnReset) {
  // Use C++ API to register a dispose callback, then call reset()
  // which should trigger it
  bool called = false;
  chromatic::js::ScriptLifecycle::onDispose([&]() { called = true; });

  EXPECT_FALSE(called);

  // reset() calls _callDisposeCallbacks() then cleanup()
  getRuntime()->reset();

  EXPECT_TRUE(called);
}

TEST_F(ChromaticTest, ScriptLifecycle_OnDisposeFromJS) {
  // Register dispose callback from JS using Script.onDispose
  // Verify the registration itself works without crashing
  chromatic::script::runtime runtime;
  std::string code = R"(
    (() => {
      const handle = Script.onDispose(() => {
        console.log('dispose called');
      });
      if (typeof handle.remove !== 'function')
        throw new Error('onDispose should return a handle with remove()');
    })()
  )";
  runtime.reset();
  auto result = runtime.eval_script(code);
  syncAwait(result.value().await());

  runtime.cleanup();
  runtime.reset();
}

TEST_F(ChromaticTest, ScriptLifecycle_MultipleCallbacks) {
  // Use C++ API directly to test multiple callbacks
  int callCount = 0;

  auto id1 = chromatic::js::ScriptLifecycle::onDispose([&]() { callCount++; });
  auto id2 = chromatic::js::ScriptLifecycle::onDispose([&]() { callCount++; });
  auto id3 = chromatic::js::ScriptLifecycle::onDispose([&]() { callCount++; });

  chromatic::js::ScriptLifecycle::_callDisposeCallbacks();
  EXPECT_EQ(callCount, 3);

  // After calling, callbacks should be cleared
  callCount = 0;
  chromatic::js::ScriptLifecycle::_callDisposeCallbacks();
  EXPECT_EQ(callCount, 0);
}

TEST_F(ChromaticTest, ScriptLifecycle_RemoveCallback) {
  int callCount = 0;

  auto id1 =
      chromatic::js::ScriptLifecycle::onDispose([&]() { callCount += 1; });
  auto id2 =
      chromatic::js::ScriptLifecycle::onDispose([&]() { callCount += 10; });

  // Remove the first callback
  chromatic::js::ScriptLifecycle::removeDisposeCallback(id1);

  chromatic::js::ScriptLifecycle::_callDisposeCallbacks();
  EXPECT_EQ(callCount, 10); // Only second callback should have fired
}

TEST_F(ChromaticTest, ScriptLifecycle_ExceptionSafety) {
  int callCount = 0;

  // First callback throws
  chromatic::js::ScriptLifecycle::onDispose([&]() {
    callCount++;
    throw std::runtime_error("test exception");
  });

  // Second callback should still run
  chromatic::js::ScriptLifecycle::onDispose([&]() { callCount++; });

  // Should not crash
  EXPECT_NO_THROW(chromatic::js::ScriptLifecycle::_callDisposeCallbacks());
  EXPECT_EQ(callCount, 2);
}
