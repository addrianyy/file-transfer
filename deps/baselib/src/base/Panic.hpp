#pragma once
#include <base/text/Format.hpp>

namespace base::detail::panicking {

[[noreturn]] void do_fatal_error(const char* file,
                                 int line,
                                 fmt::string_view fmt,
                                 fmt::format_args args);
[[noreturn]] void do_verify_fail(const char* file,
                                 int line,
                                 fmt::string_view fmt,
                                 fmt::format_args args);

template <typename... Args>
[[noreturn]] inline void fatal_error_fmt(const char* file,
                                         int line,
                                         base::format_string<Args...> fmt,
                                         Args&&... args) {
  do_fatal_error(file, line, fmt, fmt::make_format_args(args...));
}

template <typename... Args>
inline void verify_fmt(const char* file,
                       int line,
                       bool value,
                       base::format_string<Args...> fmt,
                       Args&&... args) {
  if (!value) {
    do_verify_fail(file, line, fmt, fmt::make_format_args(args...));
  }
}

}  // namespace base::detail::panicking

namespace base {
bool is_panicking();
}

#define fatal_error(format, ...) \
  ::base::detail::panicking::fatal_error_fmt(__FILE__, __LINE__, (format), ##__VA_ARGS__)

#define verify(value, format, ...) \
  ::base::detail::panicking::verify_fmt(__FILE__, __LINE__, !!(value), (format), ##__VA_ARGS__)

#define unreachable() fatal_error("entered unreachable code")
