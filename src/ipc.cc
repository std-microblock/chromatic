#include "ipc.h"
#include <stdexcept>

namespace chromatic {
void breeze_ipc::connect(std::string_view name) {
  if (!channel.connect(name.data())) {
    throw std::runtime_error("Failed to connect to IPC channel: " +
                             std::string(name));
  }
}
} // namespace chromatic