#include "Status.hpp"

std::string_view sock::Status::stringify_error(Error error) {
  switch (error) {
#define X(variant)           \
  case sock::Error::variant: \
    return #variant;

#include "Errors.inc"

#undef X

    default:
      return "<unknown>";
  }
}
std::string_view sock::Status::stringify_error(SystemError system_error) {
  switch (system_error) {
#define X(variant)                 \
  case sock::SystemError::variant: \
    return #variant;

#include "SystemErrors.inc"

#undef X

    default:
      return "<unknown>";
  }
}

std::string sock::Status::stringify() const {
  auto stringified = std::string(stringify_error(error));

  if (sub_error != Error::None) {
    stringified += " / ";
    stringified += stringify_error(sub_error);
  }
  if (system_error != SystemError::None) {
    stringified += " (";
    stringified += stringify_error(system_error);
    stringified += ')';
  }

  return stringified;
}