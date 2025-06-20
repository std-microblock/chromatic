#include "script.h"

#include "../config.h"

#include "bindings/binding_qjs.h"
#include "ylt/easylog.hpp"

namespace chromatic {

script_engine::script_engine() {

  js_watch_thread = std::thread([this]() {

    auto script_dir = config::data_directory() / "scripts";
    if (!std::filesystem::exists(script_dir)) {
      std::filesystem::create_directories(script_dir);
    }

    ctx.on_bind.push_back([this]() {
      auto &mod = ctx.js->addModule("chromatic");

      bindAll(mod);
    });

    ELOGFMT(INFO, "Script engine initialized.");
    ctx.watch_folder(script_dir);
  });
}

script_engine::~script_engine() {
  if (js_watch_thread.joinable()) {
    ctx.stop_signal = std::make_shared<int>(1);
    js_watch_thread.join();
  }
}
} // namespace chromatic