#include "Status.hpp"

std::string_view sock::Status::stringify_error_code(ErrorCode error_code) {
  switch (error_code) {
#define X(variant)               \
  case sock::ErrorCode::variant: \
    return #variant;

#include "ErrorCodes.inc"

#undef X

    default:
      return "<unknown>";
  }
}

std::string sock::Status::stringify() const {
  const auto main_error_string = stringify_error_code(error);
  const auto sub_error_string = stringify_error_code(sub_error);

  std::string stringified;
  if (sub_error == ErrorCode::Success) {
    stringified.assign(main_error_string);
  } else {
    stringified.reserve(main_error_string.size() + sub_error_string.size() + 3);
    stringified += main_error_string;
    stringified += " (";
    stringified += sub_error_string;
    stringified += ')';
  }

  return stringified;
}