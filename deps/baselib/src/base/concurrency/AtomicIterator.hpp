#pragma once
#include <base/macro/ClassTraits.hpp>

#include <algorithm>
#include <atomic>

namespace base {

template <typename T>
class AtomicIterator {
  static_assert(sizeof(T) == sizeof(std::atomic<T>), "unexpected atomic iterator size");
  static_assert(std::is_unsigned_v<T>, "atomic iterator type must be unsigned");

  std::atomic<T> iterator{0};
  T count;

 public:
  CLASS_NON_COPYABLE_NON_MOVABLE(AtomicIterator)

  explicit AtomicIterator(T count) : count(count) {}

  bool next(T& value, T step = 1) {
    const T v = iterator.fetch_add(step, std::memory_order_relaxed);
    if (v >= count) {
      return false;
    }

    value = v;
    return true;
  }

  template <typename Fn>
  void consume(T step, Fn&& body) {
    while (true) {
      T value;
      if (!next(value, step)) {
        break;
      }

      const T limit = std::min(count, value + step);
      for (T i = value; i < limit; ++i) {
        if constexpr (std::is_void_v<std::invoke_result_t<Fn, T>>) {
          body(i);
        } else {
          if (!body(i)) {
            return;
          }
        }
      }
    }
  }

  template <typename Fn>
  void consume(Fn&& body) {
    while (true) {
      T value;
      if (!next(value)) {
        break;
      }

      if constexpr (std::is_void_v<std::invoke_result_t<Fn, T>>) {
        body(value);
      } else {
        if (!body(value)) {
          break;
        }
      }
    }
  }
};

}  // namespace base