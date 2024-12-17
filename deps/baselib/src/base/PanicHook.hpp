#pragma once
#include <cstdint>
#include <functional>
#include <limits>

namespace base {

using PanicHook = std::function<void()>;

class PanicHookRegistration {
  constexpr static uint64_t invalid_index = std::numeric_limits<uint64_t>::max();

  uint64_t index{invalid_index};

  explicit PanicHookRegistration(uint64_t index) : index(index) {}

 public:
  static PanicHookRegistration register_hook(PanicHook hook);

  PanicHookRegistration() = default;

  void unregister_hook();
};

}  // namespace base