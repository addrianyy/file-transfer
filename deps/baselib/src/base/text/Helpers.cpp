#include "Helpers.hpp"

#include <cctype>

namespace base::text {

std::string to_lowercase(std::string_view s) {
  std::string result;
  result.reserve(s.size());

  for (const auto ch : s) {
    const auto conv = std::tolower(ch);
    if (conv > 0 && conv < 256) {
      result.push_back(char(conv));
    } else {
      result.push_back('?');
    }
  }

  return result;
}

std::string to_uppercase(std::string_view s) {
  std::string result;
  result.reserve(s.size());

  for (const auto ch : s) {
    const auto conv = std::toupper(ch);
    if (conv > 0 && conv < 256) {
      result.push_back(char(conv));
    } else {
      result.push_back('?');
    }
  }

  return result;
}

bool equals_case_insensitive(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) {
    return false;
  }

  for (size_t i = 0; i < a.size(); ++i) {
    if (std::tolower(a[i]) != std::tolower(b[i])) {
      return false;
    }
  }

  return true;
}

std::string_view lstrip(std::string_view s) {
  while (!s.empty() && std::isspace(s.front())) {
    s = s.substr(1);
  }
  return s;
}

std::string_view rstrip(std::string_view s) {
  while (!s.empty() && std::isspace(s.back())) {
    s = s.substr(0, s.size() - 1);
  }
  return s;
}

std::string_view strip(std::string_view s) {
  return lstrip(rstrip(s));
}

}  // namespace base::text