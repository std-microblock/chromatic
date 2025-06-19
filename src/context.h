#pragma once

#include "blook/module.h"
#include "config.h"
#include "ipc.h"
#include "script/script.h"
#include <memory>
#include <string>

namespace chromatic {
struct context {
  static std::unique_ptr<context> current;

  static void init();

  struct process_type {
    enum chrome_type { main, renderer, gpu, utility, network };

    std::optional<chrome_type> chrome_type = {};
    std::shared_ptr<blook::Module> chrome_module = {};
  };

  process_type type = {};
  
  breeze_ipc process_ipc;
  std::unique_ptr<script_engine> script = nullptr;

  void init_ipc();

  context();

private:
  void detect_process_type();
};
} // namespace chromatic