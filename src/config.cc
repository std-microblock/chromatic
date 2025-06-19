#include "config.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>

#include "rfl.hpp"
#include "rfl/DefaultIfMissing.hpp"
#include "rfl/json.hpp"
#include "ylt/easylog.hpp"


#include "utils.h"
#include "windows.h"

namespace chromatic {
std::unique_ptr<config> config::current;
std::vector<std::function<void()>> config::on_reload;

void config::write_config() {
  auto config_file = data_directory() / "config.json";
  std::ofstream ofs(config_file);
  if (!ofs) {
    std::cerr << "Failed to write config file." << std::endl;
    return;
  }

  ofs << rfl::json::write(*config::current);
}
void config::read_config() {
  auto config_file = data_directory() / "config.json";

#ifdef __llvm__
  std::ifstream ifs(config_file);
  if (!std::filesystem::exists(config_file)) {
    auto config_file = data_directory() / "config.json";
    std::ofstream ofs(config_file);
    if (!ofs) {
      std::cerr << "Failed to write config file." << std::endl;
    }

    ofs << R"({
})";
  }
  if (!ifs) {
    std::cerr
        << "Config file could not be opened. Using default config instead."
        << std::endl;
    config::current = std::make_unique<config>();
    config::current->debug_console = true;
  } else {
    std::string json_str;
    std::copy(std::istreambuf_iterator<char>(ifs),
              std::istreambuf_iterator<char>(), std::back_inserter(json_str));

    if (auto json =
            rfl::json::read<config, rfl::NoExtraFields, rfl::DefaultIfMissing>(
                json_str)) {
      config::current = std::make_unique<config>(json.value());
    } else {
      std::cerr << "Failed to read config file: " << json.error().what()
                << "\nUsing default config instead." << std::endl;
      config::current = std::make_unique<config>();
      config::current->debug_console = true;
    }
  }

  for (auto &fn : config::on_reload) {
    try {
      fn();
    } catch (const std::exception &e) {
      ELOGFMT(WARN, "Failed to run on_reload function: {}", e.what());
    }
  }
#else
#pragma message                                                                \
    "We don't support loading config file on MSVC because of a bug in MSVC."
  dbgout("We don't support loading config file when compiled with MSVC "
         "because of a bug in MSVC.");
  config::current = std::make_unique<config>();
  config::current->debug_console = true;
#endif

  if (config::current->debug_console) {
    ShowWindow(GetConsoleWindow(), SW_SHOW);
  } else {
    ShowWindow(GetConsoleWindow(), SW_HIDE);
  }
}

std::filesystem::path config::data_directory() {
  static std::optional<std::filesystem::path> path;
  static std::mutex mtx;
  std::lock_guard lock(mtx);

  if (!path) {
    path = std::filesystem::path(utils::env("USERPROFILE").value()) /
           ".chromatic" / utils::current_executable_path().filename().string();
  }

  if (!std::filesystem::exists(*path)) {
    std::filesystem::create_directories(*path);
  }

  return path.value();
}
void config::run_config_loader() {
  auto config_path = config::data_directory() / "config.json";
  ELOGFMT(INFO, "config file: {}", config_path.string());
  config::read_config();
  std::thread([config_path]() {
    auto last_mod = std::filesystem::last_write_time(config_path);
    while (true) {
      if (std::filesystem::last_write_time(config_path) != last_mod) {
        last_mod = std::filesystem::last_write_time(config_path);
        config::read_config();
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }).detach();
}
std::string config::dump_config() {
  if (!current) {
    return "{}";
  }
  return rfl::json::write(*current);
}
} // namespace chromatic
