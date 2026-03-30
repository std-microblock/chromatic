#pragma once
#include "breeze-js/script.h"
#include <optional>
#include <string>

namespace chromatic::script {
struct runtime {
  breeze::script_context context;
  ~runtime() { cleanup(); }
  void cleanup();
  void reset();
  std::expected<qjs::Value, std::string>
  eval_script(const std::string &script, std::string_view filename = "<eval>");
};

}; // namespace chromatic::script