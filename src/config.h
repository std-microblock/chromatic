#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <numbers>
#include <vector>

namespace chromatic {

struct config {
  bool debug_console = true;

  struct detector {
    struct chrome {
      bool enable = true;
      std::string chrome_module_name = "";
    } chrome;
  } detector;

  std::string $schema;

  static std::unique_ptr<config> current;
  static void read_config();
  static void write_config();
  static void run_config_loader();
  static std::string dump_config();
  static std::vector<std::function<void()>> on_reload;

  static std::filesystem::path data_directory();
};
} // namespace chromatic