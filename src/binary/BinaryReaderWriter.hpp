#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

class BinaryWriter {
  std::vector<uint8_t>& buffer;

 public:
  explicit BinaryWriter(std::vector<uint8_t>& buffer);

  size_t written_size() const;

  void write_bytes(std::span<const uint8_t> bytes);

  void write_u8(uint8_t v);
  void write_u16(uint16_t v);
  void write_u32(uint32_t v);
  void write_u64(uint64_t v);

  void write_i8(int8_t v);
  void write_i16(int16_t v);
  void write_i32(int32_t v);
  void write_i64(int64_t v);
};

class BinaryReader {
  std::span<const uint8_t> buffer;

  template <size_t N>
  [[nodiscard]] bool read_bytes_array(std::array<uint8_t, N>& v);

 public:
  explicit BinaryReader(std::span<const uint8_t> buffer);

  size_t remaining_size() const;

  [[nodiscard]] bool read_bytes(size_t size, std::span<const uint8_t>& v);

  [[nodiscard]] bool read_u8(uint8_t& v);
  [[nodiscard]] bool read_u16(uint16_t& v);
  [[nodiscard]] bool read_u32(uint32_t& v);
  [[nodiscard]] bool read_u64(uint64_t& v);

  [[nodiscard]] bool read_i8(int8_t& v);
  [[nodiscard]] bool read_i16(int16_t& v);
  [[nodiscard]] bool read_i32(int32_t& v);
  [[nodiscard]] bool read_i64(int64_t& v);
};
