#include "script.h"
#include "bindings/generated_bindings/binding_qjs.h"

namespace chromatic::script {
void runtime::cleanup() {}
void runtime::reset() {
  context.on_bind.clear();
  context.on_bind.push_back(
      [this]() { bindAll(context.js->addModule("chromatic")); });
  context.reset_runtime();
}
std::expected<qjs::Value, std::string>
runtime::eval_script(const std::string &script, std::string_view filename) {
  return context.eval_string(script, filename);
}
} // namespace chromatic::script