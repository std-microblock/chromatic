#pragma once
#include "libipc/ipc.h"
#include <atomic>
#include <expected>
#include <functional>
#include <future>
#include <string>
#include <unordered_map>

#include "ylt/struct_pack.hpp"

namespace chromatic {

template <typename T>
concept StructPackSerializable = requires(T t) {
  { struct_pack::serialize(t) } -> std::same_as<std::vector<char>>;

  struct_pack::deserialize<T>(std::declval<std::vector<char>>());
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
    std::vector<char> data;
  };

  void connect(std::string_view name);

  inline size_t inc_seq() { return seq++; }

  void send(packet &&pkt) {
    if (auto data = struct_pack::serialize(pkt); !data.empty()) {
      channel.send(data.data(), data.size());
    }
  }

  bool poll();

  template <StructPackSerializable T>
  void send(const std::string &name, const T &data) {
    send(packet{
        .seq = inc_seq(),
        .return_for_call = 0,
        .name = name,
        .data = struct_pack::serialize(data),
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
          auto data = struct_pack::deserialize<T>(pkt.data);
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
      auto data = struct_pack::deserialize<Arg>(pkt.data);
      if (!data.has_value()) {
        throw std::runtime_error("Failed to deserialize call data for " + name);
      }
      auto result = handler(data.value());
      send(packet{
          .seq = inc_seq(),
          .return_for_call = pkt.seq,
          .name = "call_result_" + name,
          .data = struct_pack::serialize(result),
      });
    });
  }

  template <StructPackSerializable RetVal>
  listener_remover add_call_handler(const std::string &name,
                                    std::function<RetVal()> &&handler) {
    return add_call_handler<RetVal, bool>(
        name, [handler = std::move(handler)](bool) { return handler(); });
  }

  template <StructPackSerializable RetVal, StructPackSerializable R>
  std::future<RetVal> call(const std::string &name, const R &data) {
    auto seq = inc_seq();
    send(packet{
        .seq = seq,
        .return_for_call = 0,
        .name = "call_" + name,
        .data = struct_pack::serialize(data),
    });

    auto promise = std::make_shared<std::promise<RetVal>>();
    call_handlers[seq] = [promise](const packet &pkt) {
      auto result = struct_pack::deserialize<RetVal>(pkt.data);
      if (result.has_value()) {
        promise->set_value(std::move(result.value()));
      } else {
        promise->set_exception(
            std::make_exception_ptr(std::runtime_error("Call failed")));
      }
    };

    return promise->get_future();
  }

  template <StructPackSerializable RetVal> auto call(const std::string &name) {
    return call<RetVal, bool>(name, true);
  }

  template <StructPackSerializable RetVal, StructPackSerializable R>
  std::optional<RetVal>
  call_and_poll(const std::string &name, const R &data,
                std::chrono::milliseconds timeout = std::chrono::seconds(5)) {
    auto future = call<RetVal, R>(name, data);
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
  call_and_poll(const std::string &name,
                std::chrono::milliseconds timeout = std::chrono::seconds(5)) {
    return call_and_poll<RetVal, bool>(name, true, timeout);
  }

  ~breeze_ipc();

private:
  std::unordered_map<std::string,
                     std::list<std::function<void(const packet &)>>>
      handlers;
  std::unordered_map<size_t, std::function<void(const packet &)>> call_handlers;
  ::ipc::channel channel;
  std::atomic_size_t seq =
      1 + std::chrono::system_clock::now().time_since_epoch().count() / 1000 %
              1000000;
  std::atomic_bool exit_signal = false;
  std::thread ipc_thread;
};
} // namespace chromatic