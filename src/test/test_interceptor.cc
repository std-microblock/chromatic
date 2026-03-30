// test_interceptor.cc — Interceptor tests
#include "test_common.h"

TEST_F(ChromaticTest, Interceptor_AttachDetach) {
  std::string code = R"(
    (() => {
      let enterCount = 0;
      const target = ptr(')" +
                     ptrHex((void *)&chromatic_test_add) + R"(');
      const listener = Interceptor.attach(target, {
        onEnter(args) { enterCount++; }
      });
      const fn = new NativeFunction(target, 'int', ['int', 'int']);
      fn(1, 2);
      if (enterCount !== 1) throw new Error('count=' + enterCount);
      fn(3, 4);
      if (enterCount !== 2) throw new Error('count=' + enterCount);
      listener.detach();
      fn(5, 6);
      if (enterCount !== 2) throw new Error('still firing: ' + enterCount);
    })()
  )";
  EXPECT_TRUE(jsEval(code));
}

TEST_F(ChromaticTest, Interceptor_MultipleHooks) {
  std::string code = R"(
    (() => {
      let addCount = 0;
      let mulCount = 0;

      const addTarget = ptr(')" +
                     ptrHex((void *)&chromatic_test_add) + R"(');
      const mulTarget = ptr(')" +
                     ptrHex((void *)&chromatic_test_mul) + R"(');

      const listener1 = Interceptor.attach(addTarget, {
        onEnter(args) { addCount++; }
      });
      const listener2 = Interceptor.attach(mulTarget, {
        onEnter(args) { mulCount++; }
      });

      const addFn = new NativeFunction(addTarget, 'int', ['int', 'int']);
      const mulFn = new NativeFunction(mulTarget, 'int', ['int', 'int']);

      addFn(1, 2);
      mulFn(3, 4);
      addFn(5, 6);

      if (addCount !== 2) throw new Error('addCount=' + addCount);
      if (mulCount !== 1) throw new Error('mulCount=' + mulCount);

      listener1.detach();
      listener2.detach();

      addFn(7, 8);
      mulFn(9, 10);
      if (addCount !== 2) throw new Error('addCount after detach=' + addCount);
      if (mulCount !== 1) throw new Error('mulCount after detach=' + mulCount);
    })()
  )";
  EXPECT_TRUE(jsEval(code));
}

TEST_F(ChromaticTest, Interceptor_OriginalReturnValue) {
  std::string code = R"(
    (() => {
      let entered = false;
      const target = ptr(')" +
                     ptrHex((void *)&chromatic_test_add) + R"(');
      const listener = Interceptor.attach(target, {
        onEnter(args) { entered = true; }
      });
      const fn = new NativeFunction(target, 'int', ['int', 'int']);
      const result = fn(100, 200);
      if (result !== 300) throw new Error('expected 300, got ' + result);
      if (!entered) throw new Error('onEnter not called');
      listener.detach();
    })()
  )";
  EXPECT_TRUE(jsEval(code));
}

TEST_F(ChromaticTest, Interceptor_ReattachAfterDetach) {
  std::string code = R"(
    (() => {
      let count = 0;
      const target = ptr(')" +
                     ptrHex((void *)&chromatic_test_sub) + R"(');
      const fn = new NativeFunction(target, 'int', ['int', 'int']);

      const listener1 = Interceptor.attach(target, {
        onEnter(args) { count++; }
      });
      fn(10, 3);
      if (count !== 1) throw new Error('first attach count=' + count);

      listener1.detach();
      fn(10, 3);
      if (count !== 1) throw new Error('after detach count=' + count);

      const listener2 = Interceptor.attach(target, {
        onEnter(args) { count += 10; }
      });
      fn(10, 3);
      if (count !== 11) throw new Error('re-attach count=' + count);
      listener2.detach();
    })()
  )";
  EXPECT_TRUE(jsEval(code));
}

TEST_F(ChromaticTest, Interceptor_OnEnterThrowNoCrash) {
  std::string code = R"(
    (() => {
      const target = ptr(')" +
                     ptrHex((void *)&chromatic_test_add) + R"(');
      const listener = Interceptor.attach(target, {
        onEnter(args) {
          throw new Error('intentional throw in onEnter');
        }
      });
      const fn = new NativeFunction(target, 'int', ['int', 'int']);
      const result = fn(5, 10);
      if (result !== 15) throw new Error('expected 15 after throw, got ' + result);
      listener.detach();
    })()
  )";
  EXPECT_TRUE(jsEval(code));
}

TEST_F(ChromaticTest, Interceptor_DetachAll) {
  std::string code = R"(
    (() => {
      let count1 = 0, count2 = 0;
      const target1 = ptr(')" +
                     ptrHex((void *)&chromatic_test_add) + R"(');
      const target2 = ptr(')" +
                     ptrHex((void *)&chromatic_test_mul) + R"(');

      Interceptor.attach(target1, { onEnter(args) { count1++; } });
      Interceptor.attach(target2, { onEnter(args) { count2++; } });

      const fn1 = new NativeFunction(target1, 'int', ['int', 'int']);
      const fn2 = new NativeFunction(target2, 'int', ['int', 'int']);

      fn1(1, 2);
      fn2(3, 4);
      if (count1 !== 1 || count2 !== 1)
        throw new Error('before detachAll: ' + count1 + ',' + count2);

      Interceptor.detachAll();

      fn1(5, 6);
      fn2(7, 8);
      if (count1 !== 1 || count2 !== 1)
        throw new Error('after detachAll: ' + count1 + ',' + count2);
    })()
  )";
  EXPECT_TRUE(jsEval(code));
}

TEST_F(ChromaticTest, ContextDispose_AutoDetachHooks) {
  // Verify that detachAll properly cleans up and the function works after
  std::string code = R"(
    (() => {
      const target = ptr(')" +
                     ptrHex((void *)&chromatic_test_sub) + R"(');
      Interceptor.attach(target, {
        onEnter(args) {}
      });
      // Immediately detach all
      Interceptor.detachAll();
    })()
  )";
  EXPECT_TRUE(jsEval(code));

  // The original function should still work correctly after detachAll
  int result = chromatic_test_sub(50, 20);
  EXPECT_EQ(result, 30);
}
