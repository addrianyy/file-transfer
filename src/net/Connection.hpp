#pragma once
#include "Framing.hpp"

#include <socklib/Socket.hpp>

namespace net {

class Connection {
 public:
  enum class ErrorType {
    SocketSendError,
    SocketReceiveError,
    FramingSendError,
    FramingReceiveError,
  };

 private:
  sock::StreamSocket socket;

  framing::FrameReceiver frame_receiver;
  framing::FrameSender frame_sender;

  bool alive_ = true;

  bool send_packet_internal(std::span<const uint8_t> data);

 protected:
  template <typename Fn>
  bool send_packet(Fn&& calllback) {
    auto writer = frame_sender.prepare();
    if (!calllback(writer)) {
      return false;
    }
    const auto packet = frame_sender.finalize();
    if (packet.empty()) {
      error(ErrorType::FramingSendError, {});
      return false;
    }
    return send_packet_internal(packet);
  }

  void set_not_alive();

  void error(ErrorType type, sock::Status status);
  void disconnect();

  virtual void on_error(ErrorType type, sock::Status status) = 0;
  virtual void on_disconnected() = 0;
  virtual void on_packet_received(BinaryReader reader) = 0;

 public:
  explicit Connection(sock::StreamSocket socket);
  virtual ~Connection() = default;

  bool alive() const { return alive_; }

  void update();
};

}  // namespace net