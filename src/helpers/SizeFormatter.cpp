#include "SizeFormatter.hpp"

std::pair<double, SizeFormatter::Unit> SizeFormatter::bytes_to_readable_units(uint64_t bytes) {
  constexpr double threshold = 1024;

  auto current = double(bytes);

  if (current < threshold) {
    return {current, Unit::Bytes};
  }
  current /= 1024;

  if (current < threshold) {
    return {current, Unit::KBytes};
  }
  current /= 1024;

  if (current < threshold) {
    return {current, Unit::MBytes};
  }
  current /= 1024;

  return {current, Unit::GBytes};
}

std::string_view SizeFormatter::unit_to_string(Unit unit) {
  switch (unit) {
    case Unit::Bytes:
      return "B";
    case Unit::KBytes:
      return "KB";
    case Unit::MBytes:
      return "MB";
    case Unit::GBytes:
      return "GB";
    default:
      return "?";
  }
}