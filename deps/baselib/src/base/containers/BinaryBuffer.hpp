#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include <base/macro/ClassTraits.hpp>

namespace base {

class BinaryBuffer {
  std::unique_ptr<uint8_t[]> buffer;
  size_t size_ = 0;
  size_t capacity_ = 0;

 public:
  CLASS_NON_COPYABLE(BinaryBuffer)

  BinaryBuffer() = default;

  explicit BinaryBuffer(std::span<const uint8_t> data);
  BinaryBuffer(const void* data, size_t size);

  BinaryBuffer(BinaryBuffer&& other) noexcept;
  BinaryBuffer& operator=(BinaryBuffer&& other) noexcept;

  size_t size() const { return size_; }
  bool empty() const { return size_ == 0; }

  size_t capacity() const { return capacity_; }
  size_t unused_capacity() const { return capacity_ - size_; }

  const uint8_t* data() const { return buffer.get(); }
  uint8_t* data() { return buffer.get(); }

  std::span<const uint8_t> span() const { return {data(), size_}; }
  std::span<uint8_t> span() { return {data(), size_}; }

  operator std::span<const uint8_t>() const { return span(); }
  operator std::span<uint8_t>() { return span(); }

  std::vector<uint8_t> to_vector() const;

  void clear();
  void clear_and_deallocate();

  void resize(size_t new_size);
  void resize_and_zero(size_t new_size);

  std::span<uint8_t> grow(size_t amount);
  std::span<uint8_t> grow_and_zero(size_t amount);

  void shrink(size_t amount);

  void append(std::span<const uint8_t> buffer);
  void append(const void* buffer, size_t buffer_size);

  void write_at_offset(size_t offset, std::span<const uint8_t> buffer);
  void write_at_offset(size_t offset, const void* buffer, size_t buffer_size);

  void trim_front(size_t amount);
  void trim_back(size_t amount);
};

}  // namespace base
