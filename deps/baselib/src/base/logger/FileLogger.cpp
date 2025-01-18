#include "FileLogger.hpp"

#include <base/Panic.hpp>
#include <base/io/Print.hpp>

bool base::FileLogger::try_log(const char* file,
                               int line,
                               LogLevel level,
                               fmt::string_view fmt,
                               fmt::format_args args) {
  (void)file;
  (void)line;

  std::string_view header{};
  switch (level) {
    case LogLevel::Debug:
      header = "DEBUG:";
      break;
    case LogLevel::Info:
      header = "INFO: ";
      break;
    case LogLevel::Warn:
      header = "WARN: ";
      break;
    case LogLevel::Error:
      header = "ERROR:";
      break;
    default:
      header = "?";
      break;
  }

  std::unique_lock lock(mutex);

  const auto timestamp = PreciseTime::now() - epoch;
  const auto message = fmt::vformat(fmt, args);
  auto log_line = base::format("[{:>10.3f}] {}: {}", timestamp.seconds(), header, message);
  log_line.push_back('\n');

  const auto written_everything =
    output_file.write(log_line.data(), log_line.size()) == log_line.size();
  if (written_everything) {
    output_file.flush();
  }

  return written_everything;
}

base::FileLogger::FileLogger(const std::string& output_file_path)
    : FileLogger(File{output_file_path, "w", base::File::OpenFlags::NoBuffering}) {}

base::FileLogger::FileLogger(File output_file) : output_file(std::move(output_file)) {
  if (!this->output_file) {
    fatal_error("FileLogger failed to open the output file");
  }
}

void base::FileLogger::log(const char* file,
                           int line,
                           LogLevel level,
                           fmt::string_view fmt,
                           fmt::format_args args) {
  if (!try_log(file, line, level, fmt, args)) {
    fatal_error("FileLogger failed to write to the output file");
  }
}

void base::FileLogger::log_panic(const char* file,
                                 int line,
                                 fmt::string_view fmt,
                                 fmt::format_args args) {
  if (!try_log(file, line, LogLevel::Error, fmt, args)) {
    base::println("FileLogger is not healthy during panic");
    base::println("panic: {}", fmt::vformat(fmt, args));
  }
}

bool base::FileLogger::supports_color() const {
  return false;
}
