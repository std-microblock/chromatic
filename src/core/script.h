#pragma once
#include <optional>
#include <string>
#include "breeze-js/script.h"

namespace chromatic::script {
struct runtime {
    breeze::script_context context;
    void cleanup();
    void reset();
    std::expected<qjs::Value, std::string> eval_script(const std::string &script,
                     std::string_view filename = "<eval>");
};

}; // namespace chromatic