#pragma once
#include <functional>
#include <list>
#include <memory>

namespace chromatic {
    struct blink_parse_html_manipulator {
      static void install();
      static bool is_available();
      static void register_js();
    };
}