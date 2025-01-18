#include "Redirection.hpp"

#include <base/Platform.hpp>

#ifdef PLATFORM_WINDOWS

#include <io.h>
#include <cstdio>

bool base::is_stdout_redirected() {
  return !_isatty(_fileno(stdout));
}
bool base::is_stderr_redirected() {
  return !_isatty(_fileno(stderr));
}

#else

#include <unistd.h>

bool base::is_stdout_redirected() {
  return !isatty(1);
}
bool base::is_stderr_redirected() {
  return !isatty(2);
}

#endif