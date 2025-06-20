#include "ipc.h"
#include <print>
#include <stdexcept>
#include <string>

#include "ylt/easylog.hpp"
namespace chromatic {
void breeze_ipc::connect(std::string_view name) {
  if (!channel.connect(name.data())) {
    throw std::runtime_error("Failed to connect to IPC channel: " +
                             std::string(name));
  }
  ipc_thread = std::thread([this]() {
    ELOGFMT(INFO, "IPC thread started, listening for packets...");
    while (!exit_signal) {
      poll();
    }
  });
}
breeze_ipc::~breeze_ipc() {
  exit_signal = true;
  if (ipc_thread.joinable()) {
    ipc_thread.join();
  }
}
bool breeze_ipc::poll() {
  auto data = channel.recv(100);
  if (!data.empty()) {
    auto pkt = deserialize<breeze_ipc::packet>(std::string((char*)data.data(), data.size()));
    if (pkt.has_value()) {
      if (pkt->return_for_call != 0) {
        auto it = call_handlers.find(pkt->return_for_call);
        if (it != call_handlers.end()) {
          it->second(*pkt);
          call_handlers.erase(it->first);
        }
      } else {
        auto &handler_list = handlers[pkt->name];
        for (auto &handler : handler_list) {
          handler(*pkt);
        }
      }
    }
    return true;
  }

  return false;
}
} // namespace chromatic