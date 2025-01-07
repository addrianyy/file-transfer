#include "BinaryBuffer.hpp"

#include <base/Panic.hpp>

#include <algorithm>
#include <bit>
#include <cstring>

namespace base {

BinaryBuffer::BinaryBuffer(std::span<const uint8_t> data) {
  append(data);
}

BinaryBuffer::BinaryBuffer(const void* data, size_t size)
    : BinaryBuffer(std::span(reinterpret_cast<const uint8_t*>(data), size)) {}

BinaryBuffer::BinaryBuffer(BinaryBuffer&& other) noexcept {
  buffer = std::move(other.buffer);
  size_ = other.size_;
  capacity_ = other.capacity_;

  other.clear_and_deallocate();
}

BinaryBuffer& BinaryBuffer::operator=(BinaryBuffer&& other) noexcept {
  if (this != &other) {
    buffer = std::move(other.buffer);
    size_ = other.size_;
    capacity_ = other.capacity_;

    other.clear_and_deallocate();
  }

  return *this;
}

std::vector<uint8_t> BinaryBuffer::to_vector() const {
  std::vector<uint8_t> result;
  if (!empty()) {
    result.resize(size_);
    std::memcpy(result.data(), data(), size_);
  }
  return result;
}

void BinaryBuffer::clear() {
  resize(0);
}

void BinaryBuffer::clear_and_deallocate() {
  buffer = nullptr;
  size_ = 0;
  capacity_ = 0;
}

void BinaryBuffer::resize(size_t new_size) {
  if (new_size > capacity_) {
    const auto new_capacity = std::max(std::bit_ceil(new_size), size_t(16));
    const auto previous_buffer = std::move(buffer);

    buffer = std::make_unique<uint8_t[]>(new_capacity);
    capacity_ = new_capacity;

    if (previous_buffer) {
      std::memcpy(buffer.get(), previous_buffer.get(), size_);
    }
  }

  size_ = new_size;
}

void BinaryBuffer::resize_and_zero(size_t new_size) {
  if (new_size > size_) {
    const size_t zero_offset = size_;
    const size_t zero_size = std::min(new_size - size_, unused_capacity());
    resize(new_size);
    std::memset(buffer.get() + zero_offset, 0, zero_size);
  } else {
    resize(new_size);
  }
}

std::span<uint8_t> BinaryBuffer::grow(size_t amount) {
  if (amount == 0) {
    return {};
  } else {
    const auto previous_size = size();
    resize(previous_size + amount);
    return span().subspan(previous_size, amount);
  }
}

std::span<uint8_t> BinaryBuffer::grow_and_zero(size_t amount) {
  if (amount == 0) {
    return {};
  } else {
    const auto previous_size = size();
    resize_and_zero(previous_size + amount);
    return span().subspan(previous_size, amount);
  }
}

void BinaryBuffer::shrink(size_t amount) {
  verify(amount <= size(), "shrinking above BinaryBuffer size");
  resize(size() - amount);
}

void BinaryBuffer::append(std::span<const uint8_t> buffer) {
  if (!buffer.empty()) {
    const auto previous_size = size();
    resize(previous_size + buffer.size());
    std::memcpy(data() + previous_size, buffer.data(), buffer.size());
  }
}
void BinaryBuffer::append(const void* buffer, size_t buffer_size) {
  return append(std::span(reinterpret_cast<const uint8_t*>(buffer), buffer_size));
}

void BinaryBuffer::write_at_offset(size_t offset, std::span<const uint8_t> buffer) {
  if (!buffer.empty()) {
    verify(offset + buffer.size() <= size(), "write out of bounds of BinaryBuffer");
    std::memcpy(data() + offset, buffer.data(), buffer.size());
  }
}

void BinaryBuffer::write_at_offset(size_t offset, const void* buffer, size_t buffer_size) {
  return write_at_offset(offset, std::span(reinterpret_cast<const uint8_t*>(buffer), buffer_size));
}
void BinaryBuffer::trim_front(size_t amount) {
  if (amount > 0) {
    verify(amount <= size(), "trimming above BinaryBuffer size");

    const auto new_size = size() - amount;
    if (!empty()) {
      std::memmove(data(), data() + amount, new_size);
    }
    resize(new_size);
  }
}

void BinaryBuffer::trim_back(size_t amount) {
  shrink(amount);
}

}  // namespace base