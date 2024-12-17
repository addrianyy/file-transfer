#pragma once
#include <base/io/File.hpp>
#include <base/logger/LoggerImpl.hpp>
#include <base/time/PreciseTime.hpp>

#include <mutex>
#include <string>

namespace base {

class FileLogger : public LoggerImpl {
  PreciseTime epoch = PreciseTime::now();

  mutable std::mutex mutex;
  File output_file;

  bool try_log(const char* file,
               int line,
               LogLevel level,
               fmt::string_view fmt,
               fmt::format_args args);

 public:
  explicit FileLogger(const std::string& output_file_path);
  explicit FileLogger(File output_file);

  void log(const char* file,
           int line,
           LogLevel level,
           fmt::string_view fmt,
           fmt::format_args args) override;
  void log_panic(const char* file, int line, fmt::string_view fmt, fmt::format_args args) override;

  bool supports_color() const override;
};

}  // namespace base