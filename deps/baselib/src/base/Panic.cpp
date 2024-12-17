#include <base/Panic.hpp>
#include <base/PanicHook.hpp>
#include <base/io/Print.hpp>
#include <base/logger/Logger.hpp>
#include <base/logger/LoggerImpl.hpp>

#include <atomic>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <vector>

namespace base {

struct PanicHookEntry {
  uint64_t id;
  PanicHook hook;
};
struct PanicHooks {
  std::mutex mutex;

  uint64_t next_hook_id{0};
  std::vector<PanicHookEntry> entries;
};
static PanicHooks g_panic_hooks;

static std::atomic_bool g_is_panicking = false;

}  // namespace base

base::PanicHookRegistration base::PanicHookRegistration::register_hook(PanicHook hook) {
  if (g_is_panicking) {
    return {};
  }

  std::unique_lock lock(g_panic_hooks.mutex);

  const auto id = g_panic_hooks.next_hook_id++;

  g_panic_hooks.entries.push_back(PanicHookEntry{
    .id = id,
    .hook = std::move(hook),
  });

  return PanicHookRegistration{id};
}

void base::PanicHookRegistration::unregister_hook() {
  if (index == invalid_index || g_is_panicking) {
    return;
  }

  {
    std::unique_lock lock(g_panic_hooks.mutex);
    std::erase_if(g_panic_hooks.entries,
                  [this](const PanicHookEntry& entry) { return entry.id == index; });
  }

  index = invalid_index;
}

bool base::is_panicking() {
  return g_is_panicking.load(std::memory_order_relaxed);
}

[[noreturn]] void base::detail::panicking::do_fatal_error(const char* file,
                                                          int line,
                                                          fmt::string_view fmt,
                                                          fmt::format_args args) {
  if (g_is_panicking.exchange(true)) {
    while (true) {
      std::this_thread::sleep_for(std::chrono::seconds(10));
    }
  }

  // Reset logger so nobody can use it when executing panic hooks.
  auto logger = Logger::set(nullptr);
  if (logger) {
    logger->log_panic(file, line, fmt, args);
  } else {
    base::println("panic: {}", fmt::vformat(fmt, args));
  }

  {
    std::unique_lock lock(g_panic_hooks.mutex);
    for (const auto& entry : g_panic_hooks.entries) {
      entry.hook();
    }
  }

  // For debugger breakpoint:
  {
    int _unused = 0;
    (void)_unused;
  }

  // Use _Exit so we don't call any destructors.
  std::_Exit(EXIT_FAILURE);
}

[[noreturn]] void base::detail::panicking::do_verify_fail(const char* file,
                                                          int line,
                                                          fmt::string_view fmt,
                                                          fmt::format_args args) {
  const auto message = fmt::vformat(fmt, args);

  if (message.empty()) {
    fatal_error_fmt(file, line, "assertion failed");
  } else {
    fatal_error_fmt(file, line, "assertion failed: {}", message);
  }
}