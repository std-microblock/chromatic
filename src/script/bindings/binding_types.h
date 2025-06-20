#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "../../hooks/blink_parse_html_manipulator.h"

namespace qjs {
template <typename T> struct rest;
}

namespace chromatic::js {
struct chrome {
  struct blink {
    struct blink_parse_manipulate_context {
      std::string html;
      std::string url;
    };
    static void add_blink_parse_html_manipulator(
        std::function<std::string(std::string)>);

    static bool is_parse_html_manipulator_available() {
      return chromatic::blink_parse_html_manipulator::is_available();
    }
  };
};

struct infra {
  static void log(qjs::rest<std::string> msg);
};
} // namespace chromatic::js