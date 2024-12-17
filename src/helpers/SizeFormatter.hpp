#pragma once
#include <string_view>
#include <utility>

#include <base/text/Format.hpp>

class SizeFormatter {
 public:
  enum class Unit {
    Bytes,
    KBytes,
    MBytes,
    GBytes,
  };

  static std::pair<double, Unit> bytes_to_readable_units(uint64_t bytes);
  static std::string_view unit_to_string(Unit unit);
};

template <>
class fmt::formatter<SizeFormatter::Unit> {
 public:
  constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }
  template <typename Context>
  constexpr auto format(SizeFormatter::Unit const& unit, Context& ctx) const {
    return format_to(ctx.out(), "{}", SizeFormatter::unit_to_string(unit));
  }
};