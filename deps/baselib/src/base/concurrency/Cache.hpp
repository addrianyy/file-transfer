#pragma once
#include <cstddef>
#include <utility>

#include <base/Platform.hpp>

namespace base {

#if defined(PLATFORM_APPLE) && defined(PLATFORM_AARCH64)
constexpr static size_t max_cache_line_size = 128;
#else
constexpr static size_t max_cache_line_size = 64;
#endif

template <typename T>
class alignas(max_cache_line_size) CacheLineAligned {
  T data;

 public:
  CacheLineAligned() = default;
  CacheLineAligned(T&& data) : data(std::move(data)) {}

  T& get() { return data; }
  T& operator*() { return get(); }
  T* operator->() { return &get(); }

  const T& get() const { return data; }
  const T& operator*() const { return get(); }
  const T* operator->() const { return &get(); }
};

}  // namespace base