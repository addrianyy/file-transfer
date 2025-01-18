#include "StdoutLogger.hpp"

#include <base/io/Print.hpp>
#include <base/io/TerminalColors.hpp>

#define TIMESTAMP_FORMAT "[{:>10.3f}]"

base::StdoutLogger::StdoutLogger(bool allow_colors) : allow_colors(allow_colors) {}

void base::StdoutLogger::log(const char* file,
                             int line,
                             LogLevel level,
                             fmt::string_view fmt,
                             fmt::format_args args) {
  (void)file;
  (void)line;

  const auto message = fmt::vformat(fmt, args);

  std::string_view header{};
  std::string_view color{};

  switch (level) {
    case LogLevel::Debug:
      header = "DEBUG:";
      color = TERMINAL_COLOR_SEQUENCE_GREEN;
      break;
    case LogLevel::Info:
      header = "INFO: ";
      color = TERMINAL_COLOR_SEQUENCE_BLUE;
      break;
    case LogLevel::Warn:
      header = "WARN: ";
      color = TERMINAL_COLOR_SEQUENCE_YELLOW;
      break;
    case LogLevel::Error:
      header = "ERROR:";
      color = TERMINAL_COLOR_SEQUENCE_RED;
      break;
    default:
      header = "?";
      color = TERMINAL_COLOR_SEQUENCE_MAGENTA;
      break;
  }

  const auto timestamp = PreciseTime::now() - epoch;

  if (allow_colors) {
    if (level == LogLevel::Error) {
      base::println(TERMINAL_COLOR_SEQUENCE_MAGENTA TIMESTAMP_FORMAT
                    " {}{} {}" TERMINAL_RESET_SEQUENCE,
                    timestamp.seconds(), color, header, message);
    } else {
      base::println(TERMINAL_COLOR_SEQUENCE_MAGENTA TIMESTAMP_FORMAT " {}{}" TERMINAL_RESET_SEQUENCE
                                                                     " {}",
                    timestamp.seconds(), color, header, message);
    }
  } else {
    base::println(TIMESTAMP_FORMAT " {} {}", timestamp.seconds(), header, message);
  }
}

void base::StdoutLogger::log_panic(const char* file,
                                   int line,
                                   fmt::string_view fmt,
                                   fmt::format_args args) {
  log(file, line, LogLevel::Error, fmt, args);
}

bool base::StdoutLogger::supports_color() const {
  return allow_colors;
}
