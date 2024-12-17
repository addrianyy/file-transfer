#include "Framing.hpp"

#include <binary/BinaryReaderWriter.hpp>
#include <binary/PrimitiveConverter.hpp>

#include <cstring>

namespace net::framing {

constexpr uint32_t frame_header_magic = 0xf150ccc2u;
constexpr uint32_t frame_header_size = 8;

// 8 MB
constexpr uint32_t frame_max_size = 8 * 1024 * 1024;

void FrameReceiver::feed(std::span<const uint8_t> data) {
  if (!data.empty()) {
    const auto previous_size = buffer.size();
    buffer.resize(previous_size + data.size());
    std::memcpy(buffer.data() + previous_size, data.data(), data.size());
  }
}

std::pair<FrameReceiver::Result, BinaryReader> FrameReceiver::update() {
  if (pending_frame_size == invalid_size && buffer.size() >= frame_header_size) {
    BinaryReader reader{buffer};

    uint32_t magic{};
    if (!reader.read_u32(magic) || magic != frame_header_magic) {
      return {Result::MalformedStream, BinaryReader{{}}};
    }

    uint32_t frame_size{};
    if (!reader.read_u32(frame_size) || frame_size <= frame_header_size ||
        frame_size > frame_max_size) {
      return {Result::MalformedStream, BinaryReader{{}}};
    }

    pending_frame_size = frame_size;
  }

  if (pending_frame_size != invalid_size && buffer.size() >= pending_frame_size) {
    const auto frame =
      std::span(buffer).subspan(frame_header_size, pending_frame_size - frame_header_size);
    return {Result::ReceivedFrame, BinaryReader{frame}};
  }

  return {Result::NeedMoreData, BinaryReader{{}}};
}

void FrameReceiver::discard_frame() {
  if (pending_frame_size != invalid_size && buffer.size() >= pending_frame_size) {
    std::memmove(buffer.data(), buffer.data() + pending_frame_size,
                 buffer.size() - pending_frame_size);
    buffer.resize(buffer.size() - pending_frame_size);

    pending_frame_size = invalid_size;
  }
}

BinaryWriter FrameSender::prepare() {
  buffer.clear();

  BinaryWriter writer{buffer};
  writer.write_u32(frame_header_magic);
  writer.write_u32(std::numeric_limits<uint32_t>::max());

  return writer;
}

std::span<const uint8_t> FrameSender::finalize() {
  const auto frame_size = buffer.size();
  if (frame_size <= frame_header_size || frame_size > frame_max_size) {
    return {};
  }

  const auto frame_size_bytes = PrimitiveConverter::u32_to_bytes(frame_size);
  static_assert(frame_size_bytes.size() == 4, "frame_size_bytes has invalid size");

  std::memcpy(buffer.data() + 4, frame_size_bytes.data(), frame_size_bytes.size());

  return buffer;
}

}  // namespace net::framing