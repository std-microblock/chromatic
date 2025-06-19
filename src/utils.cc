#include "utils.h"

#include "Windows.h"

namespace chromatic::utils {
std::optional<std::string> env(const std::string &name) {
  wchar_t buffer[32767];
  GetEnvironmentVariableW(utf8_to_wstring(name).c_str(), buffer, 32767);
  if (buffer[0] == 0) {
    return std::nullopt;
  }
  return wstring_to_utf8(buffer);
}

std::string wstring_to_utf8(std::wstring const &str) {
  int size_needed = WideCharToMultiByte(CP_UTF8, 0, str.c_str(),
                                        (int)str.size(), NULL, 0, NULL, NULL);
  std::string result(size_needed, 0);
  WideCharToMultiByte(CP_UTF8, 0, str.c_str(), (int)str.size(), &result[0],
                      size_needed, NULL, NULL);
  return result;
}

std::wstring utf8_to_wstring(std::string const &str) {
  int size_needed =
      MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), NULL, 0);
  std::wstring result(size_needed, 0);
  MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), &result[0],
                      size_needed);
  return result;
}
std::filesystem::path current_executable_path() {
  static std::filesystem::path path = chromatic::utils::get_module_path();
  return path;
}
std::filesystem::path get_module_path(void *module_handle) {
  HMODULE hModule = static_cast<HMODULE>(module_handle);
  wchar_t buffer[MAX_PATH];
  if (GetModuleFileNameW(hModule, buffer, MAX_PATH) == 0) {
    return {};
  }

  return std::filesystem::path(buffer);
}

task_queue::task_queue() : stop(false) {
  worker = std::thread(&task_queue::run, this);
}
task_queue::~task_queue() {
  {
    std::lock_guard<std::mutex> lock(queue_mutex);
    stop = true;
  }
  condition.notify_all();
  if (worker.joinable()) {
    worker.join();
  }
}
void task_queue::run() {
  while (true) {
    std::function<void()> task;
    {
      std::unique_lock<std::mutex> lock(queue_mutex);
      condition.wait(lock, [this]() { return stop || !tasks.empty(); });

      if (stop && tasks.empty()) {
        return;
      }

      task = std::move(tasks.front());
      tasks.pop();
    }

    task();
  }
}
} // namespace chromatic::utils