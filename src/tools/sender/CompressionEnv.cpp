#include "CompressionEnv.hpp"

#include <cstdlib>
#include <cstring>

bool CompressionEnv::is_compression_enabled() {
  static bool enabled = [] {
    if (const char* value = std::getenv("FT_DISABLE_COMPRESSION")) {
      if (std::strcmp(value, "1") == 0 || std::strcmp(value, "ON") == 0) {
        return false;
      }
    }
    return true;
  }();
  return enabled;
}
