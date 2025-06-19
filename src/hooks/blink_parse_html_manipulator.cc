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

#include "Windows.h"

#include "../script/bindings/binding_types.h"

extern std::list<std::function<bool(
    std::shared_ptr<
        chromatic::js::chrome::blink::blink_parse_manipulate_context>)>>
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
    return;
  }

  auto &chrome = context::current->type.chrome_module;

  auto rdata = chrome->section(".rdata").value();
  auto text = chrome->section(".text").value();

  static std::optional<blook::Function> HTMLParser__AppendBytes = {};
  if (auto res = context::current->process_ipc
                     .call<size_t>("get_symbol",
                                   std::string("HTMLParser__AppendBytes"))
                     .get();
      res) {
    HTMLParser__AppendBytes = text.add(res).as_function();
    ELOGFMT(INFO, "Using cached symbol for HTMLParser__AppendBytes: {}",
            HTMLParser__AppendBytes->data());
  } else {
    auto appendBytesText = rdata.find_one("HTMLDocumentParser::appendBytes");
    if (!appendBytesText) {
      ELOGFMT(WARN,
              "HTMLDocumentParser::appendBytes not found in rdata section.");
      return;
    }

    ELOGFMT(INFO,
            "string \"HTMLDocumentParser::appendBytes\" found at {} in rdata "
            "section.",
            appendBytesText.value().data());

    auto xref = text.find_xref(appendBytesText.value());

    if (!xref) {
      ELOGFMT(WARN,
              "HTMLDocumentParser::appendBytes not found in text section.");
      return;
    }

    ELOGFMT(
        INFO,
        "HTMLDocumentParser::appendBytes function found at {} in text section.",
        xref->data());

    HTMLParser__AppendBytes =
        xref->find_upwards({0x56, 0x57}).value().as_function();

    context::current->process_ipc
        .call<bool, std::pair<std::string, size_t>>(
            "set_symbol",
            std::pair(
                std::string("HTMLParser__AppendBytes"),
                (size_t)(HTMLParser__AppendBytes->pointer() - text).data()))
        .get();
  }

  static auto HTMLParser__AppendBytes_Hook =
      HTMLParser__AppendBytes->inline_hook();

  const auto pFunc = (uint8_t *)HTMLParser__AppendBytes->data();

  if (pFunc[0] == 0x56 && pFunc[1] == 0x57 && pFunc[2] == 0x53) {
    ELOGFMT(INFO,
            "BlinkHtmlParserHook::install() - Using older function signature");
    HTMLParser__AppendBytes_Hook->install(+[](void *self, uint8_t *data,
                                              size_t size) {
      ELOGFMT(INFO, "BlinkHtmlParserHook::install() - Hooked");
      auto span_data = std::span<char>(reinterpret_cast<char *>(data), size);
      std::vector<std::shared_ptr<std::any>> contexts;
      BlinkHTMLData html_data{.data = span_data, .replacement = {}};
      ELOGFMT(INFO, "BlinkHtmlParserHook::install() - BeforeParse");

      auto res = context::current->process_ipc
                     .call<js::chrome::blink::blink_parse_manipulate_context>(
                         "on_blink_parse_html_manipulate",
                         js::chrome::blink::blink_parse_manipulate_context{
                             .html = std::string(html_data.data.data(),
                                                 html_data.data.size()),
                             .url = ""})
                     .get();
      if (res.html !=
          std::string_view(html_data.data.data(), html_data.data.size())) {
        ELOGFMT(INFO, "BlinkHtmlParserHook::install() - Replacement found");
        html_data.replacement =
            std::vector<char>(res.html.begin(), res.html.end());
      }

      ELOGFMT(INFO, "BlinkHtmlParserHook::install() - has replacement: {}",
              html_data.replacement.has_value());
      auto ret = html_data.replacement.has_value()
                     ? HTMLParser__AppendBytes_Hook->call_trampoline<void *>(
                           self, html_data.replacement->data(),
                           html_data.replacement->size())
                     : HTMLParser__AppendBytes_Hook->call_trampoline<void *>(
                           self, data, size);
      return ret;
    });
  } else {
    ELOGFMT(INFO,
            "BlinkHtmlParserHook::install() - Using newer function signature");
    HTMLParser__AppendBytes_Hook->install(+[](void *self, BlinkUtilSpan &data) {
      ELOGFMT(INFO, "BlinkHtmlParserHook::install() - Hooked");
      auto span_data =
          std::span<char>(reinterpret_cast<char *>(data.data), data.size);
      std::vector<std::shared_ptr<std::any>> contexts;
      BlinkHTMLData html_data{.data = span_data, .replacement = {}};

      js::chrome::blink::blink_parse_manipulate_context res = {
          .html = std::string(html_data.data.data(), html_data.data.size()),
          .url = ""};
      if (res.html.contains("html>")) {
        // replace html> to html><h1>hi</h1>
        res.html =
            res.html.replace(res.html.find("html>"), 5, R"(html><div style="
    position: fixed;
    left: 13px;
    top: 11px;
    background: #00000022;
    color: white;
    z-index: 9999;
    backdrop-filter: blur(20px);
    padding: 10px 20px;
    font-size: 15px;
    border-radius: 100px;
    overflow: hidden;
    border: 1px solid #00000038;
    font-family: Consolas;
">Chromatic</div>)");
      }

      if (res.html !=
          std::string_view(html_data.data.data(), html_data.data.size())) {
        ELOGFMT(INFO, "BlinkHtmlParserHook::install() - Replacement found");
        html_data.replacement =
            std::vector<char>(res.html.begin(), res.html.end());
      }

      auto blink_span =
          html_data.replacement.has_value()
              ? BlinkUtilSpan{reinterpret_cast<char *>(
                                  html_data.replacement->data()),
                              html_data.replacement->size()}
              : BlinkUtilSpan{reinterpret_cast<char *>(span_data.data()),
                              span_data.size()};

      auto ret = HTMLParser__AppendBytes_Hook
                     ->call_trampoline<void*>(
                         self, blink_span);
      return ret;
    });
  }

  context::current->process_ipc.add_call_handler<bool>(
      "is_blink_parse_html_manipulator_available", []() { return true; });
}
bool blink_parse_html_manipulator::is_available() {
  auto result = context::current->process_ipc.call<bool>(
      "is_blink_parse_html_manipulator_available");
  if (result.valid() && result.wait_for(std::chrono::milliseconds(20)) ==
                            std::future_status::ready) {
    return true;
  } else {
    return false;
  }
}

void blink_parse_html_manipulator::register_js() {
  context::current->process_ipc
      .add_call_handler<js::chrome::blink::blink_parse_manipulate_context,
                        js::chrome::blink::blink_parse_manipulate_context>(
          "on_blink_parse_html_manipulate",
          [](js::chrome::blink::blink_parse_manipulate_context ctx) {
            ELOGFMT(INFO, "on_blink_parse_html_manipulate called with url: {}",
                    ctx.url);

            for (auto &manipulator : blink_parse_html_manipulators) {
              auto context = std::make_shared<
                  js::chrome::blink::blink_parse_manipulate_context>(ctx.html,
                                                                     ctx.url);
              if (manipulator(context)) {
                ctx.html = context->html;
              }
            }

            ELOGFMT(INFO, "on_blink_parse_html_manipulate finished");

            return ctx;
          });
}
} // namespace chromatic
