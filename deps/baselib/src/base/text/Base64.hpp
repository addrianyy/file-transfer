#pragma once
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace base {

class Base64 {
 public:
  static std::string encode(std::span<const uint8_t> input);
  static std::optional<std::vector<uint8_t>> decode(std::string_view input);

  static void encode(std::span<const uint8_t> input, std::string& output);
  static bool decode(std::string_view input, std::vector<uint8_t>& output);
};

}  // namespace base