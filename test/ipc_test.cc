#include "ipc.h"
#include <chrono>
#include <gtest/gtest.h>
#include <print>
#include <thread>

using namespace chromatic;

TEST(IPCTest, BasicMessageSendReceive) {
  breeze_ipc ipc1, ipc2;
  ipc1.connect("chromatic://process/1");
  ipc2.connect("chromatic://process/1");

  bool received = false;
  auto remover = ipc2.add_listener(
      "test_msg", [&](const breeze_ipc::packet &pkt) { received = true; });

  ipc1.send("test_msg", test_serializable_struct{1, 2.0f, {'a', 'b', 'c'}});

  std::this_thread::sleep_for(
      std::chrono::milliseconds(100)); // Give time for IPC to process
  EXPECT_TRUE(received);
}

TEST(IPCTest, RPCCall) {
  breeze_ipc server, client;
  server.connect("chromatic://process/");
  client.connect("chromatic://process/");

  auto remover =
      server.add_call_handler<int, int>("add_one", [](int x) { return x + 1; });

  auto future = client.call<int, int>("add_one", 5);

  EXPECT_EQ(future.get(), 6);
}

TEST(IPCTest, StringReturnRPC) {
  breeze_ipc server, client;
  server.connect("chromatic://process/");
  client.connect("chromatic://process/");

  auto remover = server.add_call_handler<std::string, std::string>(
      "echo", [](const std::string &s) { return s; });

  auto future = client.call<std::string, std::string>("echo", "hello world");

  EXPECT_EQ(future.get(), "hello world");
}

TEST(IPCTest, PairReturnRPC) {
  breeze_ipc server, client;
  server.connect("chromatic://process/");
  client.connect("chromatic://process/");

  auto remover = server.add_call_handler<std::pair<int, std::string>, int>(
      "make_pair", [](int x) { return std::make_pair(x, std::to_string(x)); });

  auto future = client.call<std::pair<int, std::string>, int>("make_pair", 42);

  auto result = future.get();
  EXPECT_EQ(result.first, 42);
  EXPECT_EQ(result.second, "42");
}

TEST(IPCTest, StringPairRPC) {
  breeze_ipc server, client;
  server.connect("chromatic://process/");
  client.connect("chromatic://process/");

  auto remover = server.add_call_handler<std::pair<std::string, std::string>,
                                         std::pair<std::string, std::string>>(
      "concat_pair", [](const auto &p) {
        return std::make_pair(p.first + p.second, p.second + p.first);
      });

  auto future = client.call<std::pair<std::string, std::string>,
                            std::pair<std::string, std::string>>(
      "concat_pair", std::make_pair("hello", "world"));

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
  server.connect("chromatic://process/");
  client.connect("chromatic://process/");

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

TEST(IPCTest, ALotOfPackets) {
  breeze_ipc server, client;
  server.connect("chromatic://process/");
  client.connect("chromatic://process/");

  int count = 1000;
  int received = 0;

  auto remover = server.add_listener(
      "test_msg", [&](const breeze_ipc::packet &pkt) { received++; });

  for (int i = 0; i < count; ++i) {
    client.send("test_msg", test_serializable_struct{i, i * 1.1f, {'a', 'b'}});
  }

  std::this_thread::sleep_for(
      std::chrono::seconds(1)); // Give time for IPC to process
  EXPECT_EQ(received, count);
}

TEST(IPCTest, LargePacket) {
  breeze_ipc server, client;
  server.connect("chromatic://process/");
  client.connect("chromatic://process/");

  // ~1MB of data
  std::string large_data(1024 * 1024, 'x');

  auto remover = server.add_call_handler<std::string, std::string>(
      "echo_large", [](const std::string &data) { 
        std::println("Received large data of size: {}", data.size());
        return data;
       });

  auto future = client.call<std::string, std::string>("echo_large", large_data);

  EXPECT_EQ(future.get(), large_data);
}

int main(int argc, char **argv) {

  // auto channel = breeze_ipc{};
  // channel.connect("chromatic://process/");

  // channel.add_call_handler<std::string, std::string>(
  //     "on_blink_parse_html_manipulate",
  //     [](const std::string &ctx) {
  //       std::println("on_blink_parse_html_manipulate called with: {}", ctx);
  //       return "Processed: " + ctx;
  //     });

  // while (1)
  //   ;

  std::string arg(argc > 1 ? argv[1] : "");
  if (arg == "inspect") {
    ipc::Channel channel;
    channel.connect("chromatic://process/");

    std::cout << "Connected to IPC channel." << std::endl;

    std::thread input_thread([&channel]() {
      while (true) {
        std::string input;
        std::getline(std::cin, input);
        channel.send(input);
      }
    });

    while (true) {
      std::string data;
      channel.try_receive(data);
      if (!data.empty()) {
        std::cout << "\033[47;30m["
                  << std::chrono::system_clock::now().time_since_epoch().count()
                  << "] \033[47;0m";

        if (*(char *)data.data() == '{') {
          // Assuming it's a JSON string
          std::string json_str((char *)data.data(), data.size());
          std::println("JSON: {}", json_str);
        } else {
          // Print as hex
          std::cout << "Hex: ";
          for (size_t i = 0; i < data.size(); ++i) {
            if (i % 16 == 0 && i != 0) {
              std::cout << "\n";
            }
            std::cout << std::hex << static_cast<int>(((char *)data.data())[i])
                      << " ";
          }
          std::cout << std::dec << "\n";
        }
      }
    }
  } else {
    try {
      testing::InitGoogleTest(&argc, argv);
      return RUN_ALL_TESTS();
    } catch (const std::exception &e) {
      std::cerr << "Exception during test initialization: " << e.what()
                << std::endl;
      return 1;
    } catch (...) {
      std::cerr << "Unknown exception during test initialization." << std::endl;
      return 1;
    }
  }
}