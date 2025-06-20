#include "ipc.h"
#include "libipc/ipc.h"
#include <chrono>
#include <gtest/gtest.h>
#include <print>
#include <thread>


using namespace chromatic;

TEST(IPCTest, BasicMessageSendReceive) {
  breeze_ipc ipc1, ipc2;
  ipc1.connect("test_channel");
  ipc2.connect("test_channel");

  bool received = false;
  auto remover = ipc2.add_listener(
      "test_msg", [&](const breeze_ipc::packet &pkt) { received = true; });

  ipc1.send("test_msg", test_serializable_struct{1, 2.0f, {'a', 'b', 'c'}});

  for (int i = 0; i < 10 && !received; ++i) {
    ipc1.poll();
    ipc2.poll();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  EXPECT_TRUE(received);
}

TEST(IPCTest, RPCCall) {
  breeze_ipc server, client;
  server.connect("rpc_channel");
  client.connect("rpc_channel");

  auto remover =
      server.add_call_handler<int, int>("add_one", [](int x) { return x + 1; });

  auto future = client.call<int, int>("add_one", 5);

  for (int i = 0; i < 10 && future.wait_for(std::chrono::milliseconds(0)) !=
                                std::future_status::ready;
       ++i) {
    server.poll();
    client.poll();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  EXPECT_EQ(future.get(), 6);
}

TEST(IPCTest, StringReturnRPC) {
  breeze_ipc server, client;
  server.connect("string_rpc");
  client.connect("string_rpc");

  auto remover = server.add_call_handler<std::string, std::string>(
      "echo", [](const std::string &s) { return s; });

  auto future = client.call<std::string, std::string>("echo", "hello world");

  for (int i = 0; i < 10 && future.wait_for(std::chrono::milliseconds(0)) !=
                                std::future_status::ready;
       ++i) {
    server.poll();
    client.poll();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  EXPECT_EQ(future.get(), "hello world");
}

TEST(IPCTest, PairReturnRPC) {
  breeze_ipc server, client;
  server.connect("pair_rpc");
  client.connect("pair_rpc");

  auto remover = server.add_call_handler<std::pair<int, std::string>, int>(
      "make_pair", [](int x) { return std::make_pair(x, std::to_string(x)); });

  auto future = client.call<std::pair<int, std::string>, int>("make_pair", 42);

  for (int i = 0; i < 10 && future.wait_for(std::chrono::milliseconds(0)) !=
                                std::future_status::ready;
       ++i) {
    server.poll();
    client.poll();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  auto result = future.get();
  EXPECT_EQ(result.first, 42);
  EXPECT_EQ(result.second, "42");
}

TEST(IPCTest, StringPairRPC) {
  breeze_ipc server, client;
  server.connect("string_pair_rpc");
  client.connect("string_pair_rpc");

  auto remover = server.add_call_handler<std::pair<std::string, std::string>,
                                         std::pair<std::string, std::string>>(
      "concat_pair", [](const auto &p) {
        return std::make_pair(p.first + p.second, p.second + p.first);
      });

  auto future = client.call<std::pair<std::string, std::string>,
                            std::pair<std::string, std::string>>(
      "concat_pair", std::make_pair("hello", "world"));

  for (int i = 0; i < 10 && future.wait_for(std::chrono::milliseconds(0)) !=
                                std::future_status::ready;
       ++i) {
    server.poll();
    client.poll();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  auto result = future.get();
  EXPECT_EQ(result.first, "helloworld");
  EXPECT_EQ(result.second, "worldhello");
}

struct blink_parse_manipulate_context {
  std::string html;
  std::string url;
};

TEST(IPCTest, BlinkContextRPC) {
  breeze_ipc server, client;
  server.connect("blink_rpc");
  client.connect("blink_rpc");

  auto remover = server.add_call_handler<blink_parse_manipulate_context,
                                         blink_parse_manipulate_context>(
      "process_html", [](const auto &ctx) {
        blink_parse_manipulate_context result = ctx;
        result.html += "<!-- processed -->";
        return result;
      });

  blink_parse_manipulate_context original{.html = "<html>test</html>",
                                          .url = "http://example.com"};

  auto future =
      client
          .call<blink_parse_manipulate_context, blink_parse_manipulate_context>(
              "process_html", original);

  for (int i = 0; i < 10 && future.wait_for(std::chrono::milliseconds(0)) !=
                                std::future_status::ready;
       ++i) {
    server.poll();
    client.poll();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  auto result = future.get();
  EXPECT_EQ(result.html, "<html>test</html><!-- processed -->");
  EXPECT_EQ(result.url, "http://example.com");
}

TEST(IPCTest, Serialization) {
  test_serializable_struct original{42, 3.14f, {'x', 'y', 'z'}};
  auto serialized = struct_pack::serialize(original);
  auto deserialized =
      struct_pack::deserialize<test_serializable_struct>(serialized);

  ASSERT_TRUE(deserialized.has_value());
  EXPECT_EQ(deserialized->a, 42);
  EXPECT_FLOAT_EQ(deserialized->b, 3.14f);
  auto vec = std::vector<char>{'x', 'y', 'z'};
  EXPECT_EQ(deserialized->c, vec);
}

int main(int argc, char **argv) {

  auto channel = breeze_ipc{};
  channel.connect("chromatic://process/");

  channel.add_call_handler<std::string, std::string>(
      "on_blink_parse_html_manipulate",
      [](const std::string &ctx) {
        std::println("on_blink_parse_html_manipulate called with: {}", ctx);
        return "Processed: " + ctx;
      });

  while (1)
    ;

  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}