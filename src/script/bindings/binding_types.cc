#include "binding_types.h"
#include <list>

#include "breeze-js/quickjspp.hpp"
#include "ylt/easylog.hpp"

#include "../../context.h"

std::list<std::function<std::string(std::string)>>
    blink_parse_html_manipulators;
namespace chromatic::js {
void chrome::blink::add_blink_parse_html_manipulator(
    std::function<std::string(std::string)> manipulator) {

  blink_parse_html_manipulators.push_back([manipulator](std::string ctx) {
    try {
      return manipulator(ctx);
    } catch (const std::exception &e) {
      ELOGFMT(ERROR, "Exception in blink parse HTML manipulator: {}", e.what());
      return std::string();
    } catch (...) {
      ELOGFMT(ERROR, "Unknown exception in blink parse HTML manipulator");
      return std::string();
    }
  });
}
void infra::log(qjs::rest<std::string> msg) {
  std::string log_msg;
  for (const auto &m : msg) {
    log_msg += m + " ";
  }
  log_msg.pop_back();

  ELOGFMT(INFO, "{}", log_msg);
}
} // namespace chromatic::js