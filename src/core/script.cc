#include "script.h"
#include "bindings/generated_bindings/binding_qjs.h"
#include "bindings/native_breakpoint.h"
#include "bindings/native_exception_handler.h"
#include "bindings/native_hw_breakpoint.h"
#include "bindings/native_interceptor.h"
#include "bindings/native_memory_access_monitor.h"
#include "bindings/script_lifecycle.h"
#include "fmt/base.h"

extern "C" {
extern const uint8_t _binary_index_js_start[];
extern const uint8_t _binary_index_js_end[];
}

std::string index_js = {(const char *)_binary_index_js_start,
                        (const char *)_binary_index_js_end};

namespace chromatic::script {
void runtime::cleanup() {
  // Auto-cleanup all subsystems when the script context is disposed
  chromatic::js::ScriptLifecycle::removeAllDisposeCallbacks();
  chromatic::js::NativeMemoryAccessMonitor::disableAll();
  chromatic::js::NativeHardwareBreakpoint::removeAll();
  chromatic::js::NativeSoftwareBreakpoint::removeAll();
  chromatic::js::NativeInterceptor::detachAll();
  chromatic::js::NativeExceptionHandler::removeAllCallbacks();
  chromatic::js::NativeExceptionHandler::disable();
}
void runtime::reset() {
  // Let JS do its cleanup first via dispose callbacks
  chromatic::js::ScriptLifecycle::_callDisposeCallbacks();
  // Then native cleanup
  cleanup();
  context.on_bind.clear();
  context.on_bind.push_back(
      [this]() { chromatic_bindAll(context.js->addModule("chromatic")); });
  context.reset_runtime();
  if (auto res = context.eval_string(index_js, "<index>"); !res) {
    fmt::print("Failed to eval index.js: {}\n", res.error());
    return;
  }
}
std::expected<qjs::Value, std::string>
runtime::eval_script(const std::string &script, std::string_view filename) {
  return context.eval_string(script, filename);
}
} // namespace chromatic::script
