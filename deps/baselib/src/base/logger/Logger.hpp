#pragma once
#include "LogLevel.hpp"

#include <base/text/Format.hpp>

#include <memory>

namespace base {

class LoggerImpl;

class Logger {
 public:
  static LoggerImpl* get();
  static std::unique_ptr<LoggerImpl> set(std::unique_ptr<LoggerImpl> logger);

  static LogLevel min_reported_level();
  static void set_min_reported_level(LogLevel level);

  static void log(const char* file,
                  int line,
                  LogLevel level,
                  fmt::string_view fmt,
                  fmt::format_args args);
  static void log_panic(const char* file, int line, fmt::string_view fmt, fmt::format_args args);

  static bool supports_color();
};

}  // namespace base