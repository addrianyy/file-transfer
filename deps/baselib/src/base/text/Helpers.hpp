#pragma once
#include <charconv>
#include <cstdint>
#include <string>
#include <string_view>

namespace base::text {

std::string to_lowercase(std::string_view s);
std::string to_uppercase(std::string_view s);
bool equals_case_insensitive(std::string_view a, std::string_view b);

std::string_view lstrip(std::string_view s);
std::string_view rstrip(std::string_view s);
std::string_view strip(std::string_view s);

template <typename T>
bool to_number(std::string_view s, T& value, int base = 10) {
  value = {};

  const auto end = s.data() + s.size();

  const auto result = std::from_chars(s.data(), end, value, base);
  return result.ec == std::errc{} && result.ptr == end;
}

template <typename T>
bool to_number(const char* s, T& value, int base = 10) {
  return to_number(std::string_view{s}, value, base);
}

}  // namespace base::text