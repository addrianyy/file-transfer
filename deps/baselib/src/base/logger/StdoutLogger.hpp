#pragma once
#include <base/logger/LoggerImpl.hpp>
#include <base/time/PreciseTime.hpp>

namespace base {

class StdoutLogger : public LoggerImpl {
  PreciseTime epoch = PreciseTime::now();
  bool allow_colors = false;

 public:
  explicit StdoutLogger(bool allow_colors);

  void log(const char* file,
           int line,
           LogLevel level,
           fmt::string_view fmt,
           fmt::format_args args) override;
  void log_panic(const char* file, int line, fmt::string_view fmt, fmt::format_args args) override;

  bool supports_color() const override;
};

}  // namespace base