#pragma once
#include <filesystem>
#include <optional>
#include <string>


namespace chromatic {
namespace utils {
std::optional<std::string> env(const std::string &name);
std::string wstring_to_utf8(std::wstring const &str);
std::wstring utf8_to_wstring(std::string const &str);
std::filesystem::path current_executable_path();
std::filesystem::path get_module_path(void *module_handle = nullptr);
} // namespace utils
} // namespace chromatic