#include <base/Initialization.hpp>
#include <base/Platform.hpp>

#include <base/logger/Logger.hpp>
#include <base/logger/TerminalLogger.hpp>

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
  if (!Logger::get()) {
    Logger::set(std::make_unique<TerminalLogger>());
  }
}