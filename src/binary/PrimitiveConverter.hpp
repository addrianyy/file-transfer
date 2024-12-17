#pragma once
#include <array>
#include <cstdint>

class PrimitiveConverter {
 public:
  static std::array<uint8_t, 1> u8_to_bytes(uint8_t v) { return {v}; }
  static std::array<uint8_t, 2> u16_to_bytes(uint16_t v) {
    return {uint8_t(v >> 8), uint8_t(v >> 0)};
  }
  static std::array<uint8_t, 4> u32_to_bytes(uint32_t v) {
    return {uint8_t(v >> 24), uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v >> 0)};
  }
  static std::array<uint8_t, 8> u64_to_bytes(uint64_t v) {
    return {uint8_t(v >> 56), uint8_t(v >> 48), uint8_t(v >> 40), uint8_t(v >> 32),
            uint8_t(v >> 24), uint8_t(v >> 16), uint8_t(v >> 8),  uint8_t(v >> 0)};
  }
  static std::array<uint8_t, 1> i8_to_bytes(int8_t v) { return u8_to_bytes(uint8_t(v)); }
  static std::array<uint8_t, 2> i16_to_bytes(int16_t v) { return u16_to_bytes(uint16_t(v)); }
  static std::array<uint8_t, 4> i32_to_bytes(int32_t v) { return u32_to_bytes(uint32_t(v)); }
  static std::array<uint8_t, 8> i64_to_bytes(int64_t v) { return u64_to_bytes(uint64_t(v)); }

  static uint8_t bytes_to_u8(const std::array<uint8_t, 1>& v) { return uint8_t(v[0]); }
  static uint16_t bytes_to_u16(const std::array<uint8_t, 2>& v) {
    return uint16_t((uint16_t(v[0]) << 8) | (uint16_t(v[1]) << 0));
  }
  static uint32_t bytes_to_u32(const std::array<uint8_t, 4>& v) {
    return uint32_t((uint32_t(v[0]) << 24) | (uint32_t(v[1]) << 16) | (uint32_t(v[2]) << 8) |
                    (uint32_t(v[3]) << 0));
  }
  static uint64_t bytes_to_u64(const std::array<uint8_t, 8>& v) {
    return uint64_t((uint64_t(v[0]) << 56) | (uint64_t(v[1]) << 48) | (uint64_t(v[2]) << 40) |
                    (uint64_t(v[3]) << 32) | (uint64_t(v[4]) << 24) | (uint64_t(v[5]) << 16) |
                    (uint64_t(v[6]) << 8) | (uint64_t(v[7]) << 0));
  }
  static int8_t bytes_to_i8(const std::array<uint8_t, 1>& v) { return int8_t(bytes_to_u8(v)); }
  static int16_t bytes_to_i16(const std::array<uint8_t, 2>& v) { return int16_t(bytes_to_u16(v)); }
  static int32_t bytes_to_i32(const std::array<uint8_t, 4>& v) { return int32_t(bytes_to_u32(v)); }
  static int64_t bytes_to_i64(const std::array<uint8_t, 8>& v) { return int64_t(bytes_to_u64(v)); }
};