#pragma once
#include <base/logger/LogLevel.hpp>
#include <base/text/Format.hpp>

namespace base::detail::log {

void do_log(const char* file,
            int line,
            LogLevel level,
            fmt::string_view fmt,
            fmt::format_args args);

template <typename... Args>
inline void log_fmt(const char* file,
                    int line,
                    LogLevel level,
                    base::format_string<Args...> fmt,
                    Args&&... args) {
  do_log(file, line, level, fmt, fmt::make_format_args(args...));
}

}  // namespace base::detail::log

#define log_message(level, format, ...) \
  ::base::detail::log::log_fmt(__FILE__, __LINE__, (level), (format), ##__VA_ARGS__)

#define log_debug(format, ...) log_message(::base::LogLevel::Debug, (format), ##__VA_ARGS__)
#define log_info(format, ...) log_message(::base::LogLevel::Info, (format), ##__VA_ARGS__)
#define log_warn(format, ...) log_message(::base::LogLevel::Warn, (format), ##__VA_ARGS__)
#define log_error(format, ...) log_message(::base::LogLevel::Error, (format), ##__VA_ARGS__)