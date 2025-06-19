#pragma once
#include <filesystem>
#include <future>
#include <optional>
#include <queue>
#include <string>


namespace chromatic {
namespace utils {
std::optional<std::string> env(const std::string &name);
std::string wstring_to_utf8(std::wstring const &str);
std::wstring utf8_to_wstring(std::string const &str);
std::filesystem::path current_executable_path();
std::filesystem::path get_module_path(void *module_handle = nullptr);
struct task_queue {
public:
  task_queue();

  ~task_queue();

  template <typename F, typename... Args>
  auto add_task(F &&f, Args &&...args)
      -> std::future<std::invoke_result_t<F, Args...>> {
    using return_type = std::invoke_result_t<F, Args...>;

    if (stop) {
      throw std::runtime_error("add_task called on stopped task_queue");
    }

    auto task = std::make_shared<std::packaged_task<return_type()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...));

    std::future<return_type> res = task->get_future();

    {
      std::lock_guard<std::mutex> lock(queue_mutex);
      tasks.emplace([task]() { (*task)(); });
    }

    condition.notify_one();
    return res;
  }

private:
  void run();

  std::thread worker;
  std::queue<std::function<void()>> tasks;
  std::mutex queue_mutex;
  std::condition_variable condition;
  bool stop;
};

} // namespace utils
} // namespace chromatic