#include "Logger.hpp"
#include "LoggerImpl.hpp"

#include <atomic>

static std::unique_ptr<base::LoggerImpl> g_logger;
static std::atomic_uint32_t g_min_reported_level{0};

base::LoggerImpl* base::Logger::get() {
  return g_logger.get();
}

std::unique_ptr<base::LoggerImpl> base::Logger::set(std::unique_ptr<LoggerImpl> logger) {
  auto previous_logger = std::move(g_logger);
  g_logger = std::move(logger);
  return previous_logger;
}

base::LogLevel base::Logger::min_reported_level() {
  return LogLevel(g_min_reported_level.load(std::memory_order_relaxed));
}

void base::Logger::set_min_reported_level(LogLevel level) {
  g_min_reported_level.store(uint32_t(level), std::memory_order_relaxed);
}

void base::Logger::log(const char* file,
                       int line,
                       LogLevel level,
                       fmt::string_view fmt,
                       fmt::format_args args) {
  if (g_logger && uint32_t(level) >= g_min_reported_level.load(std::memory_order_relaxed)) {
    g_logger->log(file, line, level, fmt, args);
  }
}

void base::Logger::log_panic(const char* file,
                             int line,
                             fmt::string_view fmt,
                             fmt::format_args args) {
  if (g_logger) {
    g_logger->log_panic(file, line, fmt, args);
  }
}

bool base::Logger::supports_color() {
  if (g_logger) {
    return g_logger->supports_color();
  }
  return false;
}