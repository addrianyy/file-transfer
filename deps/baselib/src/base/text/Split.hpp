#pragma once
#include <cstddef>
#include <string>
#include <array>
#include <string_view>

namespace base::text {

enum class TrailingDelimeter {
  Ignore,
  Handle,
};

template <typename Fn>
static bool split(std::string_view text,
                  std::string_view delimiter,
                  TrailingDelimeter trailing_delimeter,
                  Fn&& callback) {
  while (!text.empty()) {
    const auto delimeter_index = text.find_first_of(delimiter);
    if (delimeter_index == std::string_view::npos) {
      return callback(text);
    }

    const auto part = text.substr(0, delimeter_index);
    if (!callback(part)) {
      return false;
    }

    text = text.substr(delimeter_index + delimiter.size());
    if (text.empty() && trailing_delimeter == TrailingDelimeter::Handle) {
      return callback(text);
    }
  }

  return true;
}

template <typename Fn>
static bool splitn(std::string_view text,
                   std::string_view delimiter,
                   size_t n,
                   TrailingDelimeter trailing_delimeter,
                   Fn&& callback) {
  size_t part_index = 0;

  while (!text.empty()) {
    const auto delimeter_index = text.find_first_of(delimiter);
    if (delimeter_index == std::string_view::npos) {
      return callback(text);
    }

    if (++part_index >= n) {
      return callback(text);
    }

    const auto part = text.substr(0, delimeter_index);
    if (!callback(part)) {
      return false;
    }

    text = text.substr(delimeter_index + delimiter.size());
    if (text.empty() && trailing_delimeter == TrailingDelimeter::Handle) {
      return callback(text);
    }
  }

  return true;
}

template <size_t N>
static bool split_to(std::string_view text,
                     std::string_view delimiter,
                     TrailingDelimeter trailing_delimeter,
                     std::array<std::string_view, N>& splitted) {
  size_t index = 0;

  const auto result = split(text, delimiter, trailing_delimeter, [&](std::string_view part) {
    if (index >= N) {
      return false;
    }

    splitted[index++] = part;

    return true;
  });

  return result && index == N;
}

template <size_t N>
static bool splitn_to(std::string_view text,
                      std::string_view delimiter,
                      TrailingDelimeter trailing_delimeter,
                      std::array<std::string_view, N>& splitted) {
  size_t index = 0;

  const auto result = splitn(text, delimiter, N, trailing_delimeter, [&](std::string_view part) {
    if (index >= N) {
      return false;
    }

    splitted[index++] = part;

    return true;
  });

  return result && index == N;
}

}  // namespace base::text