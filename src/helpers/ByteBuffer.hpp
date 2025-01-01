#pragma once
#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>

#include <base/Panic.hpp>
#include <base/macro/ClassTraits.hpp>

class ByteBuffer {
  std::unique_ptr<uint8_t[]> backing;
  size_t size_ = 0;
  size_t capacity_ = 0;

 public:
  CLASS_NON_COPYABLE(ByteBuffer)

  ByteBuffer() = default;

  ByteBuffer(ByteBuffer&& other) noexcept {
    backing = std::move(other.backing);
    size_ = other.size_;
    capacity_ = other.capacity_;

    other.free();
  }

  ByteBuffer& operator=(ByteBuffer&& other) noexcept {
    if (this != &other) {
      backing = std::move(other.backing);
      size_ = other.size_;
      capacity_ = other.capacity_;

      other.free();
    }

    return *this;
  }

  size_t size() const { return size_; }
  bool empty() const { return size_ == 0; }

  size_t capacity() const { return capacity_; }
  size_t unused_capacity() const { return capacity_ - size_; }

  const uint8_t* data() const { return backing.get(); }
  uint8_t* data() { return backing.get(); }

  std::span<const uint8_t> span() const { return std::span(data(), size_); }
  std::span<uint8_t> span() { return std::span(data(), size_); }

  void clear() { resize(0); }
  void free() {
    backing = nullptr;
    size_ = 0;
    capacity_ = 0;
  }

  void resize(size_t new_size) {
    if (new_size > size_ && new_size > capacity_) {
      const auto new_capacity = new_size == 0 ? 0 : std::max(std::bit_ceil(new_size), size_t(64));
      const auto previous_backing = std::move(backing);

      backing = std::make_unique<uint8_t[]>(new_capacity);
      capacity_ = new_capacity;

      if (previous_backing) {
        std::memcpy(backing.get(), previous_backing.get(), size_);
      }
    }

    size_ = new_size;
  }

  std::span<uint8_t> grow(size_t amount) {
    if (amount == 0) {
      return {};
    } else {
      const auto previous_size = size();
      resize(previous_size + amount);
      return span().subspan(previous_size, amount);
    }
  }

  void shrink(size_t amount) {
    verify(amount <= size(), "shrinking above NetworkBuffer size");
    resize(size() - amount);
  }

  void append(std::span<const uint8_t> buffer) {
    if (!buffer.empty()) {
      const auto previous_size = size();
      resize(previous_size + buffer.size());
      std::memcpy(data() + previous_size, buffer.data(), buffer.size());
    }
  }
  void append(const void* buffer, size_t buffer_size) {
    return append(std::span(reinterpret_cast<const uint8_t*>(buffer), buffer_size));
  }

  void write_at_offset(size_t offset, std::span<const uint8_t> buffer) {
    if (!buffer.empty()) {
      verify(offset + buffer.size() <= size(), "write out of bounds");
      std::memcpy(data() + offset, buffer.data(), buffer.size());
    }
  }
  void write_at_offset(size_t offset, const void* buffer, size_t buffer_size) {
    return write_at_offset(offset,
                           std::span(reinterpret_cast<const uint8_t*>(buffer), buffer_size));
  }

  void strip_front(size_t strip_amount) {
    verify(strip_amount <= size(), "stripping above NetworkBuffer size");

    const auto new_size = size() - strip_amount;
    if (!empty()) {
      std::memmove(data(), data() + strip_amount, new_size);
    }
    resize(new_size);
  }

  void strip_back(size_t strip_amount) {
    verify(strip_amount <= size(), "stripping above NetworkBuffer size");

    const auto new_size = size() - strip_amount;
    resize(new_size);
  }
};
