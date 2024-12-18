#pragma once
#include <cstddef>
#include <cstdint>
#include <span>

#include <base/macro/ClassTraits.hpp>

class Hasher {
  void* state{};

 public:
  CLASS_NON_COPYABLE_NON_MOVABLE(Hasher);

  Hasher();
  ~Hasher();

  void reset();

  void feed(const void* bytes, size_t size) {
    return feed(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(bytes), size));
  }
  void feed(std::span<const uint8_t> bytes);
  uint64_t finalize();
};