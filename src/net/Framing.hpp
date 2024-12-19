#pragma once
#include <cstdint>
#include <limits>
#include <span>
#include <utility>
#include <vector>

#include <binary/BinaryReaderWriter.hpp>

namespace net::framing {

class FrameReceiver {
  constexpr static uint32_t invalid_size = std::numeric_limits<uint32_t>::max();

  std::vector<uint8_t> buffer;
  uint32_t used_size{};

  uint32_t receive_buffer_size{16 * 1024};
  uint32_t pending_frame_size{invalid_size};

  std::span<uint8_t> prepare_receive_buffer();
  void commit_received_data(size_t size);

 public:
  enum class Result {
    MalformedStream,
    NeedMoreData,
    ReceivedFrame,
  };

  template <typename Callback>
  void receive(Callback&& callback) {
    const auto buffer = prepare_receive_buffer();
    const auto size = callback(buffer);
    if (size > 0) {
      commit_received_data(size);
    }
  }

  std::pair<Result, BinaryReader> update();
  void discard_frame();
};

class FrameSender {
  std::vector<uint8_t> buffer;

 public:
  BinaryWriter prepare();
  std::span<const uint8_t> finalize();
};

}  // namespace net::framing
