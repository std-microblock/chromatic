#pragma once
#include <cstdint>
#include <optional>
#include <string>

namespace chromatic::injectee {

/// Fripack-compatible embedded config data.
/// This struct is deserialized from JSON embedded in the binary.
struct EmbeddedConfigData {
  enum class Mode : int32_t {
    EmbedJs = 1,   // JS content is directly embedded in the binary
    WatchPath = 2, // Read JS from a file path and watch for changes
  } mode;

  std::optional<std::string> js_filepath; // Unused (fripack legacy field)
  std::optional<std::string> js_content;  // EmbedJs mode: embedded JS source
  std::optional<std::string> watch_path;  // WatchPath mode: file path to watch
};

/// Binary header embedded in the shared library.
/// fripack patches this structure after compilation to embed data.
#pragma pack(push, 1)
struct EmbeddedConfig {
  int32_t magic1 = 0x0d000721;
  int32_t magic2 = 0x1f8a4e2b;
  int32_t version = 1;

  int32_t data_size = 0;
  int32_t data_offset = 0; // Offset from the start of the struct.
  bool data_xz = false;    // Whether the data is compressed with xz.
};
#pragma pack(pop)

/// Parse the embedded config from g_embedded_config.
/// Handles magic validation, xz decompression, and JSON deserialization.
EmbeddedConfigData parseEmbeddedConfig();

} // namespace chromatic::injectee
