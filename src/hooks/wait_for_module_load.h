#pragma once

#include "blook/module.h"
#include <functional>
#include <future>
namespace chromatic::hooks::wait_for_module_load {
std::future<std::shared_ptr<blook::Module>>
wait_for_module(std::function<bool(std::filesystem::path)> verifier);
std::future<std::shared_ptr<blook::Module>>
wait_for_module(std::string_view module_name, std::function<void(void*)> callback = [](void*) {});
} // namespace chromatic::hooks::wait_for_module_load
