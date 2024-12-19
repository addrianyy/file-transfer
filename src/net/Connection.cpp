#include "Connection.hpp"

namespace net {

bool Connection::send_packet_internal(std::span<const uint8_t> data) {
  const auto [status, bytes_sent] = socket->send_all(data);
  if (!status) {
    if (status.has_code(sock::ErrorCode::Disconnected)) {
      disconnect();
    } else {
      error(ErrorType::SocketSendError, status);
    }
    return false;
  }
  return true;
}

void Connection::set_not_alive() {
  alive_ = false;
}

void Connection::error(ErrorType type, sock::Status status) {
  set_not_alive();
  on_error(type, status);
}

void Connection::disconnect() {
  set_not_alive();
  on_disconnected();
}

Connection::Connection(std::unique_ptr<sock::SocketStream> socket) : socket(std::move(socket)) {}

void Connection::update() {
  frame_receiver.receive([&](std::span<uint8_t> receive_buffer) {
    const auto [status, bytes_received] = socket->receive(receive_buffer);
    if (!status) {
      if (status.has_code(sock::ErrorCode::Disconnected)) {
        disconnect();
      } else {
        error(ErrorType::SocketReceiveError, status);
      }
    }

    return bytes_received;
  });

  while (alive_) {
    const auto [result, received_frame] = frame_receiver.update();
    if (result == framing::FrameReceiver::Result::ReceivedFrame) {
      on_packet_received(received_frame);
      frame_receiver.discard_frame();
    } else if (result == framing::FrameReceiver::Result::MalformedStream) {
      error(ErrorType::FramingSendError, {});
    } else if (result == framing::FrameReceiver::Result::NeedMoreData) {
      break;
    }
  }
}

}  // namespace net