#include "context.h"

#include "config.h"
#include "cpptrace/basic.hpp"
#include "utils.h"
#include "ylt/easylog.hpp"

#include "cpptrace/cpptrace.hpp"
#include "cpptrace/from_current.hpp"

#include "libipc/ipc.h"

#include "Windows.h"
#include "blook/blook.h"
#include <debugapi.h>
#include <fstream>
#include <thread>
#include <unordered_map>

#include "rfl.hpp"
#include "rfl/json.hpp"

#include "./hooks/blink_parse_html_manipulator.h"
#include "./hooks/disable-integrity.h"
#include "./hooks/wait_for_module_load.h"

namespace chromatic {
std::unique_ptr<context> context::current = nullptr;

void context::init_singleton() {
  SetUnhandledExceptionFilter(+[](EXCEPTION_POINTERS *ep) -> long {
    ELOGFMT(FATAL, "Unhandled exception: {}", cpptrace::stacktrace::current());
    Sleep(1000);
    return EXCEPTION_CONTINUE_SEARCH;
  });

  CPPTRACE_TRY {
    if (!current) {
      current = std::make_unique<context>();
      current->init_context();
    }
  }
  CPPTRACE_CATCH(const std::exception &e) {
    ELOGFMT(ERROR, "Failed to initialize context: {} {}", e.what(),
            cpptrace::from_current_exception());
    Sleep(1000);
  }
  catch (...) {
    ELOGFMT(ERROR, "Failed to initialize context: unknown exception: {}",
            cpptrace::from_current_exception());
    Sleep(1000);
  }
}
context::context() {}

void context::detect_process_type() {
  auto cmdline = std::wstring(GetCommandLineW());

  auto &cfg = *config::current.get();
  if (cfg.detector.chrome.enable) {
    auto chrome_module = config::current->detector.chrome.chrome_module_name;

    if (cmdline.find(L"--type=gpu") != std::wstring::npos) {
      type.chrome_type = process_type::chrome_type::gpu;
    } else if (cmdline.find(L"--type=renderer") != std::wstring::npos) {
      type.chrome_type = process_type::chrome_type::renderer;
    } else if (cmdline.find(L"--type=utility") != std::wstring::npos) {
      type.chrome_type = process_type::chrome_type::utility;
    } else if (cmdline.find(L"--type=network") != std::wstring::npos) {
      type.chrome_type = process_type::chrome_type::network;
    } else if (cmdline.find(L"--type=") == std::wstring::npos) {
      type.chrome_type = process_type::chrome_type::main;
    }

    if (type.chrome_type) {
      ELOGFMT(INFO, "Detected Chrome process type: {}",
              static_cast<int>(type.chrome_type.value()));
    } else {
      ELOGFMT(WARN, "Failed to detect Chrome process type.");
    }

    auto proc = blook::Process::self();
    if (chrome_module == "") {
      std::shared_ptr<blook::Module> chrome_mod;
      if (auto mod = proc->module("chrome.dll")) {
        chrome_mod = mod.value();
      } else if (auto mod = proc->module("chromium.dll")) {
        chrome_mod = mod.value();
      } else if (auto mod = proc->module("libcef.dll")) {
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

      if (type.chrome_module) {
        on_before_chrome_startup();
      }
    } else {
      if (GetModuleHandleW(utils::utf8_to_wstring(chrome_module).c_str())) {
        type.chrome_module =
            proc->module(
                    std::filesystem::path(chrome_module).filename().string())
                .value();

        on_before_chrome_startup();
      } else {
        ELOGFMT(WARN, "Chrome module {} not found, waiting for it to load...",
                chrome_module);

        hooks::wait_for_module_load::wait_for_module(
            chrome_module,
            [this](void *mod) {
              if (mod) {
                ELOGFMT(INFO, "Chrome module {} loaded", mod);
                type.chrome_module =
                    blook::Process::self()
                        ->module(
                            utils::get_module_path(mod).filename().string())
                        .value();

                auto entry_hook =
                    type.chrome_module->entry_point()->inline_hook();
                entry_hook->install(
                    [this, entry_hook](size_t a, size_t b, size_t c) {
                      on_before_chrome_startup();
                      return entry_hook->call_trampoline<size_t>(a, b, c);
                    });
              } else {
                ELOGFMT(ERROR, "Failed to load Chrome module");
              }
            })
            .wait();
      }
    }

    if (type.chrome_module) {
      ELOGFMT(
          INFO, "Detected Chrome module: {}",
          utils::get_module_path(type.chrome_module->base().data()).string());
    } else {
      ELOGFMT(WARN, "Chrome module not found, some features may not work.");
    }
  }
}
void context::init_ipc() {
  process_ipc.connect(std::format(
      "chromatic://process/{}",
      std::hash<std::string>{}(utils::current_executable_path().string())));
}
void context::on_before_chrome_startup() {
  ELOGFMT(INFO, "on_before_chrome_startup called");
  blink_parse_html_manipulator::install();
}
void context::init_context() {
  auto cmdline = std::wstring(GetCommandLineW());

  ELOGFMT(INFO, "Command line: {}", utils::wstring_to_utf8(cmdline));
  bool is_probably_main = cmdline.find(L"--type=") == std::wstring::npos,
       is_renderer = cmdline.find(L"--type=renderer") != std::wstring::npos;
  if (!is_probably_main && !is_renderer) {
    ELOGFMT(INFO, "Chromatic is not running in this process.");
    return;
  }

  AllocConsole();
  freopen("CONOUT$", "w", stdout);
  freopen("CONOUT$", "w", stderr);

  init_ipc();

  if (is_probably_main) {
    DWORD mode;
    static HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    GetConsoleMode(h, &mode);
    SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);

    static auto log_msg_raw = [](const auto &msg) {
      std::string msg_formatted =
          std::format("\033[47;30m chromatic \033[0m {}", msg);

      msg_formatted.erase(
          std::find_if(msg_formatted.rbegin(), msg_formatted.rend(),
                       [](unsigned char ch) { return !std::isspace(ch); })
              .base(),
          msg_formatted.end());
      msg_formatted += "\n";
      WriteConsoleA(h, msg_formatted.c_str(),
                    static_cast<DWORD>(msg_formatted.size()), nullptr, nullptr);
    };

    easylog::add_appender(log_msg_raw);

    hooks::windows::disable_integrity();

    config::run_config_loader();
    config::on_reload.push_back([this]() {
      ELOGFMT(INFO, "Config reloaded, broadcasting to other processes.");
      process_ipc.send("config_reload", *config::current);
    });

    ELOGFMT(INFO, "Chromatic v0.0.0, initialized as main process.");

    process_ipc.send("config_reload", *config::current);

    process_ipc.add_call_handler<config>("get_config",
                                         []() { return *config::current; });

    process_ipc.add_call_handler<bool, std::string>(
        "log", [](const std::string &msg) {
          log_msg_raw("[other_proc] " + msg);
          return true;
        });

    std::thread([this]() {
      detect_process_type();
      script = std::make_unique<script_engine>();

      static std::unordered_map<std::string, std::pair<size_t, int32_t>>
          symbol_cache;

      auto symbols_file = config::data_directory() / "symbols.json";
      if (std::filesystem::exists(symbols_file)) {
        try {
          std::ifstream ifs(symbols_file, std::ios::binary);
          if (ifs) {
            symbol_cache = rfl::json::read<std::unordered_map<
                std::string, std::pair<size_t, int32_t>>>(ifs)
                               .value();
          }
        } catch (const std::exception &e) {
          ELOGFMT(ERROR, "Failed to read symbols from {}: {}",
                  symbols_file.string(), e.what());
        }
      }

      process_ipc.add_call_handler<
          bool, std::pair<std::string, std::pair<size_t, int32_t>>>(
          "set_symbol", [](auto &symbol) {
            symbol_cache[symbol.first] = symbol.second;
            std::ofstream ofs(config::data_directory() / "symbols.json",
                              std::ios::binary | std::ios::trunc);
            if (ofs) {
              ofs << rfl::json::write(symbol_cache);
            }
            return true;
          });

      process_ipc.add_call_handler<std::pair<size_t, int32_t>, std::string>(
          "get_symbol", [](const std::string &name) {
            auto it = symbol_cache.find(name);
            if (it != symbol_cache.end()) {
              return it->second;
            }
            return std::pair<size_t, int32_t>{0, 0};
          });

      blink_parse_html_manipulator::register_js();
    }).detach();
  } else if (cmdline.find(L"--type=renderer") != std::wstring::npos) {
    easylog::add_appender([this](std::string_view msg) {
      process_ipc.call<bool, std::string>("log", std::string(msg));
    });

    ELOGFMT(INFO, "requesting config from main process.");
    process_ipc.add_listener<config>("config_reload", [](const config &cfg) {
      ELOGFMT(INFO, "Received config_reload");
      config::current = std::make_unique<config>(cfg);
    });

    auto p = process_ipc.call<config>("get_config");
    for (int i = 0; i < 50; i++)
      if (p.valid() && p.wait_for(std::chrono::milliseconds(0)) ==
                           std::future_status::timeout) {
        process_ipc.poll();
      }

    if (p.valid() &&
        p.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
      config::current = std::make_unique<config>(p.get());
    } else {
      ELOGFMT(WARN, "Failed to get config from main process, using default.");
      config::current = std::make_unique<config>();
    }

    ELOGFMT(INFO, "Chromatic v0.0.0, initialized as renderer process.");

    ELOGFMT(INFO, "Config loaded: {}", config::current->dump_config());

    detect_process_type();
  }
}
} // namespace chromatic