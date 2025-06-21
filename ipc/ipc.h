#pragma once
#include "./shared_memory_ipc.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <expected>
#include <functional>
#include <future>
#include <iostream>
#include <mutex>
#include <print>
#include <sstream>
#include <string>
#include <unordered_map>

#include "rfl.hpp"
#include "rfl/json.hpp"
#include "ylt/easylog.hpp"
#include "ylt/struct_pack.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "Windows.h"

namespace chromatic {
constexpr static bool use_struct_pack = false;
constexpr static bool print_packages = false;
constexpr static size_t MAX_PACKET_SIZE = 1024;

auto serialize = [](const auto &data) {
  if constexpr (use_struct_pack) {
    return struct_pack::serialize<std::string>(data);
  } else {
    return rfl::json::write(data);
  }
};

template <typename T> auto deserialize(const std::string &data) {
  if constexpr (use_struct_pack) {
    return struct_pack::deserialize<T>(data);
  } else {
    return rfl::json::read<T, rfl::NoExtraFields, rfl::DefaultIfMissing>(data);
  }
}

template <typename T>
concept StructPackSerializable = requires(T t) {
  { serialize(t) } -> std::same_as<std::string>;
  deserialize<T>(std::declval<std::string>());
};

struct test_serializable_struct {
  int a;
  float b;
  std::vector<char> c;
};

static_assert(StructPackSerializable<test_serializable_struct>,
              "test_struct should be StructPackSerializable");

struct breeze_ipc {
  struct packet {
    size_t seq;
    size_t return_for_call = 0;
    std::string name;
    std::string data;
    DWORD from_pid = 0;        // 发送方进程ID
    DWORD to_pid = 0;          // 接收方进程ID
    bool is_fragment = false;  // 是否是分包
    size_t fragment_id = 0;    // 分包ID
    size_t fragment_index = 0; // 分包索引
    size_t fragment_total = 0; // 总分包数
  };

  // 分包重组缓存
  struct fragment_cache {
    std::vector<std::string> fragments;
    std::chrono::steady_clock::time_point created_time;
    packet base_packet;
  };

  void connect(std::string_view name);

  inline size_t inc_seq() { return seq++; }
  inline size_t next_fragment_id() { return next_fragment_id_++; }

  // 发送包（可选择目标PID）
  void send(packet &&pkt, DWORD target_pid = 0) {
    pkt.to_pid = target_pid;

    pkt.from_pid = current_pid_;

    auto serialized = serialize(pkt);

    // 分包处理
    if (serialized.size() > MAX_PACKET_SIZE) {
      send_fragmented(pkt, serialized);
      return;
    }

    send_impl(serialized);
  }

  // 实现分包发送
  void send_fragmented(const packet &base_pkt, const std::string &full_data) {
    const size_t fragment_id = next_fragment_id();
    const size_t total_fragments =
        (full_data.size() + MAX_PACKET_SIZE - 1) / MAX_PACKET_SIZE;

    for (size_t i = 0; i < total_fragments; ++i) {
      const size_t start = i * MAX_PACKET_SIZE;
      const size_t end = std::min(start + MAX_PACKET_SIZE, full_data.size());

      packet fragment = base_pkt;
      fragment.is_fragment = true;
      fragment.fragment_id = fragment_id;
      fragment.fragment_index = i;
      fragment.fragment_total = total_fragments;
      fragment.data = full_data.substr(start, end - start);

      auto serialized_fragment = serialize(fragment);
      send_impl(serialized_fragment);
    }
  }

  // 实际发送实现
  void send_impl(const std::string &data) {

    if (!data.empty()) {
      if constexpr (print_packages) {
        if constexpr (use_struct_pack) {
          for (size_t i = 0; i < data.size(); ++i) {
            if (i % 16 == 0 && i != 0) {
              printf("\n");
            }
            char buf[3];
            snprintf(buf, sizeof(buf), "%02x ",
                     static_cast<unsigned char>(data[i]));
            WriteConsoleA(GetStdHandle(STD_OUTPUT_HANDLE), buf, 2, nullptr,
                          nullptr);
          }
        } else {
          if (GetConsoleWindow())
            WriteConsoleA(GetStdHandle(STD_OUTPUT_HANDLE), data.data(),
                          static_cast<DWORD>(data.size()), nullptr, nullptr);
        }
      }
      channel.send(data);
    }
  }

  bool poll();

  // 发送给特定PID
  template <StructPackSerializable T>
  void send(const std::string &name, const T &data, DWORD target_pid = 0) {
    send(
        packet{
            .seq = inc_seq(),
            .return_for_call = 0,
            .name = name,
            .data = serialize(data),
        },
        target_pid);
  }

  template <StructPackSerializable T>
  void broadcast(const std::string &name, const T &data) {
    send(packet{
        .seq = inc_seq(),
        .return_for_call = 0,
        .name = name,
        .data = serialize(data),
    });
  }

