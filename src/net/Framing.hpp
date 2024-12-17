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
  uint32_t pending_frame_size{invalid_size};

 public:
  enum class Result {
    MalformedStream,
    NeedMoreData,
    ReceivedFrame,
  };

  void feed(std::span<const uint8_t> data);
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
