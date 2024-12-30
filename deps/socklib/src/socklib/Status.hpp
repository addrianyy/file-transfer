#pragma once
#include <string>
#include <string_view>

namespace sock {

enum class ErrorCode {
#define X(variant) variant,

#include "ErrorCodes.inc"

#undef X
};

struct [[nodiscard]] Status {
  ErrorCode error;
  ErrorCode sub_error;

  constexpr bool would_block() const { return has_code(ErrorCode::WouldBlock); }

  constexpr bool has_code(ErrorCode code) const { return error == code || sub_error == code; }
  constexpr bool success() const { return error == ErrorCode::Success; }
  constexpr operator bool() const { return success(); }

  static std::string_view stringify_error_code(ErrorCode error_code);

  std::string stringify() const;
};

template <typename Value>
struct [[nodiscard]] Result {
  Status status{};
  Value value{};

  constexpr operator bool() const { return status.success(); }
};

}  // namespace sock