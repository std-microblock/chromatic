#include "binding_types.h"
#include <list>

#include "breeze-js/quickjspp.hpp"
#include "ylt/easylog.hpp"

std::list<std::function<bool(
    std::shared_ptr<
        chromatic::js::chrome::blink::blink_parse_manipulate_context>)>>
    blink_parse_html_manipulators;
namespace chromatic::js {
void chrome::blink::add_blink_parse_html_manipulator(
    std::function<bool(std::shared_ptr<blink_parse_manipulate_context>)>
        manipulator) {
  blink_parse_html_manipulators.push_back(
      [manipulator](std::shared_ptr<blink_parse_manipulate_context> ctx) {
        try {
          return manipulator(ctx);
        } catch (const std::exception &e) {
          ELOGFMT(ERROR, "Exception in blink parse HTML manipulator: {}",
                  e.what());
          return false;
        } catch (...) {
          ELOGFMT(ERROR, "Unknown exception in blink parse HTML manipulator");
          return false;
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