#include "script.h"
#include "bindings/generated_bindings/binding_qjs.h"

namespace chromatic::script {
void runtime::cleanup() {}
void runtime::run_loop_block() {

}
void runtime::reset() {
    context.on_bind.clear();
    context.on_bind.push_back([this]() {
        bindAll(context.js->addModule("chromatic"));
    });
    context.reset_runtime();
}
} // namespace chromatic::script