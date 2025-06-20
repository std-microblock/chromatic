#include <algorithm>
#include <print>
#include <ranges>
#include <string>
#include <thread>

#include "blook/blook.h"

#include "config.h"
#include "context.h"
#include "ylt/easylog.hpp"

#include "utils.h"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include "Windows.h"

namespace chromatic {
int main() {
  easylog::init_log(easylog::Severity::INFO, "", false, false);

  easylog::add_appender(
      [](std::string_view msg) { OutputDebugStringA(msg.data()); });

  context::init_singleton();
  return 0;
}

} // namespace chromatic

int APIENTRY DllMain(HINSTANCE hInstance, DWORD fdwReason, LPVOID lpvReserved) {
  switch (fdwReason) {
  case DLL_PROCESS_ATTACH: {
    auto cmdline = std::string(GetCommandLineA());

    if (cmdline.contains("--type=gpu")) {
      return 1; // Skip if this is a gpu process
    }

    chromatic::main();

    break;
  }
  }
  return 1;
}