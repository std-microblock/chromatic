#include "ipc.h"
#include <chrono>
#include <print>
#include <stdexcept>
#include <string>

#include "shared_memory_ipc.h"
#include "ylt/easylog.hpp"

namespace chromatic {
void breeze_ipc::connect(std::string_view name) {
  channel.connect(name.data());
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
  std::string data;
  channel.try_receive(data);
  if (!data.empty()) {
    auto pkt = deserialize<breeze_ipc::packet>(data);
    if (pkt.has_value()) {
      // 检查是否是发给自己的包
      if (pkt->to_pid != 0 && pkt->to_pid != current_pid_) {
        return true; // 不是给自己的包，跳过
      }

      if (pkt->is_fragment) {
        process_fragment(*pkt);
      } else {
        process_packet(std::move(*pkt));
      }
    }
    return true;
  }
  return false;
}

void breeze_ipc::process_packet(packet &&pkt) {
  if (pkt.return_for_call != 0) {
    auto it = call_handlers.find(pkt.return_for_call);
    if (it != call_handlers.end()) {
      it->second(pkt);
      call_handlers.erase(it->first);
    }
  } else {
    auto &handler_list = handlers[pkt.name];
    for (auto &handler : handler_list) {
      handler(pkt);
    }
  }
}

void breeze_ipc::process_fragment(const packet &frag) {
  std::lock_guard lock(fragment_mutex_);

  auto &cache = fragment_cache_[frag.fragment_id];
  if (cache.fragments.empty()) {
    cache.created_time = std::chrono::steady_clock::now();
    cache.base_packet = frag;
    cache.base_packet.is_fragment = false;
    cache.base_packet.data.clear();
    cache.fragments.resize(frag.fragment_total);
  }

  if (frag.fragment_index < frag.fragment_total) {
    cache.fragments[frag.fragment_index] = frag.data;
  }

  bool complete = true;
  for (const auto &fragment : cache.fragments) {
    if (fragment.empty()) {
      complete = false;
      break;
    }
  }

  if (complete) {
    reassemble_and_process(frag.fragment_id);
  }
}

void breeze_ipc::reassemble_and_process(size_t fragment_id) {
  auto it = fragment_cache_.find(fragment_id);
  if (it == fragment_cache_.end())
    return;

  auto &cache = it->second;
  for (const auto &frag : cache.fragments) {
    cache.base_packet.data += frag;
  }

  process_packet(
      std::move(deserialize<packet>(cache.base_packet.data).value()));

  fragment_cache_.erase(it);
}
} // namespace chromatic