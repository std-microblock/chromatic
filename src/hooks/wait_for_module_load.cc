#include "wait_for_module_load.h"
#include "../utils.h"
#include "blook/blook.h"
#include <algorithm>
#include <atomic>
#include <functional>
#include <list>

#include "Windows.h"
#include "blook/process.h"
namespace chromatic::hooks::wait_for_module_load {
struct WaitTask {
  std::function<bool(std::filesystem::path)> verifier;
  std::promise<std::shared_ptr<blook::Module>> promise;

  WaitTask(std::function<bool(std::filesystem::path)> v,
           std::promise<std::shared_ptr<blook::Module>> p)
      : verifier(std::move(v)), promise(std::move(p)) {}
};

static std::list<std::unique_ptr<WaitTask>> wait_tasks;

static void process_module_load(HMODULE mod) {
  auto module_path = utils::get_module_path(mod);
  for (auto it = wait_tasks.begin(); it != wait_tasks.end();) {
    if ((*it)->verifier(module_path)) {
      (*it)->promise.set_value(blook::Process::self()
                                   ->module(module_path.filename().string())
                                   .value());
      it = wait_tasks.erase(it);
    } else {
      ++it;
    }
  }
}

static void ensure_loadlibrary_hooks() {
  static std::atomic_bool installed;
  if (installed.exchange(true))
    return;

  auto self = blook::Process::self();
  auto kernel32 = self->module("kernel32.dll").value();

  static auto LoadLibraryExAHook =
                  kernel32->exports("LoadLibraryExA")->inline_hook(),
              LoadLibraryExWHook =
                  kernel32->exports("LoadLibraryExW")->inline_hook();

  LoadLibraryExAHook->install(+[]([in] LPCSTR lpLibFileName,
                                  HANDLE hFile, [in] DWORD dwFlags) -> HMODULE {
    auto mod = LoadLibraryExAHook->call_trampoline<HMODULE>(lpLibFileName,
                                                            hFile, dwFlags);
    process_module_load(mod);
    return mod;
  });

  LoadLibraryExWHook->install(+[]([in] LPCWSTR lpLibFileName,
                                  HANDLE hFile, [in] DWORD dwFlags) -> HMODULE {
    auto mod = LoadLibraryExWHook->call_trampoline<HMODULE>(lpLibFileName,
                                                            hFile, dwFlags);
    process_module_load(mod);
    return mod;
  });
}

std::future<std::shared_ptr<blook::Module>>
wait_for_module(std::function<bool(std::filesystem::path)> verifier) {
  ensure_loadlibrary_hooks();

  std::promise<std::shared_ptr<blook::Module>> promise;
  auto future = promise.get_future();

  wait_tasks.emplace_back(
      std::make_unique<WaitTask>(std::move(verifier), std::move(promise)));

  return future;
}

static std::future<std::shared_ptr<blook::Module>>
wait_for_module(std::string_view module_name) {
  if (module_name.empty()) {
    throw std::invalid_argument("Module name cannot be empty");
  }

  if (GetModuleHandleA(module_name.data()) != nullptr) {
    return std::async(std::launch::deferred, [module_name =
                                                  std::string(module_name)]() {
      return blook::Process::self()->module(std::string(module_name)).value();
    });
  }
  return wait_for_module([module_name](std::filesystem::path path) {
    return path.filename() == module_name;
  });
};
} // namespace chromatic::hooks::wait_for_module_load