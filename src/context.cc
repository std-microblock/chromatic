#include "context.h"

#include "./hooks/wait_for_module_load.h"
#include "config.h"
#include "cpptrace/basic.hpp"
#include "utils.h"
#include "ylt/easylog.hpp"

#include "cpptrace/cpptrace.hpp"
#include "cpptrace/from_current.hpp"

#include "libipc/ipc.h"

#include "Windows.h"
#include "blook/blook.h"

namespace chromatic {
std::unique_ptr<context> context::current = nullptr;

void context::init() {
  SetUnhandledExceptionFilter(+[](EXCEPTION_POINTERS *ep) -> long {
    ELOGFMT(FATAL, "Unhandled exception: {}", cpptrace::stacktrace::current());
    return EXCEPTION_CONTINUE_SEARCH;
  });

  CPPTRACE_TRY {
    if (!current) {
      current = std::make_unique<context>();
    }
  }
  CPPTRACE_CATCH(const std::exception &e) {
    ELOGFMT(ERROR, "Failed to initialize context: {}",
            cpptrace::from_current_exception());
  }
}
context::context() {
  auto cmdline = std::wstring(GetCommandLineW());

  init_ipc();
  if (cmdline.find(L"--type=") != std::wstring::npos) {
    config::run_config_loader();

    config::on_reload.push_back([this]() {
      ELOGFMT(INFO, "Config reloaded, broadcasting to other processes.");
      process_ipc.send("config_reload", *config::current);
    });

    process_ipc.send("config_reload", *config::current);

    process_ipc.add_call_handler<config>("get_config",
                                         []() { return *config::current; });
  } else {
    process_ipc.add_listener<config>("config_reload", [](const config &cfg) {
      ELOGFMT(INFO, "Received config_reload");
      config::current = std::make_unique<config>(cfg);
    });

    config::current = std::make_unique<config>(process_ipc.call<config>("get_config").get());
  }

  detect_process_type();
}

void context::detect_process_type() {
  auto cmdline = std::wstring(GetCommandLineW());

  auto &cfg = *config::current.get();
  if (cfg.detector.chrome.enable) {
    auto chrome_module = config::current->detector.chrome.chrome_module_name;

    auto proc = blook::Process::self();
    if (chrome_module == "") {
      std::shared_ptr<blook::Module> chrome_mod;
      if (auto mod = proc->module("chrome.dll")) {
        chrome_mod = mod.value();
      } else if (auto mod = proc->module("chromium.dll")) {
        chrome_mod = mod.value();
      } else if (auto mod = proc->process_module()) {
        chrome_mod = mod.value();
      }

      // verify if the module is actually chrome
      constexpr auto chrome_signature =
          "\\content\\browser\\renderer_host\\debug_urls.cc";
      if (chrome_mod && chrome_mod->section(".rdata") &&
          chrome_mod->section(".rdata")->find_one(chrome_signature)) {
        type.chrome_module = chrome_mod;
      } else {
        type.chrome_module = {};
      }
    } else {
      if (GetModuleHandleW(utils::utf8_to_wstring(chrome_module).c_str())) {
        type.chrome_module =
            proc->module(
                    std::filesystem::path(chrome_module).filename().string())
                .value();
      } else {
        ELOGFMT(WARN, "Chrome module {} not found, waiting for it to load...",
                chrome_module);

        type.chrome_module =
            hooks::wait_for_module_load::wait_for_module(chrome_module).get();
      }
    }

    if (type.chrome_module) {
      if (cmdline.find(L"--type=gpu") != std::wstring::npos) {
        type.chrome_type = process_type::chrome_type::gpu;
      } else if (cmdline.find(L"--type=renderer") != std::wstring::npos) {
        type.chrome_type = process_type::chrome_type::renderer;
      } else if (cmdline.find(L"--type=utility") != std::wstring::npos) {
        type.chrome_type = process_type::chrome_type::utility;
      } else if (cmdline.find(L"--type=network") != std::wstring::npos) {
        type.chrome_type = process_type::chrome_type::network;
      } else if (cmdline.find(L"--type=browser") != std::wstring::npos) {
        type.chrome_type = process_type::chrome_type::main;
      }

      if (type.chrome_type) {
        ELOGFMT(
            INFO, "Detected Chrome process type: {}, module: {}",
            static_cast<int>(type.chrome_type.value()),
            utils::get_module_path(type.chrome_module->base().data()).string());
      } else {
        ELOGFMT(WARN, "Failed to detect Chrome process type.");
      }
    } else {
      ELOGFMT(WARN, "Chrome module not found.");
    }
  }
}
void context::init_ipc() {
  process_ipc.connect("chromatic://" +
                      utils::current_executable_path().string() + "/process");
}
} // namespace chromatic