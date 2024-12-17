#include "BinaryReaderWriter.hpp"
#include "PrimitiveConverter.hpp"

#include <cstring>

BinaryWriter::BinaryWriter(std::vector<uint8_t>& buffer) : buffer(buffer) {}

size_t BinaryWriter::written_size() const {
  return buffer.size();
}

void BinaryWriter::write_bytes(std::span<const uint8_t> bytes) {
  if (!bytes.empty()) {
    const auto previous_size = buffer.size();
    buffer.resize(previous_size + bytes.size());
    std::memcpy(buffer.data() + previous_size, bytes.data(), bytes.size());
  }
}

void BinaryWriter::write_u8(uint8_t v) {
  write_bytes(PrimitiveConverter::u8_to_bytes(v));
}
void BinaryWriter::write_u16(uint16_t v) {
  write_bytes(PrimitiveConverter::u16_to_bytes(v));
}
void BinaryWriter::write_u32(uint32_t v) {
  write_bytes(PrimitiveConverter::u32_to_bytes(v));
}
void BinaryWriter::write_u64(uint64_t v) {
  write_bytes(PrimitiveConverter::u64_to_bytes(v));
}

void BinaryWriter::write_i8(int8_t v) {
  write_bytes(PrimitiveConverter::i8_to_bytes(v));
}
void BinaryWriter::write_i16(int16_t v) {
  write_bytes(PrimitiveConverter::i16_to_bytes(v));
}
void BinaryWriter::write_i32(int32_t v) {
  write_bytes(PrimitiveConverter::i32_to_bytes(v));
}
void BinaryWriter::write_i64(int64_t v) {
  write_bytes(PrimitiveConverter::i64_to_bytes(v));
}

BinaryReader::BinaryReader(std::span<const uint8_t> buffer) : buffer(buffer) {}

size_t BinaryReader::remaining_size() const {
  return buffer.size();
}

template <size_t N>
bool BinaryReader::read_bytes_array(std::array<uint8_t, N>& v) {
  if (buffer.size() < N) {
    return false;
  }
  std::memcpy(v.data(), buffer.data(), N);
  buffer = buffer.subspan(N);
  return true;
}

bool BinaryReader::read_bytes(size_t size, std::span<const uint8_t>& v) {
  if (buffer.size() < size) {
    return false;
  }
  v = buffer.subspan(0, size);
  buffer = buffer.subspan(size);
  return true;
}

#define READ_PRIMITIVE_HELPER(size, type)         \
  std::array<uint8_t, size> bytes{};              \
  if (!read_bytes_array(bytes)) {                 \
    return false;                                 \
  }                                               \
  v = PrimitiveConverter::bytes_to_##type(bytes); \
  return true;

bool BinaryReader::read_u8(uint8_t& v) {
  READ_PRIMITIVE_HELPER(1, u8)
}
bool BinaryReader::read_u16(uint16_t& v) {
  READ_PRIMITIVE_HELPER(2, u16)
}
bool BinaryReader::read_u32(uint32_t& v) {
  READ_PRIMITIVE_HELPER(4, u32)
}
bool BinaryReader::read_u64(uint64_t& v) {
  READ_PRIMITIVE_HELPER(8, u64)
}

bool BinaryReader::read_i8(int8_t& v) {
  READ_PRIMITIVE_HELPER(1, i8)
}
bool BinaryReader::read_i16(int16_t& v) {
  READ_PRIMITIVE_HELPER(2, i16)
}
bool BinaryReader::read_i32(int32_t& v) {
  READ_PRIMITIVE_HELPER(4, i32)
}
bool BinaryReader::read_i64(int64_t& v) {
  READ_PRIMITIVE_HELPER(8, i64)
}

#undef READ_PRIMITIVE_HELPER