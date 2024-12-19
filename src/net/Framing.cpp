#include "Framing.hpp"

#include <binary/BinaryReaderWriter.hpp>
#include <binary/PrimitiveConverter.hpp>

#include <algorithm>
#include <cstring>

#include <base/Panic.hpp>

namespace net::framing {

constexpr uint32_t frame_header_magic = 0xf150ccc2u;
constexpr uint32_t frame_header_size = 8;

// 8 MB
constexpr uint32_t frame_max_size = 8 * 1024 * 1024;

std::span<uint8_t> FrameReceiver::prepare_receive_buffer() {
  const auto remaining_size = buffer.size() - used_size;
  if (remaining_size < receive_buffer_size) {
    const auto missing_size = receive_buffer_size - remaining_size;
    buffer.resize(buffer.size() + missing_size);
  }
  return std::span(buffer).subspan(used_size, receive_buffer_size);
}

void FrameReceiver::commit_received_data(size_t size) {
  used_size += size;
  verify(used_size <= buffer.size(), "out of bounds receive");
}

std::pair<FrameReceiver::Result, BinaryReader> FrameReceiver::update() {
  const auto received_data = std::span(buffer).subspan(0, used_size);

  if (pending_frame_size == invalid_size && received_data.size() >= frame_header_size) {
    BinaryReader reader{received_data};

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

    // Grow the receive buffer so it fits all frames.
    receive_buffer_size = std::max(receive_buffer_size, frame_size);
  }

  if (pending_frame_size != invalid_size && received_data.size() >= pending_frame_size) {
    const auto frame =
      received_data.subspan(frame_header_size, pending_frame_size - frame_header_size);
    return {Result::ReceivedFrame, BinaryReader{frame}};
  }

  return {Result::NeedMoreData, BinaryReader{{}}};
}

void FrameReceiver::discard_frame() {
  const auto received_data = std::span(buffer).subspan(0, used_size);

  if (pending_frame_size != invalid_size && received_data.size() >= pending_frame_size) {
    const auto frame_size = pending_frame_size;
    const auto leftover_size = received_data.size() - frame_size;

    std::memmove(received_data.data(), received_data.data() + frame_size, leftover_size);
    used_size = leftover_size;
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