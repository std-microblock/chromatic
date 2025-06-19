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
  std::thread([]() {
    AllocConsole();
    freopen("CONOUT$", "w", stdout);
    freopen("CONOUT$", "w", stderr);

    easylog::init_log(easylog::Severity::INFO, "", false, false);

    easylog::add_appender([](std::string_view msg) {
      auto s = utils::utf8_to_wstring(" " + std::string(msg));

      auto h = GetStdHandle(STD_OUTPUT_HANDLE);
      if (h == INVALID_HANDLE_VALUE) {
        return;
      }

      SetConsoleTextAttribute(h, BACKGROUND_INTENSITY | BACKGROUND_RED |
                                     BACKGROUND_GREEN | BACKGROUND_BLUE);
      std::string header = " chromatic ";
      WriteConsoleA(h, header.c_str(), static_cast<DWORD>(header.size()),
                    nullptr, nullptr);
      SetConsoleTextAttribute(h, FOREGROUND_RED | FOREGROUND_GREEN |
                                     FOREGROUND_BLUE);
      WriteConsoleW(h, s.c_str(), static_cast<DWORD>(s.size()), nullptr,
                    nullptr);
    });

    ELOGFMT(INFO, "Chromatic v0.0.0");
    context::init();
  }).detach();
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