#pragma once
#include <base/logger/LogLevel.hpp>
#include <base/text/Format.hpp>

namespace base {

class LoggerImpl {
 public:
  virtual ~LoggerImpl() = default;

  virtual void log(const char* file,
                   int line,
                   LogLevel level,
                   fmt::string_view fmt,
                   fmt::format_args args) = 0;
  virtual void log_panic(const char* file,
                         int line,
                         fmt::string_view fmt,
                         fmt::format_args args) = 0;

  virtual bool supports_color() const = 0;
};

}  // namespace base