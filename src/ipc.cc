#include "ipc.h"
#include "ylt/easylog.hpp"
#include <stdexcept>

namespace chromatic {
void breeze_ipc::connect(std::string_view name) {
  if (!channel.connect(name.data())) {
    throw std::runtime_error("Failed to connect to IPC channel: " +
                             std::string(name));
  }

  ipc_thread = std::thread([this]() {
    while (!exit_signal) {
      auto data = channel.recv(100);
      if (!data.empty()) {
        auto pkt = struct_pack::deserialize<breeze_ipc::packet>(data);
        if (pkt.has_value()) {
          if (pkt->return_for_call != 0) {
            auto it = call_handlers.find(pkt->return_for_call);
            if (it != call_handlers.end()) {
              it->second(*pkt);
              call_handlers.erase(it);
            }
          } else {
            auto &handler_list = handlers[pkt->name];
            for (auto &handler : handler_list) {
              handler(*pkt);
            }
          }
        }
      }
    }
  });
}
breeze_ipc::~breeze_ipc() {
  exit_signal = true;
  if (ipc_thread.joinable()) {
    ipc_thread.join();
  }
}
} // namespace chromatic