#include "blink_parse_html_manipulator.h"

#include "../context.h"
#include "../utils.h"
#include "blook/function.h"
#include "ylt/easylog.hpp"
#include <chrono>
#include <debugapi.h>
#include <future>
#include <thread>

#include "blook/blook.h"
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include "Windows.h"

#include "../script/bindings/binding_types.h"

extern std::list<std::function<std::string(std::string)>>
    blink_parse_html_manipulators;

namespace chromatic {
struct BlinkHTMLData {
  const std::span<char> &data;
  std::optional<std::vector<char>> replacement;
};

struct BlinkUtilSpan {
  char *data;
  size_t size;
};

void blink_parse_html_manipulator::install() {
  if (context::current->type.chrome_type != context::process_type::renderer ||
      !context::current->type.chrome_module) {
    ELOGFMT(WARN, "BlinkParseHTMLManipulator: Not in renderer process or "
                  "Chrome module not found");
    return;
  }

  ELOGFMT(INFO, "BlinkParseHTMLManipulator: Checking for cached symbol "
                "HTMLParser__AppendBytes");

  auto &chrome = context::current->type.chrome_module;

  auto rdata = chrome->section(".rdata").value();
  auto text = chrome->section(".text").value();

  auto crc32 = text.crc32();

  static std::optional<blook::Function> HTMLParser__AppendBytes = {};
  if (auto res = context::current->process_ipc
                     .call_and_poll<std::pair<size_t, int32_t>>(
                         "get_symbol", std::string("HTMLParser__AppendBytes"));
      res && res->first != 0 && res->second == crc32) {
    HTMLParser__AppendBytes = text.add(res->first).as_function();
    ELOGFMT(INFO,
            "BlinkParseHTMLManipulator: Using cached symbol "
            "HTMLParser__AppendBytes at {}",
            HTMLParser__AppendBytes->data());
  } else {
    auto appendBytesText = rdata.find_one("HTMLDocumentParser::appendBytes");
    if (!appendBytesText) {
      ELOGFMT(WARN,
              "BlinkParseHTMLManipulator: HTMLDocumentParser::appendBytes not "
              "found in rdata section");
      return;
    }

    ELOGFMT(INFO,
            "BlinkParseHTMLManipulator: Found HTMLDocumentParser::appendBytes "
            "string at {} in rdata section",
            appendBytesText.value().data());

    auto xref = text.find_xref(appendBytesText.value());

    if (!xref) {
      ELOGFMT(WARN,
              "BlinkParseHTMLManipulator: HTMLDocumentParser::appendBytes xref "
              "not found in text section");
      return;
    }

    ELOGFMT(INFO,
            "BlinkParseHTMLManipulator: Found HTMLDocumentParser::appendBytes "
            "function at {} in text section",
            xref->data());

    HTMLParser__AppendBytes =
        xref->find_upwards({0x56, 0x57}).value().as_function();

    context::current->process_ipc
        .call<bool, std::pair<std::string, std::pair<size_t, int32_t>>>(
            "set_symbol",
            std::pair(
                std::string("HTMLParser__AppendBytes"),
                std::pair(
                    (size_t)(HTMLParser__AppendBytes->pointer() - text).data(),
                    crc32)));
  }

  static auto HTMLParser__AppendBytes_Hook =
      HTMLParser__AppendBytes->inline_hook();

  const auto pFunc = (uint8_t *)HTMLParser__AppendBytes->data();

  if (pFunc[0] == 0x56 && pFunc[1] == 0x57 && pFunc[2] == 0x53) {
    ELOGFMT(INFO, "BlinkParseHTMLManipulator: Using older function signature");
    HTMLParser__AppendBytes_Hook->install(+[](void *self, uint8_t *data,
                                              size_t size) {
      ELOGFMT(DEBUG, "BlinkParseHTMLManipulator: Hook called with {} bytes",
              size);
      auto span_data = std::span<char>(reinterpret_cast<char *>(data), size);
      std::vector<std::shared_ptr<std::any>> contexts;
      BlinkHTMLData html_data{.data = span_data, .replacement = {}};

      auto res = context::current->process_ipc.call_and_poll<std::string>(
          "on_blink_parse_html_manipulate",
          std::string(html_data.data.data(), html_data.data.size()));

      if (res.has_value() &&
          res.value() !=
              std::string_view(html_data.data.data(), html_data.data.size())) {
        ELOGFMT(
            DEBUG,
            "BlinkParseHTMLManipulator: HTML content modified, new size: {}",
            res.value().size());
        html_data.replacement =
            std::vector<char>(res.value().begin(), res.value().end());
      }

      auto ret = html_data.replacement.has_value()
                     ? HTMLParser__AppendBytes_Hook->call_trampoline<void *>(
                           self, html_data.replacement->data(),
                           html_data.replacement->size())
                     : HTMLParser__AppendBytes_Hook->call_trampoline<void *>(
                           self, data, size);
      return ret;
    });
  } else {
    ELOGFMT(INFO, "BlinkParseHTMLManipulator: Using newer function signature");
    HTMLParser__AppendBytes_Hook->install(+[](void *self, BlinkUtilSpan &data) {
      ELOGFMT(DEBUG, "BlinkParseHTMLManipulator: Hook called with {} bytes",
              data.size);
      auto span_data =
          std::span<char>(reinterpret_cast<char *>(data.data), data.size);
      std::vector<std::shared_ptr<std::any>> contexts;
      BlinkHTMLData html_data{.data = span_data, .replacement = {}};

      auto html = std::string(html_data.data.data(), html_data.data.size());
      if (auto res = context::current->process_ipc
                         .call<std::string, std::string>(
                             "on_blink_parse_html_manipulate", html)
                         .get();
          res != html) {
        ELOGFMT(
            DEBUG,
            "BlinkParseHTMLManipulator: HTML content modified, new size: {}",
            res.size());
        html_data.replacement = std::vector<char>(res.begin(), res.end());
      }

      auto blink_span =
          html_data.replacement.has_value()
              ? BlinkUtilSpan{reinterpret_cast<char *>(
                                  html_data.replacement->data()),
                              html_data.replacement->size()}
              : BlinkUtilSpan{reinterpret_cast<char *>(span_data.data()),
                              span_data.size()};

      auto ret = HTMLParser__AppendBytes_Hook->call_trampoline<void *>(
          self, blink_span);
      return ret;
    });
  }

  context::current->process_ipc.add_call_handler<bool>(
      "is_blink_parse_html_manipulator_available", []() { return true; });

  ELOGFMT(INFO,
          "BlinkParseHTMLManipulator: Installation completed successfully");
}

bool blink_parse_html_manipulator::is_available() {
  auto result = context::current->process_ipc.call<bool>(
      "is_blink_parse_html_manipulator_available");
  if (result.valid() && result.wait_for(std::chrono::milliseconds(20)) ==
                            std::future_status::ready) {
    return true;
  } else {
    ELOGFMT(
        DEBUG,
        "BlinkParseHTMLManipulator: Availability check failed or timed out");
    return false;
  }
}

void blink_parse_html_manipulator::register_js() {
  ELOGFMT(INFO, "BlinkParseHTMLManipulator: Registering JavaScript handlers");

  context::current->process_ipc.add_call_handler<
      std::string,
      std::string>("on_blink_parse_html_manipulate", [](const std::string
                                                            &_html) {
    auto html = _html;
    size_t manipulator_count = 0;

    for (auto &manipulator : blink_parse_html_manipulators) {
      try {
        if (auto res = manipulator(html); !res.empty()) {
          html = res;
        }
      } catch (const std::exception &e) {
        ELOGFMT(ERROR,
                "BlinkParseHTMLManipulator: Exception in manipulator {}: {}",
                manipulator_count, e.what());
      } catch (...) {
        ELOGFMT(
            ERROR,
            "BlinkParseHTMLManipulator: Unknown exception in manipulator {}",
            manipulator_count);
      }
      manipulator_count++;
    }

    if (manipulator_count > 0) {
      ELOGFMT(INFO, "BlinkParseHTMLManipulator: Processed {} manipulators",
              manipulator_count);
    }

    return html;
  });

  context::current->script->ctx.on_bind.push_back([]() {
    ELOGFMT(DEBUG,
            "BlinkParseHTMLManipulator: Clearing manipulators on script bind");
    blink_parse_html_manipulators.clear();
  });
}
} // namespace chromatic
