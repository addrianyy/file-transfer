#include <base/logger/Log.hpp>
#include <base/logger/Logger.hpp>

void base::detail::log::do_log(const char* file,
                               int line,
                               LogLevel level,
                               fmt::string_view fmt,
                               fmt::format_args args) {
  Logger::log(file, line, level, fmt, args);
}