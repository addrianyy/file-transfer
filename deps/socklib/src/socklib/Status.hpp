#pragma once
#include <string>
#include <string_view>

namespace sock {

enum class Error {
#define X(variant) variant,

#include "Errors.inc"

#undef X
};

enum class SystemError {
#define X(variant) variant,

#include "SystemErrors.inc"

#undef X
};

struct [[nodiscard]] Status {
  Error error{};
  Error sub_error{};
  SystemError system_error{};

  constexpr bool has_error(Error code) const { return error == code || sub_error == code; }
  constexpr bool has_error(SystemError code) const { return system_error == code; }

  constexpr bool success() const { return error == Error::None; }
  constexpr operator bool() const { return success(); }

  constexpr bool would_block() const { return has_error(SystemError::WouldBlock); }
  constexpr bool disconnected() const { return has_error(SystemError::Disconnected); }

  static std::string_view stringify_error(Error error);
  static std::string_view stringify_error(SystemError system_error);

  std::string stringify() const;
};

template <typename Value>
struct [[nodiscard]] Result {
  Status status{};
  Value value{};

  constexpr operator bool() const { return status.success(); }
};

}  // namespace sock