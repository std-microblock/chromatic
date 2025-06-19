#pragma once
#include "breeze-js/script.h"

namespace chromatic {
struct script_engine {
  std::unique_ptr<breeze::script_context> ctx = nullptr;

  script_engine();
  ~script_engine();

private:
  std::thread js_watch_thread;
};
} // namespace chromatic