  struct listener_remover {
    breeze_ipc &ipc_instance;
    std::string name;
    std::function<void(const packet &)> &handler;
    void remove() {
      auto &handlers = ipc_instance.handlers;
      auto it = handlers.find(name);
      if (it != handlers.end()) {
        auto &handler_list = it->second;

        handler_list.remove_if(
            [&](const std::function<void(const packet &)> &h) {
              return &h == &handler;
            });
      }
    }
  };

  listener_remover add_listener(const std::string &name,
                                std::function<void(const packet &)> &&handler) {
    auto &h = handlers[name];
    h.emplace_back(std::move(handler));

    return listener_remover{*this, name, h.back()};
  }

  template <StructPackSerializable T>
  listener_remover add_listener(const std::string &name,
                                std::function<void(const T &)> &&handler) {
    return add_listener(
        name, [handler = std::move(handler)](const packet &pkt) {
          auto data = deserialize<T>(pkt.data);
          if (data.has_value()) {
            handler(data.value());
          } else {
            throw std::runtime_error("Failed to deserialize packet data");
          }
        });
  }

  template <StructPackSerializable RetVal, StructPackSerializable Arg>
  listener_remover
  add_call_handler(const std::string &name,
                   std::function<RetVal(const Arg &)> &&handler) {
    return add_listener("call_" + name, [this, handler = std::move(handler),
                                         name](const packet &pkt) {
      auto data = deserialize<Arg>(pkt.data);
      if (!data.has_value()) {
        throw std::runtime_error("Failed to deserialize call data for " + name);
      }
      auto result = handler(data.value());
      send(packet{.seq = inc_seq(),
                  .return_for_call = pkt.seq,
                  .name = "call_result_" + name,
                  .data = serialize(result),
                  .to_pid = pkt.from_pid});
    });
  }

  template <StructPackSerializable RetVal>
  listener_remover add_call_handler(const std::string &name,
                                    std::function<RetVal()> &&handler) {
    return add_call_handler<RetVal, bool>(
        name, [handler = std::move(handler)](bool) { return handler(); });
  }

  template <StructPackSerializable RetVal, StructPackSerializable R>
  std::future<RetVal> call(const std::string &name, const R &data,
                           DWORD target_pid = 0) {
    auto seq = inc_seq();
    auto promise = std::make_shared<std::promise<RetVal>>();
    call_handlers[seq] = [promise](const packet &pkt) {
      auto result = deserialize<RetVal>(pkt.data);
      if (result.has_value()) {
        promise->set_value(std::move(result.value()));
      } else {
        promise->set_exception(
            std::make_exception_ptr(std::runtime_error("Call failed")));
      }
    };

    send(packet{
        .seq = seq,
        .return_for_call = 0,
        .name = "call_" + name,
        .data = serialize(data),
        .to_pid = target_pid // 定向发送
    });

    return promise->get_future();
  }

  template <StructPackSerializable RetVal>
  std::future<RetVal> call(const std::string &name, DWORD target_pid = 0) {
    return call<RetVal, bool>(name, true, target_pid);
  }

  template <StructPackSerializable RetVal, StructPackSerializable R>
  std::optional<RetVal>
  call_and_poll(const std::string &name, const R &data, DWORD target_pid = 0,
                std::chrono::milliseconds timeout = std::chrono::seconds(5)) {
    auto future = call<RetVal, R>(name, data, target_pid);
    auto start = std::chrono::steady_clock::now();

    while (std::chrono::steady_clock::now() - start < timeout) {
      if (future.valid() && future.wait_for(std::chrono::milliseconds(0)) ==
                                std::future_status::ready) {
        return future.get();
      }
      poll();
    }
    return std::nullopt;
  }

  template <StructPackSerializable RetVal>
  std::optional<RetVal>
  call_and_poll(const std::string &name, DWORD target_pid = 0,
                std::chrono::milliseconds timeout = std::chrono::seconds(5)) {
    return call_and_poll<RetVal, bool>(name, true, target_pid, timeout);
  }

  ~breeze_ipc();

private:
  void process_packet(packet &&pkt);
  void process_fragment(const packet &frag);
  void reassemble_and_process(size_t fragment_id);

  std::unordered_map<std::string,
                     std::list<std::function<void(const packet &)>>>
      handlers;
  std::unordered_map<size_t, std::function<void(const packet &)>> call_handlers;
  ::ipc::Channel<> channel;
  std::atomic_size_t seq =
      1 + std::chrono::system_clock::now().time_since_epoch().count() / 1000 %
              1000000;
  std::atomic_size_t next_fragment_id_ = 1;
  std::atomic_bool exit_signal = false;
  std::thread ipc_thread;

  DWORD current_pid_ = ::GetCurrentProcessId();

  // 分包重组相关
  std::mutex fragment_mutex_;
  std::unordered_map<size_t, fragment_cache> fragment_cache_;
};
} // namespace chromatic