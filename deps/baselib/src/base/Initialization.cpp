#include <base/Initialization.hpp>
#include <base/Platform.hpp>

#include <base/io/Redirection.hpp>
#include <base/logger/Logger.hpp>
#include <base/logger/StdoutLogger.hpp>

#include <cstdio>

#ifdef PLATFORM_WINDOWS

#include <Windows.h>

static void initialize_colors() {
  const auto stdout_handle = GetStdHandle(STD_OUTPUT_HANDLE);
  const auto stderr_handle = GetStdHandle(STD_ERROR_HANDLE);

  DWORD console_mode;

  {
    GetConsoleMode(stdout_handle, &console_mode);
    console_mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(stdout_handle, console_mode);
  }

  {
    GetConsoleMode(stderr_handle, &console_mode);
    console_mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(stderr_handle, console_mode);
  }
}

#else

static void initialize_colors() {}

#endif

void base::initialize() {
  initialize_colors();

  // Set stdout to line-buffering on Linux and no-buffering on Windows (Windows doesn't support
  // line-buffering).
#ifdef PLATFORM_WINDOWS
  std::setvbuf(stdout, nullptr, _IONBF, 0);
#else
  std::setvbuf(stdout, nullptr, _IOLBF, 0);
#endif

  if (!Logger::get()) {
    const bool allow_colors = !base::is_stdout_redirected();

    Logger::set(std::make_unique<StdoutLogger>(allow_colors));
  }
}