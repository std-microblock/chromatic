#include "config.h"

#include <cstring>
#include <fmt/core.h>
#include <lzma.h>
#include <rfl.hpp>
#include <rfl/json.hpp>
#include <stdexcept>
#include <vector>

#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#else
#define EXPORT __attribute__((visibility("default")))
#endif

namespace chromatic::injectee {

/// Exported symbol that fripack CLI patches after compilation.
extern "C" EXPORT EmbeddedConfig g_embedded_config{};

EmbeddedConfigData parseEmbeddedConfig() {
  // Validate magic numbers
  if (g_embedded_config.magic1 != 0x0d000721 ||
      g_embedded_config.magic2 != 0x1f8a4e2b ||
      g_embedded_config.version != 1) {
    throw std::runtime_error("Invalid embedded config: bad magic or version");
  }

  if (g_embedded_config.data_size <= 0 || g_embedded_config.data_offset <= 0) {
    throw std::runtime_error(
        "Invalid embedded config: no data (was the binary patched by fripack?)");
  }

  // Read raw data from the struct's offset
  char *p_data = reinterpret_cast<char *>(&g_embedded_config) +
                 g_embedded_config.data_offset;
  std::vector<char> data(g_embedded_config.data_size);
  std::memcpy(data.data(), p_data, g_embedded_config.data_size);

  // Decompress if xz-compressed
  if (g_embedded_config.data_xz) {
    lzma_stream strm = LZMA_STREAM_INIT;

    lzma_ret ret = lzma_stream_decoder(&strm, UINT64_MAX, LZMA_CONCATENATED);
    if (ret != LZMA_OK) {
      lzma_end(&strm);
      throw std::runtime_error("Failed to initialize LZMA decoder");
    }

    strm.next_in = reinterpret_cast<const uint8_t *>(data.data());
    strm.avail_in = data.size();

    std::vector<char> decompressed;
    constexpr size_t chunk_size = 64 * 1024;
    constexpr size_t max_size = 300 * 1024 * 1024; // 300MB safety limit

    while (true) {
      std::vector<char> chunk(chunk_size);
      strm.next_out = reinterpret_cast<uint8_t *>(chunk.data());
      strm.avail_out = chunk.size();

      ret = lzma_code(&strm, LZMA_FINISH);

      size_t decoded = chunk.size() - strm.avail_out;
      if (decompressed.size() + decoded > max_size) {
        lzma_end(&strm);
        throw std::runtime_error("Decompressed data too large (> 300MB)");
      }

      decompressed.insert(decompressed.end(), chunk.begin(),
                          chunk.begin() + decoded);

      if (ret == LZMA_STREAM_END || strm.avail_in == 0) {
        break;
      } else if (ret != LZMA_OK) {
        lzma_end(&strm);
        throw std::runtime_error("LZMA decompression failed");
      }
    }

    lzma_end(&strm);
    data = std::move(decompressed);
  }

  // Parse JSON
  auto json_str = std::string(data.data(), data.size());

  auto result = rfl::json::read<EmbeddedConfigData>(json_str);
  if (!result) {
    throw std::runtime_error(
        fmt::format("Failed to parse embedded config JSON: {}",
                    result.error().what()));
  }

  return std::move(result.value());
}

} // namespace chromatic::injectee
