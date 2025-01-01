#pragma once
#include <cstdint>
#include <span>
#include <string_view>

namespace net {

enum class PacketID {
  Invalid,
  ReceiverHello,
  SenderHello,
  Acknowledged,
  CreateDirectory,
  CreateFile,
  FileChunk,
  VerifyFile,
  Max,
};

namespace packets {

struct ReceiverHello {};
struct SenderHello {};

struct Acknowledged {
  bool accepted{};
};

struct CreateDirectory {
  std::string_view path;
};

struct CreateFile {
  constexpr static uint16_t flag_compressed = 1 << 0;

  std::string_view path;
  uint64_t size{};
  uint16_t flags{};
};

struct FileChunk {
  std::span<const uint8_t> data;
};

struct VerifyFile {
  uint64_t hash{};
};

}  // namespace packets

}  // namespace net